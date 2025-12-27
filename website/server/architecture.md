# Server Architecture

Queen MQ uses a high-performance **acceptor/worker pattern** with fully asynchronous, non-blocking PostgreSQL operations for maximum throughput and minimal latency.

## System Overview

```
                     ┌─────────────────────────────────────────────────────────────┐
                     │                         QUEEN SERVER                        │
                     └─────────────────────────────────────────────────────────────┘
                                                  │
                                                  ▼
                     ┌─────────────────────────────────────────────────────────────┐
                     │              ACCEPTOR (port 6632, round-robin)              │
                     │                    uWebSockets event loop                   │
                     └────────────┬───────────────┬───────────────┬────────────────┘
                                  │               │               │
          ┌───────────────────────┼───────────────┼───────────────┼────────────────────────┐
          │                       │               │               │                        │
          ▼                       ▼               ▼               ▼                        ▼
┌─────────────────────┐ ┌─────────────────────┐       ┌─────────────────────┐ ┌─────────────────────┐
│    UWS WORKER 0     │ │    UWS WORKER 1     │  ...  │   UWS WORKER N-1    │ │    UWS WORKER N     │
│   (event loop)      │ │   (event loop)      │       │   (event loop)      │ │   (event loop)      │
│                     │ │                     │       │                     │ │                     │
│  ┌───────────────┐  │ │  ┌───────────────┐  │       │  ┌───────────────┐  │ │  ┌───────────────┐  │
│  │ HTTP Handler  │  │ │  │ HTTP Handler  │  │       │  │ HTTP Handler  │  │ │  │ HTTP Handler  │  │
│  └───────┬───────┘  │ │  └───────┬───────┘  │       │  └───────┬───────┘  │ │  └───────┬───────┘  │
│          │          │ │          │          │       │          │          │ │          │          │
│          ▼          │ │          ▼          │       │          ▼          │ │          ▼          │
│  ┌───────────────┐  │ │  ┌───────────────┐  │       │  ┌───────────────┐  │ │  ┌───────────────┐  │
│  │  Mutex Queue  │  │ │  │  Mutex Queue  │  │       │  │  Mutex Queue  │  │ │  │  Mutex Queue  │  │
│  └───────┬───────┘  │ │  └───────┬───────┘  │       │  └───────┬───────┘  │ │  └───────┬───────┘  │
│          │          │ │          │          │       │          │          │ │          │          │
│          ▼          │ │          ▼          │       │          ▼          │ │          ▼          │
│  ┌───────────────┐  │ │  ┌───────────────┐  │       │  ┌───────────────┐  │ │  ┌───────────────┐  │
│  │ LIBQUEEN 0    │  │ │  │ LIBQUEEN 1    │  │       │  │ LIBQUEEN N-1  │  │ │  │ LIBQUEEN N    │  │
│  │ (libuv loop)  │  │ │  │ (libuv loop)  │  │       │  │ (libuv loop)  │  │ │  │ (libuv loop)  │  │
│  │               │  │ │  │               │  │       │  │               │  │ │  │               │  │
│  │ Timer (5ms)   │  │ │  │ Timer (5ms)   │  │       │  │ Timer (5ms)   │  │ │  │ Timer (5ms)   │  │
│  │      │        │  │ │  │      │        │  │       │  │      │        │  │ │  │      │        │  │
│  │      ▼        │  │ │  │      ▼        │  │       │  │      ▼        │  │ │  │      ▼        │  │
│  │ Microbatch    │  │ │  │ Microbatch    │  │       │  │ Microbatch    │  │ │  │ Microbatch    │  │
│  │      │        │  │ │  │      │        │  │       │  │      │        │  │ │  │      │        │  │
│  │      ▼        │  │ │  │      ▼        │  │       │  │      ▼        │  │ │  │      ▼        │  │
│  │ PG Pool (M)   │  │ │  │ PG Pool (M)   │  │       │  │ PG Pool (M)   │  │ │  │ PG Pool (M)   │  │
│  └───────────────┘  │ │  └───────────────┘  │       │  └───────────────┘  │ │  └───────────────┘  │
└─────────────────────┘ └─────────────────────┘       └─────────────────────┘ └─────────────────────┘
          │                       │               │               │                        │
          └───────────────────────┴───────────────┴───────────────┴────────────────────────┘
                                                  │
                                                  ▼
                                     ┌─────────────────────────┐
                                     │       PostgreSQL        │
                                     │    (N×M connections)    │
                                     └─────────────────────────┘
```

## Core Components

### 1. Acceptor Thread

A single thread that listens on port 6632 and distributes connections to workers in round-robin fashion. It does nothing but accept TCP connections and pass sockets to workers — pure routing, no processing logic.

```cpp
// Acceptor distributes connections round-robin
acceptor->listen(host, port, [](auto* listen_socket) {
    // All workers registered via addChildApp()
    // uWebSockets handles round-robin internally
});
```

### 2. uWS Workers

Each worker is a thread with its own uWebSockets event loop. When an HTTP request arrives (e.g. push), the worker:

1. Parses the request
2. Registers in ResponseRegistry (maps request_id → HTTP response)
3. Pushes the operation onto a mutex-protected queue
4. **Does not wait** — the HTTP response will be sent when Postgres responds

```bash
export NUM_WORKERS=2    # Default: 2 workers
export PORT=6632        # Default listening port
```

### 3. libqueen (Per-Worker Async Database)

