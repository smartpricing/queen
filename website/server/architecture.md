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
│                      QUEUE LAYER                               │
│         AsyncQueueManager (Non-blocking Operations)            │
│    PUSH │ POP │ ACK │ TRANSACTION │ STREAM │ LEASE             │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                    DATABASE LAYER                              │
│  ┌──────────────────────────────────────────────────────┐     │
│  │  AsyncDbPool (95% of DB_POOL_SIZE connections)       │     │
│  │  + Per-worker Sidecars (libuv event loops)           │     │
│  └──────────────────────────────────────────────────────┘     │
│                            ↓                                   │
│              PostgreSQL (Stored Procedures)                    │
│    push_messages_v2 │ pop_messages_v2 │ ack_messages_v2        │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                  BACKGROUND SERVICES                           │
│  Stream Poll Workers │ Metrics │ Retention │ Eviction          │
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
- **Event loop** in each worker (uWebSockets + libuv)
- **Per-worker sidecar** for async PostgreSQL operations
- **Non-blocking I/O** throughout

**Configuration:**
```bash
export NUM_WORKERS=10
export PORT=6632
```

### 2. Sidecar Pattern (libuv + libpq)

Each worker has a dedicated **sidecar** that handles all PostgreSQL operations asynchronously using libuv.

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

**Key components:**
- **`uv_poll`** - Monitor PostgreSQL socket file descriptors
- **`uv_timer`** - Micro-batching (5ms windows) for efficiency
- **`uv_async`** - Cross-thread wakeup from HTTP to sidecar

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

### 3. Database Layer (AsyncDbPool)

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

### 4. Queue Layer (AsyncQueueManager)

Implements all message queue operations using PostgreSQL stored procedures.

**Operations:**
- **PUSH** - Calls `push_messages_v2()` - batch insert with duplicate detection
- **POP** - Calls `pop_messages_v2()` - two-phase locking, lease acquisition
- **ACK** - Calls `ack_messages_v2()` - batch acknowledgment, cursor update
- **TRANSACTION** - Atomic multi-operation execution

All operations are:
- ✅ Non-blocking (async libpq)
- ✅ Batched for efficiency
- ✅ Executed via stored procedures (single round-trip)

### 5. Background Services

#### Stream Poll Workers

Handle long-polling for stream/consumer group operations.

**Design:**
- Dedicated threads managed by DB ThreadPool
- Exponential backoff (1000ms → 5000ms)
- Window-based message consumption

**Configuration:**
```bash
export STREAM_POLL_WORKER_COUNT=1        # Stream poll worker threads
export STREAM_POLL_WORKER_INTERVAL=100   # Registry check interval (ms)
export STREAM_POLL_INTERVAL=1000         # Initial stream check interval (ms)
export STREAM_BACKOFF_THRESHOLD=5        # Empty checks before backoff
export STREAM_BACKOFF_MULTIPLIER=2.0     # Backoff multiplier
export STREAM_MAX_POLL_INTERVAL=5000     # Max backoff interval (ms)
export STREAM_CONCURRENT_CHECKS=2        # Concurrent window checks per worker
```

#### Other Services

- **MetricsCollector** - System and queue metrics (1s sample, 60s aggregate)
- **RetentionService** - Cleanup expired messages and partitions
- **EvictionService** - Handle max wait time eviction

**Configuration:**
```bash
export METRICS_SAMPLE_INTERVAL_MS=1000
export METRICS_AGGREGATE_INTERVAL_S=60
export RETENTION_INTERVAL=300000         # 5 minutes
export EVICTION_INTERVAL=60000           # 1 minute
```

### 6. Inter-Instance Communication (UDP)

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

### 7. Failover Layer (File Buffer)

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
   uv_async_send() wakes sidecar
        ↓
5. Sidecar batches requests (5ms window)
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

### POP with Wait (Long-Polling)

```
1. Client sends GET to /api/v1/pop?wait=true&timeout=30000
        ↓
2. Worker registers in ResponseRegistry
        ↓
3. Sidecar submits POP_WAIT request
        ↓
4. Sidecar polls database periodically:
   - Initial: 100ms interval
   - Backoff: 100ms → 200ms → ... → 1000ms
        ↓
5. SharedStateManager notifies on PUSH events
   → Sidecar wakes immediately, resets backoff
        ↓
6. Messages found → deliver to worker → HTTP 200
   OR timeout → HTTP 204 No Content
```

## Performance Characteristics

### Latency

| Operation | Latency | Notes |
|-----------|---------|-------|
| PUSH (single) | 10-30ms | Via sidecar + stored procedure |
| PUSH (batch) | 20-50ms | Micro-batched |
| POP (immediate) | 10-30ms | No wait |
| POP (long-poll) | 10ms-30s | Configurable timeout |
| ACK (single) | 10-30ms | Via sidecar |
| ACK (batch) | 20-50ms | Micro-batched |
| TRANSACTION | 30-100ms | Multiple operations |

### Throughput

| Metric | Value | Configuration |
|--------|-------|---------------|
| Sustained | 130K+ msg/s | Batch operations |
| Peak | 148K+ msg/s | Large batches |
| Single message | 2-5K msg/s | No batching |

### Resource Usage

| Resource | Default | Notes |
|----------|---------|-------|
| Worker threads | 10 | `NUM_WORKERS` |
| DB connections | 150 | `DB_POOL_SIZE` (95% async) |
| Sidecar connections | 50 | `SIDECAR_POOL_SIZE` (split among workers) |
| Stream workers | 1 | `STREAM_POLL_WORKER_COUNT` |

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
4. **Stored procedures** - Complex logic in PostgreSQL (single round-trip)
5. **Fail-safe** - Automatic failover to file buffer
6. **Horizontal scalability** - Stateless server design with UDP coordination

## Configuration Summary

```bash
# Server
export PORT=6632
export NUM_WORKERS=10

# Database
export DB_POOL_SIZE=150
export DB_STATEMENT_TIMEOUT=30000

# Sidecar
export SIDECAR_POOL_SIZE=50
export SIDECAR_MICRO_BATCH_WAIT_MS=5

# Stream polling
export STREAM_POLL_WORKER_COUNT=1
export STREAM_POLL_INTERVAL=1000

# Inter-instance (clustered)
export QUEEN_UDP_PEERS="queen-b:6633,queen-c:6633"
export QUEEN_UDP_NOTIFY_PORT=6633

# Failover
export FILE_BUFFER_DIR=/var/lib/queen/buffers
```

## See Also

- [How It Works](/server/how-it-works) - Deep dive into uWebSockets, libuv, and PostgreSQL stored procedures
- [Environment Variables](/server/environment-variables) - Complete configuration reference
- [Performance Tuning](/server/tuning) - Optimization guide
- [Deployment](/server/deployment) - Production deployment patterns
