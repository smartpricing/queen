# Queen Client V2

Clean, fluent API for Queen Message Queue with smart defaults.

## Features

✅ **Fluent API** - Chainable, readable method calls
✅ **Smart Defaults** - Works out of the box with sensible defaults  
✅ **Load Balancing** - Round-robin and session strategies
✅ **Auto-Failover** - Automatic server failover on errors
✅ **Client-Side Buffering** - High-throughput message batching
✅ **Transactions** - Atomic ack + push operations
✅ **Concurrent Workers** - Parallel message processing
✅ **Auto-Ack** - Optional automatic acknowledgment
✅ **Graceful Shutdown** - SIGTERM/SIGKILL handlers + buffer flush
✅ **Lease Renewal** - Manual and automatic lease extension
✅ **Operation Logging** - Detailed logging with timestamps (opt-in)

## Quick Start

```javascript
import { Queen } from './client-js/client-v2/index.js'

// Create client
const queen = new Queen('http://localhost:6632')

// Create queue
await queen.queue('tasks').create()

// Push message
await queen.queue('tasks').push([
  { payload: { job: 'process-data', id: 123 } }
])

// Consume messages (with auto-ack)
await queen.queue('tasks').consume(async (msg) => {
  console.log('Processing:', msg.payload)
  // Automatically ack'd on success, nack'd on throw
})

// Pop and manual ack
const messages = await queen.queue('tasks').pop()
await queen.ack(messages[0], true)

// Graceful shutdown
await queen.close()
```

## Time Unit Conventions

- **`Millis` suffix** → milliseconds (`timeoutMillis`, `idleMillis`, `timeMillis`)
- **`Seconds` suffix** → seconds (`retentionSeconds`, `completedRetentionSeconds`)
- **No suffix** (time-related) → seconds (`leaseTime`, `delayedProcessing`, `windowBuffer`)

## Logging

The client includes comprehensive operation logging that is **disabled by default**. Enable it via environment variable:

```bash
export QUEEN_CLIENT_LOG=true
node your-app.js
```

When enabled, all operations are logged with:
- **ISO 8601 timestamp** - Exact time of operation
- **Operation name** - Component and method (e.g., `Queen.push`, `HttpClient.request`)
- **Relevant context** - Queue name, message count, error details, etc.

Example log output:
```
[2025-10-28T10:30:45.123Z] [INFO] [Queen.constructor] {"status":"initialized","urls":1}
[2025-10-28T10:30:45.234Z] [INFO] [QueueBuilder.push] {"queue":"tasks","partition":"Default","count":5,"buffered":true}
[2025-10-28T10:30:45.456Z] [INFO] [BufferManager.addMessage] {"queueAddress":"tasks/Default","messageCount":5}
[2025-10-28T10:30:46.789Z] [INFO] [HttpClient.request] {"method":"POST","url":"http://localhost:6632/api/v1/push","hasBody":true,"timeout":30000}
[2025-10-28T10:30:46.890Z] [INFO] [HttpClient.response] {"method":"POST","url":"http://localhost:6632/api/v1/push","status":200}
```

Logged operations include:
- Client initialization and shutdown
- Queue operations (create, delete, push, pop, consume)
- HTTP requests and responses (including retries and failover)
- Buffer operations (add, flush, cleanup)
- Consumer worker lifecycle
- Message processing (ack/nack)
- Transaction commits
- Lease renewals
- DLQ queries

## Architecture

```
client-v2/
├── Queen.js                    # Main client class
├── index.js                    # Entry point
├── builders/
│   ├── QueueBuilder.js         # Fluent queue operations
│   └── TransactionBuilder.js   # Atomic transactions
├── http/
│   ├── HttpClient.js           # HTTP with retry
│   └── LoadBalancer.js         # Load balancing
├── buffer/
│   ├── BufferManager.js        # Client-side buffering
│   └── MessageBuffer.js        # Per-queue buffer
├── consumer/
│   └── ConsumerManager.js      # Worker management
└── utils/
    ├── defaults.js             # Default values
    ├── validation.js           # Input validation
    └── logger.js               # Operation logging
```

## Examples

See `/new-client-demo.js` for comprehensive examples of all features.

## Testing

```bash
node test-new-client.js
```

