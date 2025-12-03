# How Queen Works

This page provides a deep dive into Queen's internal architecture, explaining how the core technologies work together: **uWebSockets**, **libuv**, **the Sidecar pattern**, **the POP State Machine**, and **PostgreSQL stored procedures**.

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
│                    POP State Machine                           │
│     Parallel POP processing across multiple DB connections     │
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

## 3. The POP State Machine: Parallel Processing

POP operations are fundamentally different from PUSH/ACK because each partition needs:
- Independent lease management
- Separate cursor tracking
- Advisory lock handling

The **POP State Machine** enables parallel processing across all available database connections.

### Why Not Batch POP?

Traditional batching doesn't work for POP:

```sql
-- This runs sequentially in PostgreSQL
FOR v_request IN SELECT * FROM parsed_requests
LOOP
    -- Each partition one at a time
    PERFORM pg_advisory_xact_lock(...);
    UPDATE queen.partition_consumers ...;
    SELECT ... FROM queen.messages ...;
END LOOP;
```

**Problem:** 50 connections available, only 1 used. Sequential processing = 1500ms for 500 POPs.

### State Machine Solution

The state machine processes POPs **in parallel**:

```
┌──────────────────────────────────────────────────────────────────────┐
│                      PopBatchStateMachine                             │
│                                                                       │
│   requests_: [                                                        │
│     { idx: 0, queue: "orders", partition: "p-0", state: PENDING },   │
│     { idx: 1, queue: "orders", partition: "p-1", state: PENDING },   │
│     { idx: 2, queue: "orders", partition: "p-2", state: PENDING },   │
│     ...                                                               │
│   ]                                                                   │
│                                                                       │
│   assign_connection(slot) → finds PENDING request → advances state    │
│   on_query_complete(slot, result) → handles result → advances state   │
│                                                                       │
└──────────────────────────────────────────────────────────────────────┘
```

### State Transitions

```
                                    ┌──────────────────────┐
                                    │      PENDING         │
                                    │  (waiting for conn)  │
                                    └──────────┬───────────┘
                                               │
                        ┌──────────────────────┴──────────────────────┐
                        │ partition specified?                        │
                        ▼                                             ▼
            ┌───────────────────┐                         ┌───────────────────┐
            │    RESOLVING      │                         │     LEASING       │
            │ (wildcard: find   │                         │ (acquire lease)   │
            │  partition)       │                         │                   │
            └────────┬──────────┘                         └─────────┬─────────┘
                     │                                              │
         ┌───────────┴───────────┐                      ┌───────────┴───────────┐
         │ found?                │                      │ acquired?             │
         ▼                       ▼                      ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│     EMPTY       │    │    LEASING      │    │    FETCHING     │    │     EMPTY       │
│ (no partition)  │    │                 │    │ (get messages)  │    │ (lease taken)   │
└─────────────────┘    └────────┬────────┘    └────────┬────────┘    └─────────────────┘
                                │                      │
                                ▼                      │
                       ┌─────────────────┐             │
                       │    FETCHING     │             │
                       └────────┬────────┘             │
                                │                      │
                    ┌───────────┴───────────┐          │
                    │ has messages?         │          │
                    ▼                       ▼          ▼
           ┌─────────────────┐    ┌─────────────────┐
           │   COMPLETED     │    │     EMPTY       │
           │ (has messages)  │    │ (no messages)   │
           └─────────────────┘    └─────────────────┘
```

### State Machine Implementation

```cpp
class PopBatchStateMachine {
public:
    using CompletionCallback = std::function<void(std::vector<PopRequestState>&)>;
    
    PopBatchStateMachine(
        std::vector<PopRequestState> requests,
        CompletionCallback on_complete,
        int worker_id);
    
    // Called when a connection becomes available
    bool assign_connection(ConnectionSlot* slot);
    
    // Called when a query completes
    void on_query_complete(ConnectionSlot* slot, PGresult* result);
    
    // Check completion status
    bool is_complete() const;
    int pending_count() const;

private:
    std::vector<PopRequestState> requests_;
    CompletionCallback on_complete_;
    
    void advance_state(PopRequestState& req);
    void submit_resolve_query(PopRequestState& req);
    void submit_lease_query(PopRequestState& req);
    void submit_fetch_query(PopRequestState& req);
    void submit_release_query(PopRequestState& req);
};
```

### State Flow Example

**Setup:** 3 POPs, 2 connections available

