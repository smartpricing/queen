# Server Architecture

Queen MQ uses a high-performance **acceptor/worker pattern** with fully asynchronous, non-blocking PostgreSQL architecture for maximum throughput and minimal latency.

## System Overview

```
┌────────────────────────────────────────────────────────────────┐
│                        CLIENT LAYER                            │
│  JavaScript Client, Python Client, C++ Client, HTTP Direct     │
└────────────────────────────────────────────────────────────────┘
                              ↓ HTTP
┌────────────────────────────────────────────────────────────────┐
│                     NETWORK LAYER (uWebSockets)                │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐                  │
│  │ Acceptor │──→│ Worker 1 │   │ Worker N │  (Event Loops)   │
│  └──────────┘   └──────────┘   └──────────┘                  │
│                      │              │                          │
│                 ┌────┴────┐   ┌────┴────┐                     │
│                 │ Sidecar │   │ Sidecar │  (libuv + libpq)    │
│                 └─────────┘   └─────────┘                     │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                   PRIMARY OPERATIONS (Sidecar)                 │
│              PUSH │ POP │ ACK │ TRANSACTION │ LEASE            │
│         High-performance path via libuv + async libpq          │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│              SECONDARY OPERATIONS (AsyncQueueManager)          │
│     Schema Init │ Queue Config │ Tracing │ Consumer Groups     │
│              Maintenance Mode │ File Buffer Replay             │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                    DATABASE LAYER                              │
│  ┌──────────────────────────────────────────────────────┐     │
│  │  Per-worker Sidecars (libuv event loops + libpq)     │     │
│  │  + AsyncDbPool (for secondary operations)            │     │
│  └──────────────────────────────────────────────────────┘     │
│                            ↓                                   │
│              PostgreSQL (Stored Procedures)                    │
│   push_messages_v2 │ pop_unified_batch │ ack_messages_v2       │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                  BACKGROUND SERVICES                           │
│              Metrics │ Retention │ Eviction                    │
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
- **2 threads by default** (configurable via `NUM_WORKERS`)
- **Event loop** in each worker (uWebSockets + libuv)
- **Per-worker sidecar** for async PostgreSQL operations
- **Non-blocking I/O** throughout

**Configuration:**
```bash
export NUM_WORKERS=2
export PORT=6632
```

### 2. Sidecar Pattern (libuv + libpq)

Each worker has a dedicated **sidecar** that handles all PostgreSQL operations asynchronously using libuv. The sidecar is the heart of Queen's high-performance database layer.

```
┌─────────────────────────────────────────────────────────────┐
│                    Worker Thread                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │               uWS Event Loop                          │   │
│  │   HTTP Request → Parse → Queue to Sidecar → Wait      │   │
│  └──────────────────────────────────────────────────────┘   │
│                           ↕ uv_async                         │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                  Sidecar (libuv)                      │   │
│  │   ┌─────────┐  ┌─────────┐  ┌─────────────────────┐  │   │
│  │   │  Timer  │  │  Async  │  │   Poll Handles      │  │   │
│  │   │ (batch) │  │ (wake)  │  │  (PG sockets)       │  │   │
│  │   └─────────┘  └─────────┘  └─────────────────────┘  │   │
│  │                       ↓                               │   │
│  │               PostgreSQL Connections                  │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

#### libuv Components

| Component | Purpose | How It's Used |
|-----------|---------|---------------|
| `uv_poll` | Socket monitoring | Watch PostgreSQL connection file descriptors for read/write events |
| `uv_timer` (batch) | Micro-batching | Accumulate requests for 5ms before sending to maximize throughput |
| `uv_timer` (waiting) | Long-polling | Check for messages at configurable intervals with exponential backoff |
| `uv_async` | Cross-thread wakeup | Signal sidecar when HTTP thread receives a request |

#### Operation-Specific Behavior

The sidecar treats different operations differently for optimal performance:

| Operation | Signal Behavior | Why |
|-----------|-----------------|-----|
| **PUSH** | Buffer only (no immediate wake) | Allows maximum batching for throughput |
| **POP** | Immediate wake + drain | Latency-sensitive, needs fast response |
| **ACK** | Immediate wake + drain | Consumers waiting for acknowledgment |
| **POP_WAIT** | Goes to waiting queue | Separate timer-based polling |

**PUSH Optimization:** When a PUSH request arrives, it's added to the pending queue but does *not* wake up the sidecar immediately. This allows multiple PUSH requests to accumulate and be batched together when the batch timer fires, significantly improving throughput.

**Configuration:**
```bash
export SIDECAR_POOL_SIZE=50              # Total sidecar connections (split among workers)
export SIDECAR_MICRO_BATCH_WAIT_MS=5     # Batching window
export SIDECAR_MAX_ITEMS_PER_TX=1000     # Max items per transaction

# POP_WAIT backoff (long-polling)
export POP_WAIT_INITIAL_INTERVAL_MS=100  # Initial poll interval
export POP_WAIT_BACKOFF_THRESHOLD=3      # Empty checks before backoff
export POP_WAIT_BACKOFF_MULTIPLIER=2.0   # Exponential multiplier
export POP_WAIT_MAX_INTERVAL_MS=1000     # Max interval after backoff
```

### 3. Unified POP Batch (Single Round-Trip)

POP operations use a **unified batch procedure** that handles lease acquisition, message fetching, and cleanup in a single database call. This eliminates multiple round-trips and simplifies the architecture.

#### How It Works

```
POP requests arrive
        ↓
    Sidecar combines into JSON array
        ↓
    1 SQL call: SELECT queen.pop_unified_batch($1::jsonb)
        ↓
    PostgreSQL handles in single transaction:
      - Acquire leases (SKIP LOCKED for wildcards)
      - Fetch messages after cursor position
      - Release empty leases automatically
        ↓
    Return results as JSON
```

**Key features:**
- **Single round-trip**: Lease + fetch + cleanup in one call
- **SKIP LOCKED**: Wildcard POPs don't block each other
- **Automatic cleanup**: Empty leases released immediately
- **Deadlock-safe**: Consistent ordering prevents deadlocks

#### Performance

| Metric | Value |
|--------|-------|
| POP throughput | 50,000+ ops/s |
| p50 latency | 3-10ms |
| p99 latency | 15-30ms |
| Connection utilization | 80%+ |

### 4. Database Layer (AsyncDbPool)

The AsyncDbPool provides **non-blocking PostgreSQL connections** for background services.

**Key Features:**
- **95% of DB_POOL_SIZE** for async operations
- **Socket-based I/O** with non-blocking libpq
- **RAII-based** resource management
- **Connection health monitoring** with automatic reset

**Configuration:**
```bash
export DB_POOL_SIZE=150          # Total connections (95% = 142 async)
export DB_IDLE_TIMEOUT=30000     # Idle timeout (ms)
export DB_STATEMENT_TIMEOUT=30000 # Query timeout (ms)
```

### 5. AsyncQueueManager (Secondary Operations)

Handles administrative and secondary operations that don't need the high-performance sidecar path.

**Operations:**
- **Schema initialization** - Database setup and migrations
- **Queue configuration** - Create/update queue settings
- **Consumer group management** - Subscription metadata, group deletion
- **Message tracing** - Record and query traces
- **Maintenance mode** - Toggle and status
- **File buffer replay** - Internal push operations

**Primary operations (PUSH, POP, ACK)** go through the Sidecar for maximum performance.

### 6. Background Services

Queen runs three background services for housekeeping:

#### MetricsCollector
- **System metrics** - CPU, memory, connections
- **Queue metrics** - Depths, throughput, latencies
- **Sampling** - 1 second sample interval, 60 second aggregation
- **Storage** - PostgreSQL `queen.metrics_history` table

