# Queen Message Queue System - Implementation Plan

## Overview
High-performance message queue system with hierarchical organization (namespace → task → queue → message) backed by PostgreSQL and uWebSockets.js, using functional programming patterns.

## Architecture

```
Client SDK → HTTP → uWS Routes → Queue Manager → PostgreSQL
                         ↓
                  Long Polling + Events
```

## Project Structure

```
/src/
├── server.js                  # Main uWS server setup (HTTP + WebSocket)
├── config/
│   ├── database.js            # PostgreSQL connection config
│   └── server.js              # Server configuration
├── database/
│   ├── schema.sql             # Database schema (queen schema)
│   ├── migrations/            # Schema migrations
│   └── connection.js          # DB connection pool
├── managers/
│   ├── queueManager.js        # Core queue processing logic (factory)
│   ├── eventManager.js        # Event emitter for long polling (factory)
│   ├── resourceCache.js       # In-memory resource cache (factory)
│   └── transactionManager.js  # Transaction ID deduplication (factory)
├── routes/
│   ├── configure.js           # POST /api/v1/configure
│   ├── push.js                # POST /api/v1/push
│   ├── pop.js                 # GET /api/v1/pop variants
│   └── analytics.js           # Analytics endpoints
├── websocket/
│   ├── wsServer.js            # WebSocket server for dashboard events
│   ├── handlers.js            # WebSocket message handlers
│   └── broadcaster.js         # Event broadcasting to connected clients
├── services/
│   ├── namespaceService.js    # Namespace operations (factory)
│   ├── taskService.js         # Task operations (factory)
│   ├── queueService.js        # Queue operations (factory)
│   └── messageService.js      # Message operations (factory)
├── utils/
│   ├── uuid.js                # UUID v4 generation
│   ├── validation.js          # Request validation
│   ├── errors.js              # Error handling
│   └── functional.js          # Functional utilities (pipe, compose)
├── middleware/
│   ├── cors.js                # CORS handling
│   ├── logging.js             # Request logging
│   └── rateLimit.js           # Rate limiting
└── client/                    # Client SDK
    ├── index.js               # Client SDK entry point
    ├── queenClient.js         # Main client factory function
    ├── api/
    │   ├── configure.js       # Configure API calls
    │   ├── push.js            # Push API calls
    │   ├── pop.js             # Pop API calls with long polling
    │   └── analytics.js       # Analytics API calls
    └── utils/
        ├── http.js            # HTTP client wrapper
        └── retry.js           # Retry logic for failed requests
```

## Database Schema

### Tables Hierarchy
```
namespaces (ns)
└── tasks
    └── queues
        └── messages
```

### Schema Design
```sql
-- Create queen schema
CREATE SCHEMA IF NOT EXISTS queen;

-- Namespaces table
CREATE TABLE queen.namespaces (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) UNIQUE NOT NULL,
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP DEFAULT NOW()
);

-- Tasks table
CREATE TABLE queen.tasks (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    namespace_id UUID REFERENCES queen.namespaces(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP DEFAULT NOW(),
    UNIQUE(namespace_id, name)
);

-- Queues table
CREATE TABLE queen.queues (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    task_id UUID REFERENCES queen.tasks(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL,
    priority INTEGER DEFAULT 0,  -- Queue priority (higher = processed first)
    options JSONB DEFAULT '{}',   -- Includes leaseTime, delayedProcessing, windowBuffer
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP DEFAULT NOW(),
    UNIQUE(task_id, name)
);

-- Messages table
CREATE TABLE queen.messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    transaction_id UUID UNIQUE NOT NULL,  -- For idempotency
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    payload JSONB NOT NULL,
    status VARCHAR(20) DEFAULT 'pending',
    worker_id VARCHAR(255),
    created_at TIMESTAMP DEFAULT NOW(),
    locked_at TIMESTAMP,
    completed_at TIMESTAMP,
    failed_at TIMESTAMP,
    error_message TEXT,
    retry_count INTEGER DEFAULT 0,
    lease_expires_at TIMESTAMP  -- For lease-based processing
);

-- Transaction tracking for idempotency
CREATE TABLE queen.transactions (
    transaction_id UUID PRIMARY KEY,
    message_id UUID REFERENCES queen.messages(id),
    operation VARCHAR(20) NOT NULL, -- 'push' or 'pop'
    created_at TIMESTAMP DEFAULT NOW(),
    expires_at TIMESTAMP DEFAULT NOW() + INTERVAL '24 hours'
);

-- Queue processing coordination
CREATE TABLE queen.queue_processors (
    queue_path VARCHAR(500) PRIMARY KEY, -- ns/task/queue format
    worker_id VARCHAR(255) NOT NULL,
    claimed_at TIMESTAMP DEFAULT NOW(),
    last_activity TIMESTAMP DEFAULT NOW(),
    messages_processed INTEGER DEFAULT 0
);

-- Analytics/metrics table
CREATE TABLE queen.queue_metrics (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_id UUID REFERENCES queen.queues(id),
    metric_type VARCHAR(50), -- 'depth', 'throughput', 'latency'
    value NUMERIC,
    timestamp TIMESTAMP DEFAULT NOW()
);

-- WebSocket connections tracking
CREATE TABLE queen.ws_connections (
    connection_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    client_id VARCHAR(255),
    connected_at TIMESTAMP DEFAULT NOW(),
    last_ping TIMESTAMP DEFAULT NOW(),
    metadata JSONB DEFAULT '{}'
);
```

