// Functional eviction service for stale messages
import { log, LogTypes } from '../utils/logger.js';
import config from '../config.js';

const EVICTION_INTERVAL = config.JOBS.EVICTION_INTERVAL;

// Evict messages that exceeded max wait time for a queue
// Note: Eviction is already handled by POP query filtering old messages
// This service optionally moves them to DLQ for visibility
const evictMessages = async (client, queueName, maxWaitTimeSeconds) => {
  if (!maxWaitTimeSeconds || maxWaitTimeSeconds <= 0) return 0;
  
  // Find old messages that haven't been consumed and move to DLQ
  const result = await client.query(`
    WITH queue_partitions AS (
      SELECT p.id, p.queue_id
      FROM queen.partitions p
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.name = $1
    ),
    old_unconsumed_messages AS (
      SELECT m.id, m.partition_id, m.created_at
      FROM queen.messages m
      JOIN queue_partitions qp ON m.partition_id = qp.id
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = m.partition_id
        AND pc.consumer_group = '__QUEUE_MODE__'
      WHERE m.created_at < NOW() - INTERVAL '1 second' * $2
        -- Message has not been consumed yet
        AND ((m.created_at, m.id) > (COALESCE(pc.last_consumed_created_at, '1970-01-01'::timestamptz),
                                       COALESCE(pc.last_consumed_id, '00000000-0000-0000-0000-000000000000'::uuid))
             OR pc.last_consumed_id IS NULL)
    )
    INSERT INTO queen.dead_letter_queue (
        message_id, 
        partition_id, 
        consumer_group, 
        error_message, 
        original_created_at
    )
    SELECT 
        om.id,
        om.partition_id,
        '__QUEUE_MODE__',
        'Message exceeded maximum wait time',
        om.created_at
    FROM old_unconsumed_messages om
    ON CONFLICT DO NOTHING
    RETURNING message_id
  `, [queueName, maxWaitTimeSeconds]);
  
  return result.rowCount || 0;
};

// Evict messages during pop operation (inline eviction)
export const evictOnPop = async (client, queueName) => {
  const queueResult = await client.query(
    'SELECT max_wait_time_seconds FROM queen.queues WHERE name = $1',
    [queueName]
  );
  
  if (!queueResult.rows[0]) return 0;
  
  const maxWaitTimeSeconds = queueResult.rows[0].max_wait_time_seconds;
  const evictedCount = await evictMessages(client, queueName, maxWaitTimeSeconds);
  
  if (evictedCount > 0) {
    log(`${LogTypes.EVICTION} | Queue: ${queueName} | Count: ${evictedCount} | MaxWaitTime: ${maxWaitTimeSeconds}s`);
    
    // Log to retention history (reusing the table)
    await client.query(`
      INSERT INTO queen.retention_history (messages_deleted, retention_type)
      VALUES ($1, 'evicted')
    `, [evictedCount]);
  }
  
  return evictedCount;
};

// Background eviction for all queues
const performEviction = async (pool) => {
  const client = await pool.connect();
  
  try {
    // Get all queues with eviction configured
    const queuesResult = await client.query(`
      SELECT name, max_wait_time_seconds
      FROM queen.queues
      WHERE max_wait_time_seconds > 0
    `);
    
    let totalEvicted = 0;
    for (const queue of queuesResult.rows) {
      const evicted = await evictMessages(
        client, 
        queue.name, 
        queue.max_wait_time_seconds
      );
      
      if (evicted > 0) {
        log(`${LogTypes.EVICTION} | Queue: ${queue.name} | Count: ${evicted} | MaxWaitTime: ${queue.max_wait_time_seconds}s`);
        totalEvicted += evicted;
      }
    }
    
    if (totalEvicted > 0) {
      await client.query(`
        INSERT INTO queen.retention_history (messages_deleted, retention_type)
        VALUES ($1, 'evicted')
      `, [totalEvicted]);
    }
    
    return totalEvicted;
  } catch (error) {
    log('Eviction error:', error);
    return 0;
  } finally {
    client.release();
  }
};

// Start the eviction job (optional background job)
export const startEvictionJob = (pool, eventManager) => {
  const intervalId = setInterval(async () => {
    try {
      const evicted = await performEviction(pool);
      if (evicted > 0 && eventManager) {
        eventManager.emit('messages:evicted', { count: evicted });
      }
    } catch (error) {
      log('Eviction job error:', error);
    }
  }, EVICTION_INTERVAL);
  
  log(`⏰ Eviction job started (interval: ${EVICTION_INTERVAL}ms)`);
  
  // Return cleanup function
  return () => {
    clearInterval(intervalId);
    log('Eviction job stopped');
  };
};

// Export for manual execution
export const runEviction = performEviction;
