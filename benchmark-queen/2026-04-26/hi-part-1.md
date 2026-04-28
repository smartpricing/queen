# High-load partition axis — `hi-part-1` (2 partitions, 1 msg/push, 500 producer conns)

**Tune-up run** — first high-load test before committing to the full axis.

**Run:** `2026-04-25T07:53:38Z → 08:09:54Z` (15 min)
Window analyzed: `2026-04-25T07:53:38.000Z → 08:09:54.000Z`
Host: `root@138.68.70.90` (32 vCPU, 62 GiB RAM)
Queen: `smartnessai/queen-mq:0.14.0.alpha.3` · Postgres: upstream `postgres`

## Setup


| Parameter          | Value                                                                                |
| ------------------ | ------------------------------------------------------------------------------------ |
| Queue name         | `bench-hi-part-1`                                                                    |
| `MAX_PARTITION`    | **1** (2 distinct partitions written: `0`, `1`; `Default` empty)                     |
| `MSGS_PER_PUSH`    | **1**                                                                                |
| Producer           | **5 workers × 100 connections = 500 total**, ESM cluster, `pipelining=1`             |
| Consumer           | **2 workers × 50 connections = 100 total**, `batch=100`, `wait=true`, `autoAck=true` |
| Queen env          | `NUM_WORKERS=10`, `DB_POOL_SIZE=50`, `SIDECAR_POOL_SIZE=250`                         |
| Postgres tuning    | identical to long-running setup (24 GB shared_buffers, etc.)                         |
| Cleanup before run | full `docker rm -v` + `docker volume prune -f`                                       |


## Producer — autocannon (5-cluster aggregated)


| Metric                              | Value                            |
| ----------------------------------- | -------------------------------- |
| Total push requests                 | **26 536 356**                   |
| Total messages pushed               | **26 536 356**                   |
| Throughput                          | **29 487 req/s = 29 487 msg/s**  |
| Latency p50 / p90 / p99 / avg / max | **15 / 19 / 44 / 16.5 / 203 ms** |
| Errors / non-2xx / timeouts         | 0 / 0 / 0                        |


## Consumer — autocannon (2-cluster aggregated)


| Metric                                               | Value                                                             |
| ---------------------------------------------------- | ----------------------------------------------------------------- |
| Total pop requests                                   | **308 865**                                                       |
| Throughput (req/s)                                   | 343                                                               |
| Effective messages popped (req × 83.27 actual batch) | **25 716 069** msg ≈ **28 573 msg/s**                             |
| Latency p50 / p90 / p99 / avg / max                  | **59 / 364 / 4 199 / 287 / 9 322 ms**                             |
| Errors / non-2xx / timeouts                          | 96 / 0 / **96**                                                   |
| Server-reported pop batch efficiency                 | **83.27 msg/req** (out of 100 requested — partitions stayed deep) |


> The 96 consumer "timeouts" are autocannon's client-side 10s ceiling firing on long-poll responses that exceeded it. **These are not server errors** — server reports 0 ackFailed / 0 dbErrors. Server-side max msg dwell-time was only 112 ms.

## Server-side window aggregates (`/api/v1/status`, 1-min buckets)


| Metric                             | Value                                                |
| ---------------------------------- | ---------------------------------------------------- |
| **Steady-state ingest**            | **28 751 msg/s**                                     |
| **Steady-state pop**               | **27 849 msg/s**                                     |
| Max ingest / pop (1-min)           | 30 576 / 29 697 msg/s                                |
| Min ingest (any 1-min bucket)      | 19 066 msg/s (warm-up)                               |
| Avg lag (msg dwell time)           | **0 ms**                                             |
| **Max lag**                        | **112 ms**                                           |
| Avg event-loop lag / max           | 0 / **7 ms**                                         |
| **Queen CPU user %**               | avg **857 %** (sys avg 1 864 %) → **~27 vCPU total** |
| Queen RSS max                      | **63 MB**                                            |
| DB pool active avg / max           | **2.54 / 2.9** (out of 47 idle / 50 configured)      |
| Errors (`db`/`ack`/`dlq`)          | 0 / 0 / 0                                            |
| `batchEfficiency` push / pop / ack | 1.00 / **83.27** / 83.27                             |


## Worker heartbeats (libqueen, 9 032 lines / 10 workers)


| Metric                     | min | p50   | p99    | max    | avg       |
| -------------------------- | --- | ----- | ------ | ------ | --------- |
| `push.p99rtt` (ms)         | 10  | 32    | 47     | 52     | **32.0**  |
| `pop.p99rtt` (ms)          | 7   | 20    | 77     | 137    | **22.4**  |
| `push.q` queue depth       | 0   | 0     | **45** | **50** | 2.1       |
| `pop.q` queue depth        | 0   | 0     | 5      | 11     | 0.5       |
| `slots` in use (max 25)    | 21  | 24    | 25     | 25     | **24.4**  |
| `jobs/s` per worker        | 0   | 3 091 | 3 333  | 3 448  | **3 032** |
| `evl` (event-loop lag, ms) | 0   | 0     | 1      | 7      | 0.0       |


