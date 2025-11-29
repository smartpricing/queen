# ALL_ASYNC: Moving All Operations to Sidecar Pattern

## Executive Summary

This document describes the engineering plan to move **POP (wait=false)**, **ACK**, **TRANSACTION**, and **RENEWLEASE** operations to the async sidecar pattern, matching what we've already implemented for PUSH.

### Current State
- ✅ **PUSH**: Fully async via sidecar (`PUSH_USE_SIDECAR=true`)
- ❌ **POP**: Blocking (uses AsyncDbPool directly)
- ❌ **ACK**: Blocking
- ❌ **TRANSACTION**: Blocking
- ❌ **RENEWLEASE**: Blocking

### Target State
All operations use the sidecar for non-blocking I/O, freeing HTTP workers during DB operations.

### Performance Optimizations (Code Review)

| Issue | Risk | Solution |
|-------|------|----------|
| **SKIP LOCKED trap** | Sequential scans under load | Two-phase locking: find candidates first, then lock |
| **ACK deadlocks** | Concurrent ACKs deadlock | Sort by (partitionId, transactionId) before locking |
| **JSON overhead** | CPU bottleneck in Postgres | Use `to_jsonb()` instead of `jsonb_build_object()` |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              HTTP Workers (8)                                │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ... ┌─────────┐           │
│  │Worker 0 │ │Worker 1 │ │Worker 2 │ │Worker 3 │     │Worker 7 │           │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘     └────┬────┘           │
│       │           │           │           │               │                 │
│       └───────────┴───────────┴─────┬─────┴───────────────┘                 │
│                                     │                                        │
│                              ┌──────▼──────┐                                 │
│                              │  Sidecar    │  submit(SidecarRequest)         │
│                              │  Request    │  - op_type: PUSH/POP/ACK/TX/RENEW
│                              │  Queue      │  - request_id                   │
│                              └──────┬──────┘  - worker_id                    │
│                                     │         - params (JSON)                │
└─────────────────────────────────────┼───────────────────────────────────────┘
                                      │
                              ┌───────▼────────┐
                              │  SidecarDbPool │
                              │  Poller Thread │
                              │  (select() on  │
                              │   85 conns)    │
                              └───────┬────────┘
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
              ┌─────▼─────┐    ┌─────▼─────┐    ┌─────▼─────┐
              │ PG Conn 1 │    │ PG Conn 2 │    │ PG Conn N │
              │           │    │           │    │           │
              │ push_v2() │    │ pop_v2()  │    │ ack_v2()  │
              └───────────┘    └───────────┘    └───────────┘
                                      │
                              ┌───────▼────────┐
                              │  Completed     │
                              │  Response Queue│
                              └───────┬────────┘
                                      │
              ┌───────────────────────┼───────────────────────┐
              │                       │                       │
        ┌─────▼─────┐          ┌─────▼─────┐          ┌─────▼─────┐
        │Worker 0   │          │Worker 1   │          │Worker N   │
        │Resp Queue │          │Resp Queue │          │Resp Queue │
        └─────┬─────┘          └─────┬─────┘          └─────┬─────┘
              │                      │                      │
        ┌─────▼─────┐          ┌─────▼─────┐          ┌─────▼─────┐
        │ Response  │          │ Response  │          │ Response  │
        │ Timer     │          │ Timer     │          │ Timer     │
        │ (1ms)     │          │ (1ms)     │          │ (1ms)     │
        └─────┬─────┘          └─────┬─────┘          └─────┬─────┘
              │                      │                      │
              ▼                      ▼                      ▼
         HTTP Response          HTTP Response          HTTP Response
```

---

## Phase 1: Infrastructure Changes

### 1.1 Add Operation Type to SidecarRequest

**File**: `server/include/queen/sidecar_db_pool.hpp`

```cpp
// NEW: Operation type enum
enum class SidecarOpType {
    PUSH,
    POP,
    ACK,
    ACK_BATCH,
    TRANSACTION,
    RENEW_LEASE
};

struct SidecarRequest {
    SidecarOpType op_type;      // NEW: Identifies the operation
    std::string request_id;
    std::string sql;            // The stored procedure call
    std::vector<std::string> params;
    int worker_id;
    size_t item_count = 0;
    std::chrono::steady_clock::time_point queued_at;
};
```

### 1.2 Add Configuration Flags

**File**: `server/include/queen/config.hpp`

```cpp
struct QueueConfig {
    // Existing
    bool push_use_sidecar = false;
    
    // NEW: Per-operation sidecar flags
    bool pop_use_sidecar = false;
    bool ack_use_sidecar = false;
    bool transaction_use_sidecar = false;
    bool renew_lease_use_sidecar = false;
    
