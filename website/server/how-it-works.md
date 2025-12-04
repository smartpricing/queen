# How Queen Works

This page provides a deep dive into Queen's internal architecture, explaining how the core technologies work together: **uWebSockets**, **libuv**, **the Sidecar pattern**, and **PostgreSQL stored procedures**.

## Technology Stack

```
┌────────────────────────────────────────────────────────────────┐
│                    uWebSockets (HTTP Layer)                    │
│          Ultra-fast C++ web server with event loops            │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│                  Sidecar Pattern (libuv + libpq)               │
│     Per-worker async DB: PUSH, POP, ACK, TRANSACTION, LEASE    │
└────────────────────────────────────────────────────────────────┘
                              ↓
┌────────────────────────────────────────────────────────────────┐
│           PostgreSQL Stored Procedures (Data Layer)            │
│     Atomic, optimized PL/pgSQL functions for all operations    │
└────────────────────────────────────────────────────────────────┘

Secondary operations (queue config, tracing, consumer groups) use AsyncQueueManager
via a traditional connection pool, keeping the hot path (Sidecar) lean.
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
┌───────────────┬───────────────┐
│   Worker 0    │   Worker 1    │  (default: 2 workers)
│  Event Loop   │  Event Loop   │
│   (libuv)     │   (libuv)     │
│   [Sidecar]   │   [Sidecar]   │
└───────────────┴───────────────┘
```

**How it works:**

1. **Acceptor Thread** - Listens on port 6632, accepts all TCP connections
2. **Socket Distribution** - Hands off accepted sockets to workers via `adoptSocket()`
3. **Worker Threads** - Each runs its own event loop with a dedicated sidecar
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

## 2. The Sidecar Pattern: Async Database Layer

Each worker has a "sidecar" - a dedicated component that handles all PostgreSQL operations asynchronously using [libuv](https://libuv.org/) (the same event loop library that powers Node.js).

### Sidecar Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Worker Thread                                 │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                    uWS Event Loop                              │  │
│  │   HTTP Request → Parse → Register → Queue to Sidecar → Wait    │  │
│  └───────────────────────────────────────────────────────────────┘  │
│                              ↕                                       │
│                     ResponseRegistry                                 │
│              (maps request_id → HTTP response)                       │
│                              ↕                                       │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                   SidecarDbPool (libuv)                        │  │
│  │                                                                 │  │
│  │   ┌──────────────┐   ┌──────────────┐   ┌──────────────────┐   │  │
│  │   │ submit_signal│   │ batch_timer  │   │  waiting_timer   │   │  │
│  │   │  (uv_async)  │   │  (uv_timer)  │   │   (uv_timer)     │   │  │
│  │   │              │   │   5ms cycle  │   │  100ms-1000ms    │   │  │
│  │   └──────┬───────┘   └──────┬───────┘   └────────┬─────────┘   │  │
│  │          │                  │                    │              │  │
│  │          └────────┬─────────┴────────────────────┘              │  │
│  │                   ↓                                              │  │
│  │   ┌───────────────────────────────────────────────────────┐     │  │
│  │   │              pending_requests_ queue                   │     │  │
│  │   │   [PUSH] [PUSH] [ACK] [PUSH] [POP] [ACK] [PUSH]       │     │  │
│  │   └───────────────────────────────────────────────────────┘     │  │
│  │                   ↓ drain_pending_to_slots()                     │  │
│  │   ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐              │  │
│  │   │ Slot 0  │ │ Slot 1  │ │ Slot 2  │ │ Slot N  │              │  │
│  │   │  PGconn │ │  PGconn │ │  PGconn │ │  PGconn │              │  │
│  │   │uv_poll_t│ │uv_poll_t│ │uv_poll_t│ │uv_poll_t│              │  │
│  │   └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘              │  │
│  │        └───────────┴───────────┴───────────┘                    │  │
│  │                         ↓                                        │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
                        PostgreSQL
```

### libuv Components

#### 1. Async Signal (`submit_signal_`)
Cross-thread wakeup from HTTP thread to sidecar:

```cpp
// When HTTP thread receives a request:
void SidecarDbPool::submit(SidecarRequest request) {
    // Add to pending queue
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.push_back(std::move(request));
    }
    
    // Wake up the sidecar (for latency-sensitive ops)
    // PUSH operations skip this - they rely on batch timer
    if (loop_initialized_ && !is_push) {
        uv_async_send(&submit_signal_);  // Thread-safe, coalescing
    }
}

