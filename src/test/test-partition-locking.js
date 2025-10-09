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

const QUEUE_NAME = 'test-partition-lock';
const PARTITION1 = 'partition-A';
const PARTITION2 = 'partition-B';

async function cleanup() {
  const client = await pool.connect();
  try {
    // Delete test queue and all related data
    await client.query(`DELETE FROM queen.queues WHERE name = $1`, [QUEUE_NAME]);
    log('Cleanup completed');
  } finally {
    client.release();
  }
}

async function setup() {
  // Clean up any existing test data
  await cleanup();
  
  // Create test queue with two partitions
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
    
    // Create two partitions
    await client.query(`
      INSERT INTO queen.partitions (queue_id, name) VALUES 
      ($1, $2),
      ($1, $3)
    `, [queueId, PARTITION1, PARTITION2]);
    
    log('Setup completed: Created queue with 2 partitions');
  } finally {
    client.release();
  }
}

async function pushTestMessages() {
  // Push 3 messages to each partition
  const messages = [];
  
  for (let i = 1; i <= 3; i++) {
    messages.push({
      payload: { data: `Message ${i} for ${PARTITION1}` },
      partition: PARTITION1,
      transactionId: `txn-p1-${i}`
    });
  }
  
  for (let i = 1; i <= 3; i++) {
    messages.push({
      payload: { data: `Message ${i} for ${PARTITION2}` },
      partition: PARTITION2,
      transactionId: `txn-p2-${i}`
    });
  }
  
  // Push all messages
  for (const msg of messages) {
    await queueManager.pushMessages([{
      queue: QUEUE_NAME,
      partition: msg.partition,
      payload: msg.payload,
      transactionId: msg.transactionId
    }]);
  }
  
  log(`Pushed ${messages.length} messages (3 to each partition)`);
}

async function testPartitionLocking() {
  log('\n=== Testing Partition Locking ===\n');
  
  // Consumer 1: Pop without specifying partition
  log('Consumer 1: Popping messages (no partition specified)...');
  const result1 = await queueManager.popMessages(
    { queue: QUEUE_NAME },
    { batch: 2 }
  );
  
  if (result1.messages.length === 0) {
    log('ERROR: Consumer 1 got no messages');
    return false;
  }
  
  const partition1 = result1.messages[0].partition;
  log(`Consumer 1: Got ${result1.messages.length} messages from partition: ${partition1}`);
  result1.messages.forEach(msg => {
    log(`  - ${msg.transactionId}: ${JSON.stringify(msg.payload)}`);
  });
  
  // Consumer 2: Pop without specifying partition (should get different partition)
  log('\nConsumer 2: Popping messages (no partition specified)...');
  const result2 = await queueManager.popMessages(
    { queue: QUEUE_NAME },
    { batch: 2 }
  );
  
  if (result2.messages.length === 0) {
    log('ERROR: Consumer 2 got no messages');
    return false;
  }
  
  const partition2 = result2.messages[0].partition;
  log(`Consumer 2: Got ${result2.messages.length} messages from partition: ${partition2}`);
  result2.messages.forEach(msg => {
    log(`  - ${msg.transactionId}: ${JSON.stringify(msg.payload)}`);
  });
  
  // Verify they got different partitions
  if (partition1 === partition2) {
    log('\n❌ FAILED: Both consumers got the same partition!');
    log('This indicates partition locking is NOT working correctly.');
    return false;
  } else {
    log(`\n✅ SUCCESS: Consumers got different partitions (${partition1} vs ${partition2})`);
    log('Partition locking is working correctly!');
  }
  
  // Consumer 3: Try to pop again (should get no messages as both partitions are locked)
  log('\nConsumer 3: Trying to pop (both partitions should be locked)...');
  const result3 = await queueManager.popMessages(
    { queue: QUEUE_NAME },
    { batch: 2 }
  );
  
  if (result3.messages.length === 0) {
    log('✅ Consumer 3: Got no messages (correct - both partitions are locked)');
  } else {
    log(`❌ Consumer 3: Got ${result3.messages.length} messages (should have been 0)`);
    return false;
  }
  
  // Now ACK messages from Consumer 1 to release its partition
  log('\nConsumer 1: ACKing messages to release partition...');
  for (const msg of result1.messages) {
    await queueManager.acknowledgeMessage(msg.transactionId, 'completed');
  }
  log('Consumer 1: Released partition');
  
  // Consumer 4: Should now be able to get remaining message from Consumer 1's partition
  log('\nConsumer 4: Popping after Consumer 1 released...');
  const result4 = await queueManager.popMessages(
    { queue: QUEUE_NAME },
    { batch: 2 }
  );
  
  if (result4.messages.length > 0) {
    const partition4 = result4.messages[0].partition;
    log(`✅ Consumer 4: Got ${result4.messages.length} messages from partition: ${partition4}`);
    
    // Should be from the same partition as Consumer 1 had
    if (partition4 === partition1) {
      log('✅ Consumer 4 got remaining messages from Consumer 1\'s released partition');
    }
  }
  
  // Clean up: ACK all remaining messages
  log('\nCleaning up: ACKing all remaining messages...');
  for (const msg of result2.messages) {
    await queueManager.acknowledgeMessage(msg.transactionId, 'completed');
  }
  for (const msg of result4.messages) {
    await queueManager.acknowledgeMessage(msg.transactionId, 'completed');
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
    log('Starting Partition Locking Test...\n');
    
    // Setup test environment
    await setup();
    
    // Push test messages
    await pushTestMessages();
    
    // Run the partition locking test
    const success = await testPartitionLocking();
    
    // Check final state of partition leases
    await checkPartitionLeases();
    
    // Cleanup
    await cleanup();
    
    if (success) {
      log('\n🎉 All tests passed! Partition locking is working correctly.');
      process.exit(0);
    } else {
      log('\n❌ Tests failed! Partition locking needs attention.');
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