### Indexes
```sql
-- Performance indexes
CREATE INDEX idx_messages_queue_status_created ON queen.messages(queue_id, status, created_at);
CREATE INDEX idx_messages_status_created ON queen.messages(status, created_at);
CREATE INDEX idx_messages_transaction_id ON queen.messages(transaction_id);
CREATE INDEX idx_messages_lease_expires ON queen.messages(lease_expires_at) WHERE status = 'processing';
CREATE INDEX idx_transactions_expires ON queen.transactions(expires_at);
CREATE INDEX idx_queue_processors_activity ON queen.queue_processors(last_activity);
CREATE INDEX idx_namespaces_name ON queen.namespaces(name);
CREATE INDEX idx_tasks_namespace_name ON queen.tasks(namespace_id, name);
CREATE INDEX idx_queues_task_name ON queen.queues(task_id, name);
CREATE INDEX idx_queues_priority ON queen.queues(priority DESC);
CREATE INDEX idx_ws_connections_last_ping ON queen.ws_connections(last_ping);
```

## API Endpoints

### Configuration
```
POST /api/v1/configure
Body: {
  "ns": "email-service",
  "task": "send-notifications", 
  "queue": "high-priority",
  "options": {
    "leaseTime": 300,           // Seconds a message is leased to worker
    "maxSize": 10000,
    "ttl": 3600,
    "retryLimit": 3,
    "deadLetterQueue": true,
    "delayedProcessing": 60,     // Don't process messages until 60s old
    "windowBuffer": 30,          // Wait until last message is 30s old
    "priority": 10               // Higher priority queues processed first
  }
}
Response: 201 Created
```

### Push Messages
```
POST /api/v1/push
Body: {
  "items": [
    {
      "ns": "email-service",
      "task": "send-notifications",
      "queue": "high-priority",
      "payload": { "to": "user@example.com", "template": "welcome" },
      "transactionId": "550e8400-e29b-41d4-a716-446655440000"  // Optional, auto-generated if not provided
    }
  ],
  "config": {
    "batchMode": true,
    "autoCreate": true  // Auto-create missing ns/task/queue
  }
}
Response: 201 Created
{
  "messages": [
    {
      "id": "msg-uuid",
      "transactionId": "550e8400-e29b-41d4-a716-446655440000",
      "status": "queued"
    }
  ]
}

Note: 
- Resources (ns/task/queue) are auto-created if they don't exist
- transactionId ensures idempotency - duplicate pushes with same ID are ignored
```

### Pop Messages (Long Polling)
```
GET /api/v1/pop/ns/email-service?timeout=30000&wait=true
GET /api/v1/pop/ns/email-service/task/send-notifications?timeout=30000&wait=true  
GET /api/v1/pop/ns/email-service/task/send-notifications/queue/high-priority?timeout=30000&wait=true

Query Parameters:
- timeout: Max wait time in ms (default: 30000, max: 60000)
- wait: Enable long polling (default: false)
- batch: Number of messages to return (default: 1, max: 100)

Response: 200 OK
{
  "messages": [
    {
      "id": "msg-uuid",
      "transactionId": "550e8400-e29b-41d4-a716-446655440000",
      "payload": { "to": "user@example.com", "template": "welcome" },
      "queue": "email-service/send-notifications/high-priority",
      "createdAt": "2025-10-07T12:00:00Z"
    }
  ]
}
or 204 No Content on timeout

Note: 
- Batch returns messages from the same scope, respecting queue priorities
- Each message includes its transactionId for acknowledgment
- Messages are marked as 'processing' with lease time
```