// Sidecar receives the signal:
void SidecarDbPool::on_submit_signal(uv_async_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    if (pool && pool->running_) {
        pool->drain_pending_to_slots();
        uv_timer_again(&pool->batch_timer_);  // Reset timer
    }
}
```

#### 2. Batch Timer (`batch_timer_`)
Micro-batching for throughput:

```cpp
// Timer fires every 5ms
uv_timer_start(&batch_timer_, on_batch_timer, 
               tuning_.micro_batch_wait_ms,   // Initial: 5ms
               tuning_.micro_batch_wait_ms);  // Repeat: 5ms

void SidecarDbPool::on_batch_timer(uv_timer_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    if (pool && pool->running_) {
        pool->drain_pending_to_slots();  // Process accumulated requests
    }
}
```

#### 3. Waiting Timer (`waiting_timer_`)
Long-polling for POP_WAIT operations:

```cpp
// Check waiting queue every 10ms
uv_timer_start(&waiting_timer_, on_waiting_timer,
               10,   // Initial: 10ms
               10);  // Repeat: 10ms

void SidecarDbPool::on_waiting_timer(uv_timer_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    if (pool && pool->running_) {
        pool->process_waiting_queue();
    }
}
```

#### 4. Poll Handles (`uv_poll`)
Monitor PostgreSQL sockets for read/write events:

```cpp
// Initialize poll handle for PostgreSQL connection socket
uv_poll_init(loop_, &slot.poll_handle, PQsocket(conn));

// Start watching for events
void start_watching_slot(ConnectionSlot& slot, int events) {
    uv_poll_start(&slot.poll_handle, events, on_socket_event);
}

