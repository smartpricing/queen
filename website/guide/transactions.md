# Transactions

Transactions in Queen MQ provide atomic execution of multiple operations, ensuring all-or-nothing semantics. This is essential for building reliable, exactly-once message processing systems.

## What are Transactions?

A transaction combines multiple queue operations (PUSH and ACK) into a single atomic unit:

- **All operations succeed together** → Transaction commits ✅
- **Any operation fails** → Transaction rolls back, no changes made ❌

```javascript
// Atomic: both ACK and PUSH succeed, or neither happens
await queen
  .transaction()
  .ack(inputMessage)
  .queue('next-queue')
  .push([{ data: { processed: true } }])
  .commit()
```

## Why Transactions Matter

### Without Transactions (The Problem)

```javascript
// Process message
const msg = await queen.queue('input').pop()
await processMessage(msg.data)

// Push result
await queen.queue('output').push([{ data: result }])

// ACK original
await queen.ack(msg)

// ❌ Problem: If ACK fails, message redelivered
//            but already pushed to output
//            Result: DUPLICATE PROCESSING
```

### With Transactions (The Solution)

```javascript
await queen
  .transaction()
  .ack(inputMessage)
  .queue('output')
  .push([{ data: result }])
  .commit()

// ✅ If ACK fails, PUSH is rolled back
// ✅ If PUSH fails, ACK is rolled back
// ✅ Result: EXACTLY-ONCE PROCESSING
```

## Transaction Types

### 1. ACK + PUSH (Pipeline Pattern)

Most common: process and forward to next stage.

```javascript
await queen
  .transaction()
  .ack(inputMessage)
  .queue('next-step')
  .partition('workflow-1')
  .push([{
    data: { step: 2, result: 'processed' }
  }])
  .commit()
```

**Use Cases:**
- Multi-stage workflows
- Event-driven architectures
- State machine transitions
- Data pipelines

### 2. Multiple PUSHes (Fan-out Pattern)

Process one message, produce multiple outputs.

```javascript
await queen
  .transaction()
  .ack(inputMessage)
  
  // Send email
  .queue('email-queue')
  .push([{ to: 'user@example.com', subject: 'Order confirmed' }])
  
  // Update analytics
  .queue('analytics-queue')
  .push([{ event: 'order', orderId: 123 }])
  
  // Update inventory
  .queue('inventory-queue')
  .push([{ action: 'decrement', sku: 'ABC123' }])
  
  .commit()
```

**Use Cases:**
- Event broadcasting
- Saga pattern
- Notification fan-out
- Data replication

### 3. Multiple ACKs + PUSH (Join Pattern)

Acknowledge multiple messages, produce combined output.

```javascript
await queen
  .transaction()
  .ack(message1)
  .ack(message2)
  .queue('combined-results')
  .push([{
    data: {
      result1: message1.data,
      result2: message2.data
    }
  }])
  .commit()
```

**Use Cases:**
- Message aggregation
- Join operations
- Batch processing

## Basic Usage

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

// Consume from input queue
await queen
  .queue('input-queue')
  .consume(async (message) => {
    // Process message
    const result = await processData(message.data)
    
    // Atomically ACK and push result
    await queen
      .transaction()
      .ack(message)
      .queue('output-queue')
      .partition('workflow-1')
      .push([{ data: result }])
      .commit()
  })
```

## Advanced Patterns

### Pattern: Conditional Operations

```javascript
const result = await processMessage(inputMessage.data)

const txn = queen.transaction().ack(inputMessage)

if (result.success) {
  txn.queue('success-queue').push([{ data: result }])
} else {
  txn.queue('failure-queue').push([{ data: result, error: result.error }])
}

await txn.commit()
```

### Pattern: State Machine

```javascript
await queen
  .queue('workflow-states')
  .consume(async (state) => {
    const nextState = await transitionState(state.data)
    
    await queen
      .transaction()
      .ack(state)
      
      // Push to next state queue
      .queue(`workflow-${nextState.name}`)
      .partition(state.data.workflowId)
      .push([{
        workflowId: state.data.workflowId,
        state: nextState.name,
        data: nextState.data
      }])
      
      // Record state change
      .queue('audit-log')
      .push([{
        workflowId: state.data.workflowId,
        from: state.data.state,
        to: nextState.name,
        timestamp: new Date()
      }])
      
      .commit()
  })
```

### Pattern: Exactly-Once Processing

Combine transactions with deterministic IDs:

```javascript
await queen
  .transaction()
  .ack(inputMessage)
  .queue('output')
  .push([{
    // Deterministic transaction ID
    transactionId: `output-${inputMessage.transactionId}`,
    data: { result: 'processed' }
  }])
  .commit()

// If transaction retries:
// - ACK succeeds (idempotent)
// - PUSH rejected (duplicate transactionId)
// - Overall transaction succeeds
// - Result: exactly-once delivery
```

### Pattern: Retry with Backoff

```javascript
await queen
  .queue('api-calls')
  .consume(async (message) => {
    const retryCount = message.data.retryCount || 0
    
    try {
      await callExternalAPI(message.data.request)
      await queen.ack(message)
      
    } catch (error) {
      if (retryCount < 3) {
        // Retry with exponential backoff
        const delaySeconds = Math.pow(2, retryCount) * 60
        
        await queen
          .transaction()
          .ack(message)
          .queue('api-calls')
          .partition('retry')
          .push([{
            ...message.data,
            retryCount: retryCount + 1,
            scheduledFor: new Date(Date.now() + delaySeconds * 1000)
          }])
          .commit()
      } else {
        await queen.ack(message, false)  // Send to DLQ
      }
    }
  })
