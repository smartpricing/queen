# Push vs Pop — Root-cause investigation of the combined-load throughput collapse

## Symptom

On production-grade hardware we consistently observe:


| Workload            | Throughput                            |
| ------------------- | ------------------------------------- |
| Push only           | ~27k req/s                            |
| Pop only            | ~70k msg/s (batch 30)                 |
| Push + pop combined | push collapses to ~7k req/s, pop lags |


This document records the final diagnosis. Two prior hypotheses were wrong; both
are listed at the end. The right answer, confirmed by direct lock-attribution
sampling, is in §3.

---

## 1. The short answer

The contention is **not** `FOR UPDATE OF pl SKIP LOCKED` in pop v1's wildcard
discovery, and it is **not** push-vs-pop on `queen.partition_lookup` rows.

The real mechanism is two problems that feed each other:

### A. Direct: push-vs-push serialization inside `trg_update_partition_lookup`

Every `push_messages_v3` call triggers the statement-level
`update_partition_lookup_trigger`, which runs:

```sql
INSERT INTO queen.partition_lookup (...)
SELECT ... FROM batch_max ...
ON CONFLICT (queue_name, partition_id)
DO UPDATE SET
    last_message_id = EXCLUDED.last_message_id,
    last_message_created_at = EXCLUDED.last_message_created_at,
    updated_at = NOW()
WHERE EXCLUDED.last_message_created_at > queen.partition_lookup.last_message_created_at
   OR (... tie-break on id ...);
```

Two concurrent `push_messages_v3` calls whose batches touch overlapping
partition sets collide on the same `partition_lookup` row. The second push's
UPSERT waits on the first's row lock (visible as `Lock/tuple`), and then waits
on the first's xact commit so the `WHERE` can decide whether to apply the
update (visible as `Lock/transactionid`). The trigger has no `SKIP LOCKED`
and no `NOWAIT` — it always blocks.

This is measurable in isolation: push-only at 200 connections on a 4-CPU PG
shows ~8.6 pushes permanently queued behind other pushes at any instant, with
no other workload present.

### B. Indirect: eligible-row pool collapse under combined load

`queen.partition_lookup` is the wildcard discovery table. The "eligible" set
is rows matching pop's WHERE clause — roughly, partitions with unconsumed
data inside the watermark horizon. Under:

- **push-only**: ~620 eligible rows (push creates them faster than they're
consumed — they just accumulate).
- **pop-only** on a pre-existing backlog: ~130 eligible rows (pop is draining).
- **combined**: **~16 eligible rows** on average.

Under combined load, pop drains newly-pushed partitions about as fast as push
surfaces them. Of those ~16 eligible rows, about 10 are locked at any given
instant — ~4.6 held by push's trigger UPSERT, ~6.2 held by another pop's
`FOR UPDATE`. Only ~5.6 rows are freely claimable, against 2–3 concurrent
pops fighting for them.

The shrunken pool causes pops to skip 66% of the eligible set, and the
constant row-lock churn on the same few rows extends push transaction
durations, which extends the push-vs-push wait queue, which further slows
pop by extending the time rows stay locked.

---

## 2. Measurements

All numbers from the lock-attribution experiment (§5). Each scenario is 120s,
producer 2 workers × 100 conns, consumer 1 worker × 50 conns, wildcard pop
`wait=true&autoAck=true`, 4-CPU Docker PG 16 with original v1 trigger.


|                                  | push-only | pop-only | combined |
| -------------------------------- | --------- | -------- | -------- |
| `push_messages_v3` mean ms/call  | 134       | —        | 1,114    |
| `trigger` mean ms/call           | 103       | —        | 1,018    |
| `pop_unified_batch` mean ms/call | —         | 215      | 80       |
| msgs inserted/s (throughput)     | ~11,460   | —        | ~1,221   |
| Lock/tuple waiters (avg)         | 3.68      | 0.00     | 7.21     |
| Lock/transactionid waiters (avg) | 6.65      | 0.00     | 5.62     |


