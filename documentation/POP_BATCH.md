# POP Batch Operations Plan

## Executive Summary

This document outlines the implementation of **batched POP operations** to improve throughput and reduce database round-trips, plus a **lightweight pre-check query** for fallback polling optimization.

### Goals
1. **Batch multiple POP requests** into a single database call
2. **Maintain lease integrity** - each consumer gets its own unique lease
3. **Optimize fallback polling** with lightweight availability checks
4. **Leverage partition_lookup** table for fast partition discovery

---

## Current Architecture

### Single POP Flow
```
Consumer A → POP → pop_messages_v2() → Lease + Messages
Consumer B → POP → pop_messages_v2() → Lease + Messages  (separate transaction)
Consumer C → POP → pop_messages_v2() → Lease + Messages  (separate transaction)
```

Each POP = 1 database round-trip + 1 transaction.

### Why POP Isn't Batched Today

The current `pop_messages_v2` stored procedure:
1. Finds ONE available partition
2. Acquires lease for that partition
3. Fetches messages for that partition

Multiple POPs in the same transaction would conflict:
```sql
-- Consumer A: Finds partition P1, acquires lease
-- Consumer B: Finds partition P1 (same!), lease already taken → returns empty
```

### The Lease Constraint

```sql
-- partition_consumers: UNIQUE(partition_id, consumer_group)
-- Only ONE consumer from a group can have a lease on a partition at a time
```

---

## Part 1: Batched POP Implementation

### Core Insight

We can batch POPs when they target **different (partition, consumer_group) combinations**:

| Scenario | Batchable? | Reason |
|----------|------------|--------|
| Same queue, different consumer groups | ✅ YES | Different lease targets |
| Same queue, same group, different partitions | ✅ YES | Different partitions |
| Same queue, same group, specific same partition | ❌ NO | Same lease target |
| Same queue, same group, any partition (`*`) | ✅ YES | Can allocate different partitions |

### Algorithm Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                    BATCHED POP ALGORITHM                            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  INPUT: Array of POP requests                                       │
│  [                                                                  │
│    {request_id: "R1", queue: "orders", partition: "*", group: "G1"},│
│    {request_id: "R2", queue: "orders", partition: "*", group: "G1"},│
│    {request_id: "R3", queue: "orders", partition: "*", group: "G2"},│
│  ]                                                                  │
│                                                                     │
│  STEP 1: Partition Allocation                                       │
│  ─────────────────────────────                                      │
│  Find available partitions using partition_lookup.                  │
│  Rank them per (queue, consumer_group).                             │
│  Assign: R1→P1, R2→P2, R3→P1 (G2 can use P1, different group)       │
│                                                                     │
│  STEP 2: Acquire Leases (Single UPDATE)                             │
│  ──────────────────────────────────────                             │
│  UPDATE partition_consumers                                         │
│  SET lease_expires_at = NOW() + interval                            │
│  WHERE (partition_id, consumer_group) IN allocated_pairs            │
│  FOR UPDATE SKIP LOCKED                                             │
│                                                                     │
│  STEP 3: Fetch Messages (Single Query)                              │
│  ─────────────────────────────────────                              │
│  SELECT * FROM messages                                             │
│  WHERE partition_id IN (acquired_partitions)                        │
│  ORDER BY created_at, id                                            │
│                                                                     │
│  STEP 4: Return Results                                             │
│  ─────────────────────────────                                      │
│  {                                                                  │
│    "R1": {lease_id: "L1", messages: [...]},                         │
│    "R2": {lease_id: "L2", messages: [...]},                         │
│    "R3": {lease_id: "L3", messages: [...]}                          │
│  }                                                                  │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### New Stored Procedure: `pop_messages_batch_v2`