```
T=0ms   State Machine receives 3 requests
        
        Request 0: PENDING → LEASING (gets Connection 1)
        Request 1: PENDING → LEASING (gets Connection 2)
        Request 2: PENDING (waiting)
        
        Conn 1 sends: SELECT * FROM queen.pop_sm_lease(...)
        Conn 2 sends: SELECT * FROM queen.pop_sm_lease(...)

T=2ms   Connection 1 completes lease
        
        Request 0: LEASING → FETCHING
        Conn 1 sends: SELECT queen.pop_sm_fetch(...)

T=3ms   Connection 2 completes lease
        
        Request 1: LEASING → FETCHING
        Conn 2 sends: SELECT queen.pop_sm_fetch(...)

T=4ms   Connection 1 completes fetch (3 messages)
        
        Request 0: FETCHING → COMPLETED
        Connection 1 released
        
        Request 2: PENDING → LEASING (gets Connection 1)
        Conn 1 sends: SELECT * FROM queen.pop_sm_lease(...)

T=5ms   Connection 2 completes fetch (5 messages)
        
        Request 1: FETCHING → COMPLETED
        Connection 2 released

T=6ms   Connection 1 completes lease
        
        Request 2: LEASING → FETCHING
        Conn 1 sends: SELECT queen.pop_sm_fetch(...)

T=8ms   Connection 1 completes fetch (2 messages)
        
        Request 2: FETCHING → COMPLETED
        
        All requests complete → invoke on_complete_ callback

TOTAL: 8ms for 3 POPs (vs 24ms sequential)
```

### Connection Slot Management

The sidecar integrates state machines with regular batched operations:

```cpp
void SidecarDbPool::drain_pending_to_slots() {
    // First: Clean up completed state machines
    cleanup_completed_state_machines();
    
    // Second: Process pending regular requests
    // ... (PUSH, ACK batching) ...
    
    // Third: Assign remaining connections to state machines
    for (auto& sm : active_state_machines_) {
        if (sm->is_complete()) continue;
        
        for (auto& slot : slots_) {
            if (!slot.busy && slot.conn && sm->pending_count() > 0) {
                slot.busy = true;
                slot.state_machine = sm.get();
                sm->assign_connection(&slot);
            }
        }
    }
}
```

This ensures fair sharing: batched ops (PUSH, ACK) get connections first, then state machines get remaining connections.

## 4. PostgreSQL Stored Procedures

All Queen operations are implemented as PostgreSQL **PL/pgSQL functions**. This moves complex logic to the database, providing atomicity and reducing round-trips.

### Core Procedures

| Procedure | Purpose | Key Features |
|-----------|---------|--------------|
| `push_messages_v2` | Insert messages | Batch insert, duplicate detection, partition auto-creation |
| `pop_sm_resolve` | Find partition (wildcard) | SKIP LOCKED for concurrent wildcards |
| `pop_sm_lease` | Acquire partition lease | Atomic check-and-update |
| `pop_sm_fetch` | Read messages | Cursor-based, respects window_buffer |
| `pop_sm_release` | Release empty lease | Only if worker_id matches |
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

### How POP State Machine Procedures Work

#### `pop_sm_resolve`: Find Partition (Wildcard)

```sql
CREATE OR REPLACE FUNCTION queen.pop_sm_resolve(
    p_queue_name TEXT,
    p_consumer_group TEXT
) RETURNS TABLE (
    partition_id UUID,
    partition_name TEXT,
    queue_id UUID,
    lease_time INT,
    window_buffer INT,
    delayed_processing INT
)
```

**Key features:**
- **SKIP LOCKED** prevents concurrent wildcards from grabbing the same partition
- **ORDER BY last_consumed_at** for fair distribution
- Returns partition info + queue config

#### `pop_sm_lease`: Acquire Lease

```sql
CREATE OR REPLACE FUNCTION queen.pop_sm_lease(
    p_queue_name TEXT,
    p_partition_name TEXT,
    p_consumer_group TEXT,
    p_lease_seconds INT,
    p_worker_id TEXT,
    p_batch_size INT
) RETURNS TABLE (
    partition_id UUID,
    cursor_id UUID,
    cursor_ts TIMESTAMPTZ,
    queue_lease_time INT,
    window_buffer INT,
    delayed_processing INT
)
```

**Atomic lease acquisition:**

```sql
-- Try to acquire lease (atomic check-and-update)
UPDATE queen.partition_consumers pc
SET 
    lease_acquired_at = NOW(),
    lease_expires_at = NOW() + (v_effective_lease * INTERVAL '1 second'),
    worker_id = p_worker_id,
    batch_size = p_batch_size,
    acked_count = 0
WHERE pc.partition_id = v_partition_id
  AND pc.consumer_group = p_consumer_group
  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
RETURNING ...;
```

