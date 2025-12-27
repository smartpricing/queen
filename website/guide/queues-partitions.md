# Queues & Partitions

Master the foundation of Queen MQ: queues and partitions. Understanding these concepts is key to building scalable, ordered message systems.

## Queues

A queue is a logical container for messages. Think of it as a category or topic for related messages.

### Creating Queues

```javascript
// Simple queue creation
await queen.queue('orders').create()

// Queue with configuration
await queen.queue('critical-tasks')
  .config({
    leaseTime: 60,        // 60 seconds to process
    retryLimit: 5,        // Retry up to 5 times
    priority: 10,         // High priority
    encryptionEnabled: true
  })
  .create()
```

### Queue Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `leaseTime` | number | 300 | Seconds before lease expires |
| `retryLimit` | number | 3 | Max retry attempts |
| `priority` | number | 0 | Queue priority (0-10) |
| `delayedProcessing` | number | 0 | Delay before available (seconds) |
| `windowBuffer` | number | 0 | Batch window (seconds) |
| `retentionSeconds` | number | 0 | Pending message retention (0=forever) |
| `completedRetentionSeconds` | number | 0 | Completed message retention |
| `encryptionEnabled` | boolean | false | Encrypt messages at rest |
| `deadLetterQueue` | boolean | false | Enable DLQ |

### Queue Operations

```javascript
// Create queue
await queen.queue('tasks').create()

// Delete queue (destructive!)
await queen.queue('tasks').delete()

// Get queue info
const info = await queen.getQueueInfo('tasks')

// List all queues
const queues = await queen.listQueues()
```

## Partitions

Partitions are the secret weapon that makes Queen powerful. They provide:
1. **FIFO Ordering**: Messages in the same partition are processed in order
2. **Parallelism**: Different partitions can be processed simultaneously
3. **Isolation**: Only one consumer can process a partition at a time

### The Partition Model

```
Queue: "orders"
├── Partition: "customer-123"
│   ├── Message 1 (processed by Consumer A)
│   ├── Message 2 (waits for Message 1)
│   └── Message 3 (waits for Message 2)
├── Partition: "customer-456"
│   ├── Message 1 (processed by Consumer B in parallel)
│   └── Message 2
└── Partition: "customer-789"
    └── Message 1 (processed by Consumer C in parallel)
```

### Creating Partitions

Partitions are created automatically when you push messages:

```javascript
// Push to specific partition
await queen.queue('orders')
  .partition('customer-123')
  .push([
    { data: { orderId: 'ORD-001', action: 'create' } },
    { data: { orderId: 'ORD-001', action: 'update' } },
    { data: { orderId: 'ORD-001', action: 'complete' } }
  ])

// These messages are processed IN ORDER
```

### Partition Keys

Choose partition keys based on your ordering requirements:

#### ✅ Good Partition Keys

```javascript
// Customer-based partitioning
.partition(`customer-${customerId}`)

// User-based partitioning
.partition(`user-${userId}`)

// Device-based partitioning (IoT)
.partition(`device-${deviceId}`)

// Session-based partitioning
.partition(`session-${sessionId}`)

// Tenant-based partitioning (multi-tenant)
.partition(`tenant-${tenantId}`)

// Entity-based partitioning
.partition(`entity-${entityType}-${entityId}`)
```

#### ❌ Bad Partition Keys

```javascript
// Random - loses ordering benefits
.partition(Math.random().toString())

// Timestamp - creates too many partitions
.partition(new Date().toISOString())

// Static - loses parallelism
.partition('static-key')

// Too granular - one partition per message
.partition(`message-${messageId}`)
```

### Partition Strategies

#### Strategy 1: Entity-Based Ordering

**Use Case**: Process events for the same entity in order

```javascript
// All events for the same order processed in sequence
await queen.queue('order-events')
  .partition(`order-${orderId}`)
  .push([
    { data: { event: 'created', orderId } },
    { data: { event: 'paid', orderId } },
    { data: { event: 'shipped', orderId } }
  ])
```

#### Strategy 2: User/Customer Affinity

