# Message Tracing

Record breadcrumbs as messages flow through your system to debug distributed workflows and track processing timelines.

## What is Message Tracing?

Message tracing lets you record events during message processing. Each trace captures a timestamp, event data, and optional categorization, creating a timeline of what happened to a message as it moved through your system.

Unlike traditional distributed tracing (OpenTelemetry, Jaeger), Queen's message tracing is **message-centric** - it tracks the lifecycle and processing history of individual messages within the queue system.

## Basic Tracing

When you consume messages, each message gets a `trace()` method you can call to record events:

```javascript
await queen.queue('orders').consume(async (msg) => {
  // Record start
  await msg.trace({
    data: { text: 'Order processing started' }
  })
  
  // Do work
  const order = await processOrder(msg.data)
  
  // Record completion
  await msg.trace({
    data: { 
      text: 'Order processed successfully',
      orderId: order.id,
      total: order.total
    }
  })
}, { autoAck: true })
```

**Key Properties:**
- ✅ **Safe** - Never crashes your consumer (errors are logged but don't throw)
- ✅ **Async** - Call with `await` but failures won't break your flow
- ✅ **Flexible** - Store any JSON data you want

## Trace Names - Cross-Message Correlation

The real power of tracing comes from **trace names** - they let you link traces across multiple messages to track distributed workflows:

```javascript
// Service 1: Order Service
await queen.queue('orders').consume(async (msg) => {
  const orderId = msg.data.orderId
  
  await msg.trace({
    traceName: `order-${orderId}`,  // 👈 Link with this name
    data: { text: 'Order created', service: 'orders' }
  })
  
  // Push to inventory queue
  await queen.queue('inventory').push([{
    data: { orderId, items: msg.data.items }
  }])
}, { autoAck: true })

// Service 2: Inventory Service  
await queen.queue('inventory').consume(async (msg) => {
  const orderId = msg.data.orderId
  
  await msg.trace({
    traceName: `order-${orderId}`,  // 👈 Same name = connected!
    data: { text: 'Stock checked', service: 'inventory' }
  })
  
  // Push to payments queue
  await queen.queue('payments').push([{
    data: { orderId }
  }])
}, { autoAck: true })

// Service 3: Payment Service
await queen.queue('payments').consume(async (msg) => {
  const orderId = msg.data.orderId
  
  await msg.trace({
    traceName: `order-${orderId}`,  // 👈 All connected!
    data: { text: 'Payment processed', service: 'payments' }
  })
}, { autoAck: true })
```

Now you can search for `order-12345` in the Traces UI and see the **entire workflow** across all three services!

## Multi-Dimensional Tracking

You can provide **multiple trace names** to organize traces by different dimensions:

```javascript
await queen.queue('chat-messages').consume(async (msg) => {
  const { tenantId, roomId, userId } = msg.data
  
  await msg.trace({
    traceName: [
      `tenant-${tenantId}`,    // Track by tenant
      `room-${roomId}`,        // Track by room  
      `user-${userId}`         // Track by user
    ],
    data: { text: 'Message sent' }
  })
}, { autoAck: true })
```

**Query any dimension:**
- Search `tenant-acme` → See all tenant activity
- Search `room-123` → See all room activity
- Search `user-456` → See all user activity

## Event Types

Organize traces with event types for visual distinction:

```javascript
await msg.trace({
  eventType: 'info',
  data: { text: 'Started processing' }
})

await msg.trace({
  eventType: 'step',
  data: { text: 'Validated data' }
})

await msg.trace({
  eventType: 'warning',
  data: { text: 'Slow operation detected', durationMs: 5000 }
})

await msg.trace({
  eventType: 'error',
  data: { text: 'Validation failed', reason: 'Invalid email' }
})

await msg.trace({
  eventType: 'processing',
  data: { text: 'Sending email' }
})
```

**Available Event Types:**
- `info` (default) - General information
- `step` - Processing step
- `processing` - Active processing
- `warning` - Warning condition
- `error` - Error occurred

## Error Tracking

Use traces to record errors without breaking your flow:

```javascript
await queen.queue('analytics').consume(async (msg) => {
  try {
    await msg.trace({ data: { text: 'Job started' } })
    
    const result = await computeAnalytics(msg.data)
    
    await msg.trace({
      data: { 
        text: 'Job completed',
        recordsProcessed: result.count
      }
    })
  } catch (error) {
    // Record the error (safe - won't crash!)
    await msg.trace({
      eventType: 'error',
      data: { 
        text: 'Job failed',
        error: error.message,
        stack: error.stack
      }
    })
    
    throw error  // Still fail the message for retry
  }
}, { autoAck: true })
```

## Performance Tracking

Track timing and metrics:

```javascript
await queen.queue('reports').consume(async (msg) => {
  const start = Date.now()
  
  await msg.trace({ data: { text: 'Report generation started' } })
  
  const data = await fetchData(msg.data)
  const fetchTime = Date.now() - start
  
  await msg.trace({
    data: { 
      text: 'Data fetched',
      durationMs: fetchTime,
      rowCount: data.length
    }
  })
  
  const report = await generatePDF(data)
  
  await msg.trace({
    data: { 
      text: 'Report generated',
      totalDurationMs: Date.now() - start,
      sizeKB: Math.round(report.size / 1024)
    }
  })
}, { autoAck: true })
```

## Viewing Traces

### Method 1: From Message Details

1. Go to **Messages** page in the dashboard
2. Click on any message
3. View "Processing Timeline" with all trace events

### Method 2: Search by Trace Name

1. Go to **Traces** page  
2. Enter a trace name (e.g., `order-12345`)
3. See timeline across **ALL messages** with that name

### Method 3: Browse Available Traces

1. Go to **Traces** page
2. See list of all trace names with statistics
3. Click any trace name to view details

## API Reference

### Message Trace Method

Each consumed message has a `trace()` method:

```javascript
await msg.trace({
  traceName: string | string[],  // Optional: name(s) for correlation
  eventType: string,              // Optional: 'info', 'error', 'step', 'processing', 'warning'
  data: object                    // Required: any JSON data
})
```

**Parameters:**

- **`traceName`** (optional) - Single string or array of strings for categorization
  - Use meaningful business identifiers: `order-123`, `user-456`, `tenant-acme`
  - Multiple names enable multi-dimensional tracking
  - Use hyphens/underscores for readability

- **`eventType`** (optional, default: `'info'`) - Type of event
  - `info` - General information
  - `step` - Processing step
  - `processing` - Active processing
  - `warning` - Warning condition
  - `error` - Error occurred

- **`data`** (required) - JSON object with trace data
  - Must include at least some information
  - Common pattern: `{ text: '...', ...extraData }`
  - Can include IDs, durations, counts, errors, etc.

**Returns:** `Promise<{ success: boolean, error?: string }>`

## Use Cases

### 1. Debug Distributed Workflows

Track messages as they flow through multiple queues and services:

```javascript
await msg.trace({
  traceName: `workflow-${workflowId}`,
  data: { step: 'payment', status: 'completed' }
})
```

### 2. Multi-Tenant Operations

Track activity per tenant across all queues:

```javascript
await msg.trace({
  traceName: `tenant-${tenantId}`,
  data: { operation: 'data-sync', recordCount: 150 }
})
```

### 3. User Journey Tracking

Follow a user's actions across your system:

```javascript
await msg.trace({
  traceName: `user-${userId}`,
  data: { action: 'checkout', amount: 99.99 }
})
```

### 4. Performance Analysis

Identify bottlenecks with timing data:

```javascript
await msg.trace({
  data: { 
    step: 'database-query',
    durationMs: 1234,
    rowCount: 5000
  }
})
```

### 5. Error Investigation

Find where and why failures occur:

```javascript
await msg.trace({
  eventType: 'error',
  traceName: `order-${orderId}`,
  data: { 
    error: 'Payment declined',
    code: 'insufficient_funds'
  }
})
```

### 6. Audit Trails

Create compliance logs:

```javascript
await msg.trace({
  traceName: [`tenant-${tenantId}`, `user-${userId}`],
  data: { 
    action: 'data-export',
    timestamp: new Date().toISOString(),
    ipAddress: req.ip
  }
})
```

## Best Practices

### 1. Use Meaningful Trace Names

✅ **Good:**
```javascript
traceName: 'order-12345'
traceName: 'user-email-verification-abc123'
traceName: 'invoice-generation-2024-11-18'
```

❌ **Bad:**
```javascript
traceName: 'trace123'
traceName: 'abc'
traceName: msg.transactionId  // Use business IDs, not transaction IDs
```

### 2. Include Context in Data

✅ **Good:**
```javascript
data: { 
  text: 'Payment processed',
  orderId: '12345',
  amount: 99.99,
  gateway: 'stripe'
}
```

❌ **Bad:**
```javascript
data: { text: 'Done' }
```

### 3. Use Event Types Consistently

```javascript
// Start with info
await msg.trace({ eventType: 'info', data: { text: 'Started' } })

// Progress with step
await msg.trace({ eventType: 'step', data: { text: 'Validated' } })

// Errors with error
await msg.trace({ eventType: 'error', data: { text: 'Failed', reason: '...' } })
```

### 4. Propagate Trace Names

Extract business identifiers from message data and use them consistently:

```javascript
const orderId = msg.data.orderId
const traceName = `order-${orderId}`

// Use in all related traces
await msg.trace({ traceName, data: { step: 1 } })
await msg.trace({ traceName, data: { step: 2 } })

// Use when pushing to other queues (include in message data)
await queen.queue('next-step').push([{
  data: { orderId, ...otherData }  // Pass orderId so next service can trace
}])
```

### 5. Trace Both Success and Failure

```javascript
try {
  await msg.trace({ data: { text: 'Processing started' } })
  await doWork()
  await msg.trace({ data: { text: 'Processing completed' } })
} catch (error) {
  await msg.trace({ 
    eventType: 'error',
    data: { text: 'Processing failed', error: error.message }
  })
  throw error
}
```

### 6. Don't Overuse Traces

Traces are for **debugging and observability**, not for storing business data. Use them strategically:

✅ **Good:** 3-5 traces per message for key milestones

❌ **Bad:** 50 traces per message for every tiny step

## Important Notes

### Safety Guarantees

- **Never crashes** - Trace failures are logged but don't throw errors
- **Non-blocking** - Doesn't impact message processing performance  
- **Fire and forget** - Traces are stored asynchronously

### Limitations

- Traces are tied to the **message lifecycle** - they're deleted when the message is deleted
- Not a replacement for application logging or APM tools
- Best for tracking **queue-specific workflows**, not general application traces

### When to Use vs Application Logging

**Use Message Tracing when:**
- Tracking message flow across queues
- Debugging distributed workflows
- Correlating events across services
- Analyzing queue-based performance

**Use Application Logging when:**
- General application debugging
- Non-queue-related operations
- Detailed internal logic
- Security/audit events unrelated to queues

## Related

- [Consumer Groups](/guide/consumer-groups) - How to process messages with groups
- [Dead Letter Queue](/guide/dlq) - Handle failed messages
- [Web Dashboard](/webapp/overview) - View traces in the UI
- [Transactions](/guide/transactions) - Atomic operations

[Complete documentation](https://github.com/smartpricing/queen)
