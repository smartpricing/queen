export const createMessagesRoutes = (pool, queueManager) => {
  
  // List messages with filters
  const listMessages = async (filters = {}) => {
    const { ns, task, queue, status, limit = 100, offset = 0 } = filters;
    
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
        n.name || '/' || t.name || '/' || q.name as queue_path
      FROM queen.messages m
      JOIN queen.queues q ON q.id = m.queue_id
      JOIN queen.tasks t ON t.id = q.task_id
      JOIN queen.namespaces n ON n.id = t.namespace_id
      WHERE 1=1
    `;
    
    const params = [];
    let paramCount = 0;
    
    if (ns) {
      params.push(ns);
      query += ` AND n.name = $${++paramCount}`;
    }
    if (task) {
      params.push(task);
      query += ` AND t.name = $${++paramCount}`;
    }
    if (queue) {
      params.push(queue);
      query += ` AND q.name = $${++paramCount}`;
    }
    if (status) {
      params.push(status);
      query += ` AND m.status = $${++paramCount}`;
    }
    
    query += ` ORDER BY m.created_at DESC`;
    
    params.push(limit);
    query += ` LIMIT $${++paramCount}`;
    
    params.push(offset);
    query += ` OFFSET $${++paramCount}`;
    
    const result = await pool.query(query, params);
    
    return result.rows.map(row => ({
      id: row.id,
      transactionId: row.transaction_id,
      queuePath: row.queue_path,
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
        n.name || '/' || t.name || '/' || q.name as queue_path,
        q.options as queue_options
      FROM queen.messages m
      JOIN queen.queues q ON q.id = m.queue_id
      JOIN queen.tasks t ON t.id = q.task_id
      JOIN queen.namespaces n ON n.id = t.namespace_id
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
      queueOptions: row.queue_options
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
    
    return { deleted: true, transactionId };
  };
  
  // Retry message
  const retryMessage = async (transactionId) => {
    const result = await pool.query(`
      UPDATE queen.messages 
      SET 
        status = 'pending',
        worker_id = NULL,
        locked_at = NULL,
        lease_expires_at = NULL,
        retry_count = retry_count + 1,
        error_message = NULL
      WHERE transaction_id = $1 
        AND status IN ('failed', 'dead_letter')
      RETURNING id
    `, [transactionId]);
    
    if (result.rows.length === 0) {
      throw new Error('Message not found or not in failed state');
    }
    
    return { retried: true, transactionId };
  };
  
  // Move message to DLQ
  const moveToDLQ = async (transactionId) => {
    const result = await pool.query(`
      UPDATE queen.messages 
      SET 
        status = 'dead_letter',
        failed_at = NOW()
      WHERE transaction_id = $1 
        AND status = 'failed'
      RETURNING id
    `, [transactionId]);
    
    if (result.rows.length === 0) {
      throw new Error('Message not found or not in failed state');
    }
    
    return { movedToDLQ: true, transactionId };
  };
  
  // Get related messages (same queue, near in time)
  const getRelatedMessages = async (transactionId) => {
    // First get the message details
    const msgResult = await pool.query(`
      SELECT queue_id, created_at 
      FROM queen.messages 
      WHERE transaction_id = $1
    `, [transactionId]);
    
    if (msgResult.rows.length === 0) {
      return [];
    }
    
    const { queue_id, created_at } = msgResult.rows[0];
    
    // Get messages from same queue within 1 hour
    const result = await pool.query(`
      SELECT 
        m.transaction_id,
        m.status,
        m.created_at,
        m.payload
      FROM queen.messages m
      WHERE m.queue_id = $1
        AND m.transaction_id != $2
        AND m.created_at BETWEEN $3::timestamp - INTERVAL '1 hour' 
                            AND $3::timestamp + INTERVAL '1 hour'
      ORDER BY ABS(EXTRACT(EPOCH FROM (m.created_at - $3::timestamp)))
      LIMIT 10
    `, [queue_id, transactionId, created_at]);
    
    return result.rows.map(row => ({
      transactionId: row.transaction_id,
      status: row.status,
      createdAt: row.created_at,
      payload: row.payload
    }));
  };
  
  // Clear all messages in a queue
  const clearQueue = async (ns, task, queue) => {
    const result = await pool.query(`
      DELETE FROM queen.messages
      WHERE queue_id IN (
        SELECT q.id 
        FROM queen.queues q
        JOIN queen.tasks t ON t.id = q.task_id
        JOIN queen.namespaces n ON n.id = t.namespace_id
        WHERE n.name = $1 AND t.name = $2 AND q.name = $3
      )
      RETURNING id
    `, [ns, task, queue]);
    
    return { 
      cleared: true, 
      count: result.rowCount,
      queue: `${ns}/${task}/${queue}`
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
