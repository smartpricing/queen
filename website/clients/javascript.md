# JavaScript Client Guide

Complete guide for the Queen MQ JavaScript/Node.js client library.

## Installation

```bash
npm install queen-mq
```

**Requirements:** Node.js 22+ (required for native fetch and modern JS features)

## Table of Contents

[[toc]]

## Getting Started

### Import and Connect

```javascript
import { Queen } from 'queen-mq'

// Single server
const queen = new Queen('http://localhost:6632')

// Multiple servers (high availability)
const queen = new Queen([
  'http://server1:6632',
  'http://server2:6632'
])

// Full configuration
const queen = new Queen({
  urls: ['http://server1:6632', 'http://server2:6632', 'http://server3:6632'],
  timeoutMillis: 30000,
  retryAttempts: 3,
  loadBalancingStrategy: 'affinity',  // 'affinity', 'round-robin', or 'session'
  affinityHashRing: 128,              // Virtual nodes per server (for affinity)
  enableFailover: true
})

// With proxy authentication (bearer token)
const queen = new Queen({
  url: 'http://proxy.example.com:3000',
  bearerToken: process.env.QUEEN_TOKEN  // Token from create-user script
})
```

### Proxy Authentication

When connecting through the Queen proxy (which provides authentication, SSL termination, etc.), you need to provide a bearer token:

```javascript
const queen = new Queen({
  url: 'https://queen-proxy.example.com',
  bearerToken: process.env.QUEEN_TOKEN
})
```

**Getting a token:** Use the proxy's `create-user.js` script to generate tokens for microservices. See [Proxy Setup](/proxy/setup#create-microservice-tokens) for details.

::: tip Environment Variables
Store tokens in environment variables, never hardcode them:

```bash
export QUEEN_TOKEN="eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
```

```javascript
const queen = new Queen({
  url: process.env.QUEEN_PROXY_URL,
  bearerToken: process.env.QUEEN_TOKEN
})
```
:::

::: info Direct Connection
When connecting directly to the Queen server (without the proxy), no `bearerToken` is needed:

```javascript
// Direct connection - no auth required
const queen = new Queen('http://queen-server:6632')
```
:::

## Load Balancing & Affinity Routing

When connecting to multiple Queen servers, you can choose how requests are distributed. The client supports three load balancing strategies.

### Affinity Mode (Recommended for Production)

**Best for:** Production deployments with 3+ servers and multiple consumer groups.

Affinity mode uses **consistent hashing with virtual nodes** to route consumer groups to the same backend server. This optimizes database queries by consolidating poll intentions on a single server.

```javascript
const queen = new Queen({
  urls: [
    'http://queen-server-1:6632',
    'http://queen-server-2:6632',
    'http://queen-server-3:6632'
  ],
  loadBalancingStrategy: 'affinity',
  affinityHashRing: 150  // Virtual nodes per server (default: 150)
})
```

**How it works:**

1. Each consumer generates an **affinity key** from its parameters:
   - Queue-based: `queue:partition:consumerGroup`
   - Namespace-based: `namespace:task:consumerGroup`

2. The key is **hashed** using FNV-1a algorithm (fast, deterministic)

3. Hash is **mapped** to a virtual node on the consistent hash ring

4. Virtual node **maps** to a real backend server

5. **Same key always routes to same server** (unless server is unhealthy)

**Example:**

```javascript
// Consumer 1
await queen.queue('orders')
  .partition('priority')
  .group('email-processor')
  .consume(async (msg) => {
    // Affinity key: "orders:priority:email-processor"
    // Routes to: server-2 (based on hash)
  })

// Consumer 2 (same group, different worker)
await queen.queue('orders')
  .partition('priority')
  .group('email-processor')
  .consume(async (msg) => {
    // Same affinity key: "orders:priority:email-processor"  
    // Routes to: server-2 (same server!)
    // → Poll intentions consolidated
    // → Single DB query serves both workers
  })

// Consumer 3 (different group)
await queen.queue('orders')
  .partition('priority')
  .group('analytics')
  .consume(async (msg) => {
    // Different affinity key: "orders:priority:analytics"
    // Routes to: server-1 (different server)
    // → Load is distributed across servers
  })
```

**Benefits:**

