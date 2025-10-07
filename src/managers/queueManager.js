import { generateUUID } from '../utils/uuid.js';
import { withTransaction } from '../database/connection.js';

export const createQueueManager = (pool, resourceCache) => {
  
  // Ensure namespace, task, and queue exist (with caching)
  const ensureResources = async (client, ns, task, queue) => {
    // Check cache first
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
  
  // Push messages to queue
  const pushMessages = async (items) => {
    return withTransaction(pool, async (client) => {
      const results = [];
      
      for (const item of items) {
        const { ns, task, queue, payload, transactionId = generateUUID() } = item;
        
        // Check if transaction already exists (idempotency)
        const existing = await client.query(
          'SELECT id, transaction_id FROM queen.messages WHERE transaction_id = $1',
          [transactionId]
        );
        
        if (existing.rows.length > 0) {
          results.push({
            id: existing.rows[0].id,
            transactionId: existing.rows[0].transaction_id,
            status: 'duplicate'
          });
          continue;
        }
        
        // Ensure resources exist
        const resources = await ensureResources(client, ns, task, queue);
        
        // Insert message
        const result = await client.query(
          `INSERT INTO queen.messages (transaction_id, queue_id, payload, status)
           VALUES ($1, $2, $3, 'pending')
           RETURNING id, transaction_id`,
          [transactionId, resources.queueId, JSON.stringify(payload)]
        );
        
        results.push({
          id: result.rows[0].id,
          transactionId: result.rows[0].transaction_id,
          status: 'queued'
        });
      }
      
      return results;
    });
  };
  
  // Pop messages from queue with optional scope
  const popMessages = async (scope, options = {}) => {
    const { batch = 1, workerId = `worker-${process.pid}` } = options;
    
    let query;
    let params;
    
    if (scope.queue) {
      // Specific queue with delayed processing and window buffer support
      query = `
        WITH queue_config AS (
          SELECT q.id, 
                 q.options->>'delayedProcessing' as delayed_processing,
                 q.options->>'windowBuffer' as window_buffer,
                 q.options->>'leaseTime' as lease_time
          FROM queen.queues q
          JOIN queen.tasks t ON t.id = q.task_id
          JOIN queen.namespaces n ON n.id = t.namespace_id
          WHERE n.name = $1 AND t.name = $2 AND q.name = $3
        ),
        window_check AS (
          SELECT 
            CASE 
              WHEN (qc.window_buffer)::int > 0 THEN
                (SELECT MAX(m.created_at) < NOW() - INTERVAL '1 second' * (qc.window_buffer)::int
                 FROM queen.messages m 
                 WHERE m.queue_id = qc.id AND m.status = 'pending')
              ELSE true
            END as window_ready
          FROM queue_config qc
        ),
        available_messages AS (
          SELECT m.id, m.transaction_id, m.queue_id, m.payload, m.created_at,
                 qc.lease_time
          FROM queen.messages m
          CROSS JOIN queue_config qc
          CROSS JOIN window_check wc
          WHERE m.queue_id = qc.id
            AND m.status = 'pending'
            AND wc.window_ready = true
            AND m.created_at <= NOW() - INTERVAL '1 second' * COALESCE((qc.delayed_processing)::int, 0)
          ORDER BY m.created_at
          LIMIT $4
          FOR UPDATE SKIP LOCKED
        )
        UPDATE queen.messages m
        SET status = 'processing',
            worker_id = $5,
            locked_at = NOW(),
            lease_expires_at = NOW() + INTERVAL '1 second' * COALESCE((am.lease_time)::int, 300)
        FROM available_messages am
        WHERE m.id = am.id
        RETURNING m.id, m.transaction_id, m.payload, m.created_at,
                  $1 || '/' || $2 || '/' || $3 as queue_path`;
      params = [scope.ns, scope.task, scope.queue, batch, workerId];
    } else if (scope.task) {
      // Task level
      query = `
        WITH available_messages AS (
          SELECT m.id, m.transaction_id, m.queue_id, m.payload, m.created_at,
                 q.name as queue_name, q.priority, q.options->>'leaseTime' as lease_time
          FROM queen.messages m
          JOIN queen.queues q ON q.id = m.queue_id
          JOIN queen.tasks t ON t.id = q.task_id
          JOIN queen.namespaces n ON n.id = t.namespace_id
          WHERE n.name = $1 AND t.name = $2
            AND m.status = 'pending'
          ORDER BY q.priority DESC, m.created_at
          LIMIT $3
          FOR UPDATE SKIP LOCKED
        )
        UPDATE queen.messages m
        SET status = 'processing',
            worker_id = $4,
            locked_at = NOW(),
            lease_expires_at = NOW() + INTERVAL '1 second' * COALESCE((am.lease_time)::int, 300)
        FROM available_messages am
        WHERE m.id = am.id
        RETURNING m.id, m.transaction_id, m.payload, m.created_at,
                  $1 || '/' || $2 || '/' || am.queue_name as queue_path`;
      params = [scope.ns, scope.task, batch, workerId];
    } else {
      // Namespace level
      query = `
        WITH available_messages AS (
          SELECT m.id, m.transaction_id, m.queue_id, m.payload, m.created_at,
                 q.name as queue_name, q.priority, q.options->>'leaseTime' as lease_time,
                 t.name as task_name
          FROM queen.messages m
          JOIN queen.queues q ON q.id = m.queue_id
          JOIN queen.tasks t ON t.id = q.task_id
          JOIN queen.namespaces n ON n.id = t.namespace_id
          WHERE n.name = $1
            AND m.status = 'pending'
          ORDER BY q.priority DESC, m.created_at
          LIMIT $2
          FOR UPDATE SKIP LOCKED
        )
        UPDATE queen.messages m
        SET status = 'processing',
            worker_id = $3,
            locked_at = NOW(),
            lease_expires_at = NOW() + INTERVAL '1 second' * COALESCE((am.lease_time)::int, 300)
        FROM available_messages am
        WHERE m.id = am.id
        RETURNING m.id, m.transaction_id, m.payload, m.created_at,
                  $1 || '/' || am.task_name || '/' || am.queue_name as queue_path`;
      params = [scope.ns, batch, workerId];
    }
    
    const result = await pool.query(query, params);
    
    return result.rows.map(row => ({
      id: row.id,
      transactionId: row.transaction_id,
      payload: row.payload,
      queue: row.queue_path,
      createdAt: row.created_at
    }));
  };
  
  // Acknowledge message processing
  const acknowledgeMessage = async (transactionId, status, errorMessage = null) => {
    if (status === 'completed') {
      const result = await pool.query(
        `UPDATE queen.messages 
         SET status = 'completed', completed_at = NOW(), worker_id = NULL
         WHERE transaction_id = $1 AND status = 'processing'
         RETURNING id, transaction_id`,
        [transactionId]
      );
      return result.rows[0];
    } else if (status === 'failed') {
      // Check retry limit and either retry or move to dead letter
      const result = await pool.query(
        `WITH msg AS (
          SELECT m.id, m.retry_count, q.options->>'retryLimit' as retry_limit
          FROM queen.messages m
          JOIN queen.queues q ON q.id = m.queue_id
          WHERE m.transaction_id = $1 AND m.status = 'processing'
        )
        UPDATE queen.messages m
        SET status = CASE 
              WHEN msg.retry_count >= COALESCE((msg.retry_limit)::int, 3) THEN 'dead_letter'
              ELSE 'pending'
            END,
            retry_count = m.retry_count + 1,
            failed_at = NOW(),
            error_message = $2,
            worker_id = NULL,
            lease_expires_at = NULL
        FROM msg
        WHERE m.id = msg.id
        RETURNING m.id, m.transaction_id, m.status`,
        [transactionId, errorMessage]
      );
      return result.rows[0];
    }
  };
  
  // Reclaim expired leases (background job)
  const reclaimExpiredLeases = async () => {
    const result = await pool.query(
      `WITH expired AS (
        SELECT m.id, m.retry_count, q.options->>'retryLimit' as retry_limit
        FROM queen.messages m
        JOIN queen.queues q ON q.id = m.queue_id
        WHERE m.status = 'processing' 
          AND m.lease_expires_at < NOW()
      )
      UPDATE queen.messages m
      SET status = CASE 
            WHEN e.retry_count >= COALESCE((e.retry_limit)::int, 3) THEN 'dead_letter'
            ELSE 'pending'
          END,
          retry_count = m.retry_count + 1,
          worker_id = NULL,
          lease_expires_at = NULL
      FROM expired e
      WHERE m.id = e.id
      RETURNING m.id`
    );
    
    return result.rowCount;
  };
  
  // Configure queue with options
  const configureQueue = async (ns, task, queue, options) => {
    return withTransaction(pool, async (client) => {
      const resources = await ensureResources(client, ns, task, queue);
      
      const result = await client.query(
        `UPDATE queen.queues 
         SET options = $2, priority = $3
         WHERE id = $1
         RETURNING id, options`,
        [resources.queueId, JSON.stringify(options), options.priority || 0]
      );
      
      resourceCache.invalidate(ns, task, queue);
      
      return {
        queueId: result.rows[0].id,
        options: result.rows[0].options
      };
    });
  };
  
  // Batch acknowledge messages
  const acknowledgeMessages = async (acknowledgments) => {
    const results = [];
    
    for (const ack of acknowledgments) {
      try {
        const result = await acknowledgeMessage(
          ack.transactionId,
          ack.status,
          ack.error
        );
        results.push({
          transactionId: ack.transactionId,
          status: result ? result.status : 'not_found'
        });
      } catch (error) {
        results.push({
          transactionId: ack.transactionId,
          status: 'error',
          error: error.message
        });
      }
    }
    
    return results;
  };
  
  // Get queue statistics
  const getQueueStats = async (scope = {}) => {
    let query;
    let params = [];
    
    if (scope.queue && scope.task && scope.ns) {
      query = `
        SELECT 
          n.name as namespace,
          t.name as task,
          q.name as queue,
          q.priority,
          q.options,
          COUNT(CASE WHEN m.status = 'pending' THEN 1 END) as pending,
          COUNT(CASE WHEN m.status = 'processing' THEN 1 END) as processing,
          COUNT(CASE WHEN m.status = 'completed' THEN 1 END) as completed,
          COUNT(CASE WHEN m.status = 'failed' THEN 1 END) as failed,
          COUNT(CASE WHEN m.status = 'dead_letter' THEN 1 END) as dead_letter
        FROM queen.queues q
        JOIN queen.tasks t ON t.id = q.task_id
        JOIN queen.namespaces n ON n.id = t.namespace_id
        LEFT JOIN queen.messages m ON m.queue_id = q.id
        WHERE n.name = $1 AND t.name = $2 AND q.name = $3
        GROUP BY n.name, t.name, q.name, q.priority, q.options`;
      params = [scope.ns, scope.task, scope.queue];
    } else if (scope.ns) {
      query = `
        SELECT 
          n.name as namespace,
          t.name as task,
          q.name as queue,
          q.priority,
          q.options,
          COUNT(CASE WHEN m.status = 'pending' THEN 1 END) as pending,
          COUNT(CASE WHEN m.status = 'processing' THEN 1 END) as processing,
          COUNT(CASE WHEN m.status = 'completed' THEN 1 END) as completed,
          COUNT(CASE WHEN m.status = 'failed' THEN 1 END) as failed,
          COUNT(CASE WHEN m.status = 'dead_letter' THEN 1 END) as dead_letter
        FROM queen.queues q
        JOIN queen.tasks t ON t.id = q.task_id
        JOIN queen.namespaces n ON n.id = t.namespace_id
        LEFT JOIN queen.messages m ON m.queue_id = q.id
        WHERE n.name = $1
        GROUP BY n.name, t.name, q.name, q.priority, q.options`;
      params = [scope.ns];
    } else {
      query = `
        SELECT 
          n.name as namespace,
          t.name as task,
          q.name as queue,
          q.priority,
          q.options,
          COUNT(CASE WHEN m.status = 'pending' THEN 1 END) as pending,
          COUNT(CASE WHEN m.status = 'processing' THEN 1 END) as processing,
          COUNT(CASE WHEN m.status = 'completed' THEN 1 END) as completed,
          COUNT(CASE WHEN m.status = 'failed' THEN 1 END) as failed,
          COUNT(CASE WHEN m.status = 'dead_letter' THEN 1 END) as dead_letter
        FROM queen.queues q
        JOIN queen.tasks t ON t.id = q.task_id
        JOIN queen.namespaces n ON n.id = t.namespace_id
        LEFT JOIN queen.messages m ON m.queue_id = q.id
        GROUP BY n.name, t.name, q.name, q.priority, q.options`;
    }
    
    const result = await pool.query(query, params);
    
    return result.rows.map(row => ({
      queue: `${row.namespace}/${row.task}/${row.queue}`,
      priority: row.priority,
      options: row.options,
      stats: {
        pending: parseInt(row.pending),
        processing: parseInt(row.processing),
        completed: parseInt(row.completed),
        failed: parseInt(row.failed),
        deadLetter: parseInt(row.dead_letter),
        total: parseInt(row.pending) + parseInt(row.processing) + 
               parseInt(row.completed) + parseInt(row.failed) + 
               parseInt(row.dead_letter)
      }
    }));
  };
  
  return {
    pushMessages,
    popMessages,
    acknowledgeMessage,
    acknowledgeMessages,
    reclaimExpiredLeases,
    configureQueue,
    getQueueStats
  };
};
