# JavaScript Client

Complete API reference for the Queen MQ JavaScript/Node.js client library.

## Installation

```bash
npm install queen-mq
```

**Requirements:** Node.js 22+

## Quick Start

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

// Create queue
await queen.queue('tasks').create()

// Push messages
await queen.queue('tasks').push([
  { data: { task: 'send-email' } }
])

// Consume messages
await queen.queue('tasks').consume(async (message) => {
  console.log('Processing:', message.data)
})
```

## Connection

### Single Server

```javascript
const queen = new Queen('http://localhost:6632')
```

### Multiple Servers (High Availability)

```javascript
const queen = new Queen([
  'http://server1:6632',
  'http://server2:6632'
])
```

### Full Configuration

```javascript
const queen = new Queen({
  urls: ['http://server1:6632', 'http://server2:6632'],
  timeoutMillis: 30000,
  retryAttempts: 3,
  loadBalancingStrategy: 'round-robin',  // or 'session'
  enableFailover: true
})
```

## Queue Operations

### Create Queue

```javascript
await queen.queue('orders')
  .config({
    leaseTime: 60,
    retryLimit: 3,
    priority: 5,
    encryptionEnabled: false
  })
  .create()
```

### Delete Queue

```javascript
await queen.queue('orders').delete()
```

### Get Queue Info

```javascript
const info = await queen.getQueueInfo('orders')
```

## Pushing Messages

### Basic Push

```javascript
await queen.queue('tasks').push([
  { data: { task: 'process-order', orderId: 123 } }
])
```

### With Partition

```javascript
await queen.queue('tasks')
  .partition('customer-123')
  .push([
    { data: { action: 'create' } },
    { data: { action: 'update' } }
  ])
```

### With Custom Transaction ID

```javascript
await queen.queue('tasks').push([
  {
    transactionId: 'unique-id-123',
    data: { ... }
  }
])
```

### With Callbacks

```javascript
await queen.queue('tasks').push([...])
  .onSuccess(async (messages) => {
    console.log('Pushed:', messages)
  })
  .onDuplicate(async (messages) => {
    console.warn('Duplicates detected')
  })
  .onError(async (messages, error) => {
    console.error('Failed:', error)
  })
```

## Consuming Messages

### Basic Consume

```javascript
await queen.queue('tasks').consume(async (message) => {
  await processTask(message.data)
})
```

### With Consumer Group

```javascript
await queen.queue('events')
  .group('analytics')
  .subscribe('from_beginning')
  .consume(async (message) => {
    await updateAnalytics(message.data)
  })
```

### With Configuration

```javascript
await queen.queue('tasks')
  .concurrency(10)      // 10 parallel workers
  .batch(20)            // Fetch 20 at a time
  .autoAck(false)       // Manual ack
  .renewLease(true, 5000)  // Auto-renew every 5s
  .each()               // Process individually
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

## Pop Operations

### Basic Pop

```javascript
const messages = await queen.queue('tasks').pop()
```

### With Long Polling

```javascript
const messages = await queen.queue('tasks')
  .wait(true)
  .timeout(30000)
  .pop()
```

### With Batch

```javascript
const messages = await queen.queue('tasks')
  .batch(10)
  .pop()
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

### With Options

```javascript
await queen.ack(message, false, {
  reason: 'Invalid data',
  skipRetry: true  // Go directly to DLQ
})
```

## Transactions

```javascript
await queen.transaction()
  .ack(inputMessage)
  .queue('output')
  .partition('workflow-1')
  .push([{ data: result }])
  .commit()
```

## Streaming

```javascript
await queen.queue('events')
  .stream()
  .subscribe(async (message) => {
    console.log('Real-time:', message.data)
  })
```

## Complete Guide

For comprehensive examples and patterns, see:

- [Quick Start](/guide/quickstart)
- [Basic Concepts](/guide/concepts)
- [Examples](/clients/examples/basic)
- [Source README](https://github.com/smartpricing/queen/blob/master/client-js/client-v2/README.md)

## TypeScript Support

Full TypeScript definitions included:

```typescript
import { Queen, Message, QueueConfig } from 'queen-mq'

const queen: Queen = new Queen('http://localhost:6632')

interface OrderData {
  orderId: number
  amount: number
}

const message: Message<OrderData> = await queen.queue('orders').pop()
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

## Best Practices

1. ✅ Use consumer groups for scalability
2. ✅ Enable long polling for efficiency
3. ✅ Set appropriate batch sizes
4. ✅ Use transactions for atomicity
5. ✅ Handle errors gracefully
6. ✅ Monitor queue depths
7. ✅ Use meaningful partition keys

## Support

- [GitHub](https://github.com/smartpricing/queen)
- [LinkedIn](https://www.linkedin.com/company/smartness-com/)
- [Examples](/clients/examples/basic)