// Handle socket events
void SidecarDbPool::on_socket_event(uv_poll_t* handle, int status, int events) {
    auto* slot = static_cast<ConnectionSlot*>(handle->data);
    
    if (events & UV_WRITABLE) {
        // Flush pending data to PostgreSQL
        PQflush(slot->conn);
    }
    
    if (events & UV_READABLE) {
        // Read response from PostgreSQL
        PQconsumeInput(slot->conn);
        if (PQisBusy(slot->conn) == 0) {
            // Query complete - process result
            PGresult* result = PQgetResult(slot->conn);
            process_result(slot, result);
        }
    }
}
```

### Operation-Specific Signal Behavior

The sidecar handles different operations differently for optimal performance:

```cpp
void SidecarDbPool::submit(SidecarRequest request) {
    // PUSH ops should buffer without immediate wakeup
    bool is_push = (request.op_type == SidecarOpType::PUSH);
    
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.push_back(std::move(request));
    }
    
    // PUSH: rely on batch timer (maximizes batching)
    // Others: wake immediately (minimizes latency)
    if (loop_initialized_ && !is_push) {
        uv_async_send(&submit_signal_);
    }
}
```

| Operation | Signal Sent? | Why |
|-----------|--------------|-----|
| **PUSH** | ❌ No | Allows requests to accumulate for batching |
| **POP** | ✅ Yes | Latency-sensitive, immediate processing |
| **ACK** | ✅ Yes | Consumers waiting for acknowledgment |
| **POP_WAIT** | ✅ Yes | Goes to waiting queue for processing |

This design means PUSH operations naturally batch together (5ms window), while POP/ACK get immediate attention.

### Micro-Batching Deep Dive

When `drain_pending_to_slots()` runs, it batches requests of the same type:

```cpp
void SidecarDbPool::drain_pending_to_slots() {
    while (!pending_requests_.empty()) {
        // Find a free connection slot
        ConnectionSlot* free_slot = find_free_slot();
        if (!free_slot) break;
        
        // Get the operation type of the first request
        SidecarOpType batch_op_type = pending_requests_.front().op_type;
        
        // Non-batchable ops (like individual POP) - send one at a time
        if (!is_batchable_op(batch_op_type)) {
            send_single_request(free_slot, pending_requests_.front());
            continue;
        }
        
        // Batch requests of the SAME type together
        std::vector<SidecarRequest> batch;
        size_t total_items = 0;
        
        while (!pending_requests_.empty() && 
               batch.size() < MAX_BATCH_SIZE &&
               total_items < MAX_ITEMS_PER_TX) {
            
            auto& next = pending_requests_.front();
            if (next.op_type != batch_op_type) break;
            
            batch.push_back(std::move(next));
            total_items += next.item_count;
            pending_requests_.pop_front();
        }
        
        // Send batch as single query
        send_batch(free_slot, batch);
    }
}
```

**Batching limits:**
- `MAX_BATCH_SIZE` = 100 requests per batch
- `MAX_ITEMS_PER_TX` = 1000 items per transaction

## 3. Unified POP Batch Processing

POP operations use a **unified batch procedure** that handles everything in a single database round-trip:
- Lease acquisition
- Message fetching  
- Automatic cleanup of empty leases

### Single Round-Trip Design

```
┌──────────────────────────────────────────────────────────────────────┐
│                      Sidecar POP Batch                                │
│                                                                       │
│   requests: [                                                         │
│     { idx: 0, queue: "orders", partition: "p-0" },                   │
│     { idx: 1, queue: "orders", partition: "p-1" },                   │
│     { idx: 2, queue: "orders", partition: null },  // wildcard       │
│   ]                                                                   │
│                                                                       │
│   → Single call to pop_unified_batch()                                │
│   ← Results for all requests in one response                          │
│                                                                       │
└──────────────────────────────────────────────────────────────────────┘
```

### How It Works

```sql
-- Single procedure handles everything atomically
SELECT queen.pop_unified_batch('[
    {"idx": 0, "queue_name": "orders", "partition_name": "p-0", ...},
    {"idx": 1, "queue_name": "orders", "partition_name": "p-1", ...},
    {"idx": 2, "queue_name": "orders", "partition_name": null, ...}
]'::jsonb);
```

**Inside the procedure:**

1. **Specific partitions** - Direct lookup and lease acquisition
2. **Wildcard partitions** - Use `FOR UPDATE SKIP LOCKED` to find available partition
3. **Lease acquisition** - Atomic `UPDATE ... WHERE lease_expires_at IS NULL`
4. **Message fetch** - Query messages after cursor position
5. **Auto cleanup** - Release lease immediately if no messages found

### SKIP LOCKED for Wildcards

When partition is not specified, the procedure uses `SKIP LOCKED` to avoid contention:

```sql
SELECT partition_id, partition_name, ...
FROM queen.partition_lookup pl
LEFT JOIN queen.partition_consumers pc ON ...
WHERE pl.queue_name = v_queue_name
  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
  AND (has unconsumed messages)
ORDER BY pc.last_consumed_at ASC NULLS FIRST
LIMIT 1
FOR UPDATE OF pl SKIP LOCKED;  -- Critical: don't block on contested partitions
```

**Benefits:**
- Multiple concurrent wildcards get different partitions
- No blocking on contested partitions
- Fair distribution via `ORDER BY last_consumed_at`

### Performance

| Metric | Value |
|--------|-------|
| POP throughput | 50,000+ ops/s |
| Round-trips per POP | 1 |
| p50 latency | 3-10ms |
| p99 latency | 15-30ms |

## 4. PostgreSQL Stored Procedures

All Queen operations are implemented as PostgreSQL **PL/pgSQL functions**. This moves complex logic to the database, providing atomicity and reducing round-trips.

### Core Procedures

| Procedure | Purpose | Key Features |
|-----------|---------|--------------|
| `push_messages_v2` | Insert messages | Batch insert, duplicate detection, partition auto-creation |
| `pop_unified_batch` | Pop messages | Single round-trip: lease + fetch + cleanup, SKIP LOCKED for wildcards |
| `ack_messages_v2` | Acknowledge | Batch ACK, lease release, DLQ support |

### How PUSH Works

```sql
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

