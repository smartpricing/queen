# High-load partition axis — `hi-part-100` (101 partitions, 1 msg/push, 500 producer conns)

**Run:** `2026-04-25T12:13:18Z → 12:29:34Z` (15 min)

## Setup

| | |
|---|---|
| `MAX_PARTITION` | **100** (101 distinct partitions) |
| `MSGS_PER_PUSH` | 1 |
| Producer | 5 × 100 = 500 conns |
| Consumer | 2 × 50 = 100 conns, batch=100 |

## Producer — autocannon

| Metric | Value |
|---|---|
| Total push requests | 23 434 854 |
| Throughput | **26 041 msg/s** |
| Latency p50 / p99 / avg / max | **17 / 48 / 18.7 / 256 ms** |
| Errors / non-2xx / timeouts | 0 / 0 / 0 |

## Consumer — autocannon

| Metric | Value |
|---|---|
| Total pop requests | 2 357 097 |
| req/s | **2 619** (4× higher than `hi-part-10`) |
| Effective msg/s (req × 8.78 batch) | **22 995 msg/s** |
| Latency p50 / p99 / avg / max | **21 / 250 / 37.7 / 5 438 ms** |
| Errors / timeouts | 0 / 0 |
| Server-reported pop batch eff. | **8.78 msg/req** ← steep drop from 25.36 |

## Server-side window aggregates

| Metric | Value |
|---|---|
| **Steady-state ingest** | **25 929 msg/s** |
| **Steady-state pop** | **21 690 msg/s** ← consumer no longer keeping up |
| Max ingest / pop (1-min) | 27 467 / 27 271 msg/s |
| Avg msg dwell-time | 0 ms |
| **Max msg dwell-time** | **576 500 ms ≈ 9 min 36 s** ⚠️ |
| Max event-loop lag | 3 ms |
| Queen CPU user % avg | **874 %** (sys 1 912 %) → ~28 vCPU |
| Queen RSS max | **65 MB** |
| DB pool active avg / max | 2.41 / 2.6 |
| Errors (db/ack/dlq) | 0 / 0 / 0 |
| `batchEfficiency` push / pop | 1.00 / **8.78** |

## Worker heartbeats

| Metric | min | p50 | p99 | max | avg |
|---|---:|---:|---:|---:|---:|
| `push.p99rtt` (ms) | 11 | 36 | 45 | 48 | 33.8 |
| `pop.p99rtt` (ms) | 8 | 41 | 109 | 139 | 43.0 |
| `push.q` queue depth | 0 | 0 | 46 | 50 | 2.1 |
| `pop.q` queue depth | 0 | 0 | 9 | 10 | 1.9 |
| `slots` in use | 18 | 24 | 25 | 25 | 23.9 |
| `jobs/s` per worker | 1 | 2 984 | 3 241 | 3 379 | 2 909 |

## Background

| | Events | Notes |
|---|---:|---|
| `StatsService: cycle completed` | 95 | max **11 880 ms**, avg 742 ms |
| `[error]` lines | **0** | — |

## Postgres state at end

- Cache hit ratio: **100.00 %**
- `messages` size: ~10 GB
- HOT-update ratio still 99 %+ on hot tables
- 0 active queries

## Queue final state

| | Value |
|---|---|
| Total | 23 435 354 |
| Completed | 20 697 800 |
| **Pending at end** | **2 737 554 (11.7 %)** ← producer outpacing consumer significantly |

## Live containers after run

| | CPU % | Mem |
|---|---:|---|
| postgres | 1.19 % | 13.96 GiB |
| queen | 0.89 % | 58.3 MiB |

## Headline takeaways for `hi-part-100`

This is where the **consumer becomes the bottleneck** in the high-load axis.

| Test | Push msg/s | Pop msg/s | Pop batch eff. | Pending end | Max lag |
|---|---:|---:|---:|---:|---:|
| `hi-part-1` (2 part) | 29 487 | 27 849 | 83.27 | 0.9 % | 112 ms |
| `hi-part-10` (11 part) | 27 902 | 26 165 | 25.36 | 3.1 % | 33 ms |
| **`hi-part-100` (101 part)** | **26 041** | **21 690** | **8.78** | **11.7 %** | **9.6 min** |

**The fan-out math is biting**:
- Producer puts 26 k msg/s evenly across **101 partitions** → ~258 msg/s per partition.
- Consumer fires `GET /pop?batch=100&wait=true` from 100 connections.
- Each pop lands on a server-chosen partition. If that partition has been "drained" recently by another connection (or just got fewer messages this tick), the response is small. **Average return is 8.78 msgs**, so 100-msg batches are ~9 % efficient.
- Consumer can only fire ~2 619 req/s (limited by Node + autocannon overhead at 100 conns × 38 ms avg latency), so effective drain = 2 619 × 8.78 = **23 k msg/s**.
- Producer at 26 k beats this by ~3 k msg/s → backlog grows.

**The 9.6-min max-lag** means some partitions got starved: while the consumer focused on hotter partitions, others sat for nearly 10 minutes. This is a **lease/affinity behavior** worth investigating in Queen — round-robin or weighted partition selection on the server-side `pop without partition` could prevent it.

**Queen and Postgres are still healthy**: ~28 vCPU queen, 100 % PG cache, 0 active queries, 0 errors. The bottleneck is purely client-side pop yield.

**Implication for tuning**: at high partition counts under high producer load, consumers need either:
- **More connections** (e.g. 5 × 100 to match producer), or
- **Smaller batches with `wait=false`** (poll-and-return-immediately), or
- **Per-partition consumers** that pin to a partition and drain serially.
