# ACK Improvement Plan — Sequential loop → group-by-(partition, CG) set-based

## 1. Summary

`ack_messages_v2` processes ACKs one at a time in a PL/pgSQL `FOR` loop, acquires `pg_advisory_xact_lock` per message, and issues one `UPDATE` per message. In the common case where a client ACKs N messages that all belong to the same `(partition_id, consumer_group)` — which is the overwhelmingly common case because POP returns messages from a single leased partition — this performs N lock operations and N UPDATEs when exactly 1 of each would suffice.

This plan introduces `ack_messages_v3` that:

- Groups ACKs by `(partition_id, consumer_group)`.
- Takes the advisory lock **once per group**.
- Issues **one UPDATE per group** on `partition_consumers` (cursor advance + counter update + conditional lease release all in a single statement).
- Batches DLQ inserts and `messages_consumed` inserts per group.
- Fixes the `md5(partition_id || consumer_group)` collision ambiguity by introducing an in-input separator.
- Preserves all cross-transaction deadlock safety, retry-limit semantics, validation-error reporting, and response-row shape.

Target outcomes:

- ACK throughput per `(partition, CG)` hot-spot improves from O(N UPDATEs) to O(1 UPDATE) per batch.
- Advisory-lock contention drops proportionally.
- Response contract preserved bit-for-bit (same per-item result rows).

## 2. Current state (reference)

File: `lib/schema/procedures/003_ack.sql`.

Structure:

1. CTE `ack_data` parses input JSON.
2. CTE `enriched` does a LEFT JOIN to `queen.messages` + `queen.partitions` + `queen.queues`, computes per-row validation errors (message-not-found, invalid-or-expired-lease).
3. `FOR v_ack IN SELECT * FROM enriched ORDER BY partition_id, consumer_group, message_created_at, message_id LOOP`.
4. Inside the loop:
   - `pg_advisory_xact_lock(('x' || substr(md5(partition_id::text || consumer_group), 1, 16))::bit(64)::bigint)` — collision-ambiguous concatenation.
   - Branch on status:
     - `completed`/`success`: UPDATE pc with cursor advance, counter +1, batch-completion check.
     - `failed`: SELECT current `batch_retry_count`, `retry_limit`; if under limit, UPDATE pc to release lease + increment `batch_retry_count`; else INSERT DLQ + UPDATE pc.
     - `dlq`: INSERT DLQ + UPDATE pc (cursor advance + counter).
     - `retry`: UPDATE pc to release lease only.
5. `array_append` result row per ACK.
6. `unnest + jsonb_agg ORDER BY index` builds final response.

Deadlock prevention strategy: sort order inside the loop guarantees all concurrent batches touch rows in the same order.

Response shape (per item, wrapped in a JSONB array by `idx`):

```json
{
  "index": <int>,
  "transactionId": <string>,
  "success": <bool>,
  "error": <string|null>,
  "queueName": <string>,
  "partitionName": <string>,
  "leaseReleased": <bool>,
  "dlq": <bool>
}
```

## 3. Correctness invariants to preserve

Numbered because the v3 design will reference them.

**I1**. Cursor (`partition_consumers.last_consumed_id`, `last_consumed_created_at`) never regresses within or across batches.

**I2**. Cursor advances past a message iff that message was acked with a terminal status — `completed`, `success`, `dlq`, or `failed-with-retries-exhausted`.

**I3**. A `failed-with-retries-remaining` or `retry` status releases the lease and does **not** advance the cursor past the corresponding message.

**I4**. When total `acked_count + advance_count >= batch_size` (batch fully acknowledged), the lease is released and `acked_count` resets to 0 (or to the overflow, depending on interpretation).

**I5**. `batch_retry_count` is incremented when the lease is released due to retry-like status (failed-with-retries-remaining or retry). It resets to 0 when the batch completes successfully.

**I6**. DLQ insertion is per message: one row in `dead_letter_queue` per dlq or retries-exhausted message.

**I7**. Validation errors (`Message not found`, `Invalid or expired lease`) produce a response row with `success=false` but no state change.

**I8**. Cross-batch serialization per `(partition_id, consumer_group)` to prevent concurrent batches from racing cursor/counter updates.

**I9**. Response rows are returned in the same order as input (`ORDER BY index`).

**I10**. `messages_consumed` gets rows for consumption tracking (used by throughput metrics). Current v2 inserts one row per ack with `messages_completed = 1` or `messages_failed = 1`.

## 4. Known bugs to fix along the way

### 4.1 Advisory lock key ambiguity

