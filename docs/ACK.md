# Message Acknowledgment in Queen MQ

## Overview

Acknowledgment (ACK) is the mechanism by which consumers confirm that they have successfully processed a message. Queen MQ provides a robust acknowledgment system that ensures reliable message processing, supports retry logic, dead letter queues, and integrates seamlessly with consumer groups and transactions.

## Core Concepts

### What is ACK/NACK?

**ACK (Acknowledge)**: Tells the server that a message was successfully processed
- Message marked as completed
- Consumer cursor advanced
- Lease released
- Message retained based on retention policy

**NACK (Negative Acknowledge)**: Tells the server that message processing failed
- Message marked as failed
- Retry counter incremented
- Message becomes available for retry (if under retry limit)
- Moved to Dead Letter Queue (DLQ) if retry limit exceeded

### Why ACK is Important

```
Without ACK:
  Consumer crashes after getting message
  ↓
  Message lost forever ❌

With ACK:
  Consumer gets message (lease acquired)
  ↓
  Consumer crashes before ACK
  ↓
  Lease expires after 5 minutes
  ↓
  Message becomes available again ✅
  ↓
  Another consumer processes it
```

## ACK Models

### 1. Manual ACK (Default)

Consumer explicitly acknowledges each message:

```javascript
await queen
  .queue('orders')
  .group('processors')
  .autoAck(false)  // Manual ACK
  .consume(async (message) => {
    try {
      await processOrder(message.data)
      await queen.ack(message)  // Explicit ACK
    } catch (error) {
      await queen.nack(message, error.message)  // Explicit NACK
    }
  })
```

**Pros:**
- Full control over when messages are acknowledged
- Can ACK/NACK based on processing outcome
- Best for critical operations

**Cons:**
- More code
- Must remember to ACK

### 2. Auto-ACK

Messages automatically acknowledged upon delivery:

```javascript
await queen
  .queue('events')
  .group('processors')
  .autoAck(true)  // Auto ACK
  .consume(async (message) => {
    // Message already ACKed when this function is called
    await processEvent(message.data)
  })
```

**HTTP API:**
```bash
curl "http://localhost:6632/api/v1/pop/queue/events?autoAck=true&batch=10"
```

**Pros:**
- Simpler code
- Fire-and-forget semantics
- Good for non-critical workloads

**Cons:**
- No retry on failure
- Message lost if consumer crashes
- No DLQ support

### 3. Callback-based ACK

Fluent API with success/error callbacks:

```javascript
await queen
  .queue('tasks')
  .group('workers')
  .autoAck(false)
  .each()  // Process messages individually
  .consume(async (message) => {
    return await processTask(message.data)
  })
  .onSuccess(async (message) => {
    // Auto-ACK on success
    await queen.ack(message)
  })
  .onError(async (message, error) => {
    // Auto-NACK on error
    await queen.nack(message, error.message)
  })
```

**Pros:**
- Clean separation of processing and acknowledgment
- Automatic error handling
- Best of both worlds

## ACK Implementation Details

### Required Parameters

**CRITICAL**: `partitionId` is **mandatory** for all ACK operations to prevent acknowledging the wrong message when `transactionId` values are not unique across partitions.

```javascript
await queen.ack({
  transactionId: 'msg-tx-id',   // Message transaction ID
  partitionId: 'partition-uuid', // REQUIRED: Ensures correct message
  leaseId: 'lease-uuid',         // Optional: For lease validation
  consumerGroup: 'workers'       // Consumer group (if applicable)
})
```

### Why partitionId is Required

```
Scenario without partitionId:
  Partition A: Message with transactionId="tx-123"
  Partition B: Message with transactionId="tx-123"  (different message!)
  
  Consumer ACKs transactionId="tx-123"
  ↓
  Which message should be ACKed? ❌ AMBIGUOUS

Scenario with partitionId:
  Consumer ACKs transactionId="tx-123", partitionId="partition-A-uuid"
  ↓
  Exactly one message identified ✅ UNAMBIGUOUS
```

### ACK Flow

