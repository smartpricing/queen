# Fix: Eliminate push-pop lock contention in pop_unified_batch

## The Problem

Queen MQ consumers were severely degraded during push activity in production:
- **Capable of 37 msg/s** when no pushes are happening
- **Drops to 2-5 msg/s** during push bursts (7-18x degradation)
- Consumers immediately recover when push load drops

Production config: 47,805 partitions, 139,181 consumer rows, 14 queues, 2 workers, ~15-20 push msg/s sustained.

## Root Cause

The push and pop paths both locked the `partition_lookup` table:

**Push path**: Every message INSERT fires a statement-level trigger that does `INSERT ... ON CONFLICT DO UPDATE` on `partition_lookup`, taking **exclusive row locks** held until the push transaction commits.

**Pop path**: The wildcard discovery query used `FOR UPDATE OF pl SKIP LOCKED` on the same `partition_lookup` table. `SKIP LOCKED` caused pop to **skip any partition currently being pushed to**, since the push trigger holds row locks.

During a push burst, many `partition_lookup` rows are locked simultaneously by push transactions. Pop's `SKIP LOCKED` skips all of them, returning `no_available_partition`. Consumers back off, and a positive feedback loop forms: push burst → consumer delays → backlog grows.

## Solution

Two changes to `lib/schema/procedures/002_pop_unified.sql`:

### Change 1: Remove `FOR UPDATE OF pl SKIP LOCKED` from discovery queries

This eliminates all row-level lock contention between push and pop. The discovery query becomes lock-free — it reads `partition_lookup` without taking any locks, so push triggers never block it.

### Change 2: Add `pg_try_advisory_xact_lock` before INSERT/UPDATE on `partition_consumers`

Without `FOR UPDATE SKIP LOCKED`, concurrent pop transactions can discover the same partitions and INSERT into `partition_consumers` in different order, causing classic INSERT deadlocks. The non-blocking advisory lock solves this:

- **Cannot deadlock**: `pg_try_advisory_xact_lock` returns false instantly if held (never waits)
- **Distributes partitions**: Concurrent pops skip each other's partitions (same behavior as SKIP LOCKED)
- **Push-safe**: Advisory locks are independent of row locks — push triggers don't conflict
- **Consistent with ack**: Uses the same lock key as `ack_messages_v2` for pop/ack serialization

## Iteration History

1. **First attempt**: `FOR UPDATE OF pc SKIP LOCKED` (lock `partition_consumers` instead of `partition_lookup`). Failed at runtime — PostgreSQL forbids `FOR UPDATE` on the nullable side of a `LEFT JOIN`.

2. **Second attempt**: Remove `FOR UPDATE` entirely. Passed at low concurrency. At 20+ concurrent workers, deadlocks appeared on `partition_consumers` INSERT — two transactions discovering overlapping partitions in different order.

3. **Final solution**: Remove `FOR UPDATE` + add `pg_try_advisory_xact_lock`. Eliminates both push contention and INSERT deadlocks.

## Before and After

### BEFORE: Wildcard discovery in `pop_unified_batch`

```sql
IF v_is_wildcard THEN
    -- WILDCARD: Discover available partition with SKIP LOCKED
    SELECT 
        pl.partition_id, p.name, q.id,
        q.lease_time, q.window_buffer, q.delayed_processing,
        pl.last_message_created_at
    INTO v_partition_id, v_partition_name, v_queue_id, v_lease_time,
         v_window_buffer, v_delayed_processing, v_partition_last_msg_ts
    FROM queen.partition_lookup pl
    JOIN queen.partitions p ON p.id = pl.partition_id
    JOIN queen.queues q ON q.id = p.queue_id
    LEFT JOIN queen.partition_consumers pc 
        ON pc.partition_id = pl.partition_id 
        AND pc.consumer_group = v_req.consumer_group
    WHERE pl.queue_name = v_req.queue_name
      AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
      AND (pc.last_consumed_created_at IS NULL 
           OR pl.last_message_created_at > pc.last_consumed_created_at
           OR (pl.last_message_created_at = pc.last_consumed_created_at 
               AND pl.last_message_id > pc.last_consumed_id))
      AND (q.window_buffer IS NULL OR q.window_buffer = 0
           OR pl.last_message_created_at <= v_now - (q.window_buffer || ' seconds')::interval)
    ORDER BY pc.last_consumed_at ASC NULLS LAST
    LIMIT 1
    FOR UPDATE OF pl SKIP LOCKED;   -- ← Contention point: conflicts with push trigger

    -- ... handle NULL result ...
END IF;

-- COMMON PATH: Lease acquisition
INSERT INTO queen.partition_consumers (partition_id, consumer_group)
VALUES (v_partition_id, v_req.consumer_group)
ON CONFLICT (partition_id, consumer_group) DO NOTHING;   -- ← Deadlock-prone without SKIP LOCKED

UPDATE queen.partition_consumers pc
SET lease_acquired_at = v_now,
    lease_expires_at = v_now + (v_effective_lease * INTERVAL '1 second'),
    worker_id = v_req.worker_id, ...
WHERE pc.partition_id = v_partition_id
  AND pc.consumer_group = v_req.consumer_group
  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
RETURNING pc.last_consumed_id, pc.last_consumed_created_at
INTO v_cursor_id, v_cursor_ts;
```

### AFTER: Lock-free discovery + advisory lock serialization

