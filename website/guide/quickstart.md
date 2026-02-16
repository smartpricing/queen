# Quick Start Guide

Get Queen MQ up and running in just a few minutes! This guide will have you pushing and consuming messages in no time.

## Prerequisites

- Docker (recommended for quick start)
- Node.js 22+ (for JavaScript client) **or** Python 3.8+ (for Python client)
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
  -e PG_PASSWORD=postgres \
  -e DB_POOL_SIZE=10 \
  -e DEFAULT_SUBSCRIPTION_MODE=new \
  -e LOG_LEVEL=info \
  -e RETENTION_INTERVAL=10000 \
  -e RETENTION_BATCH_SIZE=50000 \
  -e QUEEN_SYNC_ENABLED=true \
  -e QUEEN_UDP_PEERS=localhost:6634,localhost:6635 \
  -e QUEEN_UDP_NOTIFY_PORT=6634 \
  -e FILE_BUFFER_DIR=/tmp/queen/s1 \
  -e SIDECAR_POOL_SIZE=70 \
  -e SIDECAR_MICRO_BATCH_WAIT_MS=10 \
  -e NUM_WORKERS=4 \
  -e POP_WAIT_INITIAL_INTERVAL_MS=10 \
  -e POP_WAIT_MAX_INTERVAL_MS=1000 \
  -e POP_WAIT_BACKOFF_MULTIPLIER=2 \
  -e POP_WAIT_BACKOFF_THRESHOLD=1 \
  -e DB_STATEMENT_TIMEOUT=300000 \
  -e STATS_RECONCILE_INTERVAL_MS=30000 \
  smartnessai/queen-mq:{{VERSION}}
```

:::tip Production Ready
Queen MQ version **{{VERSION}}** is production-ready and stable. It's currently handling millions of messages daily at [Smartness](https://www.linkedin.com/company/smartness-com/).
:::

The Queen server will be available at `http://localhost:6632`.

Check if it's running:

```bash
curl http://localhost:6632/health
```

You should see:

```json
{"database":"connected","queen":{"status":"active","worker_id":3},"server":"C++ Queen Server (libqueen)","shared_state":{"enabled":true,"maintenance_mode":false,"pop_maintenance_mode":false,"queue_config_cache":{"hit_rate":0.0,"hits":0,"misses":0,"size":85},"running":true,"server_health":{"heartbeats_received":29932,"restarts_detected":0,"servers_alive":1,"servers_dead":0,"servers_tracked":1},"server_id":"alice.local:6634","transport":{"learned_identities":{},"learned_identities_count":0,"messages_dropped":0,"messages_received":29934,"messages_sent":59868,"peer_count":2,"peers":[{"config_server_id":"localhost:6634","hostname":"localhost","port":6634,"resolved":true,"resolved_ip":"127.0.0.1"},{"config_server_id":"localhost:6635","hostname":"localhost","port":6635,"resolved":true,"resolved_ip":"127.0.0.1"}],"port":6634,"sequence_rejections":0,"server_id":"alice.local:6634","session_id":"b2705c6f66b59975","signature_failures":0}},"status":"healthy","version":"1.0.0","worker_id":3}
```

## Step 2: Install the Client

### JavaScript/Node.js

```bash
npm install queen-mq
```

### Python

```bash
pip install queen-mq
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

### Python Example

Create a file `quickstart.py`:

```python
import asyncio
from queen import Queen

async def main():
    # Connect to Queen server
    async with Queen('http://localhost:6632') as queen:
        # Create a queue with configuration
        await queen.queue('my-first-queue').config({
            'leaseTime': 30,        # 30 seconds to process each message
            'retryLimit': 3,        # Retry up to 3 times on failure
        }).create()
        
        print('✅ Queue created!')
        
        # Push a message
        await queen.queue('my-first-queue').push([
            {'data': {'message': 'Hello, Queen MQ!'}}
        ])
        
        print('✅ Message pushed!')
        
        # Pop and process the message
        messages = await queen.queue('my-first-queue').pop()
        
        print('✅ Message received:', messages[0]['data'])
        
        # Acknowledge the message
        await queen.ack(messages[0], True)
        
        print('✅ Message acknowledged!')

asyncio.run(main())
```

Run it:

```bash
python quickstart.py
```

You should see:

```
✅ Queue created!
✅ Message pushed!
✅ Message received: {'message': 'Hello, Queen MQ!'}
✅ Message acknowledged!
```

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

Watch as messages flow from producer to consumer in real-time!

## What's Next?

Now that you have Queen running, explore more advanced features:

### Learn Core Concepts
- [Queues & Partitions](/guide/queues-partitions) - Understand FIFO ordering
- [Consumer Groups](/guide/consumer-groups) - Scale your consumers
- [Transactions](/guide/transactions) - Ensure exactly-once delivery

### Explore Features
- [Long Polling](/guide/long-polling) - Efficient message waiting
- [Dead Letter Queue](/guide/dlq) - Handle failed messages
- [Message Tracing](/guide/tracing) - Debug workflows

### Go to Production
- [Server Installation](/server/installation) - Build from source
- [Deployment Guide](/server/deployment) - Docker, Kubernetes, systemd
- [Clustered Deployments](/guide/long-polling#clustered-deployments-inter-instance-notifications) - Low-latency multi-server setup

### See Examples
- [Basic Usage](/clients/examples/basic) - Simple patterns
- [Batch Operations](/clients/examples/batch) - High throughput
- [Consumer Groups](/clients/examples/consumer-groups) - Scalable processing