```sql
-- File: server/migrations/010_pop_batch_stored_procedure.sql

CREATE OR REPLACE FUNCTION queen.pop_messages_batch_v2(
    p_requests JSONB  -- Array of {request_id, queue_name, partition_name, consumer_group, batch_size, lease_time_seconds}
) RETURNS JSONB AS $$
DECLARE
    v_result JSONB := '{}'::jsonb;
    v_request JSONB;
    v_allocated JSONB := '[]'::jsonb;
    v_effective_lease_time INT;
BEGIN
    -- ============================================================
    -- PHASE 1: Parse requests and find available partitions
    -- ============================================================
    
    -- Create temp table for requests with allocated partitions
    CREATE TEMP TABLE batch_requests ON COMMIT DROP AS
    SELECT 
        (r.value->>'request_id')::TEXT AS request_id,
        (r.value->>'queue_name')::TEXT AS queue_name,
        NULLIF(r.value->>'partition_name', '*') AS partition_name,  -- NULL = any partition
        (r.value->>'consumer_group')::TEXT AS consumer_group,
        COALESCE((r.value->>'batch_size')::INT, 1) AS batch_size,
        COALESCE((r.value->>'lease_time_seconds')::INT, 0) AS lease_time_seconds,
        NULL::UUID AS allocated_partition_id,
        NULL::UUID AS queue_id,
        0 AS request_rank
    FROM jsonb_array_elements(p_requests) r;
    
    -- ============================================================
    -- PHASE 2: Allocate partitions to requests
    -- ============================================================
    
    -- For requests with specific partition, validate and assign directly
    UPDATE batch_requests br
    SET allocated_partition_id = p.id,
        queue_id = q.id
    FROM queen.partitions p
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE br.partition_name IS NOT NULL
      AND q.name = br.queue_name
      AND p.name = br.partition_name;
    
    -- For wildcard requests, find available partitions using partition_lookup
    -- Rank partitions per (queue, consumer_group) to distribute fairly
    WITH available_partitions AS (
        SELECT 
            pl.partition_id,
            pl.queue_name,
            p.name AS partition_name,
            q.id AS queue_id,
            q.lease_time AS queue_lease_time,
            pc.consumer_group,
            pc.last_consumed_created_at,
            pc.last_consumed_id,
            ROW_NUMBER() OVER (
                PARTITION BY pl.queue_name, COALESCE(pc.consumer_group, '__NEW__')
                ORDER BY 
                    pc.last_consumed_at ASC NULLS FIRST,  -- Fairness: least recently consumed first
                    pl.last_message_created_at DESC        -- Then: partitions with newer messages
            ) AS partition_rank
        FROM queen.partition_lookup pl
        JOIN queen.partitions p ON p.id = pl.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        LEFT JOIN queen.partition_consumers pc 
            ON pc.partition_id = pl.partition_id
        WHERE 
            -- Lease must be free or expired
            (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
            -- Has pending messages
            AND (pc.last_consumed_created_at IS NULL 
                 OR pl.last_message_created_at > pc.last_consumed_created_at
                 OR (pl.last_message_created_at = pc.last_consumed_created_at 
                     AND pl.last_message_id > pc.last_consumed_id))
    ),
    -- Rank wildcard requests per (queue, consumer_group)
    ranked_requests AS (
        SELECT 
            br.request_id,
            br.queue_name,
            br.consumer_group,
            br.batch_size,
            br.lease_time_seconds,
            ROW_NUMBER() OVER (
                PARTITION BY br.queue_name, br.consumer_group
                ORDER BY br.request_id  -- Deterministic ordering
            ) AS request_rank
        FROM batch_requests br
        WHERE br.partition_name IS NULL  -- Wildcard requests only
          AND br.allocated_partition_id IS NULL
    )
    -- Match requests to partitions by rank
    UPDATE batch_requests br
    SET allocated_partition_id = ap.partition_id,
        queue_id = ap.queue_id,
        request_rank = rr.request_rank
    FROM ranked_requests rr
    JOIN available_partitions ap 
        ON ap.queue_name = rr.queue_name
        AND COALESCE(ap.consumer_group, '__NEW__') = COALESCE(rr.consumer_group, '__NEW__')
        AND ap.partition_rank = rr.request_rank
    WHERE br.request_id = rr.request_id;
    
    -- ============================================================
    -- PHASE 3: Acquire leases (single UPDATE with SKIP LOCKED)
    -- ============================================================
    
    -- Ensure partition_consumers entries exist
    INSERT INTO queen.partition_consumers (partition_id, consumer_group)
    SELECT DISTINCT br.allocated_partition_id, br.consumer_group
    FROM batch_requests br
    WHERE br.allocated_partition_id IS NOT NULL
    ON CONFLICT (partition_id, consumer_group) DO NOTHING;
    
    -- Acquire leases atomically
    WITH lease_acquisition AS (
        UPDATE queen.partition_consumers pc
        SET 
            lease_acquired_at = NOW(),
            lease_expires_at = NOW() + (
                COALESCE(
                    NULLIF((SELECT lease_time FROM queen.queues WHERE id = br.queue_id), 0),
                    NULLIF(br.lease_time_seconds, 0),
                    300
                ) * INTERVAL '1 second'
            ),
            worker_id = gen_random_uuid()::TEXT,
            batch_size = br.batch_size,
            acked_count = 0,
            batch_retry_count = COALESCE(pc.batch_retry_count, 0)
        FROM batch_requests br
        WHERE pc.partition_id = br.allocated_partition_id
          AND pc.consumer_group = br.consumer_group
          AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
        RETURNING 
            pc.partition_id,
            pc.consumer_group,
            pc.lease_expires_at,
            pc.worker_id AS lease_id,
            pc.last_consumed_id,
            pc.last_consumed_created_at
    )
    -- Store acquired leases
    UPDATE batch_requests br
    SET allocated_partition_id = la.partition_id  -- Confirm allocation
    FROM lease_acquisition la
    WHERE br.allocated_partition_id = la.partition_id
      AND br.consumer_group = la.consumer_group;
    
    -- ============================================================
    -- PHASE 4: Fetch messages for all acquired partitions
    -- ============================================================
    
    WITH acquired_partitions AS (
        SELECT 
            br.request_id,
            br.allocated_partition_id,
            br.consumer_group,
            br.batch_size,
            pc.lease_expires_at,
            pc.worker_id AS lease_id,
            pc.last_consumed_id,
            pc.last_consumed_created_at
        FROM batch_requests br
        JOIN queen.partition_consumers pc 
            ON pc.partition_id = br.allocated_partition_id
            AND pc.consumer_group = br.consumer_group
        WHERE br.allocated_partition_id IS NOT NULL
          AND pc.lease_expires_at > NOW()  -- We acquired the lease
    ),
    messages_per_partition AS (
        SELECT 
            ap.request_id,
            ap.lease_id,
            ap.lease_expires_at,
            m.id AS message_id,
            m.payload,
            m.created_at,
            m.trace_id,
            q.name AS queue_name,
            p.name AS partition_name,
            ROW_NUMBER() OVER (
                PARTITION BY ap.request_id 
                ORDER BY m.created_at ASC, m.id ASC
            ) AS msg_rank
        FROM acquired_partitions ap
        JOIN queen.partitions p ON p.id = ap.allocated_partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        JOIN queen.messages m ON m.partition_id = ap.allocated_partition_id
        WHERE (ap.last_consumed_created_at IS NULL 
               OR m.created_at > ap.last_consumed_created_at
               OR (m.created_at = ap.last_consumed_created_at AND m.id > ap.last_consumed_id))
    ),
    limited_messages AS (
        SELECT *
        FROM messages_per_partition mpp
        WHERE mpp.msg_rank <= (
            SELECT batch_size FROM acquired_partitions WHERE request_id = mpp.request_id
        )
    )
    -- Build result JSON
    SELECT jsonb_object_agg(
        request_id,
        jsonb_build_object(
            'leaseId', lease_id,
            'leaseExpiresAt', lease_expires_at,
            'queue', queue_name,
            'partition', partition_name,
            'messages', COALESCE(messages, '[]'::jsonb)
        )
    )
    INTO v_result
    FROM (
        SELECT 
            ap.request_id,
            ap.lease_id::TEXT,
            ap.lease_expires_at,
            (SELECT name FROM queen.queues WHERE id = (
                SELECT queue_id FROM queen.partitions WHERE id = ap.allocated_partition_id
            )) AS queue_name,
            (SELECT name FROM queen.partitions WHERE id = ap.allocated_partition_id) AS partition_name,
            (
                SELECT jsonb_agg(
                    jsonb_build_object(
                        'messageId', lm.message_id,
                        'payload', lm.payload,
                        'createdAt', lm.created_at,
                        'traceId', lm.trace_id
                    ) ORDER BY lm.msg_rank
                )
                FROM limited_messages lm
                WHERE lm.request_id = ap.request_id
            ) AS messages
        FROM acquired_partitions ap
    ) results;
    
    -- Add empty results for requests that didn't get a partition
    FOR v_request IN 
        SELECT jsonb_build_object(
            'request_id', request_id,
            'result', jsonb_build_object('leaseId', NULL, 'messages', '[]'::jsonb)
        )
        FROM batch_requests
        WHERE allocated_partition_id IS NULL
    LOOP
        v_result := v_result || jsonb_build_object(
            v_request->>'request_id',
            v_request->'result'
        );
    END LOOP;
    
    RETURN v_result;
END;
$$ LANGUAGE plpgsql;
```

