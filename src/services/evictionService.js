// Functional eviction service for stale messages
import { log, LogTypes } from '../utils/logger.js';
import config from '../config.js';

const EVICTION_INTERVAL = config.JOBS.EVICTION_INTERVAL;

// Evict messages that exceeded max wait time for a queue
const evictMessages = async (client, queueName, maxWaitTimeSeconds) => {
  if (!maxWaitTimeSeconds || maxWaitTimeSeconds <= 0) return 0;
  
  // For the new schema, we need to update or create status entries for evicted messages
  const result = await client.query(`
    WITH queue_partitions AS (
      SELECT p.id
      FROM queen.partitions p
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.name = $1
    ),
    eligible_messages AS (
      SELECT m.id
      FROM queen.messages m
      JOIN queue_partitions qp ON m.partition_id = qp.id
      LEFT JOIN queen.messages_status ms ON m.id = ms.message_id AND ms.consumer_group IS NULL
      WHERE m.created_at < NOW() - INTERVAL '1 second' * $2
        AND (ms.status = 'pending' OR ms.status IS NULL)
    )
    INSERT INTO queen.messages_status (message_id, consumer_group, status, completed_at, error_message)
    SELECT id, NULL, 'evicted', NOW(), 'Message exceeded maximum wait time'
    FROM eligible_messages
    ON CONFLICT (message_id, consumer_group) 
    DO UPDATE SET 
      status = 'evicted',
      completed_at = NOW(),
      error_message = 'Message exceeded maximum wait time'
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
