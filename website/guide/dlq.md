# Dead Letter Queue

Automatic handling of failed messages with configurable retry limits and DLQ routing.

## What is a Dead Letter Queue?

When a message fails processing after multiple retries, it's automatically moved to the Dead Letter Queue (DLQ) for manual intervention.

## Configuration

```javascript
await queen.queue('orders')
  .config({
    retryLimit: 3,           // Retry up to 3 times
    deadLetterQueue: true,   // Enable DLQ
    dlqAfterMaxRetries: true // Auto-move to DLQ
  })
  .create()
```

## Processing Failures

```javascript
await queen.queue('orders')
  .autoAck(false)
  .consume(async (message) => {
    try {
      await processOrder(message.data)
      await queen.ack(message, true)  // Success
    } catch (error) {
      await queen.ack(message, false)  // Fail - will retry
      // After retryLimit exceeded → moves to DLQ
    }
  })
```

## Viewing DLQ Messages

```javascript
// Get DLQ messages
const dlqMessages = await queen.getDLQMessages('orders')

console.log(dlqMessages)
// [{
//   transactionId: 'msg-123',
//   data: {...},
//   failureReason: 'Processing error',
//   retryCount: 3,
//   movedToDLQAt: '2025-01-01T10:00:00Z'
// }]
```

## Reprocessing DLQ Messages

```javascript
// Replay from DLQ back to main queue
await queen.replayFromDLQ('orders', 'msg-123')

// Or replay all
const dlqMessages = await queen.getDLQMessages('orders')
for (const msg of dlqMessages) {
  await queen.replayFromDLQ('orders', msg.transactionId)
}
```

## Monitoring

```javascript
// Count DLQ messages
const dlqCount = await queen.getDLQCount('orders')

if (dlqCount > 100) {
  console.warn(`High DLQ count: ${dlqCount}`)
  await alertTeam()
}
```

## Use Cases

### 1. Invalid Data

```javascript
// Messages with invalid data go to DLQ
await queen.queue('users')
  .consume(async (message) => {
    if (!validate(message.data)) {
      throw new Error('Invalid data format')
      // After retries → DLQ
    }
  })
```

### 2. External API Failures

```javascript
// Persistent API failures
await queen.queue('notifications')
  .consume(async (message) => {
    await sendEmail(message.data.email)
    // If email service down after 3 retries → DLQ
  })
```

### 3. Manual Review

```javascript
// Complex cases need human review
if (requiresManualReview) {
  await queen.ack(message, false, {
    reason: 'Requires manual review',
    skipRetry: true  // Go directly to DLQ
  })
}
```

## Best Practices

1. **Monitor DLQ** - Set up alerts for DLQ growth
2. **Regular Review** - Process DLQ messages periodically
3. **Log Failures** - Record why messages failed
4. **Fix Root Cause** - Investigate patterns in DLQ

## Related

- [Queues & Partitions](/guide/queues-partitions)
- [Transactions](/guide/transactions)
- [Message Tracing](/guide/tracing)

[See complete documentation](https://github.com/smartpricing/queen)