```
1. Consumer receives message with lease
   {
     messageId: "msg-uuid",
     transactionId: "tx-123",
     partitionId: "partition-uuid",
     leaseId: "lease-uuid",
     data: {...}
   }
        ↓
2. Consumer processes message
   try {
     await processMessage(message.data)
   }
        ↓
3. Consumer sends ACK
   POST /api/v1/ack
   {
     transactionId: "tx-123",
     partitionId: "partition-uuid",  // REQUIRED
     leaseId: "lease-uuid",
     status: "completed"
   }
        ↓
4. Server validates lease
   SELECT 1 FROM partition_consumers
   WHERE partition_id = $partition_id
     AND worker_id = $lease_id
     AND lease_expires_at > NOW()
        ↓
5. Server updates consumer cursor
   UPDATE partition_consumers
   SET last_consumed_id = message.id,
       last_consumed_created_at = message.created_at,
       last_consumed_at = NOW(),
       total_messages_consumed = total_messages_consumed + 1,
       acked_count = acked_count + 1
        ↓
6. Server checks if batch complete
   IF acked_count >= batch_size THEN
     Release lease (set to NULL)
     Reset batch_size and acked_count
   END IF
        ↓
7. Response sent to consumer
   {
     success: true,
     message: "acknowledged"
   }
```

### NACK Flow

```
1. Consumer processes message and fails
   try {
     await processMessage(message.data)
   } catch (error) {
     await queen.nack(message, error.message)
   }
        ↓
2. Consumer sends NACK
   POST /api/v1/ack
   {
     transactionId: "tx-123",
     partitionId: "partition-uuid",
     leaseId: "lease-uuid",
     status: "failed",
     error: "Processing failed: invalid data"
   }
        ↓
3. Server increments retry counter
   UPDATE messages
   SET retry_count = retry_count + 1,
       status = 'failed',
       error_message = $error
        ↓
4. Server checks retry limit
   IF retry_count >= queue.retry_limit THEN
     Move to Dead Letter Queue
     INSERT INTO dead_letter_queue (...)
   ELSE
     Make available for retry
     status = 'pending'
   END IF
        ↓
5. Server updates consumer cursor
   (same as ACK flow)
        ↓
6. Response sent to consumer
   {
     success: true,
     message: "failed_retry"  // or "failed_dlq"
   }
```

## Batch ACK

Acknowledge multiple messages at once for better performance:

```javascript
await queen.ackBatch([
  {
    transactionId: 'tx-1',
    partitionId: 'partition-uuid-1',
    status: 'completed'
  },
  {
    transactionId: 'tx-2',
    partitionId: 'partition-uuid-2',
    status: 'completed'
  },
  {
    transactionId: 'tx-3',
    partitionId: 'partition-uuid-3',
    status: 'failed',
    error: 'Validation error'
  }
])
```

**HTTP API:**
```bash
curl -X POST http://localhost:6632/api/v1/ack/batch \
  -H "Content-Type: application/json" \
  -d '{
    "consumerGroup": "workers",
    "acknowledgments": [
      {
        "transactionId": "tx-1",
        "partitionId": "partition-uuid-1",
        "status": "completed"
      },
      {
        "transactionId": "tx-2",
        "partitionId": "partition-uuid-2",
        "status": "failed",
        "error": "Validation error"
      }
    ]
  }'
```

**Benefits:**
- Single database transaction for all ACKs
- Better throughput (20-80ms for batch vs 10-50ms per single ACK)
- Atomic: all succeed or all fail
- Reduced network overhead

