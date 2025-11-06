# Queen MQ Architecture Overview

## Introduction

Queen MQ is a high-performance, PostgreSQL-backed message queue system designed for low latency, high throughput, and reliable message delivery. This document provides a comprehensive overview of the system's architecture, design principles, and how different components work together.

## System Overview

```
┌────────────────────────────────────────────────────────────────┐
│                        CLIENT LAYER                            │
│  JavaScript Client, C++ Client, HTTP/WebSocket Direct Access   │
└────────────────────────────────────────────────────────────────┘
                              ↓ HTTP/WebSocket
┌────────────────────────────────────────────────────────────────┐
│                     NETWORK LAYER (uWebSockets)                │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐                  │
│  │ Acceptor │──→│ Worker 1 │   │ Worker N │  (Event Loops)   │
│  └──────────┘   └──────────┘   └──────────┘                  │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                      QUEUE LAYER                               │
│         AsyncQueueManager (Non-blocking Operations)            │
│    PUSH │ POP │ ACK │ TRANSACTION │ STREAM │ LEASE             │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                    DATABASE LAYER                              │
│  ┌──────────────────────────────────────────────────────┐     │
│  │  AsyncDbPool (142 non-blocking connections)          │     │
│  │  Socket-based I/O with select()                      │     │
│  └──────────────────────────────────────────────────────┘     │
│                            ↓                                   │
│                    PostgreSQL Database                         │
│           (Messages, Partitions, Consumer Groups, DLQ)         │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                  BACKGROUND SERVICES                           │
│  Poll Workers │ Metrics │ Retention │ Eviction │ Streams       │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                    FAILOVER LAYER                              │
│          File Buffer (Zero message loss on DB failure)         │
└────────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Network Layer

The network layer is built on **uWebSockets**, a high-performance HTTP/WebSocket library.

#### Acceptor Thread
- **Purpose**: Single point of entry for all client connections
- **Responsibilities**:
  - Listen on configured port (default: 6632)
  - Accept incoming connections
  - Distribute connections to worker threads using round-robin
  - No processing logic (pure routing)
- **Threading**: Single thread, non-blocking

#### Worker Threads
- **Purpose**: Handle HTTP requests and WebSocket connections in event loops
- **Count**: Configurable (default: 10 workers)
- **Responsibilities**:
  - Parse HTTP requests
  - Route to appropriate handlers
  - Execute queue operations directly (non-blocking)
  - Send responses
  - Manage WebSocket connections for streaming
- **Threading**: Each worker runs independent event loop
- **Operations**: All operations execute directly in worker thread (no thread pool delegation for standard operations)

**Configuration:**
```bash
export PORT=6632
export NUM_WORKERS=10
```

### 2. Queue Layer

The queue layer implements message queue semantics through **AsyncQueueManager**.

#### Operations

##### PUSH
- Batch duplicate detection using PostgreSQL arrays
- Dynamic batching based on estimated row size
- Transaction management (BEGIN/COMMIT/ROLLBACK)
- Encryption support (optional)
- Failover to file buffer if database unavailable
- QoS 0 (buffered) and QoS 1 (immediate) modes

**Flow:**
```
Client → Worker → AsyncQueueManager.push_messages()
  ↓
Check duplicates (UNNEST array query)
  ↓
Batch insert to PostgreSQL
  ↓
If DB fails → Write to file buffer
  ↓
Return results to client
```

##### POP
- Lease acquisition and management
- Consumer group tracking
- Subscription modes (earliest, latest, from timestamp)
- Window buffer filtering
- Delayed processing
- Auto-ack support
- Long polling support

**Flow:**
```
Client → Worker → AsyncQueueManager.pop_messages()
  ↓
Check consumer position
  ↓
Acquire lease on partition
  ↓
Fetch messages with filters
  ↓
Return messages with lease info
```

##### ACK
- Partition ID validation (mandatory)
- Lease validation and release
- Consumer progress tracking
- DLQ (Dead Letter Queue) support
- Batch transaction processing

**Flow:**
```
Client → Worker → AsyncQueueManager.acknowledge_message()
  ↓
Validate partition_id and lease
  ↓
Update consumer cursor
  ↓
Release lease if batch complete
  ↓
Move to DLQ if max retries exceeded
```

##### TRANSACTION
- Atomic execution of mixed operations
- All-or-nothing semantics
- Transaction ID generation
- Error propagation and rollback
- Supports: PUSH + ACK combinations

**Flow:**
```
Client → Worker → AsyncQueueManager.execute_transaction()
  ↓
BEGIN transaction
  ↓
Execute each operation in sequence
  ↓
If all succeed → COMMIT
If any fails → ROLLBACK
  ↓
