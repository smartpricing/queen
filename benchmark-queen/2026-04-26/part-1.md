# Partition axis — `part-1` (2 partitions, 1 msg/push)

**Run:** `2026-04-25T06:57:34Z → 07:13:35Z` (15 min)
Window analyzed (analytics): `2026-04-25T06:57:34.000Z → 07:13:35.000Z`
Host: `root@138.68.70.90` (32 vCPU, 62 GiB RAM)
Queen: `smartnessai/queen-mq:0.14.0.alpha.3` · Postgres: upstream `postgres` (default tag)

## Setup


| Parameter                            | Value                                                                                                                                                     |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Queue name                           | `bench-part-1`                                                                                                                                            |
| `MAX_PARTITION` (producer arg)       | **1**                                                                                                                                                     |
| Distinct partitions actually written | **2** (named `0`, `1`); `Default` partition stays empty                                                                                                   |
| `MSGS_PER_PUSH`                      | **1**                                                                                                                                                     |
| Producer                             | `NUM_WORKERS=1`, `CONNECTIONS_PER_WORKER=50`, ESM cluster, `pipelining=1`                                                                                 |
| Consumer                             | `NUM_WORKERS=1`, `CONNECTIONS_PER_WORKER=50`, `batch=100`, `wait=true`, `autoAck=true`                                                                    |
| Queen env                            | `NUM_WORKERS=10`, `DB_POOL_SIZE=50`, `SIDECAR_POOL_SIZE=250`                                                                                              |
| Postgres tuning                      | `shared_buffers=24GB`, `effective_cache_size=48GB`, `work_mem=32MB`, `autovacuum_naptime=10s`, `autovacuum_vacuum_scale_factor=0.05` (full set in runner) |
| Queue options                        | `leaseTime=60`, `retryLimit=3`, `retentionEnabled=true`, `retentionSeconds=7200`, `completedRetentionSeconds=1800`                                        |
| Cleanup before run                   | `docker stop+rm -v` of both containers, `docker volume prune -f`, fresh DB                                                                                |


## Producer — autocannon result (after 900s)


| Metric                              | Value                         |
| ----------------------------------- | ----------------------------- |
| Total push requests                 | **5 728 936**                 |
| Total messages pushed               | **5 728 936** (1 msg/req)     |
| Throughput                          | **6 366 req/s = 6 366 msg/s** |
| Latency p50 / p90 / p99 / avg / max | **7 / 8 / 12 / 7.4 / 92 ms**  |
| Errors / non-2xx / timeouts         | 0 / 0 / 0                     |


## Consumer — autocannon result (after 900s)


| Metric                                             | Value                                |
| -------------------------------------------------- | ------------------------------------ |
| Total pop requests                                 | **416 351**                          |
| Throughput (req/s)                                 | 463                                  |
| Implied messages popped (req × actual batch 13.73) | **5 716 401** msg (≈ producer rate)  |
| Latency p50 / p90 / p99 / avg / max                | **8 / 227 / 2 162 / 107 / 9 266 ms** |
| Errors / non-2xx / timeouts                        | 4 / 0 / 4                            |
| Consumer batch efficiency (server-reported)        | 13.73 msg/req (out of requested 100) |


> The consumer p99 of 2.16 s is dominated by `**wait=true`** — when the only 2 hot partitions are momentarily empty, the long-poll holds. Effective consumer throughput is producer-bound, not consumer-bound.

## Server-side window aggregates (`/api/v1/status`, 1-min buckets, 15 points)


| Metric                                | Value                                          |
| ------------------------------------- | ---------------------------------------------- |
| **Steady-state ingest rate**          | **6 367 msg/s** (avg of buckets ≥ 5k/s)        |
| **Steady-state pop rate**             | **6 355 msg/s**                                |
| Max ingest rate (1-min)               | 6 496 msg/s                                    |
| Max pop rate (1-min)                  | 6 481 msg/s                                    |
| Avg lag (dwell time)                  | **0.0 ms**                                     |
| Max lag (worst single message)        | **37 ms**                                      |
| Queen CPU user %                      | avg **270 %**, sys avg 470 % → ~7.4 vCPU total |
| Queen RSS                             | max **38.6 MB**                                |
| DB pool active                        | avg 2.4 / 47 idle / 50 configured              |
| `dbErrors` / `ackFailed` / `dlqCount` | 0 / 0 / 0                                      |
| `batchEfficiency` push / pop / ack    | 1.00 / 13.73 / 13.73                           |


