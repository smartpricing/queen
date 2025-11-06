# Push Operations in Queen MQ

## Overview

PUSH is the operation for sending messages to Queen MQ queues. The system provides multiple push modes optimized for different use cases, from high-reliability exactly-once delivery to high-throughput fire-and-forget scenarios.

## Basic PUSH Operation

### Simple Push

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

await queen
  .queue('orders')
  .partition('customer-123')
  .push([
    {
      data: { orderId: 'O-12345', amount: 99.99 }
    }
  ])
```

### HTTP API

```bash
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "items": [
      {
        "queue": "orders",
        "partition": "customer-123",
        "payload": {"orderId": "O-12345", "amount": 99.99}
      }
    ]
  }'
```

**Response:**
```json
{
  "pushed": true,
  "qos0": false,
  "dbHealthy": true,
  "messages": [
    {
      "transaction_id": "019a0c11-7fe8-7001-9999-0123456789ab",
      "status": "pushed",
      "message_id": "msg-uuid",
      "trace_id": "trace-uuid"
    }
  ]
}
```

## Push Flow

### Standard PUSH Flow

```
1. Client sends PUSH request
   POST /api/v1/push
   { items: [...] }
        ↓
2. Worker receives request
   Parse JSON body
        ↓
3. AsyncQueueManager.push_messages()
   Check database health
        ↓
4. Database healthy?
   YES → Continue to step 5
   NO  → Go to Failover (step 10)
        ↓
5. Acquire database connection
   auto conn = async_db_pool_->acquire()
        ↓
6. Check for duplicates
   SELECT transaction_id FROM messages
   WHERE transaction_id = ANY($1::text[])
        ↓
7. Filter out duplicates
   Keep only new transaction_ids
        ↓
8. Batch insert messages
   INSERT INTO messages (...)
   VALUES (...), (...), (...)
        ↓
9. Return results to client
   { pushed: true, messages: [...] }
        ↓
   DONE ✅

10. Failover: Write to file buffer
    file_buffer_manager->buffer_event(event)
        ↓
11. Return success to client
    { pushed: true, qos0: false, dbHealthy: false }
        ↓
12. Background processor replays to DB
    When database recovers
        ↓
    DONE ✅
```

## Message Structure

### Full Message Object

```javascript
{
  queue: 'orders',                    // Required: Queue name
  partition: 'customer-123',          // Optional: Default = 'Default'
  payload: { orderId: 'O-12345' },   // Required: Message data
  transactionId: 'custom-tx-id',     // Optional: Auto-generated if not provided
  traceId: 'trace-uuid',             // Optional: For distributed tracing
  namespace: 'billing',               // Optional: Logical grouping
  task: 'process-order'              // Optional: Task identifier
}
```

### Generated Fields

Server automatically adds:

```javascript
{
  id: 'msg-uuid',                     // Message UUID
  created_at: '2024-01-15T10:30:00Z', // Creation timestamp
  sequence: 12345,                    // Partition sequence number
  status: 'pending',                  // Message status
  retry_count: 0,                     // Retry counter
  partition_id: 'partition-uuid'      // Partition UUID
}
```

## QoS Levels

### QoS 1: At-Least-Once (Default)

Standard PUSH with acknowledgment:

```javascript
await queen.queue('orders').push([
  { data: { orderId: 'O-12345' } }
])
```

**Characteristics:**
- ✅ Message persisted to database before returning
- ✅ Guaranteed delivery
- ✅ May be delivered more than once (on retry)
- ✅ Client waits for confirmation
- 📊 Latency: 10-50ms
- 📊 Throughput: 1,000-10,000 msg/s

### QoS 2: Exactly-Once

Use transaction IDs for deduplication:

```javascript
await queen.queue('payments').push([
  {
    transactionId: 'payment-' + orderId,  // Unique ID
    data: { orderId, amount: 99.99 }
  }
])

// Duplicate pushes with same transactionId are rejected
```

**Characteristics:**
- ✅ Idempotent (safe to retry)
- ✅ Exactly-once delivery
- ✅ Prevents duplicate processing
- ✅ Transaction ID must be unique per (queue, partition)
- 📊 Latency: 15-60ms (includes duplicate check)
- 📊 Throughput: 500-5,000 msg/s

### QoS 0: At-Most-Once (Buffered)

Server-side batching for maximum throughput:

```javascript
await queen.queue('events').push([
  { data: { type: 'page_view' } }
], {
  bufferMs: 100,   // Flush after 100ms
  bufferMax: 100   // Or when 100 messages buffered
})
```

**HTTP API:**
```bash
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "items": [
      {"queue": "events", "payload": {"type": "login"}}
    ],
    "bufferMs": 100,
    "bufferMax": 100
  }'
