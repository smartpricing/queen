export const createAnalyticsRoutes = (queueManager) => {
  
  const getQueues = async (filters = {}) => {
    // Extract and pass all filter parameters
    const { fromDateTime, toDateTime, queue, namespace, task } = filters;
    const stats = await queueManager.getQueueStats({ 
      fromDateTime, 
      toDateTime, 
      queue, 
      namespace, 
      task 
    });
    
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
      
      // Create stats object from row data
      const stats = {
        pending: parseInt(row.pending || 0),
        processing: parseInt(row.processing || 0),
        completed: parseInt(row.completed || 0),
        failed: parseInt(row.failed || 0),
        deadLetter: parseInt(row.dead_letter || 0),
        total: parseInt(row.total_messages || 0)
      };
      
      queueData.partitions.push({
        name: row.partition,
        stats: stats
      });
      
      // Aggregate totals
      Object.keys(stats).forEach(key => {
        if (queueData.totals[key] !== undefined) {
          queueData.totals[key] += stats[key];
        }
      });
    });
    
    return { queues: Array.from(queueMap.values()) };
  };
  
  const getQueueStats = async (queueName, filters = {}) => {
    const { fromDateTime, toDateTime } = filters;
    const stats = await queueManager.getQueueStats({ 
      queue: queueName,
      fromDateTime,
      toDateTime
    });
    
    const totals = {
      pending: 0,
      processing: 0,
      completed: 0,
      failed: 0,
      deadLetter: 0,
      total: 0
    };
    
    const partitions = stats.map(row => {
      // Create stats object from row data
      const stats = {
        pending: parseInt(row.pending || 0),
        processing: parseInt(row.processing || 0),
        completed: parseInt(row.completed || 0),
        failed: parseInt(row.failed || 0),
        deadLetter: parseInt(row.dead_letter || 0),
        total: parseInt(row.total_messages || 0)
      };
      
      Object.keys(stats).forEach(key => {
        if (totals[key] !== undefined) {
          totals[key] += stats[key];
        }
      });
      
      return {
        name: row.partition,
        stats: stats
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
  
  const getNamespaceStats = async (namespace, filters = {}) => {
    const { fromDateTime, toDateTime } = filters;
    const stats = await queueManager.getQueueStats({ 
      namespace,
      fromDateTime,
      toDateTime
    });
    
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
      
      // Create stats object from row data
      const stats = {
        pending: parseInt(row.pending || 0),
        processing: parseInt(row.processing || 0),
        completed: parseInt(row.completed || 0),
        failed: parseInt(row.failed || 0),
        deadLetter: parseInt(row.dead_letter || 0),
        total: parseInt(row.total_messages || 0)
      };
      
      queueData.partitions.push({
        name: row.partition,
        stats: stats
      });
      
      Object.keys(stats).forEach(key => {
        if (queueData.totals[key] !== undefined) {
          queueData.totals[key] += stats[key];
        }
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
  
  const getTaskStats = async (task, filters = {}) => {
    const { fromDateTime, toDateTime } = filters;
    const stats = await queueManager.getQueueStats({ 
      task,
      fromDateTime,
      toDateTime
    });
    
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
      
      // Create stats object from row data
      const stats = {
        pending: parseInt(row.pending || 0),
        processing: parseInt(row.processing || 0),
        completed: parseInt(row.completed || 0),
        failed: parseInt(row.failed || 0),
        deadLetter: parseInt(row.dead_letter || 0),
        total: parseInt(row.total_messages || 0)
      };
      
      queueData.partitions.push({
        name: row.partition,
        stats: stats
      });
      
      Object.keys(stats).forEach(key => {
        if (queueData.totals[key] !== undefined) {
          queueData.totals[key] += stats[key];
        }
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
    const { fromDateTime, toDateTime, queue, namespace, task } = filters;
    const stats = await queueManager.getQueueStats({
      fromDateTime,
      toDateTime,
      queue,
      namespace,
      task
    });
    
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
      const pending = parseInt(row.pending || 0);
      const processing = parseInt(row.processing || 0);
      queueData.depth += pending;
      queueData.processing += processing;
      queueData.partitions.push({
        name: row.partition,
        depth: pending,
        processing: processing
      });
    });
    
    return {
      depths: Array.from(queueMap.values())
    };
  };
  
  const getThroughput = async (pool, filters = {}) => {
    // Support custom time window or default to last hour
    const { fromDateTime, toDateTime, queue, namespace, task } = filters;
    
    // Calculate time window based on provided dates or use defaults
    let timeWindow = '1 hour';
    let minuteInterval = 60; // Number of minutes to fetch
    let startTime, endTime;
    
    if (fromDateTime && toDateTime) {
      startTime = new Date(fromDateTime);
      endTime = new Date(toDateTime);
      const diffMs = endTime - startTime;
      const diffMinutes = Math.floor(diffMs / (1000 * 60));
      minuteInterval = Math.min(diffMinutes, 1440); // Cap at 24 hours worth of minutes
      timeWindow = `${diffMinutes} minutes`;
    } else {
      startTime = new Date(Date.now() - 60 * 60 * 1000); // 1 hour ago
      endTime = new Date();
    }
    
    // Build filter conditions for queries
    const filterConditions = [];
    const filterParams = [];
    let paramCounter = 1;
    
    if (queue) {
      filterConditions.push(`q.name = $${paramCounter}`);
      filterParams.push(queue);
      paramCounter++;
    }
    if (namespace) {
      filterConditions.push(`q.namespace = $${paramCounter}`);
      filterParams.push(namespace);
      paramCounter++;
    }
    if (task) {
      filterConditions.push(`q.task = $${paramCounter}`);
      filterParams.push(task);
      paramCounter++;
    }
    
    const whereClause = filterConditions.length > 0 
      ? `AND ${filterConditions.join(' AND ')}` 
      : '';
    
    try {
      // 1. Incoming messages (created/inserted)
      const incomingQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', $${paramCounter}::timestamp),
            DATE_TRUNC('minute', $${paramCounter + 1}::timestamp),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(m.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages m ON 
          DATE_TRUNC('minute', m.created_at) = ts.minute
          AND m.created_at >= $${paramCounter}::timestamp
          AND m.created_at <= $${paramCounter + 1}::timestamp
        ${whereClause ? `LEFT JOIN queen.partitions p ON m.partition_id = p.id
        LEFT JOIN queen.queues q ON p.queue_id = q.id` : ''}
        ${whereClause ? `WHERE 1=1 ${whereClause}` : ''}
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 2. Completed messages
      const completedQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', $${paramCounter}::timestamp),
            DATE_TRUNC('minute', $${paramCounter + 1}::timestamp),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(ms.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages_status ms ON 
          DATE_TRUNC('minute', ms.completed_at) = ts.minute
          AND ms.completed_at >= $${paramCounter}::timestamp
          AND ms.completed_at <= $${paramCounter + 1}::timestamp
          AND ms.status = 'completed'
          AND ms.consumer_group IS NULL
        ${whereClause ? `LEFT JOIN queen.messages m ON ms.message_id = m.id
        LEFT JOIN queen.partitions p ON m.partition_id = p.id
        LEFT JOIN queen.queues q ON p.queue_id = q.id` : ''}
        ${whereClause ? `WHERE 1=1 ${whereClause}` : ''}
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 3. Processing messages (started processing in this minute)
      const processingQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', $${paramCounter}::timestamp),
            DATE_TRUNC('minute', $${paramCounter + 1}::timestamp),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(ms.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages_status ms ON 
          DATE_TRUNC('minute', ms.processing_at) = ts.minute
          AND ms.processing_at >= $${paramCounter}::timestamp
          AND ms.processing_at <= $${paramCounter + 1}::timestamp
          AND ms.consumer_group IS NULL
        ${whereClause ? `LEFT JOIN queen.messages m ON ms.message_id = m.id
        LEFT JOIN queen.partitions p ON m.partition_id = p.id
        LEFT JOIN queen.queues q ON p.queue_id = q.id` : ''}
        ${whereClause ? `WHERE 1=1 ${whereClause}` : ''}
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 4. Failed messages
      const failedQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', $${paramCounter}::timestamp),
            DATE_TRUNC('minute', $${paramCounter + 1}::timestamp),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(ms.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages_status ms ON 
          DATE_TRUNC('minute', ms.failed_at) = ts.minute
          AND ms.failed_at >= $${paramCounter}::timestamp
          AND ms.failed_at <= $${paramCounter + 1}::timestamp
          AND ms.status = 'failed'
          AND ms.consumer_group IS NULL
        ${whereClause ? `LEFT JOIN queen.messages m ON ms.message_id = m.id
        LEFT JOIN queen.partitions p ON m.partition_id = p.id
        LEFT JOIN queen.queues q ON p.queue_id = q.id` : ''}
        ${whereClause ? `WHERE 1=1 ${whereClause}` : ''}
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 5. Dead letter messages
      const deadLetterQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', $${paramCounter}::timestamp),
            DATE_TRUNC('minute', $${paramCounter + 1}::timestamp),
            '1 minute'::interval
          ) AS minute
        )
        SELECT 
          ts.minute,
          COALESCE(COUNT(ms.id), 0) as count
        FROM time_series ts
        LEFT JOIN queen.messages_status ms ON 
          DATE_TRUNC('minute', ms.failed_at) = ts.minute
          AND ms.failed_at >= $${paramCounter}::timestamp
          AND ms.failed_at <= $${paramCounter + 1}::timestamp
          AND ms.status = 'dead_letter'
          AND ms.consumer_group IS NULL
        ${whereClause ? `LEFT JOIN queen.messages m ON ms.message_id = m.id
        LEFT JOIN queen.partitions p ON m.partition_id = p.id
        LEFT JOIN queen.queues q ON p.queue_id = q.id` : ''}
        ${whereClause ? `WHERE 1=1 ${whereClause}` : ''}
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // 6. Average lag (processing time) per minute
      const lagQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', $${paramCounter}::timestamp),
            DATE_TRUNC('minute', $${paramCounter + 1}::timestamp),
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
          AND ms.completed_at >= $${paramCounter}::timestamp
          AND ms.completed_at <= $${paramCounter + 1}::timestamp
          AND ms.status IN ('completed', 'failed')
          AND ms.completed_at IS NOT NULL
          AND ms.consumer_group IS NULL
        LEFT JOIN queen.messages m ON ms.message_id = m.id
          AND m.created_at IS NOT NULL
        ${whereClause ? `LEFT JOIN queen.partitions p ON m.partition_id = p.id
        LEFT JOIN queen.queues q ON p.queue_id = q.id` : ''}
        ${whereClause ? `WHERE 1=1 ${whereClause}` : ''}
        GROUP BY ts.minute
        ORDER BY ts.minute DESC
        LIMIT ${minuteInterval}
      `;
      
      // Prepare query parameters
      const queryParams = [...filterParams, startTime, endTime];
      
      // Execute all queries in parallel
      const [incoming, completed, processing, failed, deadLetter, lag] = await Promise.all([
        pool.query(incomingQuery, queryParams),
        pool.query(completedQuery, queryParams),
        pool.query(processingQuery, queryParams),
        pool.query(failedQuery, queryParams),
        pool.query(deadLetterQuery, queryParams),
        pool.query(lagQuery, queryParams)
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
    const { fromDateTime, toDateTime, queue, namespace, task } = filters;
    const lagStats = await queueManager.getQueueLag({
      fromDateTime,
      toDateTime,
      queue,
      namespace,
      task
    });
    
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
      
      // Handle case where stats might not exist or be malformed
      const stats = row.stats || {
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
      };
      
      queueData.partitions.push({
        name: row.partition,
        stats: stats
      });
      
      // Aggregate totals (weighted averages for processing times)
      const currentTotal = queueData.totals.completedMessages;
      const newTotal = currentTotal + (stats.completedMessages || 0);
      
      if (newTotal > 0) {
        // Weighted average for processing times
        queueData.totals.avgProcessingTimeSeconds = 
          (queueData.totals.avgProcessingTimeSeconds * currentTotal + 
           (stats.avgProcessingTimeSeconds || 0) * (stats.completedMessages || 0)) / newTotal;
        queueData.totals.medianProcessingTimeSeconds = 
          (queueData.totals.medianProcessingTimeSeconds * currentTotal + 
           (stats.medianProcessingTimeSeconds || 0) * (stats.completedMessages || 0)) / newTotal;
        queueData.totals.p95ProcessingTimeSeconds = 
          Math.max(queueData.totals.p95ProcessingTimeSeconds, stats.p95ProcessingTimeSeconds || 0);
      }
      
      // Sum up counts and lags
      queueData.totals.pendingCount += stats.pendingCount || 0;
      queueData.totals.processingCount += stats.processingCount || 0;
      queueData.totals.totalBacklog += stats.totalBacklog || 0;
      queueData.totals.completedMessages = newTotal;
      queueData.totals.estimatedLagSeconds += stats.estimatedLagSeconds || 0;
      queueData.totals.medianLagSeconds += stats.medianLagSeconds || 0;
      queueData.totals.p95LagSeconds += stats.p95LagSeconds || 0;
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