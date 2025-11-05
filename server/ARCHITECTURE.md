# Queen Server Architecture

## Overview

Queen is a high-performance message queue server built with:
- **uWebSockets** for event-driven HTTP/WebSocket handling
- **libpq async API** for non-blocking PostgreSQL operations
- **Event-loop concurrency** for scalable request processing

The architecture is designed for **low latency** (10-50ms), **high throughput** (130K+ msg/s), and **efficient resource utilization** (minimal threads).

---

## Core Architecture

### Three-Layer Design

```
┌─────────────────────────────────────────────────────────┐
│                    NETWORK LAYER                        │
│  Acceptor + Workers (uWebSockets event loops)           │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│                   QUEUE LAYER                           │
│  AsyncQueueManager (PUSH/POP/ACK/TRANSACTION logic)     │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│                  DATABASE LAYER                         │
│  AsyncDbPool (non-blocking PostgreSQL connections)      │
└─────────────────────────────────────────────────────────┘
```

---

## Network Layer

### Acceptor Thread

**Purpose**: Listen for incoming connections and distribute to workers

**Design**:
- Single thread listening on port 6632 (configurable)
- Round-robin distribution to worker threads
- No processing logic (just connection routing)

**Configuration**:
```bash
export PORT=6632
export HOST=0.0.0.0
```

### Worker Threads

**Purpose**: Handle HTTP/WebSocket requests in event loops

**Design**:
- Configurable number of workers (default: 10)
- Each worker runs independent event loop
- Non-blocking operations throughout
- Direct access to AsyncQueueManager

**Configuration**:
```bash
export NUM_WORKERS=10  # Default, capped at CPU cores
```

**Worker Responsibilities**:
1. Parse HTTP requests
2. Call AsyncQueueManager methods
3. Send responses directly (no queuing)
4. Handle WebSocket connections for streaming

---

## Queue Layer

### AsyncQueueManager

**Purpose**: Implement queue semantics and message lifecycle

**Operations**:

#### PUSH Operation
```cpp
std::vector<PushResult> push_messages(const std::vector<PushItem>& items)
```
- Batch duplicate detection using `UNNEST` arrays
- Efficient batch inserts with PostgreSQL arrays
- Dynamic batching based on estimated row size
- Transaction management (BEGIN/COMMIT/ROLLBACK)
- Encryption support
- Failover to file buffer if database unavailable

#### POP Operation
```cpp
PopResult pop_messages_from_partition(...)
PopResult pop_messages_from_queue(...)
PopResult pop_messages_filtered(...)
```
- Lease acquisition and management
- Consumer group tracking
- Subscription modes (earliest/latest/from)
- Window buffer filtering
- Delayed processing (scheduled messages)
- Auto-ack support

#### ACK Operation
```cpp
AckResult acknowledge_message(...)
BatchAckResult acknowledge_messages_batch(...)
```
- Partition ID validation
- Lease validation and release
- Consumer progress tracking
- DLQ (Dead Letter Queue) support
- Batch transaction processing

#### TRANSACTION Operation
```cpp
TransactionResult execute_transaction(const std::vector<json>& operations)
```
- Atomic execution of mixed pop/ack operations
- All-or-nothing semantics
- Transaction ID generation
- Error propagation and rollback

**Key Features**:
- ✅ All operations non-blocking
- ✅ Direct execution in worker threads
- ✅ No thread pool overhead
- ✅ Automatic failover support

---

## Database Layer

### AsyncDbPool

**Purpose**: Provide non-blocking PostgreSQL connections

**Design**:

```cpp
class AsyncDbPool {
    // Connection pool (142 connections)
    std::vector<PGConnPtr> all_connections_;
    
    // Idle connections queue
    std::queue<PGconn*> idle_connections_;
    
    // Thread synchronization
    std::mutex mtx_;
    std::condition_variable cv_;
    
public:
    PooledConnection acquire();  // Get connection
    void release(PGconn* conn);  // Return connection
};
```