**Implementation:**
```cpp
BatchAckResult acknowledge_messages_batch(
    const std::vector<json>& acknowledgments
) {
    auto conn = async_db_pool_->acquire();
    
    // BEGIN transaction
    sendAndWait(conn.get(), "BEGIN");
    
    // Group by status
    std::vector<json> completed;
    std::vector<json> failed;
    
    for (const auto& ack : acknowledgments) {
        if (ack.value("status", "") == "completed") {
            completed.push_back(ack);
        } else {
            failed.push_back(ack);
        }
    }
    
    // Process completed ACKs using UNNEST arrays
    if (!completed.empty()) {
        std::vector<std::string> txn_ids, partition_ids;
        for (const auto& ack : completed) {
            txn_ids.push_back(ack.value("transactionId", ""));
            partition_ids.push_back(ack.value("partitionId", ""));
        }
        
        // Batch update using PostgreSQL arrays
        std::string sql = R"(
            UPDATE partition_consumers pc
            SET last_consumed_id = m.id,
                last_consumed_created_at = m.created_at,
                total_messages_consumed = total_messages_consumed + 1
            FROM messages m, 
                 UNNEST($1::text[], $2::uuid[]) AS inputs(txn_id, part_id)
            WHERE pc.partition_id = inputs.part_id
              AND m.transaction_id = inputs.txn_id
              AND m.partition_id = inputs.part_id
        )";
        
        sendQueryParamsAsync(conn.get(), sql, {
            build_pg_array(txn_ids),
            build_pg_array(partition_ids)
        });
    }
    
    // Process failed ACKs similarly
    // ...
    
    // COMMIT transaction
    sendAndWait(conn.get(), "COMMIT");
}
```

## Lease Management

### Lease Lifecycle

```
1. Consumer POPs messages
   ↓
   Lease acquired (expires in 5 minutes)
   ↓
2. Consumer processes messages
   ↓
   Optional: Renew lease if taking longer
   ↓
3. Consumer ACKs all messages in batch
   ↓
   Lease released automatically
   ↓
4. Partition available for next consumer
```

### Lease Validation

Every ACK operation validates the lease:

```sql
SELECT 1 FROM partition_consumers pc
WHERE pc.partition_id = $partition_id
  AND pc.worker_id = $lease_id
  AND pc.lease_expires_at > NOW()
```

**If lease invalid:**
```json
{
  "success": false,
  "message": "invalid_lease",
  "error": "Lease validation failed"
}
```

**Causes of invalid lease:**
- Lease expired (took too long to process)
- Lease already released (batch already ACKed)
- Wrong lease ID (programming error)
- Partition reassigned to another consumer

### Lease Release

Lease is released when all messages in batch are ACKed:

```sql
UPDATE partition_consumers
SET lease_expires_at = NULL,
    lease_acquired_at = NULL,
    worker_id = NULL,
    batch_size = 0,
    acked_count = 0
WHERE partition_id = $partition_id
  AND consumer_group = $consumer_group
  AND acked_count + 1 >= batch_size
```

**Example:**
```
Batch size: 10 messages
ACK message 1: acked_count = 1, lease still active
ACK message 2: acked_count = 2, lease still active
...
ACK message 10: acked_count = 10, lease RELEASED
```

### Lease Renewal

For long-running operations:

```javascript
await queen
  .queue('long-tasks')
  .group('workers')
  .renewLease(true, 2000)  // Auto-renew every 2 seconds
  .consume(async (message) => {
    // Long operation (e.g., 10 minutes)
    await processLongTask(message.data)
    // Lease automatically renewed during processing
  })
```

**Manual renewal:**
```bash
curl -X POST http://localhost:6632/api/v1/lease/LEASE_ID/extend \
  -H "Content-Type: application/json" \
  -d '{"seconds": 60}'
```

## Consumer Groups

### Queue Mode (Default)

Default consumer group: `__QUEUE_MODE__`

```javascript
await queen
  .queue('tasks')
  // No .group() call = __QUEUE_MODE__
  .consume(async (message) => {
    await processTask(message.data)
    await queen.ack(message)
  })
```

**Characteristics:**
- Competing consumers (like RabbitMQ)
- Each message consumed exactly once
- Round-robin distribution across partitions

### Named Consumer Groups

Multiple groups can consume same messages:

```javascript
// Group 1: Send emails
await queen
  .queue('user-events')
  .group('email-sender')
  .consume(async (message) => {
    await sendEmail(message.data)
    await queen.ack(message)
  })

// Group 2: Update analytics
await queen
  .queue('user-events')
  .group('analytics')
  .consume(async (message) => {
    await updateAnalytics(message.data)
    await queen.ack(message)
  })
```

