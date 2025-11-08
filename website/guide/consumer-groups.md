# Consumer Groups

Consumer groups enable Kafka-style message distribution where multiple independent groups can process the same messages. This powerful feature allows you to use messages for different purposes simultaneously.

## What are Consumer Groups?

A consumer group is an independent cursor that tracks its position in a queue. Multiple consumer groups can read from the same queue, each maintaining their own position.

```javascript
// Group 1: Analytics
await queen.queue('events')
  .group('analytics-team')
  .consume(async (message) => {
    await updateAnalytics(message.data)
  })

// Group 2: Notifications
await queen.queue('events')
  .group('notifications-team')
  .consume(async (message) => {
    await sendNotification(message.data)
  })

// Both groups process ALL messages independently!
```

## Queue Mode vs Consumer Groups

### Queue Mode (Default)

Without a consumer group, messages are distributed across consumers (traditional queue behavior):

```javascript
// Queue mode - messages distributed
await queen.queue('tasks').consume(async (message) => {
  // Each message processed by ONE consumer only
})
```

```
Queue: "tasks"
├── Message 1 → Consumer A
├── Message 2 → Consumer B
├── Message 3 → Consumer A
└── Message 4 → Consumer B

Result: Work distributed, each message processed once
```

### Consumer Group Mode

With consumer groups, ALL messages are delivered to each group:

```javascript
// Consumer group mode
await queen.queue('events')
  .group('group-1')
  .consume(async (message) => {
    // Process for group 1
  })

await queen.queue('events')
  .group('group-2')
  .consume(async (message) => {
    // Process for group 2
  })
```

```
Queue: "events"
├── Message 1 → Group 1 Consumer A
│             → Group 2 Consumer X
├── Message 2 → Group 1 Consumer B
│             → Group 2 Consumer Y
├── Message 3 → Group 1 Consumer A
│             → Group 2 Consumer X

Result: Each group processes ALL messages
```

## Creating Consumer Groups

Consumer groups are created automatically when first used:

```javascript
// First consumer in the group creates it
await queen.queue('orders')
  .group('order-processor')
  .subscribe('new_only')  // Start from new messages
  .consume(async (message) => {
    await processOrder(message.data)
  })
```

## Subscription Modes

Control where a consumer group starts reading:

### 1. New Messages Only (Default)

```javascript
await queen.queue('events')
  .group('new-consumer')
  .subscribe('new_only')  // Default
  .consume(async (message) => {
    // Only processes messages that arrive after subscription
  })
```

Use when: You only care about new events, not historical data.

### 2. From Beginning

```javascript
await queen.queue('events')
  .group('replay-consumer')
  .subscribe('from_beginning')
  .consume(async (message) => {
    // Processes ALL messages from the start
  })
```

Use when: Replaying historical data, backfilling analytics, or testing.

### 3. From Timestamp

```javascript
await queen.queue('events')
  .group('historical-consumer')
  .subscribe('from_timestamp', new Date('2025-01-01T00:00:00Z'))
  .consume(async (message) => {
    // Processes messages from Jan 1, 2025 onwards
  })
```

Use when: You need to start from a specific point in time.

## Consumer Group Patterns

### Pattern 1: Multi-Purpose Processing

Process the same events for different purposes:

```javascript
// Purpose 1: Real-time analytics
await queen.queue('user-events')
  .group('analytics')
  .subscribe('from_beginning')  // Backfill historical data
  .consume(async (event) => {
    await metrics.track(event.data.userId, event.data.action)
  })

// Purpose 2: Notification system
await queen.queue('user-events')
  .group('notifications')
  .subscribe('new_only')  // Only new events
  .consume(async (event) => {
    if (event.data.action === 'purchase') {
      await sendPurchaseEmail(event.data.userId)
    }
  })

// Purpose 3: Audit log
await queen.queue('user-events')
  .group('audit')
  .subscribe('from_beginning')
  .consume(async (event) => {
    await auditLog.record(event.data)
  })
```

### Pattern 2: A/B Testing

Run old and new implementations in parallel:

```javascript
// Current production implementation
await queen.queue('orders')
  .group('order-processor-v1')
  .consume(async (order) => {
    await processOrderV1(order.data)
  })

// New implementation being tested
await queen.queue('orders')
  .group('order-processor-v2')
  .consume(async (order) => {
    await processOrderV2(order.data)
    // Compare results, measure performance
  })

// Both process the same orders!
```

### Pattern 3: Development/Testing

Create separate consumer groups for different environments:

