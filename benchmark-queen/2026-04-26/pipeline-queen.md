# Queen pipeline benchmark — 2026-04-27

## Test setup

A 4-stage realistic pipeline using real `queen-mq` JS clients orchestrated by
`pm2`, modelled on a typical event-driven backend:

```
[producer ×2] ──push──▶  pipe-q1  ──pop──▶  [worker ×7] ──push──▶  pipe-q2 ─┬─pop──▶ [analytics ×7]
                                                                              │
                                                                              └─pop──▶ [log ×7]
```


| component                   | configuration                                                                                                      |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| Queen image                 | `smartnessai/queen-mq:0.14.0.alpha.5`                                                                              |
| `queen-mq` client           | `0.14.0001`                                                                                                        |
| Postgres                    | `postgres:16` (24 GB shared_buffers, `synchronous_commit=on`)                                                      |
| VM                          | DigitalOcean, 16 vCPU / 64 GB RAM                                                                                  |
| Run duration                | 20 min total (3-min warm-up discarded for percentiles)                                                             |
| Partitions per queue        | 1 000                                                                                                              |
| Producers                   | 2 processes, 40 concurrent in-flight pushes each, 5 000 msg/s target each                                          |
| Push shape                  | **single message per HTTP call** to a random partition                                                             |
| Workers                     | 7 processes, batch=100, concurrency=10, partitions/pop=10, **at-least-once** semantics (push q2, then ack q1)      |
| Analytics consumers         | 7 processes, batch=100, concurrency=10, partitions/pop=10 (group=`analytics`)                                      |
| Log consumers               | 7 processes, batch=100, concurrency=10, partitions/pop=10 (group=`log`)                                            |
| Per-message work simulation | 5–20 ms long-tail sleep, run in parallel via `Promise.all` inside each batch                                       |
| Pre-creation                | 1 000 partitions per queue warmed sequentially before the benchmark to avoid the partition-creation deadlock burst |


The pipeline preserves per-partition ordering end-to-end. Each pop call drains
up to 10 partitions in a single round-trip; the worker then groups results
by their q1 partition and forwards each group to the same q2 partition with
parallel push calls (so q1 partition X → q2 partition X).

## Headline numbers

Steady state (after 3-min warm-up, over the remaining 17 min):


| metric                   | value                                               |
| ------------------------ | --------------------------------------------------- |
| **Producer push rate**   | **3,689 msg/s** aggregate (2 procs × 1,845)         |
| **Worker drain rate**    | **3,688 msg/s** aggregate (7 procs × 527)           |
| **Analytics drain rate** | **3,688 msg/s** (matches worker — q2 stays drained) |
| **Log drain rate**       | **3,688 msg/s** (matches worker — q2 stays drained) |
| **Total pipeline ops/s** | ~22 000 (push + pop + push + ack across stages)     |
| Producer txids in window | 3 763 274                                           |
| Reached worker           | 3 761 674 (**99.96 %**)                             |
| Reached analytics        | 3 761 451 (**99.95 %**)                             |
| Reached log              | 3 761 674 (**99.96 %**)                             |
| Duplicate processing     | **0** (worker, analytics, log)                      |
| Deadlocks observed       | **3** (across 20 min, all absorbed by failover)     |
| File-buffer files at end | **0**                                               |
| q1 final state           | 1 001 partitions, 123 pending, 4 428 773 completed  |
| q2 final state           | 1 001 partitions, 223 pending, 4 429 104 completed  |


Producer and worker are **throughput-balanced**: q1 backlog stays at the
in-flight floor (~100 messages) instead of growing.

## Latency profile (steady state, ms)


| stage                             | p50     | p90 | p99       | p99.9 | max    | avg |
| --------------------------------- | ------- | --- | --------- | ----- | ------ | --- |
| **end-to-end producer→analytics** | **359** | 617 | **1,024** | 2,907 | 15,464 | 399 |
| end-to-end producer→log           | 358     | 616 | 1,021     | 2,933 | 15,700 | 398 |
| q1 → worker (queue lag at q1)     | 213     | 424 | 705       | 2,408 | 15,387 | 253 |
| q2 → analytics (queue lag at q2)  | 114     | 294 | 514       | 949   | 3,198  | 146 |
| q2 → log (queue lag at q2)        | 113     | 293 | 511       | 963   | 3,317  | 145 |


