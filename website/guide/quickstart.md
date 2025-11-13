# Quick Start Guide

Get Queen MQ up and running in just a few minutes! This guide will have you pushing and consuming messages in no time.

## Prerequisites

- Docker (recommended for quick start)
- Node.js 22+ (for JavaScript client)
- PostgreSQL 13+ (if not using Docker)

## Step 1: Start PostgreSQL and Queen Server

The fastest way to get started is using Docker:

```bash
# Create a Docker network for Queen components
docker network create queen

# Start PostgreSQL
docker run --name postgres --network queen \
  -e POSTGRES_PASSWORD=postgres \
  -p 5432:5432 \
  -d postgres

# Start Queen Server
docker run -p 6632:6632 --network queen \
  -e PG_HOST=postgres \
  -e PG_PORT=5432 \
  -e PG_USER=postgres \
  -e PG_PASSWORD=postgres \
  -e PG_DB=postgres \
  -e DB_POOL_SIZE=20 \
  -e NUM_WORKERS=2 \
  smartnessai/queen-mq:0.6.6
```

:::tip Production Ready
Queen MQ version **0.5.0** is production-ready and stable. It's currently handling millions of messages daily at [Smartness](https://www.linkedin.com/company/smartness-com/).
:::

The Queen server will be available at `http://localhost:6632`.

Check if it's running:

```bash
curl http://localhost:6632/health
```

You should see:

```json
{
  "status": "healthy",
  "database": "connected",
  "server": "C++ Queen Server (Acceptor/Worker)",
  "version": "1.0.0"
}
```

## Step 2: Install the Client

### JavaScript/Node.js

```bash
npm install queen-mq
```

### C++

See the [C++ Client Guide](/clients/cpp) for installation instructions.

## Step 3: Your First Message

Create a file `quickstart.js`:

```javascript
import { Queen } from 'queen-mq'

// Connect to Queen server
const queen = new Queen('http://localhost:6632')

// Create a queue with configuration
await queen
  .queue('my-first-queue')
  .config({
    leaseTime: 30,        // 30 seconds to process each message
    retryLimit: 3,        // Retry up to 3 times on failure
  })
  .create()

console.log('✅ Queue created!')

// Push a message
await queen
  .queue('my-first-queue')
  .push([
    { 
      data: { message: 'Hello, Queen MQ!' }
    }
  ])

console.log('✅ Message pushed!')

// Pop and process the message
const messages = await queen
  .queue('my-first-queue')
  .pop()

console.log('✅ Message received:', messages[0].data)

// Acknowledge the message
await queen.ack(messages[0], true)

console.log('✅ Message acknowledged!')

process.exit(0)
```

Run it:

```bash
node quickstart.js
```

You should see:

```
✅ Queue created!
✅ Message pushed!
✅ Message received: { message: 'Hello, Queen MQ!' }
✅ Message acknowledged!
```

Congratulations! 🎉 You've just sent and received your first message with Queen MQ.

## Step 4: Try Consumer Groups

Now let's use consumer groups for continuous message processing:

Create `consumer.js`:

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

console.log('🚀 Starting consumer...')

// Consume messages with a consumer group
await queen
  .queue('my-first-queue')
  .group('my-consumer-group')    // Consumer group for scalability
  .concurrency(5)                // Process up to 5 messages in parallel
  .batch(10)                     // Fetch 10 messages at a time
  .autoAck(true)                 // Automatically acknowledge successful processing
  .each()                        // Process messages one by one
  .consume(async (message) => {
    console.log('📨 Processing:', message.data)
    
    // Simulate some work
    await new Promise(resolve => setTimeout(resolve, 100))
    
    console.log('✅ Processed:', message.data)
  })
  .onError(async (message, error) => {
    console.error('❌ Error processing message:', error)
  })

