# Message Tracing Feature Implementation

## Overview
Implemented distributed tracing capability that allows consumers to record processing steps while handling messages. These trace events are stored in the database and displayed in the frontend dashboard.

## Implementation Summary

### ✅ Database Layer
**Files Modified:**
- `server/src/managers/queue_manager.cpp` (lines 2598-2662)

**Tables Created:**
1. `queen.message_traces` - Main trace events table
   - Columns: id, message_id, partition_id, transaction_id, consumer_group, event_type, data (JSONB), created_at, worker_id
   
2. `queen.message_trace_names` - Junction table for multi-category support
   - Columns: trace_id, trace_name
   - Primary Key: (trace_id, trace_name)

**Indexes Created:**
- `idx_message_traces_message_id` - Fast lookup by message
- `idx_message_traces_transaction_partition` - Fast lookup by transaction + partition
- `idx_message_traces_created_at` - Chronological ordering
- `idx_message_trace_names_name` - Fast lookup by trace name
- `idx_message_trace_names_trace_id` - Fast lookup by trace ID

### ✅ Server-Side (C++)
**Files Modified:**

1. **server/include/queen/queue_manager.hpp** (lines 234-254)
   - Added `record_trace()` method signature
   - Added `get_message_traces()` method signature  
   - Added `get_traces_by_name()` method signature

2. **server/src/managers/queue_manager.cpp** (lines 2723-2948)
   - Implemented `record_trace()` - Stores trace with transaction support
   - Implemented `get_message_traces()` - Retrieves traces for a message
   - Implemented `get_traces_by_name()` - Retrieves traces by trace name (cross-message correlation)

3. **server/src/acceptor_server.cpp** (lines 1731-1821)
   - Added `POST /api/v1/traces` endpoint - Record trace event
   - Added `GET /api/v1/traces/:partitionId/:transactionId` endpoint - Get message traces
   - Added `GET /api/v1/traces/by-name/:traceName` endpoint - Get traces by name

### ✅ Client-Side (JavaScript)
**Files Modified:**

1. **client-js/client-v2/consumer/ConsumerManager.js** (lines 148-355)
   - Added `#enhanceMessagesWithTrace()` method
   - Automatically adds `trace()` method to all consumed messages
   - Implements safe error handling (NEVER crashes consumer)
   - Supports single or array of trace names
   - Validates input and logs failures gracefully

### ✅ Frontend (Vue.js)
**Files Modified:**

1. **webapp/src/api/messages.js** (lines 26-28)
   - Added `getTraces()` API method
   - Added `getTracesByName()` API method

2. **webapp/src/components/messages/MessageDetailPanel.vue**
   - Added trace events display section (lines 70-127)
   - Shows chronological timeline of processing events
   - Displays trace names as colored badges
   - Color-coded by event type (info, error, step, processing, warning)
   - Shows event data with expandable JSON view
   - Added helper functions for formatting (lines 254-288)

## API Documentation

### POST /api/v1/traces
Record a trace event for a message.

**Request Body:**
```json
{
  "transactionId": "msg-123",
  "partitionId": "uuid",
  "consumerGroup": "workers",
  "traceNames": ["tenant-acme", "chat-room-123"],
  "eventType": "info",
  "data": {
    "text": "Processing started",
    "orderId": 123,
    "customField": "value"
  }
}
```

**Response:**
```json
{
  "success": true
}
```

### GET /api/v1/traces/:partitionId/:transactionId
Get all trace events for a specific message.

**Response:**
```json
{
  "traces": [
    {
      "id": "uuid",
      "event_type": "info",
      "data": { "text": "Processing started" },
      "trace_names": ["tenant-acme", "chat-room-123"],
      "consumer_group": "workers",
      "worker_id": "0",
      "created_at": "2025-10-28T12:00:00Z"
    }
  ],
  "count": 1
}
```

### GET /api/v1/traces/by-name/:traceName
Get all trace events with a specific trace name (cross-message correlation).

**Query Parameters:**
- `limit` (default: 100) - Max results to return
- `offset` (default: 0) - Pagination offset

**Response:**
```json
{
  "traces": [...],
  "count": 10,
  "total": 42,
  "limit": 100,
  "offset": 0
}
```

## Usage Examples

### Example 1: Simple Tracing
```javascript
await queen.queue('orders').consume(async (msg) => {
  await msg.trace({
    data: { text: 'Processing started' }
  });
  
  const order = await processOrder(msg.payload);
  
  await msg.trace({
    data: { 
      text: 'Order processed',
      orderId: order.id
    }
  });
}, { autoAck: true });
```