p99 end-to-end through both stages of the pipeline lands at **just over 1
second**. The 15 s tail outliers come from rare q1 partitions that stay
unclaimed for several pop cycles when the candidate-scan order doesn't pick
them up; tunable with worker count and `partitions/pop`, but representative
of this configuration.

## Resource usage (steady state)


| container | CPU                | memory                                | network I/O       | block I/O       |
| --------- | ------------------ | ------------------------------------- | ----------------- | --------------- |
| queen     | ~390 % (3.9 vCPU)  | ~70 MB                                | 30.3 GB / 47.7 GB | 0               |
| postgres  | ~1 500 % (15 vCPU) | grew 2.5 GB → **16.7 GB** over 20 min | 10.4 GB / 16.4 GB | 66.7 GB written |


**Postgres carries the load**, queen's C++ engine stays light at 70 MB and
~3.9 vCPU. The PG memory growth is the `messages` table (4.4 M completed
rows + indexes) plus its working set; the block I/O is mostly WAL because
`synchronous_commit=on`.

## What's interesting

1. **3 deadlocks in 20 min, all absorbed by the file-buffer failover, 0 message
  loss.** The partition warmup eliminates the `queue_lag_metrics`
   ON-CONFLICT contention from the partition-creation trigger.
2. **0 duplicates across the whole run.** At-least-once semantics held; the
  q2 push always landed before the q1 ack, and ack(q1) never failed in a
   way that caused redelivery.
3. **q2 fan-out is essentially free.** Both `analytics` and `log` consumer
  groups drained q2 at exactly the same rate as the worker pushed in.
   Adding a third or fourth tail consumer would not have stressed q2 here.
4. **Pipeline is genuinely PG-bound, not Queen-bound.** queen container at
  ~3.9 vCPU and 70 MB while postgres is at ~15 vCPU and 16 GB. Further
   pipeline scaling is a Postgres-resourcing question.
5. **99.96 % delivery completeness.** The ~1 600 producer txids that didn't
  reach the worker are messages still in flight at the analyze cutoff
   (q1 final pending = 123, plus ~1 500 in-transit). With a 30 s drain
   tail these would have completed.

## Configuration that mattered

- **NUM_PARTITIONS=1000** with sequential warm-up (one push per partition before
the benchmark): each partition has enough depth for batch pops to fill, and
no partition-creation deadlock burst on cold start.
- `**partitions(10)` per consumer**: every pop drains up to 10 partitions in
one round-trip, sharing a global `batch=100` budget. This is the dominant
reason the pipeline is throughput-balanced.
- **Per-batch handler (no `.each()`)**: one push to q2 per batch (parallel
per-partition via `Promise.all`) and one batch-ack call. ~10× reduction
in HTTP round-trips for the same message count vs per-message handlers.
- `**synchronous_commit=on`**: kept (the durability tier we report for Queen
everywhere). Each push survives a crash before the ack returns.

## Reproducing

```bash
ssh root@<host>
cd /root/pipeline-runs/queen
DURATION_SEC=1200 WARMUP_SEC=180 \
  NUM_PRODUCERS=2 NUM_WORKERS=7 NUM_ANALYTICS=7 NUM_LOG=7 \
  NUM_PARTITIONS=1000 TARGET_RATE=5000 IN_FLIGHT=40 \
  CONCURRENCY=10 BATCH_SIZE=100 \
  MAX_PARTITIONS_PER_POP=10 \
  QUEEN_IMAGE_TAG=0.14.0.alpha.5 \
  ./runner.sh
```

Outputs land in `/root/bench-runs/pipeline-results/queen-<timestamp>/` and
contain: per-process JSONL files, queen.log, postgres-stats.json, container
docker-stats, queue resource snapshots, the analyze.js summary, and run
metadata.

## Caveats / honest limits

- **PG is the bottleneck.** Producer push rate (3.7 k/s) is set by how fast
PG absorbs the ~22 k mixed ops/s the whole pipeline generates, not by
queen's HTTP layer. More PG capacity → more end-to-end throughput.
- **15-second max latency.** Most of those tail outliers come from rare
`q1→worker` waits when a partition stays unclaimed for several pop cycles.
Tuneable with worker count or `partitions/pop`, but a real number for
this configuration.
- **One image, one VM size, one workload shape** (5–20 ms `setTimeout`
work simulation, no CPU). CPU-bound real work would change the worker
arithmetic significantly.
- Run artifacts: `/root/bench-runs/pipeline-results/queen-20260427T111403Z/`