### Sidecar Integration

#### File: `server/include/queen/sidecar_db_pool.hpp`

```cpp
// Add to SidecarOpType enum
enum class SidecarOpType {
    PUSH,
    POP,
    POP_BATCH,      // NEW: Batched POP operations
    POP_WAIT,
    ACK,
    ACK_BATCH,
    TRANSACTION,
    RENEW_LEASE
};
```

#### File: `server/src/database/sidecar_db_pool.cpp`

```cpp
// Update is_batchable_op()
bool is_batchable_op(SidecarOpType op) {
    switch (op) {
        case SidecarOpType::PUSH:
        case SidecarOpType::ACK:
        case SidecarOpType::ACK_BATCH:
        case SidecarOpType::RENEW_LEASE:
        case SidecarOpType::POP_BATCH:  // NEW: POP is now batchable!
            return true;
        case SidecarOpType::POP:        // Single POP (specific partition) - not batched
        case SidecarOpType::POP_WAIT:
        case SidecarOpType::TRANSACTION:
            return false;
    }
    return false;
}

// Update get_batched_sql() for POP_BATCH
std::string get_batched_sql(SidecarOpType op, const std::vector<SidecarRequest>& requests) {
    switch (op) {
        case SidecarOpType::POP_BATCH: {
            // Build JSON array of POP requests
            nlohmann::json requests_array = nlohmann::json::array();
            for (const auto& req : requests) {
                requests_array.push_back({
                    {"request_id", req.request_id},
                    {"queue_name", req.queue_name},
                    {"partition_name", req.partition_name},
                    {"consumer_group", req.consumer_group},
                    {"batch_size", req.batch_size},
                    {"lease_time_seconds", 0}  // Use queue default
                });
            }
            return "SELECT queen.pop_messages_batch_v2($1::jsonb)";
        }
        // ... other cases
    }
}
```

