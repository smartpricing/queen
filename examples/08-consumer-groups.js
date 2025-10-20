import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const queue = 'notifications'

await client.queue(queue, {});

// Push messages
await client.push(`${queue}/email-partition`, [
    { type: 'email', to: 'user1@example.com' },
    { type: 'email', to: 'user2@example.com' },
    { type: 'email', to: 'user3@example.com' }
]);

// Consumer Group A - all consumers in this group see ALL messages
for await (const msg of client.take(`${queue}@email-workers`, { limit: 3 })) {
    console.log('Group A got:', msg.data);
    await client.ack(msg, true, { group: 'email-workers' });
}

// Consumer Group B - independent from Group A, also gets ALL messages
for await (const msg of client.take(`${queue}@analytics-workers`, { limit: 3 })) {
    console.log('Group B got:', msg.data);
    await client.ack(msg, true, { group: 'analytics-workers' });
}