**Use Case**: Keep all actions for a user/customer ordered

```javascript
// User actions processed in order
await queen.queue('user-actions')
  .partition(`user-${userId}`)
  .push([
    { data: { action: 'login', userId } },
    { data: { action: 'purchase', userId } },
    { data: { action: 'logout', userId } }
  ])
```

#### Strategy 3: Time Window Batching

**Use Case**: Batch messages by time window

```javascript
// Batch analytics events by hour
const hourKey = new Date().toISOString().substring(0, 13)
await queen.queue('analytics')
  .partition(`hour-${hourKey}`)
  .push([{ data: analytics }])
```

#### Strategy 4: Hash-Based Distribution

**Use Case**: Distribute load evenly

```javascript
// Hash customer ID to distribute across N partitions
const partitionCount = 100
const partitionId = hashCustomerId(customerId) % partitionCount

await queen.queue('customer-events')
  .partition(`partition-${partitionId}`)
  .push([{ data: event }])
```

## FIFO Guarantees

### What is Guaranteed

✅ **Within a Partition**:
- Messages are delivered in the order they were pushed
- No message skips ahead of another in the same partition
- If message 1 fails and retries, message 2 waits

❌ **Across Partitions**:
- No ordering guaranteed between different partitions
- Partitions are independent and can be processed in any order

### Example: Order Processing

```javascript
// Push three steps for the same order
await queen.queue('orders')
  .partition('order-12345')
  .push([
    { data: { step: 1, action: 'validate' } },
    { data: { step: 2, action: 'charge' } },
    { data: { step: 3, action: 'fulfill' } }
  ])

// Consumer processes them in order:
// 1. validate (succeeds)
// 2. charge (succeeds)
// 3. fulfill (succeeds)

// If charge fails:
// 1. validate (succeeds)
// 2. charge (fails, retries)
// 3. fulfill (WAITS for charge to succeed)
```

## Parallelism and Scaling

### Single Partition, Single Consumer

```
Queue: "tasks"
└── Partition: "Default"
    ├── Message 1 ──→ Consumer A
    ├── Message 2 ──→ (waits)
    └── Message 3 ──→ (waits)

Throughput: Limited by single consumer speed
```

### Multiple Partitions, Multiple Consumers

```
Queue: "tasks"
├── Partition A ──→ Consumer 1
├── Partition B ──→ Consumer 2
├── Partition C ──→ Consumer 3
└── Partition D ──→ Consumer 4

Throughput: 4x (scales with partition count)
```

### Scaling Example

```javascript
// Producer: Distribute across partitions
const partitions = ['A', 'B', 'C', 'D']

for (let i = 0; i < 1000; i++) {
  const partition = partitions[i % partitions.length]
  
  await queen.queue('tasks')
    .partition(partition)
    .push([{ data: { taskId: i } }])
}

// Consumer: Process partitions in parallel
await queen.queue('tasks')
  .concurrency(4)  // 4 parallel workers
  .consume(async (message) => {
    // Each worker handles different partitions
    await processTask(message.data)
  })
```

## Advanced Patterns

### Pattern 1: Sharded Processing

```javascript
// Shard by customer ID for even distribution
const shardCount = 50
const shard = customerId % shardCount

await queen.queue('customer-updates')
  .partition(`shard-${shard}`)
  .push([{ data: { customerId, update } }])

// Run 50 consumers, each handling one shard
for (let shard = 0; shard < 50; shard++) {
  queen.queue('customer-updates')
    .partition(`shard-${shard}`)
    .consume(async (message) => {
      await processCustomerUpdate(message.data)
    })
}
```

### Pattern 2: Priority Lanes

```javascript
// High-priority queue
await queen.queue('urgent-tasks')
  .config({ priority: 10 })
  .create()

// Normal-priority queue
await queen.queue('normal-tasks')
  .config({ priority: 5 })
  .create()

// Low-priority queue
await queen.queue('batch-tasks')
  .config({ priority: 1 })
  .create()

// Consumers process high-priority first
```

### Pattern 3: Workflow Stages

