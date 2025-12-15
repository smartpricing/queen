# How It Works

This page provides a deep dive into Queen's internals — the asynchronous architecture, libuv integration, PostgreSQL stored procedures, and the microbatching strategy that enables 130K+ msg/s throughput.

## Overview

Queen is built on three core principles:

1. **Non-blocking I/O everywhere** — No thread ever blocks waiting for a database response
2. **Microbatching** — Multiple HTTP requests are combined into single PostgreSQL calls
3. **Stored procedures** — Complex logic runs in PostgreSQL, minimizing round-trips

The result is a system that can handle tens of thousands of concurrent connections with minimal latency and maximum throughput.

## The Acceptor/Worker Pattern

Queen uses a multi-threaded architecture where each component has a specific responsibility:

```
                              ┌─────────────────────────────────────────┐
                              │            QUEEN SERVER                  │
                              └─────────────────────────────────────────┘
                                                │
                                                ▼
                              ┌─────────────────────────────────────────┐
                              │     ACCEPTOR (port 6632, round-robin)   │
                              │          uWebSockets event loop         │
                              └──────┬──────────┬──────────┬────────────┘
                                     │          │          │
          ┌──────────────────────────┼──────────┼──────────┼─────────────────────────┐
          │                          │          │          │                         │
          ▼                          ▼          ▼          ▼                         ▼
┌──────────────────┐      ┌──────────────────┐      ┌──────────────────┐      ┌──────────────────┐
│   UWS WORKER 0   │      │   UWS WORKER 1   │      │   UWS WORKER 2   │      │   UWS WORKER N   │
│   (event loop)   │      │   (event loop)   │      │   (event loop)   │      │   (event loop)   │
│        │         │      │        │         │      │        │         │      │        │         │
│        ▼         │      │        ▼         │      │        ▼         │      │        ▼         │
│  ┌────────────┐  │      │  ┌────────────┐  │      │  ┌────────────┐  │      │  ┌────────────┐  │
│  │ libqueen 0 │  │      │  │ libqueen 1 │  │      │  │ libqueen 2 │  │      │  │ libqueen N │  │
│  │ (libuv)    │  │      │  │ (libuv)    │  │      │  │ (libuv)    │  │      │  │ (libuv)    │  │
│  │     │      │  │      │  │     │      │  │      │  │     │      │  │      │  │     │      │  │
│  │     ▼      │  │      │  │     ▼      │  │      │  │     ▼      │  │      │  │     ▼      │  │
│  │ PG Pool    │  │      │  │ PG Pool    │  │      │  │ PG Pool    │  │      │  │ PG Pool    │  │
│  └────────────┘  │      │  └────────────┘  │      │  └────────────┘  │      │  └────────────┘  │
└──────────────────┘      └──────────────────┘      └──────────────────┘      └──────────────────┘
          │                          │                        │                        │
          └──────────────────────────┴────────────────────────┴────────────────────────┘
                                                │
                                                ▼
                                    ┌──────────────────────┐
                                    │      PostgreSQL      │
                                    │   (N×M connections)  │
                                    └──────────────────────┘
```

### 1. Acceptor Thread

A single thread that listens on port 6632 and distributes connections to workers in round-robin fashion. It performs pure routing with no processing logic:

```cpp
// Acceptor distributes connections round-robin
acceptor->listen(host, port, [](auto* listen_socket) {
    // All workers registered via addChildApp()
    // uWebSockets handles round-robin internally
});
```

### 2. uWebSockets Workers