Return combined results
```

### 3. Database Layer

#### AsyncDbPool

The AsyncDbPool provides non-blocking PostgreSQL connections using libpq's async API.

**Design:**
```cpp
class AsyncDbPool {
    std::vector<PGConnPtr> all_connections_;     // All connections
    std::queue<PGconn*> idle_connections_;       // Available pool
    std::mutex mtx_;                             // Thread safety
    std::condition_variable cv_;                 // Wait mechanism
};
```

**Connection Management:**
- **Pre-allocated**: All 142 connections created at startup
- **Non-blocking**: All connections use `PQsetnonblocking(conn, 1)`
- **RAII-based**: Automatic cleanup with smart pointers
- **Health monitoring**: Automatic reset on failure
- **Result draining**: Prevents "another command in progress" errors

**Query Execution Pattern:**
```cpp
// 1. Send query (non-blocking)
PQsendQueryParams(conn, sql, params...);

// 2. Wait for socket (OS-level)
while (PQisBusy(conn)) {
    waitForSocket(conn, true);  // select() waits
    PQconsumeInput(conn);
}

// 3. Get result
PGresult* result = PQgetResult(conn);
```

**Socket-Based Waiting:**
```cpp
void waitForSocket(PGconn* conn, bool for_reading) {
    int sock = PQsocket(conn);
    fd_set input_mask, output_mask;
    
    FD_ZERO(&input_mask);
    FD_ZERO(&output_mask);
    FD_SET(sock, for_reading ? &input_mask : &output_mask);
    
    // OS-level wait (not thread blocking)
    select(sock + 1, &input_mask, &output_mask, nullptr, nullptr);
}
```

**Benefits:**
- ✅ OS-level waiting (thread can be reused)
- ✅ Non-blocking I/O
- ✅ Efficient CPU utilization
- ✅ Scalable concurrency

**Configuration:**
```bash
export DB_POOL_SIZE=150
export PG_HOST=localhost
export PG_PORT=5432
export PG_DB=postgres
export PG_USER=postgres
export PG_PASSWORD=password
```

### 4. Background Services

#### Poll Workers

**Purpose**: Handle long-polling operations for POP requests with `wait=true`

**Design:**
- 2-4 dedicated worker threads
- Non-blocking I/O with AsyncDbPool
- Exponential backoff (100ms → 2000ms)
- Intention registry for request grouping

**Flow:**
```
Client sends POP with wait=true
  ↓
Worker registers PollIntention in registry
  ↓