### Analytics
```
GET /api/v1/analytics/queues                    # All queue stats
GET /api/v1/analytics/ns/:nsId                  # Namespace stats  
GET /api/v1/analytics/ns/:nsId/task/:taskId     # Task stats
GET /api/v1/analytics/queue-depths              # Current queue depths
GET /api/v1/analytics/throughput                # Messages/second metrics
```

### Message Acknowledgment
```
POST /api/v1/ack
Body: {
  "transactionId": "550e8400-e29b-41d4-a716-446655440000",
  "status": "completed"  // or "failed"
  "error": "Optional error message if failed"
}
Response: 200 OK
{
  "transactionId": "550e8400-e29b-41d4-a716-446655440000",
  "status": "completed",
  "completedAt": "2025-10-07T12:00:00Z"
}

Note: 
- Must ACK within leaseTime or message returns to pending
- Failed messages may be retried based on queue configuration
```

### Batch Acknowledgment
```
POST /api/v1/ack/batch
Body: {
  "acknowledgments": [
    {
      "transactionId": "550e8400-e29b-41d4-a716-446655440000",
      "status": "completed"
    },
    {
      "transactionId": "660e8400-e29b-41d4-a716-446655440001",
      "status": "failed",
      "error": "Connection timeout"
    }
  ]
}
Response: 200 OK
{
  "processed": 2,
  "results": [
    { "transactionId": "550e8400...", "status": "completed" },
    { "transactionId": "660e8400...", "status": "failed", "retryScheduled": true }
  ]
}
```

### WebSocket Events (Dashboard)
```
WS /ws/dashboard

Events emitted to connected dashboard clients:
- message.pushed: { queue, transactionId, timestamp }
- message.processing: { queue, transactionId, workerId, timestamp }
- message.completed: { queue, transactionId, timestamp }
- message.failed: { queue, transactionId, error, timestamp }
- queue.created: { ns, task, queue, timestamp }
- queue.depth: { queue, depth, timestamp }
- worker.connected: { workerId, timestamp }
- worker.disconnected: { workerId, timestamp }
```

## Core Components (Functional Style)

### queueManager Factory
```javascript
// Factory function returns queue processing functions
export const createQueueManager = (db, eventManager, cache) => ({
  getNextMessage: async (scope) => { /* ... */ },
  processQueues: async () => { /* ... */ },
  handleDelayedProcessing: async () => { /* ... */ },
  handleWindowBuffer: async () => { /* ... */ },
  checkLeaseExpiry: async () => { /* ... */ }
})
```

### eventManager Factory
```javascript  
// Factory for event coordination
export const createEventManager = () => ({
  emit: (event, data) => { /* ... */ },
  on: (event, handler) => { /* ... */ },
  once: (event, handler) => { /* ... */ },
  removeListener: (event, handler) => { /* ... */ }
})
```

### resourceCache Factory
```javascript
// In-memory cache for resource existence
export const createResourceCache = () => ({
  checkResource: async (ns, task, queue) => { /* ... */ },
  cacheResource: (ns, task, queue) => { /* ... */ },
  invalidate: (ns, task, queue) => { /* ... */ }
})
```

### Route Handlers (Functional)
- Pure functions for request handling
- Composable middleware using pipe/compose
- Immutable request/response transformations
- Functional error handling with Either/Result patterns

## Implementation Phases

### Phase 1: Core Infrastructure (Week 1)
- [ ] Database schema and migrations
- [ ] Basic uWS server setup with routing
- [ ] PostgreSQL connection pool
- [ ] Basic CRUD operations for ns/task/queue/message
- [ ] UUID generation and validation utilities

### Phase 2: Queue Processing (Week 2)  
- [ ] OptimalQueueManager implementation
- [ ] Background message processing loop
- [ ] Queue coordination and locking
- [ ] Message status transitions (pending → processing → completed/failed)
- [ ] Worker heartbeat and cleanup mechanisms

### Phase 3: HTTP API (Week 3)
- [ ] Configure endpoint (create ns/task/queue)
- [ ] Push endpoint (insert messages)
- [ ] Basic pop endpoint (immediate fetch)
- [ ] Request validation and error handling
- [ ] API response formatting

### Phase 4: Long Polling & Advanced Features (Week 4)
- [ ] EventManager for real-time coordination
- [ ] Long polling implementation in pop routes
- [ ] Timeout handling and cleanup
- [ ] Connection management for concurrent requests
- [ ] Delayed processing queue support
- [ ] Window buffer queue support
- [ ] Lease-based message processing
- [ ] Performance testing and optimization