```

**Characteristics:**
- ✅ Server batches messages
- ✅ 10-100x throughput improvement
- ⚠️ Potential message loss (if server crashes before flush)
- ✅ Best for high-volume, loss-tolerant scenarios
- 📊 Latency: 0-100ms (depending on buffer)
- 📊 Throughput: 100,000+ msg/s

**Response:**
```json
{
  "pushed": true,
  "qos0": true,
  "dbHealthy": true,
  "messages": [
    {
      "transaction_id": "buffered",
      "status": "buffered"
    }
  ]
}
```

## Batch Operations

### Batch Push

Push multiple messages in single request:

```javascript
await queen.queue('orders').push([
  { data: { orderId: 'O-1', amount: 10.00 } },
  { data: { orderId: 'O-2', amount: 20.00 } },
  { data: { orderId: 'O-3', amount: 30.00 } },
  // ... up to thousands
])
```

**Benefits:**
- Single HTTP request
- Single database transaction
- Batch duplicate detection
- Better throughput

**Batch Size Recommendations:**

| Scenario | Batch Size | Rationale |
|----------|------------|-----------|
| Real-time individual events | 1 | Immediate delivery |
| Moderate throughput | 10-100 | Good balance |
| High throughput | 100-1,000 | Maximize efficiency |
| Bulk import | 1,000-10,000 | Maximum performance |

### Dynamic Batching

Server automatically splits large batches:

```cpp
// Split by target size (4MB default)
const size_t TARGET_BATCH_SIZE_MB = 4;
const size_t MAX_BATCH_SIZE_MB = 8;

// Estimate batch size
size_t estimated_size = calculate_batch_size(items);

// Split if too large
if (estimated_size > TARGET_BATCH_SIZE_MB * 1024 * 1024) {
    // Process in smaller batches
    for (const auto& batch : split_into_batches(items)) {
        insert_batch(batch);
    }
}
```

**Benefits:**
- Prevents oversized transactions
- Optimizes PostgreSQL performance
- Automatic handling (transparent to client)

## Duplicate Detection

### How It Works

```sql
-- Check for existing transaction IDs
SELECT transaction_id 
FROM messages
WHERE transaction_id = ANY($1::text[])
  AND queue_name = $2
  AND partition_name = $3

-- Filter out duplicates
-- Only insert messages with new transaction_ids
```

**Example:**
```javascript
// First push
await queen.queue('orders').push([
  { transactionId: 'tx-1', data: { order: 1 } }
])
// Result: Inserted ✅

// Duplicate push (same transactionId)
await queen.queue('orders').push([
  { transactionId: 'tx-1', data: { order: 1 } }
])
// Result: Rejected as duplicate ❌
```

### Batch Duplicate Detection

Efficient array-based checking:

```javascript
await queen.queue('orders').push([
  { transactionId: 'tx-1', data: { order: 1 } },
  { transactionId: 'tx-2', data: { order: 2 } },
  { transactionId: 'tx-3', data: { order: 3 } }
])

// Single query checks all three transaction IDs
// Using PostgreSQL UNNEST and ANY operators
```

**Performance:**
- O(1) queries regardless of batch size
- Uses PostgreSQL indexes
- Efficient for large batches

### Duplicate Handling

```javascript
await queen
  .queue('orders')
  .push([
    { transactionId: 'tx-1', data: { order: 1 } }
  ])
  .onSuccess(async (messages) => {
    console.log('New messages:', messages)
  })
  .onDuplicate(async (messages) => {
    console.log('Duplicate messages:', messages)
  })
  .onError(async (messages, error) => {
    console.error('Failed messages:', error)
  })
```

## Advanced Features

### Delayed Processing

Schedule messages for future delivery:

```javascript
await queen.queue('scheduled-tasks')
  .config({
    delayedProcessing: 3600  // 1 hour delay
  })
  .create()

await queen.queue('scheduled-tasks').push([
  { data: { task: 'send-reminder' } }
])

// Message won't be available to consumers for 1 hour
```

**Use Cases:**
- Scheduled jobs
- Retry with backoff
- Rate limiting
- Time-based triggers

### Encryption

Encrypt sensitive message payloads:

```javascript
await queen.queue('sensitive-data')
  .config({
    encryptionEnabled: true
  })
  .create()

await queen.queue('sensitive-data').push([
  { data: { ssn: '123-45-6789', name: 'John Doe' } }
])

