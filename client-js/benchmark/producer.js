import { Queen } from '../client/client.js'; 
import fs from 'fs';

const TOTAL_MESSAGES = 100000;
const PARTITIONS = 10; 
const MAX_BATCH_SIZE = 1000;  // Reduced from 10000
const PUSH_PARALLEL = 10;      // Reduced from 10 to avoid lock contention
const QUEUE_NAME = 'benchmark-queue-001';

const q = new Queen({
  baseUrls: ['http://localhost:6632'],
  timeout: 120000
});


try {
  await q.queueDelete(QUEUE_NAME);
  await q.queue(QUEUE_NAME, {
  leaseTime: 10,
  retryLimit: 3
}, { namespace: 'benchmark' });
} catch (e) {
  console.error(e);
}



let batches = []
const messagesPerPartition = Math.floor(TOTAL_MESSAGES / PARTITIONS);

// Create batches for each partition
const partitionBatches = [];
for (let i = 0; i < PARTITIONS; i++) {
  partitionBatches[i] = [];
  let messages = [];
  for (let j = 0; j < messagesPerPartition; j++) {
    messages.push({
      id: j,
      //message: `Message ${globalMessageId}`,
      //timestamp: new Date().toISOString()
    });
    if (messages.length >= MAX_BATCH_SIZE) {
      console.log(`Pushing ${messages.length} messages for partition ${i}`);
      partitionBatches[i].push({
        partition: i,
        messages: messages
      });
      messages = [];
    }
  }
  // Only add the remaining batch if it has messages
  if (messages.length > 0) {
    console.log(`Pushing ${messages.length} messages for partition ${i}`);
    partitionBatches[i].push({
      partition: i,
      messages: messages
    });
  }
}

// Interleave batches from different partitions to avoid lock contention
// This ensures parallel pushes go to different partitions
const maxBatchesPerPartition = Math.max(...partitionBatches.map(pb => pb.length));
for (let batchIndex = 0; batchIndex < maxBatchesPerPartition; batchIndex++) {
  for (let partition = 0; partition < PARTITIONS; partition++) {
    if (partitionBatches[partition][batchIndex]) {
      batches.push(partitionBatches[partition][batchIndex]);
    }
  }
}

const start = Date.now();
console.log(`Total batches: ${batches.length}, pushing ${PUSH_PARALLEL} at a time`);
for (let i = 0; i < batches.length; i += PUSH_PARALLEL) {
  const batchGroup = batches.slice(i, i + PUSH_PARALLEL);
  console.log(`Pushing batch group ${Math.floor(i/PUSH_PARALLEL) + 1}/${Math.ceil(batches.length/PUSH_PARALLEL)}: ${batchGroup.map(b => `partition ${b.partition} (${b.messages.length} msgs)`).join(', ')}`);
  await Promise.all(batchGroup.map(batch => q.push(`${QUEUE_NAME}/${batch.partition}`, batch.messages)));
}
const end = Date.now();
console.log(`Pushed ${TOTAL_MESSAGES} messages in ${(end - start) / 1000}s, ${(TOTAL_MESSAGES / ((end - start) / 1000)).toFixed(2)} msg/s`);