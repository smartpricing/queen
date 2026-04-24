# PUSHPOPLOOKUPSOL — The solution to the combined-load throughput collapse

Companion document to `[PUSHVSPOP.md](./PUSHVSPOP.md)`.

`PUSHVSPOP.md` diagnoses **why** combined push+pop throughput collapses on
the legacy schema. This document describes **what we actually built**
to fix it, **how** the pieces fit together, and **why** the design looks
the way it does. It also records the pop-side co-change that went in at
the same time (drop `ORDER BY`, drop `LIMIT` from wildcard candidate
scan — without which the push fix alone would not have produced the
observed throughput).

---

## 1. Short answer

Two coupled changes, both required:

1. **PUSHPOPLOOKUPSOL** — `queen.partition_lookup` is no longer maintained
  inside the push transaction. The `trg_update_partition_lookup` trigger
   is **dropped**. Instead, `push_messages_v3` returns a `partition_updates`
   summary in its response, and libqueen fires a **fire-and-forget**
   `queen.update_partition_lookup_v1()` call after the HTTP response has
   been returned to the client. A periodic
   `PartitionLookupReconcileService` calls
   `queen.reconcile_partition_lookup_v1()` every 5 s as a safety net.
2. `**pop_unified_batch_v3`** — the wildcard candidate scan has
  no `ORDER BY` and no `LIMIT`. Coordination across concurrent consumers
   is done by `pg_try_advisory_xact_lock(partition_id)`, a non-blocking
   probe that releases on xact end. (Earlier drafts of this doc
   referred to this procedure as `pop_unified_batch_v2_noorder`; it
   was renamed to `_v3` to match `push_messages_v3`.)

Measured on a 4-CPU Docker PG 16 at 1001 partitions, combined load, 5 min:


|                      | before (trigger, LIMIT 64) | after (this design)  |
| -------------------- | -------------------------- | -------------------- |
| push throughput      | ~7 k req/s                 | **~15.6 k msg/s**    |
| pop throughput       | lagging                    | **~15.4 k msg/s**    |
| push/pop balance     | pop never catches up       | **99.3 %**           |
| partition coverage   | 64/1001 (6.4 %)            | **1001/1001 < 30 s** |
| `push_waits_on_push` | ~36                        | **0**                |
| `pop_waits_on_push`  | ~0.2                       | **0**                |


---

## 2. Context

`PUSHVSPOP.md` established that the bottleneck is **push-vs-push
serialization** on `queen.partition_lookup` rows caused by the
synchronous, conditional UPSERT inside `trg_update_partition_lookup`.
Two concurrent pushes whose batches touch overlapping partitions
serialize on:

1. `Lock/tuple` — waiting for the holder to release the row lock;
2. `Lock/transactionid` — waiting for the holder's xact to commit so the
  `WHERE EXCLUDED.last_message_created_at > queen.partition_lookup.last_message_created_at`
   predicate can evaluate.

At 1k partitions × 200 connections × 20-msg batches this produced
**~36 queued push waiters per holder** in combined load. Push-vs-pop row
contention was measured as negligible (`push_waits_on_pop ≈ 0.7`,
`pop_waits_on_push ≈ 0.2`).

The fix therefore has to target **push writes to `partition_lookup`**.
Pop's claim mechanics are a secondary concern — but not a non-issue, as
§6 explains.

---

## 3. The design

### 3.1 High-level data flow

```
 ┌──────────┐   HTTP push       ┌──────────────────┐
 │ producer │ ───────────────▶  │ libqueen.worker  │
 └──────────┘                   │                  │
                                │  PG: push_messages_v3
                                │    ├── INSERT messages (commit)
                                │    └── RETURN {items, partition_updates}
                                │                  │
                                │  ─── HTTP 200 ──▶│  (client sees success here)
                                │                  │
                                │  fire-and-forget ▼
                                │  PG: queen.update_partition_lookup_v1(partition_updates)
                                │       └── UPSERT into partition_lookup (monotonic)
                                └──────────────────┘

 ┌──────────────────────────┐       (every 5 s, advisory-locked)
 │ PartitionLookupReconcile │ ──▶   PG: queen.reconcile_partition_lookup_v1(60)
 │      Service             │           └── LATERAL scan + FOR UPDATE SKIP LOCKED
 └──────────────────────────┘

 ┌──────────┐   HTTP pop        ┌──────────────────┐
 │ consumer │ ───────────────▶  │ libqueen.worker  │
 └──────────┘                   │                  │
                                │  PG: pop_unified_batch_v3
                                │       ├── scan partition_lookup (no ORDER BY, no LIMIT)
                                │       ├── pg_try_advisory_xact_lock(partition_id)
                                │       ├── UPDATE partition_consumers (lease)
                                │       └── SELECT messages LIMIT batch_size
                                └──────────────────┘
```

### 3.2 Design principles

1. **Nothing on `partition_lookup` in the push critical path.** Push
  commits as soon as `queen.messages` has the rows. Any downstream
   bookkeeping is asynchronous.