// Data encrypted at rest in PostgreSQL
```

**Features:**
- AES-256 encryption
- Transparent to consumers
- Per-queue configuration
- Key managed by server

### Tracing

Propagate trace IDs through workflows:

```javascript
// Initial event
const traceId = generateUUID()
await queen.queue('step-1')
  .push([{
    data: { input: 'data' },
    traceId: traceId
  }])

// Next step
await queen.queue('step-2')
  .push([{
    data: { processed: 'data' },
    traceId: traceId  // Same trace ID
  }])

// View trace in webapp
// See complete message journey
```

**Webapp Features:**
- Visual timeline
- Message relationships
- Latency analysis
- Error tracking

### Priority

Higher priority queues processed first:

```javascript
await queen.queue('critical-alerts')
  .config({ priority: 10 })
  .create()

await queen.queue('background-tasks')
  .config({ priority: 1 })
  .create()

// Critical alerts processed before background tasks
```

### Namespace & Task

Organize messages logically:

```javascript
await queen
  .queue('events')
  .config({
    namespace: 'billing',
    task: 'process-invoice'
  })
  .create()

await queen.queue('events').push([
  { data: { invoice: 'INV-123' } }
])

// Later, consume by namespace
await queen.pop({ namespace: 'billing' })
```

## Performance Optimization

### 1. Batch Messages

```javascript
// Bad: Individual pushes
for (const order of orders) {
  await queen.queue('orders').push([{ data: order }])
}
// 1,000 orders = 1,000 requests = ~10 seconds

// Good: Batch push
await queen.queue('orders').push(
  orders.map(order => ({ data: order }))
)
// 1,000 orders = 1 request = ~0.1 seconds
```

### 2. Use QoS 0 for High Volume

```javascript
// Good for: Metrics, logs, analytics
await queen.queue('page-views').push([
  { data: { page: '/home', user: 'user-123' } }
], {
  bufferMs: 100,
  bufferMax: 100
})

// 100x throughput improvement
```

### 3. Partition Strategically

```javascript
// Good: Partition by natural key
await queen.queue('orders')
  .partition('customer-' + customerId)
  .push([{ data: order }])

// Benefits:
// - Parallel processing
// - Ordered per customer
// - Load distribution
```

### 4. Reuse Transaction IDs

```javascript
// Generate deterministic transaction IDs
const txId = `order-${orderId}-${timestamp}`

await queen.queue('orders').push([
  { transactionId: txId, data: order }
])

// Safe to retry - duplicate detection prevents duplicates
```

### 5. Pre-create Queues

```javascript
// Bad: Create queue on every push
await queen.queue('events').config({...}).create()
await queen.queue('events').push([...])

// Good: Create once at startup
await queen.queue('events').config({...}).create()
// ... later ...
await queen.queue('events').push([...])
```

## Error Handling

### Common Errors

#### 1. Queue Not Found

```json
{
  "error": "Queue 'unknown-queue' does not exist"
}
```

**Solution:**
```javascript
// Create queue first
await queen.queue('my-queue').config({}).create()
// Then push
await queen.queue('my-queue').push([...])
```

#### 2. Duplicate Transaction ID

```json
{
  "transaction_id": "tx-123",
  "status": "duplicate"
}
```

**Solution:**
```javascript
// Either:
// 1. Use auto-generated transaction IDs (omit transactionId)
// 2. Ensure transaction IDs are unique
// 3. Handle duplicates in callbacks

await queen.queue('orders')
  .push([{ transactionId: 'tx-123', data: {...} }])
  .onDuplicate(async (messages) => {
    // Handle duplicate
    console.log('Already processed')
  })
```

#### 3. Database Unavailable

```json
{
  "pushed": true,
  "qos0": false,
  "dbHealthy": false,
  "messages": [...]
}
```

**Behavior:**
- Message buffered to file
- Client still receives success
- Automatic replay when database recovers

**No client action needed** - failover is automatic.

#### 4. Payload Too Large

```json
{
  "error": "Payload exceeds maximum size"
}
```

**Solution:**
```javascript
// Split large payloads
const chunks = splitIntoChunks(largeData, maxSize)
for (const chunk of chunks) {
  await queen.queue('data').push([{ data: chunk }])
}
```

### Retry Strategies

#### Exponential Backoff

```javascript
async function pushWithRetry(queue, messages, maxRetries = 3) {
  for (let attempt = 0; attempt < maxRetries; attempt++) {
    try {
      return await queen.queue(queue).push(messages)
    } catch (error) {
      if (attempt === maxRetries - 1) throw error
      
      const delay = Math.pow(2, attempt) * 1000  // 1s, 2s, 4s
      await sleep(delay)
    }
  }
}
```

#### Circuit Breaker

```javascript
class CircuitBreaker {
  constructor(threshold = 5, timeout = 60000) {
    this.failures = 0
    this.threshold = threshold
    this.timeout = timeout
    this.isOpen = false
  }
  
