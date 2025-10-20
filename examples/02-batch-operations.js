import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queue = 'batch-processing'

await client.queue(queue, {});

// Push batch of messages
const messages = Array.from({ length: 100 }, (_, i) => ({
    message: `Batch message ${i + 1}`,
    index: i + 1
}));

await client.push(`${queue}/batch-partition`, messages);

// Consume data with iterators, getting the entire batch
for await (const messages of client.takeBatch(queue, { limit: 1, batch: 10 })) {
    const newMex = messages.map(x => x.data.id * 2)
    await client.ack(messages)
}