### Phase 5: Analytics & Monitoring (Week 5)
- [ ] Queue depth tracking
- [ ] Throughput metrics collection  
- [ ] Processing latency measurements
- [ ] Analytics API endpoints
- [ ] Basic dashboard/monitoring

### Phase 6: Client SDK & Production Features (Week 6)
- [ ] JavaScript client SDK implementation
- [ ] Client connection pooling
- [ ] Client-side retry logic
- [ ] Dead letter queues
- [ ] Message retry logic
- [ ] Queue size limits and backpressure
- [ ] Rate limiting
- [ ] Comprehensive error handling
- [ ] Performance benchmarking

## Configuration

### Environment Variables
```bash
# Database
PG_USER=postgres
PG_HOST=localhost
PG_DB=postgres
PG_PASSWORD=postgres
PG_PORT=5432
DB_POOL_SIZE=20
DB_TIMEOUT=30000

# Server  
PORT=3000
HOST=0.0.0.0
WORKER_ID=worker-${HOSTNAME}-${PID}

# Queue Processing
QUEUE_POLL_INTERVAL=100
MAX_BATCH_SIZE=100
WORKER_HEARTBEAT_INTERVAL=10000
STALE_WORKER_TIMEOUT=30000

# Long Polling
DEFAULT_TIMEOUT=30000
MAX_TIMEOUT=60000
MAX_CONCURRENT_POLLS=1000

# Analytics
METRICS_COLLECTION_INTERVAL=5000
METRICS_RETENTION_DAYS=30

# WebSocket
WS_HEARTBEAT_INTERVAL=30000
WS_MAX_CONNECTIONS=1000
```

### Queue Options Schema
```javascript
{
  leaseTime: 300,             // Seconds a message is leased to worker
  maxSize: 10000,             // Max messages in queue
  ttl: 3600,                  // Message TTL in seconds
  retryLimit: 3,              // Max retry attempts
  retryDelay: 1000,           // Delay between retries (ms)
  deadLetterQueue: true,      // Enable DLQ
  priority: 0,                // Queue priority (higher = processed first)
  delayedProcessing: 0,       // Seconds to wait before processing (0 = immediate)
  windowBuffer: 0             // Seconds to wait after last message before processing batch
}
```

## Performance Targets

- **Throughput**: 10,000+ messages/second
- **Latency**: < 10ms for immediate pop, < 100ms for long polling response
- **Concurrency**: 1,000+ concurrent long polling connections
- **Memory**: < 512MB for 1M queued messages
- **CPU**: < 50% on 4-core system at target throughput

## Testing Strategy

### Unit Tests
- Database operations (CRUD)
- Queue processing logic  
- Event management
- Utility functions

### Integration Tests
- End-to-end API workflows
- Long polling behavior
- Multi-worker coordination
- Database transaction handling

### Performance Tests
- Load testing with artillery/k6
- Concurrent connection limits
- Memory leak detection
- Database performance under load

### Chaos Tests
- Worker failure scenarios
- Database connection loss
- Network partitions
- High contention situations

## Deployment Considerations

### Docker Setup
- Multi-stage build for production
- PostgreSQL container for development
- Health checks and graceful shutdown
- Resource limits and monitoring

### Production Deployment
- Horizontal scaling with load balancer
- Database connection pooling
- Monitoring and alerting
- Backup and recovery procedures

## Client SDK Design

### Installation
```javascript
npm install queen-client
```

### Basic Usage
```javascript
import { createQueenClient } from 'queen-client'

const client = createQueenClient({
  baseUrl: 'http://localhost:3000',
  timeout: 30000,
  retryAttempts: 3
})

// Configure queue
await client.configure({
  ns: 'email-service',
  task: 'notifications',
  queue: 'high-priority',
  options: {
    leaseTime: 300,
    delayedProcessing: 60,
    windowBuffer: 30,
    priority: 10
  }
})

// Push messages
const { messageIds } = await client.push({
  items: [{
    ns: 'email-service',
    task: 'notifications',
    queue: 'high-priority',
    payload: { to: 'user@example.com', template: 'welcome' }
  }]
})

// Pop with long polling
const message = await client.pop({
  ns: 'email-service',
  task: 'notifications',
  queue: 'high-priority',
  wait: true,
  timeout: 30000
})

// Process message
try {
  await processMessage(message)
  // Acknowledge successful processing
  await client.ack(message.transactionId, 'completed')
} catch (error) {
  // Acknowledge failure (message may be retried)
  await client.ack(message.transactionId, 'failed', error.message)
}
```

### Client Features
- Automatic retry with exponential backoff
- Connection pooling for HTTP/2
- Long polling support with automatic reconnection
- Batch operations for push/pop
- Promise-based API with async/await support
- Event emitter for real-time updates
- Built-in request/response validation

