export const createResourcesRoutes = (pool) => {
  
  // Get all queues with their partitions
  const getQueues = async (filters = {}) => {
    const { namespace, task } = filters;
    
    let query = `
      SELECT DISTINCT
        q.id as queue_id,
        q.name as queue_name,
        q.namespace,
        q.task,
        q.created_at as queue_created_at,
        COUNT(DISTINCT p.id) as partition_count,
        COUNT(DISTINCT m.id) as message_count,
        COUNT(DISTINCT CASE WHEN m.status = 'pending' THEN m.id END) as pending_count,
        COUNT(DISTINCT CASE WHEN m.status = 'processing' THEN m.id END) as processing_count
      FROM queen.queues q
      LEFT JOIN queen.partitions p ON p.queue_id = q.id
      LEFT JOIN queen.messages m ON m.partition_id = p.id
      WHERE 1=1
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
    
    query += ` GROUP BY q.id, q.name, q.namespace, q.task, q.created_at
               ORDER BY q.created_at DESC`;
    
    const result = await pool.query(query, params);
    
    return result.rows.map(row => ({
      id: row.queue_id,
      name: row.queue_name,
      namespace: row.namespace,
      task: row.task,
      createdAt: row.queue_created_at,
      partitions: parseInt(row.partition_count),
      messages: {
        total: parseInt(row.message_count),
        pending: parseInt(row.pending_count),
        processing: parseInt(row.processing_count)
      }
    }));
  };
  
  // Get single queue with detailed partition information
  const getQueue = async (queueName) => {
    // Get queue info
    const queueResult = await pool.query(`
      SELECT 
        q.id,
        q.name,
        q.namespace,
        q.task,
        q.created_at
      FROM queen.queues q
      WHERE q.name = $1
    `, [queueName]);
    
    if (queueResult.rows.length === 0) {
      throw new Error('Queue not found');
    }
    
    const queue = queueResult.rows[0];
    
    // Get partitions with stats
    const partitionsResult = await pool.query(`
      SELECT 
        p.id,
        p.name,
        p.priority,
        p.options,
        p.created_at,
        COUNT(DISTINCT m.id) as message_count,
        COUNT(DISTINCT CASE WHEN m.status = 'pending' THEN m.id END) as pending,
        COUNT(DISTINCT CASE WHEN m.status = 'processing' THEN m.id END) as processing,
        COUNT(DISTINCT CASE WHEN m.status = 'completed' THEN m.id END) as completed,
        COUNT(DISTINCT CASE WHEN m.status = 'failed' THEN m.id END) as failed,
        COUNT(DISTINCT CASE WHEN m.status = 'dead_letter' THEN m.id END) as dead_letter,
        MIN(m.created_at) as oldest_message,
        MAX(m.created_at) as newest_message
      FROM queen.partitions p
      LEFT JOIN queen.messages m ON m.partition_id = p.id
      WHERE p.queue_id = $1
      GROUP BY p.id, p.name, p.priority, p.options, p.created_at
      ORDER BY p.priority DESC, p.name
    `, [queue.id]);
    
    const partitions = partitionsResult.rows.map(row => ({
      id: row.id,
      name: row.name,
      priority: row.priority,
      options: row.options,
      createdAt: row.created_at,
      stats: {
        total: parseInt(row.message_count),
        pending: parseInt(row.pending),
        processing: parseInt(row.processing),
        completed: parseInt(row.completed),
        failed: parseInt(row.failed),
        deadLetter: parseInt(row.dead_letter)
      },
      oldestMessage: row.oldest_message,
      newestMessage: row.newest_message
    }));
    
    // Calculate totals
    const totals = partitions.reduce((acc, p) => ({
      total: acc.total + p.stats.total,
      pending: acc.pending + p.stats.pending,
      processing: acc.processing + p.stats.processing,
      completed: acc.completed + p.stats.completed,
      failed: acc.failed + p.stats.failed,
      deadLetter: acc.deadLetter + p.stats.deadLetter
    }), {
      total: 0,
      pending: 0,
      processing: 0,
      completed: 0,
      failed: 0,
      deadLetter: 0
    });
    
    return {
      id: queue.id,
      name: queue.name,
      namespace: queue.namespace,
      task: queue.task,
      createdAt: queue.created_at,
      partitions,
      totals
    };
  };
  
  // Get all partitions across all queues
  const getPartitions = async (filters = {}) => {
    const { queue, minDepth } = filters;
    
    let query = `
      SELECT 
        p.id,
        p.name as partition_name,
        p.priority,
        p.options,
        p.created_at,
        q.name as queue_name,
        q.namespace,
        q.task,
        COUNT(DISTINCT CASE WHEN m.status = 'pending' THEN m.id END) as pending,
        COUNT(DISTINCT CASE WHEN m.status = 'processing' THEN m.id END) as processing,
        COUNT(DISTINCT m.id) as total
      FROM queen.partitions p
      JOIN queen.queues q ON q.id = p.queue_id
      LEFT JOIN queen.messages m ON m.partition_id = p.id
      WHERE 1=1
    `;
    
    const params = [];
    if (queue) {
      params.push(queue);
      query += ` AND q.name = $${params.length}`;
    }
    
    query += ` GROUP BY p.id, p.name, p.priority, p.options, p.created_at, 
                        q.name, q.namespace, q.task`;
    
    if (minDepth) {
      params.push(minDepth);
      query += ` HAVING COUNT(DISTINCT CASE WHEN m.status = 'pending' THEN m.id END) >= $${params.length}`;
    }
    
    query += ` ORDER BY q.name, p.priority DESC, p.name`;
    
    const result = await pool.query(query, params);
    
    return result.rows.map(row => ({
      id: row.id,
      name: row.partition_name,
      queue: row.queue_name,
      namespace: row.namespace,
      task: row.task,
      priority: row.priority,
      options: row.options,
      createdAt: row.created_at,
      depth: parseInt(row.pending),
      processing: parseInt(row.processing),
      total: parseInt(row.total)
    }));
  };
  
  // Get namespace/task groupings
  const getNamespaces = async () => {
    const result = await pool.query(`
      SELECT DISTINCT
        q.namespace,
        COUNT(DISTINCT q.id) as queue_count,
        COUNT(DISTINCT p.id) as partition_count,
        COUNT(DISTINCT m.id) as message_count,
        COUNT(DISTINCT CASE WHEN m.status = 'pending' THEN m.id END) as pending_count
      FROM queen.queues q
      LEFT JOIN queen.partitions p ON p.queue_id = q.id
      LEFT JOIN queen.messages m ON m.partition_id = p.id
      WHERE q.namespace IS NOT NULL
      GROUP BY q.namespace
      ORDER BY q.namespace
    `);
    
    return result.rows.map(row => ({
      namespace: row.namespace,
      queues: parseInt(row.queue_count),
      partitions: parseInt(row.partition_count),
      messages: {
        total: parseInt(row.message_count),
        pending: parseInt(row.pending_count)
      }
    }));
  };
  
  const getTasks = async () => {
    const result = await pool.query(`
      SELECT DISTINCT
        q.task,
        COUNT(DISTINCT q.id) as queue_count,
        COUNT(DISTINCT p.id) as partition_count,
        COUNT(DISTINCT m.id) as message_count,
        COUNT(DISTINCT CASE WHEN m.status = 'pending' THEN m.id END) as pending_count
      FROM queen.queues q
      LEFT JOIN queen.partitions p ON p.queue_id = q.id
      LEFT JOIN queen.messages m ON m.partition_id = p.id
      WHERE q.task IS NOT NULL
      GROUP BY q.task
      ORDER BY q.task
    `);
    
    return result.rows.map(row => ({
      task: row.task,
      queues: parseInt(row.queue_count),
      partitions: parseInt(row.partition_count),
      messages: {
        total: parseInt(row.message_count),
        pending: parseInt(row.pending_count)
      }
    }));
  };
  
  // Get system overview
  const getSystemOverview = async () => {
    const overview = await pool.query(`
      SELECT
        (SELECT COUNT(*) FROM queen.queues) as total_queues,
        (SELECT COUNT(*) FROM queen.partitions) as total_partitions,
        (SELECT COUNT(*) FROM queen.messages) as total_messages,
        (SELECT COUNT(*) FROM queen.messages WHERE status = 'pending') as pending_messages,
        (SELECT COUNT(*) FROM queen.messages WHERE status = 'processing') as processing_messages,
        (SELECT COUNT(*) FROM queen.messages WHERE status = 'completed') as completed_messages,
        (SELECT COUNT(*) FROM queen.messages WHERE status = 'failed') as failed_messages,
        (SELECT COUNT(*) FROM queen.messages WHERE status = 'dead_letter') as dead_letter_messages,
        (SELECT COUNT(DISTINCT namespace) FROM queen.queues WHERE namespace IS NOT NULL) as namespaces,
        (SELECT COUNT(DISTINCT task) FROM queen.queues WHERE task IS NOT NULL) as tasks
    `);
    
    const row = overview.rows[0];
    
    return {
      queues: parseInt(row.total_queues),
      partitions: parseInt(row.total_partitions),
      namespaces: parseInt(row.namespaces),
      tasks: parseInt(row.tasks),
      messages: {
        total: parseInt(row.total_messages),
        pending: parseInt(row.pending_messages),
        processing: parseInt(row.processing_messages),
        completed: parseInt(row.completed_messages),
        failed: parseInt(row.failed_messages),
        deadLetter: parseInt(row.dead_letter_messages)
      },
      timestamp: new Date().toISOString()
    };
  };
  
  return {
    getQueues,
    getQueue,
    getPartitions,
    getNamespaces,
    getTasks,
    getSystemOverview
  };
};