Worker returns immediately (doesn't block)
  ↓
Poll Worker wakes every 50ms
  ↓
Check grouped intentions
  ↓
Execute non-blocking query
  ↓
If messages found:
  → Distribute to waiting clients via ResponseQueue
  → Remove intentions from registry
If no messages:
  → Apply exponential backoff
  → Check again later
If timeout reached:
  → Send 204 No Content
  → Remove intention
```

**Grouping Strategy:**
- Intentions grouped by (queue, partition, consumer_group)
- One query per group (efficient batching)
- Load balancing across poll workers
- Per-group backoff state

**Configuration:**
```bash
export POLL_WORKER_INTERVAL=50     # Registry check interval
export POLL_DB_INTERVAL=100        # DB query interval
export POLL_MAX_INTERVAL=2000      # Max backoff
```

#### Background Pool

**Purpose**: Handle non-critical operations separately from main pool

**Services:**
- **MetricsCollector**: System and queue metrics
- **RetentionService**: Cleans up expired messages
- **EvictionService**: Handles max wait time eviction
- **StreamManager**: Manages streaming subscriptions

**Configuration:**
- 8 synchronous connections
- Separate from async pool
- Non-blocking for main operations

### 5. Failover Layer

#### File Buffer Manager

**Purpose**: Zero message loss when PostgreSQL is unavailable

**Flow:**
```
Normal Operation:
  PUSH → PostgreSQL → Success

PostgreSQL Down:
  PUSH → Detect failure (timeout)
       → Write to file buffer (.buf.tmp)
       → Rotate to .buf when complete
       → Return success to client

Recovery:
  Background processor detects DB available
    → Read oldest .buf file
    → Replay events to PostgreSQL
    → Delete file on success
    → Continue with next file
```

**File Structure:**
```
/var/lib/queen/buffers/
├── failover_019a0c11-7fe8.buf.tmp  ← Active write
├── failover_019a0c11-8021.buf      ← Ready to process
├── failover_019a0c11-8054.buf      ← Queued
└── failed/
    └── failover_019a0c11-7abc.buf  ← Retry in 5s
```

**Guarantees:**
- ✅ Zero message loss (even if server crashes)
- ✅ FIFO ordering preserved (within partitions, single server)
- ✅ Duplicate detection (via transaction IDs)
- ✅ Automatic recovery

**Configuration:**
```bash
export FILE_BUFFER_DIR=/var/lib/queen/buffers
export FILE_BUFFER_FLUSH_MS=100
export FILE_BUFFER_MAX_BATCH=100
export FILE_BUFFER_EVENTS_PER_FILE=10000
```

## Request Flow Examples

### Standard PUSH Operation

```
1. Client sends HTTP POST to /api/v1/push
        ↓
2. Acceptor receives connection
        ↓
3. Acceptor routes to Worker (round-robin)
        ↓
4. Worker event loop receives request
        ↓
5. Worker parses JSON body
        ↓
6. Worker calls AsyncQueueManager.push_messages()
        ↓
7. AsyncQueueManager acquires connection from pool
        ↓
8. AsyncQueueManager sends non-blocking query
        ↓
9. Socket waits for PostgreSQL response (OS-level)
        ↓
10. AsyncQueueManager gets result
        ↓
11. Worker sends HTTP response
        ↓
12. Client receives confirmation

Total time: 10-50ms (typical)
```

### Long-Polling POP Operation

```
1. Client sends GET to /api/v1/pop?wait=true&timeout=30000
        ↓
2. Worker receives request
        ↓
3. Worker registers PollIntention in registry
        ↓
4. Worker returns immediately (doesn't block thread)
        ↓
5. Poll Worker wakes (every 50ms)
        ↓
6. Poll Worker groups intentions by (queue, partition, group)
        ↓
7. Poll Worker executes non-blocking POP query
        ↓
8. If messages found:
     → Poll Worker sends to ResponseQueue
     → Worker thread delivers response to client
     → Intention removed
   
   If no messages:
     → Apply backoff (100ms → 200ms → ... → 2000ms)
     → Check again later
        
   If timeout reached:
     → Send 204 No Content
     → Intention removed
```

### Transaction Operation (ACK + PUSH)

```
1. Client sends POST to /api/v1/transaction
   Body: [
     { "type": "ack", "transactionId": "tx-1", "partitionId": "..." },
     { "type": "push", "items": [...] }
   ]
        ↓
2. Worker calls AsyncQueueManager.execute_transaction()
        ↓
3. AsyncQueueManager acquires single connection
        ↓
4. BEGIN transaction
        ↓
5. Execute ACK operation
   - Validate partition_id and lease
   - Update consumer cursor
        ↓
6. Execute PUSH operation
   - Check duplicates
   - Insert messages
        ↓
7. COMMIT transaction (if all succeeded)
   OR ROLLBACK (if any failed)
        ↓
8. Return combined results
        ↓
9. Client receives atomicity guarantee

Total time: 50-200ms (typical)
```

## Data Model

### Core Tables

#### messages
```sql
CREATE TABLE messages (
    id UUID PRIMARY KEY,
    queue_name VARCHAR NOT NULL,
    partition_name VARCHAR NOT NULL,
    partition_id UUID NOT NULL,
    transaction_id VARCHAR NOT NULL,
    trace_id UUID,
    namespace VARCHAR,
    task VARCHAR,
    payload JSONB NOT NULL,
    status VARCHAR NOT NULL,
    sequence BIGINT NOT NULL,
    retry_count INT DEFAULT 0,
    created_at TIMESTAMP NOT NULL,
    processed_at TIMESTAMP
);
```

#### partition_consumers
```sql
CREATE TABLE partition_consumers (
    partition_id UUID NOT NULL,
    consumer_group VARCHAR NOT NULL,
    last_consumed_id UUID,
    last_consumed_created_at TIMESTAMP,
    last_consumed_at TIMESTAMP,
    lease_expires_at TIMESTAMP,
    lease_acquired_at TIMESTAMP,
    worker_id VARCHAR,
    batch_size INT DEFAULT 0,
    acked_count INT DEFAULT 0,
    subscription_mode VARCHAR DEFAULT 'latest',
    subscription_from TIMESTAMP,
    total_messages_consumed BIGINT DEFAULT 0,
    PRIMARY KEY (partition_id, consumer_group)
);
```

#### dead_letter_queue
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

## Performance Characteristics

### Latency

| Operation | Latency | Notes |
|-----------|---------|-------|
| POP (immediate) | 10-50ms | No wait, direct query |
| POP (long-poll) | 50ms-30s | Configurable timeout |
| ACK (single) | 10-50ms | Direct query |
| ACK (batch) | 20-80ms | Batch transaction |
| TRANSACTION | 50-200ms | Multiple operations |
| PUSH (single) | 15-50ms | With duplicate check |
| PUSH (batch) | 20-80ms | Dynamic batching |

### Throughput

| Metric | Value | Configuration |
|--------|-------|---------------|
| Sustained | 130K+ msg/s | Batch size 1000 |
| Peak | 148K+ msg/s | Batch size 10000 |
| Single message | 1-2K msg/s | No batching |

### Resource Usage

| Resource | Usage | Notes |
|----------|-------|-------|
| Threads | 14-16 total | 10 workers + 2-4 poll workers + 2 stream poll workers |
| DB Connections | 150 total | 142 async + 8 background |
| Memory | ~80MB | Thread stacks + connections |
| CPU | 70-80% | Under full load |

## Scalability

### Horizontal Scaling

Deploy multiple server instances behind a load balancer:

```
┌─────────────┐
│    Load     │
│  Balancer   │
└──────┬──────┘
       │
   ┌───┼────┬────────┐
   ↓   ↓    ↓        ↓
Server1 Server2 Server3 ... ServerN
   ↓   ↓    ↓        ↓
   └───┴────┴────────┘
          ↓
    PostgreSQL
```

**Benefits:**
- ✅ Linear scaling of request handling
- ✅ Shared PostgreSQL (no data sharding needed)
- ✅ Session-less design (any server handles any request)
- ✅ Built-in failover (server dies, others continue)

**Considerations:**
- FIFO ordering guaranteed per partition per server
- For strict global ordering, use single server or partition coordination

### Vertical Scaling

**Increase Workers:**
```bash
export NUM_WORKERS=20
export DB_POOL_SIZE=300  # Scale pool with workers
```

**Increase Connections:**
```bash
export DB_POOL_SIZE=300
```

**Tune PostgreSQL:**
```sql
max_connections = 400
shared_buffers = 4GB
effective_cache_size = 12GB
work_mem = 64MB
maintenance_work_mem = 256MB
```

## Design Principles

1. **Non-blocking I/O**: All database operations use async API
2. **Event-driven**: Worker threads never block on I/O
3. **Zero-copy**: Minimal data copying in hot paths
4. **RAII**: Automatic resource cleanup
5. **Thread-safe**: All shared structures protected
6. **Fail-safe**: Automatic failover to file buffer
7. **Horizontal scalability**: Stateless server design
8. **Separation of concerns**: Clean layer boundaries

## Key Innovations

### 1. Non-blocking in Event Loops
Unlike traditional queue systems that delegate to thread pools, Queen executes operations directly in event loop threads using non-blocking I/O. This eliminates context switching overhead.

### 2. Socket-Based Waiting
Instead of blocking threads, Queen waits on PostgreSQL socket file descriptors using `select()`, allowing the OS to manage I/O efficiently.

### 3. Intention Registry Pattern
Long-polling uses an intention registry that groups similar requests together, reducing database load and enabling efficient backoff strategies.

### 4. Zero-Loss Failover
File buffer provides guaranteed message persistence even during complete database outages, with automatic replay on recovery.

### 5. Unified Transaction Model
Single transaction API supports mixing PUSH and ACK operations, enabling exactly-once processing patterns.

## Security Considerations

### Encryption
- Optional message encryption at database level
- AES-256 encryption for sensitive payloads
- Per-queue encryption configuration

### Authentication
- Proxy server provides JWT-based authentication
- User management with bcrypt password hashing
- CORS support for web clients

### Network
- HTTPS support via proxy
- WebSocket secure connections
- Configurable CORS policies

## Monitoring & Observability

### Metrics
- System metrics (CPU, memory, connections)
- Queue metrics (depth, throughput, latency)
- Consumer group lag tracking
- DLQ monitoring

### Tracing
- Trace ID propagation through workflows
- Timeline visualization in webapp
- Message journey tracking

### Logging
- Structured logging with spdlog
- Configurable log levels
- Per-component logging

## Future Enhancements

- [ ] Python client library
- [ ] Enhanced concurrency in clients
- [ ] Multi-region replication
- [ ] Message compression
- [ ] Schema validation
- [ ] Rate limiting per queue
- [ ] Priority queues enhancement
- [ ] Message routing rules

## Further Reading

- [Server Features](SERVER_FEATURES.md) - Complete feature list
- [Long Polling](LONG_POLLING.md) - Long polling deep dive
- [ACK System](ACK.md) - Acknowledgment mechanisms
- [Transactions](TRANSACTIONS.md) - Transaction support
- [Streams](STREAMS.md) - Streaming capabilities
- [Failover](FAILOVER.md) - Failover mechanisms
- [Push Operations](PUSH.md) - Push operation details
- [API Reference](../server/API.md) - HTTP API documentation
- [Server Setup](../server/README.md) - Installation and configuration

