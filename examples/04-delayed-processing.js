import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queue = 'delayed-queue'

// Configure queue with delayed processing
await client.queue(queue, {
    delayedProcessing: 2 // 2 seconds delay
});

const startTime = Date.now();

// Push message
await client.push(queue, { 
    message: 'Delayed message', 
    sentAt: startTime 
});

// Try to take immediately (should get nothing)
for await (const msg of client.take(queue, { limit: 1 })) {
    // This won't execute - message is delayed
}

// Wait for delay period
await new Promise(resolve => setTimeout(resolve, 2500));

// Try to take again (should get the message now)
for await (const msg of client.take(queue, { limit: 1 })) {
    const processingDelay = Date.now() - startTime;
    console.log(`Message processed after ${processingDelay}ms delay`);
    await client.ack(msg);
}