**Characteristics:**
- Independent progress tracking per group
- Same messages delivered to all groups
- Each group ACKs independently

### Consumer Progress Tracking

Each consumer group maintains its own cursor:

```sql
CREATE TABLE partition_consumers (
    partition_id UUID NOT NULL,
    consumer_group VARCHAR NOT NULL,
    last_consumed_id UUID,                -- Last ACKed message
    last_consumed_created_at TIMESTAMP,   -- Timestamp of last ACKed message
    last_consumed_at TIMESTAMP,           -- When last ACKed
    total_messages_consumed BIGINT,       -- Total ACKs
    -- ... lease fields ...
    PRIMARY KEY (partition_id, consumer_group)
);
```

**Example:**
```
Queue: user-events
Partition: Default

Consumer Group: email-sender
  last_consumed_id: msg-100
  total_messages_consumed: 5432

Consumer Group: analytics
  last_consumed_id: msg-98
  total_messages_consumed: 5430

Next POP for email-sender: starts from msg-101
Next POP for analytics: starts from msg-99
```

## Dead Letter Queue (DLQ)

### When Messages Move to DLQ

1. **Max retries exceeded**
```javascript
await queen.queue('orders').config({
  retryLimit: 3,
  dlqAfterMaxRetries: true
}).create()

// Message fails 3 times → moved to DLQ
```

2. **Max wait time exceeded**
```javascript
await queen.queue('time-sensitive').config({
  maxWaitTimeSeconds: 3600,  // 1 hour
  deadLetterQueue: true
}).create()

// Message pending for 1 hour → moved to DLQ
```

### DLQ Structure

```sql
CREATE TABLE dead_letter_queue (
    id UUID PRIMARY KEY,
    message_id UUID NOT NULL,
    partition_id UUID NOT NULL,
    transaction_id VARCHAR NOT NULL,
    consumer_group VARCHAR NOT NULL,
    error_message TEXT,
    retry_count INT DEFAULT 0,
    original_created_at TIMESTAMP,
    moved_to_dlq_at TIMESTAMP DEFAULT NOW()
);
```

### Querying DLQ

```bash
# Get all DLQ messages for a queue
curl "http://localhost:6632/api/v1/dlq?queue=orders&limit=50"

# Filter by consumer group
curl "http://localhost:6632/api/v1/dlq?queue=orders&consumerGroup=workers"

# Date range
curl "http://localhost:6632/api/v1/dlq?queue=orders&from=2024-01-01&to=2024-01-31"
```

**Response:**
```json
{
  "messages": [
    {
      "id": "dlq-uuid",
      "messageId": "msg-uuid",
      "partitionId": "partition-uuid",
      "transactionId": "tx-123",
      "consumerGroup": "workers",
      "errorMessage": "Processing failed: invalid data",
      "retryCount": 3,
      "originalCreatedAt": "2024-01-15T10:30:00Z",
      "movedToDlqAt": "2024-01-15T10:35:00Z",
      "queueName": "orders",
      "data": {"orderId": "12345"}
    }
  ],
  "total": 42,
  "limit": 50,
  "offset": 0
}
```

### DLQ Best Practices

1. **Monitor DLQ regularly**
```javascript
// Alert when DLQ depth exceeds threshold
const dlqMessages = await queen.getDLQ('orders')
if (dlqMessages.total > 100) {
  await sendAlert('High DLQ depth for orders queue')
}
```

2. **Investigate and fix root causes**
```javascript
// Analyze DLQ messages for patterns
const dlqMessages = await queen.getDLQ('orders', { limit: 1000 })
const errorTypes = {}
for (const msg of dlqMessages.messages) {
  const errorType = msg.errorMessage.split(':')[0]
  errorTypes[errorType] = (errorTypes[errorType] || 0) + 1
}
console.log('DLQ error distribution:', errorTypes)
```

