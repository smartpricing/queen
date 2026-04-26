# v12 high-load partition — `v12-hi-part-1` (queen 0.12.19, 2 partitions, 500 conns prod / 100 conns cons)

**Run:** `2026-04-26T08:21:02Z → 08:36:53Z` (15 min)

## Producer — autocannon

| | v12 | (vs 0.14 `hi-part-1`) |
|---|---|---|
| Total push requests | 12 324 881 | (was 26 536 356) |
| Throughput | **13 696 msg/s** | (29 487 → **−53.6 %**) ⚠️ |
| Latency p50 / p90 / p99 / avg / max | **28 / 63 / 139 / 36.0 / 623 ms** | (15 / 19 / 44 / 16.5 / 203) — **~3× higher** |
| Errors / timeouts | 0 / 0 | (0 / 0) |

## Consumer — autocannon

| | v12 | (vs 0.14) |
|---|---|---|
| Total pop requests | 28 801 | (was 308 865) |
| req/s | **32** | (343 → **−91 %**) |
| Pop batch eff (server) | **99.93 msg/req** | (83.27 — actually slightly higher) |
| Effective msg/s | **3 196 msg/s** | (25 716 → **−88 %**) |
| Latency p50 / p99 / avg / max | **319 / 9 155 / 1 859 / 9 258 ms** | (59 / 4 199 / 287 / 9 322) |
| Errors / timeouts | **3 609 / 3 609** | (96 / 96) — **38× more timeouts** |

## Server-side window aggregates

| | v12 | (vs 0.14) |
|---|---|---|
| Steady-state ingest | **13 589 msg/s** | (28 751 → **−53 %**) |
| Steady-state pop | **3 194 msg/s** | (27 849 → **−89 %**) |
| Max msg dwell-time | **749 223 ms ≈ 12.5 min** | (112 ms — **incomparably worse on v12**) |
| Max event-loop lag | 14 ms | (7 ms — slightly worse) |
| Errors (db/ack/dlq) | 0 / 0 / 0 | (0 / 0 / 0) |

## Background services

| | v12 | (vs 0.14) |
|---|---|---|
| `StatsService: cycle completed` | 103 cycles | (96) |
| top 3 cycle ms | 4 348 / 4 711 / **4 817** | (top was 7 037 in 0.14) |
| `[error]` lines | **0** | 0 |

## Postgres state at end

| | v12 | 0.14 |
|---|---|---|
| Postgres mem | **16.78 GiB** | 13.57 GiB |
| Block I/O | 57.6 GB | 60.1 GB |
| Queen RSS | 38.95 MB | 59.1 MB |

## Queue final state

| | v12 | 0.14 |
|---|---|---|
| Total | 12 325 381 | 26 530 406 |
| Completed | 2 883 698 | 26 293 221 |
| **Pending end** | **9 441 683 (76.6 %)** ⚠️ | **237 185 (0.9 %)** |

## Headline takeaways

This is the **most dramatic version delta** in the entire comparison.

- **Push throughput dropped 54 %** under high partition contention. With 500 producer connections fighting for 2 partitions, 0.12.19 sustains only 13.7 k msg/s vs 0.14's 29.5 k.
- **Pop throughput dropped 89 %** (28 k → 3.2 k msg/s). The consumer **falls off a cliff** under this load on 0.12.19.
- **76 % of messages are stuck in pending at end** — the system is fundamentally backlogged.
- **3 609 consumer timeouts** (autocannon's 10 s ceiling) vs only 96 in 0.14. The pop path on 0.12 is hitting some kind of contention/serialization that makes long-poll responses routinely exceed 10 s.
- **Producer p99 latency is 3× higher**: 139 ms vs 44 ms.
- **Postgres still 0 dbErrors and 100 % cache hit** — the database engine handled it fine. The bottleneck is in **Queen's own pop path under high partition contention**, presumably the v1/v2 pop procedure or the libqueen drain/lease behavior was significantly improved between 0.12 and 0.14.

**Implication**: the 2-partition + 500-conns contention test was the single regime where the 0.13 → 0.14 work had the biggest impact. Whatever was changed in the pop procedure or partition-lease handling between these versions delivered roughly an **order-of-magnitude** improvement in pop throughput under contention.
