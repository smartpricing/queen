# Transactions in Queen MQ

## Overview

Transactions in Queen MQ provide atomic execution of multiple operations, ensuring all-or-nothing semantics. This is crucial for building reliable, exactly-once message processing systems and complex workflows.

## What are Transactions?

A transaction allows you to combine multiple queue operations (PUSH and ACK) into a single atomic unit:

- **All operations succeed together** → Transaction commits
- **Any operation fails** → Transaction rolls back, no changes made

### Why Transactions Matter

**Without Transactions:**
```javascript
// Process message from input queue
const msg = await queen.pop('input-queue')
await processMessage(msg.data)

// Push to next queue
await queen.push('next-queue', { result: 'processed' })

// ACK original message
await queen.ack(msg)

// ❌ Problem: If ACK fails, message is redelivered
//            but already pushed to next-queue
//            Result: DUPLICATE PROCESSING
```

**With Transactions:**
```javascript
// Atomic: both PUSH and ACK succeed, or neither happens
await queen.transaction()
  .ack(msg)
  .queue('next-queue')
  .push([{ result: 'processed' }])
  .commit()

// ✅ If ACK fails, PUSH is also rolled back
// ✅ If PUSH fails, ACK is also rolled back
// ✅ Result: EXACTLY-ONCE PROCESSING
```

## Transaction Types

### 1. ACK + PUSH (Pipeline Pattern)

Most common: process a message and forward to next stage.

```javascript
await queen
  .transaction()
  .ack(inputMessage)  // ACK from input queue
  .queue('next-step')
  .partition('workflow-1')
  .push([
    { data: { step: 2, result: 'processed' } }
  ])
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
  .queue('email-queue')
  .push([{ to: 'user@example.com', subject: 'Order confirmed' }])
  .queue('analytics-queue')
  .push([{ event: 'order', orderId: 123 }])
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
- Correlation

### 4. Conditional Operations

Execute operations based on processing results.

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

## Implementation Details

### Transaction Flow

```
1. Client initiates transaction
   const txn = queen.transaction()
        ↓
2. Client adds operations
   txn.ack(msg1)
   txn.queue('q2').push([...])
   txn.queue('q3').push([...])
        ↓
3. Client commits transaction
   await txn.commit()
        ↓
4. Server receives transaction request
   POST /api/v1/transaction
   {
     "operations": [
       { "type": "ack", "transactionId": "...", "partitionId": "..." },
       { "type": "push", "items": [...] },
       { "type": "push", "items": [...] }
     ]
   }
        ↓
5. Server acquires database connection
   auto conn = async_db_pool_->acquire();
        ↓
6. Server begins transaction
   BEGIN
        ↓
7. Server executes operations in sequence
   For each operation:
     - Validate parameters
     - Execute SQL
     - Check result
     - If any fails → ROLLBACK and return error
        ↓
8. All operations succeeded
   COMMIT
        ↓
9. Return results to client
   {
     "success": true,
     "transaction_id": "txn-uuid",
     "results": [...]
   }
```

### Server-Side Implementation

```cpp
TransactionResult execute_transaction(
    const std::vector<nlohmann::json>& operations
) {
    TransactionResult txn_result;
    txn_result.transaction_id = generate_transaction_id();
    txn_result.success = true;
    
    try {
        // Acquire single connection for entire transaction
        auto conn = async_db_pool_->acquire();
        
        // Begin PostgreSQL transaction
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            // Execute each operation in sequence
            for (const auto& op : operations) {
                std::string op_type = op.value("type", "");
                nlohmann::json op_result;
                
                if (op_type == "push") {
                    // Execute PUSH operation using same connection
                    auto items = op["items"];
                    op_result["type"] = "push";
                    op_result["count"] = items.size();
                    
                    // Insert messages within transaction
                    for (const auto& item : items) {
                        // ... execute INSERT using shared connection ...
                    }
                    
                } else if (op_type == "ack") {
                    // Execute ACK operation using same connection
                    op_result["type"] = "ack";
                    
                    // Validate partition_id (REQUIRED)
                    std::string partition_id = op.value("partitionId", "");
                    if (partition_id.empty()) {
                        throw std::runtime_error("partitionId is required");
                    }
                    
                    // Update consumer cursor within transaction
                    // ... execute UPDATE using shared connection ...
                    
                } else {
                    throw std::runtime_error("Unknown operation type");
                }
                
                txn_result.results.push_back(op_result);
            }
            
            // All operations succeeded - COMMIT
            sendAndWait(conn.get(), "COMMIT");
            getCommandResult(conn.get());
            
        } catch (const std::exception& e) {
            // Any operation failed - ROLLBACK
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            
            txn_result.success = false;
            txn_result.error = e.what();
        }
        
    } catch (const std::exception& e) {
        txn_result.success = false;
        txn_result.error = e.what();
    }
    
    return txn_result;
}
```

### Key Implementation Features

1. **Single Database Connection**
   - All operations use same connection
   - Ensures proper transaction isolation
   - Reduces connection overhead

2. **Sequential Execution**
   - Operations executed in order specified by client
   - Deterministic behavior
   - Error propagation

3. **All-or-Nothing**
   - PostgreSQL ACID guarantees
   - Either all operations commit or all rollback
   - No partial success

4. **Error Handling**
   - First error triggers ROLLBACK
   - Detailed error message returned
   - Remaining operations not executed

## Usage Examples

### Basic ACK + PUSH

```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

