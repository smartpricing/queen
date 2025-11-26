# Server Architecture

Queen MQ uses a high-performance **acceptor/worker pattern** with fully asynchronous, non-blocking PostgreSQL architecture for maximum throughput and minimal latency.

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

### 1. Network Layer (uWebSockets)

#### Acceptor Thread
- **Single thread** listening on port 6632
- **Round-robin distribution** of connections to workers
- **Pure routing** - no processing logic
- **Non-blocking** operation

#### Worker Threads
- **10 threads by default** (configurable via `NUM_WORKERS`)
- **Event loop** in each worker
- **Direct execution** of queue operations (no thread pool!)
- **Non-blocking I/O** throughout

**Configuration:**
```bash
export NUM_WORKERS=10
export PORT=6632
```

::: tip Key Difference
Unlike traditional queue systems, Queen executes operations **directly in worker event loops** using non-blocking I/O, eliminating thread pool overhead and context switching.
:::

### 2. Database Layer (AsyncDbPool)

#### Design

The AsyncDbPool provides **non-blocking PostgreSQL connections** using libpq's async API.

```cpp
class AsyncDbPool {
    std::vector<PGConnPtr> all_connections_;     // All connections
    std::queue<PGconn*> idle_connections_;       // Available pool
    std::mutex mtx_;                             // Thread safety
    std::condition_variable cv_;                 // Wait mechanism
};
```

#### Key Features

- **142 non-blocking connections** (95% of pool)
- **Socket-based I/O** with `select()` for efficient waiting
- **RAII-based** resource management
- **Connection health monitoring** with automatic reset
- **Thread-safe** with mutex/condition variables

#### Query Execution Pattern

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

#### Socket-Based Waiting

```cpp
void waitForSocket(PGconn* conn, bool for_reading) {
    int sock = PQsocket(conn);
    fd_set input_mask, output_mask;
    
    FD_ZERO(&input_mask);
    FD_ZERO(&output_mask);
    FD_SET(sock, for_reading ? &input_mask : &output_mask);
    
    // OS-level wait (not thread blocking!)
    select(sock + 1, &input_mask, &output_mask, nullptr, nullptr);
}
```

**Benefits:**
- ✅ OS-level waiting (worker thread can be reused by event loop)
- ✅ Non-blocking I/O throughout
- ✅ Efficient CPU utilization
- ✅ Scalable concurrency

**Configuration:**
```bash
export DB_POOL_SIZE=150  # Total pool (142 async + 8 background)
```

### 3. Queue Layer (AsyncQueueManager)

Implements all message queue operations using the async database pool.

#### Operations Execute Directly in Workers

**No Thread Pool!** Operations run directly in worker event loops:

```
Worker Thread Event Loop
    ↓
AsyncQueueManager.push_messages()
    ↓
AsyncDbPool.acquire() → Non-blocking connection
    ↓
PQsendQueryParams() → Non-blocking send
    ↓
waitForSocket() → OS-level wait (event loop continues)
    ↓
PQgetResult() → Get result
    ↓
Return to client
```

#### Supported Operations

- **PUSH** - Batch insert with duplicate detection
- **POP** - Lease acquisition and message fetching
- **ACK** - Consumer progress tracking
- **TRANSACTION** - Atomic multi-operation execution
- **LEASE RENEWAL** - Extend message locks
- **STREAM** - WebSocket-based streaming

### 4. Background Services

#### Poll Workers (ThreadPool-Managed)

**Purpose:** Handle long-polling operations (`wait=true`)

**Design:**
- **ThreadPool-managed threads** (configurable via `POLL_WORKER_COUNT` and `STREAM_POLL_WORKER_COUNT`)
- **Non-blocking I/O** with AsyncDbPool
- **Exponential backoff** (100ms → 2000ms for regular, 1000ms → 5000ms for streams)
- **Intention registry** for request grouping
- **Centralized resource management** via DB ThreadPool

**Flow:**
```
Client sends POP with wait=true
    ↓
Worker registers PollIntention in registry
    ↓
Worker returns immediately (doesn't block!)
    ↓
Poll Worker wakes every 50ms
    ↓
Groups intentions by (queue, partition, consumer_group)
    ↓
Execute non-blocking pop query via AsyncDbPool
    ↓
If messages found:
    → Send to ResponseQueue
    → Worker delivers to client
If no messages:
    → Apply exponential backoff
    → Check again later
```