```sql
IF v_is_wildcard THEN
    -- WILDCARD: Discover available partition (lock-free discovery)
    SELECT 
        pl.partition_id, p.name, q.id,
        q.lease_time, q.window_buffer, q.delayed_processing,
        pl.last_message_created_at
    INTO v_partition_id, v_partition_name, v_queue_id, v_lease_time,
         v_window_buffer, v_delayed_processing, v_partition_last_msg_ts
    FROM queen.partition_lookup pl
    JOIN queen.partitions p ON p.id = pl.partition_id
    JOIN queen.queues q ON q.id = p.queue_id
    LEFT JOIN queen.partition_consumers pc 
        ON pc.partition_id = pl.partition_id 
        AND pc.consumer_group = v_req.consumer_group
    WHERE pl.queue_name = v_req.queue_name
      AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
      AND (pc.last_consumed_created_at IS NULL 
           OR pl.last_message_created_at > pc.last_consumed_created_at
           OR (pl.last_message_created_at = pc.last_consumed_created_at 
               AND pl.last_message_id > pc.last_consumed_id))
      AND (q.window_buffer IS NULL OR q.window_buffer = 0
           OR pl.last_message_created_at <= v_now - (q.window_buffer || ' seconds')::interval)
    ORDER BY pc.last_consumed_at ASC NULLS LAST
    LIMIT 1;                         -- ← No FOR UPDATE: push triggers can't block this

    -- ... handle NULL result ...
END IF;

-- COMMON PATH: Advisory lock + lease acquisition

-- Non-blocking advisory lock: prevents INSERT deadlocks and provides
-- SKIP LOCKED-like partition distribution. Same key as ack_messages_v2.
IF NOT pg_try_advisory_xact_lock(
    ('x' || substr(md5(v_partition_id::text || v_req.consumer_group), 1, 16))::bit(64)::bigint
) THEN
    -- Another transaction is processing this partition, skip it
    v_result := jsonb_build_object('idx', v_req.idx,
        'result', jsonb_build_object('success', false,
            'error', CASE WHEN v_is_wildcard THEN 'partition_busy' ELSE 'lease_held' END,
            'messages', '[]'::jsonb));
    v_results := v_results || v_result;
    CONTINUE;
END IF;

INSERT INTO queen.partition_consumers (partition_id, consumer_group)
VALUES (v_partition_id, v_req.consumer_group)
ON CONFLICT (partition_id, consumer_group) DO NOTHING;   -- ← Safe: advisory lock serializes

UPDATE queen.partition_consumers pc
SET lease_acquired_at = v_now,
    lease_expires_at = v_now + (v_effective_lease * INTERVAL '1 second'),
    worker_id = v_req.worker_id, ...
WHERE pc.partition_id = v_partition_id
  AND pc.consumer_group = v_req.consumer_group
  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
RETURNING pc.last_consumed_id, pc.last_consumed_created_at
INTO v_cursor_id, v_cursor_ts;
```

## Full Diff

```diff
--- a/lib/schema/procedures/002_pop_unified.sql
+++ b/lib/schema/procedures/002_pop_unified.sql

 ## pop_specific_batch: Added advisory lock before INSERT

-        -- STEP 2: Ensure consumer row exists & try to acquire lease (atomic)
+        -- STEP 2: Advisory lock + ensure consumer row exists + acquire lease
+        IF NOT pg_try_advisory_xact_lock(
+            ('x' || substr(md5(v_partition_id::text || v_req.consumer_group), 1, 16))::bit(64)::bigint
+        ) THEN
+            -- return lease_held error, CONTINUE
+        END IF;
+
         INSERT INTO queen.partition_consumers ...

 ## pop_discover_batch: Removed FOR UPDATE, added advisory lock

         ORDER BY pc.last_consumed_at ASC NULLS LAST
-        LIMIT 1
-        FOR UPDATE OF pl SKIP LOCKED;
+        LIMIT 1;

-        -- STEP 2: Ensure consumer row exists
+        -- STEP 2: Advisory lock + ensure consumer row exists
+        IF NOT pg_try_advisory_xact_lock(
+            ('x' || substr(md5(v_partition_id::text || v_req.consumer_group), 1, 16))::bit(64)::bigint
+        ) THEN
+            -- return partition_busy error, CONTINUE
+        END IF;
+
         INSERT INTO queen.partition_consumers ...

 ## pop_unified_batch: Removed FOR UPDATE, added advisory lock

             ORDER BY pc.last_consumed_at ASC NULLS LAST
-            LIMIT 1
-            FOR UPDATE OF pl SKIP LOCKED;
+            LIMIT 1;

         -- COMMON PATH:
+        IF NOT pg_try_advisory_xact_lock(
+            ('x' || substr(md5(v_partition_id::text || v_req.consumer_group), 1, 16))::bit(64)::bigint
+        ) THEN
+            -- return partition_busy/lease_held error, CONTINUE
+        END IF;
+
         INSERT INTO queen.partition_consumers ...
```

## Validation

Stress test (`examples/test-no-skip-locked.js`) with 100K messages, 200 partitions, 50 concurrent workers, 2 consumer groups:

| Test | Result |
|------|--------|
| Message completeness (100K × 2 groups) | 100,000/100,000 per group |
| Per-partition FIFO ordering | 0 violations across 200 partitions |
| Consumer throughput during 1s push burst | 7,150 messages consumed during burst |
| New partition discovery (100 new partitions) | 10,000/10,000 |
| Deadlocks in server logs | Zero |
| **Overall** | **9 passed, 0 failed** |

## Deployment

The change is a `CREATE OR REPLACE FUNCTION` — takes effect on the next call, no server restart needed. Rollback: revert the SQL and re-run.

**Monitor after deploy:**
- Throughput chart: consume rate should track push rate instead of being suppressed
- `partition_busy` / `lease_race_condition` errors in pop responses (expected, non-fatal)
- Advisory lock usage: `SELECT * FROM pg_locks WHERE locktype = 'advisory'`
- Zero deadlocks on `partition_consumers`
