#!/usr/bin/env node

import pg from 'pg';
import config from '../config.js';
import { createOptimizedQueueManager } from '../managers/queueManagerOptimized.js';
import { createResourceCache } from '../managers/resourceCache.js';
import { createEventManager } from '../managers/eventManager.js';
import { log } from '../utils/logger.js';

const pool = new pg.Pool({
  host: config.DATABASE.HOST,
  port: config.DATABASE.PORT,
  database: config.DATABASE.NAME,
  user: config.DATABASE.USER,
  password: config.DATABASE.PASSWORD,
  max: config.DATABASE.POOL_SIZE
});

const resourceCache = createResourceCache();
const eventManager = createEventManager();
const queueManager = createOptimizedQueueManager(pool, resourceCache, eventManager);

const QUEUE_NAME = 'test-specific-partition';
const PARTITION_NAME = 'specific-partition';
const CONSUMER_GROUP = 'test-group';

async function cleanup() {
  const client = await pool.connect();
  try {
    await client.query(`DELETE FROM queen.queues WHERE name = $1`, [QUEUE_NAME]);
    log('Cleanup completed');
  } finally {
    client.release();
  }
}

async function setup() {
  await cleanup();
  
  const client = await pool.connect();
  try {
    await client.query(`
      INSERT INTO queen.queues (name, lease_time, retry_limit) 
      VALUES ($1, 30, 3)
    `, [QUEUE_NAME]);
    
    const queueResult = await client.query(
      `SELECT id FROM queen.queues WHERE name = $1`,
      [QUEUE_NAME]
    );
    const queueId = queueResult.rows[0].id;
    
    // Create one partition
    await client.query(`
      INSERT INTO queen.partitions (queue_id, name) VALUES ($1, $2)
    `, [queueId, PARTITION_NAME]);
    
    log('Setup completed: Created queue with specific partition');
  } finally {
    client.release();
  }
}

async function pushTestMessages() {
  const messages = [];
  
  for (let i = 1; i <= 5; i++) {
    messages.push({
      payload: { data: `Message ${i}` },
      partition: PARTITION_NAME,
      transactionId: `txn-${i}`
    });
  }
  
  for (const msg of messages) {
    await queueManager.pushMessages([{
      queue: QUEUE_NAME,
      partition: msg.partition,
      payload: msg.payload,
      transactionId: msg.transactionId
    }]);
  }
  
  log(`Pushed ${messages.length} messages to ${PARTITION_NAME}`);
}

async function testQueueModeSpecificPartition() {
  log('\n=== Testing Queue Mode with Specific Partition ===\n');
  
  // Consumer 1: Request specific partition
  log('Consumer 1: Requesting specific partition...');
  const result1 = await queueManager.popMessages(
    { queue: QUEUE_NAME, partition: PARTITION_NAME },
    { batch: 2 }
  );
  
  if (result1.messages.length === 0) {
    log('ERROR: Consumer 1 got no messages');
    return false;
  }
  
  log(`Consumer 1: Got ${result1.messages.length} messages from partition: ${result1.messages[0].partition}`);
  result1.messages.forEach(msg => {
    log(`  - ${msg.transactionId}: ${JSON.stringify(msg.payload)}`);
  });
  
  // Consumer 2: Try to request same specific partition (should get nothing due to lock)
  log('\nConsumer 2: Requesting same specific partition...');
  const result2 = await queueManager.popMessages(
    { queue: QUEUE_NAME, partition: PARTITION_NAME },
    { batch: 2 }
  );
  
  if (result2.messages.length === 0) {
    log('✅ Consumer 2: Got no messages (correct - partition is locked by Consumer 1)');
  } else {
    log(`❌ Consumer 2: Got ${result2.messages.length} messages (should have been 0 due to lock)`);
    return false;
  }
  
  // ACK messages from Consumer 1
  log('\nConsumer 1: ACKing messages...');
  for (const msg of result1.messages) {
    await queueManager.acknowledgeMessage(msg.transactionId, 'completed');
  }
  
  // Consumer 3: Should now get messages from the specific partition
  log('\nConsumer 3: Requesting specific partition after release...');
  const result3 = await queueManager.popMessages(
    { queue: QUEUE_NAME, partition: PARTITION_NAME },
    { batch: 2 }
  );
  
  if (result3.messages.length > 0) {
    log(`✅ Consumer 3: Got ${result3.messages.length} messages after lease release`);
    
    // Clean up
    for (const msg of result3.messages) {
      await queueManager.acknowledgeMessage(msg.transactionId, 'completed');
    }
  } else {
    log('❌ Consumer 3: Got no messages (should have gotten remaining messages)');
    return false;
  }
  
  return true;
}