### Example 2: Multi-Category Tracing
```javascript
await queen.queue('chat-messages').consume(async (msg) => {
  const { tenantId, roomId, userId } = msg.payload;
  
  await msg.trace({
    traceName: [
      `tenant-${tenantId}`,
      `room-${roomId}`,
      `user-${userId}`
    ],
    eventType: 'processing',
    data: {
      text: 'Message received',
      messageLength: msg.payload.message.length
    }
  });
  
  // Process message...
  
  await msg.trace({
    traceName: [`tenant-${tenantId}`, `room-${roomId}`, `user-${userId}`],
    data: { text: 'Message delivered' }
  });
});

// Query by any category:
// - All traces for tenant: GET /api/v1/traces/by-name/tenant-acme
// - All traces for room: GET /api/v1/traces/by-name/room-123
// - All traces for user: GET /api/v1/traces/by-name/user-456
```

### Example 3: Error Tracking
```javascript
await queen.queue('analytics').consume(async (msg) => {
  try {
    await msg.trace({
      data: { text: 'Started analytics job' }
    });
    
    const result = await compute(msg.payload);
    
    await msg.trace({
      data: { text: 'Job completed', recordsProcessed: result.count }
    });
  } catch (error) {
    await msg.trace({
      eventType: 'error',
      data: {
        text: 'Job failed',
        error: error.message,
        stack: error.stack
      }
    });
    throw error; // Still fail the message
  }
});
```

### Example 4: Cross-Service Workflow
```javascript
// Service 1: Order processing
await queen.queue('orders').consume(async (msg) => {
  const orderId = msg.payload.orderId;
  
  await msg.trace({
    traceName: `order-flow-${orderId}`,
    data: { text: 'Order received', service: 'order-service' }
  });
  
  // Create inventory check message
  await queen.queue('inventory').push({ payload: { orderId } });
  
  await msg.trace({
    traceName: `order-flow-${orderId}`,
    data: { text: 'Inventory check initiated' }
  });
});

// Service 2: Inventory check
await queen.queue('inventory').consume(async (msg) => {
  await msg.trace({
    traceName: `order-flow-${msg.payload.orderId}`,
    data: { text: 'Checking inventory', service: 'inventory-service' }
  });
  
  // Process...
});

// Service 3: Payment processing
await queen.queue('payments').consume(async (msg) => {
  await msg.trace({
    traceName: `order-flow-${msg.payload.orderId}`,
    data: { text: 'Processing payment', service: 'payment-service' }
  });
  
  // Process...
});

// Query entire workflow:
// GET /api/v1/traces/by-name/order-flow-ORD-123
// Returns ALL traces across orders → inventory → payments!
```

## Key Features

✅ **Multi-dimensional categorization** - Tag traces with multiple names for flexible querying  
✅ **Cross-message correlation** - Track workflows across multiple messages  
✅ **Never crashes** - Safe error handling, trace failures are logged but don't throw  
✅ **Flexible data structure** - JSONB data field accepts any structure  
✅ **Timeline visualization** - Frontend shows chronological processing events  
✅ **Color-coded events** - Different colors for info, error, step, processing, warning  
✅ **Worker tracking** - Shows which worker recorded each trace  
✅ **Efficient indexes** - Fast queries with btree indexes (no GIN)  
✅ **Transaction support** - Atomic trace recording with rollback on failure  

## Database Schema Migration

For existing deployments, run the following SQL to add trace tables:

```sql
-- Add trace tables
CREATE TABLE IF NOT EXISTS queen.message_traces (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    message_id UUID REFERENCES queen.messages(id) ON DELETE CASCADE,
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    transaction_id VARCHAR(255) NOT NULL,
    consumer_group VARCHAR(255),
    event_type VARCHAR(100),
    data JSONB NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    worker_id VARCHAR(255)
);

CREATE TABLE IF NOT EXISTS queen.message_trace_names (
    trace_id UUID REFERENCES queen.message_traces(id) ON DELETE CASCADE,
    trace_name TEXT NOT NULL,
    PRIMARY KEY (trace_id, trace_name)
);

-- Add indexes
CREATE INDEX idx_message_traces_message_id ON queen.message_traces(message_id);
CREATE INDEX idx_message_traces_transaction_partition ON queen.message_traces(transaction_id, partition_id);
CREATE INDEX idx_message_traces_created_at ON queen.message_traces(created_at DESC);
CREATE INDEX idx_message_trace_names_name ON queen.message_trace_names(trace_name);
CREATE INDEX idx_message_trace_names_trace_id ON queen.message_trace_names(trace_id);
```

For new deployments, tables will be created automatically on server startup.

## Testing

To test the tracing feature:

1. **Start the server** - Tables will be created automatically
2. **Create a test consumer** with trace calls
3. **View traces in the frontend** - Open message detail panel to see timeline
4. **Query by trace name** - Use API to find related traces across messages

## Performance Considerations

- Trace writes are async but awaited (safe, won't crash)
- Uses database transactions for atomicity
- Btree indexes for fast queries (no GIN overhead)
- Optional retention policy can be added for cleanup

## Future Enhancements

Potential improvements for the future:
- Trace sampling (only trace X% of messages)
- Batch trace writes for high throughput
- Retention/cleanup job for old traces
- OpenTelemetry integration
- Full-text search on trace data
- Metrics aggregation from traces