**Connection Management**:
- **Pre-allocated**: All connections created at startup
- **Non-blocking**: All connections use `PQsetnonblocking(conn, 1)`
- **RAII-based**: Automatic cleanup with smart pointers
- **Health monitoring**: Automatic reset on connection failure
- **Result draining**: Prevents "another command in progress" errors

**Query Execution**:

```cpp
// 1. Send query (non-blocking)
PQsendQueryParams(conn, sql, params...);

// 2. Wait for socket (OS-level, not thread blocking)
while (PQisBusy(conn)) {
    waitForSocket(conn, true);  // select() waits
    PQconsumeInput(conn);
}

// 3. Get result
PGresult* result = PQgetResult(conn);
```

**Socket-Based Waiting**:
```cpp
void waitForSocket(PGconn* conn, bool for_reading) {
    int sock = PQsocket(conn);
    fd_set input_mask, output_mask;
    
    FD_ZERO(&input_mask);
    FD_ZERO(&output_mask);
    FD_SET(sock, for_reading ? &input_mask : &output_mask);
    
    select(sock + 1, &input_mask, &output_mask, nullptr, nullptr);
}
```

**Benefits**:
- ✅ OS-level waiting (not thread blocking)
- ✅ Thread can be reused while waiting
- ✅ Efficient CPU utilization
- ✅ Scalable concurrency

---

## Background Services

### Poll Workers

**Purpose**: Handle long-polling operations (`wait=true`)

**Design**:
- 4 dedicated threads
- Non-blocking I/O with AsyncDbPool
- Exponential backoff (100ms→2000ms)
- Intention registry for request grouping

**Flow**:
```
1. Client sends POP with wait=true
2. Worker registers intention in registry
3. Poll worker wakes every 50ms
4. Execute non-blocking query via AsyncDbPool
5. If messages found, distribute to waiting client
6. If timeout reached, send 204 No Content
```

**Configuration**:
```bash
export POLL_WORKER_INTERVAL=50     # Initial interval (ms)
export POLL_DB_INTERVAL=100        # DB query interval (ms)
export POLL_MAX_INTERVAL=2000      # Max backoff (ms)
```

### Background Pool

**Purpose**: Handle non-critical operations

**Services**:
- **MetricsCollector**: Collects system and queue metrics
- **RetentionService**: Cleans up expired messages
- **EvictionService**: Handles max wait time eviction
- **StreamManager**: Manages streaming subscriptions

**Configuration**:
- 8 synchronous connections
- Separate from async pool
- Non-blocking for async pool

---

## Request Flow

### Standard Operation (PUSH/POP/ACK/TRANSACTION)

```
1. Client sends HTTP request
        ↓
2. Acceptor receives connection
        ↓
3. Acceptor routes to Worker (round-robin)
        ↓
4. Worker event loop receives request
        ↓
5. Worker parses request
        ↓
6. Worker calls AsyncQueueManager method
        ↓
7. AsyncQueueManager acquires connection from AsyncDbPool
        ↓
8. AsyncQueueManager sends query (non-blocking)
        ↓
9. Socket waits for PostgreSQL response (OS-level)
        ↓
10. AsyncQueueManager gets result
        ↓
11. AsyncQueueManager returns result to Worker
        ↓
12. Worker sends HTTP response to client

Total time: 10-50ms (typical)
```

### Long-Polling Operation (POP with wait=true)

```
1. Client sends POP request with wait=true
        ↓
2. Worker registers intention in registry
        ↓
3. Worker returns (doesn't block)
        ↓
4. Poll Worker wakes every 50ms
        ↓
5. Poll Worker checks intentions for this queue/partition
        ↓
6. Poll Worker executes non-blocking query
        ↓
7. If messages found:
        → Distribute to waiting clients
        → Remove intentions from registry
   
   If no messages:
        → Exponential backoff
        → Check again later
        
   If timeout reached:
        → Send 204 No Content
        → Remove intention from registry
```

---

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
| Batch push | 5-8K msg/s | Batch size 100 |

### Resource Usage