async function testBusModeSpecificPartition() {
  log('\n=== Testing Bus Mode with Specific Partition ===\n');
  
  // Push fresh messages
  await pushTestMessages();
  
  // Group Alpha, Consumer 1: Request specific partition
  log('Group Alpha, Consumer 1: Requesting specific partition...');
  const result1 = await queueManager.popMessages(
    { queue: QUEUE_NAME, partition: PARTITION_NAME, consumerGroup: CONSUMER_GROUP },
    { batch: 2 }
  );
  
  if (result1.messages.length === 0) {
    log('ERROR: Group Alpha Consumer 1 got no messages');
    return false;
  }
  
  log(`Group Alpha Consumer 1: Got ${result1.messages.length} messages`);
  
  // Group Alpha, Consumer 2: Try same specific partition (should be locked)
  log('\nGroup Alpha, Consumer 2: Requesting same specific partition...');
  const result2 = await queueManager.popMessages(
    { queue: QUEUE_NAME, partition: PARTITION_NAME, consumerGroup: CONSUMER_GROUP },
    { batch: 2 }
  );
  
  if (result2.messages.length === 0) {
    log('✅ Group Alpha Consumer 2: Got no messages (correct - partition locked by Consumer 1)');
  } else {
    log(`❌ Group Alpha Consumer 2: Got ${result2.messages.length} messages (should be 0)`);
    return false;
  }
  
  // Different Group, Consumer 1: Should get messages (different consumer group)
  log('\nGroup Beta, Consumer 1: Requesting same partition (different group)...');
  const result3 = await queueManager.popMessages(
    { queue: QUEUE_NAME, partition: PARTITION_NAME, consumerGroup: 'group-beta' },
    { batch: 2 }
  );
  
  if (result3.messages.length > 0) {
    log(`✅ Group Beta Consumer 1: Got ${result3.messages.length} messages (different group works)`);
  } else {
    log('❌ Group Beta should be able to access partition (different group)');
    return false;
  }
  
  // Clean up
  log('\nCleaning up...');
  for (const msg of result1.messages) {
    await queueManager.acknowledgeMessage(msg.transactionId, 'completed', null, CONSUMER_GROUP);
  }
  for (const msg of result3.messages) {
    await queueManager.acknowledgeMessage(msg.transactionId, 'completed', null, 'group-beta');
  }
  
  return true;
}

async function checkPartitionLeases() {
  log('\n=== Checking Partition Leases ===\n');
  
  const client = await pool.connect();
  try {
    const result = await client.query(`
      SELECT 
        q.name as queue_name,
        p.name as partition_name,
        pl.consumer_group,
        pl.lease_expires_at,
        pl.released_at,
        CASE 
          WHEN pl.released_at IS NOT NULL THEN 'Released'
          WHEN pl.lease_expires_at < NOW() THEN 'Expired'
          ELSE 'Active'
        END as status
      FROM queen.partition_leases pl
      JOIN queen.partitions p ON pl.partition_id = p.id
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.name = $1
      ORDER BY pl.created_at DESC
    `, [QUEUE_NAME]);
    
    if (result.rows.length === 0) {
      log('No partition leases found');
    } else {
      log('Partition Leases:');
      result.rows.forEach(row => {
        log(`  - ${row.partition_name}: ${row.status} (Consumer: ${row.consumer_group})`);
      });
    }
  } finally {
    client.release();
  }
}

async function main() {
  try {
    log('Starting Specific Partition Locking Test...\n');
    
    // Setup
    await setup();
    await pushTestMessages();
    
    // Test queue mode
    const queueModeSuccess = await testQueueModeSpecificPartition();
    
    // Check leases after queue mode
    await checkPartitionLeases();
    
    // Test bus mode
    const busModeSuccess = await testBusModeSpecificPartition();
    
    // Check final state
    await checkPartitionLeases();
    
    // Cleanup
    await cleanup();
    
    if (queueModeSuccess && busModeSuccess) {
      log('\n🎉 All tests passed! Specific partition locking works correctly.');
      process.exit(0);
    } else {
      log('\n❌ Some tests failed.');
      process.exit(1);
    }
  } catch (error) {
    log('Test error:', error);
    await cleanup();
    process.exit(1);
  } finally {
    await pool.end();
  }
}

// Run the test
main();