2. **Bounded staleness, not zero staleness.** `partition_lookup` may lag
  physical message arrival by a few ms (happy path) up to 5 s (worst
   case after a libqueen crash). Pop must tolerate this, and does — the
   watermark + `EXISTS` cache was already designed around probabilistic
   freshness.
3. **Monotonic writes, idempotent callers.** Both
  `update_partition_lookup_v1` and `reconcile_partition_lookup_v1`
   use `ON CONFLICT DO UPDATE ... WHERE newer > current`. Reordering,
   retries, and concurrent callers are all safe.
4. **Non-blocking pop claim.** Pop never takes row locks on
  `partition_lookup`. Advisory locks only.
5. **A single writer advisory ID (`737002`) on the reconciler** so
  multiple server instances don't reconcile concurrently (primary
   writer is the faster path; the reconciler just needs to *eventually*
   run).

---

## 4. Implementation

### 4.1 Schema — trigger dropped

`lib/schema/schema.sql:431`

```sql
DROP TRIGGER IF EXISTS trg_update_partition_lookup ON queen.messages;
```

The trigger function body is left in the file (commented or still
defined but unused) so it can be restored via `CREATE TRIGGER` in an
emergency rollback.

### 4.2 `push_messages_v3` — now returns a two-field object

`lib/schema/procedures/001_push.sql`

**Signature / shape:**

- **Before:** returned a `jsonb` array of per-item results.
- **After:** returns a `jsonb` object:
  ```json
  {
    "items": [ {idx, message_id, transaction_id, status, partition_id, trace_id}, ... ],
    "partition_updates": [
      { "queue_name": "...",
        "partition_id": "<uuid>",
        "last_message_id": "<uuid>",
        "last_message_created_at": "2026-04-24T15:07:49.123456Z" },
      ...
    ]
  }
  ```
- `**items**` is the unchanged per-item result list consumed by the HTTP
response.
- `**partition_updates**` is computed in a new CTE pair — `partition_max`
(`DISTINCT ON (partition_id)`) and `partition_updates_json` — which
mirror the computation the old statement-level trigger did on
`new_messages`. Empty array when nothing was inserted (e.g. all
duplicates).

### 4.3 `queen.update_partition_lookup_v1(p_updates jsonb)` — primary writer

`lib/schema/procedures/016_partition_lookup.sql`

```sql
INSERT INTO queen.partition_lookup (
    queue_name, partition_id, last_message_id, last_message_created_at, updated_at
)
SELECT (u->>'queue_name')::TEXT,
       (u->>'partition_id')::UUID,
       (u->>'last_message_id')::UUID,
       (u->>'last_message_created_at')::TIMESTAMPTZ,
       NOW()
FROM jsonb_array_elements(p_updates) u
ORDER BY (u->>'partition_id')::UUID                   -- lock ordering
ON CONFLICT (queue_name, partition_id) DO UPDATE SET
    last_message_id         = EXCLUDED.last_message_id,
    last_message_created_at = EXCLUDED.last_message_created_at,
    updated_at              = NOW()
WHERE
    EXCLUDED.last_message_created_at > queen.partition_lookup.last_message_created_at
    OR (EXCLUDED.last_message_created_at = queen.partition_lookup.last_message_created_at
        AND EXCLUDED.last_message_id > queen.partition_lookup.last_message_id);
```

Key properties:

- `**ORDER BY partition_id**` establishes a global lock-acquisition order
across concurrent callers. Two concurrent calls with overlapping
partition sets will take locks in the same order, so they can never
deadlock.
- **Monotonic `WHERE`** — same structure as the old trigger, but now
serving async updates. Out-of-order arrivals (e.g. a slow primary
writer followed by the reconciler) never regress `last_message_*`.
- **Still causes row locks**, but those locks are on the async path and
don't block the HTTP response to the producer. Throughput on this path
is bounded by the push rate, not gated by it.

### 4.4 `queen.reconcile_partition_lookup_v1(p_lookback_seconds int)` — safety net

Also in `lib/schema/procedures/016_partition_lookup.sql`.

Runs periodically. Drives the scan from `partition_lookup` (tens of
thousands of rows at most), and uses a `LATERAL` subquery per candidate
row to find the true latest message via
`idx_messages_partition_created(partition_id, created_at, id)`:

```sql
WITH stale AS (
    SELECT pl.queue_name, pl.partition_id,
           pl.last_message_id, pl.last_message_created_at
    FROM queen.partition_lookup pl
    WHERE pl.last_message_created_at >= v_cutoff
       OR pl.updated_at               <  v_cutoff
),
fresh AS (
    SELECT s.queue_name, s.partition_id,
           m.id AS last_message_id, m.created_at AS last_message_created_at
    FROM stale s,
    LATERAL (
        SELECT id, created_at
        FROM queen.messages
        WHERE partition_id = s.partition_id
        ORDER BY created_at DESC, id DESC
        LIMIT 1
    ) m
    WHERE (m.created_at, m.id) > (s.last_message_created_at, s.last_message_id)
),
lockable AS (
    SELECT f.queue_name, f.partition_id
    FROM queen.partition_lookup pl
    JOIN fresh f ON pl.queue_name = f.queue_name
                 AND pl.partition_id = f.partition_id
    FOR UPDATE OF pl SKIP LOCKED                      -- never blocks primary writer
)
UPDATE queen.partition_lookup pl
SET ...
FROM fresh f, lockable l
WHERE pl.queue_name = l.queue_name AND pl.partition_id = l.partition_id
  AND pl.queue_name = f.queue_name AND pl.partition_id = f.partition_id;
```

