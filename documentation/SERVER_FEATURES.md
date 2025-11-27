# Queen Server Features

## Overview

Queen is a feature-rich message queue server built on PostgreSQL with a focus on reliability, performance, and flexibility. This document provides a comprehensive overview of all server features and capabilities.

## Core Features

### 1. Queue Management

#### Multiple Queue Support
- Create unlimited queues with different configurations
- Per-queue settings for lease time, retry limits, priorities
- Namespace and task organization
- Queue deletion and management

**Example:**
```bash
curl -X POST http://localhost:6632/api/v1/configure \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "orders",
    "namespace": "billing",
    "task": "process-payment",
    "options": {
      "leaseTime": 300,
      "retryLimit": 3,
      "priority": 5
    }
  }'
```

#### Queue Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| `leaseTime` | Lease duration in seconds | 300 |
| `maxSize` | Maximum queue size | 10000 |
| `retryLimit` | Max retry attempts before DLQ | 3 |
| `retryDelay` | Delay between retries (ms) | 1000 |
| `priority` | Queue priority (higher = more important) | 0 |
| `delayedProcessing` | Delay before message is available (s) | 0 |
| `windowBuffer` | Time messages wait before available (s) | 0 |
| `retentionSeconds` | Retention for pending messages | 0 (infinite) |
| `completedRetentionSeconds` | Retention for completed messages | 0 (infinite) |
| `encryptionEnabled` | Enable message encryption | false |
| `deadLetterQueue` | Enable DLQ functionality | false |
| `dlqAfterMaxRetries` | Auto-move to DLQ after max retries | false |
| `maxWaitTimeSeconds` | Max wait time before DLQ | 0 (infinite) |

### 2. Partition System

#### Unlimited FIFO Partitions
- Each queue can have unlimited partitions
- FIFO ordering guaranteed within each partition
- Concurrent processing across partitions
- Partition-level locking

**Benefits:**
- Parallel processing of independent message streams
- Ordered processing where needed
- Scale horizontally by adding partitions

**Example:**
```javascript
// Push to different partitions
await queen.queue('orders')
  .partition('customer-123')
  .push([{ data: { order: 1 } }])

await queen.queue('orders')
  .partition('customer-456')
  .push([{ data: { order: 2 } }])

// Messages processed independently and in parallel
```

#### Partition Locking
- Only one consumer can hold lease on a partition at a time
- Prevents duplicate processing
- Automatic lease expiration and reacquisition
- Lease renewal for long-running tasks

### 3. Consumer Groups

#### Kafka-style Consumer Groups
- Multiple consumer groups per queue
- Independent progress tracking per group
- Load balancing within groups
- Bus/pub-sub semantics

**Example:**
```javascript
// Group 1: Email notifications
await queen.queue('user-events')
  .group('email-notifications')
  .consume(async (msg) => {
    await sendEmail(msg.data)
  })

// Group 2: Analytics
await queen.queue('user-events')
  .group('analytics')
  .consume(async (msg) => {
    await recordAnalytics(msg.data)
  })

// Same messages processed by both groups independently
```

#### Queue Mode (Default)
- Special consumer group: `__QUEUE_MODE__`
- RabbitMQ-style competing consumers
- Messages consumed only once
- Round-robin distribution

### 4. Quality of Service (QoS) Levels

#### QoS 0: At-Most-Once (Buffered)
- Server-side batching for performance
- 10-100x throughput improvement
- Fire-and-forget semantics
- Best for high-volume, loss-tolerant scenarios

**Example:**
```bash
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "items": [
      { "queue": "events", "payload": {"type": "login"} }
    ],
    "bufferMs": 100,
    "bufferMax": 100
  }'
```

#### QoS 1: At-Least-Once
- Standard mode with acknowledgments
- Messages redelivered on failure
- No transaction ID required
- Suitable for most use cases

#### QoS 2: Exactly-Once
- Transaction ID-based deduplication
- Idempotent delivery guarantee
- Combined with transactions for end-to-end exactly-once
- Best for critical operations