| Resource | Usage | Notes |
|----------|-------|-------|
| Threads | 14 total | 10 workers + 4 poll workers |
| Connections | 150 total | 142 async + 8 background |
| Memory | ~80MB | Thread stacks + connections |
| CPU | 70-80% | Under full load |

---

## Scalability

### Horizontal Scaling

**Multiple server instances**:
```
┌─────────────┐
│ Load        │
│ Balancer    │
└──────┬──────┘
       │
   ┌───┼───┬───────┐
   ↓   ↓   ↓       ↓
Server1 Server2 Server3 ... ServerN
   ↓   ↓   ↓       ↓
   └───┴───┴───────┘
          ↓
    PostgreSQL
```

**Benefits**:
- ✅ Linear scaling of request handling
- ✅ Shared PostgreSQL (no data sharding)
- ✅ Session-less design (any server can handle any request)

### Vertical Scaling

**Increase workers**:
```bash
export NUM_WORKERS=20  # More workers = more parallelism
export DB_POOL_SIZE=300  # Scale pool with workers
```

**Increase connections**:
```bash
export DB_POOL_SIZE=300  # More connections = higher concurrency
```

**Tuning PostgreSQL**:
```ini
max_connections = 400        # Support multiple servers
shared_buffers = 4GB         # 25% of RAM
effective_cache_size = 12GB  # 75% of RAM
```

---

## Failover & Reliability

### File Buffer Failover

**When PostgreSQL is unavailable**:
1. Detect database unavailability (2-30s timeout)
2. Write messages to file buffer
3. Continue accepting push requests (zero downtime)
4. Background processor attempts flush every 100ms
5. When database recovers, replay buffered messages
6. Resume normal operation

**File Structure**:
```
/var/lib/queen/buffers/
├── failover_019a0c11-7fe8.buf.tmp  ← Active write
├── failover_019a0c11-8021.buf      ← Ready to process
├── failover_019a0c11-8054.buf      ← Queued
└── failed/
    └── failover_019a0c11-7abc.buf  ← Retry in 5s
```

**Guarantees**:
- ✅ Zero message loss (even if server crashes)
- ✅ FIFO ordering preserved (within partitions)
- ✅ Duplicate detection (via transaction IDs)
- ✅ Automatic recovery

**Configuration**:
```bash
export FILE_BUFFER_DIR=/var/lib/queen/buffers
export FILE_BUFFER_FLUSH_MS=100
export FILE_BUFFER_MAX_BATCH=100
export FILE_BUFFER_EVENTS_PER_FILE=10000
```

---

## Configuration Summary

### Critical Settings

```bash
# Network
export PORT=6632
export NUM_WORKERS=10

# Database
export DB_POOL_SIZE=150              # Most important!
export PG_HOST=localhost
export PG_PORT=5432
export PG_DB=postgres
export PG_USER=postgres
export PG_PASSWORD=password

# Performance
export BATCH_INSERT_SIZE=2000
export BATCH_PUSH_TARGET_SIZE_MB=4
export BATCH_PUSH_MAX_SIZE_MB=8
export POLL_WORKER_INTERVAL=50
export POLL_DB_INTERVAL=100

# Failover
export FILE_BUFFER_DIR=/var/lib/queen/buffers
export FILE_BUFFER_FLUSH_MS=100
```

See [ENV_VARIABLES.md](ENV_VARIABLES.md) for complete list of 100+ configuration options.

---

## Design Principles

1. **Non-blocking I/O**: All database operations use async API
2. **Event-driven**: Worker threads never block
3. **Zero-copy**: Minimal data copying in hot paths
4. **RAII**: Automatic resource cleanup
5. **Thread-safe**: All shared structures protected
6. **Fail-safe**: Automatic failover to file buffer
7. **Horizontal scalability**: Stateless server design

---

## Further Reading

- [README.md](README.md) - Build and tuning guide
- [ENV_VARIABLES.md](ENV_VARIABLES.md) - All configuration options
- [API.md](API.md) - HTTP API documentation
- [ASYNC_DATABASE_IMPLEMENTATION.md](ASYNC_DATABASE_IMPLEMENTATION.md) - AsyncDbPool details