**Configuration:**
```bash
# Regular poll workers
export POLL_WORKER_COUNT=2                  # Reserved threads in DB ThreadPool
export POLL_WORKER_INTERVAL=50              # Registry check interval (ms)
export POLL_DB_INTERVAL=100                 # Initial DB query interval (ms)
export QUEUE_MAX_POLL_INTERVAL=2000         # Max backoff interval (ms)

# Stream poll workers  
export STREAM_POLL_WORKER_COUNT=2           # Reserved threads in DB ThreadPool
export STREAM_POLL_WORKER_INTERVAL=100      # Registry check interval (ms)
export STREAM_POLL_INTERVAL=1000            # Initial stream check interval (ms)
export STREAM_CONCURRENT_CHECKS=10          # Max concurrent window checks per worker
export STREAM_MAX_POLL_INTERVAL=5000        # Max backoff interval (ms)

# ThreadPool sizing (auto-calculated)
# DB ThreadPool = 2 + 2 + 20 + 5 = 29 threads
# System ThreadPool = 4 threads (fixed)
```

#### Background Pool (8 Connections)

Separate from async pool, handles non-critical operations:
- **MetricsCollector** - System and queue metrics
- **RetentionService** - Cleanup expired messages
- **EvictionService** - Handle max wait time eviction
- **StreamManager** - Manage streaming subscriptions

### 5. Inter-Instance Communication

In clustered deployments, servers notify each other when messages are pushed or acknowledged, enabling immediate consumer response across all instances.

#### Notification Types

| Event | Notification | Effect |
|-------|--------------|--------|
| PUSH (message queued) | `MESSAGE_AVAILABLE` | Reset backoff for matching poll intentions |
| ACK (partition freed) | `PARTITION_FREE` | Reset backoff for specific consumer group |

#### Protocol Options

Queen supports two protocols that can be used independently or together:

**UDP (Recommended for lowest latency):**
```
Server A: PUSH message
    ↓
sendto(udp_socket, notification, peer_addr)  ← Fire-and-forget (~0.2ms)
    ↓
Server B: recvfrom() → reset_backoff()
    ↓
Consumer wakes immediately
```

**HTTP (Guaranteed delivery):**
```
Server A: PUSH message
    ↓
Queue notification → Batch for 10ms
    ↓
POST /internal/api/notify to each peer  ← ~3ms per peer
    ↓
Server B: Handle notification → reset_backoff()
```

#### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    INTER-INSTANCE COMMS                         │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                  InterInstanceComms                      │   │
│  │   ┌─────────────┐           ┌─────────────┐             │   │
│  │   │ UDP Sender  │           │ HTTP Batch  │             │   │
│  │   │ (immediate) │           │  (10ms)     │             │   │
│  │   └──────┬──────┘           └──────┬──────┘             │   │
│  │          ↓                         ↓                     │   │
│  │   sendto() to peers         POST to /internal/api/notify│   │
│  └─────────────────────────────────────────────────────────┘   │
│                              ↑                                  │
│                    PUSH/ACK triggers                            │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                         PEER SERVERS                            │
│   ┌─────────────┐                    ┌─────────────┐           │
│   │ UDP Recv    │ ──────────────────→│ Poll        │           │
│   │ Thread      │   reset_backoff()  │ Intention   │           │
│   └─────────────┘                    │ Registry    │           │
│   ┌─────────────┐                    │             │           │
│   │ HTTP Route  │ ──────────────────→│             │           │
│   │ /notify     │   reset_backoff()  └─────────────┘           │
│   └─────────────┘                                              │
└─────────────────────────────────────────────────────────────────┘
```

#### Configuration

```bash
# UDP (fire-and-forget, ~0.2ms latency)
export QUEEN_UDP_PEERS="queen-b:6633,queen-c:6633"
export QUEEN_UDP_NOTIFY_PORT=6633

# HTTP (batched, guaranteed delivery, ~3ms latency)
export QUEEN_PEERS="http://queen-b:6632,http://queen-c:6632"
export PEER_NOTIFY_BATCH_MS=10

# Both protocols (recommended for production)
# UDP provides speed, HTTP provides reliability as backup
```

#### Latency Impact

| Scenario | Without Notifications | With UDP Notifications |
|----------|----------------------|------------------------|
| Cross-server message delivery | Up to 2000ms (backoff) | 10-50ms |
| Consumer group rebalance | Up to 2000ms | 10-50ms |

### 6. Failover Layer (File Buffer)

**Zero message loss** when PostgreSQL is unavailable.

**Flow:**
```
Normal Operation:
  PUSH → PostgreSQL → Success

PostgreSQL Down:
  PUSH → Detect failure (2s timeout)
       → Write to file buffer (.buf.tmp)
       → Rotate to .buf when complete
       → Return success to client

Recovery:
  Background processor detects DB available
    → Read oldest .buf file
    → Replay events to PostgreSQL
    → Delete file on success