  async push(queue, messages) {
    if (this.isOpen) {
      throw new Error('Circuit breaker is open')
    }
    
    try {
      const result = await queen.queue(queue).push(messages)
      this.failures = 0
      return result
    } catch (error) {
      this.failures++
      
      if (this.failures >= this.threshold) {
        this.isOpen = true
        setTimeout(() => {
          this.isOpen = false
          this.failures = 0
        }, this.timeout)
      }
      
      throw error
    }
  }
}
```

## Monitoring

### Push Metrics

```javascript
const pushMetrics = {
  total: 0,
  successful: 0,
  duplicate: 0,
  failed: 0,
  latencies: [],
  
  async push(queue, messages) {
    this.total++
    const start = Date.now()
    
    try {
      const result = await queen.queue(queue).push(messages)
      
      // Check for duplicates
      const duplicates = result.messages.filter(m => m.status === 'duplicate')
      if (duplicates.length > 0) {
        this.duplicate += duplicates.length
      }
      
      this.successful++
      this.latencies.push(Date.now() - start)
      
      return result
    } catch (error) {
      this.failed++
      throw error
    }
  },
  
  getStats() {
    return {
      total: this.total,
      successful: this.successful,
      duplicate: this.duplicate,
      failed: this.failed,
      successRate: (this.successful / this.total * 100).toFixed(2) + '%',
      avgLatency: (this.latencies.reduce((a, b) => a + b, 0) / this.latencies.length).toFixed(2) + 'ms'
    }
  }
}
```

### Server Metrics

```bash
curl http://localhost:6632/metrics
```

```json
{
  "push": {
    "total": 1523456,
    "throughput": 5234,
    "avg_latency_ms": 23,
    "p95_latency_ms": 45,
    "p99_latency_ms": 78
  },
  "database": {
    "healthy": true,
    "connections_active": 42,
    "connections_idle": 108
  },
  "file_buffer": {
    "pending": 0,
    "flushed": 1523456,
    "failed": 0
  }
}
```

## Best Practices

1. ✅ **Batch messages** when possible (10-1000 per request)
2. ✅ **Use transaction IDs** for exactly-once semantics
3. ✅ **Partition strategically** for parallelism
4. ✅ **Use QoS 0** for high-volume, loss-tolerant data
5. ✅ **Handle duplicates** gracefully in callbacks
6. ✅ **Monitor push metrics** for performance insights
7. ✅ **Implement retry logic** with exponential backoff
8. ✅ **Pre-create queues** at startup
9. ✅ **Use tracing** for debugging workflows
10. ✅ **Provision disk space** for file buffer failover

## Comparison Table

| Feature | QoS 0 (Buffered) | QoS 1 (At-Least-Once) | QoS 2 (Exactly-Once) |
|---------|------------------|------------------------|----------------------|
| Latency | 0-100ms | 10-50ms | 15-60ms |
| Throughput | 100K+ msg/s | 1K-10K msg/s | 500-5K msg/s |
| Reliability | At-most-once | At-least-once | Exactly-once |
| Duplicates | Possible | Possible | No duplicates |
| Use Case | Metrics, logs | Most applications | Payments, critical ops |

## Related Documentation

- [Architecture](ARCHITECTURE.md) - System architecture overview
- [Failover](FAILOVER.md) - How failover works during PUSH
- [Transactions](TRANSACTIONS.md) - PUSH within transactions
- [ACK System](ACK.md) - Consuming messages after PUSH
- [Server Features](SERVER_FEATURES.md) - Complete feature list
- [API Reference](../server/API.md) - HTTP API details

## Summary

Queen MQ PUSH operations provide:

- ✅ **Multiple QoS Levels**: From fire-and-forget to exactly-once
- ✅ **High Performance**: 100K+ msg/s with batching and QoS 0
- ✅ **Reliability**: Automatic failover, duplicate detection
- ✅ **Flexibility**: Batch operations, encryption, tracing, priority
- ✅ **Developer Friendly**: Fluent API, callbacks, comprehensive error handling

Choose the right QoS level and batching strategy for your use case to maximize both reliability and performance.

