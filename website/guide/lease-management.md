# Lease Management

Manage message leases for long-running tasks with automatic renewal.

## What is a Lease?

When a consumer receives a message, it gets an exclusive lock (lease) for a configured time period.

```javascript
// Configure lease time
await queen.queue('long-tasks')
  .config({ leaseTime: 300 })  // 5 minutes
  .create()
```

## Automatic Lease Renewal

For long-running tasks, auto-renew the lease:

```javascript
await queen
  .queue('video-processing')
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

## Manual Lease Renewal

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

## Lease Expiration

If lease expires before ACK:
- Message becomes available again
- Another consumer can pick it up
- Original consumer's ACK will fail

## Best Practices

1. **Set appropriate lease time** based on processing duration
2. **Use auto-renewal** for unpredictable tasks
3. **Monitor lease expirations** to detect slow consumers
4. **Handle ACK failures** gracefully

## Related

- [Queues & Partitions](/guide/queues-partitions)
- [Transactions](/guide/transactions)

[Full documentation](https://github.com/smartpricing/queen)
