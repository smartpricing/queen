import { Queen } from '../client/client.js'; 
import fs from 'fs';

const TOTAL_MESSAGES = 100000;
const PARTITIONS = 10;  // Reduced from 100 to allow larger batches per partition
const PUSH_PARALLEL = 10;
const QUEUE_NAME = 'benchmark-queue-01';

const q = new Queen({
  baseUrls: ['http://localhost:6632'],
  timeout: 120000
});

await q.queueDelete(QUEUE_NAME);
await q.queue(QUEUE_NAME, {
  leaseTime: 10,
  retryLimit: 3
}, { namespace: 'benchmark' });



let batches = []
const messagesPerPartition = Math.ceil(TOTAL_MESSAGES / PARTITIONS);
for (let i = 0; i < PARTITIONS; i++) {
  const messages = [];
  for (let j = 0; j < messagesPerPartition; j++) {
    messages.push({
      id: j,
      message: `Message ${i * messagesPerPartition + j}`,
      timestamp: new Date().toISOString()
    });
  }
  batches.push({
    partition: i,
    messages: messages
  })
}

const start = Date.now();
for (let i = 0; i < batches.length; i += PUSH_PARALLEL) {
  await Promise.all(batches.slice(i, i + PUSH_PARALLEL).map(batch => q.push(`${QUEUE_NAME}/${batch.partition}`, batch.messages)));
}
const end = Date.now();
console.log(`Pushed ${TOTAL_MESSAGES} messages in ${(end - start) / 1000}s, ${(TOTAL_MESSAGES / ((end - start) / 1000)).toFixed(2)} msg/s`);