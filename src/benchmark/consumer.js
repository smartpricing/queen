import { Queen } from '../client/client.js';
import fs from 'fs';

const QUEUE_NAME = 'benchmark-queue-01';



/*await q.queue(QUEUE_NAME, {
  leaseTime: 10,
  retryLimit: 3
});*/


// Process continuously with batch processing
let totalProcessed = 0;
let startTime = Date.now();

async function consumer () {
  const q = new Queen({
    baseUrls: ['http://localhost:6632', 'http://localhost:6633'],
    timeout: 30000
  });  
for await (const messages of q.takeBatch(QUEUE_NAME, { 
    wait: true,        // Enable long polling
    timeout: 30000,    // 30 second timeout
    batch: 10000,       // Fetch up to 1000 at once
    idleTimeout: 20000 // Stop if no messages for 10 seconds
  })) {
    try {
      // Process the batch
      const batchStartTime = Date.now();
      totalProcessed += messages.length;
       
      // Validate message ordering within each partition
      const partitionLastIds = new Map();
      
      for (const message of messages) {
        if (!message.data || typeof message.data.id !== 'number') {
          throw new Error(`Message missing or invalid data.id: ${JSON.stringify(message)}`);
        }
        
        const partition = message.partition || 'default';
        const messageId = message.data.id;
        
        if (partitionLastIds.has(partition)) {
          //console.log(message.partition, message.data.id);
          const lastId = partitionLastIds.get(partition);
          if (messageId !== lastId + 1) {
            console.log(message.partition, message);
            throw new Error(`Message ordering violation in partition '${partition}': expected id ${lastId + 1}, got ${messageId}`);
          }
        }
        
        partitionLastIds.set(partition, messageId);
      }

      // Acknowledge all messages in the batch
      await q.ack(messages);
      
      const batchEndTime = Date.now();
      const batchDuration = (batchEndTime - batchStartTime) / 1000;
      const overallDuration = (batchEndTime - startTime) / 1000;
      
      console.log(`Batch: ${messages.length} messages | Batch time: ${batchDuration.toFixed(3)}s | Total: ${totalProcessed} | Rate: ${(totalProcessed / overallDuration).toFixed(0)} msg/s`);
    } catch (error) {
      console.error('Error processing batch:', error);
      // Acknowledge all messages as failed
      await q.ack(messages, false, { error: error.message });
    }
}
}

for (let i = 0; i < 10; i++) {
   consumer();
}

const totalDuration = (Date.now() - startTime) / 1000;
console.log(`\nCompleted! Total: ${totalProcessed} messages in ${totalDuration.toFixed(2)}s`);
console.log(`Average throughput: ${(totalProcessed / totalDuration).toFixed(0)} messages/second`);