**Example:**
```javascript
await queen.queue('payments')
  .partition('customer-123')
  .push([{
    transactionId: 'payment-tx-' + orderId,  // Unique ID
    data: { amount: 100, currency: 'USD' }
  }])

// Duplicate pushes with same transactionId are rejected
```

### 5. Subscription Modes

Control where new consumer groups start reading. Subscription modes only apply when a consumer group is **first created** - existing groups continue from their saved position.

#### Server Default (Configurable)

**Default:** Process all messages from the beginning

**Override with environment variable:**
```bash
export DEFAULT_SUBSCRIPTION_MODE="new"  # Skip historical messages by default
./bin/queen-server
```

This is useful for:
- Real-time systems where only new messages matter
- Preventing accidental processing of large backlogs
- Development/staging environments
- Microservices that should only process events after deployment

#### All Messages (Default Client Behavior)

When no subscription mode is specified, consumer groups process all messages from the beginning:

```javascript
await queen.queue('orders')
  .group('analytics-processor')
  // No subscriptionMode = process ALL messages
  .consume(async (msg) => { 
    await updateAnalytics(msg.data)
  })
```

**Best for:** Analytics, data migrations, backfilling, historical replay

#### New Messages Only

Skip all historical messages and only process messages that arrive after subscription:

```javascript
await queen.queue('orders')
  .group('realtime-alerts')
  .subscriptionMode('new')  // Skip history
  .consume(async (msg) => { 
    await sendAlert(msg.data)
  })
```

**Best for:** Real-time notifications, monitoring, alerting

**Aliases:** `.subscriptionMode('new-only')` or `.subscriptionFrom('now')`

#### From Timestamp

Start processing from a specific point in time:

```javascript
await queen.queue('orders')
  .group('recovery-processor')
  .subscriptionFrom('2025-01-01T00:00:00.000Z')  // ISO 8601 timestamp
  .consume(async (msg) => { 
    await reprocessOrder(msg.data)
  })
```

**Best for:** Debugging, recovery scenarios, replaying from specific incidents

### 6. Long Polling

#### Efficient Wait Mechanism
- Server-side blocking for message availability
- Configurable timeout (up to 30s)
- Exponential backoff to reduce load
- Grouped request optimization

**Benefits:**
- Lower latency than polling
- Reduced network traffic
- Lower CPU usage
- Better user experience

**Example:**
```bash
# Wait up to 30 seconds for messages
curl "http://localhost:6632/api/v1/pop/queue/events?wait=true&timeout=30000"
```

See [LONG_POLLING.md](LONG_POLLING.md) for details.

### 7. Transactions

#### Atomic Operations
- Mix PUSH and ACK operations
- All-or-nothing semantics
- Single database transaction
- Exactly-once processing patterns

**Use Cases:**
- Process message and forward to next queue
- Acknowledge message and update state
- Complex multi-step workflows
- State machine transitions

**Example:**
```javascript
// Atomically ACK one message and PUSH another
await queen.transaction()
  .ack(originalMessage)
  .queue('next-step')
  .partition('workflow-1')
  .push([{ data: { processed: true } }])
  .commit()
```

See [TRANSACTIONS.md](TRANSACTIONS.md) for details.

### 8. Dead Letter Queue (DLQ)

#### Automatic Failure Handling
- Messages moved to DLQ after max retries
- Configurable retry limits per queue
- Separate DLQ per queue
- Error message tracking

**Query DLQ:**
```bash
curl "http://localhost:6632/api/v1/dlq?queue=orders&limit=50"
```

**Features:**
- Retry count tracking
- Original timestamp preservation
- Error message storage
- Consumer group isolation

### 9. Message Retention

#### Configurable Retention Policies
- Separate retention for pending and completed messages
- Time-based cleanup
- Automatic eviction service
- Per-queue configuration

