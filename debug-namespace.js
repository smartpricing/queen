import pg from 'pg';
import config from './src/config.js';

const pool = new pg.Pool({
  host: config.DATABASE.HOST,
  port: config.DATABASE.PORT,
  database: config.DATABASE.NAME,
  user: config.DATABASE.USER,
  password: config.DATABASE.PASSWORD
});

async function debug() {
  try {
    // Check if test queues have namespace/task set
    console.log('=== Checking queues with namespace ===');
    const queues = await pool.query(`
      SELECT name, namespace, task 
      FROM queen.queues 
      WHERE namespace IS NOT NULL OR task IS NOT NULL
    `);
    console.log('Queues with namespace/task:', queues.rows);
    
    // Check messages in those queues
    console.log('\n=== Checking messages in namespaced queues ===');
    const messages = await pool.query(`
      SELECT m.id, m.transaction_id, q.name as queue_name, q.namespace, q.task, p.name as partition_name
      FROM queen.messages m
      JOIN queen.partitions p ON m.partition_id = p.id
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.namespace IS NOT NULL
      LIMIT 10
    `);
    console.log('Messages in namespaced queues:', messages.rows);
    
    // Test the actual query used in popMessagesWithFilters
    console.log('\n=== Testing popMessagesWithFilters query ===');
    const actualConsumerGroup = '__QUEUE_MODE__';
    const namespace = 'test-namespace';
    const batch = 3;
    
    const testQuery = `
      WITH available_messages AS (
        SELECT m.id, m.transaction_id, m.trace_id, m.payload, m.is_encrypted, m.created_at,
               p.id as partition_id, p.name as partition_name, q.name as queue_name,
               q.priority, q.lease_time, q.retry_limit, q.delayed_processing,
               q.window_buffer, q.max_wait_time_seconds, q.namespace, q.task
        FROM queen.messages m
        JOIN queen.partitions p ON m.partition_id = p.id
        JOIN queen.queues q ON p.queue_id = q.id
        LEFT JOIN queen.messages_status ms ON m.id = ms.message_id AND ms.consumer_group = $1
        WHERE (ms.id IS NULL OR ms.status = 'pending')
          AND m.created_at <= NOW() - INTERVAL '1 second' * q.delayed_processing
          AND (q.max_wait_time_seconds = 0 OR 
               m.created_at > NOW() - INTERVAL '1 second' * q.max_wait_time_seconds)
          AND (q.window_buffer = 0 OR NOT EXISTS (
            SELECT 1 FROM queen.messages m2 
            WHERE m2.partition_id = p.id 
              AND m2.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
          ))
          AND NOT EXISTS (
            -- Check for active partition leases
            SELECT 1 FROM queen.partition_leases pl
            WHERE pl.partition_id = p.id
              AND pl.consumer_group = $1
              AND pl.released_at IS NULL
              AND pl.lease_expires_at > NOW()
          )
          AND q.namespace = $2
        ORDER BY q.priority DESC, m.created_at ASC LIMIT $3 FOR UPDATE OF m SKIP LOCKED
      )
      SELECT * FROM available_messages
    `;
    
    const result = await pool.query(testQuery, [actualConsumerGroup, namespace, batch]);
    console.log('Query result count:', result.rows.length);
    if (result.rows.length > 0) {
      console.log('Sample message:', result.rows[0]);
    }
    
    // Check for any blocking issues
    console.log('\n=== Checking for blocking issues ===');
    const statusCheck = await pool.query(`
      SELECT ms.*, m.transaction_id
      FROM queen.messages_status ms
      JOIN queen.messages m ON ms.message_id = m.id
      WHERE ms.consumer_group = $1
      AND ms.status != 'completed'
      LIMIT 10
    `, [actualConsumerGroup]);
    console.log('Pending/processing messages:', statusCheck.rows);
    
    const leaseCheck = await pool.query(`
      SELECT pl.*, p.name as partition_name, q.name as queue_name
      FROM queen.partition_leases pl
      JOIN queen.partitions p ON pl.partition_id = p.id
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE pl.consumer_group = $1
      AND pl.released_at IS NULL
      AND pl.lease_expires_at > NOW()
    `, [actualConsumerGroup]);
    console.log('Active partition leases:', leaseCheck.rows);
    
  } catch (error) {
    console.error('Error:', error);
  } finally {
    await pool.end();
  }
}

debug();