- ✅ **Optimized Database Queries** - Poll intentions for same consumer group are consolidated on one server, reducing duplicate SELECT queries
- ✅ **Better Cache Locality** - In-memory poll registry stays warm on the same server
- ✅ **Graceful Failover** - If a server fails, only ~33% of consumer groups move to other servers
- ✅ **Works with HA** - Perfect for 3-server high-availability setups
- ✅ **Automatic** - No manual configuration required

**Virtual Nodes:**

The `affinityHashRing` parameter controls how many virtual nodes each server gets:

```javascript
affinityHashRing: 150   // Default: good for 3-5 servers
affinityHashRing: 300   // Better distribution, more memory (~14KB total)
affinityHashRing: 50    // Less memory, worse distribution
```

::: tip Performance Impact
With affinity routing and 3 backend servers, multiple workers in the same consumer group will hit the same backend, allowing the server to consolidate poll intentions and serve them with a single database query instead of multiple queries.
:::

### Round-Robin Mode

**Best for:** Simple setups where poll optimization is not critical.

Cycles through servers in order. Simple but doesn't optimize for poll intention consolidation.

```javascript
const queen = new Queen({
  urls: ['http://server1:6632', 'http://server2:6632'],
  loadBalancingStrategy: 'round-robin'
})
```

Each request goes to the next server in the list. Load is evenly distributed but consumer groups may hit different servers on each poll.

### Session Mode

**Best for:** Single client instance that should stick to one server.

Each client instance picks a server and sticks to it for all requests.

```javascript
const queen = new Queen({
  urls: ['http://server1:6632', 'http://server2:6632'],
  loadBalancingStrategy: 'session'
})
```

### Comparison

| Feature | Affinity | Round-Robin | Session |
|---------|----------|-------------|---------|
| **Poll Consolidation** | ✅ Yes | ❌ No | ⚠️ Partial |
| **Load Distribution** | ✅ Good | ✅ Perfect | ❌ Poor |
| **Failover** | ✅ Graceful | ✅ Automatic | ✅ Automatic |
| **Memory** | ~10KB | Minimal | Minimal |
| **Best For** | Production | Testing | Simple apps |

### Push vs Consume Routing

**Important:** Affinity routing is **only applied to consumer operations** (pop/consume), not push operations.

- **Consumers (pop/consume):** Use affinity key for consistent routing
- **Producers (push):** Use default strategy (round-robin) for even write distribution

This gives you the best of both worlds: optimized reads with affinity, balanced writes without hotspots.

```javascript
// Push - uses round-robin (even distribution)
await queen.queue('orders').push([{ data: { order: 123 } }])

// Consume - uses affinity (consolidated polling)  
await queen.queue('orders').group('processors').consume(async (msg) => {
  // Same group always routes to same server
})
```

### Monitoring Affinity Routing

You can check the load balancer status:

```javascript
const httpClient = queen._httpClient
const loadBalancer = httpClient.getLoadBalancer()

if (loadBalancer) {
  console.log('Strategy:', loadBalancer.getStrategy())
  console.log('Virtual nodes:', loadBalancer.getVirtualNodeCount())
  console.log('Servers:', loadBalancer.getAllUrls())
  console.log('Health:', loadBalancer.getHealthStatus())
}
```

## Queue Management

### Create Queue

```javascript
// Simple creation
await queen.queue('my-tasks').create()

// With configuration
await queen.queue('my-tasks')
  .config({
    leaseTime: 60,           // 60 seconds to process
    retryLimit: 3,           // Max 3 retries
    dlqAfterMaxRetries: true, // Auto move to DLQ after max retries
    retentionSeconds: 86400, // Keep messages 24 hours
    encryptionEnabled: false
  })
  .create()
```

### Delete Queue

```javascript
await queen.queue('my-tasks').delete()
```

::: danger Warning
This deletes all messages, partitions, and consumer group state. Cannot be undone!
:::

## Pushing Messages

### Basic Push

```javascript
await queen.queue('tasks').push([
  { data: { task: 'send-email', to: 'user@example.com' } }
])
```

### Multiple Messages

```javascript
await queen.queue('tasks').push([
  { data: { task: 'send-email', to: 'alice@example.com' } },
  { data: { task: 'send-email', to: 'bob@example.com' } },
  { data: { task: 'resize-image', id: 123 } }
])
```

### With Custom Transaction ID

