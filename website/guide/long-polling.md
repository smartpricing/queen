# Long Polling

Long polling allows clients to wait server-side for messages to become available, providing low-latency message delivery while minimizing resource usage.

## What is Long Polling?

Instead of repeatedly asking "Any messages?", your client sends one request and the server holds it open until messages arrive or a timeout occurs.

### Traditional Polling (Inefficient)

```javascript
// Bad: Busy loop wastes resources
while (true) {
  const messages = await queen.queue('tasks').pop()
  if (messages.length > 0) {
    process(messages)
  }
  await sleep(100)  // Still wastes CPU and network
}
```

**Problems:**
- High network traffic
- High CPU usage
- Increased latency
- Resource waste

### Long Polling (Efficient)

```javascript
// Good: Wait server-side
while (true) {
  const messages = await queen
    .queue('tasks')
    .wait(true)  // Enable long polling!
    .timeout(30000)  // Wait up to 30 seconds
    .pop()
  
  if (messages.length > 0) {
    await process(messages)
  }
}
```

**Benefits:**
- Low network traffic (one request waits)
- Low CPU usage (server waits efficiently)
- Lower latency (immediate delivery)
- Resource efficient

## Basic Usage

### With Pop

```javascript
// Wait for messages with pop()
const messages = await queen
  .queue('tasks')
  .wait(true)
  .timeout(30000)  // Optional: 30 second timeout
  .pop()

// Returns immediately when messages arrive
// Or returns empty array after timeout
```

### With Consume

```javascript
// Consume automatically uses long polling
await queen
  .queue('tasks')
  .consume(async (message) => {
    // Messages delivered immediately when available
    await processMessage(message.data)
  })
```

The `consume()` method uses long polling internally, so you don't need to enable it explicitly.

## How It Works

### Architecture

```
┌─────────┐
│ Client  │ ─── POP with wait=true&timeout=30000
└─────────┘
     ↓
┌──────────┐
│  Worker  │ ─── Registers intention in registry
│  Thread  │ ─── Returns (thread free!)
└──────────┘
     ↓
┌──────────┐
│ Poll     │ ─── Checks every 50ms
│ Worker   │ ─── Queries database when ready
└──────────┘
     ↓
┌──────────┐
│ Database │ ─── Non-blocking query
└──────────┘
     ↓
┌──────────┐
│ Response │ ─── Messages sent to client
└──────────┘
```

### Flow

1. **Client** sends POP request with `wait=true`
2. **Worker** registers a "poll intention" and returns
3. **Poll Worker** (dedicated thread) wakes every 50ms
4. **Poll Worker** groups intentions and queries database
5. **Database** returns messages (if available)
6. **Poll Worker** distributes messages to waiting clients
7. **Client** receives messages instantly

## Configuration

### Timeout

Control how long to wait:

```javascript
// Wait up to 60 seconds
const messages = await queen
  .queue('tasks')
  .wait(true)
  .timeout(60000)  // milliseconds
  .pop()
```

**Recommended timeouts:**
- Short: 5-10 seconds (interactive apps)
- Medium: 30 seconds (background workers)
- Long: 60+ seconds (batch processing)

### Batch Size

Fetch multiple messages when they arrive:

```javascript
// Wait for up to 10 messages
const messages = await queen
  .queue('tasks')
  .wait(true)
  .batch(10)
  .pop()

// Returns 1-10 messages or empty array on timeout
```

## Performance Characteristics

### Latency

- **Without long polling**: 50-500ms (depends on poll interval)
- **With long polling**: 10-100ms (near-instant delivery)

### Resource Usage

**Traditional Polling (100ms interval):**
- Requests/minute: 600
- CPU: High (constant querying)
- Network: High (many small requests)

**Long Polling (30s timeout):**
- Requests/minute: 2-120 (depends on message frequency)
- CPU: Low (efficient waiting)
- Network: Low (one request waits)

## Best Practices

### 1. Use Appropriate Timeouts

```javascript
// ✅ Good: Reasonable timeout
.timeout(30000)  // 30 seconds

// ❌ Too short: frequent reconnects
.timeout(1000)  // 1 second

// ❌ Too long: slow shutdown
.timeout(300000)  // 5 minutes
```

### 2. Handle Timeouts Gracefully

```javascript
while (running) {
  const messages = await queen
    .queue('tasks')
    .wait(true)
    .timeout(30000)
    .pop()
  
  if (messages.length === 0) {
    // Timeout occurred - no messages
    console.log('No messages, will retry')
    continue
  }
  
  await processMessages(messages)
}
```

