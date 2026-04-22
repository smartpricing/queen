# PUSH Improvement Plan — Rewrite `push_messages_v2` without the TEMP TABLE

## 1. Summary

The current `push_messages_v2` stored procedure creates a `TEMP TABLE ... ON COMMIT DROP` and runs two to three `ALTER TABLE` statements on every call. This is a well-known PostgreSQL anti-pattern that causes:

- Persistent catalog churn on `pg_class`, `pg_attribute`, `pg_type`, `pg_depend`.
- Write amplification on catalog WAL.
- `AccessExclusiveLock` contention on catalog rows during `DROP`.
- Disabled plan caching in PL/pgSQL (TEMP table columns/types are statement-scoped, so the optimizer cannot stabilize a plan).
- Four separate passes over `tmp_items` (initial SELECT, partition_id UPDATE, duplicate-check UPDATE, result-building SELECT).

This plan replaces the procedure with a three-statement design that does no DDL and runs the main body as a single CTE pipeline ending in `RETURNING ...` from one `INSERT`.

Target outcomes:

- Zero catalog writes per push.
- One JSONB parse (vs four passes).
- Preserved external contract (same signature, same response JSON shape).
- Plan caching restored.
- Matching or improved semantics for intra-batch duplicates.

## 2. Current state (reference)

File: `lib/schema/procedures/001_push.sql`

Key characteristics to preserve:

- Response is a JSON array with per-item entries, ordered by input `idx`:
  - On insert: `{"index", "transaction_id", "status":"queued", "message_id", "partition_id", "trace_id"}`.
  - On duplicate: `{"index", "transaction_id", "status":"duplicate", "message_id"}`.
- Auto-creates `queues` and `partitions` rows via upserts.
- Uses `gen_random_uuid()` as fallback for both `messageId` and `transactionId` when the client doesn't provide them.
- `traceId` may be absent, empty string, or a valid UUID string.
- `producer_sub` is server-stamped; empty string must map to SQL NULL.
- Parameter `p_check_duplicates` (boolean, default true). Parameter `p_check_capacity` already ignored.

Existing table-level invariant (critical for the rewrite):

- `lib/schema/schema.sql:73-75` defines
`CREATE UNIQUE INDEX IF NOT EXISTS messages_partition_transaction_unique ON queen.messages(partition_id, transaction_id);`
This means the database already enforces uniqueness on `(partition_id, transaction_id)`. The pre-check in the procedure is an optimization to produce a nice `status='duplicate'` response, not a correctness mechanism.

Statement-level trigger:

- `lib/schema/schema.sql:405-409`: `trg_update_partition_lookup` fires `AFTER INSERT ON queen.messages ... FOR EACH STATEMENT ... REFERENCING NEW TABLE AS new_messages`.
- The rewrite must still perform a single INSERT statement so the trigger sees the full batch in `new_messages`.

## 3. Target design

Three plpgsql statements in sequence, all lock-safe on realistic input. No TEMP TABLE, no ALTER.

### Statement A — upsert queues

```sql
INSERT INTO queen.queues (name, namespace, task)
SELECT DISTINCT
    item->>'queue',
    COALESCE(item->>'namespace', split_part(item->>'queue', '.', 1)),
    COALESCE(item->>'task',
        CASE WHEN position('.' in item->>'queue') > 0
             THEN split_part(item->>'queue', '.', 2)
             ELSE '' END)
FROM jsonb_array_elements(p_items) AS item
ON CONFLICT (name) DO NOTHING;
```

### Statement B — upsert partitions

```sql
INSERT INTO queen.partitions (queue_id, name)
SELECT DISTINCT q.id, COALESCE(item->>'partition', 'Default')
FROM jsonb_array_elements(p_items) AS item
JOIN queen.queues q ON q.name = item->>'queue'
ON CONFLICT (queue_id, name) DO NOTHING;
```

### Statement C — main insert + response aggregation

The only statement that must run as a single, large CTE to benefit from data-modifying-CTE snapshot semantics.