#### RetentionService
- **Message cleanup** - Delete expired messages (TTL-based)
- **Partition cleanup** - Remove empty partitions older than N days
- **Metrics cleanup** - Prune old metrics history
- **Batch processing** - Configurable batch size to avoid long locks

#### EvictionService
- **Max wait time** - Evict messages exceeding `max_wait_time_seconds`
- **Periodic check** - Every 60 seconds (configurable)
- **DLQ routing** - Evicted messages go to dead letter queue if configured

**Configuration:**
```bash
export METRICS_SAMPLE_INTERVAL_MS=1000
export METRICS_AGGREGATE_INTERVAL_S=60
export RETENTION_INTERVAL=300000         # 5 minutes
export RETENTION_BATCH_SIZE=1000         # Messages per batch
export PARTITION_CLEANUP_DAYS=7          # Days before removing empty partitions
export METRICS_RETENTION_DAYS=30         # Days to keep metrics history
export EVICTION_INTERVAL=60000           # 1 minute
export EVICTION_BATCH_SIZE=100           # Messages per eviction batch
```

### 7. Inter-Instance Communication (UDP)

In clustered deployments, servers notify each other via UDP when messages are pushed or acknowledged.

**Notification Types:**
| Event | Notification | Effect |
|-------|--------------|--------|
| PUSH (message queued) | `MESSAGE_AVAILABLE` | Wake waiting consumers |
| ACK (partition freed) | `PARTITION_FREE` | Wake consumers for partition |

**Configuration:**
```bash
export QUEEN_UDP_PEERS="queen-b:6633,queen-c:6633"
export QUEEN_UDP_NOTIFY_PORT=6633
```

**Latency Impact:**
| Scenario | Without Notifications | With UDP Notifications |
|----------|----------------------|------------------------|
| Cross-server message delivery | Up to 5000ms (backoff) | 10-50ms |

### 8. Failover Layer (File Buffer)

**Zero message loss** when PostgreSQL is unavailable.

**Flow:**
```
Normal Operation:
  PUSH → Sidecar → PostgreSQL → Success

PostgreSQL Down:
  PUSH → Detect failure
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
export FILE_BUFFER_DIR=/var/lib/queen/buffers  # Linux default
export FILE_BUFFER_FLUSH_MS=100
export FILE_BUFFER_MAX_BATCH=100
export FILE_BUFFER_EVENTS_PER_FILE=10000
```

## Request Flow Examples

### PUSH Operation

```
1. Client sends HTTP POST to /api/v1/push
        ↓
2. Acceptor routes to Worker (round-robin)
        ↓
3. Worker parses JSON, registers in ResponseRegistry
        ↓
4. Worker queues request to Sidecar
   (PUSH does NOT call uv_async_send - relies on batch timer)
        ↓
5. Sidecar batch timer fires (every 5ms)
   Collects all pending PUSH requests
        ↓
6. Sidecar calls: SELECT queen.push_messages_v2($1)
   PQsendQueryParams() (non-blocking)
        ↓
7. uv_poll monitors socket for response
        ↓
8. Result ready → parse JSONB → deliver to worker
   uWS::Loop::defer() (thread-safe)
        ↓
9. Worker sends HTTP 201 response

Total time: 10-50ms (typical)
```

### POP Operation

```
1. Client sends GET to /api/v1/pop?partition=orders-123
        ↓
2. Worker registers in ResponseRegistry
        ↓
3. Sidecar batches POP requests
        ↓
4. Single call: SELECT queen.pop_unified_batch($1::jsonb)
   PostgreSQL:
     - Acquires lease (atomic UPDATE)
     - Fetches messages after cursor
     - Releases lease if empty
        ↓
5. Messages returned as JSON
        ↓
6. Response delivered to HTTP thread

Total time: 3-10ms (typical)
```

### POP with Wait (Long-Polling)