```javascript
await queen.queue('tasks').push([
  {
    transactionId: 'unique-id-123',  // Auto-generated if not provided
    data: { task: 'process-order', orderId: 456 }
  }
])
```

### With Callbacks

```javascript
await queen.queue('tasks').push([...])
  .onSuccess(async (messages) => {
    console.log('Pushed successfully:', messages)
  })
  .onDuplicate(async (messages) => {
    console.warn('Duplicate transaction IDs detected')
  })
  .onError(async (messages, error) => {
    console.error('Push failed:', error)
  })
```

## Consuming Messages

### Basic Consume

The easiest way to process messages continuously:

```javascript
await queen.queue('my-tasks').consume(async (message) => {
  console.log('Processing:', message.data)
  
  // Do your work
  await processTask(message.data)
  
  // Auto-ack on success, auto-nack on error
})
```

**What happens:**
1. Consumer pulls messages from the queue
2. Your function processes each message
3. If successful → message marked as complete ✅
4. If error → message goes back for retry 🔄

### With Configuration

```javascript
await queen.queue('tasks')
  .concurrency(10)          // 10 parallel workers
  .batch(20)                // Fetch 20 at a time
  .autoAck(false)           // Manual ack
  .renewLease(true, 5000)   // Auto-renew every 5s
  .each()                   // Process individually
  .consume(async (message) => {
    await process(message.data)
  })
  .onSuccess(async (message) => {
    await queen.ack(message, true)
  })
  .onError(async (message, error) => {
    console.error('Failed:', error)
    await queen.ack(message, false)
  })
```

### Process Limited Messages

```javascript
// Process exactly 100 messages then stop
await queen.queue('tasks')
  .limit(100)
  .consume(async (message) => {
    await processMessage(message.data)
  })
```

### Batch Processing

```javascript
// Process messages in batches
await queen.queue('events')
  .batch(10)
  .consume(async (messages) => {
    // messages is an array of 10 messages
    for (const msg of messages) {
      await process(msg)
    }
  })
```

## Pop vs Consume

### The Consume Way (Recommended)

**Use when:** Long-running worker that continuously processes messages.

```javascript
await queen.queue('tasks').consume(async (message) => {
  // Auto-loops, auto-ack, long-polling built-in
})
```

### The Pop Way

**Use when:** Manual control over message fetching.

```javascript
const messages = await queen.queue('tasks').pop()

if (messages.length > 0) {
  const message = messages[0]
  
  try {
    await processMessage(message.data)
    await queen.ack(message, true)
  } catch (error) {
    await queen.ack(message, false, { error: error.message })
  }
}
```

### Pop with Long Polling

```javascript
// Wait up to 30 seconds for messages
const messages = await queen.queue('tasks')
  .batch(10)
  .wait(true)
  .timeout(30000)
  .pop()
```

## Partitions

Use partitions for ordering guarantees. Messages in the same partition are processed in order.

### Push to Partition

```javascript
await queen.queue('user-events')
  .partition('user-123')
  .push([
    { data: { event: 'login', timestamp: Date.now() } },
    { data: { event: 'purchase', orderId: 456 } },
    { data: { event: 'logout', timestamp: Date.now() } }
  ])
```

### Consume from Partition

```javascript
await queen.queue('user-events')
  .partition('user-123')
  .consume(async (message) => {
    // Messages processed in exact order
    console.log('User 123:', message.data.event)
  })
```

**Important:** Messages in different partitions are independent.

## Consumer Groups

Consumer groups enable:
- Multiple workers sharing the same queue
- Fan-out patterns (same message to multiple groups)
- Message replay from any point

### Basic Consumer Group

```javascript
// Worker 1 in group "processors"
await queen.queue('emails')
  .group('processors')
  .consume(async (message) => {
    console.log('Worker 1 processing:', message.data)
  })

// Worker 2 in SAME group (shares the load)
await queen.queue('emails')
  .group('processors')
  .consume(async (message) => {
    console.log('Worker 2 processing:', message.data)
  })
```

Messages are distributed between workers. Each message goes to only ONE worker.

### Multiple Consumer Groups (Fan-Out)

```javascript
// Group 1: Send emails
await queen.queue('notifications')
  .group('email-sender')
  .consume(async (message) => {
    await sendEmail(message.data)
  })

// Group 2: Log to analytics (processes THE SAME messages)
await queen.queue('notifications')
  .group('analytics')
  .consume(async (message) => {
    await trackEvent(message.data)
  })
```

