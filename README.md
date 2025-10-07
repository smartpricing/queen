# Queen - Message Queue System

A high-performance message queue system backed by PostgreSQL and powered by uWebSockets.js.

## Features

- ✅ Hierarchical organization: namespace → task → queue → message
- ✅ FIFO ordering within queues
- ✅ Transaction ID based idempotency
- ✅ Lease-based message processing with automatic retry
- ✅ Long polling with event-driven optimization
- ✅ Queue priorities and configuration
- ✅ Delayed processing and window buffering
- ✅ WebSocket support for real-time dashboard
- ✅ Comprehensive analytics and monitoring
- ✅ Client SDK with retry logic
- ✅ Batch operations for high throughput
- ✅ No message loss guarantees

## Quick Start

### 1. Install Dependencies

```bash
nvm use 22
npm install
```

### 2. Set Environment Variables (Optional)

```bash
export PG_USER=postgres
export PG_HOST=localhost
export PG_DB=postgres
export PG_PASSWORD=postgres
export PG_PORT=5432
```

### 3. Initialize Database

```bash
node init-db.js
```

### 4. Start the Server

```bash
npm start
```

The server will start on http://localhost:3000

### 5. Run Tests

In one terminal, start the server:
```bash
npm start
```

In another terminal, run the test:

**Produce 1000 items:**
```bash
npm run test:produce
```

**Consume the items:**
```bash
npm run test:consume
```

Or run both:
```bash
npm test
```

The consumed items will be saved to `consumed_items.json`.

## API Endpoints

### Configure Queue
```http
POST /api/v1/configure
{
  "ns": "namespace",
  "task": "task",
  "queue": "queue",
  "options": {
    "leaseTime": 300,
    "priority": 10,
    "delayedProcessing": 60,
    "windowBuffer": 30,
    "retryLimit": 3
  }
}
```

### Push Messages
```http
POST /api/v1/push
{
  "items": [
    {
      "ns": "namespace",
      "task": "task",
      "queue": "queue",
      "payload": { "data": "value" },
      "transactionId": "optional-uuid"
    }
  ]
}
```

### Pop Messages
```http
GET /api/v1/pop/ns/{ns}/task/{task}/queue/{queue}?wait=true&timeout=30000&batch=10
```

### Acknowledge Message
```http
POST /api/v1/ack
{
  "transactionId": "uuid",
  "status": "completed",
  "error": "optional error message if failed"
}
```

### Batch Acknowledge
```http
POST /api/v1/ack/batch
{
  "acknowledgments": [
    { "transactionId": "uuid1", "status": "completed" },
    { "transactionId": "uuid2", "status": "failed", "error": "reason" }
  ]
}
```

### Analytics
```http
GET /api/v1/analytics/queues
GET /api/v1/analytics/ns/{namespace}
GET /api/v1/analytics/ns/{namespace}/task/{task}
GET /api/v1/analytics/queue-depths
GET /api/v1/analytics/throughput
```

### WebSocket Dashboard
```
WS ws://localhost:6632/ws/dashboard
```

## Client SDK

```javascript
import { createQueenClient } from './src/client/index.js';

const client = createQueenClient({
  baseUrl: 'http://localhost:6632',
  timeout: 30000,
  retryAttempts: 3
});

// Configure queue
await client.configure({
  ns: 'my-app',
  task: 'emails',
  queue: 'send',
  options: { priority: 10, delayedProcessing: 60 }
});

// Push messages
await client.push({
  items: [{ ns: 'my-app', task: 'emails', queue: 'send', payload: data }]
});

// Pop with long polling
const result = await client.pop({
  ns: 'my-app', task: 'emails', queue: 'send',
  wait: true, timeout: 30000, batch: 10
});

// Process and acknowledge
for (const msg of result.messages) {
  await processMessage(msg);
  await client.ack(msg.transactionId, 'completed');
}

// Consumer helper
await client.consume({
  ns: 'my-app', task: 'emails', queue: 'send',
  handler: async (msg) => { /* process */ },
  options: { batch: 10, wait: true }
});
```

Test the client SDK:
```bash
npm run test:client
```

## Architecture

- **PostgreSQL**: Message persistence and ACID guarantees
- **uWebSockets.js**: High-performance HTTP/WebSocket server
- **Functional Design**: Factory functions and immutable patterns
- **Resource Caching**: In-memory cache for namespace/task/queue lookups
- **Lease-Based Processing**: Messages are leased to workers with automatic expiry
- **Event-Driven**: Event manager for optimized long polling
- **WebSocket Broadcasting**: Real-time dashboard updates

## Performance

The system is designed to handle:
- 10,000+ messages/second throughput
- < 10ms latency for immediate pop
- 1,000+ concurrent long polling connections

## Message Guarantees

1. **No Message Loss**: All messages are persisted in PostgreSQL
2. **FIFO Ordering**: Messages within a queue are processed in order
3. **At-Least-Once Delivery**: Failed messages are retried
4. **Idempotency**: Transaction IDs prevent duplicate processing