### Lock attribution matrix on `queen.partition_lookup`

Sampled every 250ms via `queen.debug_lock_attribution` (see §4).


|                                    | push-only     | pop-only      | combined         |
| ---------------------------------- | ------------- | ------------- | ---------------- |
| Active pushes                      | 11.0          | 0             | 14.9             |
| Active pops                        | 0             | 0.48          | 2.67             |
| Eligible rows (matching pop WHERE) | 619.7         | 131.5         | **16.4**         |
| ↳ freely claimable                 | 592.1 (95.6%) | 128.5 (97.7%) | **5.64 (34.4%)** |
| ↳ locked by push trigger           | 27.6 (4.4%)   | 0 (0.0%)      | 4.59 (28.0%)     |
| ↳ locked by pop `FOR UPDATE`       | 0 (0.0%)      | 2.96 (2.3%)   | 6.17 (37.6%)     |
| Push waits caused by push holder   | 8.64          | 0             | **35.89**        |
| Push waits caused by pop holder    | 0             | 0             | 0.73             |
| Pop waits caused by push holder    | 0             | 0             | 0.23             |
| Pop waits caused by pop holder     | 0             | 0             | 0                |


### How to read this table

- **push_waits_on_push** is the only significant wait signal — always. In
combined it reaches 35.89, ~2.4× the active-push count, i.e. every push
holder has 2.4 waiters stacked behind it.
- **push_waits_on_pop** and **pop_waits_on_push** are both near zero. Push
and pop do **not** collide on row locks directly in any meaningful way.
This kills the v2-advisory-lock theory: there is no direct push-pop row
contention to fix.
- **locked_by_push** is what pop would skip because push's trigger is mid-UPSERT.
**locked_by_pop** is what pop would skip because a sibling pop is holding
it via `FOR UPDATE`. Under combined load, **both** are significant — and
pop-vs-pop (37.6%) is actually slightly larger than pop-vs-push (28%).
- The eligible row count collapsing from 619 → 16 is the defining feature
of the combined scenario.

---

## 3. Root-cause summary

1. `trg_update_partition_lookup` uses a **synchronous, conditional UPSERT**
  on every push. The `ON CONFLICT DO UPDATE ... WHERE EXCLUDED > current`
   structure forces every concurrent push that touches the same partition
   row to wait for the previous holder's xact to commit, just to decide
   whether the update fires. At 1k partitions × 200 connections × ~20
   messages/batch, batches overlap frequently and the waits stack up.
2. Under combined load the `partition_lookup`-eligible row pool collapses to
  tens of rows. Pop and push fight for the same few rows. Pop skips two-
   thirds of what it tries to claim, push serializes on each row, throughput
   of both sides falls together.

The v2 work (swap `FOR UPDATE SKIP LOCKED` for `pg_try_advisory_xact_lock`
on pop's discovery) addressed neither mechanism. The advisory lock in that
design would have removed a collision that **we measured to be essentially
zero** (pop_waits_on_push = 0.23, push_waits_on_pop = 0.73 in combined).
That is why v2 didn't help production and in some scenarios made things
worse — it added a candidate-loop and an ORDER BY overhead without
addressing the real waits.

---

## 4. Direction of the fix (not implemented yet)

The fix has to target `partition_lookup` writes from push. Three options,
roughly increasing in invasiveness:

**Option A — trigger does `ON CONFLICT DO NOTHING` only; background sweeper
updates `last_message_`*.** Push inserts new partition_lookup rows but never
updates existing ones in the hot path. A per-queue background loop (every
500ms–1s) runs one UPDATE that advances `last_message_created_at` / `id`
from `queen.messages` for any partition with activity since the previous
sweep. Push path becomes non-blocking (INSERT ON CONFLICT DO NOTHING never
waits on row locks when the row already exists). Pop tolerates ≤1s
staleness; its watermark + EXISTS cache already assumes freshness is
probabilistic.

**Option B — per-partition `pg_try_advisory_xact_lock` gate inside
`push_messages_v3`.** Before the trigger fires, push attempts a non-blocking
advisory lock per distinct `partition_id` in the batch. On success it
performs the inline UPSERT; on failure it skips the partition_lookup update
for that partition (the next push to that partition will catch it up). This
requires moving the logic out of the trigger into `push_messages_v3`'s CTE
chain; the trigger can stay as a fallback for other INSERT callers but do
`ON CONFLICT DO NOTHING`.

**Option C — drop `partition_lookup` entirely.** Use
`idx_messages_partition_created (partition_id, created_at, id)` to derive
`last_message_`* on demand in pop's wildcard discovery. Removes an entire
contention surface. Pop's discovery query becomes slightly heavier (a
`DISTINCT ON (partition_id) ... ORDER BY created_at DESC` against
`queen.messages` filtered by the watermark window) but push has no side
table to maintain.