This is where the magic happens. Each uWS worker has its own **libqueen** instance, which runs in a separate thread with its own **libuv event loop**. libqueen handles all PostgreSQL operations asynchronously.

```
┌─────────────────────────────────────────────────────────────────────┐
│                        libqueen instance                             │
│                                                                      │
│   ┌──────────────┐   ┌──────────────┐   ┌──────────────────┐        │
│   │ submit_signal│   │ batch_timer  │   │  waiting_timer   │        │
│   │  (uv_async)  │   │  (uv_timer)  │   │   (uv_timer)     │        │
│   │              │   │   5ms cycle  │   │  100ms-1000ms    │        │
│   └──────┬───────┘   └──────┬───────┘   └────────┬─────────┘        │
│          │                  │                    │                   │
│          └────────┬─────────┴────────────────────┘                   │
│                   ↓                                                  │
│   ┌───────────────────────────────────────────────────────┐         │
│   │              pending_requests_ queue                   │         │
│   │   [PUSH] [PUSH] [ACK] [PUSH] [POP] [ACK] [PUSH]       │         │
│   └───────────────────────────────────────────────────────┘         │
│                   ↓ drain_pending_to_slots()                         │
│   ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐                   │
│   │ Slot 0  │ │ Slot 1  │ │ Slot 2  │ │ Slot N  │                   │
│   │  PGconn │ │  PGconn │ │  PGconn │ │  PGconn │                   │
│   │uv_poll_t│ │uv_poll_t│ │uv_poll_t│ │uv_poll_t│                   │
│   └─────────┘ └─────────┘ └─────────┘ └─────────┘                   │
└─────────────────────────────────────────────────────────────────────┘
```

**How libqueen works:**

1. **Timer fires every 5ms** — Collects all pending operations from the queue
2. **Groups operations by type** — PUSH, POP, ACK, etc.
3. **Sends a single query per type** — Microbatching: 1000 HTTP requests → 1 Postgres call
4. **Monitors PG sockets with `uv_poll`** — Non-blocking read/write
5. **Invokes callbacks** when Postgres responds — Delivers results to HTTP thread via `uWS::Loop::defer()`

**libuv primitives used:**

| Component | Purpose |
|-----------|---------|
| `uv_poll` | Monitor PostgreSQL connection sockets for read/write events |
| `uv_timer` (batch) | Accumulate requests for 5ms before sending (microbatching) |
| `uv_timer` (waiting) | Long-polling for POP_WAIT with exponential backoff |
| `uv_async` | Wake libqueen immediately for latency-sensitive ops (POP, ACK) |

### 4. Why 1 libqueen per 1 uWS Worker?

The alternative would be a single shared libqueen instance for all workers. But this would create:

- **Lock contention** on the shared queue
- **Single libuv loop** becoming the bottleneck
- **More complexity** in callback management

With the 1:1 approach, each worker is **completely independent**. If you have 12 workers, you have 12 libuv loops running in parallel, each with its own pool of Postgres connections. **Workers never talk to each other.**

```bash
# 12 workers = 12 libqueen instances = 12 × 50 = 600 Postgres connections
export NUM_WORKERS=12
export SIDECAR_POOL_SIZE=50
```

## Background Services

Queen runs three background services:

| Service | Purpose | Default Interval |
|---------|---------|------------------|
| **MetricsCollector** | CPU, memory, queue depths | 1s sample, 60s aggregate |
| **RetentionService** | Delete expired messages, empty partitions | 5 minutes |
| **EvictionService** | Evict messages exceeding max_wait_time | 1 minute |

## Inter-Instance Communication (UDP)

In clustered deployments, servers notify each other via UDP when messages are pushed or acknowledged:

| Event | Notification | Effect |
|-------|--------------|--------|
| PUSH (message queued) | `MESSAGE_AVAILABLE` | Wake waiting consumers |
| ACK (partition freed) | `PARTITION_FREE` | Wake consumers for partition |

```bash
export QUEEN_UDP_PEERS="queen-b:6633,queen-c:6633"
export QUEEN_UDP_NOTIFY_PORT=6633
```

## Failover Layer (File Buffer)

**Zero message loss** when PostgreSQL is unavailable:

```
Normal:     PUSH → libqueen → PostgreSQL → Success
DB Down:    PUSH → Detect failure → Write to file buffer → Success
Recovery:   Background → Read .buf files → Replay to PostgreSQL
```

```bash
export FILE_BUFFER_DIR=/var/lib/queen/buffers
export FILE_BUFFER_FLUSH_MS=100
export FILE_BUFFER_MAX_BATCH=100
```

## Design Principles

1. **Non-blocking I/O** — All database operations use async libpq + libuv
2. **Event-driven** — Worker threads never block on I/O
3. **1:1 worker isolation** — Each worker has its own libqueen, no contention
4. **Microbatching** — Amortize overhead across multiple requests
5. **Stored procedures** — Complex logic in PostgreSQL (single round-trip)
6. **Fail-safe** — Automatic failover to file buffer

## Configuration Summary

```bash
# Server
export PORT=6632
export NUM_WORKERS=2

# libqueen (primary operations)
export SIDECAR_POOL_SIZE=50
export SIDECAR_MICRO_BATCH_WAIT_MS=5
export SIDECAR_MAX_ITEMS_PER_TX=1000

# AsyncQueueManager (secondary operations)
export DB_POOL_SIZE=50
export DB_STATEMENT_TIMEOUT=30000

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

- [Deployment](/server/deployment) - Production deployment patterns