### How POP Unified Batch Works

#### `pop_unified_batch`: Complete POP in One Call

```sql
CREATE OR REPLACE FUNCTION queen.pop_unified_batch(p_requests JSONB)
RETURNS JSONB
```

**Input format:**
```json
[
    {"idx": 0, "queue_name": "orders", "partition_name": "p-0", 
     "consumer_group": "worker", "batch_size": 10, "worker_id": "uuid"},
    {"idx": 1, "queue_name": "orders", "partition_name": null,
     "consumer_group": "worker", "batch_size": 10, "worker_id": "uuid2"}
]
```

**What it does (single transaction):**

1. **Specific partitions**: Direct lookup, atomic lease acquisition
2. **Wildcard partitions**: Use `FOR UPDATE SKIP LOCKED` to find available partition
3. **Lease acquisition**: Atomic `UPDATE ... WHERE lease_expires_at IS NULL OR <= NOW()`
4. **Message fetch**: Query messages after cursor position with window_buffer support
5. **Auto cleanup**: Release lease if no messages found

**Output format:**
```json
[
    {"idx": 0, "result": {"success": true, "partition": "p-0", 
     "leaseId": "uuid", "messages": [...]}},
    {"idx": 1, "result": {"success": true, "partition": "p-5",
     "leaseId": "uuid2", "messages": [...]}}
]
```

**Key features:**
- **Single round-trip**: Everything in one procedure call
- **SKIP LOCKED**: Wildcards don't block each other
- **Atomic lease**: No race conditions
- **Auto cleanup**: Empty leases released immediately

### How ACK Works

```sql
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
            END;
        
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

## 5. AsyncQueueManager: Secondary Operations

While the Sidecar handles high-performance primary operations (PUSH, POP, ACK), the **AsyncQueueManager** handles secondary operations that don't need the optimized hot path.

### What AsyncQueueManager Handles

| Operation | Description |
|-----------|-------------|
| **Schema initialization** | Create tables, load stored procedures on startup |
| **Queue configuration** | Create/update queue settings (`/api/v1/configure`) |
| **Consumer group management** | Subscription metadata, delete consumer groups |
| **Message tracing** | Record and query trace events |
| **Maintenance mode** | Toggle and status for file buffer routing |
| **File buffer replay** | Push buffered messages back to database |
| **Health checks** | Database connectivity verification |

### Why Separate from Sidecar?

1. **Keep hot path lean** - Sidecar focuses only on message operations
2. **Different access patterns** - Config changes are infrequent, don't need micro-batching
3. **Simpler code** - Admin operations use straightforward query patterns
4. **Resource isolation** - Slow admin queries don't impact message throughput

### Connection Pool

AsyncQueueManager uses a traditional connection pool (AsyncDbPool):

```cpp
// Acquire connection from pool
auto conn = async_db_pool_->acquire();

// Execute query
sendQueryParamsAsync(conn.get(), sql, params);
auto result = getTuplesResult(conn.get());

// Connection automatically returned on scope exit
```

This is simpler than the Sidecar's libuv-based approach, but sufficient for secondary operations that run infrequently.

## 6. Putting It All Together

### Complete PUSH Flow

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
│             (NO uv_async_send for PUSH - relies on timer)           │
└─────────────────────────────────────────────────────────────────────┘
                                │ pending_requests_.push_back()
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     3. SIDECAR (libuv)                              │
│             batch_timer_ fires (every 5ms)                          │
│             drain_pending_to_slots() collects PUSH requests         │
│             Batch same-type requests together                       │
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

Total latency: 5-20ms typical (batched)
```