```sql
pg_advisory_xact_lock(
    ('x' || substr(md5(v_ack.partition_id::text || v_ack.consumer_group), 1, 16))::bit(64)::bigint
)
```

`md5(A || B)` collides whenever two different `(A, B)` pairs produce the same concatenation. Example: `partition_id="abcd"` and `consumer_group="1234"` vs `partition_id="abcd1"` and `consumer_group="234"`. `partition_id::text` is always a 36-character UUID string so this specific collision is rare, but it is defense-in-depth that costs nothing. Fix:

```sql
md5(v_group.partition_id::text || E'\x01' || v_group.consumer_group)
```

`E'\x01'` (SOH) is a byte that cannot appear in a UUID text representation, and is extremely unlikely (operator should forbid it) in consumer group names. Alternatively, `'|'`: acceptable for UUIDs, but `|` is a legal character in consumer group names. Prefer `E'\x01'` for strict safety.

### 4.2 `v2` DLQ-path re-read of retry state

The `failed` branch in v2 does a SELECT to fetch current `batch_retry_count` and `retry_limit`. That's fine, but it duplicates work when multiple failed-status ACKs in the same batch target the same `(P, CG)`. Grouping naturally fixes this — v3 reads it once per group.

## 5. Target design

### 5.1 Overall shape

Single CTE to parse, resolve, validate, and group. Then a `FOR v_group IN SELECT * FROM grouped ORDER BY partition_id, consumer_group` loop over groups — not items. Inside the loop, a per-group walk decides cursor advance, DLQ list, and lease release. One UPDATE per group.

### 5.2 Grouping CTE

```sql
WITH ack_data AS (
    SELECT
        (value->>'index')::int                       AS idx,
        value->>'transactionId'                      AS txn_id,
        (value->>'partitionId')::uuid                AS partition_id,
        COALESCE(value->>'consumerGroup','__QUEUE_MODE__') AS consumer_group,
        value->>'leaseId'                            AS lease_id,
        COALESCE(value->>'status','completed')       AS status,
        value->>'error'                              AS error_msg
    FROM jsonb_array_elements(p_acknowledgments) AS value
),
enriched AS (
    SELECT
        a.*,
        m.id                AS message_id,
        m.created_at        AS message_created_at,
        q.name              AS queue_name,
        p.name              AS partition_name,
        CASE
            WHEN m.id IS NULL THEN 'Message not found'
            WHEN a.lease_id IS NOT NULL
             AND a.lease_id != ''
             AND NOT EXISTS (
                 SELECT 1 FROM queen.partition_consumers pc
                 WHERE pc.partition_id    = a.partition_id
                   AND pc.consumer_group  = a.consumer_group
                   AND pc.worker_id       = a.lease_id
                   AND pc.lease_expires_at > NOW()
             ) THEN 'Invalid or expired lease'
            ELSE NULL
        END AS validation_error
    FROM ack_data a
    LEFT JOIN queen.messages   m ON m.transaction_id = a.txn_id
                                 AND m.partition_id   = a.partition_id
    LEFT JOIN queen.partitions p ON p.id = a.partition_id
    LEFT JOIN queen.queues     q ON q.id = p.queue_id
),
-- Validation-only failures (no state change, just a response row) are peeled off here
validation_failures AS (
    SELECT
        idx, txn_id, partition_id, consumer_group,
        queue_name, partition_name, validation_error
    FROM enriched
    WHERE validation_error IS NOT NULL
),
-- Valid entries, grouped by (partition, CG). Array keeps per-group entries sorted
-- by (message_created_at, message_id), which matches v2's ORDER BY and ensures
-- monotonic cursor advance.
grouped AS (
    SELECT
        partition_id,
        consumer_group,
        MAX(queue_name)     AS queue_name,      -- same within group
        MAX(partition_name) AS partition_name,
        array_agg(
            ROW(idx, txn_id, message_id, message_created_at, status, error_msg, lease_id)::record
            ORDER BY message_created_at NULLS FIRST, message_id NULLS FIRST
        ) AS entries
    FROM enriched
    WHERE validation_error IS NULL
    GROUP BY partition_id, consumer_group
)
```

**Note on `array_agg(ROW(...)::record)`**: PL/pgSQL cannot easily `unnest` an untyped `record[]`. Better: define a typed composite or aggregate into parallel arrays. Using parallel arrays is simpler:

```sql
grouped AS (
    SELECT
        partition_id,
        consumer_group,
        MAX(queue_name)     AS queue_name,
        MAX(partition_name) AS partition_name,
        array_agg(idx                ORDER BY message_created_at NULLS FIRST, message_id NULLS FIRST) AS idxs,
        array_agg(txn_id             ORDER BY message_created_at NULLS FIRST, message_id NULLS FIRST) AS txn_ids,
        array_agg(message_id         ORDER BY message_created_at NULLS FIRST, message_id NULLS FIRST) AS message_ids,
        array_agg(message_created_at ORDER BY message_created_at NULLS FIRST, message_id NULLS FIRST) AS message_created_ats,
        array_agg(status             ORDER BY message_created_at NULLS FIRST, message_id NULLS FIRST) AS statuses,
        array_agg(error_msg          ORDER BY message_created_at NULLS FIRST, message_id NULLS FIRST) AS error_msgs
    FROM enriched
    WHERE validation_error IS NULL
    GROUP BY partition_id, consumer_group
)
```

### 5.3 Per-group walk

For each group, determine:

- `v_stop_ord` — the ORDINALITY (1-based) of the **first retry-like entry**. If none, it's `array_length(entries) + 1`, meaning we process all entries.
- `v_advance_count` — number of entries before `v_stop_ord` with terminal status (i.e., excluding the stopping retry-like entry if present).
- `v_new_cursor_id`, `v_new_cursor_ts` — the `(message_id, message_created_at)` of the last terminal entry before `v_stop_ord`.
- `v_dlq_ords[]` — ORDINALITY indices of entries that should produce DLQ rows (dlq-terminal).
- `v_release_lease` — true if `v_stop_ord <= array_length(entries)` (we stopped early).

Rules:

- **terminal** = `completed`, `success`, `dlq`, or `failed` when `batch_retry_count >= retry_limit`.
- **retry-like** = `retry`, or `failed` when `batch_retry_count < retry_limit`.

Because the `failed` classification depends on the **current** `batch_retry_count`, which is read once per group, that classification must happen inside the loop.

### 5.4 Per-group UPDATE (the one-shot)

Two cases:

**Case A: `v_advance_count > 0` (some messages to apply)**

Compute:

- `v_new_acked = v_current_acked_count + v_advance_count`.
- `v_batch_complete = (v_current_batch_size > 0) AND (v_new_acked >= v_current_batch_size)`.
- `v_final_release = v_batch_complete OR v_release_lease`.

UPDATE:

```sql
UPDATE queen.partition_consumers
SET last_consumed_id          = v_new_cursor_id,
    last_consumed_created_at  = v_new_cursor_ts,
    last_consumed_at          = NOW(),
    total_messages_consumed   = total_messages_consumed + v_advance_count,
    acked_count               = CASE WHEN v_batch_complete THEN 0 ELSE v_new_acked END,
    lease_expires_at          = CASE WHEN v_final_release THEN NULL ELSE lease_expires_at END,
    lease_acquired_at         = CASE WHEN v_final_release THEN NULL ELSE lease_acquired_at END,
    worker_id                 = CASE WHEN v_final_release THEN NULL ELSE worker_id END,
    batch_size                = CASE WHEN v_final_release THEN 0 ELSE batch_size END,
    batch_retry_count         = CASE
                                   WHEN v_batch_complete THEN 0
                                   WHEN v_release_lease AND NOT v_batch_complete THEN batch_retry_count + 1
                                   ELSE batch_retry_count
                                END
WHERE partition_id   = v_group.partition_id
  AND consumer_group = v_group.consumer_group;
```

**Case B: `v_advance_count = 0 AND v_release_lease = true` (only retry-like entries)**

```sql
UPDATE queen.partition_consumers
SET lease_expires_at   = NULL,
    lease_acquired_at  = NULL,
    worker_id          = NULL,
    batch_size         = 0,
    acked_count        = 0,
    batch_retry_count  = batch_retry_count + 1
WHERE partition_id   = v_group.partition_id
  AND consumer_group = v_group.consumer_group
  AND worker_id IS NOT NULL;   -- safety: only release if we still hold it
```

**Case C: `v_advance_count = 0 AND v_release_lease = false`**

No UPDATE. All entries were validation failures (filtered earlier) or the group is empty. Shouldn't happen in practice because we filtered validation failures out of `grouped`.

### 5.5 DLQ bulk insert

If `v_dlq_ords` non-empty:

```sql
INSERT INTO queen.dead_letter_queue
    (message_id, partition_id, consumer_group, error_message, retry_count, original_created_at)
SELECT
    v_group.message_ids[ord],
    v_group.partition_id,
    v_group.consumer_group,
    COALESCE(v_group.error_msgs[ord], 'Retries exhausted'),
    v_current_batch_retry_count,
    v_group.message_created_ats[ord]
FROM unnest(v_dlq_ords) AS ord;
```

