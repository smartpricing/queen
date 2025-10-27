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
    └── validation.js           # Input validation
```

## Examples

See `/new-client-demo.js` for comprehensive examples of all features.

## Testing

```bash
node test-new-client.js
```