// Consume from input queue
await queen
  .queue('input-queue')
  .group('processors')
  .consume(async (message) => {
    // Process message
    const result = await processData(message.data)
    
    // Atomically ACK and push to next queue
    await queen
      .transaction()
      .ack(message)
      .queue('output-queue')
      .partition('workflow-1')
      .push([{ data: result }])
      .commit()
  })
```

### HTTP API

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
        "items": [
          {
            "queue": "output-queue",
            "partition": "workflow-1",
            "payload": {"result": "processed"}
          }
        ]
      }
    ]
  }'
```

**Response:**
```json
{
  "success": true,
  "transaction_id": "txn-019a0c11-7fe8-7001-9999-0123456789ab",
  "results": [
    {
      "type": "ack",
      "message": "acknowledged"
    },
    {
      "type": "push",
      "count": 1,
      "results": [
        {
          "transaction_id": "019a0c11-7fe8-7001-9999-0123456789ac",
          "status": "pushed",
          "message_id": "msg-uuid"
        }
      ]
    }
  ]
}
```

### Fan-out Pattern

```javascript
// Process order and trigger multiple actions
await queen
  .queue('orders')
  .consume(async (order) => {
    await queen
      .transaction()
      .ack(order)
      
      // Send confirmation email
      .queue('email-queue')
      .push([{
        to: order.data.email,
        subject: 'Order Confirmed',
        orderId: order.data.id
      }])
      
      // Update inventory
      .queue('inventory-queue')
      .push([{
        action: 'decrement',
        items: order.data.items
      }])
      
      // Record analytics
      .queue('analytics-queue')
      .push([{
        event: 'order_placed',
        amount: order.data.total,
        timestamp: new Date()
      }])
      
      // Trigger fulfillment
      .queue('fulfillment-queue')
      .push([{
        orderId: order.data.id,
        address: order.data.shippingAddress
      }])
      
      .commit()
  })
```

### State Machine Pattern

```javascript
// Workflow state transitions
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
      
      // Record state change in audit queue
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

### Retry with Backoff

```javascript
await queen
  .queue('api-calls')
  .consume(async (message) => {
    const retryCount = message.data.retryCount || 0
    
    try {
      await callExternalAPI(message.data.request)
      
      // Success - just ACK
      await queen.ack(message)
      
    } catch (error) {
      if (retryCount < 3) {
        // Retry with exponential backoff
        const delaySeconds = Math.pow(2, retryCount) * 60  // 1m, 2m, 4m
        
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
        // Max retries - move to DLQ
        await queen.nack(message, 'Max retries exceeded')
      }
    }
  })
```

### Saga Pattern

```javascript
// Distributed transaction across services
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
      
      // Wait for step completion
      await waitForStepCompletion(result.transaction_id)
    }
    
    // All steps succeeded
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

## Advanced Patterns

### Exactly-Once Processing

Combine transaction IDs with transactions for guaranteed exactly-once:

```javascript
await queen
  .transaction()
  .ack(inputMessage)
  .queue('output-queue')
  .partition('workflow-1')
  .push([{
    // Use deterministic transaction ID
    transactionId: `output-${inputMessage.transactionId}`,
    data: { result: 'processed' }
  }])
  .commit()

// If transaction fails and retries:
// - ACK will succeed (idempotent)
// - PUSH will be rejected (duplicate transactionId)
// - Overall transaction still succeeds
// - Result: exactly-once delivery
```

### Message Aggregation

Collect multiple messages and produce single output:

```javascript
const buffer = []

await queen
  .queue('events')
  .group('aggregator')
  .consume(async (message) => {
    buffer.push(message)
    
    // Aggregate every 100 messages
    if (buffer.length >= 100) {
      const txn = queen.transaction()
      
      // ACK all buffered messages
      for (const msg of buffer) {
        txn.ack(msg)
      }
      
      // Push aggregated result
      txn.queue('aggregated-events')
         .push([{
           count: buffer.length,
           data: buffer.map(m => m.data)
         }])
      
      await txn.commit()
      buffer.length = 0
    }
  })
```

### Request-Reply Pattern

```javascript
// Request side
const replyQueue = `reply-${generateUUID()}`
const correlationId = generateUUID()

await queen
  .queue(replyQueue)
  .config({ retentionSeconds: 300 })  // Temp queue, 5min retention
  .create()

await queen
  .queue('request-queue')
  .push([{
    data: { request: 'getData', params: {...} },
    replyTo: replyQueue,
    correlationId
  }])

// Wait for reply
const reply = await queen
  .queue(replyQueue)
  .consume(async (message) => {
    if (message.data.correlationId === correlationId) {
      return message.data.response
    }
  })

// Reply side
await queen
  .queue('request-queue')
  .consume(async (request) => {
    const response = await processRequest(request.data)
    
    await queen
      .transaction()
      .ack(request)
      .queue(request.data.replyTo)
      .push([{
        correlationId: request.data.correlationId,
        response
      }])
      .commit()
  })
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

| Metric | Value | Configuration |
|--------|-------|---------------|
| Transactions/s | 500-1,000 | Simple ACK+PUSH |
| Transactions/s | 200-500 | Complex (5+ ops) |
| Operations/s | 5,000-10,000 | Total ops across all transactions |

### Best Practices

1. **Keep transactions small**
```javascript
// Good: 2-5 operations
await queen.transaction()
  .ack(msg)
  .queue('next').push([...])
  .commit()

// Avoid: 50+ operations
// Split into multiple transactions instead
```

2. **Use batch operations within transactions**
```javascript
// Good: Batch PUSH
await queen.transaction()
  .ack(msg)
  .queue('output').push([
    { data: item1 },
    { data: item2 },
    { data: item3 }
  ])
  .commit()

// Avoid: Multiple separate PUSHes
```

3. **Handle errors appropriately**
```javascript
try {
  await queen.transaction()
    .ack(msg)
    .queue('next').push([...])
    .commit()
} catch (error) {
  // Transaction rolled back - message will be redelivered
  console.error('Transaction failed:', error)
  // Don't manually ACK or NACK - let it retry
}
```

## Error Handling

### Transaction Rollback

```javascript
try {
  await queen.transaction()
    .ack(msg1)
    .queue('q1').push([{ data: 'valid' }])
    .queue('q2').push([{ data: 'invalid' }])  // Fails
    .commit()
} catch (error) {
  // All operations rolled back:
  // - msg1 NOT acknowledged (will be redelivered)
  // - Nothing pushed to q1
  // - Nothing pushed to q2
  
  console.error('Transaction failed:', error.message)
}
```

### Partial Success Handling

```javascript
// Use try-catch around individual operations
const txn = queen.transaction()

try {
  await txn.ack(msg)
} catch (error) {
  console.error('ACK failed:', error)
  throw error  // Abort transaction
}

try {
  await txn.queue('primary').push([...])
} catch (error) {
  console.error('Primary push failed:', error)
  throw error  // Abort transaction
}

try {
  await txn.queue('optional').push([...])
} catch (error) {
  // Optional operation - log but continue
  console.warn('Optional push failed:', error)
}

