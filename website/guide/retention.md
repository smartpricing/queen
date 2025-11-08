# Message Retention

Control how long messages are kept with configurable retention policies.

## Configuration

```javascript
await queen.queue('logs')
  .config({
    retentionSeconds: 86400,           // Keep pending for 24 hours
    completedRetentionSeconds: 3600    // Keep completed for 1 hour
  })
  .create()
```

## Retention Types

### 1. Pending Message Retention

How long unprocessed messages are kept before automatic deletion.

```javascript
retentionSeconds: 86400  // 24 hours
retentionSeconds: 0      // Keep forever (default)
```

### 2. Completed Message Retention

How long successfully processed messages are kept.

```javascript
completedRetentionSeconds: 3600  // 1 hour
completedRetentionSeconds: 0     // Keep forever
```

## Use Cases

### Short-lived Events

```javascript
// Delete old analytics events
await queen.queue('analytics')
  .config({
    retentionSeconds: 604800,        // 7 days
    completedRetentionSeconds: 86400  // 1 day
  })
  .create()
```

### Long-term Audit

```javascript
// Keep audit logs forever
await queen.queue('audit')
  .config({
    retentionSeconds: 0,          // Never delete pending
    completedRetentionSeconds: 0  // Never delete completed
  })
  .create()
```

## Monitoring

```javascript
// Check queue size
const stats = await queen.getQueueStats('logs')

console.log(`Pending: ${stats.pendingCount}`)
console.log(`Completed: ${stats.completedCount}`)
```

## Best Practices

1. **Balance storage vs history** - Keep what you need
2. **Monitor disk usage** - Set up alerts
3. **Archive important messages** before deletion
4. **Test retention settings** in development first

[Complete documentation](https://github.com/smartpricing/queen)