**Example:**
```javascript
await queen.queue('logs')
  .config({
    retentionSeconds: 3600,              // 1 hour for pending
    completedRetentionSeconds: 86400     // 24 hours for completed
  })
  .create()
```

**Benefits:**
- Automatic storage management
- Cost optimization
- Compliance with data policies
- Performance improvement

### 10. Streaming

#### Real-time Aggregation
- Time-based windowing
- Aggregated message delivery
- WebSocket connections
- Consumer group support

**Use Cases:**
- Real-time analytics
- Log aggregation
- Event batching
- Metrics collection

See [STREAMS.md](STREAMS.md) for details.

### 11. Encryption

#### Message-Level Encryption
- AES-256 encryption
- Per-queue configuration
- Transparent to clients
- Database-level encryption

**Example:**
```javascript
await queen.queue('sensitive-data')
  .config({
    encryptionEnabled: true
  })
  .create()
```

### 12. Tracing

#### Distributed Tracing Support
- Trace ID propagation
- Timeline visualization
- Message journey tracking
- Workflow debugging

**Example:**
```javascript
await queen.queue('workflow')
  .partition('job-123')
  .push([{
    data: { step: 1 },
    traceId: 'trace-xyz'  // Propagate through workflow
  }])
```

**Webapp Features:**
- Visual trace timeline
- Message relationships
- Latency analysis
- Error tracking

### 13. Lease Management

#### Lease Renewal
- Keep lease alive during long operations
- Configurable renewal interval
- Automatic renewal in clients
- Manual renewal via API

**Example:**
```javascript
// Auto-renew every 2 seconds
await queen.queue('long-tasks')
  .group('workers')
  .renewLease(true, 2000)
  .consume(async (msg) => {
    // Long-running operation
    // Lease automatically renewed
  })
```

**Manual Renewal:**
```bash
curl -X POST http://localhost:6632/api/v1/lease/LEASE_ID/extend \
  -H "Content-Type: application/json" \
  -d '{"seconds": 60}'
```

### 14. Batch Operations

#### Batch Push
- Push multiple messages at once
- Atomic batch insertion
- Duplicate detection across batch
- Dynamic batch sizing

**Example:**
```javascript
await queen.queue('events')
  .partition('batch-1')
  .push([
    { data: { event: 1 } },
    { data: { event: 2 } },
    { data: { event: 3 } },
    // ... up to thousands
  ])
```

#### Batch ACK
- Acknowledge multiple messages at once
- Single transaction for all ACKs
- Partial success handling
- Performance optimization

**Example:**
```javascript
await queen.ackBatch([
  { transactionId: 'tx-1', partitionId: 'p-1', status: 'completed' },
  { transactionId: 'tx-2', partitionId: 'p-2', status: 'completed' },
  { transactionId: 'tx-3', partitionId: 'p-3', status: 'failed' }
])
```

### 15. Auto-Ack

#### Automatic Acknowledgment
- Messages automatically acknowledged on delivery
- Fire-and-forget semantics
- Reduced client complexity
- Better for non-critical workloads

**Example:**
```bash
curl "http://localhost:6632/api/v1/pop/queue/events?autoAck=true&batch=10"
```

### 16. Delayed Processing

#### Schedule Message Delivery
- Delay message availability
- Per-queue or per-message delay
- Use cases: scheduled jobs, rate limiting, retry delays

**Example:**
```javascript
await queen.queue('scheduled-tasks')
  .config({
    delayedProcessing: 300  // 5 minutes
  })
  .create()
```

### 17. Window Buffer

#### Message Batching
- Wait for multiple messages before delivery
- Time-based windowing
- Improves batch processing efficiency
- Configurable per queue

**Example:**
```javascript
await queen.queue('batch-processing')
  .config({
    windowBuffer: 60  // Wait 60 seconds for messages to accumulate
  })
  .create()
```

### 18. Priority Queues

#### Priority-based Processing
- Higher priority queues processed first
- Configurable priority levels
- Queue-level configuration
- Useful for SLA management