Every message is processed by BOTH groups independently! 🎉

### Subscription Modes

Control whether consumer groups process historical messages or only new ones.

**Default Behavior (Process All Messages):**

```javascript
await queen.queue('events')
  .group('new-analytics')
  .consume(async (message) => {
    // Processes ALL messages, including historical
  })
```

**Skip Historical Messages:**

```javascript
await queen.queue('events')
  .group('realtime-monitor')
  .subscriptionMode('new')  // Skip history
  .consume(async (message) => {
    // Only processes messages arriving after subscription
  })
```

**Subscribe from Specific Timestamp:**

```javascript
const startTime = '2025-10-28T10:00:00.000Z'

await queen.queue('events')
  .group('replay-from-10am')
  .subscriptionFrom(startTime)
  .consume(async (message) => {
    // Process messages from 10am onwards
  })
```

**Server Default:**

The server can be configured to change default subscription behavior:
```bash
export DEFAULT_SUBSCRIPTION_MODE="new"
./bin/queen-server
```

When set, new consumer groups automatically skip historical messages unless you explicitly override with `.subscriptionMode('all')`.

## Transactions

Transactions ensure atomic operations. Either everything succeeds or nothing does.

### Basic Transaction: Ack + Push

```javascript
// Pop a message
const messages = await queen.queue('raw-data').batch(1).pop()

if (messages.length > 0) {
  const message = messages[0]
  
  // Process it
  const processed = await transformData(message.data)
  
  // Atomically: ack input AND push output
  await queen.transaction()
    .ack(message)                    // Complete input
    .queue('processed-data')
    .push([{ data: processed }])     // Push to next queue
    .commit()
}

// If commit fails, NOTHING happens. Message stays in raw-data!
```

### Multi-Queue Pipeline

```javascript
const messages = await queen.queue('queue-a').batch(1).pop()

// Atomic: ack from A, push to B and C
await queen.transaction()
  .ack(messages[0])
  .queue('queue-b')
  .push([{ data: { step: 2, value: messages[0].data.value * 2 } }])
  .queue('queue-c')
  .push([{ data: { step: 2, value: messages[0].data.value * 2 } }])
  .commit()
```

### Transaction with Consumer

```javascript
await queen.queue('source')
  .autoAck(false)  // Must disable auto-ack
  .consume(async (message) => {
    // Do work
    const result = await processMessage(message.data)
    
    // Transactionally ack and push result
    await queen.transaction()
      .ack(message)
      .queue('destination')
      .push([{ data: result }])
      .commit()
  })
```

## Client-Side Buffering

Buffering batches messages for 10x-100x faster throughput!

### How It Works

1. Messages collect in a local buffer
2. Buffer flushes when it reaches count OR time threshold
3. All buffered messages sent in one HTTP request

```javascript
// Buffer up to 100 messages OR 1 second
await queen.queue('logs')
  .buffer({ messageCount: 100, timeMillis: 1000 })
  .push([
    { data: { level: 'info', message: 'User logged in' } }
  ])
```

### High-Throughput Example

```javascript
// Send 10,000 messages super fast
for (let i = 0; i < 10000; i++) {
  await queen.queue('events')
    .buffer({ messageCount: 500, timeMillis: 100 })
    .push([{ data: { id: i, timestamp: Date.now() } }])
}

// Flush remaining buffered messages
await queen.flushAllBuffers()
```

### Manual Flush

```javascript
// Flush all buffers
await queen.flushAllBuffers()

// Flush specific queue
await queen.queue('my-queue').flushBuffer()

// Get buffer statistics
const stats = queen.getBufferStats()
console.log('Buffers:', stats)
```

## Acknowledgment

### ACK (Success)

```javascript
await queen.ack(message, true)
```

### NACK (Failure)

```javascript
await queen.ack(message, false)
```

### With Error Context

```javascript
await queen.ack(message, false, {
  error: 'Invalid data format',
  details: { field: 'email', reason: 'not a valid email' }
})
```

### Batch ACK

```javascript
// Ack multiple messages at once
await queen.ack([msg1, msg2, msg3], true)
```

## Dead Letter Queue