Option A is the smallest change that removes the push-vs-push serialization
entirely. Option B is the cleanest without a background worker. Option C is
the architecturally simplest.

---

## 5. How to reproduce

Everything below runs against a local Docker PG pinned to 4 CPUs. The
test harness is in `test-perf/scripts/long-running-mon/` and drives the
existing `examples/long-running/{producer-cluster,consumer-clustered}.js`.

### 5.1 Prerequisites

- Docker running.
- Node 22 via nvm (`nvm install 22 && nvm use 22`).
- Build the server at least once: `cd server && make build-only`.

### 5.2 Start PostgreSQL

```bash
docker run -d --name queen-pg-5433 \
  --cpus=4 --memory=8g \
  -p 5433:5432 \
  -e POSTGRES_PASSWORD=postgres -e POSTGRES_DB=queen \
  postgres:16 postgres \
  -c shared_preload_libraries=pg_stat_statements \
  -c pg_stat_statements.max=10000 \
  -c pg_stat_statements.track=all \
  -c track_functions=pl \
  -c track_io_timing=on \
  -c log_lock_waits=on \
  -c deadlock_timeout=100ms \
  -c shared_buffers=2048MB \
  -c effective_cache_size=4096MB \
  -c work_mem=16MB \
  -c max_connections=400 \
  -c synchronous_commit=on \
  -c max_wal_size=2048MB \
  -c checkpoint_completion_target=0.9 \
  -c log_min_duration_statement=2000

# wait for ready
for i in $(seq 1 30); do
  docker exec queen-pg-5433 psql -U postgres -d queen -tAc "SELECT 1" 2>/dev/null && break
  sleep 1
done

docker exec queen-pg-5433 psql -U postgres -d queen -c \
  "CREATE EXTENSION IF NOT EXISTS pg_stat_statements"
```

### 5.3 Start Queen server

```bash
cd server
NUM_WORKERS=2 \
QUEEN_ENCRYPTION_KEY=2e433dbbc61b88406530f4613ddd9ea4e5b575364029587ee829fbe285f8dbbc \
SIDECAR_POOL_SIZE=20 \
DB_POOL_SIZE=10 \
PG_PORT=5433 PG_DB=queen PG_HOST=localhost \
PG_USER=postgres PG_PASSWORD=postgres \
./bin/queen-server
```

Queen applies the schema on startup, including the trigger we're testing.
Wait for `Acceptor listening` in the log.

### 5.4 Install the lock-attribution diagnostic function

One-time, from a side connection:

