// Setup script for Queen MQ
// Creates 5000 queues (one per partition for fair comparison)

import { Queen } from 'queen-mq';
import { config } from '../config.js';

function getPartitionName(index) {
  return `partition-${index}`;
}

const queen = new Queen(config.endpoints.queen);

async function setup() {
  console.log('Setting up Queen MQ...');
  console.log(`Creating ${config.partitionCount} partitions...`);
  
  const queueName = 'benchmark';
  const startTime = Date.now();
  
  // Delete existing queue if present
  try {
    await queen.queue(queueName).delete();
    console.log('Deleted existing benchmark queue');
  } catch (e) {
    // Queue might not exist
  }
  
  // Create queue with benchmark configuration
  await queen.queue(queueName).config({
    leaseTime: 300,
    retryLimit: 3,
    retentionSeconds: 3600,
    encryptionEnabled: false,
  }).create();
  
  console.log('Created benchmark queue');
  
  // Pre-create partitions by pushing a dummy message to each
  // This ensures partitions exist before benchmark starts
  const batchSize = 100;
  for (let i = 0; i < config.partitionCount; i += batchSize) {
    const batch = [];
    for (let j = i; j < Math.min(i + batchSize, config.partitionCount); j++) {
      batch.push({
        transactionId: `setup-${j}`,
        data: { setup: true },
      });
    }
    
    // Push to different partitions
    const promises = batch.map((msg, idx) => 
      queen
        .queue(queueName)
        .partition(getPartitionName(i + idx))
        .push([msg])
    );
    
    await Promise.all(promises);
    
    if ((i + batchSize) % 500 === 0 || i + batchSize >= config.partitionCount) {
      console.log(`Created ${Math.min(i + batchSize, config.partitionCount)} partitions...`);
    }
  }
  
  // Consume and ack all setup messages
  console.log('Cleaning up setup messages...');
  let consumed = 0;
  while (consumed < config.partitionCount) {
    // pop() returns an array of messages directly
    // wait(false) = don't block waiting for messages
    const messages = await queen
      .queue(queueName)
      .wait(false)
      .pop(10000);
    
    if (messages && messages.length > 0) {
      // Ack each message
      for (const msg of messages) {
        await queen.ack(msg.id, true);
      }
      consumed += messages.length;
    } else {
      break;
    }
  }
  
  const elapsed = Date.now() - startTime;
  console.log(`\nQueen setup complete in ${elapsed}ms`);
  console.log(`Created ${config.partitionCount} partitions`);
}

setup().catch(console.error);
