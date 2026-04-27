# Queen pipeline benchmark — 2026-04-27

## Test setup

A 4-stage realistic pipeline using real `queen-mq` JS clients orchestrated
by `pm2`, exercising what a typical event-driven backend looks like:

```
[producer ×2] ──push──▶  pipe-q1  ──pop──▶  [worker ×7] ──push──▶  pipe-q2 ─┬─pop──▶ [analytics ×7]
                                                                              │
                                                                              └─pop──▶ [log ×7]
```

| component | configuration |
|---|---|
| Queen image | `smartnessai/queen-mq:0.14.0.alpha.3` |
| Postgres image | `postgres:16` (24 GB shared_buffers, synchronous_commit=on) |
| VM | DigitalOcean, 16 vCPU / 64 GB RAM |
| Run duration | 20 min total (3-min warm-up discarded for percentiles) |
| Partitions per queue | **1000** |
| Producers | 2 processes, 40 concurrent in-flight pushes each, 5000 msg/s target each |
| Push shape | **single message per HTTP call** to a random partition |
| Workers | 7 processes, batch=100, concurrency=10, **at-least-once** semantics (push q2 then ack q1) |
| Analytics consumers | 7 processes, batch=100, concurrency=10 (group=`analytics`) |
| Log consumers | 7 processes, batch=100, concurrency=10 (group=`log`) |
| Per-message work simulation | 5–20 ms long-tail sleep, run in parallel via `Promise.all` inside each batch |
| Pre-creation | 1000 partitions per queue warmed sequentially before the benchmark to avoid the partition-creation deadlock burst |

The pipeline preserves per-partition ordering end-to-end: because Queen's pop
claims one partition per call (advisory lock), every batch popped from q1
shares the same q1 partition; we forward to the same q2 partition with a
single push call.

## Headline numbers

Steady state (after 3-min warm-up, over the remaining 17 min):

| metric | value |
|---|---|
| **Producer push rate (steady)** | **5,345 msg/s** aggregate (2 procs × 2,672) |
| **Worker drain rate (steady)** | **1,022 msg/s** aggregate (7 procs × ~146) |
| **Analytics drain rate (steady)** | **1,022 msg/s** (matches worker — q2 stays drained) |
| **Log drain rate (steady)** | **1,022 msg/s** (matches worker — q2 stays drained) |
| **Total pipeline ops/s** | ~12,300 (push + pop + push + ack across stages) |
| Producer txids in window | 5,451,440 |
| Reached worker | 1,042,178 (19.1 %) |
| Reached analytics | 1,042,178 (19.1 %) |
| Reached log | 1,042,178 (19.1 %) |
| Duplicate processing | **0** (worker, analytics, log) |
| Deadlocks observed | **5** (across 20 min, all absorbed by failover) |
| File-buffer files at end | **0** |
| q1 final state | 1,001 partitions, 5,138,325 pending, 1,262,944 completed |
| q2 final state | 1,001 partitions, **0 pending**, 1,263,723 completed |

## Latency profile (steady state, ms)

| stage | p50 | p90 | p99 | p99.9 | max | avg |
|---|---:|---:|---:|---:|---:|---:|
| **end-to-end producer→analytics** | 96 | 2,955 | 221,546 | 751,245 | 1,001,537 | 8,103 |
| end-to-end producer→log | 97 | 2,956 | 221,739 | 751,477 | 1,001,404 | 8,106 |
| q1 → worker (queue lag at q1) | 60 | 2,747 | 219,946 | 751,167 | 1,001,092 | 8,002 |
| q2 → analytics (queue lag at q2) | 30 | 209 | 1,062 | 6,465 | 7,072 | 101 |
| q2 → log (queue lag at q2) | 30 | 215 | 1,169 | 7,093 | 7,519 | 104 |

**Two regimes are visible in those numbers**:

- The **q2 stage** is balanced: worker pushes to q2 at ~1 k/s, analytics and log each drain q2 at ~1 k/s. p50 = 30 ms, p99 ≈ 1 s, p99.9 ≈ 7 s. **q2 stays drained.**
- The **q1 stage** is saturation-limited: producer pushes 5.3 k/s, workers drain 1.0 k/s, so q1 backlog grows at ~4.3 k/s. Messages that miss the worker pop on first attempt sit in q1 for many minutes — that's the source of the multi-second p90, multi-minute p99, and 1000-second max.

