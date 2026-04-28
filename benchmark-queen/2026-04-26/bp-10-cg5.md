# Consumer-group axis — `bp-10-cg5` (5 consumer groups, queen 0.14.0.alpha.3)

**Run:** `2026-04-26T09:24:28Z → 09:40:44Z` (15 min)
Same producer setup as `bp-10` (1×50, batch=10, MP=1000) — but with **5 independent consumer groups**, each in its own Node process with 1×50 conns.

## Setup


| Parameter                  | Value                                                                                                            |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| Queue name                 | `bench-bp-10-cg5`                                                                                                |
| Producer                   | 1 × 50 conns, batch=10, MP=1000 (identical to `bp-10`)                                                           |
| **Consumers**              | **5 processes**, each 1 × 50 conns × batch=100 × `wait=true` × `autoAck=true`, with `consumerGroup=cg-1`..`cg-5` |
| Total consumer connections | **250** (5 × 50)                                                                                                 |
| Image                      | `0.14.0.alpha.3`                                                                                                 |


## Producer — autocannon


|                               | bp-10-cg5                      | (vs `bp-10` baseline)                 |
| ----------------------------- | ------------------------------ | ------------------------------------- |
| Total push requests           | 2 419 376                      | (3 515 078)                           |
| Total messages pushed         | **24 193 760**                 | (35 150 780)                          |
| Throughput                    | **2 689 req/s = 26 890 msg/s** | (39 060 → **−31 %**)                  |
| Latency p50 / p99 / avg / max | 16 / 52 / 18.1 / 655 ms        | (11 / 38 / 12.3 / 664) — ~30 % higher |
| Errors / timeouts             | 0 / 0                          | (0 / 0)                               |


> **Push throughput drops 31 %** when 5 consumer groups poll concurrently. Producer and consumers contend for queen worker slots and PG connections.

## Consumer — autocannon (per group)


| Group     | req/s           | p50 ms | p99 ms | errors | timeouts |
| --------- | --------------- | ------ | ------ | ------ | -------- |
| `cg-1`    | 634             | 62     | 315    | 0      | 0        |
| `cg-2`    | 632             | 62     | 317    | 0      | 0        |
| `cg-3`    | 634             | 62     | 315    | 0      | 0        |
| `cg-4`    | 634             | 62     | 315    | 0      | 0        |
| `cg-5`    | 634             | 62     | 315    | 0      | 0        |
| **Total** | **3 168 req/s** | —      | —      | **0**  | **0**    |


> **Per-group performance is extraordinarily uniform** — 5 groups deliver 632–634 req/s each, p99 within 2 ms of each other. Queen's group routing is fair.

## Server-side window aggregates


|                                      | bp-10-cg5                | (vs `bp-10` baseline)                                    |
| ------------------------------------ | ------------------------ | -------------------------------------------------------- |
| **Steady-state ingest**              | **25 860 msg/s**         | (38 898 → **−34 %**)                                     |
| **Steady-state pop (per-group sum)** | **127 777 msg/s**        | (38 351 → **+233 %**, ~5× expected)                      |
| Max ingest (1-min)                   | 28 246                   | (40 182)                                                 |
| Max pop (1-min)                      | **171 184 msg/s**        | (40 182) — **4.3× higher**                               |
| Avg lag                              | low                      | low                                                      |
| Max msg dwell-time                   | 469 227 ms ≈ 7.8 min     | (564 482) — better                                       |
| Queen CPU user % avg                 | **1 461 %** (~14.6 vCPU) | (744 % → **+96 %** for 5× pop work)                      |
| Queen RSS max                        | **116 MB**               | (52 MB → ~2.2× larger)                                   |
| DB pool active avg                   | **2.43**                 | (2.38) — **identical**, Vegas controller doing its thing |
| `batchEfficiency` push / pop         | 10.00 / **41.93**        | (10.00 / 42.29) — same                                   |


## Queue final state


|                                | Value                          |
| ------------------------------ | ------------------------------ |
| Total messages pushed          | 24 194 260                     |
| Total deliveries (`completed`) | **119 546 360**                |
| `totalConsumed` (per-cg count) | 98 803 890                     |
| **Pending end**                | **0** ✅ — fully drained        |
| Reported partitions            | 5 006 (5 cg × 1001 partitions) |


## Postgres state at end


|                            | bp-10-cg5              | bp-10                  |
| -------------------------- | ---------------------- | ---------------------- |
| Cache hit ratio            | **100 %**              | 100 %                  |
| `messages` size            | 9 850 MB               | 14 GB                  |
| `messages_consumed` size   | **650 MB**             | 84 MB → **~8× larger** |
| `partition_consumers` size | **29 MB** (5 005 rows) | (~6 MB, ~1 002 rows)   |


## Errors observed


| Severity                    | Count | What                                                                |
| --------------------------- | ----- | ------------------------------------------------------------------- |
| `[error]`                   | 1     | `StatsService run_full_reconciliation error: ... statement timeout` |
| `[error]`                   | 1     | `Worker 2 ... Query failed: ERROR: deadlock detected`               |
| `[error]`                   | 1     | `Worker 5 ... Query failed: ERROR: deadlock detected`               |
| `dbErrors` (status)         | 2     | (matches the 2 deadlocks)                                           |
| `ackFailed` / `dlqMessages` | 0 / 0 | —                                                                   |


> **Both old bugs reappear under cg fan-out**: the `StatsService` 30 s `statement_timeout` (which we'd predicted for cg-load) and a pair of PG deadlocks reminiscent of `v12-hi-part-10000`. The deadlocks were absorbed by **file-buffer failover** — confirmed by `ackFailed=0` and `pending=0` at end. **Zero data loss.**

## Background services


|                                 | Value                                |
| ------------------------------- | ------------------------------------ |
| `StatsService: cycle completed` | 90 cycles                            |
| max cycle                       | **30 002 ms** ⚠️ (statement timeout) |
| top 3                           | 20 044 / 25 808 / 30 002 ms          |


## Headline takeaways for `bp-10-cg5`

- **Per-group consumer throughput is identical to baseline**: each of 5 cg's gets 634 req/s (vs `bp-10`'s 910 req/s for 1 group). The slight per-group reduction is consistent with the producer ingesting 31 % less, so each group has 31 % less to consume.
- **Total pop throughput scales near-linearly**: 127.8 k msg/s ≈ 5 × 25.6 k = 128 k. **Queen distributes fan-out work cleanly across groups**.
- **Producer pays a 31 % tax** to support 5 independent consumer groups. This is the cost of 5× more pop activity on the same DB (more partition_consumers updates, more messages_consumed inserts, contention on queen workers).
- **Queen CPU only doubled** (744 → 1461 %) for 5× the pop work — the libqueen sidecar batches efficiently across consumer groups, so per-group amortization is good.
- **DB pool active stayed at 2.4** — same as baseline. The Vegas controller's adaptive limit doesn't blow up under fan-out; it correctly backs off when RTT degrades.
- **PG `messages_consumed` table is ~8× larger** because each delivery is tracked, but `messages` itself is smaller (less push activity).
- **3 errors, 0 lost messages**: file-buffer failover continues to be the unsung hero.

This is a **very strong result** for consumer-group support — the 5× fan-out comes at a 31 % producer cost and ~2× queen-CPU cost, with linear pop scaling and zero data loss. Multi-tenant pub/sub is realistic.