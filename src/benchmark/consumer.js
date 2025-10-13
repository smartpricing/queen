import { Queen } from '../client/client.js';
import fs from 'fs';

const QUEUE_NAME = 'benchmark-queue-01';
const NUMBER_OF_CONSUMERS = 10;
const BATCH_SIZE = 10000;
const CONSUME_MODE = 'partition';

// Global metrics tracking
const metrics = {
  startTime: null,
  endTime: null,
  totalProcessed: 0,
  consumers: [],
  batchTimes: [],
  ackTimes: [],
  popTimes: [],
  firstMessageTime: null,
  lastMessageTime: null
};

async function consumer(consumerId, partition) {
  const q = new Queen({
    baseUrls: ['http://localhost:6632'],
    timeout: 30000
  });
  
  const consumerMetrics = {
    consumerId,
    partition,
    messagesProcessed: 0,
    batchesProcessed: 0,
    batchTimes: [],
    ackTimes: [],
    popTimes: [],
    startTime: Date.now(),
    endTime: null
  };

  const target = CONSUME_MODE === 'partition' ? `${QUEUE_NAME}/${partition}` : `${QUEUE_NAME}`;
  
  metrics.consumers.push(consumerMetrics);
  if (target === `${QUEUE_NAME}`) {
    console.log(`[Consumer ${consumerId}] Started, consuming all partitions`);
  } else {
    console.log(`[Consumer ${consumerId}] Started, consuming partition ${partition}`);
  }
  
  try {
    for await (const messages of q.takeBatch(target, { //`namespace:benchmark`, 
      wait: true,
      timeout: 30000,
      batch: BATCH_SIZE,
      idleTimeout: 5000
    })) {
      if (!metrics.firstMessageTime) {
        metrics.firstMessageTime = Date.now();
      }
      metrics.lastMessageTime = Date.now();
      const batchStartTime = Date.now();
      const popTime = batchStartTime - (consumerMetrics.endTime || consumerMetrics.startTime);
      consumerMetrics.popTimes.push(popTime);
      metrics.popTimes.push(popTime);
      
      try {
        // Validate message ordering
        const partitionLastIds = new Map();
        
        for (const message of messages) {
          if (!message.data || typeof message.data.id !== 'number') {
            throw new Error(`Message missing or invalid data.id: ${JSON.stringify(message)}`);
          }
          
          const partition = message.partition || 'default';
          const messageId = message.data.id;
          
          if (partitionLastIds.has(partition)) {
            const lastId = partitionLastIds.get(partition);
            if (messageId !== lastId + 1) {
              console.log(message.partition, message);
              throw new Error(`Message ordering violation in partition '${partition}': expected id ${lastId + 1}, got ${messageId}`);
            }
          }
          
          partitionLastIds.set(partition, messageId);
        }

        // Acknowledge all messages
        const ackStartTime = Date.now();
        q.ack(messages);
        const ackTime = Date.now() - ackStartTime;
        consumerMetrics.ackTimes.push(ackTime);
        metrics.ackTimes.push(ackTime);
        
        const batchEndTime = Date.now();
        const batchDuration = batchEndTime - batchStartTime;
        
        consumerMetrics.messagesProcessed += messages.length;
        consumerMetrics.batchesProcessed++;
        consumerMetrics.batchTimes.push(batchDuration);
        consumerMetrics.endTime = batchEndTime;
        
        metrics.totalProcessed += messages.length;
        metrics.batchTimes.push(batchDuration);
        
        const overallDuration = (batchEndTime - metrics.startTime) / 1000;
        const currentRate = (metrics.totalProcessed / overallDuration).toFixed(0);
        
        console.log(new Date().toISOString(), `[C${consumerId}] Batch: ${messages.length} msgs | Time: ${(batchDuration/1000).toFixed(3)}s (pop:${(popTime/1000).toFixed(3)}s ack:${(ackTime/1000).toFixed(3)}s) | Total: ${metrics.totalProcessed} | Rate: ${currentRate} msg/s`);
        metrics.lastMessageTime = Date.now();
      } catch (error) {
        console.error(new Date().toISOString(), `[Consumer ${consumerId}] Error processing batch:`, error);
        await q.ack(messages, false, { error: error.message });
      }
    }
  } catch (error) {
    console.error(`[Consumer ${consumerId}] Fatal error:`, error);
  }
  
  consumerMetrics.endTime = Date.now();
  console.log(`[Consumer ${consumerId}] Finished. Processed ${consumerMetrics.messagesProcessed} messages in ${consumerMetrics.batchesProcessed} batches`);
}

// Start metrics
metrics.startTime = Date.now();

