# Batch-push axis — `bp-1` (1 001 partitions, **batch=1**, low load 1×50)

**Run:** `2026-04-25T13:03:32Z → 13:19:48Z` (15 min)

## Setup

| | |
|---|---|
| `MAX_PARTITION` | 1000 (1 001 partitions) |
| `MSGS_PER_PUSH` | **1** |
| Producer | 1 worker × 50 conns = 50 conns |
| Consumer | 1 worker × 50 conns = 50 conns, batch=100 |

## Producer — autocannon

| | |
|---|---|
| Total push requests | 5 216 228 |
| Throughput | **5 796 req/s = 5 796 msg/s** |
| Latency p50 / p99 / avg / max | **8 / 13 / 8.2 / 524 ms** |
| Errors / timeouts | 0 / 0 |

## Consumer — autocannon

| | |
|---|---|
| Total pop requests | 799 997 |
| req/s | 889 |
| Effective msg/s (req × 5.36 batch) | **4 768 msg/s** |
| Latency p50 / p99 / avg / max | **11 / 355 / 55.7 / 7 274 ms** |
| Errors / timeouts | 0 / 0 |
| Server-reported pop batch eff. | **5.36 msg/req** |

## Server-side window aggregates

| | |
|---|---|
| Steady-state ingest | **5 616 msg/s** |
| Steady-state pop | **4 586 msg/s** |
| Max ingest | 5 902 msg/s |
| Max msg dwell-time | **734 140 ms ≈ 12 min** |
| Queen CPU user % avg | 252 % → ~5.2 vCPU |
| Queen RSS max | 47.2 MB |
| DB pool active avg | 2.39 |
| Errors | 0 |
| `batchEfficiency` push / pop | 1.00 / 5.36 |

## Queue final state

| | |
|---|---|
| Total | 5 216 278 |
| Completed | 4 288 367 |
| **Pending end** | **927 911 (17.8 %)** |

## Postgres at end

- Cache hit: **100 %**
- `messages` size: 2 073 MB

## Headline takeaways

- **Push throughput 5 796 msg/s** — slightly lower than `part-100` (6 032) and `part-10` (6 266), continuing the gentle downward trend in partition count even at low load.
- **Pop is starved**: with 1 001 partitions and only 50 consumer connections (1 worker), pop batch efficiency dropped to **5.36** — much lower than `part-100`'s 4.62 (similar) and `part-10`'s 6.57. Consumer falls behind by ~17.8 %.
- **No DB stress**: 100 % cache hit, 2.4 active conns, ~5 vCPU queen.
- **Baseline for batch axis**: this run's **5 796 msg/s** is the "batch=1" data point. `bp-10` and `bp-100` will show how batching multiplies throughput.
