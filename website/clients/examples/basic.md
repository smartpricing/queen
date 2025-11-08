# Basic Usage Examples

Simple examples to get you started with Queen MQ.

## Push and Pop

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

// Create queue
await queen.queue('tasks').create()

// Push messages
await queen.queue('tasks').push([
  { data: { task: 'send-email', to: 'user@example.com' } },
  { data: { task: 'generate-report', id: 123 } }
])

// Pop messages
const messages = await queen.queue('tasks').pop()

for (const message of messages) {
  console.log('Processing:', message.data)
  await queen.ack(message, true)
}
```

## Simple Consumer

```javascript
await queen.queue('tasks').consume(async (message) => {
  console.log('Got task:', message.data)
  await processTask(message.data)
  // Automatically ACK'd on success
})
```

More examples in [GitHub repository](https://github.com/smartpricing/queen/tree/master/examples).
