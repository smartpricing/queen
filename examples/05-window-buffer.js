import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queue = 'window-buffer-queue'

// Configure queue with window buffer
// Messages won't be available for consumption for 5 seconds after being pushed
await client.queue(queue, { windowBuffer: 5 });

// Push a message
await client.push(`${queue}/test-partition`, { test: 'data' });

// Try to take immediately - should get nothing
let gotImmediate = false;
for await (const msg of client.take(`${queue}/test-partition`, { limit: 1 })) {
    gotImmediate = true;
    await client.ack(msg);
}

if (!gotImmediate) {
    console.log('windowBuffer correctly delayed message');
}

// Wait for window to pass
await new Promise(resolve => setTimeout(resolve, 6000));

// Now should get the message
for await (const msg of client.take(`${queue}/test-partition`, { limit: 1 })) {
    console.log('Message available after window buffer expired');
    await client.ack(msg);
}

