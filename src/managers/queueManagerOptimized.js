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
  
  // Ensure queue and partition exist (with caching)
  const ensureResources = async (client, queueName, partitionName = 'Default', namespace = null, task = null) => {
    // Check cache first
    const cacheKey = `${queueName}:${partitionName}`;
    const cached = resourceCache.checkResource(queueName, partitionName);
    if (cached) return cached;
    
    // Insert or get queue
    const queueResult = await client.query(
      `INSERT INTO queen.queues (name, namespace, task) VALUES ($1, $2, $3) 
       ON CONFLICT (name) DO UPDATE SET 
         namespace = COALESCE(EXCLUDED.namespace, queen.queues.namespace),
         task = COALESCE(EXCLUDED.task, queen.queues.task)
       RETURNING id`,
      [queueName, namespace, task]
    );
    const queueId = queueResult.rows[0].id;
    
    // Insert or get partition
    const partitionResult = await client.query(
      `INSERT INTO queen.partitions (queue_id, name) VALUES ($1, $2) 
       ON CONFLICT (queue_id, name) DO UPDATE SET name = EXCLUDED.name 
       RETURNING id, options`,
      [queueId, partitionName]
    );
    
    const result = {
      queueId,
      partitionId: partitionResult.rows[0].id,
      partitionOptions: partitionResult.rows[0].options
    };
    
    // Cache the result
    resourceCache.cacheResource(queueName, partitionName, result);
    
    return result;
  };
  
  // Optimized batch insert for high throughput
  const pushMessagesBatch = async (items) => {
    // Group items by queue and partition for efficient resource lookup
    const partitionGroups = {};
    for (const item of items) {
      const partition = item.partition ?? 'Default';
      const key = `${item.queue}:${partition}`;
      if (!partitionGroups[key]) {
        partitionGroups[key] = [];
      }
      partitionGroups[key].push(item);
    }
    
    const allResults = [];
    
    // Process each partition group
    for (const [partitionKey, partitionItems] of Object.entries(partitionGroups)) {
      const [queueName, partitionName] = partitionKey.split(':');
      
      // Use pool manager for better connection handling
      const results = await poolManager.withClient(async (client) => {
        // Ensure resources exist (cached after first call)
        const resources = await ensureResources(client, queueName, partitionName);
        const batchResults = [];
        
        // Process in batches to avoid overwhelming the database
        for (let i = 0; i < partitionItems.length; i += BATCH_INSERT_SIZE) {
          const batch = partitionItems.slice(i, i + BATCH_INSERT_SIZE);
          
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
                partitionId: resources.partitionId
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
              params.push(item.transactionId, item.partitionId, JSON.stringify(item.payload));
              paramIndex += 3;
            });
            
            const insertQuery = `
              INSERT INTO queen.messages (transaction_id, partition_id, payload, status)
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
        const queuePath = `${queueName}/${partitionName}`;
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
  
  // Pop messages from queue with optional partition
  const popMessages = async (scope, options = {}) => {
    const { queue, partition } = scope;
    const { wait = false, timeout = 30000, batch = 1 } = options;
    
    return withTransaction(pool, async (client) => {
      let result;
      
      if (partition) {
        // Specific partition: use a simpler, more efficient query
        result = await client.query(`
          WITH partition_info AS (
            SELECT p.id, p.name as partition_name, p.options, p.priority,
                   q.name as queue_name
            FROM queen.partitions p
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
            LIMIT 1
          )
          SELECT m.*, pi.partition_name, pi.queue_name, pi.options, pi.priority
          FROM queen.messages m
          JOIN partition_info pi ON m.partition_id = pi.id
          WHERE m.status = 'pending'
          ORDER BY m.created_at ASC
          LIMIT $3
          FOR UPDATE OF m SKIP LOCKED
        `, [queue, partition, batch]);
      } else {
        // Queue level: get from any partition in the queue (oldest first across all partitions)
        result = await client.query(`
          SELECT m.*, p.name as partition_name, q.name as queue_name, 
                 p.options, p.priority
          FROM queen.messages m
          JOIN queen.partitions p ON m.partition_id = p.id
          JOIN queen.queues q ON p.queue_id = q.id
          WHERE q.name = $1
            AND m.status = 'pending'
          ORDER BY m.created_at ASC
          LIMIT $2
          FOR UPDATE OF m SKIP LOCKED
        `, [queue, batch]);
      }
      
      if (result.rows.length === 0 && wait) {
        // Release the transaction and wait for messages
        await client.query('COMMIT');
        
        const queuePath = partition ? `${queue}/${partition}` : queue;
        const notification = await eventManager.waitForMessage(queuePath, timeout);
        
        if (!notification) {
          return { messages: [] }; // Timeout
        }
        
        // Try again after notification
        await client.query('BEGIN');
        if (partition) {
          result = await client.query(`
            WITH partition_info AS (
              SELECT p.id, p.name as partition_name, p.options, p.priority,
                     q.name as queue_name
              FROM queen.partitions p
              JOIN queen.queues q ON p.queue_id = q.id
              WHERE q.name = $1 AND p.name = $2
              LIMIT 1
            )
            SELECT m.*, pi.partition_name, pi.queue_name, pi.options, pi.priority
            FROM queen.messages m
            JOIN partition_info pi ON m.partition_id = pi.id
            WHERE m.status = 'pending'
            ORDER BY m.created_at ASC
            LIMIT $3
            FOR UPDATE OF m SKIP LOCKED
          `, [queue, partition, batch]);
        } else {
          result = await client.query(`
            SELECT m.*, p.name as partition_name, q.name as queue_name,
                   p.options, p.priority
            FROM queen.messages m
            JOIN queen.partitions p ON m.partition_id = p.id
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1
              AND m.status = 'pending'
            ORDER BY m.created_at ASC
            LIMIT $2
            FOR UPDATE OF m SKIP LOCKED
          `, [queue, batch]);
        }
      }
      
      if (result.rows.length === 0) {
        return { messages: [] };
      }
      
      // Update status to processing
      const messageIds = result.rows.map(r => r.id);
      const leaseTime = 300; // Default 5 minutes lease
      
      await client.query(
        `UPDATE queen.messages 
         SET status = 'processing', 
             locked_at = NOW(),
             processing_at = NOW(),
             lease_expires_at = NOW() + INTERVAL '${leaseTime} seconds',
             retry_count = retry_count + 1
         WHERE id = ANY($1::uuid[])`,
        [messageIds]
      );
      
      // Format messages
      const messages = result.rows.map(row => ({
        id: row.id,
        transactionId: row.transaction_id,
        queue: row.queue_name,
        partition: row.partition_name,
        data: row.payload,
        retryCount: row.retry_count || 0,
        priority: row.priority || 0,
        createdAt: row.created_at,
        lockedAt: row.locked_at,
        options: row.options || {}
      }));
      
      return { messages };
    });
  };
  
  // Pop with filters (namespace, task)
  const popMessagesWithFilters = async (filters, options = {}) => {
    const { namespace, task } = filters;
    const { wait = false, timeout = 30000, batch = 1 } = options;
    
    return withTransaction(pool, async (client) => {
      let query = `
        SELECT m.*, p.name as partition_name, q.name as queue_name,
               p.options, p.priority, q.namespace, q.task
        FROM queen.messages m
        JOIN queen.partitions p ON m.partition_id = p.id
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE m.status = 'pending'
      `;
      
      const params = [];
      if (namespace) {
        params.push(namespace);
        query += ` AND q.namespace = $${params.length}`;
      }
      if (task) {
        params.push(task);
        query += ` AND q.task = $${params.length}`;
      }
      
      params.push(batch);
      query += ` ORDER BY m.created_at ASC LIMIT $${params.length} FOR UPDATE OF m SKIP LOCKED`;
      
      let result = await client.query(query, params);
      
      if (result.rows.length === 0 && wait) {
        // For filtered pops, we don't have a specific path to wait on
        // Just do simple timeout-based polling
        await client.query('COMMIT');
        await new Promise(resolve => setTimeout(resolve, Math.min(1000, timeout)));
        
        await client.query('BEGIN');
        result = await client.query(query, params);
      }
      
      if (result.rows.length === 0) {
        return { messages: [] };
      }
      
      // Update status to processing
      const messageIds = result.rows.map(r => r.id);
      const leaseTime = 300;
      
      await client.query(
        `UPDATE queen.messages 
         SET status = 'processing',
             locked_at = NOW(),
             processing_at = NOW(),
             lease_expires_at = NOW() + INTERVAL '${leaseTime} seconds',
             retry_count = retry_count + 1
         WHERE id = ANY($1::uuid[])`,
        [messageIds]
      );
      
      // Format messages
      const messages = result.rows.map(row => ({
        id: row.id,
        transactionId: row.transaction_id,
        queue: row.queue_name,
        partition: row.partition_name,
        namespace: row.namespace,
        task: row.task,
        data: row.payload,
        retryCount: row.retry_count || 0,
        priority: row.priority || 0,
        createdAt: row.created_at,
        lockedAt: row.locked_at,
        options: row.options || {}
      }));
      
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
           RETURNING id, partition_id, retry_count`
        : `UPDATE queen.messages 
           SET status = $1, 
               failed_at = NOW(),
               error_message = $2
           WHERE transaction_id = $3
           RETURNING id, partition_id, retry_count`;
           
      const result = await client.query(updateQuery, [status, error, transactionId]);
      
      if (result.rows.length === 0) {
        throw new Error(`Message not found: ${transactionId}`);
      }
      
      // Handle retry logic for failed messages
      if (status === 'failed') {
        const message = await client.query(
          `SELECT m.*, p.options 
           FROM queen.messages m
           JOIN queen.partitions p ON m.partition_id = p.id
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
             SET status = 'dead_letter'
             WHERE id = $1`,
            [result.rows[0].id]
          );
          
          return { status: 'moved_to_dlq' };
        }
      }
      
      return { status, transaction_id: transactionId };
    });
  };
  
  // Batch acknowledge messages
  const acknowledgeMessages = async (acknowledgments) => {
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
  
  // Configure partition options
  const configureQueue = async (queueName, partitionName = 'Default', options = {}) => {
    return withTransaction(pool, async (client) => {
      const resources = await ensureResources(client, queueName, partitionName);
      
      await client.query(
        `UPDATE queen.partitions 
         SET options = $1
         WHERE id = $2`,
        [JSON.stringify(options), resources.partitionId]
      );
      
      // Invalidate cache
      resourceCache.invalidate(queueName, partitionName);
      
      return { queue: queueName, partition: partitionName, options };
    });
  };
  
  // Get queue statistics
  const getQueueStats = async (filters = {}) => {
    const { queue, namespace, task } = filters;
    
    let query = `
      SELECT 
        q.name as queue,
        q.namespace,
        q.task,
        p.name as partition,
        COUNT(CASE WHEN m.status = 'pending' THEN 1 END) as pending,
        COUNT(CASE WHEN m.status = 'processing' THEN 1 END) as processing,
        COUNT(CASE WHEN m.status = 'completed' THEN 1 END) as completed,
        COUNT(CASE WHEN m.status = 'failed' THEN 1 END) as failed,
        COUNT(CASE WHEN m.status = 'dead_letter' THEN 1 END) as dead_letter,
        COUNT(*) as total
      FROM queen.queues q
      LEFT JOIN queen.partitions p ON p.queue_id = q.id
      LEFT JOIN queen.messages m ON m.partition_id = p.id
    `;
    
    const conditions = [];
    const params = [];
    
    if (queue) {
      conditions.push(`q.name = $${params.length + 1}`);
      params.push(queue);
    }
    if (namespace) {
      conditions.push(`q.namespace = $${params.length + 1}`);
      params.push(namespace);
    }
    if (task) {
      conditions.push(`q.task = $${params.length + 1}`);
      params.push(task);
    }
    
    if (conditions.length > 0) {
      query += ` WHERE ${conditions.join(' AND ')}`;
    }
    
    query += ` GROUP BY q.name, q.namespace, q.task, p.name ORDER BY q.name, p.name`;
    
    const result = await pool.query(query, params);
    
    // Transform the flat structure into the expected nested format
    return result.rows.map(row => ({
      queue: row.queue,
      namespace: row.namespace,
      task: row.task,
      partition: row.partition,
      stats: {
        pending: parseInt(row.pending) || 0,
        processing: parseInt(row.processing) || 0,
        completed: parseInt(row.completed) || 0,
        failed: parseInt(row.failed) || 0,
        deadLetter: parseInt(row.dead_letter) || 0,
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
  
  return {
    pushMessages,
    pushMessagesBatch,
    popMessages,
    popMessagesWithFilters,
    acknowledgeMessage,
    acknowledgeMessages,
    configureQueue,
    getQueueStats,
    reclaimExpiredLeases
  };
};