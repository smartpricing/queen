export const createAnalyticsRoutes = (queueManager) => {
  
  const getQueues = async () => {
    const stats = await queueManager.getQueueStats();
    return { queues: stats };
  };
  
  const getNamespaceStats = async (ns) => {
    const stats = await queueManager.getQueueStats({ ns });
    
    // Aggregate namespace-level stats
    const totals = stats.reduce((acc, queue) => ({
      pending: acc.pending + queue.stats.pending,
      processing: acc.processing + queue.stats.processing,
      completed: acc.completed + queue.stats.completed,
      failed: acc.failed + queue.stats.failed,
      deadLetter: acc.deadLetter + queue.stats.deadLetter,
      total: acc.total + queue.stats.total
    }), {
      pending: 0,
      processing: 0,
      completed: 0,
      failed: 0,
      deadLetter: 0,
      total: 0
    });
    
    return {
      namespace: ns,
      totals,
      queues: stats
    };
  };
  
  const getTaskStats = async (ns, task) => {
    const stats = await queueManager.getQueueStats({ ns, task });
    
    const totals = stats.reduce((acc, queue) => ({
      pending: acc.pending + queue.stats.pending,
      processing: acc.processing + queue.stats.processing,
      completed: acc.completed + queue.stats.completed,
      failed: acc.failed + queue.stats.failed,
      deadLetter: acc.deadLetter + queue.stats.deadLetter,
      total: acc.total + queue.stats.total
    }), {
      pending: 0,
      processing: 0,
      completed: 0,
      failed: 0,
      deadLetter: 0,
      total: 0
    });
    
    return {
      namespace: ns,
      task,
      totals,
      queues: stats
    };
  };
  
  const getQueueDepths = async () => {
    const stats = await queueManager.getQueueStats();
    
    return {
      depths: stats.map(q => ({
        queue: q.queue,
        depth: q.stats.pending,
        processing: q.stats.processing
      }))
    };
  };
  
  const getThroughput = async (pool) => {
    // Calculate throughput over last minute
    const result = await pool.query(`
      SELECT 
        DATE_TRUNC('minute', completed_at) as minute,
        COUNT(*) as completed_count
      FROM queen.messages
      WHERE completed_at >= NOW() - INTERVAL '5 minutes'
        AND status = 'completed'
      GROUP BY minute
      ORDER BY minute DESC
    `);
    
    const throughput = result.rows.map(row => ({
      timestamp: row.minute,
      messagesPerMinute: parseInt(row.completed_count),
      messagesPerSecond: Math.round(parseInt(row.completed_count) / 60)
    }));
    
    return { throughput };
  };
  
  return {
    getQueues,
    getNamespaceStats,
    getTaskStats,
    getQueueDepths,
    getThroughput
  };
};