### 5.6 `messages_consumed` insert

V2 inserts one row per ack. Decision: keep one row per ack (via `unnest`) for minimal behavior change — downstream analytics readers may aggregate by `acked_at`, so bunching them into one row per group with a single `acked_at` would slightly change throughput-rate accuracy.

```sql
-- messages_completed: count of terminal-non-dlq entries before the stop
INSERT INTO queen.messages_consumed
    (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
SELECT
    v_group.partition_id,
    v_group.consumer_group,
    CASE WHEN ord = ANY(v_dlq_ords) THEN 0 ELSE 1 END,
    CASE WHEN ord = ANY(v_dlq_ords) THEN 1 ELSE 0 END,
    NOW()
FROM unnest(v_terminal_ords) AS ord;
```

Where `v_terminal_ords` = all ordinalities that were actually applied (both terminal and dlq). Skip this for retry-like entries.

Alternative: one row per group with summed counters, which is what a future optimization could do. Match v2 for now.

### 5.7 Full shape (sketch)

```sql
CREATE OR REPLACE FUNCTION queen.ack_messages_v3(p_acknowledgments JSONB)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_group  RECORD;
    v_result_rows JSONB[] := ARRAY[]::JSONB[];
    v_failure RECORD;

    -- Per-group scratch
    v_entry_count         INT;
    v_ord                 INT;
    v_stop_ord            INT;
    v_advance_count       INT;
    v_new_cursor_id       UUID;
    v_new_cursor_ts       TIMESTAMPTZ;
    v_dlq_ords            INT[];
    v_terminal_ords       INT[];
    v_release_lease       BOOLEAN;

    v_current_batch_size      INT;
    v_current_acked_count     INT;
    v_current_batch_retry_cnt INT;
    v_retry_limit             INT;

    v_new_acked       INT;
    v_batch_complete  BOOLEAN;
    v_final_release   BOOLEAN;
BEGIN
    -- 1) Build validation-failure rows (no state change)
    FOR v_failure IN
        WITH ack_data AS (...),
             enriched AS (...)
        SELECT idx, txn_id, partition_id, queue_name, partition_name, validation_error
        FROM enriched
        WHERE validation_error IS NOT NULL
        ORDER BY idx
    LOOP
        v_result_rows := array_append(v_result_rows, jsonb_build_object(
            'index',         v_failure.idx,
            'transactionId', v_failure.txn_id,
            'success',       false,
            'error',         v_failure.validation_error,
            'queueName',     v_failure.queue_name,
            'partitionName', v_failure.partition_name,
            'leaseReleased', false,
            'dlq',           false
        ));
    END LOOP;

    -- 2) Per-group processing
    FOR v_group IN
        WITH ack_data AS (...), enriched AS (...), grouped AS (...)
        SELECT * FROM grouped
        ORDER BY partition_id, consumer_group
    LOOP
        v_entry_count := array_length(v_group.idxs, 1);
        IF v_entry_count IS NULL THEN CONTINUE; END IF;

        -- Advisory lock, NUL-safe separator
        PERFORM pg_advisory_xact_lock(
            ('x' || substr(md5(v_group.partition_id::text || E'\x01' || v_group.consumer_group), 1, 16))::bit(64)::bigint
        );

        -- Load per-group mutable state once
        SELECT pc.batch_size, pc.acked_count, COALESCE(pc.batch_retry_count,0), COALESCE(q.retry_limit,3)
        INTO v_current_batch_size, v_current_acked_count, v_current_batch_retry_cnt, v_retry_limit
        FROM queen.partition_consumers pc
        JOIN queen.partitions p ON p.id = pc.partition_id
        JOIN queen.queues     q ON q.id = p.queue_id
        WHERE pc.partition_id   = v_group.partition_id
          AND pc.consumer_group = v_group.consumer_group;

        -- Walk entries, classify, decide stop point
        v_stop_ord       := v_entry_count + 1;
        v_advance_count  := 0;
        v_new_cursor_id  := NULL;
        v_new_cursor_ts  := NULL;
        v_dlq_ords       := ARRAY[]::INT[];
        v_terminal_ords  := ARRAY[]::INT[];
        v_release_lease  := FALSE;

        FOR v_ord IN 1 .. v_entry_count LOOP
            DECLARE
                v_status TEXT := v_group.statuses[v_ord];
                v_is_terminal BOOLEAN := FALSE;
                v_is_dlq      BOOLEAN := FALSE;
                v_is_retry    BOOLEAN := FALSE;
            BEGIN
                IF v_status IN ('completed','success') THEN
                    v_is_terminal := TRUE;
                ELSIF v_status = 'dlq' THEN
                    v_is_terminal := TRUE;
                    v_is_dlq      := TRUE;
                ELSIF v_status = 'failed' THEN
                    IF v_current_batch_retry_cnt >= v_retry_limit THEN
                        v_is_terminal := TRUE;
                        v_is_dlq      := TRUE;
                    ELSE
                        v_is_retry    := TRUE;
                    END IF;
                ELSIF v_status = 'retry' THEN
                    v_is_retry := TRUE;
                END IF;

                IF v_is_retry THEN
                    v_stop_ord := v_ord;
                    v_release_lease := TRUE;
                    -- Result row for the retry-like entry
                    v_result_rows := array_append(v_result_rows, jsonb_build_object(
                        'index',         v_group.idxs[v_ord],
                        'transactionId', v_group.txn_ids[v_ord],
                        'success',       true,    -- v2 sets success=true for retry
                        'error',         NULL,
                        'queueName',     v_group.queue_name,
                        'partitionName', v_group.partition_name,
                        'leaseReleased', true,
                        'dlq',           false
                    ));
                    EXIT;
                END IF;

                -- Terminal: record it
                v_advance_count := v_advance_count + 1;
                v_new_cursor_id := v_group.message_ids[v_ord];
                v_new_cursor_ts := v_group.message_created_ats[v_ord];
                v_terminal_ords := array_append(v_terminal_ords, v_ord);
                IF v_is_dlq THEN
                    v_dlq_ords := array_append(v_dlq_ords, v_ord);
                END IF;

                v_result_rows := array_append(v_result_rows, jsonb_build_object(
                    'index',         v_group.idxs[v_ord],
                    'transactionId', v_group.txn_ids[v_ord],
                    'success',       true,
                    'error',         NULL,
                    'queueName',     v_group.queue_name,
                    'partitionName', v_group.partition_name,
                    'leaseReleased', false,  -- filled after UPDATE if needed
                    'dlq',           v_is_dlq
                ));
            END;
        END LOOP;

        -- Decide batch completion & final release
        v_new_acked      := v_current_acked_count + v_advance_count;
        v_batch_complete := (v_current_batch_size > 0 AND v_new_acked >= v_current_batch_size);
        v_final_release  := v_batch_complete OR v_release_lease;

        -- UPDATE per group
        IF v_advance_count > 0 THEN
            UPDATE queen.partition_consumers
            SET last_consumed_id          = v_new_cursor_id,
                last_consumed_created_at  = v_new_cursor_ts,
                last_consumed_at          = NOW(),
                total_messages_consumed   = total_messages_consumed + v_advance_count,
                acked_count               = CASE WHEN v_batch_complete THEN 0 ELSE v_new_acked END,
                lease_expires_at          = CASE WHEN v_final_release THEN NULL ELSE lease_expires_at END,
                lease_acquired_at         = CASE WHEN v_final_release THEN NULL ELSE lease_acquired_at END,
                worker_id                 = CASE WHEN v_final_release THEN NULL ELSE worker_id END,
                batch_size                = CASE WHEN v_final_release THEN 0 ELSE batch_size END,
                batch_retry_count         = CASE
                                                WHEN v_batch_complete THEN 0
                                                WHEN v_release_lease AND NOT v_batch_complete THEN batch_retry_count + 1
                                                ELSE batch_retry_count
                                            END
            WHERE partition_id   = v_group.partition_id
              AND consumer_group = v_group.consumer_group;
        ELSIF v_release_lease THEN
            UPDATE queen.partition_consumers
            SET lease_expires_at  = NULL,
                lease_acquired_at = NULL,
                worker_id         = NULL,
                batch_size        = 0,
                acked_count       = 0,
                batch_retry_count = batch_retry_count + 1
            WHERE partition_id   = v_group.partition_id
              AND consumer_group = v_group.consumer_group
              AND worker_id IS NOT NULL;
        END IF;

        -- DLQ bulk insert
        IF array_length(v_dlq_ords, 1) > 0 THEN
            INSERT INTO queen.dead_letter_queue
                (message_id, partition_id, consumer_group, error_message, retry_count, original_created_at)
            SELECT
                v_group.message_ids[ord],
                v_group.partition_id,
                v_group.consumer_group,
                COALESCE(v_group.error_msgs[ord], 'Retries exhausted'),
                v_current_batch_retry_cnt,
                v_group.message_created_ats[ord]
            FROM unnest(v_dlq_ords) AS ord;
        END IF;

        -- messages_consumed bulk insert (one row per terminal entry; matches v2 granularity)
        IF array_length(v_terminal_ords, 1) > 0 THEN
            INSERT INTO queen.messages_consumed
                (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
            SELECT
                v_group.partition_id,
                v_group.consumer_group,
                CASE WHEN ord = ANY(v_dlq_ords) THEN 0 ELSE 1 END,
                CASE WHEN ord = ANY(v_dlq_ords) THEN 1 ELSE 0 END,
                NOW()
            FROM unnest(v_terminal_ords) AS ord;
        END IF;

        -- Patch leaseReleased field on the final terminal result row if applicable
        IF v_final_release AND v_advance_count > 0 THEN
            -- Find the last terminal result row for this group and flip leaseReleased=true.
            -- Implement by remembering the index of the last append, or just leave leaseReleased=false
            -- on intermediate rows and set it only on the last row of the group (which is the last terminal one).
            -- See §5.8 for a cleaner approach.
            NULL;
        END IF;
    END LOOP;

    -- 3) Return sorted response
    RETURN (SELECT COALESCE(jsonb_agg(item ORDER BY (item->>'index')::int), '[]'::jsonb)
            FROM unnest(v_result_rows) AS item);
END;
$$;
```

