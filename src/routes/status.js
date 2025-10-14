export const createStatusRoutes = (pool) => {
  
  // Helper to format duration in human-readable format
  const formatDuration = (seconds) => {
    if (!seconds || seconds === 0) return '0s';
    if (seconds < 60) return `${seconds.toFixed(1)}s`;
    if (seconds < 3600) {
      const mins = Math.floor(seconds / 60);
      const secs = Math.floor(seconds % 60);
      return `${mins}m ${secs}s`;
    }
    if (seconds < 86400) {
      const hours = Math.floor(seconds / 3600);
      const mins = Math.floor((seconds % 3600) / 60);
      return `${hours}h ${mins}m`;
    }
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    return `${days}d ${hours}h`;
  };
  
  // Helper to get default time range (last hour)
  const getTimeRange = (from, to) => {
    const toDate = to ? new Date(to) : new Date();
    const fromDate = from ? new Date(from) : new Date(toDate.getTime() - 60 * 60 * 1000);
    return { from: fromDate, to: toDate };
  };
  
  // 1. Dashboard Status Endpoint
  const getStatus = async (filters = {}) => {
    const { from, to, queue, namespace, task } = filters;
    const timeRange = getTimeRange(from, to);
    
    // Build filter conditions
    const buildFilters = (startParam = 3) => {
      const conditions = [];
      const params = [timeRange.from, timeRange.to];
      let paramCount = startParam;
      
      if (queue) {
        conditions.push(`q.name = $${paramCount}`);
        params.push(queue);
        paramCount++;
      }
      if (namespace) {
        conditions.push(`q.namespace = $${paramCount}`);
        params.push(namespace);
        paramCount++;
      }
      if (task) {
        conditions.push(`q.task = $${paramCount}`);
        params.push(task);
        paramCount++;
      }
      
      return { conditions, params, whereClause: conditions.length > 0 ? `AND ${conditions.join(' AND ')}` : '' };
    };
    
    try {
      // Query 1: Throughput per minute
      const filterInfo = buildFilters(3);
      const throughputQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('minute', $1::timestamptz),
            DATE_TRUNC('minute', $2::timestamptz),
            '1 minute'::interval
          ) AS minute
        ),
        ingested_per_minute AS (
          SELECT 
            DATE_TRUNC('minute', m.created_at) as minute,
            COUNT(DISTINCT m.id) as messages_ingested
          FROM queen.messages m
          JOIN queen.partitions p ON p.id = m.partition_id
          JOIN queen.queues q ON q.id = p.queue_id
          WHERE m.created_at >= $1::timestamptz
            AND m.created_at <= $2::timestamptz
            ${filterInfo.whereClause}
          GROUP BY DATE_TRUNC('minute', m.created_at)
        )
        SELECT 
          ts.minute,
          COALESCE(ipm.messages_ingested, 0) as messages_ingested,
          0 as messages_processed
        FROM time_series ts
        LEFT JOIN ingested_per_minute ipm ON ipm.minute = ts.minute
        ORDER BY ts.minute DESC
      `;
      
      // Query 2: Active queues in time range
      const queuesQuery = `
        SELECT 
          q.id, q.name, q.namespace, q.task,
          COUNT(DISTINCT p.id) as partition_count,
          COALESCE(SUM(pc.total_messages_consumed), 0) as total_consumed
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
          AND pc.consumer_group = '__QUEUE_MODE__'
        LEFT JOIN queen.messages m ON m.partition_id = p.id
          AND m.created_at >= $1 AND m.created_at <= $2
        WHERE 1=1 ${filterInfo.whereClause}
        GROUP BY q.id, q.name, q.namespace, q.task
        HAVING COUNT(DISTINCT m.id) > 0
        ORDER BY q.name
      `;
      
      // Query 3: Message counts (using partition_consumers estimates)
      const countsQuery = `
        SELECT 
          COUNT(DISTINCT m.id) as total_messages,
          (SELECT COALESCE(SUM(pending_estimate), 0)::integer 
           FROM queen.partition_consumers 
           WHERE consumer_group = '__QUEUE_MODE__') as pending,
          (SELECT COUNT(*) 
           FROM queen.partition_consumers 
           WHERE consumer_group = '__QUEUE_MODE__' 
           AND lease_expires_at IS NOT NULL 
           AND lease_expires_at > NOW()) as processing,
          (SELECT COALESCE(SUM(total_messages_consumed), 0)::integer 
           FROM queen.partition_consumers 
           WHERE consumer_group = '__QUEUE_MODE__') as completed,
          0 as failed,
          (SELECT COUNT(*) 
           FROM queen.dead_letter_queue 
           WHERE consumer_group IS NULL OR consumer_group = '__QUEUE_MODE__') as dead_letter
        FROM queen.messages m
        LEFT JOIN queen.partitions p ON p.id = m.partition_id
        LEFT JOIN queen.queues q ON q.id = p.queue_id
        WHERE m.created_at >= $1 AND m.created_at <= $2
          ${filterInfo.whereClause}
      `;
      
      // Query 4: Active leases
      const leasesQuery = `
        SELECT 
          COUNT(*) as active_leases,
          COUNT(DISTINCT partition_id) as partitions_with_leases,
          SUM(batch_size) as total_batch_size,
          SUM(acked_count) as total_acked
        FROM queen.partition_consumers
        WHERE lease_expires_at IS NOT NULL AND lease_expires_at > NOW()
      `;
      
      // Query 5: DLQ stats
      const dlqQuery = `
        SELECT 
          COUNT(*) as total_dlq_messages,
          COUNT(DISTINCT partition_id) as affected_partitions,
          error_message,
          COUNT(*) as error_count
        FROM queen.dead_letter_queue dlq
        LEFT JOIN queen.partitions p ON p.id = dlq.partition_id
        LEFT JOIN queen.queues q ON q.id = p.queue_id
        WHERE dlq.failed_at >= $1 AND dlq.failed_at <= $2
          ${filterInfo.whereClause}
        GROUP BY error_message
        ORDER BY error_count DESC
        LIMIT 5
      `;
      
      // Execute all queries in parallel
      const [throughputResult, queuesResult, countsResult, leasesResult, dlqResult] = await Promise.all([
        pool.query(throughputQuery, filterInfo.params),
        pool.query(queuesQuery, filterInfo.params),
        pool.query(countsQuery, filterInfo.params),
        pool.query(leasesQuery),
        pool.query(dlqQuery, filterInfo.params)
      ]);
      
      // Format throughput data
      const throughput = throughputResult.rows.map(row => ({
        timestamp: row.minute,
        ingested: parseInt(row.messages_ingested),
        processed: parseInt(row.messages_processed),
        ingestedPerSecond: parseFloat((parseInt(row.messages_ingested) / 60).toFixed(2)),
        processedPerSecond: parseFloat((parseInt(row.messages_processed) / 60).toFixed(2))
      }));
      
      // Format queues data
      const queues = queuesResult.rows.map(row => ({
        id: row.id,
        name: row.name,
        namespace: row.namespace,
        task: row.task,
        partitions: parseInt(row.partition_count),
        totalConsumed: parseInt(row.total_consumed || 0)
      }));
      
      // Format message counts
      const counts = countsResult.rows[0] || {};
      const messages = {
        total: parseInt(counts.total_messages || 0),
        pending: parseInt(counts.pending || 0),
        processing: parseInt(counts.processing || 0),
        completed: parseInt(counts.completed || 0),
        failed: parseInt(counts.failed || 0),
        deadLetter: parseInt(counts.dead_letter || 0)
      };
      
      // Format leases data
      const leaseData = leasesResult.rows[0] || {};
      const leases = {
        active: parseInt(leaseData.active_leases || 0),
        partitionsWithLeases: parseInt(leaseData.partitions_with_leases || 0),
        totalBatchSize: parseInt(leaseData.total_batch_size || 0),
        totalAcked: parseInt(leaseData.total_acked || 0)
      };
      
      // Get DLQ totals and top errors
      const dlqTotal = dlqResult.rows.reduce((sum, row) => sum + parseInt(row.error_count || 0), 0);
      const dlqPartitions = new Set(dlqResult.rows.map(row => row.partition_id)).size;
      const topErrors = dlqResult.rows
        .filter(row => row.error_message)
        .map(row => ({
          error: row.error_message,
          count: parseInt(row.error_count)
        }));
      
      return {
        timeRange: {
          from: timeRange.from.toISOString(),
          to: timeRange.to.toISOString()
        },
        throughput,
        queues,
        messages,
        leases,
        deadLetterQueue: {
          totalMessages: dlqTotal,
          affectedPartitions: dlqPartitions,
          topErrors
        }
      };
      
    } catch (error) {
      console.error('Error in getStatus:', error);
      throw error;
    }
  };
  
  // 2. Queues List Endpoint
  const getQueues = async (filters = {}) => {
    const { from, to, namespace, task, limit = 100, offset = 0 } = filters;
    const timeRange = getTimeRange(from, to);
    
    const conditions = [];
    const params = [timeRange.from, timeRange.to];
    let paramCount = 3;
    
    if (namespace) {
      conditions.push(`q.namespace = $${paramCount}`);
      params.push(namespace);
      paramCount++;
    }
    if (task) {
      conditions.push(`q.task = $${paramCount}`);
      params.push(task);
      paramCount++;
    }
    
    params.push(limit, offset);
    const whereClause = conditions.length > 0 ? `AND ${conditions.join(' AND ')}` : '';
    
    const query = `
      SELECT 
        q.id,
        q.name,
        q.namespace,
        q.task,
        q.priority,
        q.created_at,
        COUNT(DISTINCT p.id) as partition_count,
        COUNT(DISTINCT m.id) as total_messages,
        COALESCE(SUM(DISTINCT pc.pending_estimate), 0) as pending,
        COUNT(DISTINCT CASE WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN p.id END) as processing,
        COALESCE(SUM(DISTINCT pc.total_messages_consumed), 0) as completed,
        0 as failed,
        (SELECT COUNT(*) FROM queen.dead_letter_queue dlq 
         WHERE dlq.consumer_group = '__QUEUE_MODE__') as dead_letter,
        (SELECT EXTRACT(EPOCH FROM (NOW() - MIN(m2.created_at)))
         FROM queen.messages m2
         JOIN queen.partitions p2 ON p2.id = m2.partition_id
         LEFT JOIN queen.partition_consumers pc2 ON pc2.partition_id = p2.id 
           AND pc2.consumer_group = '__QUEUE_MODE__'
         WHERE p2.queue_id = q.id
           AND (m2.created_at, m2.id) > (COALESCE(pc2.last_consumed_created_at, '1970-01-01'::timestamptz),
                                          COALESCE(pc2.last_consumed_id, '00000000-0000-0000-0000-000000000000'::uuid))
        ) as lag_seconds,
        0 as avg_processing_time_seconds
      FROM queen.queues q
      LEFT JOIN queen.partitions p ON p.queue_id = q.id
      LEFT JOIN queen.messages m ON m.partition_id = p.id 
        AND m.created_at >= $1 AND m.created_at <= $2
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
        AND pc.consumer_group = '__QUEUE_MODE__'
      WHERE 1=1 ${whereClause}
      GROUP BY q.id, q.name, q.namespace, q.task, q.priority, q.created_at
      ORDER BY q.priority DESC, q.name
      LIMIT $${paramCount} OFFSET $${paramCount + 1}
    `;
    
    const countQuery = `
      SELECT COUNT(DISTINCT q.id) as total
      FROM queen.queues q
      WHERE 1=1 ${whereClause.replace(/\$\d+/g, (match) => {
        const num = parseInt(match.substring(1));
        return num <= 2 ? match : `$${num - 2}`;
      })}
    `;
    
    const [result, countResult] = await Promise.all([
      pool.query(query, params),
      pool.query(countQuery, params.slice(2, -2))
    ]);
    
    const queues = result.rows.map(row => ({
      id: row.id,
      name: row.name,
      namespace: row.namespace,
      task: row.task,
      priority: row.priority,
      createdAt: row.created_at,
      partitions: parseInt(row.partition_count),
      messages: {
        total: parseInt(row.total_messages || 0),
        pending: parseInt(row.pending || 0),
        processing: parseInt(row.processing || 0),
        completed: parseInt(row.completed || 0),
        failed: parseInt(row.failed || 0),
        deadLetter: parseInt(row.dead_letter || 0)
      },
      lag: row.lag_seconds ? {
        seconds: parseFloat(row.lag_seconds),
        formatted: formatDuration(parseFloat(row.lag_seconds))
      } : null,
      performance: row.avg_processing_time_seconds ? {
        avgProcessingTimeSeconds: parseFloat(row.avg_processing_time_seconds),
        avgProcessingTimeFormatted: formatDuration(parseFloat(row.avg_processing_time_seconds))
      } : null
    }));
    
    return {
      queues,
      pagination: {
        limit: parseInt(limit),
        offset: parseInt(offset),
        total: parseInt(countResult.rows[0]?.total || 0)
      },
      timeRange: {
        from: timeRange.from.toISOString(),
        to: timeRange.to.toISOString()
      }
    };
  };
  
  // 3. Queue Detail Endpoint
  const getQueueDetail = async (queueName, filters = {}) => {
    const { from, to } = filters;
    const timeRange = getTimeRange(from, to);
    
    const query = `
      SELECT 
        q.id, q.name, q.namespace, q.task, q.priority,
        q.lease_time, q.retry_limit, q.ttl, q.max_queue_size,
        q.created_at,
        p.id as partition_id,
        p.name as partition_name,
        p.created_at as partition_created_at,
        p.last_activity as partition_last_activity,
        COUNT(DISTINCT m.id) as partition_total_messages,
        COALESCE(pc.pending_estimate, 0) as partition_pending,
        CASE WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 1 ELSE 0 END as partition_processing,
        COALESCE(pc.total_messages_consumed, 0) as partition_completed,
        0 as partition_failed,
        pc.total_messages_consumed,
        pc.total_batches_consumed,
        pc.last_consumed_at,
        pc.lease_expires_at,
        pc.batch_size as lease_batch_size,
        pc.acked_count as lease_acked_count,
        (SELECT MIN(created_at) FROM queen.messages m2 
         WHERE m2.partition_id = p.id
           AND (m2.created_at, m2.id) > (COALESCE(pc.last_consumed_created_at, '1970-01-01'::timestamptz),
                                          COALESCE(pc.last_consumed_id, '00000000-0000-0000-0000-000000000000'::uuid))
        ) as oldest_pending,
        MAX(m.created_at) as newest_message
      FROM queen.queues q
      LEFT JOIN queen.partitions p ON p.queue_id = q.id
      LEFT JOIN queen.messages m ON m.partition_id = p.id 
        AND m.created_at >= $2 AND m.created_at <= $3
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
        AND pc.consumer_group = '__QUEUE_MODE__'
      WHERE q.name = $1
      GROUP BY q.id, q.name, q.namespace, q.task, q.priority, q.lease_time, 
               q.retry_limit, q.ttl, q.max_queue_size, q.created_at,
               p.id, p.name, p.created_at, p.last_activity,
               pc.total_messages_consumed, pc.total_batches_consumed, pc.last_consumed_at,
               pc.lease_expires_at, pc.batch_size, pc.acked_count, pc.pending_estimate, pc.last_consumed_created_at, pc.last_consumed_id
      ORDER BY p.name
    `;
    
    const result = await pool.query(query, [queueName, timeRange.from, timeRange.to]);
    
    if (result.rows.length === 0) {
      throw new Error('Queue not found');
    }
    
    const firstRow = result.rows[0];
    const queue = {
      id: firstRow.id,
      name: firstRow.name,
      namespace: firstRow.namespace,
      task: firstRow.task,
      priority: firstRow.priority,
      config: {
        leaseTime: firstRow.lease_time,
        retryLimit: firstRow.retry_limit,
        ttl: firstRow.ttl,
        maxQueueSize: firstRow.max_queue_size
      },
      createdAt: firstRow.created_at
    };
    
    // Aggregate totals
    const totals = {
      messages: {
        total: 0,
        pending: 0,
        processing: 0,
        completed: 0,
        failed: 0
      },
      partitions: result.rows.filter(r => r.partition_id).length,
      consumed: 0,
      batches: 0
    };
    
    const partitions = result.rows
      .filter(row => row.partition_id)
      .map(row => {
        const total = parseInt(row.partition_total_messages || 0);
        const pending = parseInt(row.partition_pending || 0);
        const processing = parseInt(row.partition_processing || 0);
        const completed = parseInt(row.partition_completed || 0);
        const failed = parseInt(row.partition_failed || 0);
        
        totals.messages.total += total;
        totals.messages.pending += pending;
        totals.messages.processing += processing;
        totals.messages.completed += completed;
        totals.messages.failed += failed;
        totals.consumed += parseInt(row.total_messages_consumed || 0);
        totals.batches += parseInt(row.total_batches_consumed || 0);
        
        const partition = {
          id: row.partition_id,
          name: row.partition_name,
          createdAt: row.partition_created_at,
          lastActivity: row.partition_last_activity,
          messages: {
            total,
            pending,
            processing,
            completed,
            failed
          },
          cursor: {
            totalConsumed: parseInt(row.total_messages_consumed || 0),
            batchesConsumed: parseInt(row.total_batches_consumed || 0),
            lastConsumedAt: row.last_consumed_at
          }
        };
        
        if (row.lease_expires_at) {
          partition.lease = {
            active: new Date(row.lease_expires_at) > new Date(),
            expiresAt: row.lease_expires_at,
            batchSize: parseInt(row.lease_batch_size || 0),
            ackedCount: parseInt(row.lease_acked_count || 0)
          };
        }
        
        if (row.oldest_pending) {
          const lagSeconds = (new Date() - new Date(row.oldest_pending)) / 1000;
          partition.lag = {
            oldestPending: row.oldest_pending,
            lagSeconds: parseFloat(lagSeconds.toFixed(1))
          };
        }
        
        return partition;
      });
    
    return {
      queue,
      totals,
      partitions,
      timeRange: {
        from: timeRange.from.toISOString(),
        to: timeRange.to.toISOString()
      }
    };
  };
  
  // 4. Queue Messages Endpoint
  const getQueueMessages = async (queueName, filters = {}) => {
    const { status, partition, from, to, limit = 50, offset = 0 } = filters;
    const timeRange = getTimeRange(from, to);
    
    const conditions = ['q.name = $1', 'm.created_at >= $2', 'm.created_at <= $3'];
    const params = [queueName, timeRange.from, timeRange.to];
    let paramCount = 4;
    
    // Note: V2 schema doesn't have a messages_status table
    // Status must be inferred from partition_consumers and dead_letter_queue
    if (status) {
      if (status === 'failed') {
        conditions.push(`EXISTS (SELECT 1 FROM queen.dead_letter_queue dlq WHERE dlq.message_id = m.id)`);
      } else if (status === 'completed') {
        conditions.push(`(m.created_at, m.id) <= (pc.last_consumed_created_at, pc.last_consumed_id)`);
      } else if (status === 'pending') {
        conditions.push(`(m.created_at, m.id) > (COALESCE(pc.last_consumed_created_at, '1970-01-01'::timestamptz), COALESCE(pc.last_consumed_id, '00000000-0000-0000-0000-000000000000'::uuid))`);
      }
    }
    
    if (partition) {
      conditions.push(`p.name = $${paramCount}`);
      params.push(partition);
      paramCount++;
    }
    
    params.push(limit, offset);
    
    const query = `
      SELECT 
        m.id,
        m.transaction_id,
        m.trace_id,
        m.created_at,
        m.is_encrypted,
        m.payload,
        p.name as partition,
        CASE 
          WHEN EXISTS (SELECT 1 FROM queen.dead_letter_queue dlq WHERE dlq.message_id = m.id) THEN 'failed'
          WHEN (m.created_at, m.id) <= (COALESCE(pc.last_consumed_created_at, '1970-01-01'::timestamptz), COALESCE(pc.last_consumed_id, '00000000-0000-0000-0000-000000000000'::uuid)) THEN 'completed'
          WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 'processing'
          ELSE 'pending'
        END as status,
        pc.worker_id,
        pc.lease_acquired_at as locked_at,
        pc.last_consumed_at as completed_at,
        dlq.failed_at,
        dlq.error_message,
        dlq.retry_count,
        pc.lease_acquired_at as processing_at,
        NULL::numeric as processing_time_seconds,
        EXTRACT(EPOCH FROM (NOW() - m.created_at)) as age_seconds
      FROM queen.messages m
      JOIN queen.partitions p ON p.id = m.partition_id
      JOIN queen.queues q ON q.id = p.queue_id
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
        AND pc.consumer_group = '__QUEUE_MODE__'
      LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
        AND dlq.consumer_group = '__QUEUE_MODE__'
      WHERE ${conditions.join(' AND ')}
      ORDER BY m.created_at DESC
      LIMIT $${paramCount} OFFSET $${paramCount + 1}
    `;
    
    const countQuery = `
      SELECT COUNT(*) as total
      FROM queen.messages m
      JOIN queen.partitions p ON p.id = m.partition_id
      JOIN queen.queues q ON q.id = p.queue_id
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
        AND pc.consumer_group = '__QUEUE_MODE__'
      LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
        AND dlq.consumer_group = '__QUEUE_MODE__'
      WHERE ${conditions.join(' AND ')}
    `;
    
    const [result, countResult] = await Promise.all([
      pool.query(query, params),
      pool.query(countQuery, params.slice(0, -2))
    ]);
    
    const messages = result.rows.map(row => {
      const message = {
        id: row.id,
        transactionId: row.transaction_id,
        traceId: row.trace_id,
        partition: row.partition,
        createdAt: row.created_at,
        status: row.status,
        retryCount: row.retry_count || 0,
        isEncrypted: row.is_encrypted
      };
      
      if (!row.is_encrypted) {
        message.payload = row.payload;
      }
      
      if (row.status === 'pending' || row.status === 'processing') {
        message.age = {
          seconds: parseFloat(row.age_seconds),
          formatted: formatDuration(parseFloat(row.age_seconds))
        };
      }
      
      if (row.status === 'completed' || row.status === 'failed') {
        message.completedAt = row.completed_at || row.failed_at;
        if (row.processing_time_seconds) {
          message.processingTime = {
            seconds: parseFloat(row.processing_time_seconds),
            formatted: formatDuration(parseFloat(row.processing_time_seconds))
          };
        }
      }
      
      if (row.worker_id) {
        message.workerId = row.worker_id;
      }
      
      if (row.error_message) {
        message.errorMessage = row.error_message;
      }
      
      return message;
    });
    
    return {
      messages,
      pagination: {
        limit: parseInt(limit),
        offset: parseInt(offset),
        total: parseInt(countResult.rows[0]?.total || 0)
      },
      queue: queueName,
      filters: {
        status: status || null,
        partition: partition || null
      },
      timeRange: {
        from: timeRange.from.toISOString(),
        to: timeRange.to.toISOString()
      }
    };
  };
  
  // 5. Analytics Endpoint
  const getAnalytics = async (filters = {}) => {
    const { from, to, queue, namespace, task, interval = 'hour' } = filters;
    
    // Default to last 24 hours for analytics
    const toDate = to ? new Date(to) : new Date();
    const fromDate = from ? new Date(from) : new Date(toDate.getTime() - 24 * 60 * 60 * 1000);
    
    // Determine interval
    const intervalMap = {
      'minute': '1 minute',
      'hour': '1 hour',
      'day': '1 day'
    };
    const sqlInterval = intervalMap[interval] || '1 hour';
    
    const buildFilters = (startParam = 3) => {
      const conditions = [];
      const params = [fromDate, toDate];
      let paramCount = startParam;
      
      if (queue) {
        conditions.push(`q.name = $${paramCount}`);
        params.push(queue);
        paramCount++;
      }
      if (namespace) {
        conditions.push(`q.namespace = $${paramCount}`);
        params.push(namespace);
        paramCount++;
      }
      if (task) {
        conditions.push(`q.task = $${paramCount}`);
        params.push(task);
        paramCount++;
      }
      
      return { conditions, params, whereClause: conditions.length > 0 ? `AND ${conditions.join(' AND ')}` : '' };
    };
    
    const filterInfo = buildFilters(3);
    
    try {
      // Query 1: Throughput time series
      // Note: V2 schema doesn't track per-message consumption timestamps,
      // so we can only show ingested messages accurately. "Processed" would
      // require tracking individual message acks with timestamps.
      const throughputQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('${interval}', $1::timestamptz),
            DATE_TRUNC('${interval}', $2::timestamptz),
            '${sqlInterval}'::interval
          ) AS bucket
        ),
        ingested_counts AS (
          SELECT 
            DATE_TRUNC('${interval}', m.created_at) as bucket,
            COUNT(DISTINCT m.id) as ingested
          FROM queen.messages m
          JOIN queen.partitions p ON p.id = m.partition_id
          JOIN queen.queues q ON q.id = p.queue_id
          WHERE m.created_at >= $1 
            AND m.created_at <= $2
            ${filterInfo.whereClause}
          GROUP BY DATE_TRUNC('${interval}', m.created_at)
        ),
        failed_counts AS (
          SELECT 
            DATE_TRUNC('${interval}', dlq.failed_at) as bucket,
            COUNT(*) as failed
          FROM queen.dead_letter_queue dlq
          JOIN queen.partitions p ON p.id = dlq.partition_id
          JOIN queen.queues q ON q.id = p.queue_id
          WHERE dlq.failed_at >= $1::timestamptz 
            AND dlq.failed_at <= $2::timestamptz
            AND (dlq.consumer_group IS NULL OR dlq.consumer_group = '__QUEUE_MODE__')
            ${filterInfo.whereClause}
          GROUP BY DATE_TRUNC('${interval}', dlq.failed_at)
        )
        SELECT 
          ts.bucket as timestamp,
          COALESCE(ic.ingested, 0) as ingested,
          0 as processed,
          COALESCE(fc.failed, 0) as failed
        FROM time_series ts
        LEFT JOIN ingested_counts ic ON ic.bucket = ts.bucket
        LEFT JOIN failed_counts fc ON fc.bucket = ts.bucket
        ORDER BY ts.bucket DESC
      `;
      
      // Query 2: Latency percentiles time series
      // Note: In V2 schema, we don't track individual message completion times
      // This would require additional instrumentation. For now, return empty latency data.
      const latencyQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('${interval}', $1::timestamptz),
            DATE_TRUNC('${interval}', $2::timestamptz),
            '${sqlInterval}'::interval
          ) AS bucket
        )
        SELECT 
          ts.bucket as timestamp,
          NULL::numeric as p50,
          NULL::numeric as p95,
          NULL::numeric as p99,
          NULL::numeric as avg,
          NULL::numeric as min,
          NULL::numeric as max,
          0 as sample_count
        FROM time_series ts
        ORDER BY ts.bucket DESC
      `;
      
      // Query 3: Top queues by volume (based on ingested messages in time range)
      const topQueuesQuery = `
        SELECT 
          q.name,
          q.namespace,
          COUNT(DISTINCT m.id) as messages_ingested,
          COALESCE(SUM(DISTINCT pc.total_messages_consumed), 0) as total_consumed_lifetime,
          COUNT(DISTINCT dlq.id) as failed_in_range,
          COUNT(DISTINCT dlq.id)::float / NULLIF(COUNT(DISTINCT m.id), 0) as error_rate
        FROM queen.queues q
        JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.messages m ON m.partition_id = p.id
          AND m.created_at >= $1 AND m.created_at <= $2
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
          AND pc.consumer_group = '__QUEUE_MODE__'
        LEFT JOIN queen.dead_letter_queue dlq ON dlq.partition_id = p.id
          AND dlq.consumer_group = '__QUEUE_MODE__'
          AND dlq.failed_at >= $1 AND dlq.failed_at <= $2
        WHERE 1=1 ${filterInfo.whereClause}
        GROUP BY q.id, q.name, q.namespace
        HAVING COUNT(DISTINCT m.id) > 0
        ORDER BY messages_ingested DESC
        LIMIT 10
      `;
      
      // Query 4: DLQ time series
      const dlqQuery = `
        WITH time_series AS (
          SELECT generate_series(
            DATE_TRUNC('${interval}', $1::timestamptz),
            DATE_TRUNC('${interval}', $2::timestamptz),
            '${sqlInterval}'::interval
          ) AS bucket
        ),
        dlq_counts AS (
          SELECT 
            DATE_TRUNC('${interval}', dlq.failed_at) as bucket,
            COUNT(*) as messages
          FROM queen.dead_letter_queue dlq
          JOIN queen.partitions p ON p.id = dlq.partition_id
          JOIN queen.queues q ON q.id = p.queue_id
          WHERE dlq.failed_at >= $1::timestamptz 
            AND dlq.failed_at <= $2::timestamptz
            AND (dlq.consumer_group IS NULL OR dlq.consumer_group = '__QUEUE_MODE__')
            ${filterInfo.whereClause}
          GROUP BY DATE_TRUNC('${interval}', dlq.failed_at)
        )
        SELECT 
          ts.bucket as timestamp,
          COALESCE(dc.messages, 0) as messages
        FROM time_series ts
        LEFT JOIN dlq_counts dc ON dc.bucket = ts.bucket
        ORDER BY ts.bucket DESC
      `;
      
      // Query 5: DLQ top errors
      const dlqErrorsQuery = `
        SELECT 
          error_message,
          COUNT(*) as count
        FROM queen.dead_letter_queue dlq
        LEFT JOIN queen.partitions p ON p.id = dlq.partition_id
        LEFT JOIN queen.queues q ON q.id = p.queue_id
        WHERE dlq.failed_at >= $1 AND dlq.failed_at <= $2
          AND (dlq.consumer_group IS NULL OR dlq.consumer_group = '__QUEUE_MODE__')
          ${filterInfo.whereClause}
        GROUP BY error_message
        ORDER BY count DESC
        LIMIT 10
      `;
      
      // Execute all queries in parallel
      const [throughputResult, latencyResult, topQueuesResult, dlqResult, dlqErrorsResult] = await Promise.all([
        pool.query(throughputQuery, filterInfo.params),
        pool.query(latencyQuery, filterInfo.params.slice(0, 2)),
        pool.query(topQueuesQuery, filterInfo.params),
        pool.query(dlqQuery, filterInfo.params),
        pool.query(dlqErrorsQuery, filterInfo.params)
      ]);
      
      // Calculate seconds per bucket for rate calculations
      const secondsPerBucket = interval === 'minute' ? 60 : interval === 'hour' ? 3600 : 86400;
      
      // Format throughput data
      const throughputTimeSeries = throughputResult.rows.map(row => ({
        timestamp: row.timestamp,
        ingested: parseInt(row.ingested || 0),
        processed: parseInt(row.processed || 0),
        failed: parseInt(row.failed || 0),
        ingestedPerSecond: parseFloat((parseInt(row.ingested || 0) / secondsPerBucket).toFixed(2)),
        processedPerSecond: parseFloat((parseInt(row.processed || 0) / secondsPerBucket).toFixed(2))
      }));
      
      const throughputTotals = throughputTimeSeries.reduce((acc, row) => ({
        ingested: acc.ingested + row.ingested,
        processed: acc.processed + row.processed,
        failed: acc.failed + row.failed
      }), { ingested: 0, processed: 0, failed: 0 });
      
      const totalSeconds = ((toDate - fromDate) / 1000);
      throughputTotals.avgIngestedPerSecond = parseFloat((throughputTotals.ingested / totalSeconds).toFixed(2));
      throughputTotals.avgProcessedPerSecond = parseFloat((throughputTotals.processed / totalSeconds).toFixed(2));
      
      // Format latency data
      const latencyTimeSeries = latencyResult.rows
        .filter(row => row.sample_count > 0)
        .map(row => ({
          timestamp: row.timestamp,
          p50: parseFloat(row.p50 || 0),
          p95: parseFloat(row.p95 || 0),
          p99: parseFloat(row.p99 || 0),
          avg: parseFloat(row.avg || 0),
          min: parseFloat(row.min || 0),
          max: parseFloat(row.max || 0)
        }));
      
      // Calculate overall latency
      const allSamples = latencyResult.rows.filter(row => row.sample_count > 0);
      const latencyOverall = allSamples.length > 0 ? {
        p50: parseFloat((allSamples.reduce((sum, row) => sum + parseFloat(row.p50 || 0), 0) / allSamples.length).toFixed(2)),
        p95: parseFloat((allSamples.reduce((sum, row) => sum + parseFloat(row.p95 || 0), 0) / allSamples.length).toFixed(2)),
        p99: parseFloat((allSamples.reduce((sum, row) => sum + parseFloat(row.p99 || 0), 0) / allSamples.length).toFixed(2)),
        avg: parseFloat((allSamples.reduce((sum, row) => sum + parseFloat(row.avg || 0), 0) / allSamples.length).toFixed(2))
      } : null;
      
      // Format error rates
      const errorRatesTimeSeries = throughputTimeSeries.map(row => {
        const total = row.processed + row.failed;
        const rate = total > 0 ? row.failed / total : 0;
        return {
          timestamp: row.timestamp,
          failed: row.failed,
          processed: row.processed,
          rate: parseFloat(rate.toFixed(4)),
          ratePercent: `${(rate * 100).toFixed(2)}%`
        };
      });
      
      const errorRatesOverall = {
        failed: throughputTotals.failed,
        processed: throughputTotals.processed,
        rate: throughputTotals.processed > 0 ? 
          parseFloat((throughputTotals.failed / throughputTotals.processed).toFixed(4)) : 0
      };
      errorRatesOverall.ratePercent = `${(errorRatesOverall.rate * 100).toFixed(2)}%`;
      
      // Format top queues
      const topQueues = topQueuesResult.rows.map(row => ({
        name: row.name,
        namespace: row.namespace,
        messagesIngested: parseInt(row.messages_ingested || 0),
        totalConsumedLifetime: parseInt(row.total_consumed_lifetime || 0),
        failedInRange: parseInt(row.failed_in_range || 0),
        errorRate: parseFloat(row.error_rate || 0)
      }));
      
      // Format DLQ data
      const dlqTimeSeries = dlqResult.rows.map(row => ({
        timestamp: row.timestamp,
        messages: parseInt(row.messages || 0)
      }));
      
      const dlqTotal = dlqTimeSeries.reduce((sum, row) => sum + row.messages, 0);
      const totalErrors = dlqErrorsResult.rows.reduce((sum, row) => sum + parseInt(row.count || 0), 0);
      
      const dlqTopErrors = dlqErrorsResult.rows.map(row => ({
        error: row.error_message,
        count: parseInt(row.count || 0),
        percentage: totalErrors > 0 ? parseFloat(((parseInt(row.count || 0) / totalErrors) * 100).toFixed(1)) : 0
      }));
      
      // Calculate queue depths (current state)
      const depthFilters = buildFilters(1);
      const depthQuery = `
        SELECT 
          COALESCE(SUM(pc.pending_estimate), 0) as pending,
          COALESCE(SUM(CASE WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN pc.batch_size ELSE 0 END), 0) as processing
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
          AND pc.consumer_group = '__QUEUE_MODE__'
        WHERE 1=1 ${depthFilters.whereClause}
      `;
      
      const depthResult = await pool.query(depthQuery, depthFilters.params.slice(2));
      const currentDepth = {
        pending: parseInt(depthResult.rows[0]?.pending || 0),
        processing: parseInt(depthResult.rows[0]?.processing || 0)
      };
      
      return {
        timeRange: {
          from: fromDate.toISOString(),
          to: toDate.toISOString(),
          interval
        },
        throughput: {
          timeSeries: throughputTimeSeries,
          totals: throughputTotals
        },
        latency: {
          timeSeries: latencyTimeSeries,
          overall: latencyOverall
        },
        errorRates: {
          timeSeries: errorRatesTimeSeries,
          overall: errorRatesOverall
        },
        queueDepths: {
          timeSeries: [], // Could add historical depth tracking if needed
          current: currentDepth
        },
        topQueues,
        deadLetterQueue: {
          timeSeries: dlqTimeSeries,
          total: dlqTotal,
          topErrors: dlqTopErrors
        }
      };
      
    } catch (error) {
      console.error('Error in getAnalytics:', error);
      throw error;
    }
  };
  
  return {
    getStatus,
    getQueues,
    getQueueDetail,
    getQueueMessages,
    getAnalytics
  };
};