await txn.commit()
```

### Idempotency

Ensure transactions are idempotent:

```javascript
async function processMessageIdempotent(message) {
  // Generate deterministic IDs
  const outputTxId = `output-${message.transactionId}`
  
  try {
    await queen.transaction()
      .ack(message)
      .queue('output')
      .push([{
        transactionId: outputTxId,  // Deterministic
        data: { processed: true }
      }])
      .commit()
      
  } catch (error) {
    if (error.message.includes('duplicate')) {
      // Already processed - safe to ignore
      console.log('Already processed:', message.transactionId)
    } else {
      throw error
    }
  }
}
```

## Limitations

### 1. No Nested Transactions

PostgreSQL doesn't support nested transactions:

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

// Use savepoints if you need nesting
```

### 2. Connection Held During Transaction

Transaction holds database connection until commit:

```javascript
// Avoid long-running operations in transaction
await queen.transaction()
  .ack(msg)
  .execute(async () => {
    await sleep(60000)  // ❌ Holds connection for 1 minute
  })
  .queue('next').push([...])
  .commit()

// Better: Keep transactions fast
const result = await longRunningOperation()  // Outside transaction
await queen.transaction()
  .ack(msg)
  .queue('next').push([{ data: result }])
  .commit()  // Fast transaction
```

### 3. No Cross-Database Transactions

Transactions are limited to single PostgreSQL database:

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

## Monitoring & Debugging

### Transaction Metrics

```javascript
const txnMetrics = {
  total: 0,
  successful: 0,
  failed: 0,
  latencies: [],
  
  async execute(txnFn) {
    this.total++
    const start = Date.now()
    
    try {
      const result = await txnFn()
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
      failed: this.failed,
      successRate: (this.successful / this.total * 100).toFixed(2) + '%',
      avgLatency: (this.latencies.reduce((a, b) => a + b, 0) / this.latencies.length).toFixed(2) + 'ms'
    }
  }
}

// Usage
await txnMetrics.execute(async () => {
  return await queen.transaction()
    .ack(msg)
    .queue('next').push([...])
    .commit()
})
```

### Transaction Logging

```javascript
class LoggedTransaction {
  constructor() {
    this.operations = []
  }
  
  ack(message) {
    this.operations.push({
      type: 'ack',
      transactionId: message.transactionId,
      partitionId: message.partitionId
    })
    return this
  }
  
  queue(name) {
    this.currentQueue = name
    return this
  }
  
  push(items) {
    this.operations.push({
      type: 'push',
      queue: this.currentQueue,
      count: items.length
    })
    return this
  }
  
  async commit() {
    console.log('Transaction starting:', JSON.stringify(this.operations, null, 2))
    
    try {
      const result = await queen.transaction()
        // ... apply operations ...
        .commit()
      
      console.log('Transaction succeeded:', result.transaction_id)
      return result
      
    } catch (error) {
      console.error('Transaction failed:', error.message)
      console.error('Operations:', JSON.stringify(this.operations, null, 2))
      throw error
    }
  }
}
```

## Best Practices Summary

1. ✅ **Keep transactions small** - 2-5 operations ideal
2. ✅ **Use batch operations** - Push multiple items at once
3. ✅ **Handle errors gracefully** - Let transactions retry on rollback
4. ✅ **Make transactions idempotent** - Use deterministic transaction IDs
5. ✅ **Avoid long operations** - Keep transactions fast (<200ms)
6. ✅ **Always include partitionId** - Required for ACK operations
7. ✅ **Monitor transaction metrics** - Track success rate and latency
8. ✅ **Log transaction details** - Aid in debugging failures

## Related Documentation

- [Architecture](ARCHITECTURE.md) - System architecture overview
- [ACK System](ACK.md) - Acknowledgment mechanisms in detail
- [Push Operations](PUSH.md) - How PUSH works within transactions
- [Server Features](SERVER_FEATURES.md) - Complete feature list
- [API Reference](../server/API.md) - HTTP API details

## Summary

Queen MQ transactions provide:

- ✅ **Atomicity**: All operations succeed or all fail
- ✅ **Reliability**: PostgreSQL ACID guarantees
- ✅ **Exactly-Once**: When combined with transaction IDs
- ✅ **Flexibility**: Mix ACK and PUSH operations
- ✅ **Performance**: 500-1,000 transactions/second
- ✅ **Simplicity**: Fluent API for easy usage

Transactions are essential for building reliable, exactly-once message processing systems in Queen MQ.