Why `LATERAL` and not a naïve `SELECT DISTINCT partition_id FROM queen.messages WHERE created_at > cutoff`?
Because `queen.messages` has no index on `created_at` alone, and we
deliberately don't want one (extra WAL + write amplification on every
insert). The naïve form triggers a parallel seq scan — measured at **13 s
per cycle** at 3 M rows. The `LATERAL` form is **~45 ms per cycle** and
stays flat regardless of `queen.messages` size, because each seek is an
index lookup on `(partition_id, created_at DESC)`.

`FOR UPDATE OF pl SKIP LOCKED` means the reconciler never waits for the
primary writer — if a row is mid-UPSERT from
`update_partition_lookup_v1`, the reconciler skips it (the primary
writer is going to advance it anyway).

### 4.5 libqueen — async follow-up from PUSH

`lib/queen.hpp`

Two changes around the PUSH handler in `_process_slot_result`:

**(a) parse the new object-shaped response:**

```cpp
// PUSHPOPLOOKUPSOL: push_messages_v3 now returns
//   { "items": [...], "partition_updates": [...] }
nlohmann::json partition_updates = nlohmann::json::array();
if (!slot.jobs.empty()
    && slot.jobs[0]->job.op_type == JobType::PUSH
    && json_results.is_object()
    && json_results.contains("items")) {
    if (json_results.contains("partition_updates")) {
        partition_updates = std::move(json_results["partition_updates"]);
    }
    json_results = std::move(json_results["items"]);   // existing dispatch unchanged
}
```

**(b) after the per-job callbacks fire (HTTP response is on its way
back), enqueue a `CUSTOM` job for the partition_lookup refresh:**

```cpp
if (partition_updates.is_array() && !partition_updates.empty()) {
    JobRequest pl_job;
    pl_job.op_type    = JobType::CUSTOM;
    pl_job.request_id = "pl-refresh";
    pl_job.sql        = "SELECT queen.update_partition_lookup_v1($1::jsonb)";
    pl_job.params     = { partition_updates.dump() };
    pl_job.item_count = partition_updates.size();
    submit(std::move(pl_job), [wid](std::string result) { /* log failures */ });
}
```

This is **fire-and-forget** by design:

- The callback logs warnings on failure but doesn't retry or surface
errors to the producer.
- If the SQL call is lost (crash, queue eviction, PG connection reset),
the reconciler recovers within `reconcile_interval_ms` (default 5 s)
via `reconcile_partition_lookup_v1`.
- Successful primary-writer calls are idempotent with the reconciler's
output (monotonic `WHERE`).

### 4.6 `PartitionLookupReconcileService`

`server/src/services/partition_lookup_reconcile_service.cpp` /
`server/include/queen/partition_lookup_reconcile_service.hpp`

Modeled on `RetentionService`. Runs on the `system_thread_pool` (not on
the HTTP worker loops). Each cycle:

1. `BEGIN` on a pooled connection.
2. `pg_try_advisory_xact_lock(737002)`. If another server instance holds
  it, log at DEBUG and skip the cycle.
3. `SELECT queen.reconcile_partition_lookup_v1($lookback)`.
4. Log the returned row count at INFO if non-zero; DEBUG otherwise.
5. `COMMIT` (releases the advisory lock).
6. Sleep `interval_ms - cycle_duration`, then loop.