### 5.8 `leaseReleased` per-row semantics

In v2, `leaseReleased` is set per ACK row based on the UPDATE's RETURNING. In v3, lease release is a group-level event. The response contract should stay consistent:

- For terminal entries before the stop point: `leaseReleased` should be true on the last terminal row of the group iff `v_final_release`. False on earlier rows.
- For the retry-like row at the stop point: `leaseReleased` = true.

Simplest implementation: after the walk, if `v_final_release AND v_advance_count > 0`, go back and patch the LAST terminal result row's `leaseReleased` to true.

```sql
-- After building v_result_rows for this group:
IF v_final_release AND v_advance_count > 0 THEN
    DECLARE
        v_last_ord INT := v_terminal_ords[array_length(v_terminal_ords, 1)];
        v_last_idx INT := v_group.idxs[v_last_ord];
        v_pos INT;
    BEGIN
        -- Find and patch in v_result_rows. Since we append in-order, the last
        -- appended row for this group is at position array_length(v_result_rows,1).
        -- But we've potentially already appended the retry row after terminals — easier to
        -- record the array positions during append.
        NULL;  -- See note below
    END;
END IF;
```

Cleaner approach: build a parallel `v_result_positions` array of the array-indices where each group's terminal rows land, then patch at the end.

Even cleaner: don't patch. Flip the semantic of `leaseReleased` so that it's always false for terminal rows (match reality: the lease release is a group-level thing). Update the client SDK behavior accordingly if anyone depends on it.

