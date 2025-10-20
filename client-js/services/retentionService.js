// Functional retention service for message cleanup
import { log, LogTypes } from '../utils/logger.js';
import config from '../config.js';

const RETENTION_INTERVAL = config.JOBS.RETENTION_INTERVAL;

// Apply retention policy to a single queue
const retentionQueue = async (client, queue) => {
  const { 
    retention_seconds: retentionSeconds = 0,
    completed_retention_seconds: completedRetentionSeconds = 0,
    retention_enabled: retentionEnabled = false 
  } = queue;
  
  if (!retentionEnabled) return 0;
  
  let totalDeleted = 0;
  
  // Delete old unconsumed messages from all partitions in this queue
  if (retentionSeconds > 0) {
    const result = await client.query(`
      DELETE FROM queen.messages m
      WHERE m.partition_id IN (
        SELECT id FROM queen.partitions WHERE queue_id = $1
      )
      AND m.created_at < NOW() - INTERVAL '1 second' * $2
      AND NOT EXISTS (
          -- Message has been consumed by at least one consumer
          SELECT 1 
          FROM queen.partition_consumers pc
          WHERE pc.partition_id = m.partition_id
            AND (m.created_at, m.id) <= (pc.last_consumed_created_at, pc.last_consumed_id)
      )
      RETURNING id
    `, [queue.id, retentionSeconds]);
    
    totalDeleted += result.rowCount || 0;
  }
  
  // Delete old consumed messages from all partitions in this queue
  // (messages consumed by ALL consumer groups on that partition)
  if (completedRetentionSeconds > 0) {
    const result = await client.query(`
      DELETE FROM queen.messages m
      WHERE m.partition_id IN (
        SELECT id FROM queen.partitions WHERE queue_id = $1
      )
      AND m.created_at < NOW() - INTERVAL '1 second' * $2
      AND NOT EXISTS (
          -- No consumer group that hasn't consumed this message yet
          SELECT 1 
          FROM queen.partition_consumers pc
          WHERE pc.partition_id = m.partition_id
            AND ((m.created_at, m.id) > (pc.last_consumed_created_at, pc.last_consumed_id)
                 OR pc.last_consumed_id IS NULL)
      )
      RETURNING id
    `, [queue.id, completedRetentionSeconds]);
    
    totalDeleted += result.rowCount || 0;
  }
  
  // Log retention if messages were deleted
  if (totalDeleted > 0) {
    log(`${LogTypes.RETENTION} | Queue: ${queue.name} | Count: ${totalDeleted} | RetentionSeconds: ${retentionSeconds} | CompletedRetentionSeconds: ${completedRetentionSeconds}`);
  }
  
  return totalDeleted;
};

// Clean up empty partitions (no longer based on partition-level config)
const cleanupEmptyPartitions = async (client) => {
  // For now, we keep Default partitions and only clean up truly empty non-default partitions
  // that haven't had activity for a long time (e.g., 7 days)
  const result = await client.query(`
    DELETE FROM queen.partitions p
    WHERE p.name != 'Default'
      AND NOT EXISTS (
        SELECT 1 FROM queen.messages m WHERE m.partition_id = p.id
      )
      AND p.last_activity < NOW() - INTERVAL '7 days'
    RETURNING id, name
  `);
  
  if (result.rowCount > 0) {
    log(`🗑️  Deleted ${result.rowCount} empty partitions`);
  }
  
  return result.rowCount;
};

// Clean up old metrics data (messages_consumed table)
const cleanupOldMetrics = async (client) => {
  const retentionDays = config.JOBS.METRICS_RETENTION_DAYS;
  
  // Delete metrics data older than the retention period
  const result = await client.query(`
    DELETE FROM queen.messages_consumed
    WHERE acked_at < NOW() - INTERVAL '1 day' * $1
  `, [retentionDays]);
  
  if (result.rowCount > 0) {
    log(`📊 Deleted ${result.rowCount} old metrics records (older than ${retentionDays} days)`);
  }
  
  return result.rowCount;
};

// Main retention function
const performRetention = async (pool) => {
  const client = await pool.connect();
  
  try {
    // Get queues with retention enabled
    const queuesResult = await client.query(`
      SELECT q.id, q.name, q.retention_enabled, q.retention_seconds, 
             q.completed_retention_seconds
      FROM queen.queues q
      WHERE q.retention_enabled = true
    `);
    
    let totalDeleted = 0;
    for (const queue of queuesResult.rows) {
      totalDeleted += await retentionQueue(client, queue);
    }
    
    // Cleanup empty partitions
    await cleanupEmptyPartitions(client);
    
    // Cleanup old metrics data
    await cleanupOldMetrics(client);
    
    return totalDeleted;
  } catch (error) {
    log('Retention error:', error);
    return 0;
  } finally {
    client.release();
  }
};

// Start the retention job
export const startRetentionJob = (pool) => {
  const intervalId = setInterval(async () => {
    try {
      await performRetention(pool);
    } catch (error) {
      log('Retention job error:', error);
    }
  }, RETENTION_INTERVAL);
  
  log(`♻️  Retention job started (interval: ${RETENTION_INTERVAL}ms)`);
  
  // Return cleanup function
  return () => {
    clearInterval(intervalId);
    log('Retention job stopped');
  };
};

// Export for manual execution
export const runRetention = performRetention;
