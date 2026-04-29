# 09 — Retention

Queen's retention is a single C++ background service (`RetentionService`, in `server/src/services/retention_service.cpp`) that runs four independent cleanup jobs on a fixed interval, coordinating across replicas with a Postgres advisory lock.

This page covers what it does, how it's configured, and how to extend it safely.

---

## The four cleanup jobs

`RetentionService::cleanup_cycle()` runs all four in sequence on every tick:


| #   | Method                          | What it deletes                                                                                        | Per-queue config                                   | Server config            |
| --- | ------------------------------- | ------------------------------------------------------------------------------------------------------ | -------------------------------------------------- | ------------------------ |
| 1   | `cleanup_expired_messages()`    | messages older than `retention_seconds`, regardless of consumption                                     | `retention_enabled`, `retention_seconds`           | `RETENTION_BATCH_SIZE`   |
| 2   | `cleanup_completed_messages()`  | messages already consumed by **all** consumer groups, older than `completed_retention_seconds`         | `retention_enabled`, `completed_retention_seconds` | `RETENTION_BATCH_SIZE`   |
| 3   | `cleanup_inactive_partitions()` | partitions with no messages and no consumer activity for `PARTITION_CLEANUP_DAYS`                      | n/a                                                | `PARTITION_CLEANUP_DAYS` |
| 4   | `cleanup_old_metrics()`         | rows in `messages_consumed`, `system_metrics`, `retention_history` older than `METRICS_RETENTION_DAYS` | n/a                                                | `METRICS_RETENTION_DAYS` |


All four are **opt-in per queue** for messages, **always-on globally** for the metrics tables.

---

## Per-queue configuration

Set on the queue via `configure` (or the `queues` table directly):

```sql
UPDATE queen.queues
SET retention_enabled = true,
    retention_seconds = 86400,            -- delete messages > 1 day old
    completed_retention_seconds = 3600    -- delete consumed messages > 1 hour old
WHERE name = 'orders';
```

Both flags must be set:

- `retention_enabled = true` — global on/off for this queue
- `retention_seconds > 0` — TTL for any message (consumed or not)
- `completed_retention_seconds > 0` — TTL for already-consumed messages

A common pattern is `retention_seconds = 7 days` (hard ceiling) + `completed_retention_seconds = 1 hour` (clean up consumed quickly, keep unconsumed for a week as a safety net).

If both are 0 (the default), **no message is ever deleted by retention**. The queue grows forever until you change the config.

---

## Server-wide configuration

Set on the broker:


| Variable                 | Default          | Effect                                                               |
| ------------------------ | ---------------- | -------------------------------------------------------------------- |
| `RETENTION_INTERVAL`     | `300000` (5 min) | How often `cleanup_cycle()` runs                                     |
| `RETENTION_BATCH_SIZE`   | `1000`           | Rows deleted per `DELETE LIMIT N` chunk                              |
| `PARTITION_CLEANUP_DAYS` | `7`              | Inactive-partition cutoff                                            |
| `METRICS_RETENTION_DAYS` | `90`             | TTL for `messages_consumed` / `system_metrics` / `retention_history` |


Tuning notes:

- `RETENTION_INTERVAL` is a *target* — if a cycle takes longer than the interval, the next cycle starts immediately.
- `RETENTION_BATCH_SIZE` controls per-statement work. Larger = faster cleanup but bigger Postgres locks. The default `1000` is conservative; `2000–5000` is fine on a beefy DB.
- For high throughput, `RETENTION_BATCH_SIZE=2000` paired with `RETENTION_INTERVAL=60000` (every minute) keeps the heap small without spiky pressure.

---

## Cross-replica coordination: advisory lock `737001`

A fleet of Queen replicas all run `RetentionService` independently, but only **one** of them runs cleanup per cycle. The mechanism:

```cpp
constexpr int64_t CLEANUP_LOCK_ID = 737001;

// inside cleanup_cycle():
BEGIN;
SELECT pg_try_advisory_xact_lock(737001);   -- non-blocking try
if got_the_lock:
    run all 4 cleanup jobs
COMMIT;   -- xact-level lock auto-releases here
```

Why a transaction-level (`xact`) lock and not a session-level one?

- **PgBouncer transaction pooling** can hand the same physical connection to multiple logical sessions. Session-level advisory locks would survive across logical "sessions" inside a pool, leading to "you don't own this lock" errors. Transaction-level locks auto-release on `COMMIT`/`ROLLBACK`.

`EvictionService` (`max_wait_time` cleanup) reuses the **same lock ID `737001`**. This is intentional — the two services are heavyweight cleanup operations and shouldn't run concurrently against the same database. If you add a new cleanup-style background service, decide:

- Reuse `737001` if your work overlaps with retention/eviction (e.g. another bulk DELETE on `queen.messages`)
- Pick a new ID and document it here if your work is independent

---

## How each job actually runs

### 1. Expired messages

```sql
-- Step 1: find partitions with at least one expired message
SELECT p.id AS partition_id,
       NOW() - (q.retention_seconds || ' seconds')::INTERVAL AS cutoff
FROM queen.partitions p
JOIN queen.queues q ON p.queue_id = q.id
WHERE q.retention_enabled = true
  AND q.retention_seconds > 0
  AND p.created_at < NOW() - (q.retention_seconds || ' seconds')::INTERVAL
  AND EXISTS (
      SELECT 1 FROM queen.messages m
      WHERE m.partition_id = p.id
        AND m.created_at < NOW() - (q.retention_seconds || ' seconds')::INTERVAL
  );

-- Step 2: per-partition, delete in batches of RETENTION_BATCH_SIZE
DELETE FROM queen.messages
WHERE id IN (
    SELECT id FROM queen.messages
    WHERE partition_id = $1::uuid
      AND created_at < $2::timestamptz
    LIMIT $3
);
```