Recommended: match v2 exactly. Keep the patch logic. Implementation detail, not a blocker.

### 5.9 Alternative: single-pass using a window function

Rather than a walk, compute classification and stop-point set-theoretically:

```sql
classified AS (
    SELECT
        g.*,
        ord,
        CASE status WHEN 'completed' THEN 'terminal'
                    WHEN 'success'   THEN 'terminal'
                    WHEN 'dlq'       THEN 'dlq'
                    WHEN 'failed'    THEN CASE WHEN v_current_batch_retry_cnt >= v_retry_limit THEN 'dlq' ELSE 'retry' END
                    WHEN 'retry'     THEN 'retry'
        END AS class
    FROM ...
),
with_stop AS (
    SELECT *,
        MIN(CASE WHEN class = 'retry' THEN ord END) OVER () AS stop_ord
    FROM classified
),
...
```

Rejected: the classification depends on `v_current_batch_retry_cnt` which is per-group state. Making this fully set-based across groups would require carrying per-group state through the CTE. Keep the walk for clarity.

## 6. Behavioral compatibility matrix

| Scenario | v2 behavior | v3 behavior | Same contract? |
|---|---|---|---|
| Single completed ACK | cursor advance, counter +1, lease released if acked_count+1==batch_size | Same | Yes |
| 100 completed ACKs to same (P, CG) with batch_size=100 | 100 UPDATEs, lease released on the 100th | 1 UPDATE, lease released | **Yes** (same final state) |
| Mixed completed + completed + retry + completed | First two advance cursor, retry releases lease, fourth is silently dropped (never processed because the FOR loop has already exited mid-batch? actually v2 processes every row) | First two advance, retry stops the walk, fourth is **not** processed | **Semantic diff** — see §6.1 |
| Mixed completed + dlq + completed | All three terminal, cursor advances past all three, DLQ row for middle | Same | Yes |
| Cross-(P, CG) batch | Sorted by (P, CG); each row locks independently | Sorted by (P, CG); each group locks once | Yes |
| `failed` with retries-remaining | Release lease, +1 batch_retry_count | Same, but group-level | Yes |
| `failed` with retries-exhausted | DLQ + cursor advance + counter +1 | Same | Yes |
| Invalid lease | response error, no state change | Same | Yes |
| Unknown transaction_id | response error, no state change | Same | Yes |
| Two parallel batches for same (P, CG) | Serialized via advisory lock; interleaved by MD5 key | Serialized via advisory lock; separator-safe key | Yes, stricter |