// The consumer will keep running...
```

In another terminal, create `producer.js`:

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

console.log('🚀 Starting producer...')

// Push 20 messages
for (let i = 1; i <= 20; i++) {
  await queen
    .queue('my-first-queue')
    .push([
      { data: { message: `Message ${i}`, timestamp: new Date() } }
    ])
  
  console.log(`✅ Pushed message ${i}`)
  
  // Wait a bit between messages
  await new Promise(resolve => setTimeout(resolve, 200))
}

console.log('✅ All messages pushed!')
process.exit(0)
```

Start the consumer first:

```bash
node consumer.js
```

Then in another terminal, run the producer:

```bash
node producer.js
```

Watch as messages flow from producer to consumer in real-time! 🎯

## Step 5: Explore the Dashboard

Queen comes with a beautiful web dashboard. Open your browser and visit:

```
http://localhost:6632
```

You'll see:
- 📊 Real-time throughput metrics
- 📈 Queue statistics
- 💬 Message browser
- 🔍 Trace explorer
- 👥 Consumer group status

![Queen Dashboard](/dashboard.png)

## What's Next?

Now that you have Queen running, explore more advanced features:

### Learn Core Concepts
- [Queues & Partitions](/guide/queues-partitions) - Understand FIFO ordering
- [Consumer Groups](/guide/consumer-groups) - Scale your consumers
- [Transactions](/guide/transactions) - Ensure exactly-once delivery

### Explore Features
- [Long Polling](/guide/long-polling) - Efficient message waiting
- [Streaming](/guide/streaming) - Real-time message streams
- [Dead Letter Queue](/guide/dlq) - Handle failed messages
- [Message Tracing](/guide/tracing) - Debug workflows

### Go to Production
- [Server Installation](/server/installation) - Build from source
- [Performance Tuning](/server/tuning) - Optimize for your workload
- [Deployment Guide](/server/deployment) - Docker, Kubernetes, systemd

### See Examples
- [Basic Usage](/clients/examples/basic) - Simple patterns
- [Batch Operations](/clients/examples/batch) - High throughput
- [Consumer Groups](/clients/examples/consumer-groups) - Scalable processing
- [Streaming Examples](/clients/examples/streaming) - Real-time processing

## Common Next Steps

### Using Partitions for Ordering

```javascript
// Messages in the same partition are processed in order
await queen
  .queue('orders')
  .partition('customer-123')  // All messages for this customer are ordered
  .push([
    { data: { action: 'create', orderId: 'ORD-001' } },
    { data: { action: 'update', orderId: 'ORD-001' } },
    { data: { action: 'complete', orderId: 'ORD-001' } }
  ])
```

### Using Transactions

```javascript
// Atomically forward a message to another queue
await queen
  .transaction()
  .queue('next-queue')
  .push([{ data: { processed: true } }])
  .ack(currentMessage)  // Only ack if push succeeds
  .commit()
```

### Multiple Consumer Groups

```javascript
// Process the same messages for different purposes
// Consumer group 1: analytics
queen.queue('events').group('analytics').consume(async (msg) => {
  await updateAnalytics(msg.data)
})

// Consumer group 2: notifications
queen.queue('events').group('notifications').consume(async (msg) => {
  await sendNotification(msg.data)
})
```

## Troubleshooting

### Server won't start?

Check the logs:
```bash
docker logs -f <container-id>
```

Make sure PostgreSQL is accessible and the database exists.

### Messages not being consumed?

1. Check if the consumer group is created
2. Verify the queue exists
3. Look for errors in the console
4. Check the dashboard for stuck messages

### Need Help?

- Check the [Server Troubleshooting](/server/troubleshooting) guide
- Review [Examples](/clients/examples/basic) for common patterns
- Visit our [GitHub repository](https://github.com/smartpricing/queen)
- Connect with us on [LinkedIn](https://www.linkedin.com/company/smartness-com/)

---

Ready to build something awesome? Dive deeper into the [JavaScript Client Documentation](/clients/javascript) or explore the [Complete Guide](/guide/concepts)!

