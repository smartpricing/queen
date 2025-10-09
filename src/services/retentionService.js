// Functional retention service for message cleanup

const RETENTION_INTERVAL = parseInt(process.env.RETENTION_INTERVAL) || 300000; // 5 minutes

// Apply retention policy to a single partition
const retentionPartition = async (client, partition) => {
  const { 
    retentionSeconds = 0,
    completedRetentionSeconds = 0,
    retentionEnabled = false 
  } = partition.options || {};
  
  if (!retentionEnabled) return 0;
  
  let totalDeleted = 0;
  
  // Delete old pending messages
  if (retentionSeconds > 0) {
    const result = await client.query(`
      DELETE FROM queen.messages
      WHERE partition_id = $1
        AND status = 'pending'
        AND created_at < NOW() - INTERVAL '1 second' * $2
      RETURNING id
    `, [partition.id, retentionSeconds]);
    
    totalDeleted += result.rowCount || 0;
  }
  
  // Delete completed/failed/evicted messages
  if (completedRetentionSeconds > 0) {
    const result = await client.query(`
      DELETE FROM queen.messages
      WHERE partition_id = $1
        AND status IN ('completed', 'failed', 'evicted')
        AND COALESCE(completed_at, failed_at, created_at) < NOW() - INTERVAL '1 second' * $2
      RETURNING id
    `, [partition.id, completedRetentionSeconds]);
    
    totalDeleted += result.rowCount || 0;
  }
  
  // Log retention if messages were deleted
  if (totalDeleted > 0) {
    await client.query(`
      INSERT INTO queen.retention_history (partition_id, messages_deleted, retention_type)
      VALUES ($1, $2, 'retention')
    `, [partition.id, totalDeleted]);
    
    console.log(`🗑️  Retained ${totalDeleted} messages from ${partition.queue_name}/${partition.name}`);
  }
  
  return totalDeleted;
};

// Clean up empty partitions
const cleanupEmptyPartitions = async (client) => {
  const result = await client.query(`
    DELETE FROM queen.partitions p
    WHERE p.name != 'Default'
      AND (p.options->>'partitionRetentionSeconds')::int > 0
      AND (p.options->>'retentionEnabled')::boolean = true
      AND NOT EXISTS (
        SELECT 1 FROM queen.messages m WHERE m.partition_id = p.id
      )
      AND p.last_activity < NOW() - INTERVAL '1 second' * (p.options->>'partitionRetentionSeconds')::int
    RETURNING id, name
  `);
  
  if (result.rowCount > 0) {
    console.log(`🗑️  Deleted ${result.rowCount} empty partitions`);
  }
  
  return result.rowCount;
};

// Main retention function
const performRetention = async (pool) => {
  const client = await pool.connect();
  
  try {
    // Get partitions with retention enabled
    const partitionsResult = await client.query(`
      SELECT p.id, p.name, p.options, q.name as queue_name
      FROM queen.partitions p
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE (p.options->>'retentionEnabled')::boolean = true
    `);
    
    let totalDeleted = 0;
    for (const partition of partitionsResult.rows) {
      totalDeleted += await retentionPartition(client, partition);
    }
    
    // Cleanup empty partitions
    await cleanupEmptyPartitions(client);
    
    return totalDeleted;
  } catch (error) {
    console.error('Retention error:', error);
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
      console.error('Retention job error:', error);
    }
  }, RETENTION_INTERVAL);
  
  console.log(`♻️  Retention job started (interval: ${RETENTION_INTERVAL}ms)`);
  
  // Return cleanup function
  return () => {
    clearInterval(intervalId);
    console.log('Retention job stopped');
  };
};

// Export for manual execution
export const runRetention = performRetention;