// Launch all consumers
const consumerPromises = [];
for (let i = 0; i < NUMBER_OF_CONSUMERS; i++) {
  consumerPromises.push(consumer(i, i));
}

// Wait for all consumers to complete
await Promise.all(consumerPromises);
metrics.endTime = Date.now();

// Calculate actual processing time (excluding idle timeout)
// Find the time when the last batch was actually completed
let lastBatchCompletedTime = metrics.startTime;
metrics.consumers.forEach(c => {
  if (c.endTime && c.batchesProcessed > 0) {
    // c.endTime is when consumer exited (after idle timeout)
    // Actual last batch time = c.endTime - (idle timeout we didn't track)
    // Better: use the sum of all processing times
    const totalBatchTime = c.batchTimes.reduce((a,b)=>a+b,0);
    const totalPopTime = c.popTimes.reduce((a,b)=>a+b,0);
    const actualEndTime = c.startTime + totalBatchTime + totalPopTime;
    if (actualEndTime > lastBatchCompletedTime) {
      lastBatchCompletedTime = actualEndTime;
    }
  }
});

const totalDuration = (metrics.endTime - metrics.startTime) / 1000;
const actualProcessingDuration = (lastBatchCompletedTime - metrics.startTime) / 1000;
const avgThroughput = (metrics.totalProcessed / actualProcessingDuration).toFixed(0);

console.log('\n' + '='.repeat(80));
console.log('📊 BENCHMARK RESULTS');
console.log('='.repeat(80));
console.log(`Total Messages Processed: ${metrics.totalProcessed.toLocaleString()}`);
console.log(`Total Wall Clock Time: ${totalDuration.toFixed(2)}s (includes idle timeout)`);
console.log(`Actual Processing Time: ${actualProcessingDuration.toFixed(2)}s`);
console.log(`Idle/Wait Time: ${(totalDuration - actualProcessingDuration).toFixed(2)}s`);
console.log(`Average Throughput: ${avgThroughput} msg/s (based on processing time)`);
console.log(`Number of Consumers: ${NUMBER_OF_CONSUMERS}`);
console.log(`Total Batches: ${metrics.batchTimes.length}`);
console.log(`Total Time: ${metrics.lastMessageTime - metrics.firstMessageTime}ms`);
console.log(`Total msg/s: ${metrics.totalProcessed / (metrics.lastMessageTime - metrics.firstMessageTime) * 1000} msg/s`);

if (metrics.batchTimes.length > 0) {
  const avgBatchTime = (metrics.batchTimes.reduce((a, b) => a + b, 0) / metrics.batchTimes.length / 1000).toFixed(3);
  const minBatchTime = (Math.min(...metrics.batchTimes) / 1000).toFixed(3);
  const maxBatchTime = (Math.max(...metrics.batchTimes) / 1000).toFixed(3);
  
  console.log(`\nBatch Times:`);
  console.log(`  Average: ${avgBatchTime}s`);
  console.log(`  Min: ${minBatchTime}s`);
  console.log(`  Max: ${maxBatchTime}s`);
}

if (metrics.popTimes.length > 0) {
  const avgPopTime = (metrics.popTimes.reduce((a, b) => a + b, 0) / metrics.popTimes.length / 1000).toFixed(3);
  console.log(`\nAverage POP Time: ${avgPopTime}s`);
}

if (metrics.ackTimes.length > 0) {
  const avgAckTime = (metrics.ackTimes.reduce((a, b) => a + b, 0) / metrics.ackTimes.length / 1000).toFixed(3);
  console.log(`Average ACK Time: ${avgAckTime}s`);
}

console.log(`\nPer-Consumer Stats:`);
metrics.consumers.forEach(c => {
  // Calculate actual processing time (sum of all batch times + pop times)
  const totalBatchTime = c.batchTimes.reduce((a,b)=>a+b,0);
  const totalPopTime = c.popTimes.reduce((a,b)=>a+b,0);
  const actualProcessingTime = (totalBatchTime + totalPopTime) / 1000;
  
  const wallClockTime = ((c.endTime - c.startTime) / 1000).toFixed(2);
  const rate = (c.messagesProcessed / actualProcessingTime).toFixed(0);
  const avgBatch = c.batchTimes.length > 0 ? (totalBatchTime / c.batchTimes.length / 1000).toFixed(3) : 'N/A';
  const idleTime = (wallClockTime - actualProcessingTime).toFixed(2);
  
  console.log(`  [C${c.consumerId}] ${c.messagesProcessed.toLocaleString()} msgs | Active: ${actualProcessingTime.toFixed(2)}s (${rate} msg/s) | Idle: ${idleTime}s | ${c.batchesProcessed} batches | avg: ${avgBatch}s`);
});

console.log('='.repeat(80));