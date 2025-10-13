import { log, LogTypes } from '../utils/logger.js';
import config from '../config.js';

export const createMessagesRoutes = (pool, queueManager) => {
  
  // List messages with filters
  const listMessages = async (filters = {}) => {
    const { queue, partition, namespace, task, status, limit = config.API.DEFAULT_LIMIT, offset = config.API.DEFAULT_OFFSET } = filters;
    
    let query = `
      SELECT 
        m.id,
        m.transaction_id,
        m.payload,
        m.status,
        m.worker_id,
        m.created_at,
        m.locked_at,
        m.completed_at,
        m.failed_at,
        m.error_message,
        m.retry_count,
        m.lease_expires_at,
        q.name || '/' || p.name as queue_path,
        q.name as queue_name,
        p.name as partition_name,
        q.namespace,
        q.task
      FROM queen.messages m
      JOIN queen.partitions p ON p.id = m.partition_id
      JOIN queen.queues q ON q.id = p.queue_id
      WHERE 1=1
    `;
    
    const params = [];
    let paramCount = 0;
    
    if (queue) {
      params.push(queue);
      query += ` AND q.name = $${++paramCount}`;
    }
    if (partition) {
      params.push(partition);
      query += ` AND p.name = $${++paramCount}`;
    }
    if (namespace) {
      params.push(namespace);
      query += ` AND q.namespace = $${++paramCount}`;
    }
    if (task) {
      params.push(task);
      query += ` AND q.task = $${++paramCount}`;
    }
    if (status) {
      params.push(status);
      query += ` AND m.status = $${++paramCount}`;
    }
    
    query += ` ORDER BY m.created_at DESC, m.id DESC`;
    
    params.push(limit);
    query += ` LIMIT $${++paramCount}`;
    
    params.push(offset);
    query += ` OFFSET $${++paramCount}`;
    
    const result = await pool.query(query, params);
    
    return result.rows.map(row => ({
      id: row.id,
      transactionId: row.transaction_id,
      queuePath: row.queue_path,
      queue: row.queue_name,
      partition: row.partition_name,
      namespace: row.namespace,
      task: row.task,
      payload: row.payload,
      status: row.status,
      workerId: row.worker_id,
      createdAt: row.created_at,
      lockedAt: row.locked_at,
      completedAt: row.completed_at,
      failedAt: row.failed_at,
      errorMessage: row.error_message,
      retryCount: row.retry_count,
      leaseExpiresAt: row.lease_expires_at
    }));
  };
  
  // Get single message by transaction ID
  const getMessage = async (transactionId) => {
    const result = await pool.query(`
      SELECT 
        m.*,
        q.name || '/' || p.name as queue_path,
        q.name as queue_name,
        p.name as partition_name,
        q.namespace,
        q.task,
        q.lease_time,
        q.retry_limit,
        q.retry_delay,
        q.ttl,
        q.priority
      FROM queen.messages m
      JOIN queen.partitions p ON p.id = m.partition_id
      JOIN queen.queues q ON q.id = p.queue_id
      WHERE m.transaction_id = $1
    `, [transactionId]);
    
    if (result.rows.length === 0) {
      throw new Error('Message not found');
    }
    
    const row = result.rows[0];
    return {
      id: row.id,
      transactionId: row.transaction_id,
      queuePath: row.queue_path,
      queue: row.queue_name,
      partition: row.partition_name,
      namespace: row.namespace,
      task: row.task,
      payload: row.payload,
      status: row.status,
      workerId: row.worker_id,
      createdAt: row.created_at,
      lockedAt: row.locked_at,
      completedAt: row.completed_at,
      failedAt: row.failed_at,
      errorMessage: row.error_message,
      retryCount: row.retry_count,
      leaseExpiresAt: row.lease_expires_at,
      queueConfig: {
        leaseTime: row.lease_time,
        retryLimit: row.retry_limit,
        retryDelay: row.retry_delay,
        ttl: row.ttl,
        priority: row.priority
      }
    };
  };
  
  // Delete message
  const deleteMessage = async (transactionId) => {
    const result = await pool.query(`
      DELETE FROM queen.messages 
      WHERE transaction_id = $1 
      RETURNING id
    `, [transactionId]);
    
    if (result.rows.length === 0) {
      throw new Error('Message not found');
    }
    
    log(`${LogTypes.DELETE} | TransactionId: ${transactionId} | MessageId: ${result.rows[0].id}`);
    return { deleted: true, transactionId };
  };
  
  // Retry message
  const retryMessage = async (transactionId) => {
    // Find the message
    const msgCheck = await pool.query(`
      SELECT m.id, ms.consumer_group, ms.status, ms.retry_count
      FROM queen.messages m
      LEFT JOIN queen.messages_status ms ON ms.message_id = m.id
      WHERE m.transaction_id = $1
    `, [transactionId]);
    
    if (msgCheck.rows.length === 0) {
      throw new Error('Message not found');
    }
    
    const msg = msgCheck.rows[0];
    const consumerGroup = msg.consumer_group || '__QUEUE_MODE__';
    const newRetryCount = (msg.retry_count || 0) + 1;
    
    // Check if message is in DLQ and remove it
    await pool.query(`
      DELETE FROM queen.dead_letter_queue
      WHERE message_id = $1
    `, [msg.id]);
    
    // Update or create status as pending for retry
    const result = await pool.query(`
      INSERT INTO queen.messages_status (message_id, consumer_group, status, retry_count, worker_id, locked_at, lease_expires_at, error_message)
      VALUES ($1, $2, 'pending', $3, NULL, NULL, NULL, NULL)
      ON CONFLICT (message_id, consumer_group) 
      DO UPDATE SET
        status = 'pending',
        worker_id = NULL,
        locked_at = NULL,
        lease_expires_at = NULL,
        retry_count = EXCLUDED.retry_count,
        error_message = NULL
      RETURNING retry_count
    `, [msg.id, consumerGroup, newRetryCount]);
    
    log(`${LogTypes.RETRY} | TransactionId: ${transactionId} | NewRetryCount: ${result.rows[0].retry_count}`);
    return { retried: true, transactionId, newRetryCount: result.rows[0].retry_count };
  };
  
  // Move message to DLQ
  const moveToDLQ = async (transactionId) => {
    // Find the message and check if it's in failed state
    const msgCheck = await pool.query(`
      SELECT m.id, m.partition_id, ms.consumer_group, ms.error_message, ms.retry_count, m.created_at
      FROM queen.messages m
      LEFT JOIN queen.messages_status ms ON ms.message_id = m.id
      WHERE m.transaction_id = $1
        AND (ms.status = 'failed' OR ms.status IS NULL)
    `, [transactionId]);
    
    if (msgCheck.rows.length === 0) {
      throw new Error('Message not found or not in failed state');
    }
    
    const msg = msgCheck.rows[0];
    const consumerGroup = msg.consumer_group || '__QUEUE_MODE__';
    
    // Insert into dead_letter_queue
    const result = await pool.query(`
      INSERT INTO queen.dead_letter_queue (message_id, partition_id, consumer_group, error_message, retry_count, original_created_at)
      VALUES ($1, $2, $3, $4, $5, $6)
      ON CONFLICT DO NOTHING
      RETURNING id
    `, [msg.id, msg.partition_id, consumerGroup, msg.error_message || 'Manually moved to DLQ', msg.retry_count || 0, msg.created_at]);
    
    log(`${LogTypes.MANUAL_DLQ} | TransactionId: ${transactionId} | MessageId: ${msg.id}`);
    return { movedToDLQ: true, transactionId, messageId: msg.id };
  };
  
  // Get related messages (same partition, near in time)
  const getRelatedMessages = async (transactionId) => {
    // First get the message details
    const msgResult = await pool.query(`
      SELECT partition_id, created_at 
      FROM queen.messages 
      WHERE transaction_id = $1
    `, [transactionId]);
    
    if (msgResult.rows.length === 0) {
      return [];
    }
    
    const { partition_id, created_at } = msgResult.rows[0];
    
    // Get messages from same partition within 1 hour
    const result = await pool.query(`
      SELECT 
        m.transaction_id,
        m.status,
        m.created_at,
        m.payload
      FROM queen.messages m
      WHERE m.partition_id = $1
        AND m.transaction_id != $2
        AND m.created_at BETWEEN $3::timestamp - INTERVAL '1 hour' 
                            AND $3::timestamp + INTERVAL '1 hour'
      ORDER BY ABS(EXTRACT(EPOCH FROM (m.created_at - $3::timestamp)))
      LIMIT ${config.ANALYTICS.MAX_RELATED_MESSAGES}
    `, [partition_id, transactionId, created_at]);
    
    return result.rows.map(row => ({
      transactionId: row.transaction_id,
      status: row.status,
      createdAt: row.created_at,
      payload: row.payload
    }));
  };
  
  // Clear all messages in a queue or partition
  const clearQueue = async (queue, partition = null) => {
    let query;
    let params;
    
    if (partition) {
      // Clear specific partition
      query = `
        DELETE FROM queen.messages
        WHERE partition_id IN (
          SELECT p.id 
          FROM queen.partitions p
          JOIN queen.queues q ON q.id = p.queue_id
          WHERE q.name = $1 AND p.name = $2
        )
        RETURNING id
      `;
      params = [queue, partition];
    } else {
      // Clear entire queue (all partitions)
      query = `
        DELETE FROM queen.messages
        WHERE partition_id IN (
          SELECT p.id 
          FROM queen.partitions p
          JOIN queen.queues q ON q.id = p.queue_id
          WHERE q.name = $1
        )
        RETURNING id
      `;
      params = [queue];
    }
    
    const result = await pool.query(query, params);
    
    const clearedCount = result.rowCount || 0;
    log(`${LogTypes.CLEAR_QUEUE} | Queue: ${queue} | Partition: ${partition || 'all'} | Count: ${clearedCount} messages deleted`);
    
    return { 
      cleared: true, 
      count: clearedCount,
      queue,
      partition: partition || 'all'
    };
  };
  
  return {
    listMessages,
    getMessage,
    deleteMessage,
    retryMessage,
    moveToDLQ,
    getRelatedMessages,
    clearQueue
  };
};