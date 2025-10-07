import { generateUUID } from '../utils/uuid.js';
import { withTransaction } from '../database/connection.js';
import { PoolManager } from '../database/poolManager.js';

export const createOptimizedQueueManager = (pool, resourceCache, eventManager) => {
  
  // Batch size for database operations
  const BATCH_INSERT_SIZE = 1000;
  
  // Create pool manager for better connection handling
  const poolManager = new PoolManager({
    max: pool.options?.max || parseInt(process.env.DB_POOL_SIZE) || 20
  });
  
  // Ensure namespace, task, and queue exist (with caching)
  const ensureResources = async (client, ns, task, queue) => {
    // Check cache first
    const cacheKey = `${ns}:${task}:${queue}`;
    const cached = resourceCache.checkResource(ns, task, queue);
    if (cached) return cached;
    
    // Insert or get namespace
    const nsResult = await client.query(
      `INSERT INTO queen.namespaces (name) VALUES ($1) 
       ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name 
       RETURNING id`,
      [ns]
    );
    const namespaceId = nsResult.rows[0].id;
    
    // Insert or get task
    const taskResult = await client.query(
      `INSERT INTO queen.tasks (namespace_id, name) VALUES ($1, $2) 
       ON CONFLICT (namespace_id, name) DO UPDATE SET name = EXCLUDED.name 
       RETURNING id`,
      [namespaceId, task]
    );
    const taskId = taskResult.rows[0].id;
    
    // Insert or get queue
    const queueResult = await client.query(
      `INSERT INTO queen.queues (task_id, name) VALUES ($1, $2) 
       ON CONFLICT (task_id, name) DO UPDATE SET name = EXCLUDED.name 
       RETURNING id, options`,
      [taskId, queue]
    );
    
    const result = {
      namespaceId,
      taskId,
      queueId: queueResult.rows[0].id,
      queueOptions: queueResult.rows[0].options
    };
    
    // Cache the result
    resourceCache.cacheResource(ns, task, queue, result);
    
    return result;
  };
  
  // Optimized batch insert for high throughput
  const pushMessagesBatch = async (items) => {
    // Group items by queue for efficient resource lookup
    const queueGroups = {};
    for (const item of items) {
      const key = `${item.ns}:${item.task}:${item.queue}`;
      if (!queueGroups[key]) {
        queueGroups[key] = [];
      }
      queueGroups[key].push(item);
    }
    
    const allResults = [];
    
    // Process each queue group
    for (const [queueKey, queueItems] of Object.entries(queueGroups)) {
      const [ns, task, queue] = queueKey.split(':');
      
      // Use pool manager for better connection handling
      const results = await poolManager.withClient(async (client) => {
        // Ensure resources exist (cached after first call)
        const resources = await ensureResources(client, ns, task, queue);
        const batchResults = [];
        
        // Process in batches to avoid overwhelming the database
        for (let i = 0; i < queueItems.length; i += BATCH_INSERT_SIZE) {
          const batch = queueItems.slice(i, i + BATCH_INSERT_SIZE);
          
          // Generate transaction IDs
          const transactionIds = batch.map(item => item.transactionId || generateUUID());
          
          // Check for duplicates (quick check, non-blocking)
          const existingCheck = await client.query(
            `SELECT transaction_id FROM queen.messages 
             WHERE transaction_id = ANY($1::uuid[])`,
            [transactionIds]
          );
          
          const existingIds = new Set(existingCheck.rows.map(r => r.transaction_id));
          
          // Prepare batch insert data
          const newItems = [];
          const duplicates = [];
          
          batch.forEach((item, index) => {
            const txId = transactionIds[index];
            if (existingIds.has(txId)) {
              duplicates.push({
                transactionId: txId,
                status: 'duplicate'
              });
            } else {
              newItems.push({
                ...item,
                transactionId: txId,
                queueId: resources.queueId
              });
            }
          });
          
          // Perform batch insert if there are new items
          if (newItems.length > 0) {
            // Build VALUES clause for batch insert
            const values = [];
            const params = [];
            let paramIndex = 1;
            
            newItems.forEach(item => {
              values.push(`($${paramIndex}, $${paramIndex + 1}, $${paramIndex + 2}, 'pending')`);
              params.push(item.transactionId, item.queueId, JSON.stringify(item.payload));
              paramIndex += 3;
            });
            
            const insertQuery = `
              INSERT INTO queen.messages (transaction_id, queue_id, payload, status)
              VALUES ${values.join(', ')}
              ON CONFLICT (transaction_id) DO NOTHING
              RETURNING id, transaction_id
            `;
            
            const insertResult = await client.query(insertQuery, params);
            
            insertResult.rows.forEach(row => {
              batchResults.push({
                id: row.id,
                transactionId: row.transaction_id,
                status: 'queued'
              });
            });
            
            // Add any items that weren't inserted due to conflict
            const insertedTxIds = new Set(insertResult.rows.map(r => r.transaction_id));
            newItems.forEach(item => {
              if (!insertedTxIds.has(item.transactionId)) {
                batchResults.push({
                  transactionId: item.transactionId,
                  status: 'duplicate'
                });
              }
            });
          }
          
          // Add duplicates to results
          batchResults.push(...duplicates);
        }
        
        // Notify event manager about new messages
        const queuePath = `${ns}/${task}/${queue}`;
        eventManager.notifyMessageAvailable(queuePath);
        
        return batchResults;
      });
      
      allResults.push(...results);
    }
    
    return allResults;
  };
  
  // Original push method for backward compatibility
  const pushMessages = async (items) => {
    // Use batch method for better performance
    return pushMessagesBatch(items);
  };
  
  // Pop messages from queue with optional scope
  const popMessages = async (scope, options = {}) => {
    const { ns, task, queue } = scope;
    const { wait = false, timeout = 30000, batch = 1 } = options;
    
    return withTransaction(pool, async (client) => {
      // Debug: Log the query being executed
      console.log('Pop query:', { ns, task, queue, batch, wait });
      
      let result;
      let queueInfo = {};
      
      if (queue) {
        // Specific queue: use a simpler, more efficient query
        result = await client.query(`
          WITH queue_info AS (
            SELECT q.id, q.name as queue_name, q.options, q.priority,
                   t.name as task_name, n.name as ns_name
            FROM queen.queues q
            JOIN queen.tasks t ON q.task_id = t.id
            JOIN queen.namespaces n ON t.namespace_id = n.id
            WHERE n.name = $1 AND t.name = $2 AND q.name = $3
            LIMIT 1
          )
          SELECT m.*, qi.queue_name, qi.task_name, qi.ns_name, qi.options, qi.priority
          FROM queen.messages m
          JOIN queue_info qi ON m.queue_id = qi.id
          WHERE m.status = 'pending'
          ORDER BY m.created_at ASC
          LIMIT $4
          FOR UPDATE OF m SKIP LOCKED
        `, [ns, task, queue, batch]);
      } else if (task) {
        // Task level: get from any queue in the task
        result = await client.query(`
          SELECT m.*, q.name as queue_name, t.name as task_name, n.name as ns_name, 
                 q.options, q.priority
          FROM queen.messages m
          JOIN queen.queues q ON m.queue_id = q.id
          JOIN queen.tasks t ON q.task_id = t.id
          JOIN queen.namespaces n ON t.namespace_id = n.id
          WHERE n.name = $1 AND t.name = $2
            AND m.status = 'pending'
          ORDER BY q.priority DESC, m.created_at ASC
          LIMIT $3
          FOR UPDATE OF m SKIP LOCKED
        `, [ns, task, batch]);
      } else {
        // Namespace level: get from any queue in the namespace
        result = await client.query(`
          SELECT m.*, q.name as queue_name, t.name as task_name, n.name as ns_name,
                 q.options, q.priority
          FROM queen.messages m
          JOIN queen.queues q ON m.queue_id = q.id
          JOIN queen.tasks t ON q.task_id = t.id
          JOIN queen.namespaces n ON t.namespace_id = n.id
          WHERE n.name = $1
            AND m.status = 'pending'
          ORDER BY q.priority DESC, m.created_at ASC
          LIMIT $2
          FOR UPDATE OF m SKIP LOCKED
        `, [ns, batch]);
      }
      
      console.log('Pop query result:', result.rows.length, 'messages found');
      
      if (result.rows.length === 0 && wait) {
        // Release the transaction and wait for messages
        await client.query('COMMIT');
        
        const queuePath = queue ? `${ns}/${task}/${queue}` : task ? `${ns}/${task}` : ns;
        const notification = await eventManager.waitForMessage(queuePath, timeout);
        
        if (!notification) {
          return { messages: [] }; // Timeout
        }
        
        // Try again after notification
        await client.query('BEGIN');
        const retryResult = await client.query(query, params);
        
        if (retryResult.rows.length === 0) {
          return { messages: [] };
        }
        
        result.rows = retryResult.rows;
      }
      
      if (result.rows.length === 0) {
        return { messages: [] };
      }
      
      // Update status to processing
      const messageIds = result.rows.map(r => r.id);
      console.log('Updating message IDs to processing:', messageIds.length);
      
      const leaseTime = 300; // Default 5 minutes lease
      const updateResult = await client.query(
        `UPDATE queen.messages 
         SET status = 'processing', 
             locked_at = NOW(),
             lease_expires_at = NOW() + INTERVAL '${leaseTime} seconds',
             retry_count = retry_count + 1
         WHERE id = ANY($1::uuid[])`,
        [messageIds]
      );
      console.log('Update result:', updateResult.rowCount, 'rows updated');
      
      // Format messages - handle both cases where we have metadata and where we don't
      const messages = result.rows.map(row => ({
        id: row.id,
        transactionId: row.transaction_id,
        ns: row.ns_name || ns,
        task: row.task_name || task,
        queue: row.queue_name || queue,
        data: row.payload,
        retryCount: row.retry_count || 0,
        priority: row.priority || 0,
        createdAt: row.created_at,
        lockedAt: row.locked_at,
        options: row.options || {}
      }));
      
      console.log('Returning', messages.length, 'formatted messages');
      return { messages };
    });
  };
  
  // Acknowledge message completion
  const acknowledgeMessage = async (transactionId, status = 'completed', error = null) => {
    const validStatuses = ['completed', 'failed'];
    if (!validStatuses.includes(status)) {
      throw new Error(`Invalid status: ${status}`);
    }
    
    return withTransaction(pool, async (client) => {
      const updateQuery = status === 'completed' 
        ? `UPDATE queen.messages 
           SET status = $1, 
               completed_at = NOW(),
               error_message = $2
           WHERE transaction_id = $3
           RETURNING id, queue_id, retry_count`
        : `UPDATE queen.messages 
           SET status = $1, 
               failed_at = NOW(),
               error_message = $2
           WHERE transaction_id = $3
           RETURNING id, queue_id, retry_count`;
           
      const result = await client.query(updateQuery, [status, error, transactionId]);
      
      if (result.rows.length === 0) {
        throw new Error(`Message not found: ${transactionId}`);
      }
      
      // Handle retry logic for failed messages
      if (status === 'failed') {
        const message = await client.query(
          `SELECT m.*, q.options 
           FROM queen.messages m
           JOIN queen.queues q ON m.queue_id = q.id
           WHERE m.id = $1`,
          [result.rows[0].id]
        );
        
        const options = message.rows[0].options || {};
        const maxRetries = options.retryLimit || 3;
        const currentRetryCount = result.rows[0].retry_count;
        
        if (currentRetryCount < maxRetries) {
          // Reset to pending for retry
          await client.query(
            `UPDATE queen.messages 
             SET status = 'pending',
                 locked_at = NULL,
                 lease_expires_at = NULL,
                 error_message = NULL
             WHERE id = $1`,
            [result.rows[0].id]
          );
          
          return { status: 'retry_scheduled', retryCount: currentRetryCount };
        } else if (options.dlqAfterMaxRetries) {
          // Move to DLQ
          await client.query(
            `UPDATE queen.messages 
             SET status = 'dlq'
             WHERE id = $1`,
            [result.rows[0].id]
          );
          
          return { status: 'moved_to_dlq' };
        }
      }
      
      return { status };
    });
  };
  
  // Batch acknowledge messages
  const acknowledgeBatch = async (acknowledgments) => {
    const results = [];
    
    // Group by status for efficient processing
    const grouped = {
      completed: [],
      failed: []
    };
    
    acknowledgments.forEach(ack => {
      if (grouped[ack.status]) {
        grouped[ack.status].push(ack);
      }
    });
    
    await withTransaction(pool, async (client) => {
      // Process completed messages
      if (grouped.completed.length > 0) {
        const ids = grouped.completed.map(a => a.transactionId);
        await client.query(
          `UPDATE queen.messages 
           SET status = 'completed',
               completed_at = NOW()
           WHERE transaction_id = ANY($1::uuid[])`,
          [ids]
        );
        
        grouped.completed.forEach(ack => {
          results.push({ transactionId: ack.transactionId, status: 'completed' });
        });
      }
      
      // Process failed messages (needs individual handling for retry logic)
      for (const ack of grouped.failed) {
        const result = await acknowledgeMessage(ack.transactionId, 'failed', ack.error);
        results.push({ transactionId: ack.transactionId, ...result });
      }
    });
    
    return results;
  };
  
  // Configure queue options
  const configureQueue = async (ns, task, queue, options) => {
    return withTransaction(pool, async (client) => {
      const resources = await ensureResources(client, ns, task, queue);
      
      await client.query(
        `UPDATE queen.queues 
         SET options = $1
         WHERE id = $2`,
        [JSON.stringify(options), resources.queueId]
      );
      
      // Invalidate cache
      resourceCache.invalidate(ns, task, queue);
      
      return { ns, task, queue, options };
    });
  };
  
  // Get queue statistics
  const getQueueStats = async (scope = {}) => {
    const { ns, task, queue } = scope;
    
    let query = `
      SELECT 
        n.name as namespace,
        t.name as task,
        q.name as queue,
        COUNT(CASE WHEN m.status = 'pending' THEN 1 END) as pending,
        COUNT(CASE WHEN m.status = 'processing' THEN 1 END) as processing,
        COUNT(CASE WHEN m.status = 'completed' THEN 1 END) as completed,
        COUNT(CASE WHEN m.status = 'failed' THEN 1 END) as failed,
        COUNT(CASE WHEN m.status = 'dlq' THEN 1 END) as dlq,
        COUNT(*) as total
      FROM queen.namespaces n
      LEFT JOIN queen.tasks t ON t.namespace_id = n.id
      LEFT JOIN queen.queues q ON q.task_id = t.id
      LEFT JOIN queen.messages m ON m.queue_id = q.id
    `;
    
    const conditions = [];
    const params = [];
    
    if (ns) {
      conditions.push(`n.name = $${params.length + 1}`);
      params.push(ns);
    }
    if (task) {
      conditions.push(`t.name = $${params.length + 1}`);
      params.push(task);
    }
    if (queue) {
      conditions.push(`q.name = $${params.length + 1}`);
      params.push(queue);
    }
    
    if (conditions.length > 0) {
      query += ` WHERE ${conditions.join(' AND ')}`;
    }
    
    query += ` GROUP BY n.name, t.name, q.name ORDER BY n.name, t.name, q.name`;
    
    const result = await pool.query(query, params);
    
    // Transform the flat structure into the expected nested format
    return result.rows.map(row => ({
      namespace: row.namespace,
      task: row.task,
      queue: row.queue,
      stats: {
        pending: parseInt(row.pending) || 0,
        processing: parseInt(row.processing) || 0,
        completed: parseInt(row.completed) || 0,
        failed: parseInt(row.failed) || 0,
        dlq: parseInt(row.dlq) || 0,
        total: parseInt(row.total) || 0
      }
    }));
  };
  
  // Reclaim expired leases
  const reclaimExpiredLeases = async () => {
    const result = await pool.query(
      `UPDATE queen.messages 
       SET status = 'pending',
           locked_at = NULL,
           lease_expires_at = NULL,
           worker_id = NULL
       WHERE status = 'processing' 
         AND lease_expires_at < NOW()
       RETURNING id`
    );
    
    return result.rowCount;
  };
  
  // Acknowledge multiple messages (batch)
  const acknowledgeMessages = async (acknowledgments) => {
    return acknowledgeBatch(acknowledgments);
  };
  
  return {
    pushMessages,
    pushMessagesBatch,
    popMessages,
    acknowledgeMessage,
    acknowledgeMessages,
    acknowledgeBatch,
    configureQueue,
    getQueueStats,
    reclaimExpiredLeases
  };
};