**Example:**
```javascript
await queen.queue('critical-alerts')
  .config({ priority: 10 })
  .create()

await queen.queue('background-tasks')
  .config({ priority: 1 })
  .create()
```

### 19. Namespace and Task Organization

#### Logical Grouping
- Organize queues by namespace
- Tag messages by task
- Filter by namespace/task
- Multi-tenancy support

**Example:**
```javascript
await queen.queue('events')
  .config({
    namespace: 'billing',
    task: 'process-invoice'
  })
  .create()

// Pop by namespace
curl "http://localhost:6632/api/v1/pop?namespace=billing&batch=10"
```

### 20. Failover & High Availability

#### Zero-Loss Failover
- Automatic file buffer when PostgreSQL unavailable
- Zero message loss guarantee
- Automatic recovery and replay
- Survives server crashes

See [FAILOVER.md](FAILOVER.md) for details.

### 21. Multi-Server & Distributed Cache (UDPSYNC)

#### Horizontal Scaling with Shared State
- Deploy multiple server instances behind a load balancer
- Distributed cache reduces database queries by 80-90%
- Real-time peer notifications for instant message delivery
- Automatic server health tracking and failover

**Key Features:**
- **Queue Config Cache** - Share queue configurations across servers
- **Consumer Presence** - Track which servers have active consumers (targeted notifications)
- **Partition ID Cache** - Local LRU cache for partition lookups
- **Lease Hints** - Advisory hints about which server holds leases
- **Server Health** - Heartbeat-based dead server detection

**Configuration:**
```bash
# Enable distributed cache by configuring UDP peers
export QUEEN_UDP_PEERS="server2:6634,server3:6634"
export QUEEN_UDP_NOTIFY_PORT=6634

# Optional: Add security for production
export QUEEN_SYNC_SECRET=$(openssl rand -hex 32)
```

**Monitoring:**
- Dashboard shows cache hit rates and peer connectivity
- API endpoint: `GET /api/v1/system/shared-state`

**Correctness Guarantee:** The cache is always advisory. Even with stale data, correctness is never compromised - only extra DB queries occur.

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed multi-server setup guide.

## Advanced Features

### 21. Metrics & Monitoring

#### System Metrics
- CPU, memory, connection usage
- Request throughput and latency
- Queue depth and lag
- Consumer group tracking

**Endpoints:**
```bash
# Health check
curl http://localhost:6632/health

# Metrics
curl http://localhost:6632/metrics

# Dashboard data
curl http://localhost:6632/api/v1/status

# Analytics
curl "http://localhost:6632/api/v1/status/analytics?interval=hour"
```

#### Queue Metrics
- Messages pending, completed, failed
- Throughput (messages/second)
- Average processing time
- DLQ depth

#### Consumer Group Metrics
- Lag per partition
- Total consumed messages
- Last consumed timestamp
- Group state (Stable, Lagging, Dead)

### 22. Web Application

#### Modern Vue 3 Dashboard
- Real-time monitoring
- Queue management
- Message browser
- Trace explorer
- Analytics charts
- Dark/light themes

**Features:**
- Create/delete queues
- View queue statistics
- Browse messages
- Trace message workflows
- Monitor consumer groups
- System health overview

### 23. Resource Management

#### Dynamic Resource Allocation
- Configurable worker threads
- Adjustable connection pool
- Background service tuning
- Memory management
- Auto-scaling thread pools

**Worker Thread Configuration:**
```bash
# Main HTTP workers (handles incoming requests)
export NUM_WORKERS=10              # Number of uWebSockets worker threads

# Queue long-polling workers (handles wait=true requests)
export POLL_WORKER_COUNT=2         # Reserved threads in DB ThreadPool

# Stream long-polling workers (handles stream poll requests)
export STREAM_POLL_WORKER_COUNT=2  # Reserved threads in DB ThreadPool
export STREAM_CONCURRENT_CHECKS=10 # Concurrent window checks per worker
```

**Thread Pool Allocation:**
The system uses two ThreadPools for managed resource allocation:

**DB ThreadPool:** `P + S + (S × C) + T` threads (e.g., 2 + 2 + 20 + 5 = 29 threads by default)
  - P = `POLL_WORKER_COUNT` threads (reserved for regular poll worker loops)
  - S = `STREAM_POLL_WORKER_COUNT` threads (reserved for stream poll worker loops)
  - S × C = Concurrent window check jobs (`STREAM_CONCURRENT_CHECKS` per stream worker)
  - T = `DB_THREAD_POOL_SERVICE_THREADS` threads (background service DB operations)

**System ThreadPool:** 4 threads (metrics sampling, retention scheduling, eviction scheduling)

**HTTP Workers:** 10 workers (uWebSockets event loops, independent of ThreadPools)

**Configurable ThreadPool Settings:**
```bash
export POLL_WORKER_COUNT=2                # Regular poll workers
export STREAM_POLL_WORKER_COUNT=2         # Stream poll workers
export STREAM_CONCURRENT_CHECKS=10        # Max concurrent window checks per stream worker
export DB_THREAD_POOL_SERVICE_THREADS=5   # Threads for service DB operations
```

**Why ThreadPools?**
All long-running worker threads use the ThreadPool for:
- Centralized resource management
- Visibility into thread usage (metrics can report ThreadPool queue depth)
- Proper resource limits and backpressure
- Clean shutdown capabilities
- No orphaned threads

**Database Configuration:**
```bash
export DB_POOL_SIZE=150            # Total async DB connections
export BATCH_INSERT_SIZE=2000      # Batch size for bulk inserts
```

**Background Services:**
```bash
# Metrics collector
export METRICS_SAMPLE_INTERVAL_MS=1000    # Sample every 1s
export METRICS_AGGREGATE_INTERVAL_S=60    # Aggregate and save every 60s

# Retention service
export RETENTION_INTERVAL=300000          # Run every 5 minutes
export RETENTION_BATCH_SIZE=1000          # Process 1000 messages per run

# Eviction service
export EVICTION_INTERVAL=60000            # Run every 1 minute
export EVICTION_BATCH_SIZE=1000           # Process 1000 messages per run
```

### 24. Authentication & Security

#### Proxy Server
- JWT-based authentication
- User management
- bcrypt password hashing
- CORS support
- Rate limiting ready

**Deployment:**
```bash
cd proxy
npm install
npm start
```

### 25. Multi-Language Clients

#### Official Clients
- **JavaScript/Node.js**: Full-featured client with fluent API
- **C++**: High-performance client for C++ applications
- **HTTP**: Direct HTTP API access from any language

**Planned:**
- Python client
- Go client
- Java client

## Performance Features

### 26. Connection Pooling

#### AsyncDbPool
- 142 pre-allocated non-blocking connections
- RAII-based resource management
- Automatic connection health monitoring
- Thread-safe with mutex protection

### 27. Non-Blocking I/O

#### Event-Loop Architecture
- All database operations non-blocking
- Socket-based waiting with select()
- No thread pool overhead for main operations
- Efficient CPU utilization

### 28. Batch Optimization

#### Dynamic Batching
- Automatic batch size adjustment
- Target size in MB (configurable)
- Prevents oversized transactions
- Optimizes throughput

### 29. Query Optimization

#### Efficient SQL
- UNNEST for batch operations
- Index-optimized queries
- Prepared statement caching
- Connection reuse

### 30. Exponential Backoff

#### Load Reduction
- Backoff for empty long-polling
- Per-group backoff state
- Configurable thresholds
- Prevents database overload

## Operational Features

### 31. Health Checks

#### Comprehensive Health Endpoints
- Database connectivity
- Connection pool status
- Worker thread status
- File buffer status

**Example:**
```bash
curl http://localhost:6632/health
```

**Response:**
```json
{
  "status": "healthy",
  "database": "connected",
  "server": "C++ Queen Server",
  "worker_id": 0,
  "version": "1.0.0"
}
```

