import { Queen } from '../src/client/client.js';

const q = new Queen({ baseUrl: 'http://localhost:6632' });

await q.queue('demo-queue', {
  leaseTime: 30,
  retryLimit: 3
});

console.log('=== Example 1: take() - Individual message processing ===');
let count1 = 0;
for await (const message of q.take('demo-queue', { 
  batch: 10,      // Fetch 10 at a time internally
  limit: 25,      // Process 25 total
  wait: false 
})) {
  console.log(`Message ${++count1}:`, message.payload);
  await q.ack(message);
}

console.log('\n=== Example 2: takeBatch() - Batch processing ===');
let count2 = 0;
for await (const messages of q.takeBatch('demo-queue', { 
  batch: 10,      // Fetch 10 at a time
  limit: 25,      // Process 25 total (across batches)
  wait: false 
})) {
  console.log(`Batch ${++count2}: ${messages.length} messages`);
  
  // Process entire batch
  for (const msg of messages) {
    console.log(`  - ${msg.payload}`);
  }
  
  // Acknowledge entire batch at once
  await q.ack(messages);
}

console.log('\n=== Example 3: High-throughput batch processing ===');
let totalProcessed = 0;
const startTime = Date.now();

for await (const messages of q.takeBatch('demo-queue', { 
  batch: 1000,       // Large batches for throughput
  wait: true,        // Long polling
  idleTimeout: 5000  // Exit after 5s of no messages
})) {
  totalProcessed += messages.length;
  await q.ack(messages);
  
  const rate = totalProcessed / ((Date.now() - startTime) / 1000);
  console.log(`Processed ${messages.length} | Total: ${totalProcessed} | Rate: ${rate.toFixed(0)} msg/s`);
}

console.log(`\nCompleted: ${totalProcessed} messages`);

await q.close();