### Route Changes

#### File: `server/src/routes/pop.cpp`

```cpp
// For wildcard partition POPs, use POP_BATCH
if (partition_name == "*" || partition_name.empty()) {
    SidecarRequest sidecar_req;
    sidecar_req.op_type = SidecarOpType::POP_BATCH;  // NEW: Use batch-capable type
    sidecar_req.request_id = request_id;
    sidecar_req.queue_name = queue_name;
    sidecar_req.partition_name = "*";
    sidecar_req.consumer_group = consumer_group;
    sidecar_req.batch_size = batch_size;
    // ... rest of setup
    ctx.sidecar->submit(std::move(sidecar_req));
} else {
    // Specific partition - use regular POP (can't batch with others targeting same partition)
    SidecarRequest sidecar_req;
    sidecar_req.op_type = SidecarOpType::POP;
    // ...
}
```

### Response Handling

```cpp
// In acceptor_server.cpp sidecar_callback
case SidecarOpType::POP_BATCH: {
    // Result is keyed by request_id
    auto result_json = nlohmann::json::parse(resp.result_json);
    auto request_result = result_json[resp.request_id];
    
    if (request_result["leaseId"].is_null()) {
        status = 204;  // No messages
    } else {
        status = 200;
        // Format response for client
    }
    break;
}
```