```sql
WITH parsed AS (
    SELECT
        (t.idx - 1)::int                                   AS idx,
        COALESCE((t.item->>'messageId')::uuid,
                 gen_random_uuid())                        AS message_id,
        COALESCE(t.item->>'transactionId',
                 gen_random_uuid()::text)                  AS transaction_id,
        t.item->>'queue'                                   AS queue_name,
        COALESCE(t.item->>'partition', 'Default')          AS partition_name,
        COALESCE(t.item->'payload', '{}'::jsonb)           AS payload,
        NULLIF(t.item->>'traceId', '')::uuid               AS trace_id,
        COALESCE((t.item->>'is_encrypted')::boolean,false) AS is_encrypted,
        NULLIF(t.item->>'producerSub', '')                 AS producer_sub
    FROM jsonb_array_elements(p_items) WITH ORDINALITY AS t(item, idx)
),
items AS (
    -- Resolve partition_id and compute dup_rank in one pass.
    -- dup_rank=1 is the sole representative of each (partition_id, transaction_id)
    -- within this batch; rn>1 rows are intra-batch duplicates.
    SELECT
        p.id AS partition_id,
        par.*,
        ROW_NUMBER() OVER (
            PARTITION BY p.id, par.transaction_id
            ORDER BY par.idx
        ) AS dup_rank
    FROM parsed par
    JOIN queen.queues     q ON q.name = par.queue_name
    JOIN queen.partitions p ON p.queue_id = q.id
                           AND p.name     = par.partition_name
),
inserted AS (
    INSERT INTO queen.messages
        (id, transaction_id, partition_id, payload,
         trace_id, is_encrypted, producer_sub)
    SELECT message_id, transaction_id, partition_id, payload,
           trace_id, is_encrypted, producer_sub
    FROM items
    WHERE dup_rank = 1
    ON CONFLICT (partition_id, transaction_id) DO NOTHING
    RETURNING id, transaction_id, partition_id
),
resolved AS (
    SELECT
        i.idx,
        i.transaction_id,
        i.partition_id,
        i.trace_id,
        i.message_id,
        i.dup_rank,
        ins.id AS inserted_id,
        -- Only evaluate the subquery when we didn't insert a new row. Because
        -- data-modifying CTEs see the pre-statement snapshot, this subquery
        -- returns the row that was *already there* when the statement started,
        -- which is the correct "existing duplicate" target.
        CASE WHEN ins.id IS NULL THEN
            (SELECT m.id
             FROM queen.messages m
             WHERE m.partition_id    = i.partition_id
               AND m.transaction_id  = i.transaction_id)
        ELSE ins.id
        END AS effective_id
    FROM items i
    LEFT JOIN inserted ins
        ON ins.partition_id   = i.partition_id
       AND ins.transaction_id = i.transaction_id
)
SELECT COALESCE(jsonb_agg(
    CASE
        WHEN r.inserted_id IS NOT NULL AND r.dup_rank = 1 THEN
            jsonb_build_object(
                'index',          r.idx,
                'transaction_id', r.transaction_id,
                'status',         'queued',
                'message_id',     r.effective_id::text,
                'partition_id',   r.partition_id::text,
                'trace_id',       r.trace_id::text
            )
        ELSE
            jsonb_build_object(
                'index',          r.idx,
                'transaction_id', r.transaction_id,
                'status',         'duplicate',
                'message_id',     r.effective_id::text
            )
    END
    ORDER BY r.idx
), '[]'::jsonb)
INTO v_results
FROM resolved r;
```

### Full shape of the procedure

```sql
CREATE OR REPLACE FUNCTION queen.push_messages_v2(
    p_items             jsonb,
    p_check_duplicates  boolean DEFAULT true,
    p_check_capacity    boolean DEFAULT true
)
RETURNS jsonb
LANGUAGE plpgsql
AS $$
DECLARE
    v_results jsonb;
BEGIN
    -- Parameter p_check_duplicates is accepted for backward-compatible signature
    -- but no longer toggles behavior: the unique index + ON CONFLICT DO NOTHING
    -- always produces correct duplicate detection. Ignoring it is safe because
    -- the "false" legacy path relied on the same unique index throwing inside
    -- a statement anyway, which was strictly worse (whole-batch abort).
    -- Parameter p_check_capacity has always been ignored.

    -- Statement A
    INSERT INTO queen.queues (name, namespace, task)
    SELECT DISTINCT ...  -- see Statement A above
    ON CONFLICT (name) DO NOTHING;

    -- Statement B
    INSERT INTO queen.partitions (queue_id, name)
    SELECT DISTINCT ...  -- see Statement B above
    ON CONFLICT (queue_id, name) DO NOTHING;

    -- Statement C
    WITH parsed AS (...), items AS (...), inserted AS (...), resolved AS (...)
    SELECT COALESCE(jsonb_agg(...), '[]'::jsonb)
    INTO v_results
    FROM resolved;

    RETURN v_results;
END;
$$;

GRANT EXECUTE ON FUNCTION queen.push_messages_v2(jsonb, boolean, boolean) TO PUBLIC;
```

