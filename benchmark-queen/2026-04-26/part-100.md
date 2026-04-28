# Partition axis — `part-100` (101 partitions, 1 msg/push)

**Run:** `2026-04-25T07:30:18Z → 07:46:18Z` (15 min)
Window analyzed (analytics): `2026-04-25T07:30:18.000Z → 07:46:18.000Z`
Host: `root@138.68.70.90` (32 vCPU, 62 GiB RAM)
Queen: `smartnessai/queen-mq:0.14.0.alpha.3` · Postgres: upstream `postgres`

## Setup

| Parameter | Value |
|---|---|
| Queue name | `bench-part-100` |
| `MAX_PARTITION` | **100** |
| Distinct partitions actually written | **101** (`0..100`); `Default` partition unused (102 reported in queue resource) |
| `MSGS_PER_PUSH` | **1** |
| Producer | 1 worker × 50 conns, ESM cluster, `pipelining=1` |
| Consumer | 1 worker × 50 conns, `batch=100`, `wait=true`, `autoAck=true` |
| Queen / Postgres / Queue options / Cleanup | identical to `part-1` & `part-10` |

## Producer — autocannon

| Metric | Value |
|---|---|
| Total push requests | **5 428 513** |
| Total messages pushed | **5 428 513** |
| Throughput | **6 032 req/s = 6 032 msg/s** |
| Latency p50 / p90 / p99 / avg / max | **7 / 9 / 12 / 7.8 / 105 ms** |
| Errors / non-2xx / timeouts | 0 / 0 / 0 |

## Consumer — autocannon

| Metric | Value |
|---|---|
| Total pop requests | **1 175 626** |
| Throughput (req/s) | 1 306 |
| Implied messages popped (req × actual batch 4.62) | **5 423 360** msg |
| Latency p50 / p90 / p99 / avg / max | **8 / 122 / 249 / 38 / 5 207 ms** |
| Errors / non-2xx / timeouts | 0 / 0 / 0 |
| Server-reported pop batch efficiency | **4.62 msg/req** |

## Server-side window aggregates (`/api/v1/status`, 1-min buckets)

| Metric | Value |
|---|---|
| **Steady-state ingest** | **6 035 msg/s** |
| **Steady-state pop** | **6 035 msg/s** |
| Max ingest / pop (1-min) | 6 155 / 6 157 msg/s |
| Avg lag | 0.0 ms |
| Max lag | **57 ms** |
| Queen CPU user % avg | **286 %** (sys avg 503 %) → ~7.9 vCPU total |
| Queen RSS max | **32.5 MB** |
| DB pool active avg | 2.40 |
| Errors (`db`/`ack`/`dlq`) | 0 / 0 / 0 |
| `batchEfficiency` push / pop / ack | 1.00 / 4.62 / 4.62 |

## Worker heartbeats (libqueen, 9 032 lines / 10 workers)

| Metric | min | p50 | p99 | max | avg |
|---|---:|---:|---:|---:|---:|
| `push.p99rtt` (ms) | 2 | 3 | 22 | 26 | **4.7** |
| `pop.p99rtt` (ms) | 1 | 1 | 5 | 7 | **1.5** |
| `jobs/s` per worker | 1 | 863 | 930 | 977 | **851** |
| `evl` (event-loop lag, ms) | 0 | 0 | 0 | 2 | 0.0 |

## Background services

| Service | Events | Notes |
|---|---:|---|
| `StatsService: cycle completed` | 94 | min 6 ms, max **1 958 ms**, avg **121 ms** |
| `PartitionLookupReconcileService` | 57 | total **146 rows** fixed |
| `[error]` lines | **0** | — |

## Postgres state at end

| Metric | Value |
|---|---|
| **Cache hit ratio (DB)** | **100.00 %** (650 disk reads only) |
| `messages` size | **2 171 MB** (943 MB heap + 1 228 MB indexes) |
| `partition_lookup` HOT-update % | **99.93 %** (1.40 M HOT) |
| `partition_consumers` HOT-update % | 99.81 % (2.66 M HOT) |

## Queue final state

| Field | Value |
|---|---|
| Partitions reported | 102 |
| Total messages | 5 428 563 |
| Completed | 5 427 427 |
| **Pending at end** | **1 136** (0.02 %) — fully drained |

## Live container resources (post-test snapshot)

| Container | CPU % | Mem |
|---|---:|---|
| postgres | 1.10 % | 4.44 GiB |
| queen | 0.79 % | 28.3 MiB |

## Headline takeaways for `part-100`

- **Throughput essentially identical** to `part-1` and `part-10`: 6 032 msg/s push, 6 035 msg/s pop. Variation across the three is **6 366 → 6 266 → 6 032 msg/s**, a 5 % spread that's explained entirely by the small dip in pop batch efficiency (each pop drains fewer per-partition messages as fan-out widens).
- **Pop batch efficiency continues its monotonic decline**: 13.73 (2 part) → 6.57 (11 part) → **4.62 (101 part)**. The consumer's 100-msg batch ceiling is being filled less and less because each per-partition queue is shorter at the moment of pop.
- **Consumer p99 latency keeps improving**: 2 162 → 361 → **249 ms**. Wider fan-out → fewer empty long-polls → faster pop completion.
- **Queen and Postgres footprints are flat** across the whole low-load axis: ~7.9 vCPU queen, ~2.4 active DB connections, 100 % cache hit, ~30 MB RSS. The system is well below saturation.
- **Conclusion of the low-load axis**: at 50-connection load, partition count between 2 and 101 is irrelevant for push throughput. The producer is HTTP-RPS-bound by autocannon's 50-connection × 7 ms-latency loop, regardless of how many partitions exist server-side.

---

## Why we stopped here

Confirmed across `part-1`, `part-10`, `part-100`: with **1 producer × 50 connections × batch=1**, the bottleneck is HTTP request fan-out from the client (~6.3k req/s ceiling), not Queen and not Postgres. Running `part-1000` and `part-10000` at the same low load would only reproduce ~6 000 msg/s with marginally different pop-batch efficiency — no new information about partition scaling.

The next axis run will use **5 producer workers × 100 connections each = 500 total connections** to push Queen above its low-load comfort zone and expose how partition count interacts with concurrency, lock contention, and per-partition lease throughput.
