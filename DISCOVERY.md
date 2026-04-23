# Discovery log — push/pop investigation (April 2026)

Notes from a multi-day session running the `long-running` bench against Queen
0.13.0, peeling layers until push and pop behaved predictably. The fixes that
survived are now in the repo (advisory-lock pop in `002b_pop_unified_v2.sql`,
C++ dispatcher switched to `pop_unified_batch_v2`, and two redundant indexes
removed from `schema.sql`). The list below captures what we learned, in the
order that made things click.

1. `**queen.partition_lookup` was the main push/pop collision point.** v1's
  `pop_unified_batch` held a `FOR UPDATE OF pl` row lock on a
   `partition_lookup` row for the entire pop transaction. Push's
   statement-level trigger `trg_update_partition_lookup` does
   `INSERT ... ON CONFLICT DO UPDATE` on that same row with no SKIP LOCKED,
   so every concurrent push blocked on pop's row lock. `pg_stat_activity`
   reported this as `Lock/tuple` and `Lock/transactionid` waits attributed
   to `push_messages_v3`. Switching the claim to
   `pg_try_advisory_xact_lock(hashtextextended(partition_id::text, ns))`
   eliminated the collision.
2. **v1's `pop_unified_batch` was missing a fair-distribution `ORDER BY`.**
  The sibling `pop_discover_batch` has
   `ORDER BY pc.last_consumed_at ASC NULLS LAST`, but it was silently dropped
   from `pop_unified_batch`. Without it, every wildcard consumer clustered on
   the low end of the index, and `msgs/pop` stayed in the single digits
   regardless of how long partitions had been accumulating. Adding the
   ORDER BY back jumped observed `msgs/pop` from ~4 to ~80 (at a 1000-partition
   workload). Worth porting to v1 too if it ever gets touched again.
3. **Five indexes on `queen.messages`; two were redundant.**
  `idx_messages_txn_partition (transaction_id, partition_id)` duplicated the
   UNIQUE index's columns in reversed order, and `idx_messages_transaction_id`
   was its prefix. Neither was used by any hot query. On a 31M-row table they
   summed to 7.8 GB. Dropping them (`DROP INDEX CONCURRENTLY`) took <3
   seconds each and immediately recovered the 8-hour throughput degradation
   described in point 4.
4. **Index working set exceeded `shared_buffers` after ~8 hours of load.**
  Total index size on `queen.messages` was 20 GB against a 16 GB
   `shared_buffers`. Hot indexes were at 89–97% cache hit (need ≥99.5% for
   this workload), producing hundreds of millions of disk reads over 8h.
   Throughput drifted from ~~18k msgs/s at start to ~10k over the day. After
   the two-index drop the working set fit (~~12 GB < 16 GB), hit ratios
   returned to near-100%, and throughput recovered in the same minute.
5. **Consumer pop capacity is dramatically higher than steady-state shows.**
  At matched push=pop the consumer (50 conn, `batch=100`, `wait=true`)
   runs ~~8k msgs/s. When the producer is throttled to 1 connection (~~110
   msgs/s) and a large backlog exists, the same consumer hits **~70k
   msgs/s**. This rules out "consumer is the bottleneck" as an explanation
   for the observed push/pop gap.
6. **The steady-state `pop/push ≈ 0.88` ratio is a chase-dynamics artifact,
  not a libqueen bug.** It arises from wildcard discovery over N partitions
   with Poisson-like per-partition arrivals. Each pop visits a partition and
   returns `min(batch_size, accumulated_msgs_since_last_visit)`. When push
   is at equilibrium, some fraction of pops always race ahead of the
   corresponding push, returning thin batches. This shows up in 0.11.x,
   0.12.x, and 0.13.x identically — it is not new.
7. **The December 2025 benchmark matched push=pop because of a different
  consumer strategy, not because of libqueen.** December used
   direct-partition addressing (`/pop/queue/Q/partition/P`) with
   `wait=false`. That shape removes server-side wildcard discovery entirely:
   each connection does client-side round-robin across known partition IDs.
   No ORDER BY staleness, no advisory lock race, no watermark filter. If you
   want push=pop today, use that pattern. The wildcard API's gap is intrinsic
   to wildcard discovery.
8. **Neither `wait=true` (parking) nor `wait=false` (immediate retry)
  closes the gap — they're equivalent operating points.**
   `POP_WAIT_INITIAL_INTERVAL_MS=100` parking trades consumer-side idle time
   for PG efficiency; `wait=false` flips it (PG thrashes on empty pops at
   5× the request rate, latency balloons 8×). Both settle at the same
   aggregate ~8k msgs/s pop throughput. The parking system is not the
   throttle — it's a smart idle mechanism that correctly minimizes PG
   load on an empty queue.
9. `**synchronous_commit=on` is viable with these fixes applied.** December
  used `sync_commit=off` to hide inefficiencies we've since fixed (the
   redundant indexes, the `partition_lookup` collision, the pop procedure's
   missing ORDER BY). With the current schema, durable WAL fsync is no
   longer a bottleneck — PG spends most of its time idle at typical
   workloads. Keep `sync_commit=on` in production.
10. **Retention + autovacuum on `queen.messages` become the cap at long
  runtimes.** Each retention cycle DELETEs several million rows (oldest-
    first), generating 3× the write amplification of the INSERTs they
    reclaim (data heap + 3 indexes per row). Autovacuum then has to clean
    the dead tuples. At ~10k msgs/s with 2h retention, this is a ~10-core
    background workload that shares PG CPU with push/pop. Not fixable at
    the libqueen level; table partitioning on `queen.messages` by
    `partition_id` hash would be the real lever if this matters.

---

### What the repo changes do


| File                                            | Change                                                                                                                                                                                                                                                                                                                                                                       |
| ----------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `lib/schema/procedures/002_pop_unified.sql`     | Left untouched. v1 `pop_unified_batch` preserved for reference and rollback.                                                                                                                                                                                                                                                                                                 |
| `lib/schema/procedures/002b_pop_unified_v2.sql` | New file: `pop_unified_batch_v2`. Advisory-lock partition claim (no `FOR UPDATE`) + fair-distribution `ORDER BY pc.last_consumed_at ASC NULLS LAST`. Same signature as v1 so C++ switches by name only.                                                                                                                                                                      |
| `lib/queen/pending_job.hpp`                     | `JobType::POP` dispatcher updated from `queen.pop_unified_batch($1::jsonb)` → `queen.pop_unified_batch_v2($1::jsonb)`.                                                                                                                                                                                                                                                       |
| `lib/schema/schema.sql`                         | Removed `CREATE INDEX idx_messages_transaction_id` and `CREATE INDEX idx_messages_txn_partition`. Added idempotent `DROP INDEX IF EXISTS` for both so existing databases reclaim the bloat on the next server restart (the schema file runs at boot). Kept three useful indexes: `messages_pkey`, `messages_partition_transaction_unique`, `idx_messages_partition_created`. |


### Roll-back

- Pop procedure: flip `pending_job.hpp:66` back to `pop_unified_batch`, rebuild
and redeploy. No schema change needed; v1 is still installed.
- Indexes: `CREATE INDEX CONCURRENTLY idx_messages_transaction_id ON queen.messages(transaction_id);` and the `idx_messages_txn_partition`
equivalent, if a future query path needs them.