## 4. Why this is correct

### 4.1 Duplicate semantics

- `INSERT ... ON CONFLICT (partition_id, transaction_id) DO NOTHING RETURNING id, transaction_id, partition_id` returns *only* the rows actually inserted. This is the standard PostgreSQL pattern for "insert-if-missing and tell me what you inserted".
- For rows that conflicted, the scalar subquery in `resolved`
  ```sql
  (SELECT m.id FROM queen.messages m
   WHERE m.partition_id = i.partition_id AND m.transaction_id = i.transaction_id)
  ```
  uses the pre-statement snapshot (data-modifying-CTE semantics). The pre-existing duplicate is guaranteed to be in that snapshot because that is exactly why the ON CONFLICT fired.
- The subquery hits the unique index `messages_partition_transaction_unique` → one index lookup per conflict, O(1) each.

### 4.2 Intra-batch duplicates

- `ROW_NUMBER() OVER (PARTITION BY partition_id, transaction_id ORDER BY idx)` tags identical-key rows. Only `dup_rank = 1` feeds the INSERT.
- For `dup_rank > 1`, we still join `resolved` to `items` via `(partition_id, transaction_id)`. Because the `dup_rank=1` row for the same key is in `resolved` and shares the `effective_id`, a follow-up LEFT JOIN approach or a direct SELECT picks up the same `effective_id` for all occurrences.
- **Watch out**: the current shape of `resolved` above keys by `idx`, not by `(partition_id, transaction_id)`. Rows with `dup_rank > 1` will have `inserted_id IS NULL` and the scalar subquery will return the pre-existing id if the key pre-existed, or the `dup_rank=1` sibling's id if the sibling was just inserted.
- **But** the scalar subquery uses the *pre-statement* snapshot, so the newly-inserted `dup_rank=1` sibling is invisible. For the case "intra-batch duplicates with no pre-existing row", all `dup_rank > 1` rows would get `effective_id = NULL`.
- **Fix**: enrich the CTE as follows so that intra-batch duplicates fall back to the message_id of the `dup_rank=1` sibling that actually got inserted.

```sql
batch_winners AS (
    -- For each (partition_id, transaction_id), which message_id did we pick?
    SELECT
        i.partition_id,
        i.transaction_id,
        COALESCE(
            ins.id,      -- newly inserted
            (SELECT m.id FROM queen.messages m
             WHERE m.partition_id   = i.partition_id
               AND m.transaction_id = i.transaction_id)    -- pre-existing
        ) AS winner_id
    FROM (
        SELECT DISTINCT partition_id, transaction_id
        FROM items
    ) i
    LEFT JOIN inserted ins
      ON  ins.partition_id   = i.partition_id
      AND ins.transaction_id = i.transaction_id
),
resolved AS (
    SELECT
        i.idx,
        i.transaction_id,
        i.partition_id,
        i.trace_id,
        i.dup_rank,
        bw.winner_id AS effective_id,
        (ins.id IS NOT NULL AND i.dup_rank = 1) AS was_inserted
    FROM items i
    JOIN batch_winners bw
      ON  bw.partition_id   = i.partition_id
      AND bw.transaction_id = i.transaction_id
    LEFT JOIN inserted ins
      ON  ins.partition_id   = i.partition_id
      AND ins.transaction_id = i.transaction_id
)
```

Every input item now has a well-defined `effective_id`: either the newly-inserted message or the pre-existing one. The final `jsonb_agg` emits one entry per input item, preserving input-order via `ORDER BY r.idx`.

### 4.3 Trigger contract

- Single `INSERT INTO queen.messages` statement → `trg_update_partition_lookup` fires once, sees the entire batch via `REFERENCING NEW TABLE AS new_messages`, and updates `partition_lookup` with the current `DISTINCT ON (partition_id) ORDER BY created_at DESC, id DESC` logic.
- The trigger's existing monotonicity guard and `updated_at = NOW()` remain in effect. The watermark optimization described in the POP path continues to work unchanged.

