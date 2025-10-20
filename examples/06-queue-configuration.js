import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queue = 'configured-queue'

// Configure queue with advanced options
await client.queue(queue, {
    leaseTime: 600,           // Time in seconds before message lease expires
    retryLimit: 5,            // Maximum retry attempts before message goes to failed
    priority: 8,              // Queue priority (higher = processed first)
    delayedProcessing: 2,     // Delay in seconds before message is available
    windowBuffer: 3,          // Time in seconds messages wait before being available
    maxSize: 10000            // Maximum number of messages in queue
});

// Push a message
await client.push(queue, { data: 'test message' });

// Take and process
for await (const msg of client.take(queue, { limit: 1 })) {
    console.log('Processing message:', msg.data);
    await client.ack(msg);
}