**Key features:**
- **Single UPDATE with WHERE clause** = atomic check-and-set
- No race conditions between check and acquire
- Returns cursor position for fetching

#### `pop_sm_fetch`: Get Messages

```sql
CREATE OR REPLACE FUNCTION queen.pop_sm_fetch(
    p_partition_id UUID,
    p_cursor_ts TIMESTAMPTZ,
    p_cursor_id UUID,
    p_batch_size INT,
    p_window_buffer INT DEFAULT 0,
    p_delayed_processing INT DEFAULT 0
) RETURNS JSONB
```

**Cursor-based fetching:**

```sql
SELECT COALESCE(jsonb_agg(
    jsonb_build_object(
        'id', m.id::text,
        'transactionId', m.transaction_id,
        'data', m.payload,
        'createdAt', to_char(m.created_at, ...)
    ) ORDER BY m.created_at, m.id
), '[]'::jsonb)
FROM queen.messages m
WHERE m.partition_id = p_partition_id
  -- After cursor position
  AND (p_cursor_ts IS NULL OR (m.created_at, m.id) > (p_cursor_ts, v_effective_cursor_id))
  -- Respect window_buffer
  AND (p_window_buffer = 0 OR m.created_at <= NOW() - (p_window_buffer || ' seconds')::interval)
  -- Respect delayed_processing
  AND (p_delayed_processing = 0 OR m.created_at <= NOW() - (p_delayed_processing || ' seconds')::interval)
LIMIT p_batch_size;
```

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

### Complete POP Flow (State Machine)

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
│             Creates PopBatchStateMachine for POP requests           │
│             Assigns available connection to state machine           │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                  3. STATE MACHINE: LEASING                          │
│             Request state: PENDING → LEASING                        │
│             Send: SELECT * FROM queen.pop_sm_lease(...)             │
│             Poll socket for response                                │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                  4. POSTGRESQL: pop_sm_lease                        │
│             Ensure partition_consumers row exists                   │
│             Atomic UPDATE with WHERE lease_expires_at check         │
│             Return: partition_id, cursor_id, cursor_ts, config      │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                  5. STATE MACHINE: FETCHING                         │
│             on_query_complete() processes lease result              │
│             Request state: LEASING → FETCHING                       │
│             Send: SELECT queen.pop_sm_fetch(...)                    │
│             Poll socket for response                                │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                  6. POSTGRESQL: pop_sm_fetch                        │
│             Query messages after cursor position                    │
│             Apply window_buffer and delayed_processing              │
│             Return: JSONB array of messages                         │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                  7. STATE MACHINE: COMPLETED                        │
│             on_query_complete() processes fetch result              │
│             Request state: FETCHING → COMPLETED                     │
│             Release connection slot                                 │
│             All requests done → invoke completion callback          │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     8. WORKER (uWebSockets)                         │
│             Completion callback delivers response                   │
│             uWS::Loop::defer() posts to HTTP thread                 │
│             Look up request in ResponseRegistry                     │
│             Send HTTP 200 with messages                             │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        CLIENT RESPONSE                              │
│            HTTP 200 { messages: [...], leaseId: "..." }             │
└─────────────────────────────────────────────────────────────────────┘

Total latency: 3-10ms typical
```

## Performance Benefits

| Component | Contribution |
|-----------|--------------|
| **uWebSockets** | 100K+ req/s HTTP handling |
| **Sidecar (libuv)** | Non-blocking I/O, operation-specific optimization |
| **PUSH buffering** | Maximum batching, 130K+ msg/s throughput |
| **POP State Machine** | Parallel processing, 50K+ ops/s |
| **Stored Procedures** | 1 round-trip, atomic operations |
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

Queen's architecture combines four key innovations:

1. **uWebSockets** - Handles HTTP with minimal overhead using an acceptor/worker pattern

2. **Sidecar Pattern** - Per-worker libuv event loops for primary operations (PUSH, POP, ACK):
   - PUSH: buffered for maximum batching (relies on timer, no immediate wakeup)
   - POP/ACK: immediate processing for low latency
   - POP_WAIT: separate timer-based polling with exponential backoff

3. **POP State Machine** - Parallel POP processing across all database connections:
   - 10x throughput improvement (5K → 50K+ ops/s)
   - 25x latency reduction for large batches

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