## Advanced Queue Processing

### Delayed Processing
Messages in queues with `delayedProcessing` are not eligible for processing until they've aged for the specified duration:
```sql
-- Only select messages older than delayedProcessing seconds
WHERE created_at <= NOW() - INTERVAL '60 seconds'
```

### Window Buffer
Queues with `windowBuffer` wait until the newest message in the queue is at least X seconds old before processing any messages:
```sql
-- Check if newest message is old enough
SELECT MAX(created_at) < NOW() - INTERVAL '30 seconds' 
FROM messages 
WHERE queue_id = ? AND status = 'pending'
```

### Lease-Based Processing
Messages are leased to workers for a specific duration. If not acknowledged within the lease time, they become available again:
```sql
-- Reclaim expired leases (runs periodically)
UPDATE queen.messages 
SET status = 'pending', 
    worker_id = NULL, 
    lease_expires_at = NULL,
    retry_count = retry_count + 1
WHERE status = 'processing' 
  AND lease_expires_at < NOW()
  AND retry_count < (
    SELECT (options->>'retryLimit')::int 
    FROM queen.queues 
    WHERE id = queue_id
  );

-- Move to DLQ if retry limit exceeded
UPDATE queen.messages
SET status = 'dead_letter'
WHERE status = 'processing'
  AND lease_expires_at < NOW()
  AND retry_count >= (
    SELECT (options->>'retryLimit')::int 
    FROM queen.queues 
    WHERE id = queue_id
  );
```

### Queue Priority Processing
Higher priority queues are processed first:
```sql
-- Select next message respecting queue priorities
WITH prioritized_queues AS (
  SELECT q.id, q.priority
  FROM queues q
  JOIN messages m ON m.queue_id = q.id
  WHERE m.status = 'pending'
  ORDER BY q.priority DESC, m.created_at
  LIMIT 1
)
SELECT m.* FROM messages m
JOIN prioritized_queues pq ON m.queue_id = pq.id
WHERE m.status = 'pending'
ORDER BY m.created_at
LIMIT 1
FOR UPDATE SKIP LOCKED
```

## Message Durability & Ordering Guarantees

### Message Durability Guarantees
1. **Transactional Writes**: All message inserts are within PostgreSQL transactions
2. **Idempotency**: Transaction IDs prevent duplicate message insertion
3. **Lease-Based Processing**: Messages have lease timeouts - if not acknowledged, they return to pending
4. **Retry Logic**: Failed messages can be retried with configurable limits
5. **Dead Letter Queue**: Messages exceeding retry limits go to DLQ (not lost)
6. **WAL & Replication**: PostgreSQL WAL ensures durability even on crashes

### Message Ordering Guarantees  
1. **FIFO Within Queue**: Messages are strictly ordered by `created_at` within each queue
2. **FOR UPDATE SKIP LOCKED**: Ensures only one worker processes a message
3. **Queue-Level Coordination**: Optional queue locking prevents concurrent processing
4. **Atomic Status Updates**: Message status changes are atomic operations
5. **No Out-of-Order Processing**: Delayed processing and window buffer maintain order

### Failure Scenarios Handled
- **Worker Crash**: Lease expires, message returns to pending
- **Database Connection Loss**: Transactions rollback, no partial state
- **Network Partition**: Client retries with same transactionId (idempotent)
- **Server Restart**: All pending messages preserved in PostgreSQL
- **Duplicate Push**: Transaction ID prevents duplicate insertion
- **Missing ACK**: Lease timeout returns message to queue for retry
- **Explicit NACK**: Client sends failed status, message retried per configuration

## Summary of Key Decisions

1. **Auto-creation**: Resources (ns/task/queue) are auto-created on push if they don't exist, with efficient in-memory caching
2. **Batch behavior**: Batch returns N messages from the same scope, respecting queue priorities
3. **No global pop**: Removed `GET /api/v1/pop` - must specify at least namespace
4. **Queue priority**: Queues have priority (not messages) to maintain FIFO within each queue
5. **No authentication**: Skipped for initial implementation
6. **Functional style**: Factory functions instead of classes throughout
7. **Advanced features**: Support for delayed processing, window buffering, and lease-based message handling
8. **Transaction IDs**: Every message has a transactionId for idempotency and deduplication
9. **WebSocket Support**: Real-time events for dashboard monitoring
10. **Client SDK in /src/client**: JavaScript client with long polling and retry support
11. **Queen Schema**: All PostgreSQL tables under `queen` schema for isolation
