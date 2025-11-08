# Basic Concepts

Understanding Queen MQ's core concepts will help you build reliable, scalable message-driven systems. Let's explore the fundamental building blocks.

## Queues

Queues are containers for messages. Each queue is a logical grouping of messages that belong together conceptually.

```javascript
// Create a queue
await queen.queue('orders').create()

// Push messages to the queue
await queen.queue('orders').push([
  { data: { orderId: 'ORD-001', amount: 99.99 } }
])
```

### Queue Configuration

Each queue can be configured independently:

```javascript
await queen.queue('critical-tasks')
  .config({
    leaseTime: 30,           // 30 seconds to process each message
    retryLimit: 3,           // Retry up to 3 times on failure
    priority: 5,             // Queue priority (0-10)
    encryptionEnabled: true  // Encrypt messages at rest
  })
  .create()
```

## Partitions

Partitions are the secret sauce that makes Queen powerful. They provide FIFO ordering within a queue.

### What are Partitions?

Think of partitions as separate lanes within a queue:

- Messages in the same partition are processed **in order**
- Messages in different partitions can be processed **in parallel**
- Only one consumer can process a partition at a time
- Unlimited partitions per queue

### Why Use Partitions?

**Ordering:** Some workflows require strict ordering per entity:

```javascript
// All orders for customer-123 processed in order
await queen.queue('orders')
  .partition('customer-123')
  .push([
    { data: { action: 'create', orderId: 'ORD-001' } },
    { data: { action: 'update', orderId: 'ORD-001' } },
    { data: { action: 'complete', orderId: 'ORD-001' } }
  ])

// Orders for customer-456 can be processed in parallel
await queen.queue('orders')
  .partition('customer-456')
  .push([
    { data: { action: 'create', orderId: 'ORD-002' } }
  ])
```

**Parallelism:** Different partitions can be processed by different consumers simultaneously:

```
┌─────────────┐
│   Queue     │
├─────────────┤
│ Partition A │ ──→ Consumer 1
│ Partition B │ ──→ Consumer 2
│ Partition C │ ──→ Consumer 3
│ Partition D │ ──→ Consumer 4
└─────────────┘
```

### Default Partition

If you don't specify a partition, Queen uses `"Default"`:

```javascript
// These are equivalent
await queen.queue('tasks').push([...])
await queen.queue('tasks').partition('Default').push([...])
```

### Choosing Partition Keys

Pick partition keys based on your ordering requirements:

```javascript
// ✅ Good: Customer orders need ordering per customer
.partition(`customer-${customerId}`)

// ✅ Good: User sessions need ordering per user
.partition(`user-${userId}`)

// ✅ Good: IoT devices need ordering per device
.partition(`device-${deviceId}`)

// ❌ Bad: Random partitions lose ordering benefits
.partition(Math.random().toString())
```

## Consumer Groups

Consumer groups enable Kafka-style message distribution where multiple consumer groups can independently process the same messages.

### Queue Mode vs Consumer Groups

**Queue Mode (default):**
```javascript
// Messages are distributed across consumers
// Each message processed by ONE consumer only
await queen.queue('tasks').consume(async (message) => {
  // Process message
})
```

**Consumer Group Mode:**
```javascript
// Each group processes ALL messages independently
await queen.queue('events')
  .group('analytics')
  .consume(async (message) => {
    // Update analytics
  })

await queen.queue('events')
  .group('notifications')
  .consume(async (message) => {
    // Send notifications
  })

// Both groups process the same messages!
```

### Use Cases for Consumer Groups

1. **Multiple Purposes**: Process the same events for different reasons
2. **Replay**: Start a new consumer group to reprocess historical messages
3. **Testing**: Create a test consumer group without affecting production
4. **Migration**: Run old and new processors in parallel

### Consumer Group Positioning

Control where a consumer group starts:

```javascript
// Only process NEW messages (default)
await queen.queue('events')
  .group('new-consumer')
  .subscribe('new_only')
  .consume(...)

// Process ALL messages from the beginning
await queen.queue('events')
  .group('replay-consumer')
  .subscribe('from_beginning')
  .consume(...)

// Process messages from a specific timestamp
await queen.queue('events')
  .group('historical-consumer')
  .subscribe('from_timestamp', new Date('2025-01-01'))
  .consume(...)
```

## Lease Management

When a consumer receives a message, it gets a **lease** - an exclusive lock for a specific time period.

### How Leases Work

```
1. Consumer pops message ──→ Gets 30-second lease
2. Processing message...
3. Two outcomes:
   a) Success: ACK message → Lease released
   b) Timeout: After 30s → Lease expires → Message available again
```

### Lease Time Configuration

```javascript
// Set lease time when creating queue
await queen.queue('long-tasks')
  .config({ leaseTime: 300 })  // 5 minutes
  .create()
```

### Lease Renewal

For long-running tasks, renew the lease to prevent timeout:

```javascript
await queen.queue('video-processing')
  .autoAck(false)
  .renewLease(true, 10000)  // Renew every 10 seconds
  .consume(async (message) => {
    // This might take 10 minutes
    await processVideo(message.data.videoUrl)
  })
  .onSuccess(async (message) => {
    await queen.ack(message, true)
  })
```

Manual lease renewal:

```javascript
const messages = await queen.queue('tasks').pop()
const message = messages[0]

// Start processing
while (stillProcessing) {
  await doSomeWork()
  
  // Renew lease every 20 seconds
  if (shouldRenewLease) {
    await queen.renewLease(message.leaseId)
  }
}

await queen.ack(message, true)
```

## Acknowledgment (Ack/Nack)

Acknowledgment tells Queen whether a message was processed successfully.

### Automatic Acknowledgment