```sql
CREATE OR REPLACE FUNCTION queen.debug_lock_attribution(p_queue text)
RETURNS jsonb
LANGUAGE plpgsql AS $$
DECLARE
    v_result jsonb;
BEGIN
    WITH
    eligible AS (
        SELECT pl.partition_id, pl.xmax::text AS xmax_txt
        FROM queen.partition_lookup pl
        JOIN queen.partitions p ON p.id = pl.partition_id
        JOIN queen.queues     q ON q.id = p.queue_id
        LEFT JOIN queen.partition_consumers pc
            ON pc.partition_id   = pl.partition_id
           AND pc.consumer_group = '__QUEUE_MODE__'
        WHERE pl.queue_name = p_queue
          AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
          AND (pc.last_consumed_created_at IS NULL
               OR pl.last_message_created_at > pc.last_consumed_created_at
               OR (pl.last_message_created_at = pc.last_consumed_created_at
                   AND pl.last_message_id > pc.last_consumed_id))
          AND (q.window_buffer IS NULL OR q.window_buffer = 0
               OR pl.last_message_created_at <= NOW() - (q.window_buffer || ' seconds')::interval)
    ),
    classified AS (
        SELECT e.partition_id,
               CASE
                   WHEN e.xmax_txt = '0' THEN 'unlocked'
                   WHEN a.pid IS NULL    THEN 'unlocked_stale_xmax'
                   WHEN a.query LIKE '%push_messages_v3%'  THEN 'push'
                   WHEN a.query LIKE '%pop_unified_batch%' THEN 'pop'
                   ELSE 'other'
               END AS kind
        FROM eligible e
        LEFT JOIN pg_stat_activity a
            ON a.backend_xid::text = e.xmax_txt
           AND a.state = 'active'
           AND a.pid  <> pg_backend_pid()
    ),
    waiters AS (
        SELECT
            CASE WHEN aw.query LIKE '%push_messages_v3%'  THEN 'push'
                 WHEN aw.query LIKE '%pop_unified_batch%' THEN 'pop'
                 ELSE 'other' END AS waiter_kind,
            CASE WHEN ab.query LIKE '%push_messages_v3%'  THEN 'push'
                 WHEN ab.query LIKE '%pop_unified_batch%' THEN 'pop'
                 ELSE 'other' END AS blocker_kind
        FROM pg_stat_activity aw
        CROSS JOIN LATERAL unnest(pg_blocking_pids(aw.pid)) AS bp(pid)
        JOIN pg_stat_activity ab ON ab.pid = bp.pid
        WHERE aw.state = 'active'
          AND aw.wait_event_type = 'Lock'
          AND aw.wait_event IN ('tuple', 'transactionid')
          AND aw.pid <> pg_backend_pid()
    )
    SELECT jsonb_build_object(
        'eligible',            (SELECT count(*) FROM classified),
        'unlocked',            (SELECT count(*) FROM classified
                                WHERE kind IN ('unlocked','unlocked_stale_xmax')),
        'locked_by_push',      (SELECT count(*) FROM classified WHERE kind='push'),
        'locked_by_pop',       (SELECT count(*) FROM classified WHERE kind='pop'),
        'locked_by_other',     (SELECT count(*) FROM classified WHERE kind='other'),
        'push_waits_on_push',  (SELECT count(*) FROM waiters WHERE waiter_kind='push' AND blocker_kind='push'),
        'push_waits_on_pop',   (SELECT count(*) FROM waiters WHERE waiter_kind='push' AND blocker_kind='pop'),
        'push_waits_on_other', (SELECT count(*) FROM waiters WHERE waiter_kind='push' AND blocker_kind='other'),
        'pop_waits_on_push',   (SELECT count(*) FROM waiters WHERE waiter_kind='pop'  AND blocker_kind='push'),
        'pop_waits_on_pop',    (SELECT count(*) FROM waiters WHERE waiter_kind='pop'  AND blocker_kind='pop'),
        'active_pushes',       (SELECT count(*) FROM pg_stat_activity
                                WHERE query LIKE '%push_messages_v3%' AND state='active'
                                  AND pid <> pg_backend_pid()),
        'active_pops',         (SELECT count(*) FROM pg_stat_activity
                                WHERE query LIKE '%pop_unified_batch%' AND state='active'
                                  AND pid <> pg_backend_pid())
    ) INTO v_result;
    RETURN v_result;
END;
$$;
```

