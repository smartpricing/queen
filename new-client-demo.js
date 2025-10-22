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

// Consume data with iterators
for await (const msg of client.rx(queue, { limit: 1 })) {
    console.log(msg.data.id)
    await client.ack(msg) //  OR await client.ack(msg, false) for nack
}

// Consume data with iterators, getting the entire batch
for await (const messages of client.mrx(queue, { limit: 1, batch: 10, wait: true, timeout: 2000 })) {
    const newMex = messages.map(x => x.data.id * 2)
    await client.ack(messages) //  OR await client.ack(messages, false) for nack
}

// Consume data with a consumer group
for await (const msg of client.rx(`${queue}@analytics-data`, { limit: 2, batch: 2 })) {
    // Do your computation and than ack with consumer group
    await client.ack(msg, true, { group: 'analytics-data' });
}

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