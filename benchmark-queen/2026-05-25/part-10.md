# Partition axis — `part-10` (11 partitions, 1 msg/push)

**Run:** `2026-04-25T07:13:56Z → 07:29:56Z` (15 min)
Window analyzed (analytics): `2026-04-25T07:13:56.000Z → 07:29:56.000Z`
Host: `root@138.68.70.90` (32 vCPU, 62 GiB RAM)
Queen: `smartnessai/queen-mq:0.14.0.alpha.3` · Postgres: upstream `postgres`

## Setup


| Parameter                                             | Value                                                         |
| ----------------------------------------------------- | ------------------------------------------------------------- |
| Queue name                                            | `bench-part-10`                                               |
| `MAX_PARTITION`                                       | **10**                                                        |
| Distinct partitions actually written                  | **11** (named `0..10`); `Default` partition unused            |
| `MSGS_PER_PUSH`                                       | **1**                                                         |
| Producer                                              | 1 worker × 50 conns, ESM cluster, `pipelining=1`              |
| Consumer                                              | 1 worker × 50 conns, `batch=100`, `wait=true`, `autoAck=true` |
| Queen env / Postgres tuning / Queue options / Cleanup | identical to `part-1`                                         |


## Producer — autocannon


| Metric                              | Value                         |
| ----------------------------------- | ----------------------------- |
| Total push requests                 | **5 639 191**                 |
| Total messages pushed               | **5 639 191**                 |
| Throughput                          | **6 266 req/s = 6 266 msg/s** |
| Latency p50 / p90 / p99 / avg / max | **7 / 8 / 12 / 7.5 / 94 ms**  |
| Errors / non-2xx / timeouts         | 0 / 0 / 0                     |


## Consumer — autocannon


| Metric                                            | Value                                                                                                    |
| ------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| Total pop requests                                | **857 331**                                                                                              |
| Throughput (req/s)                                | 953                                                                                                      |
| Implied messages popped (req × actual batch 6.57) | **5 632 656** msg                                                                                        |
| Latency p50 / p90 / p99 / avg / max               | **8 / 122 / 361 / 52 / 7 236 ms**                                                                        |
| Errors / non-2xx / timeouts                       | 0 / 0 / 0                                                                                                |
| Server-reported pop batch efficiency              | **6.57 msg/req** (down from 13.73 with 2 partitions — fan-out across more, smaller per-partition queues) |


## Server-side window aggregates (`/api/v1/status`, 1-min buckets)


| Metric                             | Value                                       |
| ---------------------------------- | ------------------------------------------- |
| **Steady-state ingest**            | **6 240 msg/s**                             |
| **Steady-state pop**               | **6 237 msg/s**                             |
| Max ingest / pop (1-min)           | 6 409 / 6 404 msg/s                         |
| Avg lag                            | **0.0 ms**                                  |
| Max lag                            | **74 ms**                                   |
| Queen CPU user % avg               | **287 %** (sys avg 503 %) → ~7.9 vCPU total |
| Queen RSS max                      | **35.0 MB**                                 |
| DB pool active avg                 | 2.41                                        |
| Errors (`db`/`ack`/`dlq`)          | 0 / 0 / 0                                   |
| `batchEfficiency` push / pop / ack | 1.00 / 6.57 / 6.57                          |


## Worker heartbeats (libqueen, 9 036 lines / 10 workers)


| Metric                     | min | p50 | p99 | max | avg     |
| -------------------------- | --- | --- | --- | --- | ------- |
| `push.p99rtt` (ms)         | 2   | 3   | 17  | 27  | **4.6** |
| `pop.p99rtt` (ms)          | 1   | 1   | 6   | 9   | **1.6** |
| `jobs/s` per worker        | 1   | 855 | 941 | 989 | **844** |
| `evl` (event-loop lag, ms) | 0   | 0   | 0   | 2   | 0.0     |


## Background services


| Service                           | Events | Notes                                      |
| --------------------------------- | ------ | ------------------------------------------ |
| `StatsService: cycle completed`   | 94     | min 0 ms, max **1 738 ms**, avg **101 ms** |
| `PartitionLookupReconcileService` | 56     | total **99 rows** fixed                    |
| `[error]` lines                   | **0**  | —                                          |


## Postgres state at end


| Metric                             | Value                                         |
| ---------------------------------- | --------------------------------------------- |
| **Cache hit ratio (DB)**           | **100.00 %** (650 disk reads only)            |
| Tables / Indexes hit ratio         | 100.00 % / 100.00 %                           |
| `messages` size                    | **2 234 MB** (980 MB heap + 1 254 MB indexes) |
| `partition_lookup` HOT-update %    | **99.86 %** (1.24 M HOT)                      |
| `partition_consumers` HOT-update % | 99.74 % (1.77 M HOT)                          |
| Dead tuples on `messages`          | 0 reported (vacuum keeping up)                |


## Queue final state


| Field               | Value                                         |
| ------------------- | --------------------------------------------- |
| Partitions reported | 12 (`0..10` + `Default`)                      |
| Total messages      | 5 639 241                                     |
| Completed           | 5 636 788                                     |
| **Pending at end**  | **2 453** (0.04 %) — consumer fully caught up |


## Live container resources (snapshot after `DURATION` ended)


| Container | CPU %   | Mem      |
| --------- | ------- | -------- |
| postgres  | 54.74 % | 4.45 GiB |
| queen     | 0.66 %  | 29.9 MiB |


## Headline takeaways for `part-10`

- **Push throughput effectively unchanged** vs `part-1` (6 266 vs 6 366 msg/s — ~1.6 % slower, within noise). The producer is HTTP-RPS-bound at this batch size, partition count is irrelevant up to the connection pool's parallelism.
- **Consumer p99 dropped from 2.16 s → 361 ms** (≈ 6× better). With 11 partitions instead of 2, the long-poll on `wait=true` resolves much faster — fewer empty-queue stalls.
- **Pop batch efficiency dropped** from 13.73 → 6.57: with the same 6 266 msg/s spread over 11 partitions instead of 2, each per-partition queue is shallower, so a single pop drains fewer messages on average. Net effect: more pop requests (953/s vs 463/s) for the same total messages.
- **Queen CPU went up slightly** (287 % vs 270 % user) — extra cost is in the partition-fan-out (lookup + lease creation per partition).
- **Postgres footprint nearly identical**: same 100 % cache, same `messages` table size class. Partition count changes are negligible at the row-count and index-size level.
- **Pending at end ≈ 2.5k** vs 123k in `part-1` — better fan-out lets consumer fully catch up.
- **No errors anywhere**, max-lag 74 ms, p99rtt < 20 ms throughout. System is bored.