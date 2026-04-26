# increment-bench

A focused, reproducible micro-benchmark for `queen.increment_message_counts_v1()`
(and any future v2). The point is to:

1. **Capture a correctness baseline** for the current implementation, so any
   refactor can be diffed byte-for-byte against it.
2. **Measure the real cost** (wall-clock time, WAL bytes, dead tuples on
   `queen.stats`, sequential scans of `queen.messages`) on a deterministic,
   prod-shape database so two implementations are directly comparable.
3. **Avoid touching the hot path.** This bench drives the function via psql
   only -- no producers, no consumers, no pop / ack code under measurement.
   Push/pop/ack regressions, if we ever cause any while refactoring, are the
   job of the existing E2E benchmarks under
   `benchmark-queen/2026-05-25/_runner/`.

## Why a synthetic micro-benchmark

The function's cost is dominated by **the shape of `queen.stats` and the size
of `queen.messages`**, not by ingest rate. From the production data captured
in `test-perf/scripts/long-running-mon/out/combined/pg_stat_statements.txt`:

```
65 calls × 2,151 ms mean × 87 rows returned per call
```

87 row deltas per call means each call is paying ~2.1 s mostly to update
hundreds of thousands of *unrelated* rows in `queen.stats` and to seq-scan
`queen.messages`. A tiny synthetic database with the same shape reproduces
this in <5 minutes; we don't need to replay 45 M messages of production
traffic to expose the issue.

## Layout

```
00_pg_up.sh              -- start an isolated Postgres in Docker
01_install_schema.sh     -- apply lib/schema/{schema.sql,procedures/*.sql}
02_seed.sql              -- seed deterministic prod-shape state
03_inject_new_messages.sql  -- add a controlled trickle of fresh messages
04_run_calls.sql         -- run the function once, emit a CSV measurement row
05_explain.sql           -- EXPLAIN (ANALYZE, BUFFERS, WAL) of the inner SQL
06_snapshot.sql          -- correctness snapshot (CSV + md5 contract hash)
07_diff.py               -- compare two snapshots
run.sh                   -- end-to-end driver (you almost always want this)
out/<label>/             -- artefacts of one run (gitignored)
```

## The correctness contract

`06_snapshot.sql` defines what "behaviourally equivalent" means. Per the
agreed scope:

* **MUST match** between v1 and v2, given the same seed and same injected
  messages:
  - `stat_key`
  - `total_messages`
  - `pending_messages`
  - `newest_message_at` (truncated to seconds)
* **Allowed to differ** (informational, not part of the contract):
  - `last_scanned_at` -- v2 may legitimately stop bumping it for idle
    partitions, mirroring the optimisation already shipped in
    `compute_partition_stats_v3` step 5.
* **Ignored** (the function does not touch them): `processing_messages`,
  `completed_messages`, `dead_letter_messages`, `last_computed_at`, etc.

The snapshot script emits an `md5` over the must-match columns. `07_diff.py`
compares two snapshots by that hash; if the hashes are equal, behaviour is
guaranteed identical at the row level on every contractually-relevant
column.

The hot path (`push_messages_v3`, `pop_unified_batch_v3`, `ack_*`) is not
touched by this harness -- if we propose v2 changes that add or modify
indexes on `queen.messages` or `queen.partition_lookup`, regression-testing
those is the job of the E2E benchmark under
`benchmark-queen/2026-05-25/_runner/run_test_v3.sh`.

## Running

Default profile (`200k partitions × 10M messages`, 30 measured iterations,
~5 minutes wall clock on a laptop):

```bash
./run.sh
```

All knobs are env vars:

```bash
LABEL=v1-baseline \
CALLS=30 \
PARTITION_COUNT=200000 \
MESSAGE_COUNT=10000000 \
INJECT_MESSAGES=10 \
INJECT_PARTITIONS=5 \
./run.sh
```

Production-scale (~360k partitions, ~45M messages -- ~20 min, needs ~10 GB
disk in the docker volume):

```bash
LABEL=v1-prod-scale \
PARTITION_COUNT=360000 \
MESSAGE_COUNT=45000000 \
PG_MEM_GB=8 \
./run.sh
```

Iterating quickly while developing v2 (skips PG bring-up + seed, reuses an
already-prepared container):

```bash
SKIP_PG_UP=1 SKIP_SEED=1 LABEL=v2-attempt-1 ./run.sh
```

## Comparing two runs

```bash
# 1. Baseline run on current code
LABEL=v1-baseline ./run.sh

# 2. Apply your v2 patch to lib/schema/procedures/013_stats.sql
#    (edit increment_message_counts_v1 in place, OR add v2 and update
#     stats_service.cpp -- but this harness calls increment_message_counts_v1
#     by name, so the simplest thing during development is to keep that name)

# 3. Re-run with the SAME seed parameters (skip pg-up to keep DB isolated
#    from the v1 run; we tear down via run.sh's pg-up step on every run by
#    default).
LABEL=v2-candidate ./run.sh

# 4. Diff correctness
./07_diff.py out/v1-baseline/snapshot_after.txt out/v2-candidate/snapshot_after.txt

# 5. Diff performance
diff <(jq . out/v1-baseline/summary.json) <(jq . out/v2-candidate/summary.json)
```

The `must_match_md5` line in each `summary.json` is the single value that
proves correctness equivalence between the two runs.

## What "passes" means

A v2 candidate is acceptable iff:

1. `07_diff.py` returns `0` against the v1 baseline -- the must-match hash
   is identical, meaning every partition's `total_messages`,
   `pending_messages` and `newest_message_at` are unchanged.
2. `summary.json`'s `timing_ms.p50` is meaningfully lower than baseline (we
   target an order of magnitude on the prod-shape default profile).
3. `summary.json`'s `stats_n_tup_upd.p50` drops sharply (today it's
   approximately `partition_count`; v2 should make it proportional to the
   number of partitions that actually had new messages).
4. `summary.json`'s `messages_seq_scan.p50` drops to 0 (v2 should use
   `partition_lookup.updated_at` to drive the scan and never seq-scan
   `queen.messages`).
5. The full E2E benchmark (`benchmark-queen/2026-05-25/_runner/run_test_v3.sh`)
   shows no regression in push/pop/ack throughput or latency. (Tier B,
   not driven by this harness.)

## Caveats

* We disable autovacuum on `queen.messages` and `queen.stats` in the seed
  step (so a vacuum kicking in mid-run doesn't pollute the timing
  distribution). This means we are measuring the function in isolation, not
  its interaction with autovacuum. On production both costs add together;
  if anything we are *understating* v1's badness.
* The Zipf-like message distribution uses `random()` with a pinned seed,
  but Postgres `random()` is per-session. A different psql client version
  could in theory produce different RNG output. In practice we have not
  observed this on Postgres 16.
* The function uses an advisory transaction-level lock. Within a single
  bench process this never collides, so we always exercise the "real"
  branch (not the `'skipped'` one). If you ever see `returned_skipped=true`
  in `per_call.csv`, something is running the function concurrently from
  outside the bench.