### 6.1 Mixed completed + retry + completed: semantic question

V2 processes each row independently. For the sequence `[completed, completed, retry, completed]`:

- Row 1 (completed): cursor advances to msg1, counter +1.
- Row 2 (completed): cursor advances to msg2, counter +1.
- Row 3 (retry): lease released, counter unchanged (retry branch just releases).
- Row 4 (completed): attempts UPDATE pc with cursor = msg4, counter +1. But the lease is already released, and `worker_id` is NULL. UPDATE still runs because v2's completed branch has no `WHERE worker_id = ...`. Cursor jumps forward.

That last behavior — cursor advancing **past** the retry message even though the retry did not succeed — is a bug (or at least a surprising consequence). Under normal client behavior, a client never sends a `retry` in the middle of completed acks, so it's never observed. But it's latent.

V3 explicitly stops at the first retry-like entry. This is strictly more correct per invariants I2 and I3.

Decision: v3 fixes this behavior. Document in release notes. Add a specific test.

## 7. Test plan

### 7.1 Unit / functional

Create `clients/client-js/test-v2/ack.js` (or extend existing tests):

1. `testAckSingleCompleted` — single ack, verify cursor advances and counters update.
2. `testAckBatchAllCompleted` — 100 acks for same (P, CG), verify final state as if processed one-by-one.
3. `testAckBatchMixedSuccessAndDlq` — mix of completed and dlq, verify cursor advances past all, DLQ rows inserted.
4. `testAckBatchCompletedThenRetry` — `[completed x5, retry]`, verify first 5 advance cursor, retry releases lease, batch_retry_count +1.
5. `testAckBatchCompletedThenRetryThenCompleted` — v3-specific behavior: last completed is NOT applied. Compare to v2 explicitly to document the intentional semantic fix.
6. `testAckBatchFailedRetriesExhausted` — `[failed]` with batch_retry_count already at limit. Verify DLQ row, cursor advances, counter +1.
7. `testAckCrossPartitionCGBatch` — acks for (P1, CG1), (P1, CG2), (P2, CG1) interleaved. Verify each group gets exactly one UPDATE.
8. `testAckInvalidLease` — send ACK with stale lease, verify response error, no state change.
9. `testAckUnknownTransactionId` — verify response error, no state change.
10. `testAckEmptyArray` — returns `[]`.

### 7.2 Concurrency / deadlock

11. `testAckConcurrentBatchesSameGroup` — 10 clients, each ACKing 50 messages for same (P, CG) in parallel. Verify:
    - No deadlocks.
    - Final `total_messages_consumed` = 500.
    - `last_consumed_*` = max message acked.
12. `testAckConcurrentBatchesCrossGroups` — 10 clients, disjoint (P, CG) sets. Verify parallelism (ops/sec should scale with clients).
13. `testAckAdvisoryLockCollisionSafety` — synthetic test feeding consumer group names `"abcd"` + partition UUID suffix `"1234"` vs `"abc"` + `"d1234"`. Verify no state interleaving.

### 7.3 Contention benchmark

14. Throughput bench for 50 workers acking same (P, CG) at 1000 acks/s each. Compare v2 vs v3:
    - Acks/s processed.
    - `pg_stat_statements`: UPDATE count on `partition_consumers`.
    - p99 lock-wait time via `pg_locks` sampling.

### 7.4 Consistency diff

15. **Golden-run diff**: replay a recorded production ACK workload through v2 and v3 on identical initial state. Assert final database state is identical except for the mixed-with-retry case (§6.1). Automate this as a regression guard.

### 7.5 `messages_consumed` accuracy

16. Ack 1000 messages via v2 vs v3. Query:
    ```sql
    SELECT SUM(messages_completed), SUM(messages_failed) FROM queen.messages_consumed
    WHERE acked_at > <start>;
    ```
    Verify v2 == v3 exactly.