The function is purely read-only: it inspects `xmax` to find row holders
without taking a lock itself, and uses `pg_blocking_pids()` to attribute
active waiters. Zero perturbation of the workload.

### 5.5 Configure the queue

```bash
curl -s -X POST http://localhost:6632/api/v1/configure \
  -H "Content-Type: application/json" \
  -d '{"queue":"queen-long-running","options":{"leaseTime":60,"retryLimit":3,"retentionEnabled":true,"retentionSeconds":1800,"completedRetentionSeconds":1800}}'
```

### 5.6 Load generators

Must use **wildcard pop** (`/pop/queue/{q}?...`), not specific-partition pop.
Specific-partition pop doesn't touch `partition_lookup` at all and won't
reproduce the collapse.

`examples/long-running/consumer-clustered.js` generates:

```js
`/api/v1/pop/queue/${QUEUE_NAME}?batch=${batchSize}&wait=true&autoAck=true`
```

`examples/long-running/producer-cluster.js` generates push batches across
1,001 partitions.

Both accept env overrides:

```
NUM_WORKERS, CONNECTIONS_PER_WORKER, DURATION, MAX_PARTITION
```

For the reference experiment:

- Producer: `NUM_WORKERS=2 CONNECTIONS_PER_WORKER=100 DURATION=120`
- Consumer: `NUM_WORKERS=1 CONNECTIONS_PER_WORKER=50 DURATION=120 BATCH_SIZE=1000`

### 5.7 Run a single scenario

The `test-perf/scripts/long-running-mon/run-scenario.sh` orchestrator does:

1. `pg_stat_statements_reset()` + `pg_stat_reset()` at t0.
2. Launch the correct load generator(s) in the background.
3. Run `monitor.sh <scenario> 120` alongside — this samples every 2s
  (`pg_stat_activity`, `pg_stat_user_tables`, `pg_stat_wal`, `docker stats`)
   plus every 250ms (`queen.debug_lock_attribution`).
4. Dump `pg_stat_statements` and `pg_stat_user_functions` at t1.
5. Write a human-readable summary to `out/<scenario>/summary.txt`.

```bash
cd /path/to/queen
source ~/.nvm/nvm.sh && nvm use 22

# Push only
bash test-perf/scripts/long-running-mon/run-scenario.sh push-only 120

# Pop only (run after push-only has left a backlog, or push some data first)
bash test-perf/scripts/long-running-mon/run-scenario.sh pop-only 120

# Combined (this is the scenario the symptom lives in)
bash test-perf/scripts/long-running-mon/run-scenario.sh combined 120
```

Between scenarios you can TRUNCATE to reset state; this is only needed if
you want strictly comparable absolute throughput numbers (attribution ratios
are insensitive to backlog size).

```bash
docker exec queen-pg-5433 psql -U postgres -d queen -c "
  TRUNCATE queen.messages, queen.partition_lookup, queen.partition_consumers,
           queen.consumer_watermarks, queen.consumer_groups_metadata,
           queen.messages_consumed, queen.dead_letter_queue, queen.message_traces,
           queen.partitions, queen.queues CASCADE;
  SELECT pg_stat_reset();
  SELECT pg_stat_statements_reset();"
# Reconfigure queue because CASCADE drops it:
curl -s -X POST http://localhost:6632/api/v1/configure \
  -H "Content-Type: application/json" \
  -d '{"queue":"queen-long-running","options":{"leaseTime":60,"retryLimit":3,"retentionEnabled":true,"retentionSeconds":1800,"completedRetentionSeconds":1800}}'
```

### 5.8 Reading the output

Each scenario directory under `test-perf/scripts/long-running-mon/out/<name>/`
contains:

