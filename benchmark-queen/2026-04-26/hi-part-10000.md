# High-load partition axis — `hi-part-10000` (10 001 partitions, 1 msg/push, 500 producer conns)

**Run:** `2026-04-25T12:46:47Z → 13:03:03Z` (15 min)

## Setup

| | |
|---|---|
| `MAX_PARTITION` | **10 000** (10 001 distinct partitions) |
| `MSGS_PER_PUSH` | 1 |
| Producer | 5 × 100 = 500 conns |
| Consumer | 2 × 50 = 100 conns, batch=100 |

## Producer — autocannon

| | |
|---|---|
| Total push requests | 18 937 729 |
| Throughput | **21 044 msg/s** |
| Latency p50 / p99 / avg / max | **22 / 55 / 23.4 / 9 982 ms** |
| Errors / non-2xx / timeouts | 14 / 0 / **14** ⚠️ |

> 14 producer timeouts (autocannon-side, 10 s default) — first time we see producer-side timeouts. Server logs report 0 errors.

## Consumer — autocannon

| | |
|---|---|
| Total pop requests | 788 484 |
| req/s | 876 |
| Effective msg/s (req × 20.42 batch) | **17 904 msg/s** |
| Latency p50 / p99 / avg / max | **66 / 504 / 112 / 8 569 ms** |
| Errors / timeouts | 100 / **100** ⚠️ |
| Server-reported pop batch eff. | **20.42 msg/req** |

## Server-side window aggregates

| | |
|---|---|
| **Steady-state ingest** | **20 973 msg/s** |
| **Steady-state pop** | **17 825 msg/s** |
| Max ingest / pop (1-min) | 24 628 / 23 145 msg/s |
| Avg / max msg dwell-time | 0 / **728 848 ms ≈ 12 min 9 s** ⚠️ |
| Max event-loop lag | 5 ms |
| Queen CPU user % avg | **711 %** (sys 1 497 %) → ~22 vCPU |
| Queen RSS max | **69.7 MB** |
| DB pool active avg / max | 2.42 / 2.6 |
| Errors (db/ack/dlq) | 0 / 0 / 0 |
| `batchEfficiency` push / pop | 1.00 / **20.42** |

## Background services

| | Events | Notes |
|---|---:|---|
| `StatsService: cycle completed` | 95 | max **17 921 ms** (18 s — closest yet to 30 s `statement_timeout`!), avg **1 020 ms** |
| `[error]` lines | **0** | no statement timeout errors |

## Postgres state at end

| | |
|---|---|
| Cache hit ratio (DB) | **100.00 %** |
| `messages` size | **7 744 MB** |
| `partition_lookup` size | **30 MB** (29 MB heap + 1 832 kB indexes) ← **200× larger** than at 101 partitions |
| `partition_consumers` size | 9.8 MB |

## Queue final state

| | Value |
|---|---|
| Total | 18 938 232 |
| Completed | 16 104 389 |
| **Pending at end** | **2 833 843 (15.0 %)** |

## Headline takeaways

- **Push throughput dropped to 21 044 msg/s** — that's a **29 % cumulative decline** from `hi-part-1` (29 487). The slowdown is monotonic across the axis, not a step-cost.
- **Push latency p99 jumped to 55 ms**, with **producer-side timeouts (14)** appearing for the first time. The 9 982 ms producer max-latency single-request is the worst we've seen.
- **`partition_lookup` table is now 30 MB** (was 152 kB at 101 partitions). Each row is ~3 kB on average — that's wider than it should be for ~10 001 rows, suggests bloat (last vacuum lagging) or wide payloads. Despite this, **HOT-update ratio stayed near 99 %**, so it's not contention-related.
- **`StatsService` max cycle hit 17.9 s** — closing in on the 30 s `statement_timeout` ceiling. Under heavier sustained load (or with longer test duration), this would tip over and produce the same `canceling statement due to statement timeout` errors we saw in the long-running test.
- **Consumer batch efficiency = 20.42** (recovered from `hi-part-100`'s 8.78). The intuition: with 10 001 partitions, even random partition selection is unlikely to hit a "just-drained" partition, so each pop tends to find a non-empty one and drain ~20 messages.
- **Max msg dwell-time = 12 min** — the worst on the axis. With 100 consumer connections trying to cover 10 001 partitions, partition starvation is severe.
- **Postgres still 100 % cache hit, 2.4 active connections** — the database is *still* not the bottleneck despite a 200× larger `partition_lookup` and 30× larger `messages` working set. PG buffer pool (24 GB) absorbs everything.
- **0 server errors** — no DLQ, no ackFailed, no dbErrors. All "errors" are autocannon client-side timeouts.

---

## Summary of high-load partition axis

| Test | Push msg/s | Pop msg/s | Pop batch eff. | Pending end | Max msg lag | Queen CPU | StatsService max |
|---|---:|---:|---:|---:|---:|---:|---:|
| `hi-part-1` (2) | **29 487** | 27 849 | 83.27 | 0.9 % | 112 ms | ~27 vCPU | 7.0 s |
| `hi-part-10` (11) | 27 902 | 26 165 | 25.36 | 3.1 % | 33 ms | ~26 vCPU | 12.7 s |
| `hi-part-100` (101) | 26 041 | 21 690 | **8.78** ⚠️ | **11.7 %** | **9.6 min** | ~28 vCPU | 11.9 s |
| `hi-part-1000` (1 001) | 25 016 | 22 679 | 15.55 | 2.8 % | 5.7 min | ~25 vCPU | — |
| **`hi-part-10000` (10 001)** | **21 044** | 17 825 | 20.42 | **15.0 %** | **12.1 min** | ~22 vCPU | **17.9 s** |

**Producer throughput is monotonic-decreasing in partition count.** Slope:
- 2 → 11: 5.4 % loss (small)
- 11 → 101: 6.7 % loss
- 101 → 1 001: 3.9 % loss
- 1 001 → 10 001: 15.9 % loss ← here the cost becomes real

**Consumer pop yield is U-shaped**: 83 (2 part) → 8.78 (101 part) → 20.42 (10 001 part). The 100-partition mid-zone is genuinely worst because consumer connections collide on recently-drained partitions; very-low (rich per-partition) and very-high (rare collision) cases both deliver more messages per pop.

**Postgres scales completely transparently** across the entire axis — cache hit stays 100 %, active connections never exceed 3, no errors. The `partition_lookup` table grew linearly (152 kB → 30 MB), but HOT-update efficiency held at 99 %+ so vacuum cost stayed manageable.

**The only worry is `StatsService` cycle time**, which trended 7.0 → 17.9 s. At ≥ 30 s it triggers PG `statement_timeout` and logs an `[error]`. At the partition counts tested it stayed below the threshold; in production with sustained ≥ 30 k push/s and many partitions this needs the fix I flagged in the long-running report (raise `statement_timeout` for the reconciliation session, or chunk the CTE).