### Enable DLQ

```javascript
await queen.queue('risky-business')
  .config({
    retryLimit: 3,
    dlqAfterMaxRetries: true  // Auto-move to DLQ after 3 failures
  })
  .create()
```

### Query DLQ

```javascript
const dlq = await queen.queue('risky-business')
  .dlq()
  .limit(10)
  .get()

console.log(`Found ${dlq.total} failed messages`)

for (const message of dlq.messages) {
  console.log('Failed:', message.data)
  console.log('Error:', message.errorMessage)
  console.log('Failed at:', message.dlqTimestamp)
}
```

### DLQ with Time Range

```javascript
const dlq = await queen.queue('risky-business')
  .dlq()
  .from('2025-01-01')
  .to('2025-01-31')
  .limit(100)
  .get()
```

## Lease Renewal

Keep locks active during long-running tasks.

### Automatic Lease Renewal

```javascript
await queen.queue('long-tasks')
  .renewLease(true, 60000)  // Renew every 60 seconds
  .consume(async (message) => {
    // Even if this takes 30 minutes, lease keeps renewing!
    await processVeryLongTask(message.data)
  })
```

### Manual Lease Renewal

```javascript
const messages = await queen.queue('long-tasks').pop()
const message = messages[0]

// Start renewal
const timer = setInterval(async () => {
  await queen.renew(message)
  console.log('Lease renewed')
}, 30000)

try {
  await processVeryLongTask(message.data)
  await queen.ack(message, true)
} finally {
  clearInterval(timer)
}
```

## Message Tracing

Record breadcrumbs as messages flow through your system.

### Basic Tracing

```javascript
await queen.queue('orders').consume(async (msg) => {
  // Record trace event
  await msg.trace({
    data: { text: 'Order processing started' }
  })
  
  const order = await processOrder(msg.data)
  
  await msg.trace({
    data: { 
      text: 'Order processed successfully',
      orderId: order.id,
      total: order.total
    }
  })
})
```

### Trace Names (Connect the Dots)

Link traces across multiple messages:

```javascript
// Service 1: Order Service
await queen.queue('orders').consume(async (msg) => {
  const orderId = msg.data.orderId
  
  await msg.trace({
    traceName: `order-${orderId}`,  // Link traces
    data: { text: 'Order created', service: 'orders' }
  })
  
  await queen.queue('inventory').push([{
    data: { orderId, items: msg.data.items }
  }])
})

// Service 2: Inventory Service
await queen.queue('inventory').consume(async (msg) => {
  const orderId = msg.data.orderId
  
  await msg.trace({
    traceName: `order-${orderId}`,  // Same name = connected!
    data: { text: 'Stock checked', service: 'inventory' }
  })
})
```

**In the dashboard:**
- Search for `order-12345`
- See the ENTIRE workflow across all services! 🎉

### Event Types

Organize traces with event types:

```javascript
await msg.trace({
  eventType: 'info',  // Blue in UI
  data: { text: 'Started processing' }
})

await msg.trace({
  eventType: 'error',  // Red in UI
  data: { text: 'Validation failed', reason: 'Invalid email' }
})

await msg.trace({
  eventType: 'processing',  // Green in UI
  data: { text: 'Sending email' }
})
```

## Namespaces & Tasks

Logical grouping with wildcard filtering.

### Namespaces

```javascript
// Create queues with namespaces
await queen.queue('billing-invoices').namespace('accounting').create()
await queen.queue('billing-receipts').namespace('accounting').create()

// Consume from ALL queues in the namespace
await queen.queue()
  .namespace('accounting')
  .consume(async (message) => {
    // Receives from BOTH billing-invoices AND billing-receipts
  })
```

### Tasks

```javascript
// Create queues with tasks
await queen.queue('video-uploads').task('video-processing').create()
await queen.queue('image-uploads').task('image-processing').create()

// Consume by task type
await queen.queue()
  .task('video-processing')
  .consume(async (message) => {
    // Only video processing messages
  })
```

### Combining Namespace + Task

```javascript
await queen.queue()
  .namespace('media')
  .task('urgent-processing')
  .consume(async (message) => {
    // Only urgent media processing from media namespace
  })
```

## Advanced Configuration

### Queue Configuration Options