```

### Pattern: Saga Orchestration

```javascript
async function executeSaga(sagaData) {
  const steps = [
    { queue: 'reserve-inventory', compensate: 'release-inventory' },
    { queue: 'charge-payment', compensate: 'refund-payment' },
    { queue: 'ship-order', compensate: 'cancel-shipment' }
  ]
  
  const completedSteps = []
  
  try {
    for (const step of steps) {
      const result = await queen
        .transaction()
        .queue(step.queue)
        .push([{ sagaId: sagaData.id, data: sagaData }])
        .commit()
      
      completedSteps.push(step)
      await waitForStepCompletion(result.transaction_id)
    }
    
    return { success: true }
    
  } catch (error) {
    // Compensate in reverse order
    for (const step of completedSteps.reverse()) {
      await queen
        .transaction()
        .queue(step.compensate)
        .push([{ sagaId: sagaData.id, data: sagaData }])
        .commit()
    }
    
    return { success: false, error }
  }
}
```

## HTTP API

You can also use transactions via the HTTP API:

```bash
curl -X POST http://localhost:6632/api/v1/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "operations": [
      {
        "type": "ack",
        "transactionId": "input-msg-tx-id",
        "partitionId": "partition-uuid",
        "status": "completed"
      },
      {
        "type": "push",
        "items": [{
          "queue": "output-queue",
          "partition": "workflow-1",
          "payload": {"result": "processed"}
        }]
      }
    ]
  }'
```

## Performance Characteristics

### Latency

| Transaction Type | Latency | Notes |
|------------------|---------|-------|
| ACK only | 10-50ms | Same as single ACK |
| ACK + 1 PUSH | 50-100ms | Two operations |
| ACK + 3 PUSHes | 100-200ms | Multiple operations |
| Complex (10+ ops) | 200-500ms | Many operations |

### Throughput

- **Transactions/s**: 500-1,000 (simple ACK+PUSH)
- **Transactions/s**: 200-500 (complex, 5+ ops)
- **Operations/s**: 5,000-10,000 (total across all transactions)

## Best Practices

### 1. Keep Transactions Small

```javascript
// ✅ Good: 2-5 operations
await queen.transaction()
  .ack(msg)
  .queue('next').push([...])
  .commit()

// ❌ Avoid: 50+ operations
// Split into multiple transactions instead
```

### 2. Use Batch Operations

```javascript
// ✅ Good: Batch PUSH
await queen.transaction()
  .ack(msg)
  .queue('output').push([
    { data: item1 },
    { data: item2 },
    { data: item3 }
  ])
  .commit()
```

### 3. Handle Errors Appropriately

```javascript
try {
  await queen.transaction()
    .ack(msg)
    .queue('next').push([...])
    .commit()
} catch (error) {
  // Transaction rolled back - message will be redelivered
  console.error('Transaction failed:', error)
  // Don't manually ACK or NACK
}
```

### 4. Make Transactions Idempotent

```javascript
// Use deterministic IDs
const outputTxId = `output-${inputMessage.transactionId}`

await queen.transaction()
  .ack(inputMessage)
  .queue('output')
  .push([{
    transactionId: outputTxId,  // Deterministic!
    data: { processed: true }
  }])
  .commit()
```

## Error Handling

### Transaction Rollback

```javascript
try {
  await queen.transaction()
    .ack(msg1)
    .queue('q1').push([{ data: 'valid' }])
    .queue('q2').push([{ data: 'invalid' }])  // Fails!
    .commit()
} catch (error) {
  // All operations rolled back:
  // - msg1 NOT acknowledged (will be redelivered)
  // - Nothing pushed to q1
  // - Nothing pushed to q2
  
  console.error('Transaction failed:', error.message)
}
```

## Limitations

### 1. No Nested Transactions

```javascript
// NOT SUPPORTED
await queen.transaction()
  .ack(msg1)
  .execute(async () => {
    await queen.transaction()  // ❌ Nested transaction
      .ack(msg2)
      .commit()
  })
  .commit()
```

### 2. Connection Held During Transaction

```javascript
// ❌ Avoid: Long operations in transaction
await queen.transaction()
  .ack(msg)
  .execute(async () => {
    await sleep(60000)  // Holds connection for 1 minute!
  })
  .queue('next').push([...])
  .commit()

// ✅ Better: Keep transactions fast
const result = await longRunningOperation()  // Outside transaction
await queen.transaction()
  .ack(msg)
  .queue('next').push([{ data: result }])
  .commit()  // Fast!
```

### 3. No Cross-Database Transactions

Transactions are limited to Queen/PostgreSQL only:

```javascript
// NOT SUPPORTED
await queen.transaction()
  .ack(msg)
  .execute(async () => {
    await updateMongoDB()  // ❌ Different database
  })
  .commit()

// Use saga pattern for distributed transactions
```

## Related Topics

- [Queues & Partitions](/guide/queues-partitions) - Understanding FIFO ordering
- [Consumer Groups](/guide/consumer-groups) - Multi-purpose processing
- [Long Polling](/guide/long-polling) - Efficient message waiting
- [Dead Letter Queue](/guide/dlq) - Handling failures

## Summary

Queen MQ transactions provide:

- ✅ **Atomicity**: All operations succeed or all fail
- ✅ **Reliability**: PostgreSQL ACID guarantees
- ✅ **Exactly-Once**: When combined with transaction IDs
- ✅ **Flexibility**: Mix ACK and PUSH operations
- ✅ **Performance**: 500-1,000 transactions/second

Transactions are essential for building reliable message-driven systems! 🎯