```javascript
// Production consumer group
if (process.env.NODE_ENV === 'production') {
  await queen.queue('tasks')
    .group('prod-workers')
    .consume(async (task) => {
      await processTask(task.data)
    })
}

// Development consumer group (doesn't affect production)
if (process.env.NODE_ENV === 'development') {
  await queen.queue('tasks')
    .group('dev-workers')
    .subscribe('from_beginning')  // Test with real data
    .consume(async (task) => {
      await testProcessTask(task.data)
    })
}
```

### Pattern 4: Fan-Out Processing

One source, multiple destinations:

```javascript
// Single producer
await queen.queue('events').push([
  { data: { type: 'user_signup', userId: 123 } }
])

// Multiple consumers, each doing different work
const consumers = [
  {
    group: 'email-service',
    handler: async (event) => {
      await sendWelcomeEmail(event.data.userId)
    }
  },
  {
    group: 'crm-sync',
    handler: async (event) => {
      await syncToCRM(event.data)
    }
  },
  {
    group: 'analytics',
    handler: async (event) => {
      await trackSignup(event.data.userId)
    }
  },
  {
    group: 'webhook-service',
    handler: async (event) => {
      await triggerWebhooks(event.data)
    }
  }
]

// Start all consumers
for (const consumer of consumers) {
  queen.queue('events')
    .group(consumer.group)
    .consume(consumer.handler)
}
```

## Scaling Consumer Groups

### Single Consumer per Group

```javascript
// One consumer in the group
await queen.queue('events')
  .group('processor')
  .concurrency(1)
  .consume(async (message) => {
    // Process messages sequentially
  })
```

### Multiple Consumers per Group

```javascript
// Multiple consumers in the same group
// They cooperate to process partitions

// Consumer 1
await queen.queue('events')
  .group('processor')
  .concurrency(5)
  .consume(async (message) => {
    // Handles some partitions
  })

// Consumer 2 (different process/machine)
await queen.queue('events')
  .group('processor')
  .concurrency(5)
  .consume(async (message) => {
    // Handles other partitions
  })
```

**Partition Distribution:**
```
Queue: "events"
├── Partition A → Group "processor" → Consumer 1
├── Partition B → Group "processor" → Consumer 2
├── Partition C → Group "processor" → Consumer 1
└── Partition D → Group "processor" → Consumer 2
```

## Managing Consumer Groups

### List Consumer Groups

```javascript
const groups = await queen.listConsumerGroups('events')

console.log(groups)
// [
//   { name: 'analytics', position: 1250 },
//   { name: 'notifications', position: 1248 },
//   { name: 'audit', position: 1250 }
// ]
```

### Get Consumer Group Info

```javascript
const info = await queen.getConsumerGroupInfo('events', 'analytics')

console.log(info)
// {
//   group: 'analytics',
//   queue: 'events',
//   position: 1250,
//   lag: 5,  // Messages behind
//   partitions: [
//     { partition: 'A', position: 100, lag: 2 },
//     { partition: 'B', position: 200, lag: 3 }
//   ]
// }
```

### Reset Consumer Group Position

```javascript
// Reset to beginning
await queen.resetConsumerGroup('events', 'analytics', 'beginning')

// Reset to specific timestamp
await queen.resetConsumerGroup(
  'events',
  'analytics',
  'timestamp',
  new Date('2025-01-01')
)

// Reset to end (skip all pending)
await queen.resetConsumerGroup('events', 'analytics', 'end')
```

### Delete Consumer Group

```javascript
// Remove consumer group entirely
await queen.deleteConsumerGroup('events', 'old-processor')
```

## Consumer Group Lag

Lag indicates how far behind a consumer group is:

```javascript
const lag = await queen.getConsumerGroupLag('events', 'analytics')

console.log(lag)
// {
//   group: 'analytics',
//   totalLag: 1250,  // Total messages behind
//   partitions: [
//     { partition: 'A', lag: 500 },
//     { partition: 'B', lag: 750 }
//   ]
// }

// Alert if lag is too high
if (lag.totalLag > 10000) {
  console.warn('Consumer group falling behind!')
  // Scale up consumers or investigate slow processing
}
```

## Advanced Patterns

### Pattern: Data Migration

Use consumer groups to migrate data:

```javascript
// Create migration consumer group
await queen.queue('user-data')
  .group('migration-to-new-system')
  .subscribe('from_beginning')
  .batch(100)  // Process in batches
  .consume(async (message) => {
    // Migrate data to new system
    await newSystem.import(message.data)
  })
  .onSuccess(async (message) => {
    await queen.ack(message, true)
    
    // Track progress
    const lag = await queen.getConsumerGroupLag('user-data', 'migration-to-new-system')
    console.log(`Migration progress: ${lag.totalLag} remaining`)
  })
```

