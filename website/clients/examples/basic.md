# Basic Usage Examples

Simple examples to get you started with Queen MQ.

## Push and Pop

::: code-group

```javascript [JavaScript]
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

```python [Python]
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create queue
        await queen.queue('tasks').create()
        
        # Push messages
        await queen.queue('tasks').push([
            {'data': {'task': 'send-email', 'to': 'user@example.com'}},
            {'data': {'task': 'generate-report', 'id': 123}}
        ])
        
        # Pop messages
        messages = await queen.queue('tasks').pop()
        
        for message in messages:
            print('Processing:', message['data'])
            await queen.ack(message, True)

asyncio.run(main())
```

:::

## Simple Consumer

::: code-group

```javascript [JavaScript]
await queen.queue('tasks').consume(async (message) => {
  console.log('Got task:', message.data)
  await processTask(message.data)
  // Automatically ACK'd on success
})
```

```python [Python]
async def handler(message):
    print('Got task:', message['data'])
    await process_task(message['data'])
    # Automatically ACK'd on success

await queen.queue('tasks').consume(handler)
```

:::

More examples in [GitHub repository](https://github.com/smartpricing/queen/tree/master/examples).
