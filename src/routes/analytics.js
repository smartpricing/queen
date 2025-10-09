export const createAnalyticsRoutes = (queueManager) => {
  
  const getQueues = async (filters = {}) => {
    const stats = await queueManager.getQueueStats(filters);
    
    // Group by queue for aggregation
    const queueMap = new Map();
    
    stats.forEach(row => {
      if (!queueMap.has(row.queue)) {
        queueMap.set(row.queue, {
          queue: row.queue,
          namespace: row.namespace,
          task: row.task,
          partitions: [],
          totals: {
            pending: 0,
            processing: 0,
            completed: 0,
            failed: 0,
            deadLetter: 0,
            total: 0
          }
        });
      }
      
      const queueData = queueMap.get(row.queue);
      queueData.partitions.push({
        name: row.partition,
        stats: row.stats
      });
      
      // Aggregate totals
      Object.keys(row.stats).forEach(key => {
        queueData.totals[key] += row.stats[key];
      });
    });
    
    return { queues: Array.from(queueMap.values()) };
  };
  
  const getQueueStats = async (queueName) => {
    const stats = await queueManager.getQueueStats({ queue: queueName });
    
    const totals = {
      pending: 0,
      processing: 0,
      completed: 0,
      failed: 0,
      deadLetter: 0,
      total: 0
    };
    
    const partitions = stats.map(row => {
      Object.keys(row.stats).forEach(key => {
        totals[key] += row.stats[key];
      });
      
      return {
        name: row.partition,
        stats: row.stats
      };
    });
    
    return {
      queue: queueName,
      namespace: stats[0]?.namespace,
      task: stats[0]?.task,
      totals,
      partitions
    };
  };
  
  const getNamespaceStats = async (namespace) => {
    const stats = await queueManager.getQueueStats({ namespace });
    
    // Group by queue
    const queueMap = new Map();
    
    stats.forEach(row => {
      if (!queueMap.has(row.queue)) {
        queueMap.set(row.queue, {
          queue: row.queue,
          task: row.task,
          partitions: [],
          totals: {
            pending: 0,
            processing: 0,
            completed: 0,
            failed: 0,
            deadLetter: 0,
            total: 0
          }
        });
      }
      
      const queueData = queueMap.get(row.queue);
      queueData.partitions.push({
        name: row.partition,
        stats: row.stats
      });
      
      Object.keys(row.stats).forEach(key => {
        queueData.totals[key] += row.stats[key];
      });
    });
    
    // Calculate namespace totals
    const totals = {
      pending: 0,
      processing: 0,
      completed: 0,
      failed: 0,
      deadLetter: 0,
      total: 0
    };
    
    queueMap.forEach(queueData => {
      Object.keys(queueData.totals).forEach(key => {
        totals[key] += queueData.totals[key];
      });
    });
    
    return {
      namespace,
      totals,
      queues: Array.from(queueMap.values())
    };
  };
  
  const getTaskStats = async (task) => {
    const stats = await queueManager.getQueueStats({ task });
    
    // Group by queue
    const queueMap = new Map();
    
    stats.forEach(row => {
      if (!queueMap.has(row.queue)) {
        queueMap.set(row.queue, {
          queue: row.queue,
          namespace: row.namespace,
          partitions: [],
          totals: {
            pending: 0,
            processing: 0,
            completed: 0,
            failed: 0,
            deadLetter: 0,
            total: 0
          }
        });
      }
      
      const queueData = queueMap.get(row.queue);
      queueData.partitions.push({
        name: row.partition,
        stats: row.stats
      });
      
      Object.keys(row.stats).forEach(key => {
        queueData.totals[key] += row.stats[key];
      });
    });
    
    // Calculate task totals
    const totals = {
      pending: 0,
      processing: 0,
      completed: 0,
      failed: 0,
      deadLetter: 0,
      total: 0
    };
    
    queueMap.forEach(queueData => {
      Object.keys(queueData.totals).forEach(key => {
        totals[key] += queueData.totals[key];
      });
    });
    
    return {
      task,
      totals,
      queues: Array.from(queueMap.values())
    };
  };
  
  const getQueueDepths = async (filters = {}) => {
    const stats = await queueManager.getQueueStats(filters);
    
    // Group by queue for aggregation
    const queueMap = new Map();
    
    stats.forEach(row => {
      if (!queueMap.has(row.queue)) {
        queueMap.set(row.queue, {
          queue: row.queue,
          depth: 0,
          processing: 0,
          partitions: []
        });
      }
      
      const queueData = queueMap.get(row.queue);
      queueData.depth += row.stats.pending;
      queueData.processing += row.stats.processing;
      queueData.partitions.push({
        name: row.partition,
        depth: row.stats.pending,
        processing: row.stats.processing
      });
    });
    
    return {
      depths: Array.from(queueMap.values())
    };
  };
  
  const getThroughput = async (pool) => {
    // Get comprehensive throughput metrics over the last hour
    const timeWindow = '1 hour';
    const minuteInterval = 60; // Number of minutes to fetch
    
    try {
      // 1. Incoming messages (created/inserted)
      const incomingQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', NOW() - INTERVAL '${timeWindow}'),
            DATE_TRUNC('minute', NOW()),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(m.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages m ON 
          DATE_TRUNC('minute', m.created_at) = ts.minute
          AND m.created_at >= NOW() - INTERVAL '${timeWindow}'
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 2. Completed messages
      const completedQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', NOW() - INTERVAL '${timeWindow}'),
            DATE_TRUNC('minute', NOW()),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(ms.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages_status ms ON 
          DATE_TRUNC('minute', ms.completed_at) = ts.minute
          AND ms.completed_at >= NOW() - INTERVAL '${timeWindow}'
          AND ms.status = 'completed'
          AND ms.consumer_group IS NULL
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 3. Processing messages (started processing in this minute)
      const processingQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', NOW() - INTERVAL '${timeWindow}'),
            DATE_TRUNC('minute', NOW()),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(ms.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages_status ms ON 
          DATE_TRUNC('minute', ms.processing_at) = ts.minute
          AND ms.processing_at >= NOW() - INTERVAL '${timeWindow}'
          AND ms.consumer_group IS NULL
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 4. Failed messages
      const failedQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', NOW() - INTERVAL '${timeWindow}'),
            DATE_TRUNC('minute', NOW()),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(ms.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages_status ms ON 
          DATE_TRUNC('minute', ms.failed_at) = ts.minute
          AND ms.failed_at >= NOW() - INTERVAL '${timeWindow}'
          AND ms.status = 'failed'
          AND ms.consumer_group IS NULL
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 5. Dead letter messages
      const deadLetterQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', NOW() - INTERVAL '${timeWindow}'),
            DATE_TRUNC('minute', NOW()),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(ms.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages_status ms ON 
          DATE_TRUNC('minute', ms.failed_at) = ts.minute
          AND ms.failed_at >= NOW() - INTERVAL '${timeWindow}'
          AND ms.status = 'dead_letter'
          AND ms.consumer_group IS NULL
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 6. Average lag (processing time) per minute
      const lagQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', NOW() - INTERVAL '${timeWindow}'),
            DATE_TRUNC('minute', NOW()),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(
            AVG(
              EXTRACT(EPOCH FROM (ms.completed_at - m.created_at))
            ), 
            0
          ) as avg_lag_seconds,
          COUNT(m.id) as sample_count
        FROM time_series ts
        LEFT JOIN queen.messages_status ms ON 
          DATE_TRUNC('minute', ms.completed_at) = ts.minute
          AND ms.completed_at >= NOW() - INTERVAL '${timeWindow}'
          AND ms.status IN ('completed', 'failed')
          AND ms.completed_at IS NOT NULL
          AND ms.consumer_group IS NULL
        LEFT JOIN queen.messages m ON ms.message_id = m.id
          AND m.created_at IS NOT NULL
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // Execute all queries in parallel
      const [incoming, completed, processing, failed, deadLetter, lag] = await Promise.all([
        pool.query(incomingQuery),
        pool.query(completedQuery),
        pool.query(processingQuery),
        pool.query(failedQuery),
        pool.query(deadLetterQuery),
        pool.query(lagQuery)
      ]);
      
      // If we have no data at all in the last hour, get the last 10 minutes of actual data
      if (incoming.rows.every(row => row.count === 0 || row.count === '0')) {
        const fallbackQuery = `
          SELECT 
            DATE_TRUNC('minute', m.created_at) as minute,
            COUNT(DISTINCT m.id) as incoming_count,
            COUNT(DISTINCT CASE WHEN ms.status = 'completed' THEN m.id END) as completed_count,
            COUNT(DISTINCT CASE WHEN ms.status = 'processing' THEN m.id END) as processing_count,
            COUNT(DISTINCT CASE WHEN ms.status = 'failed' THEN m.id END) as failed_count,
            COUNT(DISTINCT CASE WHEN ms.status = 'dead_letter' THEN m.id END) as dead_letter_count,
            AVG(
              CASE 
                WHEN ms.completed_at IS NOT NULL AND m.created_at IS NOT NULL 
                THEN EXTRACT(EPOCH FROM (ms.completed_at - m.created_at))
                ELSE NULL
              END
            ) as avg_lag_seconds
          FROM queen.messages m
          LEFT JOIN queen.messages_status ms ON m.id = ms.message_id AND ms.consumer_group IS NULL
          WHERE m.created_at IS NOT NULL
          GROUP BY minute
          ORDER BY minute DESC
          LIMIT 10
        `;
        
        const fallbackResult = await pool.query(fallbackQuery);
        
        const throughput = fallbackResult.rows.map(row => ({
          timestamp: row.minute,
          incoming: {
            messagesPerMinute: parseInt(row.incoming_count || 0),
            messagesPerSecond: Math.round(parseInt(row.incoming_count || 0) / 60)
          },
          completed: {
            messagesPerMinute: parseInt(row.completed_count || 0),
            messagesPerSecond: Math.round(parseInt(row.completed_count || 0) / 60)
          },
          processing: {
            messagesPerMinute: parseInt(row.processing_count || 0),
            messagesPerSecond: Math.round(parseInt(row.processing_count || 0) / 60)
          },
          failed: {
            messagesPerMinute: parseInt(row.failed_count || 0),
            messagesPerSecond: Math.round(parseInt(row.failed_count || 0) / 60)
          },
          deadLetter: {
            messagesPerMinute: parseInt(row.dead_letter_count || 0),
            messagesPerSecond: Math.round(parseInt(row.dead_letter_count || 0) / 60)
          },
          lag: {
            avgSeconds: parseFloat(row.avg_lag_seconds || 0),
            avgMilliseconds: Math.round(parseFloat(row.avg_lag_seconds || 0) * 1000)
          }
        }));
        
        return { throughput };
      }
      
      // Combine all metrics by timestamp
      const throughputMap = new Map();
      
      // Initialize with time series
      incoming.rows.forEach(row => {
        throughputMap.set(row.minute.toISOString(), {
          timestamp: row.minute,
          incoming: {
            messagesPerMinute: parseInt(row.count || 0),
            messagesPerSecond: Math.round(parseInt(row.count || 0) / 60)
          },
          completed: {
            messagesPerMinute: 0,
            messagesPerSecond: 0
          },
          processing: {
            messagesPerMinute: 0,
            messagesPerSecond: 0
          },
          failed: {
            messagesPerMinute: 0,
            messagesPerSecond: 0
          },
          deadLetter: {
            messagesPerMinute: 0,
            messagesPerSecond: 0
          },
          lag: {
            avgSeconds: 0,
            avgMilliseconds: 0
          }
        });
      });
      
      // Add completed metrics
      completed.rows.forEach(row => {
        const key = row.minute.toISOString();
        if (throughputMap.has(key)) {
          const entry = throughputMap.get(key);
          entry.completed = {
            messagesPerMinute: parseInt(row.count || 0),
            messagesPerSecond: Math.round(parseInt(row.count || 0) / 60)
          };
        }
      });
      
      // Add processing metrics
      processing.rows.forEach(row => {
        const key = row.minute.toISOString();
        if (throughputMap.has(key)) {
          const entry = throughputMap.get(key);
          entry.processing = {
            messagesPerMinute: parseInt(row.count || 0),
            messagesPerSecond: Math.round(parseInt(row.count || 0) / 60)
          };
        }
      });
      
      // Add failed metrics
      failed.rows.forEach(row => {
        const key = row.minute.toISOString();
        if (throughputMap.has(key)) {
          const entry = throughputMap.get(key);
          entry.failed = {
            messagesPerMinute: parseInt(row.count || 0),
            messagesPerSecond: Math.round(parseInt(row.count || 0) / 60)
          };
        }
      });
      
      // Add dead letter metrics
      deadLetter.rows.forEach(row => {
        const key = row.minute.toISOString();
        if (throughputMap.has(key)) {
          const entry = throughputMap.get(key);
          entry.deadLetter = {
            messagesPerMinute: parseInt(row.count || 0),
            messagesPerSecond: Math.round(parseInt(row.count || 0) / 60)
          };
        }
      });
      
      // Add lag metrics
      lag.rows.forEach(row => {
        const key = row.minute.toISOString();
        if (throughputMap.has(key)) {
          const entry = throughputMap.get(key);
          const avgSeconds = parseFloat(row.avg_lag_seconds || 0);
          entry.lag = {
            avgSeconds: avgSeconds,
            avgMilliseconds: Math.round(avgSeconds * 1000),
            sampleCount: parseInt(row.sample_count || 0)
          };
        }
      });
      
      // Convert map to array and sort by timestamp (newest first)
      const throughput = Array.from(throughputMap.values())
        .sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp));
      
      return { throughput };
      
    } catch (error) {
      console.error('Error calculating throughput metrics:', error);
      throw error;
    }
  };
  
  const getQueueLag = async (filters = {}) => {
    const lagStats = await queueManager.getQueueLag(filters);
    
    // Group by queue for aggregation
    const queueMap = new Map();
    
    lagStats.forEach(row => {
      if (!queueMap.has(row.queue)) {
        queueMap.set(row.queue, {
          queue: row.queue,
          namespace: row.namespace,
          task: row.task,
          partitions: [],
          totals: {
            pendingCount: 0,
            processingCount: 0,
            totalBacklog: 0,
            completedMessages: 0,
            avgProcessingTimeSeconds: 0,
            medianProcessingTimeSeconds: 0,
            p95ProcessingTimeSeconds: 0,
            estimatedLagSeconds: 0,
            medianLagSeconds: 0,
            p95LagSeconds: 0
          }
        });
      }
      
      const queueData = queueMap.get(row.queue);
      queueData.partitions.push({
        name: row.partition,
        stats: row.stats
      });
      
      // Aggregate totals (weighted averages for processing times)
      const currentTotal = queueData.totals.completedMessages;
      const newTotal = currentTotal + row.stats.completedMessages;
      
      if (newTotal > 0) {
        // Weighted average for processing times
        queueData.totals.avgProcessingTimeSeconds = 
          (queueData.totals.avgProcessingTimeSeconds * currentTotal + 
           row.stats.avgProcessingTimeSeconds * row.stats.completedMessages) / newTotal;
        queueData.totals.medianProcessingTimeSeconds = 
          (queueData.totals.medianProcessingTimeSeconds * currentTotal + 
           row.stats.medianProcessingTimeSeconds * row.stats.completedMessages) / newTotal;
        queueData.totals.p95ProcessingTimeSeconds = 
          Math.max(queueData.totals.p95ProcessingTimeSeconds, row.stats.p95ProcessingTimeSeconds);
      }
      
      // Sum up counts and lags
      queueData.totals.pendingCount += row.stats.pendingCount;
      queueData.totals.processingCount += row.stats.processingCount;
      queueData.totals.totalBacklog += row.stats.totalBacklog;
      queueData.totals.completedMessages = newTotal;
      queueData.totals.estimatedLagSeconds += row.stats.estimatedLagSeconds;
      queueData.totals.medianLagSeconds += row.stats.medianLagSeconds;
      queueData.totals.p95LagSeconds += row.stats.p95LagSeconds;
    });
    
    // Add human-readable formats to totals
    Array.from(queueMap.values()).forEach(queueData => {
      const formatDuration = (seconds) => {
        if (seconds === 0) return '0s';
        if (seconds < 60) return `${seconds.toFixed(1)}s`;
        if (seconds < 3600) return `${Math.floor(seconds / 60)}m ${Math.floor(seconds % 60)}s`;
        if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ${Math.floor((seconds % 3600) / 60)}m`;
        return `${Math.floor(seconds / 86400)}d ${Math.floor((seconds % 86400) / 3600)}h`;
      };
      
      queueData.totals.estimatedLag = formatDuration(queueData.totals.estimatedLagSeconds);
      queueData.totals.medianLag = formatDuration(queueData.totals.medianLagSeconds);
      queueData.totals.p95Lag = formatDuration(queueData.totals.p95LagSeconds);
      queueData.totals.avgProcessingTime = formatDuration(queueData.totals.avgProcessingTimeSeconds);
      queueData.totals.medianProcessingTime = formatDuration(queueData.totals.medianProcessingTimeSeconds);
      queueData.totals.p95ProcessingTime = formatDuration(queueData.totals.p95ProcessingTimeSeconds);
    });
    
    return { queues: Array.from(queueMap.values()) };
  };

  return {
    getQueues,
    getQueueStats,
    getNamespaceStats,
    getTaskStats,
    getQueueDepths,
    getThroughput,
    getQueueLag
  };
};