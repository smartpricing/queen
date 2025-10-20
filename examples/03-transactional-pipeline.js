import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queue = 'html-processing'

await client.queue(queue, {});

// Push test messages
await client.push(queue, [
    { id: 1, data: 'Message 1' },
    { id: 2, data: 'Message 2' },
    { id: 3, data: 'Message 3' }
]);

// Consume data from any partition of the queue, continuously
// This "pipeline" is useful for doing exactly-once processing
await client 
    .pipeline(queue)
    .take(10)
    .processBatch(async (messages) => {
        return messages.map(x => x.data.id * 2);
    })
    .atomically((tx, originalMessages, processedMessages) => { // ack and push are transactional
        tx.ack(originalMessages);
        tx.push('another-queue', processedMessages); 
    })
    .repeat({ continuous: true })
    .execute();