### 3. Use with Consumer Groups

```javascript
// Long polling works great with consumer groups
await queen
  .queue('events')
  .group('processor')
  .concurrency(10)  // 10 parallel consumers
  .batch(20)  // Fetch 20 at a time
  .consume(async (message) => {
    // Long polling happens automatically
    await process(message.data)
  })
```

### 4. Graceful Shutdown

```javascript
let running = true

process.on('SIGTERM', () => {
  running = false
})

while (running) {
  const messages = await queen
    .queue('tasks')
    .wait(true)
    .timeout(5000)  // Shorter timeout for faster shutdown
    .pop()
  
  if (messages.length > 0) {
    await processMessages(messages)
  }
}

console.log('Shutdown complete')
```

## Comparison

| Method | Requests/min | Latency | CPU Usage | Best For |
|--------|--------------|---------|-----------|----------|
| No polling | Manual | Varies | None | Interactive scripts |
| Traditional polling (100ms) | 600 | 50-150ms | High | Legacy systems |
| Long polling (30s) | 2-120 | 10-100ms | Low | Modern apps ✅ |
| WebSocket streaming | N/A | <10ms | Low | Real-time feeds |

## Advanced Patterns

### Pattern: Adaptive Timeout

```javascript
let timeout = 30000  // Start with 30s

while (running) {
  const start = Date.now()
  
  const messages = await queen
    .queue('tasks')
    .wait(true)
    .timeout(timeout)
    .pop()
  
  const elapsed = Date.now() - start
  
  if (messages.length > 0) {
    // Messages arriving quickly - use shorter timeout
    timeout = Math.max(5000, timeout / 2)
    await process(messages)
  } else {
    // No messages - use longer timeout
    timeout = Math.min(60000, timeout * 1.5)
  }
}
```

### Pattern: Multi-Queue Polling

```javascript
// Poll multiple queues with Promise.race
async function pollMultipleQueues() {
  const queues = ['queue1', 'queue2', 'queue3']
  
  while (running) {
    // Race all queues - process first to respond
    const results = await Promise.race(
      queues.map(q =>
        queen.queue(q)
          .wait(true)
          .timeout(30000)
          .pop()
          .then(msgs => ({ queue: q, messages: msgs }))
      )
    )
    
    if (results.messages.length > 0) {
      console.log(`Got messages from ${results.queue}`)
      await process(results.messages)
    }
  }
}
```

## Troubleshooting

### Connections Timing Out

```javascript
// Symptom: Frequent timeouts, no messages delivered

// Solution 1: Check if messages exist
const count = await queen.getQueueDepth('tasks')
console.log(`Queue depth: ${count}`)

// Solution 2: Increase timeout
.timeout(60000)  // Try 60 seconds

// Solution 3: Check network stability
// Ensure client can maintain long connections
```

### High Latency

```javascript
// Symptom: Messages take long to arrive

// Solution 1: Check poll worker configuration
// Server should have NUM_POLL_WORKERS=4 (default)

// Solution 2: Monitor server logs
// Look for "Poll worker" messages

// Solution 3: Check database performance
// Slow queries affect poll workers
```

### Memory Leaks

```javascript
// Symptom: Memory grows over time

// Solution: Ensure proper cleanup
const controller = new AbortController()

try {
  while (running) {
    const messages = await queen
      .queue('tasks')
      .wait(true)
      .timeout(30000)
      .signal(controller.signal)  // Add abort signal
      .pop()
    
    await process(messages)
  }
} finally {
  controller.abort()  // Cancel any pending requests
}
```

## Server Configuration

Long polling is configured on the server:

```bash
# Number of poll worker threads
NUM_POLL_WORKERS=4  # Default

# Poll interval (milliseconds)
POLL_INTERVAL_MS=50  # Default

# Max poll timeout (milliseconds)
MAX_POLL_TIMEOUT_MS=120000  # Default: 2 minutes
```

## Related Topics

- [Streaming](/guide/streaming) - Real-time WebSocket streaming
- [Consumer Groups](/guide/consumer-groups) - Scaling consumption
- [Performance Tuning](/server/tuning) - Optimize server settings

## Summary

Long polling in Queen MQ provides:

- ✅ **Low Latency**: Near-instant message delivery
- ✅ **Resource Efficient**: Minimal CPU and network usage
- ✅ **Simple API**: Just add `.wait(true)`
- ✅ **Scalable**: Dedicated poll worker threads
- ✅ **Reliable**: Automatic retry and timeout handling

Use long polling for efficient, low-latency message consumption! 🚀