Each worker is a thread with its own [uWebSockets](https://github.com/uNetworking/uWebSockets) event loop. When an HTTP request arrives:

1. Parse the request
2. Register in ResponseRegistry (maps `request_id` → HTTP response)
3. Push the operation onto a mutex-protected queue for libqueen
4. **Return immediately** — the HTTP response will be sent when PostgreSQL responds

Workers never block on I/O. They hand off work and continue processing other requests.

### 3. libqueen — The Async Database Layer

This is where the magic happens. Each uWS worker has its own **libqueen** instance running in a separate thread with its own [libuv](https://libuv.org/) event loop.

```
┌─────────────────────────────────────────────────────────────────────┐
│                        libqueen instance                             │
│                                                                      │
│   ┌──────────────┐   ┌──────────────┐   ┌──────────────────┐        │
│   │ submit_signal│   │ batch_timer  │   │  waiting_timer   │        │
│   │  (uv_async)  │   │  (uv_timer)  │   │   (uv_timer)     │        │
│   │              │   │   5ms cycle  │   │  100ms-5000ms    │        │
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

**How libqueen processes requests:**

1. **Timer fires every 5ms** — Collects all pending operations from the queue
2. **Groups operations by type** — PUSH, POP, ACK, TRANSACTION, RENEW_LEASE
3. **Combines each group into a single JSONB array** — Microbatching: 1000 HTTP requests → 1 PostgreSQL call
4. **Sends one query per type** — Each stored procedure accepts the batched array
5. **Monitors PostgreSQL sockets with `uv_poll`** — Non-blocking read/write
6. **Dispatches results by index** — Each result has an `idx` field matching the original request
7. **Invokes callbacks when PostgreSQL responds** — Delivers results to HTTP thread via `uWS::Loop::defer()`

### libuv Primitives Used

| Component | Purpose |
|-----------|---------|
| `uv_poll` | Monitor PostgreSQL connection sockets for read/write events |
| `uv_timer` (batch) | Accumulate requests for 5ms before sending (microbatching) |
| `uv_timer` (stats) | Periodic metrics collection and stale backoff cleanup |
| `uv_async` (reconnect) | Signal event loop when background thread reconnects a connection |
| `uv_async` (backoff) | Wake waiting consumers when UDP notifications arrive |

### Why 1 libqueen per 1 uWS Worker?

The alternative would be a single shared libqueen instance for all workers. But this would create:

- **Lock contention** on the shared queue
- **Single libuv loop** becoming the bottleneck
- **More complexity** in callback management

With the 1:1 approach, each worker is **completely independent**. If you have 12 workers, you have 12 libuv loops running in parallel, each with its own pool of PostgreSQL connections. **Workers never talk to each other.**

```bash
# 12 workers = 12 libqueen instances = 12 × 50 = 600 PostgreSQL connections
export NUM_WORKERS=12
export SIDECAR_POOL_SIZE=50
```

## Microbatching: The Performance Secret

The key to Queen's performance is **microbatching** — combining multiple client requests into a single database call.

### How It Works

When any request arrives (PUSH, POP, ACK, etc.), it doesn't go to the database immediately. Instead:

1. Request is added to the pending queue
2. libqueen's 5ms timer fires
3. All pending requests are grouped by operation type
4. Each group is combined into a single JSONB array
5. One stored procedure call is made per operation type

**Example:** 1000 clients each push 1 message in a 5ms window:
- Without batching: 1000 database round-trips
- With batching: 1 database round-trip with 1000 messages

**Example:** 100 POP requests + 50 ACK requests arrive in a 5ms window:
- Without batching: 150 database round-trips
- With batching: 2 database round-trips (1 for POPs, 1 for ACKs)

### All Operations Are Batched

Every operation type is batched the same way — combined into a single JSONB array and sent to PostgreSQL:

| Operation | Stored Procedure | Batchable |
|-----------|------------------|-----------|
| **PUSH** | `queen.push_messages_v2($1::jsonb)` | ✅ Yes |
| **POP** | `queen.pop_unified_batch($1::jsonb)` | ✅ Yes |
| **ACK** | `queen.ack_messages_v2($1::jsonb)` | ✅ Yes |
| **TRANSACTION** | `queen.execute_transaction_v2($1::jsonb)` | ✅ Yes |
| **RENEW_LEASE** | `queen.renew_lease_v2($1::jsonb)` | ✅ Yes |
| **CUSTOM** | User-provided SQL | ❌ No (one per slot) |

All operations are queued and processed when the 5ms timer fires. The timer drains the queue, groups jobs by type, and sends each group as a single batched call to PostgreSQL.

## PostgreSQL Stored Procedures

Queen pushes complex logic into PostgreSQL stored procedures, reducing round-trips and leveraging PostgreSQL's transactional guarantees.

### Push Messages

The `queen.push_messages_v2` procedure handles batch inserts with automatic queue/partition creation and duplicate detection:

```sql
CREATE OR REPLACE FUNCTION queen.push_messages_v2(p_items jsonb)
RETURNS jsonb
LANGUAGE plpgsql
AS $$
BEGIN
    -- Create temp table from input JSON (single pass)
    CREATE TEMP TABLE tmp_items ON COMMIT DROP AS
    SELECT 
        idx,
        COALESCE((item->>'messageId')::uuid, gen_random_uuid()) as message_id,
        COALESCE(item->>'transactionId', gen_random_uuid()::text) as transaction_id,
        item->>'queue' as queue_name,
        -- ... more fields
    FROM jsonb_array_elements(p_items) WITH ORDINALITY AS t(item, idx);

    -- Ensure queues exist (batch upsert)
    INSERT INTO queen.queues (name, namespace, task)
    SELECT DISTINCT queue_name, namespace, task FROM tmp_items
    ON CONFLICT (name) DO NOTHING;

    -- Ensure partitions exist (batch upsert)
    INSERT INTO queen.partitions (queue_id, name)
    SELECT DISTINCT q.id, t.partition_name
    FROM tmp_items t
    JOIN queen.queues q ON q.name = t.queue_name
    ON CONFLICT (queue_id, name) DO NOTHING;

    -- Handle duplicates, insert messages, return results...
END;
$$;
```

Key benefits:
- **Single round-trip** for any number of messages
- **Automatic queue/partition creation** with upserts
- **Duplicate detection** using transaction IDs
- **All-or-nothing semantics** within a transaction

### Pop Messages (Unified Batch)

The `queen.pop_unified_batch` procedure handles both specific partition pops and wildcard discovery in a single call:

```sql
CREATE OR REPLACE FUNCTION queen.pop_unified_batch(p_requests JSONB)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
BEGIN
    FOR v_req IN SELECT * FROM jsonb_array_elements(p_requests)
    LOOP
        IF v_is_wildcard THEN
            -- WILDCARD: Discover available partition with SKIP LOCKED
            SELECT pl.partition_id, p.name, ...
            INTO v_partition_id, v_partition_name, ...
            FROM queen.partition_lookup pl
            JOIN queen.partitions p ON p.id = pl.partition_id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = pl.partition_id
            WHERE pl.queue_name = v_req.queue_name
              AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
            ORDER BY pc.last_consumed_at ASC NULLS FIRST
            LIMIT 1
            FOR UPDATE OF pl SKIP LOCKED;  -- Critical for concurrent access
        ELSE
            -- SPECIFIC: Direct partition lookup
            SELECT p.id, ... FROM queen.partitions p WHERE ...;
        END IF;
        
        -- Acquire lease, fetch messages, cleanup...
    END LOOP;
    RETURN v_results;
END;
$$;
```

Key features:
- **SKIP LOCKED** for wildcard discovery prevents race conditions
- **Lease management** integrated into the same call
- **Fair distribution** via ordering by `last_consumed_at`
- **Auto-acknowledge** option for QoS 0 delivery

## Long Polling with Exponential Backoff

When a client calls `POP` with `wait=true`, Queen uses intelligent polling with exponential backoff:

```
Initial:    100ms wait
Attempt 2:  100ms wait  
Attempt 3:  100ms wait
Attempt 4:  300ms wait (backoff kicks in)
Attempt 5:  900ms wait
Attempt 6:  2700ms wait
...
Maximum:    5000ms wait
```

### How Backoff Works

1. **First few attempts** — Poll at initial interval (100ms)
2. **After threshold** — Backoff multiplier kicks in (3×)
3. **Cap at maximum** — Never wait more than 5 seconds
4. **Reset on activity** — When messages arrive or push happens, backoff resets

```cpp
// Backoff calculation in libqueen
if (job.backoff_count > _pop_wait_backoff_threshold) {
    next_interval = initial_interval * backoff_count * backoff_multiplier;
    if (next_interval > max_interval) {
        next_interval = max_interval;
    }
} else {
    next_interval = initial_interval;
}
```

### Wake-Up on Push

When a PUSH completes, libqueen wakes up waiting consumers for that queue/partition:

```cpp
// After successful PUSH
_update_pop_backoff_tracker(queue_name, partition_name);
```

This resets the backoff timer and immediately checks for messages, providing low-latency delivery.

## Request Flow Examples

### PUSH Flow

```
1. Client sends HTTP POST to /api/v1/push
        ↓
2. Acceptor routes to Worker (round-robin)
        ↓
3. Worker parses JSON, registers in ResponseRegistry
        ↓
4. Worker queues request to libqueen
        ↓
5. libqueen batch timer fires (every 5ms)
   Groups all pending PUSH requests into single JSONB
        ↓
6. libqueen calls: SELECT queen.push_messages_v2($1::jsonb)
   PQsendQueryParams() (non-blocking)
        ↓
7. uv_poll monitors socket for response
        ↓
8. Result ready → parse JSONB → dispatch by idx → deliver to workers
   uWS::Loop::defer() (thread-safe)
        ↓
9. Worker sends HTTP 201 response

Total time: 5-15ms (typical)
```

### POP Flow

```
1. Client sends GET to /api/v1/pop?queue=orders
        ↓
2. Worker registers in ResponseRegistry
        ↓
3. Worker queues request to libqueen
        ↓
4. libqueen batch timer fires (every 5ms)
   Groups all pending POP requests into single JSONB
        ↓
5. Single call: SELECT queen.pop_unified_batch($1::jsonb)
   PostgreSQL handles atomically:
     - Acquire leases (SKIP LOCKED for wildcards)
     - Fetch messages after cursor
     - Release lease if empty
        ↓
6. Results returned as JSONB array with idx for each request
        ↓
7. libqueen dispatches each result to matching callback
   uWS::Loop::defer() (thread-safe)
        ↓
8. Worker sends HTTP response

Total time: 3-10ms (typical)
```

## Connection Management

### Connection Pools

Each libqueen instance maintains its own pool of PostgreSQL connections:

```cpp
// DBConnection structure
struct DBConnection {
    PGconn* conn = nullptr;      // PostgreSQL connection
    int socket_fd = -1;          // Socket file descriptor
    uv_poll_t poll_handle;       // libuv poll handle
    std::vector<PendingJob> jobs; // Jobs using this connection
};
```

### Automatic Reconnection

libqueen runs a background reconnection thread that:

1. Checks for dead connections every second
2. Reconnects in the background (non-blocking)
3. Signals the event loop to initialize poll handles
4. Returns the connection to the pool

```cpp
void _reconnect_loop() {
    while (_reconnect_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        for (auto& slot : _db_connections) {
            if (slot.conn == nullptr) {
                if (_reconnect_slot_db(slot)) {
                    slot.needs_poll_init = true;
                    uv_async_send(&_reconnect_signal);
                }
            }
        }
    }
}
```

## Inter-Instance Communication (UDP)

In clustered deployments, Queen servers notify each other when messages are pushed or partitions become free:

| Event | Notification | Effect |
|-------|--------------|--------|
| PUSH (message queued) | `MESSAGE_AVAILABLE` | Wake waiting consumers |
| ACK (partition freed) | `PARTITION_FREE` | Wake consumers for partition |

```bash
export QUEEN_UDP_PEERS="queen-b:6633,queen-c:6633"
export QUEEN_UDP_NOTIFY_PORT=6633
```

When Server A pushes a message, it sends a UDP notification to Servers B and C. Those servers immediately wake up any consumers waiting for that queue, resetting their backoff timers.


## Database Schema Highlights

### Partition Consumers Table

Tracks the consumption state for each partition/consumer-group combination:

```sql
CREATE TABLE queen.partition_consumers (
    partition_id UUID,
    consumer_group VARCHAR(255),
    last_consumed_id UUID,           -- Cursor: last message ID
    last_consumed_created_at TIMESTAMPTZ,  -- Cursor: last message timestamp
    lease_expires_at TIMESTAMPTZ,    -- When current lease expires
    worker_id VARCHAR(255),          -- Which worker holds the lease
    batch_size INTEGER,              -- Expected batch size
    acked_count INTEGER,             -- Messages acknowledged so far
    UNIQUE(partition_id, consumer_group)
);
```

### Partition Lookup Table

Maintains a fast lookup for partition discovery:

```sql
CREATE TABLE queen.partition_lookup (
    queue_name VARCHAR(255),
    partition_id UUID,
    last_message_id UUID,            -- Latest message in partition
    last_message_created_at TIMESTAMPTZ,
    UNIQUE(queue_name, partition_id)
);
```

This table is updated by a trigger on message inserts, enabling O(1) partition discovery.

## Performance Characteristics

| Metric | Value |
|--------|-------|
| PUSH sustained | 130K+ msg/s (batched) |
| POP sustained | 50K+ ops/s |
| Single message | 10K+ msg/s (no client batching) |
| PUSH latency | 5-15ms |
| POP latency | 3-10ms |

## Configuration Reference

```bash
# Server
export PORT=6632
export NUM_WORKERS=2

# libqueen (primary operations)
export SIDECAR_POOL_SIZE=50              # Connections per worker
export SIDECAR_MICRO_BATCH_WAIT_MS=5     # Batching window
export SIDECAR_MAX_ITEMS_PER_TX=1000     # Max items per transaction

# Long polling
export SIDECAR_POP_WAIT_INITIAL_MS=100   # Initial poll interval
export SIDECAR_POP_WAIT_BACKOFF_THRESHOLD=3  # Attempts before backoff
export SIDECAR_POP_WAIT_BACKOFF_MULTIPLIER=3.0
export SIDECAR_POP_WAIT_MAX_MS=5000      # Maximum poll interval

# Inter-instance (clustered)
export QUEEN_UDP_PEERS="queen-b:6633,queen-c:6633"
export QUEEN_UDP_NOTIFY_PORT=6633
```

## See Also

- [Architecture](/server/architecture) — System overview diagram
- [Environment Variables](/server/environment-variables) — Complete configuration reference
- [Performance Tuning](/server/tuning) — Optimization guide
- [Failover & Recovery](/guide/failover) — Zero message loss guarantees