### 4.4 `gen_random_uuid()` stability

- `parsed` computes `transaction_id` once per input row using `COALESCE(item->>'transactionId', gen_random_uuid()::text)`. That value is then referenced *by name* in `items` (for `PARTITION BY ... transaction_id`) and in `inserted` (for INSERT) and in `resolved` (for response).
- Volatile function `gen_random_uuid()` in `parsed.transaction_id` must be evaluated *once per row* in `parsed` and then treated as a stable column. Postgres honors this because each CTE is materialized (or at least evaluated consistently per source row) when it is referenced with dependencies. Using a CTE rather than an inline subquery is the standard way to pin volatile-function evaluation — this is exactly why we split `parsed` from `items`.
- **Regression to watch for**: if a future optimizer inlines `parsed` into `items`, `gen_random_uuid()` could run twice per row with different values — once for PARTITION BY, once for SELECT — breaking the intra-batch dedup. Defense: add a test that verifies intra-batch dedup with missing `transactionId` is handled deterministically, and document the `AS MATERIALIZED` hint (PG 12+) as a fallback.

```sql
-- Defensive form if future PG versions become aggressive about CTE inlining:
WITH parsed AS MATERIALIZED (
    ...
)
```

## 5. Behavioral compatibility matrix


| Input scenario                                | Current v2 behavior                                                              | New behavior                  | Contract preserved?                                       |
| --------------------------------------------- | -------------------------------------------------------------------------------- | ----------------------------- | --------------------------------------------------------- |
| Single new message                            | `status=queued`                                                                  | `status=queued`               | Yes                                                       |
| Single duplicate of pre-existing              | `status=duplicate, message_id=existing.id`                                       | Same                          | Yes                                                       |
| Intra-batch duplicate (both new)              | One `status=queued`, the rest `status=duplicate, message_id=winner.id`           | Same                          | Yes                                                       |
| Intra-batch duplicate (matching pre-existing) | All `status=duplicate, message_id=pre-existing.id`                               | Same                          | Yes                                                       |
| Two concurrent batches inserting same key     | One wins at UNIQUE index, other reports `status=duplicate, message_id=winner.id` | Same                          | Yes                                                       |
| Empty `items` array                           | `[]`                                                                             | `[]`                          | Yes                                                       |
| `p_check_duplicates=false`, all new           | All `status=queued`                                                              | All `status=queued`           | Yes                                                       |
| `p_check_duplicates=false`, with duplicate    | **Unique violation abort** (legacy behavior)                                     | `status=duplicate` (no abort) | **Behavior strictly improved; documented as intentional** |
| Missing `transactionId` on all items          | Each gets fresh UUID, all succeed                                                | Same                          | Yes                                                       |
| `traceId` present but malformed               | Cast error, aborts batch                                                         | Cast error, aborts batch      | Yes (unchanged)                                           |
| `producerSub` empty string                    | Stored as NULL                                                                   | Stored as NULL                | Yes                                                       |
| Unknown queue                                 | Auto-created via Statement A                                                     | Auto-created via Statement A  | Yes                                                       |


## 6. Performance expectations

Baseline: current `push_messages_v2` on a batch of N items.


| Cost dimension              | Current                                     | After                | Delta                                |
| --------------------------- | ------------------------------------------- | -------------------- | ------------------------------------ |
| Catalog writes per call     | 3+ (CREATE TEMP, 2–3 ALTER, DROP at commit) | 0                    | **−100%**                            |
| JSONB element parses        | 4+ passes                                   | 1 pass (parsed CTE)  | **−75%**                             |
| Plpgsql statements per call | 6–8                                         | 3                    | **−50%+**                            |
| Round trips                 | 1 (plpgsql is server-side)                  | 1                    | same                                 |
| Per-row message INSERT      | 1 (fine)                                    | 1                    | same                                 |
| Index lookups on duplicates | 1 per item (full pre-scan)                  | 1 per duplicate only | **−fraction that aren't duplicates** |


Expected headline: sustained push throughput per worker should improve measurably (the exact number depends on concurrency; expect 1.5×–3× on realistic workloads with low duplicate rates, dominated by the elimination of catalog contention). The wins are larger under concurrency because pg_class locking disappears.

