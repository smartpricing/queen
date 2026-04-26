# High-load partition axis — `hi-part-1000` (1 001 partitions, 1 msg/push, 500 producer conns)

**Run:** `2026-04-25T12:30:02Z → 12:46:18Z` (15 min)

## Setup

| | |
|---|---|
| `MAX_PARTITION` | **1000** (1 001 distinct partitions) |
| `MSGS_PER_PUSH` | 1 |
| Producer | 5 × 100 = 500 conns |
| Consumer | 2 × 50 = 100 conns, batch=100 |

## Producer — autocannon

| | |
|---|---|
| Total push requests | 22 511 629 |
| Throughput | **25 016 msg/s** |
| Latency p50 / p99 / avg / max | **18 / 50 / 19.5 / 1 036 ms** |
| Errors / non-2xx / timeouts | 0 / 0 / 0 |

## Consumer — autocannon

| | |
|---|---|
| Total pop requests | 1 407 297 |
| req/s | 1 564 |
| Effective msg/s (req × 15.55 batch) | **21 884 msg/s** |
| Latency p50 / p99 / avg / max | **35 / 321 / 63.5 / 7 339 ms** |
| Errors / timeouts | 0 / 0 |
| Server-reported pop batch eff. | **15.55 msg/req** |

## Server-side window aggregates

| | |
|---|---|
| **Steady-state ingest** | **24 939 msg/s** |
| **Steady-state pop** | **22 679 msg/s** |
| Max ingest / pop | 25 973 / 28 411 msg/s |
| Avg / max msg dwell-time | 0 / **341 202 ms ≈ 5 min 41 s** |
| Max event-loop lag | 4 ms |
| Queen CPU user % avg | **798 %** (sys 1 690 %) → ~25 vCPU |
| Queen RSS max | 65.1 MB |
| DB pool active avg / max | 2.36 / 2.4 |
| Errors (db/ack/dlq) | 0 / 0 / 0 |
| `batchEfficiency` push / pop | 1.00 / **15.55** |

## Postgres state at end

- Cache hit ratio: **100.00 %**
- `messages` size: **9 135 MB** (3 910 MB heap + 5 224 MB indexes)
- Postgres mem: 13.5 GiB

## Queue final state

| | Value |
|---|---|
| Total | 22 512 129 |
| Completed | 21 881 819 |
| **Pending at end** | **630 310 (2.8 %)** |

## Headline takeaways

- **Push throughput continues monotonic decline**: 29 487 → 27 902 → 26 041 → **25 016 msg/s** as partition count grows from 2 → 1 001. Cumulative slowdown ~15 % over the range.
- **Pop batch efficiency non-monotonic**: 83 → 25 → **8.78 (101 part) → 15.55 (1 001 part)**. The dip at 101 partitions is a real artifact — at that fan-out, the consumer's 100 connections collide with each other on partitions that just got drained. With 1 001 partitions there's enough "room" that connections rarely contend on the same partition, even with poor selection — they each find a non-empty one.
- **Max-lag improved**: 576 s → **341 s** vs `hi-part-100`. Wider fan-out reduces partition starvation.
- **Postgres still 100 % cache hit**, 2.4 active connections — Postgres has not become a factor yet at any partition count.
- **0 errors anywhere**.
