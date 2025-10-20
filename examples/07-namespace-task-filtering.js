import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632']
});

const namespace = 'billing'
const task = 'process-invoice'

// Configure queues with namespace and task metadata
await client.queue('invoice-queue-1', { leaseTime: 30 }, {
    namespace: namespace,
    task: task
});

await client.queue('invoice-queue-2', { leaseTime: 30 }, {
    namespace: namespace,
    task: task
});

// Push messages to different queues
await client.push('invoice-queue-1/customer-A', { invoice: 'INV-001' });
await client.push('invoice-queue-2/customer-B', { invoice: 'INV-002' });

// Take messages by namespace only (gets from all queues in namespace)
for await (const msg of client.take('namespace:billing', { limit: 2 })) {
    console.log('From namespace:', msg.data);
    await client.ack(msg);
}

// Take messages by specific namespace and task
for await (const msg of client.take('namespace:billing/task:process-invoice', { limit: 2 })) {
    console.log('From namespace+task:', msg.data);
    await client.ack(msg);
}