**Advisory lock IDs used elsewhere** (don't reuse):

- `737000` — RetentionService
- `737001` — EvictionService
- `737002` — **PartitionLookupReconcileService** (this service)

### 4.7 Config (`server/include/queen/config.hpp`)

```cpp
// PUSHPOPLOOKUPSOL: partition_lookup reconciliation (safety-net for
// queen.update_partition_lookup_v1 calls missed during crashes or
// transient failures of the per-push follow-up call).
int partition_lookup_reconcile_interval_ms      = 5000;  // 5 seconds
int partition_lookup_reconcile_lookback_seconds = 60;
```

Env overrides:

- `PARTITION_LOOKUP_RECONCILE_INTERVAL_MS` (default 5000)
- `PARTITION_LOOKUP_RECONCILE_LOOKBACK_SECONDS` (default 60)

Lookback bounds what the reconciler will repair. If the server is down
longer than `lookback`, raise this value transiently before starting
back up, or accept that dormant partitions will catch up on their next
push.

### 4.8 Service wiring (`server/src/acceptor_server.cpp`)

Instantiated only in `Worker 0` at startup:

```cpp
global_partition_lookup_reconcile_service =
    std::make_shared<queen::PartitionLookupReconcileService>(
        db_pool, system_thread_pool,
        config.jobs.partition_lookup_reconcile_interval_ms,
        config.jobs.partition_lookup_reconcile_lookback_seconds
    );
global_partition_lookup_reconcile_service->start();
```

---

## 5. Consistency model

`partition_lookup` is now an **eventually-consistent** view of
`queen.messages`. The reader (pop's wildcard discovery) tolerates this
already; it uses `partition_lookup` as a candidate filter and
re-validates against `queen.messages` itself when it fetches the batch.

### 5.1 Happy-path staleness

```
t=0ms       push commits messages
t≈1-5ms     libqueen schedules update_partition_lookup_v1
t≈5-20ms    update_partition_lookup_v1 commits partition_lookup row
```

Window in which pop can see "this partition has no new data" even
though messages are physically present: **~5-20 ms** under typical load.
Pop simply skips the partition and revisits it on the next round. No
message is lost, delivery is only delayed.

### 5.2 Crash-path staleness


| failure                                                  | bound on staleness                                                                                         |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| libqueen worker crashes between push commit and submit() | `reconcile_interval_ms` (5 s default)                                                                      |
| update_partition_lookup_v1 fails (PG err, network)       | `reconcile_interval_ms` (5 s default)                                                                      |
| Reconciler holder dies mid-cycle                         | `reconcile_interval_ms` (next instance takes over)                                                         |
| Server completely offline for N seconds, N ≤ lookback    | `reconcile_interval_ms` after restart                                                                      |
| Server completely offline for N seconds, N > lookback    | partition catches up on next push to it (via primary writer) — can be indefinite for truly idle partitions |


### 5.3 Ordering guarantees

- The `WHERE` clause on both `update_partition_lookup_v1` and
`reconcile_partition_lookup_v1` compares `(last_message_created_at, last_message_id)` lexicographically. Earlier writes never overwrite
later ones regardless of which path applies them.
- `ORDER BY partition_id` inside `update_partition_lookup_v1` makes the
function deadlock-free under concurrent callers.
- The reconciler uses `FOR UPDATE SKIP LOCKED`, so it is never a waiter
on the primary writer.

### 5.4 What would break it

- Two separate writers using the same `partition_lookup` row but
computing `last_message_*` from *different* source-of-truth tables.
Not the case here — both paths read from `queen.messages` (directly
or via the `partition_updates` relayed from push).
- A non-monotonic writer (e.g. a backfill tool) dropping the `WHERE newer > current` predicate. Don't do that.

---

## 6. The pop-side co-change: `pop_unified_batch_v3`

File: `lib/schema/procedures/002c_pop_unified_v3.sql`. Wired into
`lib/queen/pending_job.hpp` as the `JobType::POP` dispatch target.

### 6.1 Differences vs the legacy `pop_unified_batch` (v1)

1. **Advisory lock** (`pg_try_advisory_xact_lock(partition_id)`)
  replaces `FOR UPDATE OF pl SKIP LOCKED`. Non-blocking, no row locks
   on `partition_lookup`. Push never takes this lock, so push-vs-pop
   coordination on `partition_lookup` is not just reduced — it is
   **architecturally absent**.
2. **No `ORDER BY`** in the candidate scan. Legacy v1 (and the v2
  prototype that was reverted) used
   `ORDER BY pc.last_consumed_at ASC NULLS LAST` for fairness. At 30k
   partitions this produces a full-eligible-set sort per pop call,
   measured as catastrophic in
   `test-perf/scripts/collision/run-30k.sh`.
3. **No `LIMIT`.** An intermediate version used `LIMIT 64`. At 1001+
  partitions this caused severe **partition starvation**: concurrent
   pops only ever saw the first 64 partitions (by physical index order
   on `idx_partition_lookup_queue`) and raced each other for that
   subset. Measured pop coverage at 1001 partitions:
  - with `LIMIT 64`: 64/1001 partitions popped per 30 s window (6.4 %).
  - without `LIMIT`: **1001/1001 partitions within 30 s** (100 %).

### 6.2 Why removing `LIMIT` is safe (and why the candidate scan doesn't blow up)

The scan still exits at the first partition whose advisory lock we
acquire:

```plpgsql
FOR v_cand IN
    SELECT pl.partition_id, ...
    FROM queen.partition_lookup pl
    ...
    -- NO LIMIT here
LOOP
    IF pg_try_advisory_xact_lock(hashtextextended(v_cand.partition_id::text, c_lock_ns)) THEN
        ...claim v_cand...
        EXIT;
    END IF;
END LOOP;
```

So the *worst-case* work per pop is "scan until first unlocked eligible
partition." Under normal combined load that's typically the first
candidate; under heavy pop contention it's O(concurrent pops) — still
tiny. The `LIMIT` never helped bound the common case; it only bounded a
pathological worst case that never occurred in practice, at the cost of
catastrophic starvation in the common case.

### 6.3 Fairness

The v2 prototype used `ORDER BY pc.last_consumed_at` to approximate
fairness across consumer groups. In the current design fairness comes
from two places:

- **Across concurrent consumers on one queue** — `pg_try_advisory_xact_lock`
is effectively a random-probe distribution: different pops starting
the scan at overlapping index positions claim different partitions
because advisory locks are held for the xact duration.
- **Within a single consumer process** — the physical index scan order
(stable but arbitrary UUID order) is a sufficient approximation. The
workload doesn't require strict fairness; it requires absence of
starvation. Empirically, coverage is 70 % of partitions in any 10 s
window and 91 % in any 30 s window (see §7).

### 6.4 Why pop still oscillates slightly around push

Observable as a sine-like deviation of pop rate around the flat push
rate in any real-time throughput graph. Mechanisms, in order of
contribution:

1. **Eligible-pool size varies with push/pop phase offset.** When push
  is ahead, the eligible pool widens → advisory-lock hit rate rises →
   pop surges. When pop catches up, pool narrows → hit rate drops →
   pop slows. Self-correcting.
2. `**update_partition_lookup_v1` is async.** Pop's view of "partition
  X has new data" lags physical message arrival by a few ms, and
   lags in bursts because the async update queue shares libqueen
   worker slots with pushes.
3. **HTTP long-poll parking.** Many consumer connections are parked at
  the server. A push burst unparks many of them within ms.
4. **CPU sharing on the DB.** Push/pop compete for the same cores; any
  flush, checkpoint, or buffer eviction that favors one transiently
   shifts the other.

Self-correcting, bounded amplitude, near-zero lock contention. Not a
regression — a signature of a correctly tuned lock-free handoff. See
§7 for the measured picture.

---

## 7. Measurements

### 7.1 Reference run: 5 min combined, 1001 partitions, 4-CPU Docker PG

Sampled every 60 s:


| window    | push/s  | pop/s   | pop/push | lag<5s | lag<30s | lag≥30s | pop-cov 30s | lock waits    |
| --------- | ------- | ------- | -------- | ------ | ------- | ------- | ----------- | ------------- |
| 0→60 s    | 15.81 k | 13.27 k | 83.9 %   | 644    | 1001    | 0       | 803/1001    | 0             |
| 60→120 s  | 15.61 k | 12.29 k | 78.7 %   | 665    | 1001    | 0       | 768/1001    | 0             |
| 120→180 s | 15.68 k | 12.32 k | 78.6 %   | 713    | 1001    | 0       | 697/1001    | 5 (transient) |
| 180→240 s | 15.90 k | 10.94 k | 68.8 %   | 765    | 1001    | 0       | 678/1001    | 1             |
| 240→300 s | 14.81 k | 28.49 k | catch-up | 706    | 1001    | 0       | 510/1001    | 1             |


Totals over 5 min:

- Push: **15.6 k msg/s average**
- Pop:  **15.4 k msg/s average**
- Balance: **99.3 %**
- Backlog drift: +30 k / 300 s ≈ 100 msg/s (steady state)
- No partition ever exceeded 30 s lag.

### 7.2 Comparison vs prior designs


| Design                                                  | combined throughput      | `push_waits_on_push` | pop coverage (30 s)        |
| ------------------------------------------------------- | ------------------------ | -------------------- | -------------------------- |
| v1 trigger + v1 pop                                     | ~7 k req/s push          | **~36**              | ~620 eligible, 16 combined |
| v1 trigger + v2 advisory-lock pop                       | ~3.3 k msg/s             | ~28                  | unchanged                  |
| PUSHPOPLOOKUPSOL + v2_noorder with `LIMIT 64`           | ~6.8 k msg/s             | **0**                | **64/1001**                |
| PUSHPOPLOOKUPSOL + v2_noorder **without** `LIMIT` (now) | **~31 k msg/s combined** | **0**                | **~1001/1001**             |


Both halves of the design are necessary. PUSHPOPLOOKUPSOL alone raises
push throughput but leaves pop starved at 1001 partitions. Removing
`LIMIT` alone on the legacy trigger leaves push serialized on the
trigger UPSERT.

### 7.3 Stress test: 5× load, ~100 k msg/s sustained

After the 5-min reference run we scaled up the producer and consumer
clusters (more workers, more HTTP connections per worker) on the same
queue, same schema, same config. The system held.

**Live throughput**


| 10-s sample | messages count | push/s    |
| ----------- | -------------- | --------- |
| T = 10 s    | 57,686,753     | —         |
| T = 20 s    | 58,769,943     | **108 k** |
| T = 30 s    | 59,752,843     | **98 k**  |


Parallel 30-s sample across the same window: ~81 k push/s + ~78 k pop/s
observed at one point (consumer briefly catching up); stabilized around
**100 k push/s** sustained. The grafana throughput graph reads ~63 k
msg/s "mean" with a peak of ~85 k over a 1-hour window that includes
the earlier 15 k ramp.

**Wait-event breakdown** — 30 samples × all active backends over 15 s,
grouped by query category:


| Category                                          | Total       | Top waits                                                                                                                                             |
| ------------------------------------------------- | ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| **push** — `push_messages_v3`                     | 204 running | 85 `LWLock/WALWrite`, 65 `LWLock/BufferContent`, 28 `Lock/extend`, 13 `LWLock/WALInsert`, 9 `IO/WalSync` — **0 `Lock/tuple`, 0 `Lock/transactionid*`* |
| **pop** — `pop_unified_batch_v3`                  | 316 running | 51 `LWLock/WALWrite`, 32 `LWLock/BufferContent`, 5 `IO/WalSync` — **0 `Lock/tuple`, 0 `Lock/transactionid`**                                          |
| **upd_pl** — `update_partition_lookup_v1` (async) | 5 running   | **146 `Lock/transactionid`**, **48 `Lock/tuple`**, 36 `LWLock/WALWrite`, 11 `IO/WalSync`                                                              |


**What this picture shows**

1. **Push has zero row-lock waits at 100 k msg/s.** Every push-side wait
  is one of the irreducible costs of PostgreSQL doing that volume of
   inserts: WAL buffer flushes (`WALWrite`, `WALInsert`), heap page
   extension (`Lock/extend`, `IO/DataFileExtend`), buffer pool pressure
   (`BufferContent`, `BufferMapping`), WAL fsync (`WalSync`). No
   architectural change can remove these — they're what Postgres must
   do. Compare to `PUSHVSPOP.md` §2 where pre-fix measurements at only
   11 k/s showed `Lock/tuple = 3.68` and `Lock/transactionid = 6.65`.
   We went from 11 k → 100 k and from two busy row-lock waits → zero.
2. **Pop has zero row-lock waits at ~78 k msg/s.** Same story: only
  WAL / buffer / I-O waits from the work it has to do (update
   `partition_consumers`, insert into `messages_consumed`).
3. **The async path absorbs ALL the row-lock contention.** The
  `update_partition_lookup_v1` call is now the single serialization
   point in the system (146 `Lock/transactionid` + 48 `Lock/tuple`
   samples). This is exactly what PUSHPOPLOOKUPSOL was designed to do:
   move the contention **off the hot path** into an asynchronous path
   where its only cost is bounded staleness of `partition_lookup`.

`**partition_lookup` staleness under stress**


| measurement                                                      | value          |
| ---------------------------------------------------------------- | -------------- |
| avg age of `updated_at`                                          | **~17.7 s**    |
| max age of `updated_at`                                          | ~44 s          |
| rows with `updated_at > 5 s`                                     | 713 / 1001     |
| rows with `updated_at > 30 s`                                    | 242 / 1001     |
| partitions with **actual** last-message > 30 s old (direct scan) | **244 / 1001** |


The `actual` column is the critical one. The async writer is behind by
~17 s on average, but the 242 rows with stale `updated_at` almost
perfectly match the 244 partitions that genuinely haven't received a
new message in 30 s. The producer's random-partition distribution
leaves a tail of cold partitions each minute — that's workload shape,
not a reconciler backlog.

**The new (non-architectural) bottleneck**

At 100 k msg/s the limiting factor is `Lock/extend` on push (28
samples) and WAL buffer / fsync behavior — pure PostgreSQL internals
for a write-heavy workload. If even higher throughput is ever needed,
the knobs are:

- Larger `shared_buffers` and `max_wal_size` to reduce eviction and
checkpoint pressure.
- Faster disks (SSD vs NVMe — `IO/WalSync` drops).
- `synchronous_commit = off` or `remote_write` — trades durability for
throughput.
- Partition `queen.messages` by time (e.g. daily) so heap extension
spreads across multiple relations.
- UNLOGGED `queen.messages` for workloads that can rebuild on crash.

None of these are related to our design; they are PostgreSQL tuning
for a 100 k-insert/s workload.

**Takeaway**

PUSHPOPLOOKUPSOL scaled **~9×** on the push path (11 k → 100 k msg/s)
while removing every row-lock wait from the push and pop hot paths.
The remaining contention is on the async path, where it costs only
partition_lookup staleness — and the reconciler keeps even that
bounded. See §10 for a cheap optimization available if 100 k ever
isn't enough.

---

## 8. Operational notes

### 8.1 Default config

```
PARTITION_LOOKUP_RECONCILE_INTERVAL_MS      = 5000   # 5 s
PARTITION_LOOKUP_RECONCILE_LOOKBACK_SECONDS = 60
```

### 8.2 Tuning

- **Lower interval** (e.g. 2000) → faster healing of missed updates, at
the cost of one extra `reconcile_partition_lookup_v1` call every
N ms. Cycle cost is ~45 ms at 1k partitions, so anything above 500 ms
is safe.
- **Higher lookback** (e.g. 600) → more forgiving after a long outage.
At the cost of larger per-cycle work (`stale` CTE scans more rows).
- **Disable reconciler** — there is intentionally no "off" switch.
Rely on running the service; if you need to gate it for a test, stop
the service programmatically and rely on the primary writer
exclusively.

### 8.3 Observability

Logs to watch:

- `PartitionLookupReconcileService: fixed N partition_lookup rows` at
INFO — any non-zero value is a signal that primary-writer calls were
dropped. Occasional small numbers during restarts are expected; a
sustained high rate indicates a problem with the fire-and-forget
path.
- `[libqueen] update_partition_lookup_v1 failed: <error>` at WARN — per
individual primary-writer failure. Reconciler recovers; investigate
if high volume.

PG queries:

```sql
-- how fresh is partition_lookup relative to messages?
SELECT
  NOW() - MAX(pl.updated_at)             AS oldest_pl_update,
  NOW() - MAX(pl.last_message_created_at) AS oldest_message_tracked,
  COUNT(*) FILTER (WHERE pl.updated_at < NOW() - interval '10 seconds') AS stale_rows
FROM queen.partition_lookup pl;

-- is the reconciler ever doing work?
SELECT calls, total_time, mean_time, rows
FROM pg_stat_statements
WHERE query LIKE '%reconcile_partition_lookup_v1%';
```

### 8.4 Rollback plan

If PUSHPOPLOOKUPSOL needs to be rolled back in an incident:

1. Recreate the trigger from the function body still present in
  `schema.sql`:
2. Stop `PartitionLookupReconcileService` (or accept the extra but
  harmless UPSERTs it will still do; idempotent with the trigger).
3. libqueen will still emit the fire-and-forget call; it is idempotent
  with the trigger, harmless.

No schema rollback is needed — `update_partition_lookup_v1` and
`reconcile_partition_lookup_v1` stay callable, both paths are
idempotent.

---

## 9. Alternatives considered and rejected


| Option                                                                                              | Why not                                                                                                                                                                                                                                                                                                         |
| --------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **A. Keep trigger but `ON CONFLICT DO NOTHING` only; background sweeper updates `last_message_*`.** | Adds a second source of truth for `last_message_*`. Requires the sweeper to own the whole eventual-consistency story, and the "happy path" still pays the trigger cost for the INSERT half.                                                                                                                     |
| **B. Per-partition `pg_try_advisory_xact_lock` inside the trigger.**                                | Validated in a one-off experiment: removes push-vs-push but exposes `push_waits_on_pop` because pop's legacy `FOR UPDATE` still touches the row. Combined throughput ~3.3 k msg/s. Half a fix.                                                                                                                  |
| **C. Drop `partition_lookup` entirely; derive on demand from `queen.messages`.**                    | Architecturally cleanest. Rejected because pop's discovery would have to scan `queen.messages` with a `DISTINCT ON (partition_id)` over the recent-window — measured as non-trivial at 3 M rows, and the `partition_lookup` table provides a natural hot-set cache that the watermark logic already depends on. |
| **D. New `partition_lookup_leased` coordination table for pop.**                                    | Tried; adds another contention surface without removing push-vs-push. Rolled back.                                                                                                                                                                                                                              |
| **E. Move partition_lookup UPSERT inline into `push_messages_v3`'s CTE chain.**                     | Same fundamental cost as the trigger (the UPSERT is what blocks, not the trigger machinery). Same push-vs-push serialization.                                                                                                                                                                                   |


---

## 10. Future headroom — batching the async updates

**Status: not implemented. Not needed at current load. Documented here
so the option is on the table when/if push rates push past ~100 k/s
and partition_lookup staleness becomes operationally uncomfortable.**

### 10.1 The opportunity

At 100 k msg/s push, libqueen's PUSH handler produces roughly one
`update_partition_lookup_v1($1::jsonb)` follow-up call per push batch
— empirically **~5 k async calls/s**. Each call tries to UPSERT a few
`partition_lookup` rows with the monotonic `WHERE newer > current`
predicate.

The async path is now the single serialization point in the system:
§7.3 measured **146 `Lock/transactionid` + 48 `Lock/tuple`** samples
on it, versus **0 + 0** on push and pop. That contention costs us
~17 s of average `partition_lookup` staleness. Not a correctness
problem, but it is a rate ceiling for the async writer.

### 10.2 The fix

In libqueen, coalesce outstanding `partition_updates` across multiple
push commits into a **single** `update_partition_lookup_v1` call every
N milliseconds (e.g. N = 50 ms):

1. After a push commit, instead of submitting the CUSTOM job
  immediately, append `partition_updates` into a per-worker in-memory
   accumulator (already thread-local, no extra sync).
2. A light timer (e.g. fires from the event loop every N ms, or when
  the accumulator crosses a size threshold like 256 partition entries)
   flushes the accumulator into a single `CUSTOM` job.
3. Server-side, pre-aggregate duplicates: if the accumulator already
  contains a newer `(partition_id, last_message_id, last_message_created_at)`
   tuple for the same partition, drop the older one before flushing.
   This is pure N log N on the flushed batch, cheap.

Shape of the change: ~30 lines in `lib/queen.hpp` around the existing
`submit(JobRequest{ op_type = CUSTOM, ... })` block. No schema change
— `update_partition_lookup_v1` already accepts an arbitrarily long
`jsonb` array of updates and handles duplicates via `ORDER BY partition_id` + the monotonic `WHERE` clause.

### 10.3 Expected effect

- **5 k async calls/s → ~20 calls/s** at 50 ms batch interval.
- Per-call payload grows from 1-20 partitions → 100-500 partitions;
`update_partition_lookup_v1` does ordered UPSERT in a single
statement so wall-clock cost scales near-linearly with rows, not
with call count.
- The PL-UPDATE serialization pressure collapses: at 20 calls/s there
is essentially never a second caller waiting on the first.
`Lock/transactionid` and `Lock/tuple` on `update_partition_lookup_v1`
should go to zero in steady state.
- `partition_lookup` staleness drops from ~17 s avg to ~50 ms
(worst-case = one flush interval).

### 10.4 Trade-offs to be aware of

- **Slightly higher per-partition staleness in the unlucky-timing
case** (a push that lands right after a flush waits up to 50 ms for
the next flush instead of being submitted immediately). In practice
the current ~17 s staleness is *worse* than this, so the batched
version strictly dominates once load is high enough to warrant it.
- **Crash window grows** — between crash and restart, a libqueen worker
can lose up to N ms worth of accumulated `partition_updates` instead
of ≤1 ms. Still bounded by the reconciler (5 s cycle), so no data
loss — just slightly longer worst-case time to catch up after a
crash.
- **One more in-memory structure to manage per worker.** Small
complexity cost.

### 10.5 When to do it

Triggers that should motivate implementing this:

- `avg age of partition_lookup.updated_at > ~5 s` in steady state.
- Pop throughput starts to under-read because pop's `partition_lookup`
view is materially behind the actual message arrival.
- Reconciler reports large `fixed N partition_lookup rows` batches
consistently (indicating primary-writer backpressure, not crashes).

At current loads none of these are observable. The optimization is
deliberately filed as "future headroom" rather than implemented
preemptively.

---

## 11. Reproduction

All repro instructions for the benchmark harness are in
`[PUSHVSPOP.md` §5](./PUSHVSPOP.md). The same harness reproduces the
combined-load throughput numbers in §7 above; the only difference is
that the schema under test is HEAD (with PUSHPOPLOOKUPSOL applied and
`pop_unified_batch_v3` wired in).

To verify the fix in a running deployment:

```bash
# confirm the trigger is gone
psql -c "SELECT tgname FROM pg_trigger WHERE tgrelid = 'queen.messages'::regclass
         AND tgname = 'trg_update_partition_lookup'"
# → 0 rows

# confirm the two new procedures exist
psql -c "\df queen.update_partition_lookup_v1"
psql -c "\df queen.reconcile_partition_lookup_v1"

# confirm the pop dispatch is the no-order variant
psql -c "\sf queen.pop_unified_batch_v3" | grep -n 'NO LIMIT'
# → shows the 'NO LIMIT: advisory-loop EXIT' comment line

# confirm the reconcile service is running (look for log line on startup)
# PartitionLookupReconcileService started: interval=5000ms, lookback=60s
```

Under combined load, the single strongest positive signal is this
table being all-zero:

```sql
SELECT wait_event_type || '/' || COALESCE(wait_event,'-') AS kind,
       COUNT(*) AS n
FROM pg_stat_activity
WHERE state = 'active' AND wait_event IS NOT NULL
GROUP BY 1 ORDER BY n DESC;
-- Expect only Client/ClientRead. Any Lock/tuple, Lock/transactionid,
-- or LWLock/* entries sustained > 1 are a regression.
```

---

## 12. File summary


| File                                                                | Change                                                                                                                           |
| ------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| `lib/schema/schema.sql`                                             | `DROP TRIGGER IF EXISTS trg_update_partition_lookup`                                                                             |
| `lib/schema/procedures/001_push.sql`                                | `push_messages_v3` returns `{items, partition_updates}` via new `partition_max` / `partition_updates_json` CTEs                  |
| `lib/schema/procedures/016_partition_lookup.sql` (new)              | `queen.update_partition_lookup_v1`, `queen.reconcile_partition_lookup_v1`                                                        |
| `lib/schema/procedures/002c_pop_unified_v2_noorder.sql`             | No `ORDER BY`, no `LIMIT`, advisory-lock claim                                                                                   |
| `lib/queen/pending_job.hpp`                                         | `JobType::POP → pop_unified_batch_v3`                                                                                            |
| `lib/queen.hpp`                                                     | Parse `{items, partition_updates}` from `push_messages_v3`; fire `update_partition_lookup_v1` as `JobType::CUSTOM` post-response |
| `server/include/queen/partition_lookup_reconcile_service.hpp` (new) | Service header                                                                                                                   |
| `server/src/services/partition_lookup_reconcile_service.cpp` (new)  | Service implementation (advisory lock 737002)                                                                                    |
| `server/include/queen/config.hpp`                                   | Added `partition_lookup_reconcile_interval_ms`, `partition_lookup_reconcile_lookback_seconds` + env overrides                    |
| `server/src/acceptor_server.cpp`                                    | Instantiate & start service on `Worker 0`                                                                                        |


