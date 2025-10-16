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
        m.created_at,
        m.trace_id,
        q.name || '/' || p.name as queue_path,
        q.name as queue_name,
        p.name as partition_name,
        q.namespace,
        q.task,
        q.priority as queue_priority,
        pc.lease_expires_at,
        pc.last_consumed_created_at,
        pc.last_consumed_id,
        CASE
          WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
          WHEN pc.last_consumed_created_at IS NOT NULL AND (
            m.created_at < pc.last_consumed_created_at OR 
            (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id)
          ) THEN 'completed'
          WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 'processing'
          ELSE 'pending'
        END as message_status
      FROM queen.messages m
      JOIN queen.partitions p ON p.id = m.partition_id
      JOIN queen.queues q ON q.id = p.queue_id
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
      LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
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
      // Filter by computed status
      params.push(status);
      query += ` AND CASE
          WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
          WHEN pc.last_consumed_created_at IS NOT NULL AND (
            m.created_at < pc.last_consumed_created_at OR 
            (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id)
          ) THEN 'completed'
          WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 'processing'
          ELSE 'pending'
        END = $${++paramCount}`;
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
      status: row.message_status,
      traceId: row.trace_id,
      queuePriority: row.queue_priority,
      createdAt: row.created_at,
      leaseExpiresAt: row.lease_expires_at
    }));
  };
  
  // Get single message by transaction ID
  const getMessage = async (transactionId) => {
    const result = await pool.query(`
      SELECT 
        m.id,
        m.transaction_id,
        m.partition_id,
        m.payload,
        m.created_at,
        m.trace_id,
        q.name || '/' || p.name as queue_path,
        q.name as queue_name,
        p.name as partition_name,
        q.namespace,
        q.task,
        q.lease_time,
        q.retry_limit,
        q.retry_delay,
        q.ttl,
        q.priority as queue_priority,
        pc.lease_expires_at,
        pc.last_consumed_created_at,
        pc.last_consumed_id,
        dlq.error_message,
        dlq.retry_count,
        CASE
          WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
          WHEN pc.last_consumed_created_at IS NOT NULL AND (
            m.created_at < pc.last_consumed_created_at OR 
            (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id)
          ) THEN 'completed'
          WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 'processing'
          ELSE 'pending'
        END as message_status
      FROM queen.messages m
      JOIN queen.partitions p ON p.id = m.partition_id
      JOIN queen.queues q ON q.id = p.queue_id
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
      LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
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
      status: row.message_status,
      traceId: row.trace_id,
      createdAt: row.created_at,
      errorMessage: row.error_message,
      retryCount: row.retry_count,
      leaseExpiresAt: row.lease_expires_at,
      queueConfig: {
        leaseTime: row.lease_time,
        retryLimit: row.retry_limit,
        retryDelay: row.retry_delay,
        ttl: row.ttl,
        priority: row.queue_priority
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
  
  // Retry message (remove from DLQ - cursor will process it if not consumed yet)
  const retryMessage = async (transactionId) => {
    // Find the message
    const msgCheck = await pool.query(`
      SELECT m.id, m.partition_id, m.created_at,
             dlq.consumer_group, dlq.retry_count
      FROM queen.messages m
      LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
      WHERE m.transaction_id = $1
    `, [transactionId]);
    
    if (msgCheck.rows.length === 0) {
      throw new Error('Message not found');
    }
    
    const msg = msgCheck.rows[0];
    
    if (!msg.consumer_group) {
      throw new Error('Message is not in DLQ, cannot retry');
    }
    
    const consumerGroup = msg.consumer_group;
    const newRetryCount = (msg.retry_count || 0) + 1;
    
    // Remove from DLQ - message will be available for consumption if before cursor
    const deleteResult = await pool.query(`
      DELETE FROM queen.dead_letter_queue
      WHERE message_id = $1 AND consumer_group = $2
      RETURNING message_id
    `, [msg.id, consumerGroup]);
    
    if (deleteResult.rows.length === 0) {
      throw new Error('Message not found in DLQ');
    }
    
    log(`${LogTypes.RETRY} | TransactionId: ${transactionId} | ConsumerGroup: ${consumerGroup}`);
    return { retried: true, transactionId, consumerGroup };
  };
  
  // Move message to DLQ
  const moveToDLQ = async (transactionId, consumerGroup = '__QUEUE_MODE__', errorMessage = 'Manually moved to DLQ') => {
    // Find the message
    const msgCheck = await pool.query(`
      SELECT m.id, m.partition_id, m.created_at
      FROM queen.messages m
      WHERE m.transaction_id = $1
    `, [transactionId]);
    
    if (msgCheck.rows.length === 0) {
      throw new Error('Message not found');
    }
    
    const msg = msgCheck.rows[0];
    
    // Insert into dead_letter_queue
    const result = await pool.query(`
      INSERT INTO queen.dead_letter_queue (message_id, partition_id, consumer_group, error_message, retry_count, original_created_at)
      VALUES ($1, $2, $3, $4, 0, $5)
      ON CONFLICT DO NOTHING
      RETURNING id
    `, [msg.id, msg.partition_id, consumerGroup, errorMessage, msg.created_at]);
    
    if (result.rows.length === 0) {
      throw new Error('Message already in DLQ for this consumer group');
    }
    
    log(`${LogTypes.MANUAL_DLQ} | TransactionId: ${transactionId} | MessageId: ${msg.id} | ConsumerGroup: ${consumerGroup}`);
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
        m.created_at,
        m.payload,
        pc.lease_expires_at,
        pc.last_consumed_created_at,
        pc.last_consumed_id,
        CASE
          WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
          WHEN pc.last_consumed_created_at IS NOT NULL AND (
            m.created_at < pc.last_consumed_created_at OR 
            (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id)
          ) THEN 'completed'
          WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 'processing'
          ELSE 'pending'
        END as message_status
      FROM queen.messages m
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = m.partition_id AND pc.consumer_group = '__QUEUE_MODE__'
      LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
      WHERE m.partition_id = $1
        AND m.transaction_id != $2
        AND m.created_at BETWEEN $3::timestamp - INTERVAL '1 hour' 
                            AND $3::timestamp + INTERVAL '1 hour'
      ORDER BY ABS(EXTRACT(EPOCH FROM (m.created_at - $3::timestamp)))
      LIMIT ${config.ANALYTICS.MAX_RELATED_MESSAGES}
    `, [partition_id, transactionId, created_at]);
    
    return result.rows.map(row => ({
      transactionId: row.transaction_id,
      status: row.message_status,
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