## 8. Rollout plan

### Phase 1 — parallel deployment

- Add `queen.ack_messages_v3` alongside v2. Keep `JobTypeToSql[JobType::ACK]` on v2.

### Phase 2 — shadow comparison

- Env var `QUEEN_ACK_PROC=v2|v3` at Queen ctor. Default v2.
- Run v3 in staging with real traffic for 48 h. Run golden-diff daily.

### Phase 3 — flip

- Default v3. Env var remains as kill-switch.

### Phase 4 — clean up

- Remove v2 after 30 days of stability. Drop `p_check_duplicates` equivalent parameters if any remain.

### Rollback

- `ALTER FUNCTION` or env-var flip. Stateless — no data migration needed.

## 9. Risks

### 9.1 Mixed-status semantic change

The post-retry drop behavior in §6.1 is strictly more correct but technically a behavior change. Mitigation: documented release note; explicit test; monitor for any client code that relies on the buggy v2 behavior (none known).

### 9.2 `leaseReleased` patching complexity

See §5.8. If implementation effort is too high, consider changing the contract: `leaseReleased` becomes a group-level flag attached to the last entry of each group. Simpler to implement, slight client-visible semantic shift.

### 9.3 `messages_consumed` granularity

Currently one row per ack. If we change to one row per group (to reduce insert volume), downstream analytics queries that assume one-row-per-message will break. Keep per-message for now; optimize later.

### 9.4 Array sizes

`array_agg` over large groups produces large arrays. A group of 10 000 acks would create six parallel arrays of 10 000 elements each. Memory within the PL/pgSQL function should be fine, but watch for aggregate memory limits (`work_mem`). Mitigation: libqueen's `_max_items_per_batch` (from the hot-path plan) caps batches far below this.

### 9.5 Advisory lock ordering and deadlocks

Today, v2 sorts by `(partition_id, consumer_group, message_created_at, message_id)` and takes locks per row. V3 sorts by `(partition_id, consumer_group)` and takes one lock per group. Both orderings are consistent across concurrent calls, so no new deadlock risk.

### 9.6 Behavior under concurrent POPs

An ACK UPDATE on `partition_consumers` races with a POP's UPDATE of the same row. v2 and v3 both rely on the advisory lock for ACK-vs-ACK serialization; ACK-vs-POP is serialized via the row lock on `partition_consumers` (POP uses `UPDATE ... WHERE ... RETURNING`). No change.

### 9.7 `batch_size = 0` reset on release

V3 sets `batch_size = 0` on final release. V2 does not do this for the completed branch but does for the retry branch. If anything reads `batch_size` between lease release and next POP, it would see 0 in v3 vs the stale value in v2. Next POP reassigns `batch_size` anyway. Probably harmless; verify via tests.

## 10. Code locations that change

| File | Change |
|---|---|
| `lib/schema/procedures/003_ack.sql` | Add `ack_messages_v3` function. Keep `v2`. |
| `lib/queen.hpp` | `JobTypeToSql[JobType::ACK]` switch via env var in ctor; OR keep pointing to v2 until phase 3. |
| `server/src/acceptor_server.cpp` | Read `QUEEN_ACK_PROC` env var, pass to Queen ctor. |
| `clients/client-js/test-v2/ack.js` | New/extended tests. |
| `lib/test_contention.cpp` or similar | Add concurrency test for 9.1 and 7.2. |
| Release notes | Document semantic change in §6.1. |

## 11. Out of scope

- Rewriting v2 to v3 in place (we keep side-by-side).
- Auto-ack path (the auto-ack branch lives inside `pop_unified_batch`; unifying it with ACK is a separate refactor).
- Changing the HTTP endpoint shape (`/api/v1/ack`, `/api/v1/ack/batch`).
- Changes to retry-limit configuration per queue.
- Adjusting `batch_retry_count` rules.

## 12. Follow-up: unify auto-ack and explicit ACK

Separate refactor (not in this plan):

- `pop_unified_batch` has an auto-ack branch that duplicates v2's cursor-advance-and-release logic.
- Once v3 is shipped, extract the common "advance cursor + update counters + conditionally release lease" into a helper function `queen._advance_consumer_cursor(partition_id, consumer_group, new_cursor_id, new_cursor_ts, advance_count, release_lease)`.
- Both v3 ACK and auto-ack POP call the helper. Single source of truth for cursor semantics.

Value: eliminates a known divergence risk (two places implementing the same invariants).