- `summary.txt` — aggregated table deltas, WAL deltas, wait-event histogram,
docker CPU/mem stats, and the **LOCK ATTRIBUTION** section (the key
evidence for this document).
- `pg_stat_statements.txt` — top queries by total exec time, with per-call
mean.
- `pg_stat_user_functions.txt` — per-function CPU time.
- `wait_events.jsonl`, `lock_attr.jsonl`, `tables.jsonl`, `wal.jsonl`,
`io.jsonl`, `docker.jsonl`, `activity_top.jsonl` — raw streams for
deeper analysis.

### 5.9 The essential single-scenario signature

On the 4-CPU PG with `v1` trigger + wildcard pop, expect combined to show
roughly:

```
LOCK ATTRIBUTION on queen.partition_lookup  (~120 samples)
  eligible                        16 rows
  unlocked                         5–6
  locked_by_push                   4–5
  locked_by_pop                    6
  push_waits_on_push              ~36    ← the whole bottleneck
  push_waits_on_pop               ~0.7
  pop_waits_on_push               ~0.2
  pop_waits_on_pop                 0
  active_pushes                   ~15
  active_pops                     ~3
```

If `push_waits_on_push` is the dominant line and `push_waits_on_pop` /
`pop_waits_on_*` are near zero, you are looking at exactly the mechanism
this document describes.

### 5.10 Confirmation experiment: disable the trigger

To prove the trigger is the sole cause, disable it and rerun push-only:

```sql
ALTER TABLE queen.messages DISABLE TRIGGER trg_update_partition_lookup;
```

Push throughput jumps from ~10.4k msg/s to ~17.7k msg/s (+70%),
`push_messages_v3` mean drops from 184ms to 48ms (–74%), and
`Lock/tuple`/`Lock/transactionid` waits go to zero. With the trigger
disabled, `partition_lookup` is not maintained, so pop wildcard discovery
can't find data — don't run combined in this state, just compare push
throughput to confirm the mechanism. Re-enable afterwards:

```sql
ALTER TABLE queen.messages ENABLE TRIGGER trg_update_partition_lookup;
```

---

## 6. Prior hypotheses that were wrong

Documented so future investigators don't re-walk the same path.

**Hypothesis 1 — pop v1's `FOR UPDATE OF pl SKIP LOCKED` collides with push's
trigger UPSERT.** This was the motivation for `pop_unified_batch_v2`, which
replaces `FOR UPDATE` with `pg_try_advisory_xact_lock`. The collision the v2
design targets is `push_waits_on_pop` + `pop_waits_on_push`. Measurement:
those counters are 0.73 and 0.23 in combined — 2% of push waits and
essentially nothing for pop. The v2 design fixed a collision that wasn't
there in the first place. See `cdocs/../lib/schema/procedures/002b_pop_unified_v2.sql`
for the v2 code (not wired into `pending_job.hpp` in main).

**Hypothesis 2 — missing `ORDER BY pc.last_consumed_at ASC NULLS LAST` in
pop v1's wildcard scan causes fairness-driven starvation.** True at small
partition counts (see `DISCOVERY.md`), but catastrophic at 30k+ partitions:
the sort is over the full eligible set on every pop. Also unrelated to the
push-vs-pop symptom that motivated v2. See `test-perf/scripts/collision/`
for the benchmark that established this.

Both hypotheses were ruled out by the lock-attribution measurement in §5.4.

---

## 7. Summary in one paragraph

The combined-load throughput collapse is caused by `trg_update_partition_lookup`
running a synchronous conditional UPSERT on `queen.partition_lookup` inside
every push transaction. Two concurrent pushes with overlapping partition
sets serialize on row locks and then on the holder's xact commit, producing
~36 queued push waiters at combined load. Pop does not contend with push
on row locks in any meaningful way (pop_waits_on_push ≈ 0.23,
push_waits_on_pop ≈ 0.73); the "push vs pop" framing is a misnomer. The
secondary effect is that the eligible pool on `partition_lookup` shrinks to
~16 rows under combined load and pop-vs-pop skipping becomes significant
(37.6% of eligible rows in combined). The fix targets push-side writes to
`partition_lookup` — making them insert-only or defferred — not pop-side
claim mechanics.