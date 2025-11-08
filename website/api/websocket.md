# WebSocket API

Real-time streaming via WebSocket.

## Connection

```javascript
const ws = new WebSocket('ws://localhost:6632/stream')

ws.onopen = () => {
  // Subscribe to queue
  ws.send(JSON.stringify({
    action: 'subscribe',
    queue: 'events',
    partition: 'Default'
  }))
}

ws.onmessage = (event) => {
  const message = JSON.parse(event.data)
  console.log('Real-time:', message)
}
```

## Using Client Library

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

await queen.queue('events')
  .stream()
  .subscribe(async (message) => {
    console.log('Streaming:', message.data)
  })
```

## Use Cases

- Real-time dashboards
- Live notifications
- Event monitoring
- Log tailing
- Metrics streaming

[Examples](https://github.com/smartpricing/queen/tree/master/examples)
