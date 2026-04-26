# High-load partition axis — `hi-part-10` (11 partitions, 1 msg/push, 500 producer conns)

**Run:** `2026-04-25T11:56:33Z → 12:12:49Z` (15 min, plus ~1 min collection)
Queen: `smartnessai/queen-mq:0.14.0.alpha.3` · Postgres upstream

## Setup

| | |
|---|---|
| `MAX_PARTITION` | **10** (11 distinct partitions) |
| `MSGS_PER_PUSH` | 1 |
| Producer | 5 workers × 100 conns = 500 conns |
| Consumer | 2 workers × 50 conns = 100 conns, batch=100, wait=true, autoAck=true |
| Queen / Postgres / cleanup | identical to `hi-part-1` |

## Producer — autocannon

| Metric | Value |
|---|---|
| Total push requests | **25 109 897** |
| Throughput | **27 902 msg/s** |
| Latency p50 / p90 / p99 / avg / max | **16 / 21 / 45 / 17.4 / 564 ms** |
| Errors / non-2xx / timeouts | 0 / 0 / 0 |

## Consumer — autocannon

| Metric | Value |
|---|---|
| Total pop requests | **959 307** (~1 066 req/s) |
| Effective msg/s (req × 25.36 batch) | **26 547 msg/s** |
| Latency p50 / p90 / p99 / avg / max | **20 / 234 / 1 182 / 93 / 8 372 ms** |
| Errors / timeouts (autocannon-side) | 1 / 1 |
| Server-reported pop batch eff. | **25.36 msg/req** |

## Server-side window aggregates

| Metric | Value |
|---|---|
| **Steady-state ingest** | **27 023 msg/s** |
| **Steady-state pop** | **26 165 msg/s** |
| Max ingest / pop (1-min) | 29 432 / 28 746 msg/s |
| Avg / max msg dwell-time | 0 / **33 ms** |
| Avg / max event-loop lag | 0 / **28 ms** |
| **Queen CPU user %** | avg **832 %** (sys avg 1 797 %) → ~26 vCPU |
| Queen RSS max | **53.1 MB** |
| DB pool active avg / max | 2.52 / 2.7 |
| Errors (db/ack/dlq) | 0 / 0 / 0 |
| `batchEfficiency` push / pop | 1.00 / **25.36** |

## Worker heartbeats

| Metric | min | p50 | p99 | max | avg |
|---|---:|---:|---:|---:|---:|
| `push.p99rtt` (ms) | 10 | 33 | 42 | 45 | **32.1** |
| `pop.p99rtt` (ms) | 6 | 27 | 44 | 66 | **26.3** |
| `push.q` queue depth | 0 | 0 | 45 | 50 | 1.9 |
| `pop.q` queue depth | 0 | 0 | 7 | 10 | 1.0 |
| `slots` in use (max 25) | 21 | 24 | 25 | 25 | **24.1** |
| `jobs/s` per worker | 1 | 3 013 | 3 284 | 3 405 | **2 945** |
| `evl` (ms) | 0 | 0 | 1 | **28** | 0.0 |

## Background services

| | Events | Notes |
|---|---:|---|
| `StatsService: cycle completed` | 95 | min 0 ms, max **12 727 ms**, avg **724 ms** |
| `[error]` lines | **0** | — |

## Postgres state at end

| | |
|---|---|
| Cache hit ratio (DB) | **100.00 %** |
| `messages` size | **10 080 MB** (4 360 MB heap + 5 719 MB indexes) |
| `partition_lookup` HOT-update % | 99.31 % (3.31 M HOT) |
| `partition_consumers` HOT-update % | 99.27 % (1.95 M HOT) |

## Queue final state

| | Value |
|---|---|
| Total messages | 25 110 397 |
| Completed | 24 326 035 |
| **Pending at end** | **784 362** (3.1 %) |

## Live container resources after run

| Container | CPU % | Mem |
|---|---:|---|
| postgres | 0.69 % | 13.93 GiB |
| queen | 0.83 % | 46.2 MiB |

## Headline takeaways for `hi-part-10`

- **Push throughput 27 902 msg/s** vs `hi-part-1` 29 487 msg/s — ~5 % slower, within run-to-run variance.
- **Pop batch efficiency dropped 83 → 25** — same pattern as low-load axis, fan-out across more partitions thins each per-partition queue at pop time.
- **Consumer p99 1 182 ms** is much better than `hi-part-1`'s 4 199 ms — wider fan-out means fewer empty long-poll stalls.
- **StatsService max cycle hit 12.7 s** (was 7.0 s on `hi-part-1`) — trending up, but still under the 30 s timeout.
- **No errors anywhere** (db/ack/dlq/queen logs).
- **Postgres still bored**: 100 % cache hit, 2.5 active conns out of 50, no active queries.