If you cut the producer rate to ~1 k/s (matching worker drain), the q1 lag collapses to the q2 profile and end-to-end p99 drops below 1 s.

## Resource usage (steady state)

| container | CPU | memory | block I/O |
|---|---:|---:|---:|
| queen | ~340 % (3.4 vCPU) | ~75 MB | none (PG-backed) |
| postgres | ~800 % (8 vCPU) | grew from 2 GB → 10.5 GB over 20 min | 52.5 GB written |

**Postgres carries the load**, queen's C++ engine stays light on both CPU and
memory. The PG memory growth is q1's pending-message backlog (5.1 M rows at
end) plus indexes; the block I/O is mostly WAL because `synchronous_commit=on`.

## What's interesting

1. **5 deadlocks in 20 min, all absorbed by file-buffer failover, 0 message loss.**
   Compared to the very first version of this same test (1,847 deadlocks
   without partition pre-creation, sometimes 209 even with smaller scale),
   the partition warmup completely silences the
   `queue_lag_metrics` ON-CONFLICT contention from the
   `trg_partitions_created_counter` trigger.

2. **0 duplicates across the whole run.**
   At-least-once semantics held; the q2 push always landed before the q1
   ack, and ack(q1) never failed in a way that caused redelivery.

3. **q2 fan-out is essentially free.** Both `analytics` and `log` consumer
   groups drained q2 at exactly the same rate as the worker pushed in.
   Adding a third or fourth tail consumer would not have stressed q2 here.

4. **q1 saturation = per-(queue,group) pop ceiling, not Queen-side resource exhaustion.**
   Queen ran at 3.4 vCPU and 75 MB, postgres at 8 vCPU and 10 GB while q1
   filled to 5 M pending. The bottleneck is the rate at which one consumer
   group can issue partition-claiming pop+ack ops against a queue, not
   server-side capacity. Splitting workers across more consumer groups
   (with broadcast-style fan-out) would scale linearly, but that changes
   the semantics from "shared work" to "every group sees every message".

5. **Per-pop batch size matters more than `batch=N` setting.** Pop returns
   "up to N from one partition" (advisory-lock claim). With 1,000 partitions
   and 5.3 k push/s spread randomly, each partition holds ≈3–10 messages
   when claimed, so effective harvest is ~5 msgs/pop, not 100. This is the
   reason the worker rate is ~150 msg/s/process even though
   `concurrency=10 × batch=100` looks like 1,000 in-flight slots.

## Configuration that mattered

- **NUM_PARTITIONS=1000** (down from 10 000): with 10 k partitions × random
  push the per-partition harvest collapses to 1–3 msgs/pop.
- **Per-batch handler (no `.each()`)**: one push call per batch (not per msg)
  and one batch ack call. ~10× reduction in HTTP round-trips for the same
  message count.
- **Pre-warming partitions**: drops deadlock count from ~200 (or 1,847 in
  the very first un-tuned smoke) to ~5.
- **`synchronous_commit=on`**: kept (this is the durability tier we report
  for Queen everywhere). Each push survives a crash before the ack returns.

## Reproducing

```bash
ssh root@<host>
cd /root/pipeline-runs/queen
DURATION_SEC=1200 WARMUP_SEC=180 \
  NUM_PRODUCERS=2 NUM_WORKERS=7 NUM_ANALYTICS=7 NUM_LOG=7 \
  NUM_PARTITIONS=1000 TARGET_RATE=5000 IN_FLIGHT=40 \
  CONCURRENCY=10 BATCH_SIZE=100 \
  ./runner.sh
```

Outputs land in `/root/bench-runs/pipeline-results/queen-<timestamp>/` and
contain: per-process JSONL files, queen.log, postgres-stats.json, container
docker-stats, queue resource snapshots, the analyze.js summary and the run
metadata.

## Caveats / honest limits

- Producer outran the worker drain ~5× — the test is **producer-saturated, not pipeline-balanced**. The end-to-end p99/p999/max numbers reflect q1 backlog growth rather than steady-state pipeline latency. **The 96 ms p50** is the right number for "happy-path end-to-end".
- We measured one queen-image version (`0.14.0.alpha.3`) on one VM size. Throughput per worker is per-(queue,group) bound; reading more processes through the same group doesn't add throughput.
- The work simulation is a `setTimeout` of 5–20 ms; CPU-bound real work would alter the worker arithmetic significantly.
- Run artifacts: `/root/bench-runs/pipeline-results/queen-20260427T080752Z/`