```javascript
// Stage 1: Ingest
await queen.queue('stage-1-ingest')
  .partition(`workflow-${workflowId}`)
  .push([{ data: rawData }])

// Consumer: Stage 1 → Stage 2
await queen.queue('stage-1-ingest').consume(async (msg) => {
  const validated = await validate(msg.data)
  
  await queen.transaction()
    .ack(msg)
    .queue('stage-2-process')
    .partition(`workflow-${msg.data.workflowId}`)
    .push([{ data: validated }])
    .commit()
})

// Consumer: Stage 2 → Stage 3
await queen.queue('stage-2-process').consume(async (msg) => {
  const processed = await process(msg.data)
  
  await queen.transaction()
    .ack(msg)
    .queue('stage-3-deliver')
    .partition(`workflow-${msg.data.workflowId}`)
    .push([{ data: processed }])
    .commit()
})
```

## Monitoring Partitions

```javascript
// Get partition information
const partitions = await queen.getPartitions('orders')

console.log(partitions)
// [
//   { partition: 'customer-123', messageCount: 5 },
//   { partition: 'customer-456', messageCount: 3 },
//   { partition: 'customer-789', messageCount: 0 }
// ]

// Check specific partition
const info = await queen.getPartitionInfo('orders', 'customer-123')

console.log(info)
// {
//   partition: 'customer-123',
//   messageCount: 5,
//   oldestMessage: '2025-01-01T10:00:00Z',
//   newestMessage: '2025-01-01T10:05:00Z'
// }
```

## Best Practices

### 1. Choose Appropriate Partition Keys

✅ **Do**:
- Use natural business entities (customer, order, device)
- Keep partition count reasonable (10s-1000s, not millions)
- Balance partition distribution evenly

❌ **Don't**:
- Use random keys (loses ordering)
- Use too many partitions (overhead)
- Use too few partitions (limits parallelism)

### 2. Design for Parallelism

```javascript
// Good: Partition by customer for parallelism
.partition(`customer-${customerId}`)

// Bad: Single partition limits throughput
.partition('Default')
```

### 3. Handle Partition Hotspots

```javascript
// Detect hotspot
const partitions = await queen.getPartitions('orders')
const maxMessages = Math.max(...partitions.map(p => p.messageCount))

if (maxMessages > 1000) {
  console.warn('Partition hotspot detected!')
  // Consider sub-partitioning or adding more consumers
}
```

### 4. Clean Up Unused Partitions

Queen automatically manages partitions, but you can monitor them:

```javascript
// Find inactive partitions
const inactive = partitions.filter(p => p.messageCount === 0)
console.log(`${inactive.length} inactive partitions`)
```

## Performance Considerations

### Partition Count vs Throughput

| Partitions | Consumers | Throughput | Notes |
|------------|-----------|------------|-------|
| 1 | 1 | 1x | Limited by single consumer |
| 10 | 10 | ~10x | Good parallelism |
| 100 | 10 | ~10x | Many partitions, few consumers |
| 100 | 100 | ~100x | Excellent parallelism |

### Optimal Partition Count

```javascript
// Rule of thumb: partition count ≈ max concurrent consumers
const optimalPartitions = maxConcurrentConsumers

// Example: 10 consumer processes, 10 partitions
const partitionCount = 10

for (let i = 0; i < messages.length; i++) {
  const partition = `partition-${i % partitionCount}`
  await queen.queue('tasks').partition(partition).push([...])
}
```

## Related Topics

- [Consumer Groups](/guide/consumer-groups) - Multiple groups processing same partitions
- [Transactions](/guide/transactions) - Atomic operations across queues
- [Long Polling](/guide/long-polling) - Efficient message waiting

## Summary

- **Queues** organize messages into logical groups
- **Partitions** provide FIFO ordering and enable parallelism
- **Partition keys** should be based on business entities
- **FIFO guarantees** apply within partitions, not across them
- **Scale** by adding more partitions and consumers
- **Monitor** partition distribution to avoid hotspots

Master queues and partitions, and you'll build scalable, ordered message systems! 🚀

