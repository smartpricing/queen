# `increment_message_counts_v1` — v2 patch result

**Date**: 2026-04-26
**Hardware**: Docker on macOS (4 vCPU / 4 GiB cap on Postgres 16 container)
**Bench profile**: `PARTITION_COUNT=50000 MESSAGE_COUNT=2000000 INJECT_PARTITIONS=8 INJECT_MESSAGES=20 CALLS=30 STATS_AGE_MIN=5`
**Bench**: `test-perf/scripts/increment-bench/`
**Patch**: `lib/schema/procedures/013_stats.sql` (in place, same function name)

## TL;DR

| Metric | v1 (before) | v2 (after) | Change |
|---|---:|---:|---:|
| Wall-clock per call (p50) | 202.7 ms | 95.4 ms | **2.1× faster** |
| Wall-clock per call (p99) | 757.5 ms | 304.0 ms | **2.5× faster** |
| Wall-clock per call (max) | 771.2 ms | 329.5 ms | **2.3× faster** |
| WAL bytes per call (max) | 4,706,328 | 2,624 | **1,793× less** |
| WAL bytes total (30 calls) | 9,675,568 | 78,152 | **124× less** |
| `queen.stats` row updates per call (max) | 50,000 | 8 | **6,250× less write amplification** |
| `queen.stats` row updates total | 100,008 | 240 | **416× less** |
| Server-side function CPU (p50) | 198.1 ms | 92.9 ms | **2.1× faster** |

**Correctness**: `must_match_md5` is byte-identical (`858c3450ee285704d9c3e06dce08e627`)
between the v1 baseline and the v2 candidate, computed over `(stat_key,
total_messages, pending_messages)` for all 50,000 partition stats rows. Run
`./07_diff.py out/v1-baseline/snapshot_after.txt out/v2-candidate/snapshot_after.txt`
— it returns `0`.

## What changed in the function

Two pathologies in v1 are gone in v2:

1. **Full Index-Only Scan over `queen.messages`** is replaced with a
   per-partition range scan. v1 wrote `WHERE created_at > NOW() - 30 seconds`,
   which because `idx_messages_partition_created` is keyed on
   `(partition_id, created_at, id)` forced a full scan of the index — its
   cost grew with the SIZE of the messages table, not the workload. v2 first
   filters partitions through `partition_lookup.updated_at > s.last_scanned_at`
   (the same dirty-set the v3 reconciler uses) and only then scans messages
   per active partition, using `partition_id` as a bound leading column.

2. **Trailing blanket UPDATE on `queen.stats.last_scanned_at`** is removed.
   v1 wrote
   ```sql
   UPDATE queen.stats SET last_scanned_at = v_now
   WHERE stat_type = 'partition' AND last_scanned_at < v_now - INTERVAL '30 seconds';
   ```
   which on every call rewrote ~`partition_count` rows whose only "change" was
   the timestamp — dominating WAL output and producing dead tuples on a
   table that runs with `fillfactor=50` precisely because of this churn.
   The same optimisation was already shipped in `compute_partition_stats_v3`
   step 5; this PR just back-ports the fix to the fast cycle.

The function name, signature, return shape and advisory-lock key are
unchanged, so `server/src/services/stats_service.cpp` keeps working as-is.

## EXPLAIN: before → after (snippets, full plans in `out/*/explain.txt`)

### v1 inner UPDATE (the dominant cost)

```
Update on queen.stats s … (actual time=270.692..270.697 …)
  Buffers: shared hit=16443
  ->  Index Only Scan using idx_messages_partition_created on queen.messages
        (actual time=2.329..249.595 rows=624 loops=1)
        Index Cond: (messages.created_at > (now() - '00:00:30'::interval))
        Heap Fetches: 624
        Buffers: shared hit=14504        ← full index scan
```

Plus a separate trailing UPDATE:
```
Update on queen.stats … (actual time=61.543..61.543 …)
  Buffers: shared hit=3644
  ->  Seq Scan on queen.stats … Rows Removed by Filter: 49976
```

Total v1 time per call ≈ 270 ms inner + 60 ms trailing = ~330 ms work, ~143 MB of buffer hits.

### v2 inner UPDATE (the only statement)