3. **Reprocess when appropriate**
```javascript
// After fixing the issue, reprocess DLQ messages
const dlqMessages = await queen.getDLQ('orders')
for (const dlqMsg of dlqMessages.messages) {
  await queen.push('orders', dlqMsg.partition, {
    data: dlqMsg.data,
    transactionId: 'reprocess-' + dlqMsg.transactionId
  })
}
```

## Performance Characteristics

### Latency

| Operation | Latency | Notes |
|-----------|---------|-------|
| Single ACK | 10-50ms | Direct query |
| Batch ACK (10 messages) | 20-80ms | Single transaction |
| Batch ACK (100 messages) | 50-150ms | UNNEST array optimization |
| ACK with lease validation | +5-10ms | Additional SELECT query |

### Throughput

| Metric | Value | Configuration |
|--------|-------|---------------|
| Single ACKs/s | 1,000-2,000 | Individual transactions |
| Batch ACKs/s | 10,000-20,000 | Batches of 10 |
| Concurrent ACKs | Unlimited | Limited by connection pool |

### Best Practices for Performance

1. **Use batch ACK for high throughput**
```javascript
// Collect ACKs and send in batches
const acksBuffer = []
await queen.queue('events')
  .batch(100)
  .consume(async (messages) => {
    for (const msg of messages) {
      await processMessage(msg)
      acksBuffer.push({
        transactionId: msg.transactionId,
        partitionId: msg.partitionId,
        status: 'completed'
      })
    }
    
    // Batch ACK every 100 messages
    if (acksBuffer.length >= 100) {
      await queen.ackBatch(acksBuffer)
      acksBuffer.length = 0
    }
  })
```

2. **Include lease ID to skip validation query**
```javascript
await queen.ack({
  transactionId: msg.transactionId,
  partitionId: msg.partitionId,
  leaseId: msg.leaseId  // Saves 5-10ms per ACK
})
```

3. **Use auto-ACK for non-critical messages**
```javascript
await queen.queue('logs')
  .autoAck(true)  // No ACK overhead
  .consume(async (message) => {
    await logEvent(message.data)
  })
```

## Error Handling

### Common ACK Errors

#### 1. Invalid Lease
```json
{
  "success": false,
  "message": "invalid_lease",
  "error": "Lease validation failed"
}
```

**Causes:**
- Lease expired (processing took too long)
- Message already ACKed by another consumer
- Partition reassigned

**Solution:**
```javascript
try {
  await queen.ack(message)
} catch (error) {
  if (error.message.includes('invalid_lease')) {
    // Don't retry - message will be redelivered to another consumer
    console.log('Lease expired, message will be reprocessed')
  }
}
```

#### 2. Missing partitionId
```json
{
  "error": "partitionId is required to ensure message uniqueness"
}
```

**Solution:**
```javascript
// Always include partitionId
await queen.ack({
  transactionId: msg.transactionId,
  partitionId: msg.partitionId  // REQUIRED
})
```

#### 3. Message Not Found
```json
{
  "success": false,
  "message": "not_found"
}
```

**Causes:**
- Message already ACKed
- Wrong transaction ID
- Message deleted by retention policy

**Solution:**
```javascript
// Idempotent ACK - safe to call multiple times
try {
  await queen.ack(message)
} catch (error) {
  if (error.message.includes('not_found')) {
    // Already ACKed, safe to ignore
    console.log('Message already acknowledged')
  }
}
```

### Retry Strategies

#### Exponential Backoff
```javascript
async function ackWithRetry(message, maxRetries = 3) {
  for (let attempt = 0; attempt < maxRetries; attempt++) {
    try {
      await queen.ack(message)
      return
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
const circuitBreaker = {
  failures: 0,
  threshold: 5,
  isOpen: false,
  
  async ack(message) {
    if (this.isOpen) {
      throw new Error('Circuit breaker is open')
    }
    
    try {
      await queen.ack(message)
      this.failures = 0
    } catch (error) {
      this.failures++
      if (this.failures >= this.threshold) {
        this.isOpen = true
        setTimeout(() => {
          this.isOpen = false
          this.failures = 0
        }, 60000)  // Reset after 1 minute
      }
      throw error
    }
  }
}
```