---

## Part 2: Lightweight Pre-Check Query

### Purpose

When doing fallback polling (no PUSH notification received), avoid executing full `pop_messages_v2` if no messages are available.

### New Function: `has_pending_messages`

```sql
-- File: server/migrations/011_has_pending_messages.sql

CREATE OR REPLACE FUNCTION queen.has_pending_messages(
    p_queue_name VARCHAR,
    p_partition_name VARCHAR,  -- NULL or '*' = any partition
    p_consumer_group VARCHAR
) RETURNS BOOLEAN AS $$
BEGIN
    -- Fast check using partition_lookup
    RETURN EXISTS (
        SELECT 1 
        FROM queen.partition_lookup pl
        LEFT JOIN queen.partition_consumers pc 
            ON pc.partition_id = pl.partition_id
            AND pc.consumer_group = p_consumer_group
        WHERE pl.queue_name = p_queue_name
          -- Partition filter (if specific partition requested)
          AND (p_partition_name IS NULL 
               OR p_partition_name = '*'
               OR pl.partition_id = (
                   SELECT p.id 
                   FROM queen.partitions p
                   JOIN queen.queues q ON p.queue_id = q.id
                   WHERE q.name = p_queue_name AND p.name = p_partition_name
               ))
          -- Lease must be free or expired
          AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
          -- Has pending messages (newer than cursor)
          AND (pc.last_consumed_created_at IS NULL 
               OR pl.last_message_created_at > pc.last_consumed_created_at
               OR (pl.last_message_created_at = pc.last_consumed_created_at 
                   AND pl.last_message_id > pc.last_consumed_id))
        LIMIT 1
    );
END;
$$ LANGUAGE plpgsql STABLE;

-- Create index for fast lookup
CREATE INDEX IF NOT EXISTS idx_partition_lookup_queue_timestamp 
    ON queen.partition_lookup(queue_name, last_message_created_at DESC);
```

### Sidecar Integration

#### New Operation Type

```cpp
enum class SidecarOpType {
    // ... existing types ...
    HAS_PENDING,    // NEW: Lightweight check
};
```

#### Usage in POP_WAIT Processing

```cpp
// In sidecar_db_pool.cpp process_waiting_queue()

void SidecarDbPool::process_waiting_queue() {
    // ... existing code ...
    
    for (auto& req : to_process) {
        std::string group_key = make_group_key(req.queue_name, req.partition_name, req.consumer_group);
        
        // Check backoff
        if (global_shared_state && !global_shared_state->should_check_group(group_key)) {
            // Not due yet - but do lightweight check if it's been a while
            auto interval = global_shared_state->get_group_interval(group_key);
            if (interval >= std::chrono::milliseconds(500)) {
                // Do lightweight check before full query
                if (!check_has_pending(req.queue_name, req.partition_name, req.consumer_group)) {
                    // No messages - increase backoff, re-queue
                    req.next_check = now + interval;
                    std::lock_guard<std::mutex> lock(waiting_mutex_);
                    waiting_requests_.push_back(std::move(req));
                    continue;
                }
            }
            // Messages exist or short interval - proceed with full query
        }
        // ... rest of processing
    }
}

bool SidecarDbPool::check_has_pending(
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group
) {
    // Use a dedicated connection for this quick check
    auto conn = get_quick_check_connection();
    if (!conn) return true;  // Assume messages exist if can't check
    
    std::string sql = "SELECT queen.has_pending_messages($1, $2, $3)";
    const char* params[3] = {
        queue_name.c_str(),
        partition_name.empty() ? nullptr : partition_name.c_str(),
        consumer_group.c_str()
    };
    
    auto result = PQexecParams(conn, sql.c_str(), 3, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        PQclear(result);
        return true;  // Assume messages exist on error
    }
    
    bool has_messages = (PQgetvalue(result, 0, 0)[0] == 't');
    PQclear(result);
    return has_messages;
}
```