```javascript
// Auto-ack on success, auto-nack on error
await queen.queue('tasks')
  .autoAck(true)  // Default
  .consume(async (message) => {
    await processMessage(message.data)
    // Automatically ACK'd if no error thrown
  })
```

### Manual Acknowledgment

```javascript
// Full control over ack/nack
await queen.queue('tasks')
  .autoAck(false)
  .consume(async (message) => {
    const result = await processMessage(message.data)
    
    if (result.success) {
      // Mark as successfully processed
      await queen.ack(message, true)
    } else if (result.shouldRetry) {
      // Mark as failed, will retry
      await queen.ack(message, false)
    } else {
      // Mark as failed, don't retry
      await queen.ack(message, false, { 
        skipRetry: true,
        reason: 'Invalid data'
      })
    }
  })
```

### What Happens on Nack?

1. **Retry Counter Increments**
2. **Retry Limit Check**: If not exceeded, message becomes available again
3. **Dead Letter Queue**: If retry limit exceeded, message moves to DLQ

## Transaction IDs

Every message has a unique transaction ID for idempotency and deduplication.

### Automatic Transaction IDs

```javascript
// Queen generates unique IDs automatically
await queen.queue('tasks').push([
  { data: { work: 'do this' } }
  // transactionId auto-generated
])
```

### Custom Transaction IDs

```javascript
// Provide your own for exactly-once semantics
await queen.queue('tasks').push([
  {
    transactionId: 'order-123-payment',
    data: { orderId: 123, action: 'charge' }
  }
])

// Pushing same transactionId again = rejected as duplicate
await queen.queue('tasks').push([
  {
    transactionId: 'order-123-payment',  // Duplicate!
    data: { orderId: 123, action: 'charge' }
  }
])
// This push fails with duplicate error
```

### Benefits of Transaction IDs

1. **Idempotency**: Safe to retry push operations
2. **Exactly-Once**: Prevent duplicate message processing
3. **Tracing**: Track messages across queues and systems

## Namespaces and Tasks

Organize messages within queues using optional metadata:

```javascript
// Add namespace and task to messages
await queen.queue('events')
  .namespace('billing')
  .task('invoice-generation')
  .push([
    { data: { customerId: 123 } }
  ])

// Consume only specific namespace/task
await queen.queue('events')
  .namespace('billing')
  .task('invoice-generation')
  .consume(async (message) => {
    // Only processes billing/invoice-generation messages
  })
```

### Use Cases

- **Multi-tenant**: Namespace per tenant
- **Event types**: Task per event type
- **Filtering**: Consume only relevant messages

## Message Priority

Control processing order with priority levels:

```javascript
// Create queue with priority
await queen.queue('tasks')
  .config({ priority: 5 })  // 0-10, higher = more important
  .create()

// High-priority messages processed first
await queen.queue('urgent-tasks')
  .config({ priority: 10 })
  .create()
```

## Message Retention

Control how long messages are kept:

```javascript
await queen.queue('logs')
  .config({
    retentionSeconds: 86400,           // Keep pending messages for 24 hours
    completedRetentionSeconds: 3600    // Keep completed messages for 1 hour
  })
  .create()
```

- **Retention**: How long pending/failed messages are kept before deletion
- **Completed Retention**: How long successfully processed messages are kept
- **Zero means forever** (until manually deleted)

## Quality of Service (QoS)

Queen supports multiple delivery guarantees:

### At-Most-Once (QoS 0)
```javascript
// Fire and forget - messages buffered locally
await queen.queue('analytics')
  .qos(0)
  .push([...])
// Returns immediately, messages sent in background
```

### At-Least-Once (QoS 1)
```javascript
// Wait for server confirmation (default)
await queen.queue('orders')
  .qos(1)  // Default
  .push([...])
// Returns after server stores messages
```

### Exactly-Once (with transactions)
```javascript
// Use transactions + transaction IDs
await queen.transaction()
  .ack(inputMessage)
  .queue('output')
  .push([{
    transactionId: `output-${inputMessage.transactionId}`,
    data: { ... }
  }])
  .commit()
// Atomic operation with deduplication
```

## Conceptual Hierarchy

```
Server
  └─ Queues (unlimited)
       ├─ Configuration (lease time, retry limit, etc.)
       ├─ Partitions (unlimited)
       │    └─ Messages (FIFO ordered)
       └─ Consumer Groups (unlimited)
            └─ Per-partition position tracking
```

## Comparison with Other Systems

| Concept | Queen | RabbitMQ | Kafka | NATS |
|---------|-------|----------|-------|------|
| Queue | Queue | Queue | Topic | Stream |
| Partition | Partition | N/A | Partition | N/A |
| Consumer Group | Consumer Group | Pattern | Consumer Group | Queue Group |
| Ordering | Per-partition FIFO | Per-queue | Per-partition | Per-stream |
| Replay | ✅ Timestamp | ❌ | ✅ Offset | ⚠️ Limited |
| Transaction | ✅ Atomic | ⚠️ Complex | ⚠️ Producer | ❌ |

## Next Steps

Now that you understand the concepts, explore how to use them:

- [Queues & Partitions](/guide/queues-partitions) - Deep dive into partitioning strategies
- [Consumer Groups](/guide/consumer-groups) - Master consumer group patterns
- [Transactions](/guide/transactions) - Build reliable workflows
- [JavaScript Client](/clients/javascript) - Complete client API reference

## Key Takeaways

1. **Queues** organize messages into logical groups
2. **Partitions** provide FIFO ordering and parallelism
3. **Consumer Groups** enable independent message processing
4. **Leases** prevent duplicate processing with timeouts
5. **Transaction IDs** enable exactly-once semantics
6. **Ack/Nack** control message lifecycle
7. **Namespaces/Tasks** add flexible organization

Master these concepts, and you'll build bulletproof message-driven systems! 🎯