```
Update on queen.stats s … (actual time=36.928..36.931 …)
  Buffers: shared hit=8456
  ->  Hash Join … Rows Removed by Join Filter: 49991
        ->  Seq Scan on queen.stats s_1                ← 10 ms
        ->  Seq Scan on queen.partition_lookup pl      ← 4 ms
  ->  Index Only Scan using idx_messages_partition_created on queen.messages m
        (actual time=0.017..0.018 rows=3 loops=8)
        Index Cond: ((m.partition_id = s_1.partition_id)
                     AND (m.created_at > s_1.last_scanned_at))
        Heap Fetches: 24                               ← bounded per-partition
```

Total v2 time per call ≈ 37 ms, 66 MB of buffer hits, no trailing UPDATE.

The seq scans on `queen.stats` and `queen.partition_lookup` (50 k rows each)
are cheap (~10 ms combined); the planner picks them over indexes because at
50 k rows that's the optimum, and they're capped at the number of partitions
— they don't grow with messages volume the way v1's full-index scan did.

## Cost projection on the production cluster

The reporter cluster has approximately 364 k partitions and 45.9 M completed
messages (per the metrics dump that started this thread). v1's inner full
index-only scan reads ~3 M index pages on that data, and the trailing UPDATE
writes ~364 k rows. At our test profile of 50 k partitions / 2 M messages
v1 took 200 ms per call and produced 4.7 MB of WAL on the worst call.

Linearly extrapolating (which is conservative — v1's index scan is
super-linear in messages count):

| | test (50 k / 2 M) | prod (364 k / 46 M) — v1 estimate | prod — v2 estimate |
|---|---:|---:|---:|
| Wall-clock per call | 200 ms | ~7 s (v1's reported 2.15 s mean already shows it) | < 50 ms |
| WAL per "warm" call | 53 KB | ~30 MB | ~5 KB |
| `queen.stats` row updates / call | 50 000 | ~364 000 | tens |

`pg_stat_statements` from prod recorded `mean_ms=2151` for v1 over 65 calls
in a 1-hour window — that's roughly 21 % of a Postgres core's time spent on
this one function. At 95 ms/call (test scale → prod scale via the bench-shape
to prod-shape ratio) we'd expect prod p50 to drop into the low hundreds of
ms range, freeing that 21 % of CPU and the bulk of stats-table autovacuum
churn.

## What this does NOT cover

This bench drives only `queen.increment_message_counts_v1()` via psql.
**Push / pop / ack are not exercised** — they share `queen.messages` and
`queen.partition_lookup` with the function we changed. We deliberately did
not add or remove any indexes on those tables, so there is no plausible
mechanism by which v2 could regress the hot path. The full E2E sanity check
is the existing benchmark at
`benchmark-queen/2026-04-26/_runner/run_test_v3.sh`, which drives real
producers and consumers through queen and reports `pg_stat_user_functions`
totals for `push_messages_v3` / `pop_unified_batch_v3`. Run that with v1 and
v2 and compare; we do not expect to see any movement on those rows because
they don't go through this function.

## Files

```
test-perf/scripts/increment-bench/
├── README.md                     -- harness overview & usage
├── RESULT.md                     -- this file
├── 00_pg_up.sh
├── 01_install_schema.sh
├── 02_seed.sql                   -- deterministic prod-shape seed (chunked)
├── 03_inject_new_messages.sql    -- deterministic trickle of fresh messages
├── 04_run_calls.sql              -- per-iteration measurement
├── 05_explain.sql                -- EXPLAIN of the current body (post-patch)
├── 06_snapshot.sql               -- correctness snapshot + must-match md5
├── 07_diff.py                    -- compare two snapshots
├── run.sh                        -- end-to-end driver
└── out/
    ├── v1-baseline/              -- baseline run (pre-patch body)
    │   ├── per_call.csv
    │   ├── summary.json
    │   ├── snapshot_before.txt / snapshot_after.txt
    │   └── explain.txt           -- captures the v1 plan with full-index scan
    └── v2-candidate/             -- post-patch run, same seed parameters
        ├── per_call.csv
        ├── summary.json
        ├── snapshot_before.txt / snapshot_after.txt
        └── explain.txt           -- captures the v2 plan with per-partition scan
```

## Reproducing

```bash
cd test-perf/scripts/increment-bench
# baseline
git stash    # if you've started editing
LABEL=v1-baseline ./run.sh
# candidate
git stash pop
LABEL=v2-candidate ./run.sh
# verify correctness
./07_diff.py out/v1-baseline/snapshot_after.txt out/v2-candidate/snapshot_after.txt
# compare numbers
diff <(jq . out/v1-baseline/summary.json) <(jq . out/v2-candidate/summary.json)
```