### Pattern: Time-Travel Debugging

Replay messages for debugging:

```javascript
// Create debug consumer group
await queen.queue('transactions')
  .group('debug-session-' + Date.now())
  .subscribe('from_timestamp', problemStartTime)
  .consume(async (message) => {
    // Replay and debug problematic messages
    console.log('Replaying:', message.data)
    
    try {
      await processTransaction(message.data)
    } catch (error) {
      console.error('Found the bug:', error)
      process.exit(0)  // Stop when found
    }
  })
```

### Pattern: Aggregate Views

Build multiple aggregate views from same source:

```javascript
// Aggregate by customer
await queen.queue('orders')
  .group('customer-aggregates')
  .subscribe('from_beginning')
  .consume(async (order) => {
    await updateCustomerStats(order.data.customerId, order.data)
  })

// Aggregate by product
await queen.queue('orders')
  .group('product-aggregates')
  .subscribe('from_beginning')
  .consume(async (order) => {
    for (const item of order.data.items) {
      await updateProductStats(item.productId, item)
    }
  })

// Aggregate by region
await queen.queue('orders')
  .group('region-aggregates')
  .subscribe('from_beginning')
  .consume(async (order) => {
    await updateRegionStats(order.data.region, order.data)
  })
```

## Best Practices

### 1. Name Consumer Groups Descriptively

```javascript
// ✅ Good: Describes purpose
.group('analytics-daily-reports')
.group('email-notification-service')
.group('crm-sync-v2')

// ❌ Bad: Unclear purpose
.group('consumer1')
.group('test')
.group('new')
```

### 2. Choose Appropriate Subscription Mode

```javascript
// ✅ Good: New consumer, only new messages
.group('new-feature').subscribe('new_only')

// ✅ Good: Backfill analytics from beginning
.group('analytics-backfill').subscribe('from_beginning')

// ✅ Good: Recover from specific incident
.group('recovery').subscribe('from_timestamp', incidentTime)
```

### 3. Monitor Consumer Group Lag

```javascript
// Set up monitoring
setInterval(async () => {
  const groups = await queen.listConsumerGroups('orders')
  
  for (const group of groups) {
    const lag = await queen.getConsumerGroupLag('orders', group.name)
    
    if (lag.totalLag > THRESHOLD) {
      await alertTeam(`Group ${group.name} lag: ${lag.totalLag}`)
    }
  }
}, 60000)  // Check every minute
```

### 4. Clean Up Old Consumer Groups

```javascript
// Remove unused consumer groups
const groups = await queen.listConsumerGroups('events')

for (const group of groups) {
  if (group.name.includes('test-') || group.name.includes('debug-')) {
    const lastUsed = await getLastUsedTime(group.name)
    
    if (Date.now() - lastUsed > 7 * 24 * 60 * 60 * 1000) {  // 7 days
      await queen.deleteConsumerGroup('events', group.name)
      console.log(`Deleted old group: ${group.name}`)
    }
  }
}
```

## Comparison with Other Systems

| Feature | Queen | Kafka | RabbitMQ | NATS |
|---------|-------|-------|----------|------|
| Consumer Groups | ✅ Native | ✅ Native | ⚠️ Pattern | ✅ Queue Groups |
| Replay from Beginning | ✅ Yes | ✅ Yes | ❌ No | ⚠️ Limited |
| Replay from Timestamp | ✅ Yes | ❌ Offset only | ❌ No | ❌ No |
| Independent Cursors | ✅ Yes | ✅ Yes | ❌ No | ✅ Yes |
| Dynamic Groups | ✅ Yes | ✅ Yes | ⚠️ Manual | ✅ Yes |

## Related Topics

- [Queues & Partitions](/guide/queues-partitions) - Understanding FIFO ordering
- [Transactions](/guide/transactions) - Atomic operations
- [Long Polling](/guide/long-polling) - Efficient message waiting
- [JavaScript Client](/clients/javascript) - Complete API reference

## Summary

Consumer groups in Queen MQ provide:

- **Multiple Purposes**: Process same messages for different reasons
- **Independent Cursors**: Each group tracks its own position
- **Replay Capability**: Start from beginning, timestamp, or end
- **Scalability**: Add consumers to a group for parallelism
- **Flexibility**: Perfect for analytics, testing, and fan-out patterns

Master consumer groups to unlock the full power of Queen MQ! 🚀