---

## Part 3: Flow Diagrams

### Batched POP Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                      BATCHED POP FLOW                               │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Worker 1         Worker 2         Worker 3         Sidecar         │
│     │                │                │                │            │
│     │ POP(q1,*,G1)   │                │                │            │
│     │───────────────────────────────────────────────>│             │
│     │                │ POP(q1,*,G1)   │                │            │
│     │                │───────────────────────────────>│             │
│     │                │                │ POP(q1,*,G2)   │            │
│     │                │                │───────────────>│            │
│     │                │                │                │            │
│     │                │                │    ┌───────────┴───────────┐│
│     │                │                │    │ Micro-batch window    ││
│     │                │                │    │ (5ms)                 ││
│     │                │                │    └───────────┬───────────┘│
│     │                │                │                │            │
│     │                │                │    ┌───────────┴───────────┐│
│     │                │                │    │ pop_messages_batch_v2 ││
│     │                │                │    │ - Allocate partitions ││
│     │                │                │    │ - Acquire 3 leases    ││
│     │                │                │    │ - Fetch all messages  ││
│     │                │                │    │ (SINGLE DB CALL)      ││
│     │                │                │    └───────────┬───────────┘│
│     │                │                │                │            │
│     │<───────────────────────────────────────────────│ R1 result   │
│     │                │<───────────────────────────────│ R2 result   │
│     │                │                │<──────────────│ R3 result   │
│     │                │                │                │            │
└─────────────────────────────────────────────────────────────────────┘
```

### Lightweight Check Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                  FALLBACK POLLING WITH PRE-CHECK                    │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  POP_WAIT Request                                                   │
│       │                                                             │
│       ▼                                                             │
│  ┌─────────────────┐                                                │
│  │ Backoff expired?│──NO──> Re-queue with next_check                │
│  └────────┬────────┘                                                │
│           │ YES                                                     │
│           ▼                                                         │
│  ┌─────────────────────────┐                                        │
│  │ Interval >= 500ms?      │──NO──> Execute full pop_messages_v2    │
│  └────────┬────────────────┘                                        │
│           │ YES                                                     │
│           ▼                                                         │
│  ┌─────────────────────────┐                                        │
│  │ has_pending_messages()  │  (FAST: ~0.1ms)                        │
│  │ Check partition_lookup  │                                        │
│  └────────┬────────────────┘                                        │
│           │                                                         │
│      ┌────┴────┐                                                    │
│      │         │                                                    │
│   FALSE      TRUE                                                   │
│      │         │                                                    │
│      ▼         ▼                                                    │
│  ┌─────────┐  ┌─────────────────┐                                   │
│  │ Skip    │  │ Execute full    │                                   │
│  │ full    │  │ pop_messages_v2 │                                   │
│  │ query   │  └────────┬────────┘                                   │
│  │         │           │                                            │
│  │ Increase│      ┌────┴────┐                                       │
│  │ backoff │      │         │                                       │
│  └────┬────┘   EMPTY     MESSAGES                                   │
│       │         │         │                                         │
│       ▼         ▼         ▼                                         │
│  Re-queue    Re-queue   Deliver                                     │
│  (longer     (backoff)  response                                    │
│   interval)                                                         │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Part 4: Performance Analysis

### Current (Single POP)

| Metric | Value |
|--------|-------|
| DB round-trips per POP | 1 |
| Transactions per POP | 1 |
| For 100 concurrent POPs | 100 round-trips |

### With Batched POP

| Metric | Value |
|--------|-------|
| DB round-trips per batch | 1 |
| Transactions per batch | 1 |
| For 100 concurrent POPs (10 batches of 10) | 10 round-trips |
| **Reduction** | **90%** |

### Lightweight Pre-Check

| Query | Estimated Time |
|-------|---------------|
| Full `pop_messages_v2` (with messages) | ~5-10ms |
| Full `pop_messages_v2` (no messages) | ~2-5ms |
| `has_pending_messages` (fast check) | ~0.1-0.5ms |

**Savings**: When no messages available, avoid ~2-5ms query, do ~0.1ms check instead.

---

## Part 5: Implementation Checklist

### Phase 1: Lightweight Pre-Check (Low Risk)

- [ ] Create migration `011_has_pending_messages.sql`
- [ ] Add `HAS_PENDING` operation type to `SidecarOpType`
- [ ] Implement `check_has_pending()` in `sidecar_db_pool.cpp`
- [ ] Integrate into `process_waiting_queue()` for fallback polling
- [ ] Add metrics: `has_pending_checks`, `has_pending_hits`, `has_pending_misses`
- [ ] Test: Verify reduced DB load during idle periods

### Phase 2: Batched POP (Medium Risk)

- [ ] Create migration `010_pop_batch_stored_procedure.sql`
- [ ] Add `POP_BATCH` operation type to `SidecarOpType`
- [ ] Update `is_batchable_op()` to return `true` for `POP_BATCH`
- [ ] Implement `get_batched_sql()` for `POP_BATCH`
- [ ] Modify `pop.cpp` to use `POP_BATCH` for wildcard partitions
- [ ] Update response handling in `acceptor_server.cpp`
- [ ] Add metrics: `pop_batch_size`, `pop_batch_count`
- [ ] Test: Multiple consumers, same queue, verify lease uniqueness
- [ ] Test: Race conditions, verify no duplicate leases
- [ ] Test: Mixed specific/wildcard partition requests

### Phase 3: Integration Testing

- [ ] Benchmark: Before/after DB round-trips
- [ ] Benchmark: Latency under high concurrency
- [ ] Test: Cluster mode with multiple Queen instances
- [ ] Test: Consumer group behavior with batched POPs
- [ ] Monitor: PostgreSQL connection usage

---

## Part 6: Rollback Plan

### Feature Flags

```cpp
// In config.hpp
struct QueueConfig {
    // ...
    bool enable_pop_batching = false;       // Enable batched POP
    bool enable_pending_check = false;      // Enable lightweight pre-check
};
```

### Gradual Rollout

1. Deploy with flags disabled
2. Enable `enable_pending_check` first (low risk)
3. Monitor for 24h
4. Enable `enable_pop_batching` in staging
5. Monitor for 24h
6. Enable in production

### Rollback Trigger

- Lease integrity issues (duplicate leases)
- Increased error rates
- Higher latency than expected
- Connection pool exhaustion

---

## Appendix: partition_lookup Table Usage

The `partition_lookup` table is key to both optimizations:

```sql
-- Updated on every PUSH (via trigger)
partition_lookup:
┌─────────────┬──────────────┬──────────────────┬─────────────────────────┐
│ queue_name  │ partition_id │ last_message_id  │ last_message_created_at │
├─────────────┼──────────────┼──────────────────┼─────────────────────────┤
│ orders      │ P1           │ msg-999          │ 2024-01-01 12:05:00     │
│ orders      │ P2           │ msg-850          │ 2024-01-01 12:03:00     │
│ orders      │ P3           │ msg-200          │ 2024-01-01 11:00:00     │
└─────────────┴──────────────┴──────────────────┴─────────────────────────┘
```

### How It Helps

1. **Fast partition discovery**: No need to scan `messages` table
2. **Efficient availability check**: Compare `last_message_created_at` with consumer cursor
3. **Fair allocation**: Order by `last_message_created_at` for oldest-first

### Required Indexes

```sql
-- Already created in schema
CREATE INDEX idx_partition_lookup_queue_name ON partition_lookup(queue_name);
CREATE INDEX idx_partition_lookup_timestamp ON partition_lookup(last_message_created_at DESC);

-- New composite index for batch queries
CREATE INDEX idx_partition_lookup_queue_timestamp 
    ON partition_lookup(queue_name, last_message_created_at DESC);
```