```
1. Client sends GET to /api/v1/pop?wait=true&timeout=30000
        ↓
2. Worker registers in ResponseRegistry
        ↓
3. Sidecar submits POP_WAIT request to waiting queue
        ↓
4. Sidecar polls database periodically:
   - Initial: 100ms interval
   - Backoff: 100ms → 200ms → ... → 1000ms
        ↓
5. SharedStateManager notifies on PUSH events
   → Sidecar wakes immediately, resets backoff
        ↓
6. Messages found → HTTP 200
   OR timeout → HTTP 204 No Content
```

## Performance Characteristics

### Latency

| Operation | Latency | Notes |
|-----------|---------|-------|
| PUSH (single) | 5-15ms | Batched via timer |
| PUSH (batch) | 10-30ms | Micro-batched |
| POP (immediate) | 3-10ms | State machine parallel |
| POP (long-poll) | 10ms-30s | Configurable timeout |
| ACK (single) | 5-15ms | Via sidecar |
| ACK (batch) | 10-30ms | Micro-batched |
| TRANSACTION | 30-100ms | Multiple operations |

### Throughput

| Metric | Value | Configuration |
|--------|-------|---------------|
| PUSH sustained | 130K+ msg/s | Batch operations |
| POP sustained | 50K+ ops/s | State machine parallel |
| Peak | 148K+ msg/s | Large batches |
| Single message | 2-5K msg/s | No batching |

### Resource Usage

| Resource | Default | Notes |
|----------|---------|-------|
| Worker threads | 2 | `NUM_WORKERS` |
| AsyncDbPool connections | 50 | `DB_POOL_SIZE` (for secondary ops) |
| Sidecar connections | 50 | `SIDECAR_POOL_SIZE` (split among workers) |
| Thread pool | 4 threads | Database operations |
| System thread pool | 2 threads | Background services |

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
   │    │    │        │
   └────┴────┴────────┘
          ↓
    PostgreSQL
```

**Benefits:**
- ✅ Linear scaling of request handling
- ✅ Shared PostgreSQL (no data sharding)
- ✅ Session-less design (any server handles any request)
- ✅ UDP notifications for cross-server coordination

### Vertical Scaling

```bash
# More workers
export NUM_WORKERS=20
export DB_POOL_SIZE=300
export SIDECAR_POOL_SIZE=100
```

## Design Principles

1. **Non-blocking I/O** - All database operations use async libpq + libuv
2. **Event-driven** - Worker threads never block on I/O
3. **Micro-batching** - Amortize overhead across multiple requests
4. **Operation-specific optimization** - PUSH buffers, POP parallelizes
5. **Stored procedures** - Complex logic in PostgreSQL (single round-trip)
6. **Fail-safe** - Automatic failover to file buffer
7. **Horizontal scalability** - Stateless server design with UDP coordination

## Configuration Summary

```bash
# Server
export PORT=6632
export NUM_WORKERS=2

# Database (secondary operations)
export DB_POOL_SIZE=50
export DB_STATEMENT_TIMEOUT=30000

# Sidecar (primary operations)
export SIDECAR_POOL_SIZE=50
export SIDECAR_MICRO_BATCH_WAIT_MS=5
export SIDECAR_MAX_ITEMS_PER_TX=1000

# Background services
export METRICS_SAMPLE_INTERVAL_MS=1000
export RETENTION_INTERVAL=300000
export EVICTION_INTERVAL=60000

# Inter-instance (clustered)
export QUEEN_UDP_PEERS="queen-b:6633,queen-c:6633"
export QUEEN_UDP_NOTIFY_PORT=6633

# Failover
export FILE_BUFFER_DIR=/var/lib/queen/buffers
```

## See Also

- [How It Works](/server/how-it-works) - Deep dive into uWebSockets, libuv, Sidecar pattern, and PostgreSQL stored procedures
- [Environment Variables](/server/environment-variables) - Complete configuration reference
- [Performance Tuning](/server/tuning) - Optimization guide
- [Deployment](/server/deployment) - Production deployment patterns