```javascript
await queen.queue('super-queue').config({
  // Lease & Retry
  leaseTime: 300,                // 5 minutes to process (seconds)
  retryLimit: 3,                 // Retry 3 times
  retryDelay: 5000,              // Wait 5 seconds between retries (ms)
  
  // Dead Letter Queue
  dlqAfterMaxRetries: true,      // Move to DLQ after max retries
  
  // Delays & Buffers
  delayedProcessing: 60,         // Available after 60 seconds
  windowBuffer: 30,              // Hold messages for 30 seconds to batch
  
  // Retention
  retentionSeconds: 86400,       // Keep pending messages 24 hours
  completedRetentionSeconds: 3600, // Keep completed 1 hour
  ttl: 86400,                    // Message expires after 24 hours
  
  // Security
  encryptionEnabled: true        // Encrypt payloads at rest
}).create()
```

### Consumer Configuration

```javascript
await queen.queue('tasks')
  .group('workers')
  .concurrency(10)        // 10 parallel workers
  .batch(20)              // Fetch 20 at a time
  .autoAck(true)          // Auto-ack on success
  .renewLease(true, 5000) // Auto-renew every 5s
  .limit(1000)            // Process 1000 messages then stop
  .each()                 // Process individually (vs batch)
  .consume(async (message) => {
    await process(message.data)
  })
```

## Graceful Shutdown

Always clean up properly!

### Automatic Shutdown

Queen automatically handles `SIGINT` and `SIGTERM`:

```javascript
const queen = new Queen('http://localhost:6632')

// Your app runs...

// User presses Ctrl+C:
// Queen automatically flushes buffers and closes cleanly!
```

### Manual Shutdown

```javascript
await queen.close()
console.log('Queen shut down cleanly')
```

### With AbortController

```javascript
const controller = new AbortController()

const consumerPromise = queen.queue('tasks')
  .consume(async (message) => {
    await processMessage(message.data)
  }, { signal: controller.signal })

// Later... stop consumer
controller.abort()

// Wait for consumer to finish current message
await consumerPromise

// Close Queen
await queen.close()
```

## Error Handling

```javascript
try {
  await queen.queue('tasks').push([...])
} catch (error) {
  if (error.code === 'DUPLICATE') {
    console.log('Message already exists')
  } else if (error.code === 'TIMEOUT') {
    console.log('Operation timed out')
  } else {
    console.error('Error:', error.message)
  }
}
```

## Configuration Defaults

### Client Defaults

```javascript
{
  timeoutMillis: 30000,
  retryAttempts: 3,
  retryDelayMillis: 1000,
  loadBalancingStrategy: 'affinity',    // 'affinity', 'round-robin', or 'session'
  affinityHashRing: 150,                // Virtual nodes per server
  enableFailover: true,
  bearerToken: null                     // For proxy authentication
}
```

### Queue Defaults

```javascript
{
  leaseTime: 300,          // 5 minutes
  retryLimit: 3,
  delayedProcessing: 0,
  windowBuffer: 0,
  maxSize: 0,              // Unlimited
  retentionSeconds: 0,     // Keep forever
  encryptionEnabled: false
}
```

### Consume Defaults

```javascript
{
  concurrency: 1,
  batch: 1,
  autoAck: true,
  wait: true,              // Long polling
  timeoutMillis: 30000,
  limit: null,             // Run forever
  renewLease: false
}
```

## Logging

Enable detailed logging for debugging:

```bash
export QUEEN_CLIENT_LOG=true
node your-app.js
```

Example output:
```
[2025-10-28T10:30:45.123Z] [INFO] [Queen.constructor] {"status":"initialized","urls":1}
[2025-10-28T10:30:45.234Z] [INFO] [QueueBuilder.push] {"queue":"tasks","partition":"Default","count":5}
```

## Best Practices

1. ✅ **Use affinity routing** - Enable `loadBalancingStrategy: 'affinity'` for production to optimize poll intentions
2. ✅ **Use `consume()` for workers** - Simpler, handles retries automatically
3. ✅ **Use `pop()` for control** - When you need precise control over acking
4. ✅ **Buffer for speed** - Always use buffering when pushing many messages
5. ✅ **Partitions for order** - Use partitions when message order matters
6. ✅ **Consumer groups for scale** - Run multiple workers in the same group
7. ✅ **Transactions for consistency** - Use transactions when operations must be atomic
8. ✅ **Enable DLQ** - Always enable DLQ in production to catch failures
9. ✅ **Renew long leases** - Use auto-renewal for long-running tasks
10. ✅ **Graceful shutdown** - Always call `queen.close()` before exiting
11. ✅ **Monitor DLQ** - Regularly check your DLQ for failed messages

