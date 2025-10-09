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

const QUEUE_NAME = 'test-bus-partition-lock';
const PARTITION1 = 'partition-X';
const PARTITION2 = 'partition-Y';
const CONSUMER_GROUP1 = 'group-alpha';
const CONSUMER_GROUP2 = 'group-beta';

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
      transactionId: `txn-px-${i}`
    });
  }
  
  for (let i = 1; i <= 3; i++) {
    messages.push({
      payload: { data: `Message ${i} for ${PARTITION2}` },
      partition: PARTITION2,
      transactionId: `txn-py-${i}`
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

async function testBusPartitionLocking() {
  log('\n=== Testing Bus Mode Partition Locking ===\n');
  
  // Group Alpha, Consumer 1: Pop without specifying partition
  log('Group Alpha, Consumer 1: Popping messages...');
  const result1 = await queueManager.popMessages(
    { queue: QUEUE_NAME, consumerGroup: CONSUMER_GROUP1 },
    { batch: 2 }
  );
  
  if (result1.messages.length === 0) {
    log('ERROR: Group Alpha Consumer 1 got no messages');
    return false;
  }
  
  const partition1 = result1.messages[0].partition;
  log(`Group Alpha Consumer 1: Got ${result1.messages.length} messages from partition: ${partition1}`);
  result1.messages.forEach(msg => {
    log(`  - ${msg.transactionId}: ${JSON.stringify(msg.payload)}`);
  });
  
  // Group Alpha, Consumer 2: Pop without specifying partition (should get different partition if locking works)
  log('\nGroup Alpha, Consumer 2: Popping messages...');
  const result2 = await queueManager.popMessages(
    { queue: QUEUE_NAME, consumerGroup: CONSUMER_GROUP1 },
    { batch: 2 }
  );
  
  if (result2.messages.length === 0) {
    log('Group Alpha Consumer 2 got no messages (might indicate locking is working)');
  } else {
    const partition2 = result2.messages[0].partition;
    log(`Group Alpha Consumer 2: Got ${result2.messages.length} messages from partition: ${partition2}`);
    
    if (partition1 === partition2) {
      log('⚠️  WARNING: Both consumers in same group got same partition!');
      log('This means partition locking is NOT implemented for bus mode.');
      return false;
    } else {
      log('✅ Consumers in same group got different partitions');
    }
  }
  
  // Group Beta, Consumer 1: Should be able to get messages from any partition (different consumer group)
  log('\nGroup Beta, Consumer 1: Popping messages (different consumer group)...');
  const result3 = await queueManager.popMessages(
    { queue: QUEUE_NAME, consumerGroup: CONSUMER_GROUP2 },
    { batch: 2 }
  );
  
  if (result3.messages.length > 0) {
    const partition3 = result3.messages[0].partition;
    log(`✅ Group Beta Consumer 1: Got ${result3.messages.length} messages from partition: ${partition3}`);
    log('Different consumer groups can access same partitions independently (correct)');
  }
  
  // Clean up: ACK all messages
  log('\nCleaning up: ACKing all messages...');
  for (const msg of result1.messages) {
    await queueManager.acknowledgeMessage(msg.transactionId, 'completed', null, CONSUMER_GROUP1);
  }
  for (const msg of result2.messages) {
    await queueManager.acknowledgeMessage(msg.transactionId, 'completed', null, CONSUMER_GROUP1);
  }
  for (const msg of result3.messages) {
    await queueManager.acknowledgeMessage(msg.transactionId, 'completed', null, CONSUMER_GROUP2);
  }
  
  return true;
}

async function checkPartitionLeases() {
  log('\n=== Checking Partition Leases for Bus Mode ===\n');
  
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
      ORDER BY pl.consumer_group, pl.created_at DESC
    `, [QUEUE_NAME]);
    
    if (result.rows.length === 0) {
      log('No partition leases found for bus mode');
      log('⚠️  This indicates partition locking is NOT implemented for bus mode');
    } else {
      log('Partition Leases:');
      result.rows.forEach(row => {
        log(`  - ${row.partition_name}: ${row.status} (Consumer Group: ${row.consumer_group})`);
      });
    }
  } finally {
    client.release();
  }
}

async function main() {
  try {
    log('Starting Bus Mode Partition Locking Test...\n');
    
    // Setup test environment
    await setup();
    
    // Push test messages
    await pushTestMessages();
    
    // Run the bus mode partition locking test
    const success = await testBusPartitionLocking();
    
    // Check final state of partition leases
    await checkPartitionLeases();
    
    // Cleanup
    await cleanup();
    
    if (success) {
      log('\n📋 Test completed. Check results above.');
      process.exit(0);
    } else {
      log('\n❌ Bus mode partition locking needs implementation.');
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