```

**Configuration:**
```bash
export FILE_BUFFER_DIR=/var/lib/queen/buffers
export FILE_BUFFER_FLUSH_MS=100
export FILE_BUFFER_MAX_BATCH=100
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
4. Worker returns immediately (doesn't block thread!)
        ↓
5. Poll Worker wakes (every 50ms)
        ↓
6. Poll Worker groups intentions
        ↓
7. Poll Worker executes non-blocking POP query via AsyncDbPool
        ↓
8. If messages found:
     → Poll Worker sends to ResponseQueue
     → Worker thread delivers response to client
   
   If no messages:
     → Apply backoff (100ms → 200ms → ... → 2000ms)
     → Check again later
        
   If timeout reached:
     → Send 204 No Content
     → Remove intention
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
3. AsyncQueueManager acquires single connection from pool
        ↓
4. BEGIN transaction
        ↓
5. Execute ACK operation (non-blocking)
   - Validate partition_id and lease
   - Update consumer cursor
        ↓
6. Execute PUSH operation (non-blocking)
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
| Threads | 14-16 total | 10 workers + 2-4 poll + 2 stream |
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
export DB_POOL_SIZE=300  # 285 async + 15 background
```

**Tune PostgreSQL:**
```sql
max_connections = 400
shared_buffers = 4GB
effective_cache_size = 12GB
work_mem = 64MB
```

## Design Principles

1. **Non-blocking I/O** - All database operations use async API
2. **Event-driven** - Worker threads never block on I/O
3. **Zero-copy** - Minimal data copying in hot paths
4. **RAII** - Automatic resource cleanup
5. **Thread-safe** - All shared structures protected
6. **Fail-safe** - Automatic failover to file buffer
7. **Horizontal scalability** - Stateless server design
8. **No thread pools** - Direct execution in event loops

## Key Innovations

### 1. Non-blocking in Event Loops

Unlike traditional queue systems that delegate to thread pools, Queen executes operations **directly in event loop threads** using non-blocking I/O. This eliminates context switching overhead.

### 2. Socket-Based Waiting

Instead of blocking threads, Queen waits on PostgreSQL socket file descriptors using `select()`, allowing the OS to manage I/O efficiently.

### 3. Intention Registry Pattern

Long-polling uses an intention registry that groups similar requests together, reducing database load and enabling efficient backoff strategies.

### 4. Zero-Loss Failover

File buffer provides guaranteed message persistence even during complete database outages, with automatic replay on recovery.

### 5. Unified Transaction Model

Single transaction API supports mixing PUSH and ACK operations, enabling exactly-once processing patterns.

## Architecture Comparison

### Traditional (Thread Pool Based)

```
Worker → Thread Pool → DB Connection
   ↓
Blocks worker while waiting for thread pool
   ↓
Context switches between threads
   ↓
Higher latency, limited scalability
```

### Queen (Async, No Thread Pool)

```
Worker → AsyncQueueManager → AsyncDbPool → PostgreSQL
   ↓                           (non-blocking)
OS-level wait (select on socket)
   ↓
No thread blocking, no context switches
   ↓
Lower latency, better scalability
```

## Configuration Examples

### Balanced (Default)

```bash
export NUM_WORKERS=10
export DB_POOL_SIZE=150
export POLL_WORKER_COUNT=2
export STREAM_POLL_WORKER_COUNT=2
export POLL_WORKER_INTERVAL=50
export POLL_DB_INTERVAL=100
# DB ThreadPool: 2 + 2 + 20 + 5 = 29 threads
```

### High Throughput

```bash
export NUM_WORKERS=20
export DB_POOL_SIZE=300
export POLL_WORKER_COUNT=10
export STREAM_POLL_WORKER_COUNT=4
export STREAM_CONCURRENT_CHECKS=20
export POLL_WORKER_INTERVAL=25
export POLL_DB_INTERVAL=50
export BATCH_INSERT_SIZE=2000
# DB ThreadPool: 10 + 4 + 80 + 5 = 99 threads
```

### Low Latency

```bash
export NUM_WORKERS=10
export DB_POOL_SIZE=150
export POLL_WORKER_COUNT=4
export STREAM_POLL_WORKER_COUNT=2
export POLL_WORKER_INTERVAL=25
export POLL_DB_INTERVAL=50
export RESPONSE_TIMER_INTERVAL_MS=10
# DB ThreadPool: 4 + 2 + 20 + 5 = 31 threads
```

## See Also

- [Configuration Guide](/server/configuration) - Configure the server
- [Environment Variables](/server/environment-variables) - Complete variable reference
- [Performance Tuning](/server/tuning) - Optimize for your workload
- [Deployment](/server/deployment) - Production deployment patterns
- [Complete Architecture Doc](https://github.com/smartpricing/queen/blob/master/documentation/ARCHITECTURE.md) - Extended technical documentation
