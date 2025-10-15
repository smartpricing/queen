import { Queen } from '../client/client.js'; 
import fs from 'fs';

const TOTAL_MESSAGES = 100000;
const NUM_QUEUES = 50;          // X: Number of queues
const PARTITIONS_PER_QUEUE = 10; // Y: Partitions per queue
const MAX_BATCH_SIZE = 1;  
const PUSH_PARALLEL = 1;      

const q = new Queen({
  baseUrls: ['http://localhost:6632'],
  timeout: 120000
});

// Create X queues, each with Y partitions, all in 'benchmark' namespace
console.log(`Creating ${NUM_QUEUES} queues with ${PARTITIONS_PER_QUEUE} partitions each...`);
for (let queueIdx = 0; queueIdx < NUM_QUEUES; queueIdx++) {
  const queueName = `benchmark-queue-${String(queueIdx + 1).padStart(2, '0')}`;
  try {
    //await q.queueDelete(queueName);
  } catch (e) {
    // Queue might not exist
  }
  await q.queue(queueName, {
    leaseTime: 10,
    retryLimit: 3
  }, { namespace: 'benchmark' });
  console.log(`  Created queue: ${queueName}`);
}


// Calculate total partitions across all queues
const TOTAL_PARTITIONS = NUM_QUEUES * PARTITIONS_PER_QUEUE;
const messagesPerPartition = Math.floor(TOTAL_MESSAGES / TOTAL_PARTITIONS);

console.log(`\nDistributing ${TOTAL_MESSAGES} messages across ${NUM_QUEUES} queues × ${PARTITIONS_PER_QUEUE} partitions = ${TOTAL_PARTITIONS} total partitions`);
console.log(`Messages per partition: ${messagesPerPartition}\n`);

// Create batches for each queue/partition combination
const queuePartitionBatches = [];
for (let queueIdx = 0; queueIdx < NUM_QUEUES; queueIdx++) {
  for (let partitionIdx = 0; partitionIdx < PARTITIONS_PER_QUEUE; partitionIdx++) {
    const key = `q${queueIdx}_p${partitionIdx}`;
    queuePartitionBatches.push({ queueIdx, partitionIdx, batches: [] });
    
    let messages = [];
    for (let msgIdx = 0; msgIdx < messagesPerPartition; msgIdx++) {
      messages.push({
        id: msgIdx,
        queue: queueIdx,
        partition: partitionIdx
      });
      
      if (messages.length >= MAX_BATCH_SIZE) {
        queuePartitionBatches[queuePartitionBatches.length - 1].batches.push([...messages]);
        messages = [];
      }
    }
    
    // Add remaining messages
    if (messages.length > 0) {
      queuePartitionBatches[queuePartitionBatches.length - 1].batches.push([...messages]);
    }
  }
}

// Flatten and interleave batches to distribute load evenly
let batches = [];
const maxBatchesPerQueuePartition = Math.max(...queuePartitionBatches.map(qp => qp.batches.length));
for (let batchIdx = 0; batchIdx < maxBatchesPerQueuePartition; batchIdx++) {
  for (const qp of queuePartitionBatches) {
    if (qp.batches[batchIdx]) {
      batches.push({
        queueIdx: qp.queueIdx,
        partitionIdx: qp.partitionIdx,
        messages: qp.batches[batchIdx]
      });
    }
  }
}

const start = Date.now();
console.log(`Total batches: ${batches.length}, pushing ${PUSH_PARALLEL} at a time\n`);

for (let i = 0; i < batches.length; i += PUSH_PARALLEL) {
  const batchGroup = batches.slice(i, i + PUSH_PARALLEL);
  const batchGroupNum = Math.floor(i/PUSH_PARALLEL) + 1;
  const totalBatchGroups = Math.ceil(batches.length/PUSH_PARALLEL);
  
  console.log(`[${batchGroupNum}/${totalBatchGroups}] Pushing: ${batchGroup.map(b => {
    const queueName = `benchmark-queue-${String(b.queueIdx + 1).padStart(2, '0')}`;
    return `${queueName}/p${b.partitionIdx} (${b.messages.length} msgs)`;
  }).join(', ')}`);
  
  await Promise.all(batchGroup.map(batch => {
    const queueName = `benchmark-queue-${String(batch.queueIdx + 1).padStart(2, '0')}`;
    return q.push(`${queueName}/${batch.partitionIdx}`, batch.messages);
  }));
}

const end = Date.now();
const duration = (end - start) / 1000;
const throughput = (TOTAL_MESSAGES / duration).toFixed(2);

console.log('\n' + '='.repeat(80));
console.log('📤 PRODUCER RESULTS');
console.log('='.repeat(80));
console.log(`Total Messages Pushed: ${TOTAL_MESSAGES.toLocaleString()}`);
console.log(`Queues: ${NUM_QUEUES}`);
console.log(`Partitions per Queue: ${PARTITIONS_PER_QUEUE}`);
console.log(`Total Partitions: ${TOTAL_PARTITIONS}`);
console.log(`Duration: ${duration.toFixed(2)}s`);
console.log(`Throughput: ${throughput} msg/s`);
console.log('='.repeat(80));