## Complete Example

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

// Setup queues
await queen.queue('raw-events').create()
await queen.queue('processed-events').create()

// Stage 1: Ingest with buffering
async function ingestEvents() {
  for (let i = 0; i < 10000; i++) {
    await queen.queue('raw-events')
      .partition(`user-${i % 100}`)
      .buffer({ messageCount: 500, timeMillis: 1000 })
      .push([{
        data: {
          userId: i % 100,
          event: 'page_view',
          timestamp: Date.now()
        }
      }])
  }
  await queen.flushAllBuffers()
}

// Stage 2: Process with transactions
async function processEvents() {
  await queen.queue('raw-events')
    .group('processors')
    .concurrency(5)
    .batch(10)
    .autoAck(false)
    .consume(async (messages) => {
      const processed = messages.map(m => ({
        userId: m.data.userId,
        processed: true,
        timestamp: Date.now()
      }))
      
      const txn = queen.transaction()
      for (const msg of messages) {
        txn.ack(msg)
      }
      txn.queue('processed-events').push(
        processed.map(p => ({ data: p }))
      )
      await txn.commit()
    })
    .onError(async (messages, error) => {
      console.error('Processing failed:', error)
      await queen.ack(messages, false)
    })
}

// Run pipeline
await ingestEvents()
await processEvents()

// Graceful shutdown
process.on('SIGINT', async () => {
  await queen.close()
  process.exit(0)
})
```


## Admin API

The Admin API provides administrative and observability operations typically used by dashboards, monitoring tools, and admin scripts. These endpoints are for inspecting the system, not for regular application message processing.

### Accessing the Admin API

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

// Access via .admin property
const overview = await queen.admin.getOverview()
```

### Resources

```javascript
// System overview (queue counts, message stats, etc.)
const overview = await queen.admin.getOverview()

// List all namespaces
const namespaces = await queen.admin.getNamespaces()

// List all tasks  
const tasks = await queen.admin.getTasks()
```

### Queue Management

```javascript
// List all queues
const queues = await queen.admin.listQueues({ limit: 100, offset: 0 })

// Get specific queue details
const queue = await queen.admin.getQueue('my-queue')

// List partitions
const partitions = await queen.admin.getPartitions({ queue: 'my-queue' })
```

### Message Inspection

```javascript
// List messages with filters
const messages = await queen.admin.listMessages({
  queue: 'my-queue',
  status: 'pending',  // pending, processing, completed, failed
  limit: 50,
  offset: 0
})

// Get specific message
const message = await queen.admin.getMessage(partitionId, transactionId)

// Delete a message (DESTRUCTIVE!)
await queen.admin.deleteMessage(partitionId, transactionId)

// Retry a failed message
await queen.admin.retryMessage(partitionId, transactionId)

// Move message to Dead Letter Queue
await queen.admin.moveMessageToDLQ(partitionId, transactionId)
```

### Traces

```javascript
// Get available trace names
const traceNames = await queen.admin.getTraceNames({ limit: 100 })

// Get traces by name (cross-service correlation)
const traces = await queen.admin.getTracesByName('order-12345', {
  limit: 100,
  from: '2025-01-01T00:00:00Z',
  to: '2025-01-31T23:59:59Z'
})

// Get traces for a specific message
const messageTraces = await queen.admin.getTracesForMessage(partitionId, transactionId)
```

### Analytics & Status

```javascript
// System status
const status = await queen.admin.getStatus()

// Queue statistics
const queueStats = await queen.admin.getQueueStats({ limit: 100 })

// Detailed stats for specific queue
const detail = await queen.admin.getQueueDetail('my-queue')

// Analytics data (throughput, latency, etc.)
const analytics = await queen.admin.getAnalytics({
  from: '2025-01-01T00:00:00Z',
  to: '2025-01-31T23:59:59Z'
})
```

### Consumer Groups