## Advanced Patterns

### Transactional ACK + PUSH

Process one message and forward to next queue atomically:

```javascript
await queen.transaction()
  .ack(inputMessage)  // ACK from input queue
  .queue('next-step')
  .partition('workflow-1')
  .push([{ data: { processed: true } }])  // PUSH to next queue
  .commit()  // Atomic: both succeed or both fail
```

See [TRANSACTIONS.md](TRANSACTIONS.md) for details.

### Conditional ACK

ACK based on processing outcome:

```javascript
await queen.queue('tasks')
  .consume(async (message) => {
    const result = await processTask(message.data)
    
    if (result.success) {
      await queen.ack(message)
    } else if (result.retryable) {
      await queen.nack(message, result.error)
    } else {
      // Move directly to DLQ (custom logic)
      await moveToDLQ(message, result.error)
      await queen.ack(message)  // ACK to prevent redelivery
    }
  })
```

### Delayed NACK (Manual Retry)

```javascript
await queen.queue('rate-limited')
  .consume(async (message) => {
    try {
      await callRateLimitedAPI(message.data)
      await queen.ack(message)
    } catch (error) {
      if (error.code === 'RATE_LIMIT') {
        // Don't NACK - push back with delay
        await queen.push('rate-limited', message.partition, {
          data: message.data,
          transactionId: message.transactionId + '-retry',
          delaySeconds: 60  // Retry in 1 minute
        })
        await queen.ack(message)  // ACK original
      } else {
        await queen.nack(message, error.message)
      }
    }
  })
```

## Monitoring & Observability

### ACK Metrics

Track ACK performance and errors:

```javascript
const ackMetrics = {
  total: 0,
  successful: 0,
  failed: 0,
  latencies: [],
  
  async ack(message) {
    this.total++
    const start = Date.now()
    
    try {
      await queen.ack(message)
      this.successful++
      this.latencies.push(Date.now() - start)
    } catch (error) {
      this.failed++
      throw error
    }
  },
  
  getStats() {
    return {
      total: this.total,
      successful: this.successful,
      failed: this.failed,
      successRate: (this.successful / this.total * 100).toFixed(2) + '%',
      avgLatency: (this.latencies.reduce((a, b) => a + b, 0) / this.latencies.length).toFixed(2) + 'ms'
    }
  }
}
```

### Consumer Group Lag

Monitor how far behind each consumer group is:

```bash
curl "http://localhost:6632/api/v1/consumer-groups"
```

```json
{
  "groups": [
    {
      "name": "workers",
      "topics": ["orders"],
      "members": 5,
      "totalLag": 1234,      // Messages behind
      "maxTimeLag": 120,     // Seconds behind
      "state": "Stable"
    }
  ]
}
```

## Summary

Queen MQ's acknowledgment system provides:

- ✅ **Reliability**: Lease-based processing ensures no message loss
- ✅ **Flexibility**: Manual ACK, auto-ACK, and callback-based patterns
- ✅ **Performance**: Batch ACK for high throughput (10K+ ACKs/s)
- ✅ **Safety**: Mandatory partitionId prevents wrong-message ACKs
- ✅ **DLQ Support**: Automatic failure handling with retry limits
- ✅ **Consumer Groups**: Independent progress tracking per group
- ✅ **Transactions**: Atomic ACK+PUSH operations
- ✅ **Monitoring**: Comprehensive metrics and lag tracking

## Related Documentation

- [Architecture](ARCHITECTURE.md) - System architecture overview
- [Transactions](TRANSACTIONS.md) - Transactional ACK+PUSH patterns
- [Long Polling](LONG_POLLING.md) - How messages are delivered
- [Push Operations](PUSH.md) - How messages are sent
- [Server Features](SERVER_FEATURES.md) - Complete feature list
- [API Reference](../server/API.md) - HTTP API details