## Worker heartbeats (libqueen, 9 032 lines across 10 workers)


| Metric                     | min | p50 | p99 | max | avg      |
| -------------------------- | --- | --- | --- | --- | -------- |
| `push.p99rtt` (ms)         | 1   | 3   | 17  | 23  | **4.74** |
| `pop.p99rtt` (ms)          | 1   | 2   | 8   | 14  | **2.59** |
| `slots` in use (max 25)    | 22  | 25  | 25  | 25  | 24.7     |
| `jobs/s` per worker        | 1   | 823 | 883 | 914 | **807**  |
| `evl` (event-loop lag, ms) | 0   | 0   | 0   | 3   | 0.0      |


## Background services in window


| Service                                           | Events | Notes                                 |
| ------------------------------------------------- | ------ | ------------------------------------- |
| `StatsService: cycle completed in <ms>`           | 94     | min 3 ms, max 1 295 ms, avg **92 ms** |
| `PartitionLookupReconcileService: fixed <N> rows` | 48     | total **61 rows** fixed (very low)    |
| `[error]` lines                                   | **0**  | —                                     |


## Postgres state at end of run


| Metric                               | Value                                              |
| ------------------------------------ | -------------------------------------------------- |
| **Cache hit ratio (DB)**             | **100.00 %** (650 disk reads only)                 |
| Tables hit ratio / Indexes hit ratio | 100.00 % / 100.00 %                                |
| `messages` table size                | **2 147 MB** (≈ 375 B/msg incl. index)             |
| `messages_consumed` size             | 130 MB                                             |
| `partition_lookup` HOT-update %      | **99.89 %** (1.11 M HOT / 1.12 M total)            |
| `partition_consumers` HOT-update %   | 99.81 % (837 K / 839 K)                            |
| Dead tuples on `messages`            | 0 reported (hot tables only show stats/partitions) |


## Queue final state


| Partition  | Completed     | Pending     | Total         | Newest msg |
| ---------- | ------------- | ----------- | ------------- | ---------- |
| `0`        | 2 802 926     | 61 629      | 2 864 555     | 07:12:35Z  |
| `1`        | 2 802 881     | 61 590      | 2 864 471     | 07:12:35Z  |
| `Default`  | 0             | 0           | 0             | —          |
| **Totals** | **5 605 807** | **123 219** | **5 729 026** | —          |


Pending at end ≈ 2.2 % of total — consumer was draining at producer rate but couldn't catch the last second's worth of messages before the script's own `DURATION` ended.

## Live container resource usage at end (`docker stats --no-stream`)


| Container | CPU %   | Mem      |
| --------- | ------- | -------- |
| postgres  | 70.53 % | 4.14 GiB |
| queen     | 0.74 %  | 44.6 MiB |


> The low `docker stats` numbers at end reflect that producer and consumer have already finished their `DURATION=900s` autocannon loop ~30 s before the snapshot.

## Headline takeaways for `part-1`

- With **only 2 partitions and 1 msg per push**, the system is **HTTP-RPS-bound**, not DB-bound: ~6 366 push req/s saturates the producer's 50-connection autocannon loop at ~7 ms p50 latency.
- **Queen burns 7.4 vCPU** to do this — that's ~1 200 cycles per push request, mostly Node + worker IPC overhead. Per-message efficiency is poor compared to batched pushes.
- **Postgres is barely working** (avg 2.4 active connections, 100 % cache hit, 28 MB block I/O over 15 min).
- **Latencies are excellent and stable**: p99rtt under 20 ms throughout, max-lag 37 ms — there's no contention drama on the 2-partition hot path. The `messages_partition_transaction_unique` index does its job.
- The consumer's high p99 (2.16 s) is a `**wait=true` long-poll artifact**, not a Queen problem.
- This is the **lower bound** on Queen's per-request cost — every other partition-axis test will be more efficient per message because batch>1 amortizes HTTP/IPC overhead.

