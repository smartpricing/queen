# How Queen Works

This page provides a deep dive into Queen's internal architecture, explaining how the three core technologies work together: **uWebSockets**, **libuv**, and **PostgreSQL stored procedures**.

## Technology Stack

```
┌────────────────────────────────────────────────────────────────┐
│                    uWebSockets (HTTP Layer)                    │
│          Ultra-fast C++ web server with event loops            │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                    libuv (Event Loop Engine)                   │
│   Cross-platform async I/O: timers, sockets, signals, files    │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│           PostgreSQL Stored Procedures (Data Layer)            │
│     Atomic, optimized PL/pgSQL functions for all operations    │
└────────────────────────────────────────────────────────────────┘
```

## 1. uWebSockets: The Network Layer

Queen uses [uWebSockets](https://github.com/uNetworking/uWebSockets) - one of the fastest HTTP libraries available. It's written in C++ and provides an event-driven, non-blocking architecture.

### Acceptor/Worker Pattern

```
Client Connections
        │
        ↓
┌───────────────┐
│   Acceptor    │  Single thread listening on port 6632
│    Thread     │  Accepts all incoming connections
└───────┬───────┘
        │ Round-robin distribution
        ↓
┌───────┴───────┬───────────────┬───────────────┐
│   Worker 1    │   Worker 2    │   Worker N    │
│  Event Loop   │  Event Loop   │  Event Loop   │
│   (libuv)     │   (libuv)     │   (libuv)     │
└───────────────┴───────────────┴───────────────┘
```

**How it works:**

1. **Acceptor Thread** - Listens on port 6632, accepts all TCP connections
2. **Socket Distribution** - Hands off accepted sockets to workers via `adoptSocket()`
3. **Worker Threads** - Each runs its own event loop, processing requests independently
4. **No Locking** - Each worker has its own resources, no thread contention

```cpp
// Acceptor distributes connections round-robin
acceptor->listen(host, port, [](auto* listen_socket) {
    // All workers registered via addChildApp()
    // uWebSockets handles round-robin internally
});
```

### Why uWebSockets?

- **Speed**: Handles 100K+ HTTP requests/second
- **Memory**: Minimal allocations, zero-copy where possible
- **Event-driven**: Non-blocking I/O throughout
- **WebSocket support**: Native streaming support

## 2. libuv: The Async I/O Engine

Queen uses [libuv](https://libuv.org/) - the same event loop library that powers Node.js. It provides cross-platform async I/O for everything: sockets, timers, files, and signals.

### The Sidecar Pattern

Each worker has a "sidecar" - a dedicated component that handles all PostgreSQL operations asynchronously using libuv.

```
┌─────────────────────────────────────────────────────────────┐
│                    Worker Thread                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │               uWS Event Loop                          │   │
│  │   HTTP Request → Parse → Route → Wait for Response    │   │
│  └──────────────────────────────────────────────────────┘   │
│                           ↕                                  │
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

### libuv Components Used

#### 1. Poll Handles (`uv_poll`)
Monitor PostgreSQL socket file descriptors for read/write events:

```cpp
// Initialize poll handle for PostgreSQL connection socket
uv_poll_init(loop_, &slot.poll_handle, PQsocket(conn));

// Start polling for readable events (query results ready)
uv_poll_start(&slot.poll_handle, UV_READABLE, on_socket_readable);
```

#### 2. Timers (`uv_timer`)
Micro-batching and long-polling intervals:

```cpp
// Batch timer - accumulate requests for 5ms before sending
uv_timer_init(loop_, &batch_timer_);
uv_timer_start(&batch_timer_, on_batch_timer, 5, 5);

// Waiting timer - check for pending messages
uv_timer_init(loop_, &waiting_timer_);
uv_timer_start(&waiting_timer_, on_waiting_timer, 100, 100);
```

#### 3. Async Handles (`uv_async`)
Cross-thread wakeup (HTTP thread → sidecar thread):

```cpp
// Initialize async signal for cross-thread wakeup
uv_async_init(loop_, &submit_signal_, on_submit_signal);

// HTTP thread signals sidecar when new request arrives
uv_async_send(&submit_signal_);
```

### Non-Blocking PostgreSQL Flow

```
1. HTTP thread receives request
        │
        ↓
2. Request queued to sidecar
   uv_async_send() wakes sidecar
        │
        ↓
3. Sidecar batches requests (5ms)
   uv_timer fires → process batch
        │
        ↓
4. Send query to PostgreSQL
   PQsendQueryParams() (non-blocking)
        │
        ↓
5. Poll socket for response
   uv_poll_start(UV_READABLE)
        │
        ↓
6. Socket becomes readable
   PQconsumeInput() + PQgetResult()
        │
        ↓
7. Deliver result to HTTP thread
   uWS::Loop::defer() (thread-safe)
        │
        ↓
8. HTTP response sent to client
```

### Why libuv?

- **Single event loop**: All I/O handled in one place
- **Non-blocking**: Never blocks on I/O operations
- **Cross-platform**: Works on Linux, macOS, Windows
- **Battle-tested**: Powers Node.js (millions of deployments)

## 3. PostgreSQL Stored Procedures

All Queen operations are implemented as PostgreSQL **PL/pgSQL functions**. This moves complex logic to the database, providing atomicity and reducing round-trips.

### Core Procedures

| Procedure | Purpose | Key Features |
|-----------|---------|--------------|
| `push_messages_v2` | Insert messages | Batch insert, duplicate detection, partition auto-creation |
| `pop_messages_v2` | Consume messages | Two-phase locking, lease acquisition, cursor tracking |
| `ack_messages_v2` | Acknowledge | Batch ACK, lease release, DLQ support |
| `execute_transaction` | Atomic multi-op | BEGIN/COMMIT/ROLLBACK wrapper |
| `renew_lease` | Extend lease | Validate and extend lease time |

### How PUSH Works

```sql
-- push_messages_v2: High-performance batch push
CREATE OR REPLACE FUNCTION queen.push_messages_v2(
    p_items jsonb,
    p_check_duplicates boolean DEFAULT true
) RETURNS jsonb
```

**Internal flow:**

```sql
1. Create temp table from JSON input (single pass)
   CREATE TEMP TABLE tmp_items AS SELECT ... FROM jsonb_array_elements(p_items)

2. Auto-create queues (batch upsert)
   INSERT INTO queen.queues ... ON CONFLICT DO NOTHING

3. Auto-create partitions (batch upsert)
   INSERT INTO queen.partitions ... ON CONFLICT DO NOTHING

4. Check duplicates (if enabled)
   UPDATE tmp_items SET is_duplicate = true WHERE EXISTS (
       SELECT 1 FROM queen.messages WHERE transaction_id = ...
   )

5. Insert non-duplicate messages (bulk)
   INSERT INTO queen.messages SELECT ... FROM tmp_items WHERE NOT is_duplicate

6. Return results as JSON array
   RETURN jsonb_agg(jsonb_build_object('status', 'queued', ...))
```

**Why it's fast:**

- ✅ Single function call (1 round-trip)
- ✅ Bulk operations (no row-by-row loops)
- ✅ ON CONFLICT for upserts (no SELECT + INSERT)
- ✅ Temp table for working data (efficient)

### How POP Works

```sql
-- pop_messages_v2: Two-phase locking pop
CREATE OR REPLACE FUNCTION queen.pop_messages_v2(
    p_queue_name TEXT,
    p_partition_name TEXT DEFAULT NULL,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INT DEFAULT 10,
    p_lease_time_seconds INT DEFAULT 300
) RETURNS JSONB
```

**Two-phase locking:**

```sql
-- PHASE 1: Find available partition
SELECT partition_id FROM queen.partition_lookup
WHERE queue_name = p_queue_name
  AND (lease_expires_at IS NULL OR lease_expires_at <= NOW())
LIMIT 1;

-- PHASE 2: Acquire lease (atomic upsert)
INSERT INTO queen.partition_consumers (partition_id, consumer_group, lease_expires_at, ...)
VALUES (v_partition_id, p_consumer_group, NOW() + lease_time, ...)
ON CONFLICT (partition_id, consumer_group) DO UPDATE
  SET lease_expires_at = NOW() + lease_time
  WHERE lease_expires_at IS NULL OR lease_expires_at <= NOW()
RETURNING worker_id = v_lease_id INTO v_lease_acquired;

-- PHASE 3: If lease acquired, fetch messages
WITH locked_messages AS (
    SELECT * FROM queen.messages
    WHERE partition_id = v_partition_id
      AND created_at > v_cursor_ts
    FOR UPDATE SKIP LOCKED  -- Skip rows locked by other transactions
    LIMIT p_batch_size
)
SELECT jsonb_agg(...) INTO v_messages FROM locked_messages;
```

**Key guarantees:**

- ✅ **Atomic lease acquisition**: No race conditions
- ✅ **SKIP LOCKED**: Concurrent consumers don't block
- ✅ **Cursor tracking**: Consumer resumes where it left off
- ✅ **Exactly-once**: Messages locked until ACKed

### How ACK Works

```sql
-- ack_messages_v2: Batch acknowledgment
CREATE OR REPLACE FUNCTION queen.ack_messages_v2(
    p_acknowledgments JSONB
) RETURNS JSONB
```

**Batch processing with deadlock prevention:**

```sql
-- Sort by partitionId + transactionId to prevent deadlocks
FOR v_ack IN (
    SELECT * FROM jsonb_array_elements(p_acknowledgments)
    ORDER BY partition_id, txn_id
)
LOOP
    -- Validate lease
    IF lease_id IS VALID THEN
        -- Update consumer cursor
        UPDATE queen.partition_consumers
        SET last_consumed_id = v_message_id,
            acked_count = acked_count + 1,
            -- Release lease when batch complete
            lease_expires_at = CASE 
                WHEN acked_count + 1 >= batch_size THEN NULL 
                ELSE lease_expires_at 
            END
        WHERE partition_id = v_partition_id
          AND consumer_group = v_consumer_group;
        
        -- Delete message (or move to DLQ if failed)
        DELETE FROM queen.messages WHERE id = v_message_id;
    END IF;
END LOOP;
```

**Key features:**

- ✅ **Deadlock prevention**: Sorted processing order
- ✅ **Auto lease release**: When batch complete
- ✅ **DLQ support**: Failed messages moved to dead letter queue
- ✅ **Progress tracking**: Consumer cursor updated atomically

### Why Stored Procedures?

| Benefit | Explanation |
|---------|-------------|
| **Atomicity** | Entire operation in single transaction |
| **Reduced round-trips** | 1 call vs multiple queries |
| **Server-side logic** | Complex operations without network overhead |
| **Consistency** | All clients use same logic |
| **Performance** | Query plans cached, optimized execution |

## Putting It All Together

### Complete Request Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                        CLIENT REQUEST                               │
│                POST /api/v1/push { items: [...] }                   │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     1. ACCEPTOR (uWebSockets)                       │
│                Accept TCP connection, route to worker               │
└─────────────────────────────────────────────────────────────────────┘
                                │ adoptSocket()
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     2. WORKER (uWebSockets)                         │
│             Parse HTTP request, extract JSON body                   │
│             Register request in ResponseRegistry                    │
│             Queue operation to Sidecar                              │
└─────────────────────────────────────────────────────────────────────┘
                                │ uv_async_send()
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     3. SIDECAR (libuv)                              │
│             Batch requests for 5ms (micro-batching)                 │
│             Prepare SQL: SELECT queen.push_messages_v2($1)          │
│             Send query: PQsendQueryParams() (non-blocking)          │
│             Poll socket: uv_poll_start(UV_READABLE)                 │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                  4. POSTGRESQL (Stored Procedure)                   │
│             BEGIN transaction                                        │
│             Execute push_messages_v2():                              │
│               - Create temp table from JSON                          │
│               - Upsert queues + partitions                           │
│               - Check duplicates                                     │
│               - Bulk insert messages                                 │
│             COMMIT transaction                                       │
│             Return JSONB result                                      │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     5. SIDECAR (libuv)                              │
│             Socket readable: uv_poll callback fires                 │
│             Read result: PQgetResult()                              │
│             Parse JSONB response                                    │
│             Deliver to worker: uWS::Loop::defer()                   │
└─────────────────────────────────────────────────────────────────────┘
                                │ defer()
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     6. WORKER (uWebSockets)                         │
│             Look up request in ResponseRegistry                     │
│             Send HTTP response with JSON body                       │
│             Status: 201 Created                                     │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        CLIENT RESPONSE                              │
│            HTTP 201 { results: [{ status: "queued", ... }] }        │
└─────────────────────────────────────────────────────────────────────┘

Total latency: 10-50ms typical
```

### Performance Benefits

| Component | Contribution |
|-----------|--------------|
| **uWebSockets** | 100K+ req/s HTTP handling |
| **libuv** | Non-blocking I/O, efficient polling |
| **Stored Procedures** | 1 round-trip, atomic operations |
| **Micro-batching** | Amortize overhead across requests |
| **Connection pooling** | Reuse PostgreSQL connections |

## Configuration Impact

### Tuning the Components

```bash
# uWebSockets (HTTP layer)
export NUM_WORKERS=10          # Event loop threads
export PORT=6632               # Listening port

# libuv (Sidecar)
export SIDECAR_POOL_SIZE=50    # PostgreSQL connections per worker pool
export SIDECAR_MICRO_BATCH_WAIT_MS=5    # Batching window
export SIDECAR_MAX_ITEMS_PER_TX=1000    # Max items per transaction

# PostgreSQL (Stored procedures)
export DB_POOL_SIZE=150        # Total connection pool
export DB_STATEMENT_TIMEOUT=30000       # Query timeout
```

### Scaling Guidelines

| Workload | NUM_WORKERS | SIDECAR_POOL_SIZE | DB_POOL_SIZE |
|----------|-------------|-------------------|--------------|
| Light | 4 | 25 | 50 |
| Medium | 10 | 50 | 150 |
| Heavy | 20 | 100 | 300 |
| Very Heavy | 30 | 150 | 500 |

## Summary

Queen's architecture combines three powerful technologies:

1. **uWebSockets** - Handles HTTP with minimal overhead using an acceptor/worker pattern
2. **libuv** - Provides non-blocking PostgreSQL I/O with poll handles, timers, and async signals
3. **PostgreSQL stored procedures** - Execute complex operations atomically with single round-trips

This combination delivers:

- ✅ **Low latency**: 10-50ms typical
- ✅ **High throughput**: 130K+ msg/s
- ✅ **Reliability**: Atomic operations, no message loss
- ✅ **Scalability**: Horizontal and vertical scaling
- ✅ **Simplicity**: PostgreSQL as the single source of truth

## See Also

- [Architecture Overview](/server/architecture) - High-level system design
- [Environment Variables](/server/environment-variables) - Configuration reference
- [Performance Tuning](/server/tuning) - Optimization guide