Two pre-filters are critical:

- `p.created_at < cutoff` — a partition newer than the cutoff cannot possibly contain expired messages
- `EXISTS (...)` — skips partitions whose oldest message is still within retention

These cut the per-partition probe loop from ~90k iterations to a few hundred at high partition cardinality.

### 2. Completed messages

Like #1, but only deletes messages **all** consumer groups have already advanced past:

```sql
SELECT p.id AS partition_id,
       NOW() - (q.completed_retention_seconds || ' seconds')::INTERVAL AS cutoff,
       MIN(pc.last_consumed_id::text)::uuid AS safe_consumed_id
FROM queen.partitions p
JOIN queen.queues q ON p.queue_id = q.id
JOIN queen.partition_consumers pc ON pc.partition_id = p.id
WHERE q.retention_enabled = true
  AND q.completed_retention_seconds > 0
  AND p.created_at < NOW() - (q.completed_retention_seconds || ' seconds')::INTERVAL
  AND EXISTS (...)
GROUP BY p.id, q.completed_retention_seconds
HAVING MIN(pc.last_consumed_id::text)::uuid != '00000000-0000-0000-0000-000000000000';
```

The `MIN(last_consumed_id)` across consumer groups is the **safe deletion frontier**: any message with `id <= safe_consumed_id` has been consumed (and acked) by every group.

`'00000000-0000-0000-0000-000000000000'` is the sentinel for "this consumer group has not consumed anything yet." If even one group is still on the sentinel, the partition is excluded — we won't delete data the new group hasn't read yet.

A row is also written into `queen.retention_history` so the dashboard can show the cleanup pace.

### 3. Inactive partitions

```sql
SELECT p.id, q.name, p.name
FROM queen.partitions p
JOIN queen.queues q ON p.queue_id = q.id
LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
WHERE NOT EXISTS (SELECT 1 FROM queen.messages m WHERE m.partition_id = p.id LIMIT 1)
GROUP BY p.id, q.name, p.name, p.created_at
HAVING GREATEST(
    p.created_at,
    COALESCE(MAX(pc.last_consumed_at), p.created_at),
    COALESCE(MAX(pc.created_at), p.created_at)
) < NOW() - ($1 || ' days')::INTERVAL
LIMIT 1000;
```

Three independent "still in use" checks via `GREATEST`:

- partition was created < N days ago
- *some* consumer group acked from it < N days ago
- *some* consumer group registered (created) on it < N days ago (but hasn't acked yet — newly attached consumer)

If all three are older than `PARTITION_CLEANUP_DAYS`, the partition is safe to delete. `ON DELETE CASCADE` from `queen.partitions` handles the rest.

Capped at 1000 deletions per cycle to keep pressure bounded.

### 4. Old metrics

Three plain DELETEs against `messages_consumed`, `system_metrics`, `retention_history`. `METRICS_RETENTION_DAYS=90` is the default; lower it on small instances if those tables grow large.

---

## Observability

Every cycle logs:

```
[info] RetentionService: Running cleanup cycle
[info] RetentionService: Cleaned up expired_messages=12, completed_messages=4032,
       inactive_partitions=8, old_metrics=512
[info] RetentionService: cycle completed in 173ms
```

If any value is 0, that line is **suppressed** to keep logs quiet during steady state. To always see it (debugging), bump log level:

```bash
LOG_LEVEL=debug
```

`queen.retention_history` is a per-partition audit:

```sql
SELECT partition_id, retention_type, messages_deleted, executed_at
FROM queen.retention_history
ORDER BY executed_at DESC
LIMIT 20;
```

`017_retention_analytics.sql` exposes a Postgres function the dashboard uses for the retention chart.

---

## Adding a new cleanup job

1. Add a method on `RetentionService`:
  ```cpp
   int RetentionService::cleanup_my_thing() {
       try {
           auto conn = db_pool_->acquire();
           // batched DELETE in a loop, just like the existing methods
           return total_deleted;
       } catch (const std::exception& e) {
           spdlog::error("cleanup_my_thing error: {}", e.what());
           return 0;
       }
   }
  ```
2. Call it inside `cleanup_cycle()` after the existing four. **Inside the `if (has_lock)` block** — otherwise multiple replicas will run it in parallel.
3. Add server-wide config (env var) if applicable.
4. If the new job touches a hot table that PUSH/POP also use, **batch the deletes** (`LIMIT N`) and consider running it less often. Don't hold long locks on `queen.messages`.

---

## Common pitfalls

- **"Retention isn't deleting anything."** Check `retention_enabled = true` *and* `retention_seconds > 0` (or `completed_retention_seconds > 0`) on the queue. Both are required.
- **"Completed retention deletes too aggressively."** Make sure every consumer group has actually started consuming. A group that exists but never called `pop` keeps `last_consumed_id` at the sentinel — but a group that did one pop and stopped pins deletion at that ID forever. Either prune unused groups or shorten subscription mode.
- **"Inactive-partition cleanup keeps deleting partitions I want to keep."** Increase `PARTITION_CLEANUP_DAYS` or, if you want a partition to live forever, push a heartbeat message there once a week.
- **"Cleanup never runs in my multi-replica setup."** Each replica tries `pg_try_advisory_xact_lock` non-blocking. If logs show "Skipping cycle, another instance holds the lock" on every replica, that means *no* replica is acquiring it. Check that the cleanup transaction isn't being rolled back somewhere; the lock auto-releases on `COMMIT`, not on lock-acquire failure.