    // Or: Single flag to enable all
    bool all_ops_use_sidecar = false;  // Master switch
};

// Environment variables:
// ALL_OPS_USE_SIDECAR=true           (master switch)
// POP_USE_SIDECAR=true               (individual)
// ACK_USE_SIDECAR=true
// TRANSACTION_USE_SIDECAR=true
// RENEW_LEASE_USE_SIDECAR=true
```

### 1.3 Response Parsing by Operation Type

**File**: `server/src/acceptor_server.cpp` (response_timer_callback)

Currently, response parsing assumes PUSH format. We need to handle different response formats:

```cpp
for (const auto& resp : sidecar_responses) {
    nlohmann::json json_response;
    int status_code = 200;
    bool is_error = false;
    
    if (resp.success) {
        try {
            json_response = nlohmann::json::parse(resp.result_json);
            
            // Response format varies by operation type
            // PUSH: [{index, transaction_id, status, message_id}, ...]
            // POP:  {messages: [{id, payload, ...}], lease_id: "..."}
            // ACK:  [{message_id, success, error}, ...]
            // etc.
            
            // Set appropriate status code
            if (resp.op_type == SidecarOpType::PUSH) {
                status_code = 201;
            }
        } catch (...) {
            // error handling
        }
    }
    // ... route to worker queue
}
```

---

## Phase 2: Stored Procedures

> **⚠️ PERFORMANCE OPTIMIZATIONS** (from code review):
> 1. **SKIP LOCKED trap**: Use CTE to find candidate IDs first, then lock
> 2. **Deadlock prevention**: Always sort inputs by ID before locking  
> 3. **JSON performance**: Use `to_jsonb()` instead of `jsonb_build_object()`

### 2.1 pop_messages_v2

**File**: `server/migrations/004_pop_stored_procedure.sql`

```sql
CREATE OR REPLACE FUNCTION queen.pop_messages_v2(
    p_queue_name TEXT,
    p_partition_name TEXT DEFAULT NULL,  -- NULL = any partition
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INT DEFAULT 10,
    p_lease_time_seconds INT DEFAULT 300,
    p_subscription_mode TEXT DEFAULT 'all',
    p_subscription_from TEXT DEFAULT NULL
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_partition_id UUID;
    v_lease_id UUID;
    v_messages JSONB;
    v_cursor_id UUID;
    v_cursor_ts TIMESTAMPTZ;
    v_queue_name TEXT;
    v_partition_name TEXT;
BEGIN
    -- Generate lease ID
    v_lease_id := gen_random_uuid();
    
    -- Find partition (specific or any with messages)
    IF p_partition_name IS NOT NULL THEN
        SELECT p.id, q.name, p.name INTO v_partition_id, v_queue_name, v_partition_name
        FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name = p_queue_name AND p.name = p_partition_name;
    ELSE
        -- Find partition with available messages (considering consumer group cursor)
        -- ... complex query to find best partition
    END IF;
    
    IF v_partition_id IS NULL THEN
        RETURN jsonb_build_object('messages', '[]'::jsonb, 'leaseId', NULL);
    END IF;
    
    -- Get cursor position for consumer group
    SELECT last_consumed_id, last_consumed_created_at 
    INTO v_cursor_id, v_cursor_ts
    FROM queen.partition_consumers
    WHERE partition_id = v_partition_id AND consumer_group = p_consumer_group;
    
    -- OPTIMIZED: Two-phase locking to avoid SKIP LOCKED performance trap
    -- Phase 1: Cheaply identify candidate IDs (index scan only, no locks)
    -- Phase 2: Lock and update only the candidates
    WITH candidates AS (
        -- Lightweight index scan to find candidate message IDs
        -- Grab extras in case some are locked by concurrent consumers
        SELECT m.id
        FROM queen.messages m
        WHERE m.partition_id = v_partition_id
          AND m.status = 'pending'
          AND (m.lease_expires_at IS NULL OR m.lease_expires_at < NOW())
          AND (v_cursor_ts IS NULL OR m.created_at > v_cursor_ts 
               OR (m.created_at = v_cursor_ts AND m.id > v_cursor_id))
        ORDER BY m.created_at, m.id
        LIMIT p_batch_size * 2  -- Overfetch to handle locked rows
    ),
    locked_ids AS (
        -- Lock only what we need from candidates
        SELECT id FROM candidates
        FOR UPDATE SKIP LOCKED
        LIMIT p_batch_size
    ),
    leased AS (
        -- Update the locked messages
        UPDATE queen.messages m
        SET lease_id = v_lease_id,
            lease_expires_at = NOW() + (p_lease_time_seconds || ' seconds')::interval,
            status = 'leased'
        WHERE m.id IN (SELECT id FROM locked_ids)
        RETURNING m.id, m.transaction_id, m.payload, m.trace_id,
                  m.retry_count, m.priority, m.created_at
    )
    -- OPTIMIZED: Use to_jsonb() instead of jsonb_build_object() (C-native, faster)
    SELECT jsonb_agg(
        to_jsonb(sub) || jsonb_build_object(
            'queue', v_queue_name,
            'partition', v_partition_name,
            'partitionId', v_partition_id::text
        )
        ORDER BY sub.created_at, sub.id
    )
    INTO v_messages
    FROM (
        SELECT 
            id::text as id,
            transaction_id as "transactionId",
            trace_id::text as "traceId",
            payload as data,
            retry_count as "retryCount",
            priority,
            to_char(created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') as "createdAt"
        FROM leased
    ) sub;
    
    RETURN jsonb_build_object(
        'messages', COALESCE(v_messages, '[]'::jsonb),
        'leaseId', v_lease_id::text
    );
END;
$$;
```

### 2.2 ack_messages_v2

**File**: `server/migrations/005_ack_stored_procedure.sql`

> **⚠️ DEADLOCK PREVENTION**: Sort inputs by (partitionId, transactionId) before locking.
> Without sorting, concurrent requests ACKing `[Msg1, Msg2]` and `[Msg2, Msg1]` will deadlock.

```sql
CREATE OR REPLACE FUNCTION queen.ack_messages_v2(
    p_acknowledgments JSONB
    -- [{index, transactionId, partitionId, leaseId, status, consumerGroup, error}, ...]
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_ack JSONB;
    v_results JSONB := '[]'::jsonb;
    v_message_id UUID;
    v_partition_id UUID;
    v_queue_name TEXT;
    v_partition_name TEXT;
    v_success BOOLEAN;
    v_error TEXT;
    v_new_status TEXT;
BEGIN
    -- ⚠️ CRITICAL: Sort by partitionId + transactionId to prevent deadlocks
    -- When two concurrent requests ACK overlapping messages in different order,
    -- sorting ensures consistent lock acquisition order across all transactions
    FOR v_ack IN 
        SELECT value FROM jsonb_array_elements(p_acknowledgments)
        ORDER BY (value->>'partitionId'), (value->>'transactionId')
    LOOP
        v_success := false;
        v_error := NULL;
        
        -- Find the message
        SELECT m.id, p.id, q.name, p.name
        INTO v_message_id, v_partition_id, v_queue_name, v_partition_name
        FROM queen.messages m
        JOIN queen.partitions p ON m.partition_id = p.id
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE m.transaction_id = v_ack->>'transactionId'
          AND p.id = (v_ack->>'partitionId')::uuid;
        
        IF v_message_id IS NULL THEN
            v_error := 'Message not found';
        ELSE
            -- Validate lease if provided
            IF v_ack->>'leaseId' IS NOT NULL THEN
                IF NOT EXISTS (
                    SELECT 1 FROM queen.messages 
                    WHERE id = v_message_id 
                      AND lease_id = (v_ack->>'leaseId')::uuid
                      AND lease_expires_at > NOW()
                ) THEN
                    v_error := 'Invalid or expired lease';
                END IF;
            END IF;
            
            IF v_error IS NULL THEN
                -- Determine new status
                v_new_status := CASE v_ack->>'status'
                    WHEN 'success' THEN 'consumed'
                    WHEN 'completed' THEN 'consumed'
                    WHEN 'failed' THEN 'failed'
                    WHEN 'dlq' THEN 'dlq'
                    ELSE 'consumed'
                END;
                
                -- Update message (FOR UPDATE implicit in UPDATE)
                UPDATE queen.messages
                SET status = v_new_status,
                    consumed_at = NOW(),
                    lease_id = NULL,
                    lease_expires_at = NULL,
                    error = v_ack->>'error'
                WHERE id = v_message_id;
                
                -- Update partition consumer cursor
                INSERT INTO queen.partition_consumers 
                    (partition_id, consumer_group, last_consumed_id, last_consumed_created_at)
                SELECT partition_id, v_ack->>'consumerGroup', id, created_at
                FROM queen.messages WHERE id = v_message_id
                ON CONFLICT (partition_id, consumer_group) 
                DO UPDATE SET 
                    last_consumed_id = EXCLUDED.last_consumed_id,
                    last_consumed_created_at = EXCLUDED.last_consumed_created_at
                WHERE EXCLUDED.last_consumed_created_at > partition_consumers.last_consumed_created_at
                   OR (EXCLUDED.last_consumed_created_at = partition_consumers.last_consumed_created_at 
                       AND EXCLUDED.last_consumed_id > partition_consumers.last_consumed_id);
                
                v_success := true;
            END IF;
        END IF;
        
        -- Use original index from input for result ordering
        v_results := v_results || jsonb_build_object(
            'index', (v_ack->>'index')::int,
            'transactionId', v_ack->>'transactionId',
            'success', v_success,
            'error', v_error,
            'queueName', v_queue_name,
            'partitionName', v_partition_name
        );
    END LOOP;
    
    -- Sort results by original index before returning
    SELECT COALESCE(jsonb_agg(item ORDER BY (item->>'index')::int), '[]'::jsonb)
    INTO v_results
    FROM jsonb_array_elements(v_results) item;
    
    RETURN v_results;
END;
$$;
```

### 2.3 execute_transaction_v2

**File**: `server/migrations/006_transaction_stored_procedure.sql`

```sql
CREATE OR REPLACE FUNCTION queen.execute_transaction_v2(
    p_operations JSONB
    -- [{type: "push", queue, partition, payload, ...}, 
    --  {type: "ack", transactionId, partitionId, ...}]
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_op JSONB;
    v_results JSONB := '[]'::jsonb;
    v_transaction_id TEXT;
    v_idx INT := 0;
    v_push_result JSONB;
    v_ack_result JSONB;
BEGIN
    -- Generate transaction ID for this atomic operation
    v_transaction_id := gen_random_uuid()::text;
    
    FOR v_op IN SELECT * FROM jsonb_array_elements(p_operations)
    LOOP
        CASE v_op->>'type'
            WHEN 'push' THEN
                -- Call push logic (inline or via push_messages_v2 for single item)
                -- Insert message...
                v_results := v_results || jsonb_build_object(
                    'index', v_idx,
                    'type', 'push',
                    'success', true,
                    'transactionId', v_op->>'transactionId'
                );
                
            WHEN 'ack' THEN
                -- Call ack logic
                -- Update message status...
                v_results := v_results || jsonb_build_object(
                    'index', v_idx,
                    'type', 'ack',
                    'success', true,
                    'transactionId', v_op->>'transactionId'
                );
                
            ELSE
                RAISE EXCEPTION 'Unknown operation type: %', v_op->>'type';
        END CASE;
        
        v_idx := v_idx + 1;
    END LOOP;
    
    RETURN jsonb_build_object(
        'transactionId', v_transaction_id,
        'success', true,
        'results', v_results
    );
    
EXCEPTION WHEN OTHERS THEN
    -- Transaction auto-rollbacks
    RETURN jsonb_build_object(
        'transactionId', v_transaction_id,
        'success', false,
        'error', SQLERRM,
        'results', v_results
    );
END;
$$;
```

### 2.4 renew_lease_v2

**File**: `server/migrations/007_renew_lease_stored_procedure.sql`

> **Note**: Lease renewals on the same message are rare, so deadlock risk is low.
> But we sort anyway for consistency with the other procedures.

```sql
CREATE OR REPLACE FUNCTION queen.renew_lease_v2(
    p_items JSONB
    -- [{index, leaseId, extendSeconds}, ...]
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_item JSONB;
    v_results JSONB := '[]'::jsonb;
    v_success BOOLEAN;
BEGIN
    -- Sort by leaseId for consistent lock ordering
    FOR v_item IN 
        SELECT value FROM jsonb_array_elements(p_items)
        ORDER BY (value->>'leaseId')
    LOOP
        UPDATE queen.messages
        SET lease_expires_at = NOW() + ((v_item->>'extendSeconds')::int || ' seconds')::interval
        WHERE lease_id = (v_item->>'leaseId')::uuid
          AND lease_expires_at > NOW();  -- Only if not already expired
        
        v_success := FOUND;
        
        v_results := v_results || jsonb_build_object(
            'index', (v_item->>'index')::int,
            'leaseId', v_item->>'leaseId',
            'success', v_success,
            'error', CASE WHEN NOT v_success THEN 'Lease not found or expired' ELSE NULL END
        );
    END LOOP;
    
    -- Sort results by original index
    SELECT COALESCE(jsonb_agg(item ORDER BY (item->>'index')::int), '[]'::jsonb)
    INTO v_results
    FROM jsonb_array_elements(v_results) item;
    
    RETURN v_results;
END;
$$;
```

### 2.5 Required Indexes

The two-phase locking optimization in `pop_messages_v2` relies on efficient index scans.

**File**: `server/migrations/004_pop_stored_procedure.sql` (include with procedure)

```sql
-- Critical index for POP performance
-- Enables efficient candidate selection without table locks
CREATE INDEX IF NOT EXISTS idx_messages_pop_candidates 
ON queen.messages (partition_id, status, created_at, id)
WHERE status = 'pending';

-- Index for lease lookups (RENEW_LEASE)
CREATE INDEX IF NOT EXISTS idx_messages_lease_id 
ON queen.messages (lease_id)
WHERE lease_id IS NOT NULL;

-- Index for ACK lookups by transaction_id
CREATE INDEX IF NOT EXISTS idx_messages_transaction_partition 
ON queen.messages (transaction_id, partition_id);
```

---

## Phase 3: Route Modifications

### 3.1 POP Route (wait=false only)

**File**: `server/src/routes/pop.cpp`

```cpp
// Non-waiting mode: check for sidecar
if (!wait) {
    if (ctx.config.queue.pop_use_sidecar && global_sidecar_pool_ptr) {
        // Register response for async delivery
        std::string request_id = global_response_registry->register_response(
            res, ctx.worker_id, nullptr
        );
        
        // Build JSON params
        nlohmann::json params = {
            {"queue", queue_name},
            {"partition", partition_name},  // or null
            {"consumerGroup", consumer_group},
            {"batch", batch},
            {"leaseTime", ctx.config.queue.default_lease_time},
            {"subscriptionMode", options.subscription_mode.value_or("")},
            {"subscriptionFrom", options.subscription_from.value_or("")}
        };
        
        // Build sidecar request
        SidecarRequest req;
        req.op_type = SidecarOpType::POP;
        req.request_id = request_id;
        req.sql = "SELECT queen.pop_messages_v2($1, $2, $3, $4, $5, $6, $7)";
        req.params = {
            queue_name,
            partition_name.empty() ? "NULL" : partition_name,
            consumer_group,
            std::to_string(batch),
            std::to_string(ctx.config.queue.default_lease_time),
            options.subscription_mode.value_or("all"),
            options.subscription_from.value_or("")
        };
        req.worker_id = ctx.worker_id;
        req.item_count = 1;
        
        global_sidecar_pool_ptr->submit(std::move(req));
        return;
    }
    
    // Fallback to blocking mode...
}
```

### 3.2 ACK Route

**File**: `server/src/routes/ack.cpp`

```cpp
if (ctx.config.queue.ack_use_sidecar && global_sidecar_pool_ptr) {
    std::string request_id = global_response_registry->register_response(
        res, ctx.worker_id, nullptr
    );
    
    // Build acknowledgments JSON
    nlohmann::json acks_json = nlohmann::json::array();
    for (const auto& ack : ack_items) {
        acks_json.push_back(ack);
    }
    
    SidecarRequest req;
    req.op_type = SidecarOpType::ACK_BATCH;
    req.request_id = request_id;
    req.sql = "SELECT queen.ack_messages_v2($1::jsonb)";
    req.params = {acks_json.dump()};
    req.worker_id = ctx.worker_id;
    req.item_count = ack_items.size();
    
    global_sidecar_pool_ptr->submit(std::move(req));
    return;
}
```

### 3.3 Transaction Route

**File**: `server/src/routes/transactions.cpp`

```cpp
if (ctx.config.queue.transaction_use_sidecar && global_sidecar_pool_ptr) {
    std::string request_id = global_response_registry->register_response(
        res, ctx.worker_id, nullptr
    );
    
    // Add messageId to push operations (UUIDv7)
    nlohmann::json ops_json = nlohmann::json::array();
    for (const auto& op : operations) {
        nlohmann::json op_copy = op;
        if (op["type"] == "push") {
            op_copy["messageId"] = ctx.async_queue_manager->generate_uuid();
        }
        ops_json.push_back(op_copy);
    }
    
    SidecarRequest req;
    req.op_type = SidecarOpType::TRANSACTION;
    req.request_id = request_id;
    req.sql = "SELECT queen.execute_transaction_v2($1::jsonb)";
    req.params = {ops_json.dump()};
    req.worker_id = ctx.worker_id;
    req.item_count = operations.size();
    
    global_sidecar_pool_ptr->submit(std::move(req));
    return;
}
```

### 3.4 Renew Lease Route

**File**: `server/src/routes/leases.cpp`

```cpp
if (ctx.config.queue.renew_lease_use_sidecar && global_sidecar_pool_ptr) {
    std::string request_id = global_response_registry->register_response(
        res, ctx.worker_id, nullptr
    );
    
    nlohmann::json items = nlohmann::json::array();
    items.push_back({
        {"leaseId", lease_id},
        {"extendSeconds", seconds}
    });
    
    SidecarRequest req;
    req.op_type = SidecarOpType::RENEW_LEASE;
    req.request_id = request_id;
    req.sql = "SELECT queen.renew_lease_v2($1::jsonb)";
    req.params = {items.dump()};
    req.worker_id = ctx.worker_id;
    req.item_count = 1;
    
    global_sidecar_pool_ptr->submit(std::move(req));
    return;
}
```

---

## Phase 4: Sidecar Modifications

### 4.1 Enable Micro-batching for All Batchable Operations

**File**: `server/src/database/sidecar_db_pool.cpp`

Current code assumes all requests are PUSH and does string-splicing of JSON arrays. We need to extend this to support other operations.

**Operation-specific batching strategies:**

| Operation | Batching Strategy |
|-----------|------------------|
| PUSH | Combine JSON arrays → `push_messages_v2([...])` |
| ACK | Combine acknowledgments → `ack_messages_v2([...])` |
| POP | Combine pop requests → `pop_messages_batch_v2([...])` |
| TRANSACTION | **No batching** (each is atomic unit) |
| RENEW_LEASE | Combine lease extensions → `renew_lease_v2([...])` |

```cpp
// In poller_loop():
void process_batch(std::vector<SidecarRequest>& requests, SlotInfo& slot) {
    if (requests.empty()) return;
    
    SidecarOpType op_type = requests[0].op_type;
    
    switch (op_type) {
        case SidecarOpType::PUSH:
        case SidecarOpType::ACK:
        case SidecarOpType::POP:
        case SidecarOpType::RENEW_LEASE:
            // All use JSON array batching - combine into single SP call
            combine_and_execute_batch(requests, slot);
            break;
            
        case SidecarOpType::TRANSACTION:
            // No batching - each transaction is atomic
            for (auto& req : requests) {
                execute_single(req, slot);
            }
            break;
    }
}
```

**Batching constraints:**
- Only batch requests of the **same operation type**
- Collect for up to 5ms (when `MICRO_BATCH_WAIT_MS > 0`)
- Max 500 items per batch (`MAX_ITEMS_PER_TX`)

### 4.2 Response Parsing

The sidecar currently returns raw JSON from the stored procedure. The response timer callback needs to know how to interpret it:

```cpp
// In response_timer_callback():
for (const auto& resp : sidecar_responses) {
    nlohmann::json json_response;
    int status_code = 200;
    
    if (resp.success) {
        json_response = nlohmann::json::parse(resp.result_json);
        
        // Set status code based on operation
        switch (resp.op_type) {
            case SidecarOpType::PUSH:
                status_code = 201;
                break;
            case SidecarOpType::POP:
                // Check if messages were returned
                if (json_response["messages"].empty()) {
                    status_code = 204;  // No Content
                }
                break;
            // ... other operations
        }
    }
}
```

---

## Phase 5: Testing Strategy

### 5.1 Unit Tests

Create tests for each stored procedure in isolation:
- `test_pop_messages_v2.sql`
- `test_ack_messages_v2.sql`
- `test_execute_transaction_v2.sql`
- `test_renew_lease_v2.sql`

### 5.2 Integration Tests

Run existing test suite with each sidecar flag enabled individually:

```bash
# Test POP only
POP_USE_SIDECAR=true ./run_tests.sh

# Test ACK only
ACK_USE_SIDECAR=true ./run_tests.sh

# Test all together
ALL_OPS_USE_SIDECAR=true ./run_tests.sh
```

### 5.3 Tests to Watch

| Test | Operations Used | Risk |
|------|-----------------|------|
| `manualAck` | PUSH + POP + ACK | High |
| `testConsumerOrdering*` | PUSH + POP + ACK | High (ordering) |
| `testLoad*` | PUSH + POP + ACK | High (volume) |
| `transaction*` | TRANSACTION | Medium |
| `autoRenewLease` | PUSH + POP + RENEWLEASE | Medium |

---

## Phase 6: Rollout Plan

### Step 1: Deploy stored procedures
```bash
psql $DB_URL -f server/migrations/004_pop_stored_procedure.sql
psql $DB_URL -f server/migrations/005_ack_stored_procedure.sql
psql $DB_URL -f server/migrations/006_transaction_stored_procedure.sql
psql $DB_URL -f server/migrations/007_renew_lease_stored_procedure.sql
```

### Step 2: Deploy code with flags disabled
```bash
# All flags default to false
./bin/queen-server
```

### Step 3: Enable one operation at a time
```bash
# Week 1: RENEW_LEASE (lowest risk)
RENEW_LEASE_USE_SIDECAR=true

# Week 2: ACK
ACK_USE_SIDECAR=true

# Week 3: POP
POP_USE_SIDECAR=true

# Week 4: TRANSACTION
TRANSACTION_USE_SIDECAR=true
```

### Step 4: Monitor and validate
- Watch for increased latency
- Monitor error rates
- Check message ordering in tests
- Validate throughput improvements

---

## Design Decisions

### 1. Micro-batching: Enabled for ACK and POP ✅

Like PUSH, we will batch multiple ACK and POP requests in the sidecar's 5ms collection window.

**ACK Micro-batching:**
```
┌────────────────────────────────────────────────────────────┐
│ 5ms window                                                  │
│                                                             │
│ ACK Request 1: [{txn1, partition1}, {txn2, partition1}]    │
│ ACK Request 2: [{txn3, partition2}]                        │
│ ACK Request 3: [{txn4, partition1}, {txn5, partition3}]    │
│                                                             │
│ Combined: SELECT queen.ack_messages_v2('[                  │
│   {txn1, partition1, start_idx: 0},                        │
│   {txn2, partition1, start_idx: 0},                        │
│   {txn3, partition2, start_idx: 2},                        │
│   {txn4, partition1, start_idx: 3},                        │
│   {txn5, partition3, start_idx: 3}                         │
│ ]')                                                         │
│                                                             │
│ Results split back by start_idx → each client gets theirs  │
└────────────────────────────────────────────────────────────┘
```

**POP Micro-batching:**
```
┌────────────────────────────────────────────────────────────┐
│ 5ms window                                                  │
│                                                             │
│ POP Request 1: queue=orders, batch=10, consumer=group1     │
│ POP Request 2: queue=orders, batch=5, consumer=group2      │
│ POP Request 3: queue=events, batch=20, consumer=group1     │
│                                                             │
│ Combined: SELECT queen.pop_messages_batch_v2('[            │
│   {queue: orders, batch: 10, consumer: group1, idx: 0},    │
│   {queue: orders, batch: 5, consumer: group2, idx: 1},     │
│   {queue: events, batch: 20, consumer: group1, idx: 2}     │
│ ]')                                                         │
│                                                             │
│ SP returns: [{idx: 0, messages: [...]},                    │
│              {idx: 1, messages: [...]},                    │
│              {idx: 2, messages: [...]}]                    │
│                                                             │
│ Results routed back by idx                                  │
└────────────────────────────────────────────────────────────┘
```

### 2. POP with wait=true: Use Sidecar for Final DB Query ✅

Currently, Poll Intention Registry workers call AsyncQueueManager directly (blocking).
We will modify poll workers to use sidecar for the actual DB query:

```
┌──────────────────────────────────────────────────────────────┐
│ Current (Blocking):                                           │
│                                                               │
│ Poll Worker → AsyncQueueManager.pop_messages() → BLOCKS      │
│            → Gets connection from pool                        │
│            → Executes query                                   │
│            → Returns messages                                 │
│            → Sends HTTP response                              │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│ New (Async):                                                  │
│                                                               │
│ Poll Worker → Sidecar.submit(pop_request) → RETURNS IMMEDIATELY
│                                                               │
│ Sidecar Poller → Executes query                               │
│               → Adds to completed_responses_                  │
│                                                               │
│ Response Timer → Pops response                                │
│               → Routes to correct worker queue                │
│               → Worker sends HTTP response                    │
└──────────────────────────────────────────────────────────────┘
```

**File changes needed:**
- `server/src/services/poll_worker.cpp` - Submit to sidecar instead of direct call
- Poll workers need access to `global_sidecar_pool_ptr`

### 3. No Fallback to Blocking Mode ✅

This is new functionality. If sidecar is enabled, we use it exclusively.
If it fails, we return an error to the client (500 Internal Server Error).

```cpp
// NO fallback code like this:
if (sidecar_failed) {
    // DON'T: return blocking_mode_fallback();
    // DO: return error response
}
```

### 4. Operation-Type-Specific Metrics ✅

Add per-operation metrics to sidecar stats:

```cpp
struct Stats {
    // Existing
    size_t total_connections;
    size_t busy_connections;
    size_t pending_requests;
    size_t completed_responses;
    uint64_t total_queries;
    uint64_t total_query_time_us;
    
    // NEW: Per-operation stats
    struct OpStats {
        uint64_t count;
        uint64_t total_time_us;
        uint64_t items_processed;  // For batched ops
    };
    
    std::unordered_map<SidecarOpType, OpStats> op_stats;
    // op_stats[PUSH] = {count: 1000, total_time_us: 50000, items: 50000}
    // op_stats[ACK]  = {count: 2000, total_time_us: 20000, items: 10000}
    // op_stats[POP]  = {count: 500, total_time_us: 15000, items: 2500}
};
```

**Expose via metrics endpoint:**
```json
{
  "sidecar": {
    "connections": {"total": 85, "busy": 12},
    "queue": {"pending": 5, "completed": 0},
    "operations": {
      "push": {"count": 1000, "avg_ms": 50, "items": 50000},
      "pop": {"count": 500, "avg_ms": 30, "items": 2500},
      "ack": {"count": 2000, "avg_ms": 10, "items": 10000},
      "transaction": {"count": 100, "avg_ms": 45, "items": 300},
      "renew_lease": {"count": 5000, "avg_ms": 2, "items": 5000}
    }
  }
}
```

---

## Updated Stored Procedures (with Batching)

### pop_messages_batch_v2 (NEW - for micro-batching)

> **I/O Optimization**: Sort requests by (queue, partition) to minimize random disk I/O.
> Sequential access to the same partition's messages is faster than jumping around.

```sql
CREATE OR REPLACE FUNCTION queen.pop_messages_batch_v2(
    p_requests JSONB
    -- [{idx, queue, partition, consumerGroup, batch, leaseTime, subMode, subFrom}, ...]
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_req JSONB;
    v_results JSONB := '[]'::jsonb;
    v_pop_result JSONB;
BEGIN
    -- Sort by queue/partition to optimize I/O locality
    -- Accessing messages from the same partition sequentially is faster
    FOR v_req IN 
        SELECT value FROM jsonb_array_elements(p_requests)
        ORDER BY (value->>'queue'), (value->>'partition')
    LOOP
        -- Call single pop for each request
        v_pop_result := queen.pop_messages_v2(
            v_req->>'queue',
            v_req->>'partition',
            v_req->>'consumerGroup',
            (v_req->>'batch')::int,
            (v_req->>'leaseTime')::int,
            v_req->>'subMode',
            v_req->>'subFrom'
        );
        
        v_results := v_results || jsonb_build_object(
            'idx', (v_req->>'idx')::int,
            'result', v_pop_result
        );
    END LOOP;
    
    -- Sort results by original index for response routing
    SELECT COALESCE(jsonb_agg(item ORDER BY (item->>'idx')::int), '[]'::jsonb)
    INTO v_results
    FROM jsonb_array_elements(v_results) item;
    
    RETURN v_results;
END;
$$;
```

---

## Updated File Changes Summary

| File | Changes |
|------|---------|
| `include/queen/sidecar_db_pool.hpp` | Add `SidecarOpType` enum, per-op stats |
| `include/queen/config.hpp` | Add per-operation sidecar flags |
| `src/database/sidecar_db_pool.cpp` | Handle different op types, micro-batch ACK/POP |
| `src/acceptor_server.cpp` | Parse responses by operation type |
| `src/routes/pop.cpp` | Add sidecar path for wait=false |
| `src/routes/ack.cpp` | Add sidecar path |
| `src/routes/transactions.cpp` | Add sidecar path |
| `src/routes/leases.cpp` | Add sidecar path |
| `src/services/poll_worker.cpp` | **NEW**: Use sidecar for wait=true final query |
| `src/routes/metrics.cpp` | Add per-operation sidecar metrics |
| `migrations/004_pop_stored_procedure.sql` | NEW |
| `migrations/005_pop_batch_stored_procedure.sql` | NEW (for micro-batching) |
| `migrations/006_ack_stored_procedure.sql` | NEW |
| `migrations/007_transaction_stored_procedure.sql` | NEW |
| `migrations/008_renew_lease_stored_procedure.sql` | NEW |

---

## Updated Estimated Effort

| Phase | Effort | Description |
|-------|--------|-------------|
| Phase 1 | 3 hours | Infrastructure (types, config, metrics) |
| Phase 2 | 6 hours | Stored procedures (incl. batch versions) |
| Phase 3 | 4 hours | Route modifications |
| Phase 4 | 3 hours | Sidecar modifications (micro-batching) |
| Phase 5 | 2 hours | Poll worker integration (wait=true) |
| Phase 6 | 4 hours | Testing |
| Phase 7 | 2 hours | Rollout |

**Total: ~24 hours**

