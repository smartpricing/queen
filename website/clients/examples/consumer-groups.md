# Consumer Group Examples

Multiple independent processors.

## Multiple Purposes

```javascript
// Analytics
await queen.queue('events')
  .group('analytics')
  .subscribe('from_beginning')
  .consume(async (event) => {
    await updateMetrics(event.data)
  })

// Notifications
await queen.queue('events')
  .group('notifications')
  .subscribe('new_only')
  .consume(async (event) => {
    await sendNotification(event.data)
  })
```

[More examples](https://github.com/smartpricing/queen/blob/master/examples/08-consumer-groups.js)
