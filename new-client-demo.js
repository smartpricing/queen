import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632'],
    timeout: 30000,
    retryAttempts: 3
});

const queue = 'html-processing'

// Create a queue
await client.queue(queue, { leaseTime: 30 });

// Push some data, specifyng the partition
await client.tx(`${queue}/customer-1828`, [ { id: 1 } ]); 

await client.rx(queue, { limit: 1, batch: 10, wait: true, timeout: 30000 }, async (msg) => {
    console.log(msg.data.id)
});

// Consume data from any partition of the queue, continusly
// This "pipeline" is useful for doing exactly one processing
await client 
.pipeline(queue)
.withAutoRenewal({ 
  interval: 5000  // Renew lease every 5 seconds
})    
.withConcurrency(5) // Five parallel promises
.rx(10, {
  wait: true, // Use long polling
  timeout: 30000 // Long polling length in millisconds
})
.processBatch(async (messages) => {
    return messages.map(x => x.data.id * 2);
})
.transaction((tx, originalMessages, processedMessages) => { // ack and push are transactional inside atomically
    tx.ack(originalMessages);
    tx.tx('another-queue', processedMessages); 
})
.repeat({ continuous: true })
.execute();