## 7. Rollout plan

### Phase 1 — parallel deployment

- Add `push_messages_v3` as a new function alongside `push_messages_v2`. Identical signature, new body.
- Keep `JobTypeToSql[JobType::PUSH]` pointed at `v2` in `lib/queen.hpp`.
- Run existing test suite against v3 by temporarily switching in a dev environment.

### Phase 2 — shadow traffic

- Add an env var `QUEEN_PUSH_PROC` with values `v2` (default) or `v3`. Select the SQL at startup.
- Run `v3` in staging for 24 h under realistic load. Observe:
  - Catalog bloat (`pg_stat_user_tables` for `pg_class`, `pg_attribute`).
  - `pg_stat_statements` row for `SELECT queen.push_messages_v*($1::jsonb)` — rows_per_call, mean_time, calls.
  - End-to-end p50/p99 push latency from client-side.
  - Any new errors in server logs.

### Phase 3 — flip and remove

- Flip default to `v3`.
- After 1 week of production stability: alias `push_messages_v2` to call `v3` (or replace v2's body with v3's), so the two names are always in sync.
- Remove `QUEEN_PUSH_PROC` env var.
- Optionally deprecate `p_check_duplicates` and `p_check_capacity` parameters in a subsequent release.

### Rollback

- `ALTER FUNCTION` back to previous body, or flip the env var. Zero-downtime because `CREATE OR REPLACE` keeps the OID stable.

## 8. Test plan

### 8.1 Unit / functional

Extend `client-js/test-v2/push.js`:

1. `testSimplePushRoundTrip` — push one message, verify `status=queued`, retrievable via pop.
2. `testPushAutoCreatesQueueAndPartition` — push to never-seen queue/partition, verify rows exist.
3. `testPushPreExistingDuplicate` — push same `transactionId` twice, second returns `status=duplicate` with first message's id.
4. `testPushIntraBatchDuplicatesAllNew` — two items with same `transactionId` in one array, both to new key. First queued, second duplicate, same message_id.
5. `testPushIntraBatchDuplicatesMatchingPreExisting` — same `transactionId` pre-exists plus two new items, all three get `status=duplicate` with pre-existing id.
6. `testPushConcurrentBatchesToSameKey` — two parallel calls each with the same `(queue, partition, transactionId)`. Exactly one `status=queued`, the other `status=duplicate` with the winner's message_id.
7. `testPushMissingTransactionId` — no `transactionId` provided on 100 items in one batch, verify 100 distinct ids in the response.
8. `testPushEmptyArray` — input `[]` returns `[]`.
9. `testPushMalformedTraceId` — invalid UUID in `traceId`, verify behavior matches legacy (abort is acceptable; document).
10. `testPushProducerSubEmptyStringBecomesNull` — inspect stored `producer_sub` is NULL.

### 8.2 Catalog health probe

After running the push suite at high QPS for 60 s, compare:

```sql
SELECT relname, n_tup_ins, n_tup_del, n_dead_tup
FROM pg_stat_user_tables
WHERE schemaname = 'pg_catalog'
  AND relname IN ('pg_class', 'pg_attribute', 'pg_depend', 'pg_type');
```

Expected: deltas are zero or near-zero for the new path, high for the old path.

### 8.3 Microbenchmark

Use `benchmark/benchmark-queen.js` push phase or write a dedicated microbench:

- Single-connection pusher at max rate, 60 s.
- 50 concurrent pushers, batch sizes {1, 10, 100, 1000}, 60 s each.
- Collect: p50, p95, p99, throughput, server CPU, postgres CPU.
- Compare v2 vs v3 side-by-side.

### 8.4 Plan cache probe

```sql
PREPARE q AS SELECT queen.push_messages_v3($1::jsonb);
EXPLAIN (ANALYZE, BUFFERS) EXECUTE q ('[{...}]'::jsonb);   -- first call
EXPLAIN (ANALYZE, BUFFERS) EXECUTE q ('[{...}]'::jsonb);   -- second call, should be plan-cached
```

Verify "Planning Time" drops sharply on repeat.

### 8.5 Chaos

- While running the push suite, repeatedly `ALTER SYSTEM SET ... ; pg_reload_conf()` to simulate config churn and ensure no function is pinned in an incompatible plan.