### 32. Logging

#### Structured Logging
- spdlog-based logging
- Configurable log levels
- Per-component logging
- JSON log format support

**Configuration:**
```bash
export LOG_LEVEL=info  # debug, info, warn, error
```

### 33. Graceful Shutdown

#### Clean Termination
- Drains in-flight requests
- Closes connections properly
- Saves file buffer state
- No message loss on shutdown

### 34. Hot Reload Configuration

#### Runtime Configuration
- Environment variable based
- No restart required for some settings
- Override defaults easily
- Container-friendly

### 35. Docker & Kubernetes Support

#### Container Deployment
- Official Dockerfile
- Kubernetes manifests
- Helm charts
- Health probes configured

**Example:**
```bash
./build.sh  # Build Docker image
kubectl apply -f server/k8s-example.yaml
```

## Reliability Features

### 36. Duplicate Detection

#### Transaction ID Deduplication
- Prevents duplicate message insertion
- UNNEST array-based checking
- Per-partition uniqueness
- Configurable behavior

### 37. Retry Mechanism

#### Automatic Retries
- Configurable retry limit
- Exponential backoff
- Error tracking
- DLQ after max retries

### 38. Lease Expiration

#### Automatic Recovery
- Expired leases auto-released
- Messages become available again
- Prevents stuck messages
- Configurable lease time

### 39. Connection Recovery

#### Automatic Reconnection
- Database connection health monitoring
- Automatic connection reset
- Pool replenishment
- Graceful degradation

### 40. Data Integrity

#### ACID Guarantees
- PostgreSQL transactions
- Atomic operations
- Consistent state
- Durable writes

## Developer Features

### 41. Fluent API

#### Intuitive Client Interface
- Method chaining
- Readable code
- Type safety (TypeScript)
- Auto-completion support

**Example:**
```javascript
await queen
  .queue('tasks')
  .partition('worker-1')
  .group('processors')
  .autoAck(false)
  .batch(10)
  .renewLease(true)
  .consume(async (msg) => { /* ... */ })
  .onSuccess(async (msg) => await queen.ack(msg))
  .onError(async (msg, err) => await queen.nack(msg))
```

### 42. Event Callbacks

#### Hook into Message Lifecycle
- onSuccess, onError, onDuplicate
- Per-operation callbacks
- Async/await support
- Error propagation

### 43. Testing Support

#### Test-Friendly Design
- Deterministic behavior
- Queue isolation
- Easy cleanup
- Mock-friendly

### 44. Debugging Tools

#### Development Aid
- Trace visualization
- Message inspection
- Queue statistics
- Log aggregation

### 45. Documentation

#### Comprehensive Guides
- API reference
- Architecture documentation
- Feature guides
- Code examples

## Summary

Queen MQ provides a comprehensive feature set for building reliable, high-performance message-based systems:

- ✅ **Reliability**: Zero message loss, exactly-once delivery, automatic failover
- ✅ **Performance**: 130K+ msg/s, non-blocking I/O, efficient batching
- ✅ **Flexibility**: Multiple patterns (queue, pub-sub, streaming), QoS levels
- ✅ **Scalability**: Horizontal scaling, unlimited partitions, consumer groups
- ✅ **Observability**: Metrics, tracing, web dashboard, structured logging
- ✅ **Developer Experience**: Fluent API, comprehensive docs, multi-language support
- ✅ **Operations**: Docker/K8s ready, health checks, graceful shutdown

## Further Reading

- [Architecture](ARCHITECTURE.md) - System architecture overview
- [Long Polling](LONG_POLLING.md) - Long polling deep dive
- [ACK System](ACK.md) - Acknowledgment mechanisms
- [Transactions](TRANSACTIONS.md) - Transaction support
- [Streams](STREAMS.md) - Streaming capabilities
- [Failover](FAILOVER.md) - Failover mechanisms
- [Push Operations](PUSH.md) - Push operation details
- [API Reference](../server/API.md) - HTTP API documentation