### Complete POP Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                        CLIENT REQUEST                               │
│           GET /api/v1/pop?queue=orders&partition=p-123              │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     1. WORKER (uWebSockets)                         │
│             Parse request, register in ResponseRegistry             │
│             Queue POP_BATCH operation to Sidecar                    │
│             uv_async_send() wakes sidecar immediately               │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     2. SIDECAR (libuv)                              │
│             on_submit_signal() fires                                │
│             drain_pending_to_slots() processes request              │
│             Batches POP requests together                           │
│             Sends unified batch query                               │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                  3. POSTGRESQL: pop_unified_batch                   │
│             Single transaction handles all:                         │
│               - Acquire leases (SKIP LOCKED for wildcards)          │
│               - Fetch messages after cursor                         │
│               - Release empty leases                                │
│             Return: JSONB array of results                          │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     4. SIDECAR (libuv)                              │
│             Socket readable: uv_poll callback fires                 │
│             Read result: PQgetResult()                              │
│             Parse JSONB, distribute to requests                     │
│             Deliver to worker: uWS::Loop::defer()                   │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     5. WORKER (uWebSockets)                         │
│             Look up request in ResponseRegistry                     │
│             Send HTTP 200 with messages                             │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        CLIENT RESPONSE                              │
│            HTTP 200 { messages: [...], leaseId: "..." }             │
└─────────────────────────────────────────────────────────────────────┘

Total latency: 3-10ms typical (single DB round-trip)
```

## Performance Benefits

| Component | Contribution |
|-----------|--------------|
| **uWebSockets** | 100K+ req/s HTTP handling |
| **Sidecar (libuv)** | Non-blocking I/O, operation-specific optimization |
| **PUSH buffering** | Maximum batching, 130K+ msg/s throughput |
| **Unified POP Batch** | Single round-trip, 50K+ ops/s |
| **Stored Procedures** | Atomic operations, minimal round-trips |
| **Connection pooling** | Reuse PostgreSQL connections |

## Configuration Impact

### Tuning the Components

```bash
# uWebSockets (HTTP layer)
export NUM_WORKERS=2           # Event loop threads (default: 2)
export PORT=6632               # Listening port

# libuv (Sidecar) - Primary operations
export SIDECAR_POOL_SIZE=50    # PostgreSQL connections (split among workers)
export SIDECAR_MICRO_BATCH_WAIT_MS=5    # Batching window
export SIDECAR_MAX_ITEMS_PER_TX=1000    # Max items per transaction

# AsyncDbPool (Secondary operations)
export DB_POOL_SIZE=50         # Connections for queue config, tracing, etc.
export DB_STATEMENT_TIMEOUT=30000       # Query timeout
```

### Scaling Guidelines

| Workload | NUM_WORKERS | SIDECAR_POOL_SIZE | DB_POOL_SIZE |
|----------|-------------|-------------------|--------------|
| Light | 2 | 20 | 20 |
| Medium | 4 | 50 | 50 |
| Heavy | 8 | 100 | 100 |
| Very Heavy | 16 | 200 | 150 |

**Note:** SIDECAR_POOL_SIZE is the primary scaling knob for throughput. DB_POOL_SIZE only affects secondary operations.

## Summary

Queen's architecture combines key innovations:

1. **uWebSockets** - Handles HTTP with minimal overhead using an acceptor/worker pattern

2. **Sidecar Pattern** - Per-worker libuv event loops for primary operations (PUSH, POP, ACK):
   - PUSH: buffered for maximum batching (relies on timer, no immediate wakeup)
   - POP/ACK: immediate processing for low latency
   - POP_WAIT: separate timer-based polling with exponential backoff

3. **Unified POP Batch** - Single database round-trip for POP operations:
   - Lease + fetch + cleanup in one call
   - SKIP LOCKED for concurrent wildcard POPs
   - 50K+ ops/s throughput

4. **PostgreSQL Stored Procedures** - Execute complex operations atomically with single round-trips

5. **AsyncQueueManager** - Handles secondary operations (queue config, tracing, consumer groups) via traditional connection pool, keeping the hot path lean

This combination delivers:

- ✅ **Low latency**: 3-20ms typical
- ✅ **High throughput**: 130K+ msg/s PUSH, 50K+ ops/s POP
- ✅ **Reliability**: Atomic operations, file buffer failover
- ✅ **Scalability**: Horizontal and vertical scaling
- ✅ **Simplicity**: PostgreSQL as the single source of truth

## See Also

- [Architecture Overview](/server/architecture) - High-level system design
- [Environment Variables](/server/environment-variables) - Configuration reference
- [Performance Tuning](/server/tuning) - Optimization guide