## 9. Risks

### 9.1 Subquery evaluation cost

The scalar subquery inside `resolved.effective_id` runs for rows where `ins.id IS NULL`. For conflict-heavy workloads this is still one index lookup per conflict, which is cheaper than the current full-table pre-check — but if the planner fails to push `CASE WHEN ins.id IS NULL` down, it could evaluate the subquery for every row. Defense: `EXPLAIN ANALYZE` on a realistic duplicate-heavy input and, if necessary, rewrite `resolved` as a LEFT JOIN to the `queen.messages` table, because the LEFT JOIN form is more reliably short-circuited:

```sql
resolved AS (
    SELECT
        i.idx, i.transaction_id, i.partition_id, i.trace_id, i.message_id, i.dup_rank,
        ins.id AS inserted_id,
        COALESCE(ins.id, m.id) AS effective_id
    FROM items i
    LEFT JOIN inserted  ins ON ins.partition_id = i.partition_id
                            AND ins.transaction_id = i.transaction_id
    LEFT JOIN queen.messages m ON ins.id IS NULL
                               AND m.partition_id   = i.partition_id
                               AND m.transaction_id = i.transaction_id
)
```

Same semantics, easier-to-plan shape. Keep this as the preferred form.

### 9.2 Volatile-function inlining

See 4.4. Test explicitly for this regression. Consider `AS MATERIALIZED`.

### 9.3 Behavior change when `p_check_duplicates=false`

Legacy path aborts on conflict; new path returns `status=duplicate`. This is an improvement, but any client relying on "whole-batch abort on duplicate" (unlikely) will notice. Document in release notes.

### 9.4 Concurrent `ON CONFLICT DO NOTHING` semantics

Postgres behavior: when two concurrent transactions both attempt to insert the same unique key, the second one's `ON CONFLICT DO NOTHING` waits for the first to commit, then sees the conflict and does nothing. It does **not** see the first transaction's row in its own snapshot (READ COMMITTED with ON CONFLICT has a special "get the winning row" internal semantics that populates the EXCLUDED pseudorelation but not the snapshot). After the statement, subsequent reads do see the row because of statement snapshot refresh.

Implication for our scalar subquery inside the same statement: the conflicting row is visible to the scalar subquery because ON CONFLICT internally snapshots the conflicting row for the EXCLUDED machinery, and the scalar subquery's visibility is the one used by subsequent reads within the same statement — **needs verification**. If this is unreliable across PG versions, a defensive approach is to move the duplicate lookup into a second statement executed after the INSERT CTE:

```sql
-- Statement C1: INSERT + return newly-inserted
-- Statement C2: resolve duplicates via separate SELECT
```

At the cost of one extra SQL statement. Prefer the single-statement form if PG 15+ confirms the semantics; otherwise fall back.

### 9.5 `queen.queues` and `queen.partitions` upsert contention

Statements A and B upsert concurrently across many parallel calls. `ON CONFLICT DO NOTHING` is safe and free of deadlocks when the conflict set is well-ordered. Current temp-table version has the same behavior. No new risk.

## 10. Observability additions

- Add a tag in pg_stat_statements: name each push call path so old vs new are distinguishable.
- Server metrics already track `push_request` and `push_messages`. Add a counter for `push_duplicate_returned` (response entries with `status=duplicate`) to detect whether client retry storms are wasting DB work.

## 11. Code locations that change


| File                                 | Change                                                                                   |
| ------------------------------------ | ---------------------------------------------------------------------------------------- |
| `lib/schema/procedures/001_push.sql` | Replace body of `push_messages_v2` (or add `push_messages_v3` side-by-side for Phase 1). |
| `lib/queen.hpp`                      | `JobTypeToSql[JobType::PUSH]` updated if function is renamed; otherwise unchanged.       |
| `client-js/test-v2/push.js`          | Add tests listed in §8.1.                                                                |
| `server/ENV_VARIABLES.md`            | Document `QUEEN_PUSH_PROC` during Phase 2.                                               |


## 12. Out of scope

- Splitting push into multiple slots in parallel. This is addressed in the libqueen hot-path plan.
- Changes to the push request shape at the HTTP layer.
- Changes to encryption handling, which happens in C++ before the SQL call.
- Changes to the file-buffer failover path.