```javascript
// List all consumer groups
const groups = await queen.admin.listConsumerGroups()

// Get specific consumer group details
const group = await queen.admin.getConsumerGroup('my-consumer-group')

// Find lagging consumers (behind by > 60 seconds)
const lagging = await queen.admin.getLaggingConsumers(60)

// Refresh consumer statistics
await queen.admin.refreshConsumerStats()

// Delete consumer group for a specific queue
await queen.admin.deleteConsumerGroupForQueue('my-group', 'my-queue', true)

// Seek consumer group offset
await queen.admin.seekConsumerGroup('my-group', 'my-queue', {
  timestamp: '2025-01-15T10:00:00Z'
})
```

### System Operations

```javascript
// Health check
const health = await queen.admin.health()

// Prometheus metrics (raw text)
const metrics = await queen.admin.metrics()

// Maintenance mode (push operations)
const maintenanceStatus = await queen.admin.getMaintenanceMode()
await queen.admin.setMaintenanceMode(true)  // Enable
await queen.admin.setMaintenanceMode(false) // Disable

// Pop maintenance mode
const popMaintenance = await queen.admin.getPopMaintenanceMode()
await queen.admin.setPopMaintenanceMode(true)

// System metrics (CPU, memory, connections)
const systemMetrics = await queen.admin.getSystemMetrics()

// Worker metrics
const workerMetrics = await queen.admin.getWorkerMetrics()

// PostgreSQL statistics
const pgStats = await queen.admin.getPostgresStats()
```

### Admin API Reference

| Method | Description |
|--------|-------------|
| **Resources** | |
| `getOverview()` | System overview with counts |
| `getNamespaces()` | List all namespaces |
| `getTasks()` | List all tasks |
| **Queues** | |
| `listQueues(params)` | List queues with pagination |
| `getQueue(name)` | Get queue details |
| `clearQueue(name, partition?)` | Clear queue messages |
| `getPartitions(params)` | List partitions |
| **Messages** | |
| `listMessages(params)` | List messages with filters |
| `getMessage(partitionId, txId)` | Get specific message |
| `deleteMessage(partitionId, txId)` | Delete message |
| `retryMessage(partitionId, txId)` | Retry failed message |
| `moveMessageToDLQ(partitionId, txId)` | Move to DLQ |
| **Traces** | |
| `getTraceNames(params)` | List available trace names |
| `getTracesByName(name, params)` | Get traces by name |
| `getTracesForMessage(partitionId, txId)` | Get message traces |
| **Analytics** | |
| `getStatus(params)` | System status |
| `getQueueStats(params)` | Queue statistics |
| `getQueueDetail(name, params)` | Detailed queue stats |
| `getAnalytics(params)` | Analytics data |
| **Consumer Groups** | |
| `listConsumerGroups()` | List all consumer groups |
| `getConsumerGroup(name)` | Get consumer group details |
| `getLaggingConsumers(minLagSeconds)` | Find lagging consumers |
| `refreshConsumerStats()` | Refresh statistics |
| `deleteConsumerGroupForQueue(cg, queue, deleteMeta)` | Delete CG for queue |
| `seekConsumerGroup(cg, queue, options)` | Seek offset |
| **System** | |
| `health()` | Health check |
| `metrics()` | Prometheus metrics |
| `getMaintenanceMode()` | Get push maintenance status |
| `setMaintenanceMode(enabled)` | Set push maintenance |
| `getPopMaintenanceMode()` | Get pop maintenance status |
| `setPopMaintenanceMode(enabled)` | Set pop maintenance |
| `getSystemMetrics(params)` | System metrics |
| `getWorkerMetrics(params)` | Worker metrics |
| `getPostgresStats()` | PostgreSQL stats |

::: tip When to Use Admin API
Use the Admin API for:
- **Dashboards** - Building monitoring UIs
- **Scripts** - Maintenance and debugging scripts
- **Monitoring** - Integration with alerting systems
- **Operations** - Managing consumer groups and queues

For normal message processing, use `queue().push()`, `queue().consume()`, and `ack()` instead.
:::

## See Also

- [Quick Start Guide](/guide/quickstart) - Get started quickly
- [Examples](/clients/examples/basic) - More code examples
- [API Reference](/api/http) - Complete HTTP API
- [GitHub README](https://github.com/smartpricing/queen/blob/master/client-js/client-v2/README.md) - Extended tutorial (1940 lines!)
