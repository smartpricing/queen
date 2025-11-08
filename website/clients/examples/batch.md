# Batch Operations

High-throughput batch processing examples.

## Batch Push

```javascript
const messages = []
for (let i = 0; i < 1000; i++) {
  messages.push({ data: { id: i } })
}

await queen.queue('tasks').push(messages)
```

## Batch Consume

```javascript
await queen.queue('tasks')
  .batch(100)  // Fetch 100 at a time
  .concurrency(10)  // 10 parallel workers
  .consume(async (message) => {
    await process(message.data)
  })
```

[More examples](https://github.com/smartpricing/queen/tree/master/examples)