> **Push queue depth p99 = 45** is the most interesting heartbeat number. Under low load it was max 5; here workers occasionally accumulate up to 50 queued push jobs before draining. This is the first sign of meaningful pressure on the per-worker pipeline. Slots stayed near max (24.4/25 average) — workers were essentially always full.

## Background services


| Service                           | Events | Notes                                      |
| --------------------------------- | ------ | ------------------------------------------ |
| `StatsService: cycle completed`   | 96     | min 6 ms, max **7 037 ms**, avg **448 ms** |
| `PartitionLookupReconcileService` | 53     | total **86 rows** fixed                    |
| `[error]` lines                   | **0**  | no statement-timeout errors                |


> StatsService cycle max went from ~1.7 s (low load) to ~7 s here. Still under the 30 s `statement_timeout` ceiling that fired in the long-running test, so no errors — but trending upward with load. Worth watching.

## Postgres state at end


| Metric                             | Value                                                                                |
| ---------------------------------- | ------------------------------------------------------------------------------------ |
| **Cache hit ratio (DB)**           | **100.00 %** (650 disk reads only — entire working set fits in 24 GB shared_buffers) |
| `messages` size                    | **10 159 MB** (4 509 MB heap + 5 649 MB indexes)                                     |
| `messages_consumed` size           | 84 MB                                                                                |
| `partition_lookup` HOT-update %    | **99.62 %** (728 911 HOT / 731 655 total)                                            |
| `partition_consumers` HOT-update % | 99.37 % (616 192 HOT)                                                                |
| Postgres mem usage at end of run   | **13.57 GiB** (filling shared_buffers)                                               |
| Active queries at snapshot time    | 0                                                                                    |


## Queue final state


| Partition  | Completed      | Pending     | Total          | Newest msg |
| ---------- | -------------- | ----------- | -------------- | ---------- |
| `0`        | 13 146 805     | 118 595     | 13 265 400     | 08:08:38Z  |
| `1`        | 13 146 416     | 118 590     | 13 265 006     | 08:08:38Z  |
| `Default`  | 0              | 0           | 0              | —          |
| **Totals** | **26 293 221** | **237 185** | **26 530 406** | —          |


Pending at end ≈ **0.9 %** of total — consumer was within ~30 s of catching up.

## Live container resources after producer/consumer ended


| Container | CPU %         | Mem       |
| --------- | ------------- | --------- |
| postgres  | 0.09 % (idle) | 13.57 GiB |
| queen     | 0.90 % (idle) | 59.1 MiB  |


## Headline takeaways for `hi-part-1`

- **Push throughput: 6 366 → 29 487 msg/s** (4.6× the low-load number). The 10× increase in client connections (50 → 500) yielded 4.6× more throughput before queen workers became the bottleneck.
- **Pop kept up**: 27 849 msg/s steady, ~0.9 % pending at end. With only 2 partitions and 100 consumer connections, the **server delivered nearly full 100-msg batches (83.27 actual)** because the partitions are deep enough at any pop instant to fill the request.
- **Queen CPU jumped from ~7.7 vCPU → ~27 vCPU** — that's where the budget went. With 32 vCPU total host, queen + producer + consumer + postgres are now using maybe 35-40 vCPU sustained (oversubscribed but workable).
- **Postgres remains underutilized**: 100 % cache hit, 2.5 active connections out of 47 idle, 0 active queries at snapshot. Postgres has *enormous* headroom even at this load.
- **Latencies stayed great**:
  - Producer p99 = 44 ms (was 12 ms at low load — 3.7× worse, still excellent)
  - Server-side msg dwell-time max = 112 ms
  - Server event-loop lag max = 7 ms
- **0 errors anywhere** — no DLQ, no ackFailed, no dbErrors, no statement timeouts.
- **First sign of internal queueing**: per-worker push queue depth p99 = 45 (was max 5 at low load). Workers are full but still draining within the heartbeat tick.
- **The 2-partition extreme worked surprisingly well**: with 500 producer connections hammering 2 partitions, we expected lock contention on `partition_lookup` row updates. Instead, HOT updates kept the contention off the indexes (99.62 % HOT) and the per-partition INSERT path is concurrent enough that throughput scaled cleanly.

## Recommendation

This config is sound — proceed with the full high-load axis using **producer 5×100 + consumer 2×50** for `hi-part-{10, 100, 1000, 10000}`. Predictions:

- `hi-part-10`/`hi-part-100`: similar push throughput (29-32k msg/s), pop batch efficiency drops as fan-out widens (83 → ~30-50 → ~10-15), pop request rate climbs.
- `hi-part-1000`: same range; pop batch efficiency probably ~5-10.
- `hi-part-10000`: this is the interesting one — `partition_lookup` table grows to 10 001 rows, autovacuum churn increases. Watch StatsService cycle max (might cross the 30 s timeout under this load) and disk space (`messages` table will be ~10 GB per run × 5 runs = ~50 GB cumulative if not cleaned, but we clean between runs so not an issue).

Resource ceiling check: at 27 vCPU queen + ~3 vCPU postgres + ~4 vCPU autocannon, we're using ~34 vCPU on a 32 vCPU host — slightly oversubscribed. Throughput numbers are real but we'd see ~10-20% more headroom on a beefier host.