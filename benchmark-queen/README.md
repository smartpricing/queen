# Queen benchmarks

Results from benchmarking [Queen MQ](https://github.com/smartness-ai/queen) on a 32 vCPU / 62 GiB host with PostgreSQL.

## Sessions

| Date | Image(s) | Tests | Summary |
|---|---|---|---|
| [`2026-04-25`](./2026-04-25/) | `0.14.0.alpha.3` | 14 h long-running production-load test | [long-running.md](./2026-04-25/long-running.md) |
| [`2026-05-25`](./2026-05-25/) | `0.14.0.alpha.3` + `0.12.19` | 18 short benchmarks across 4 axes (partition, batch, queue, consumer group) + version comparison | [README.md](./2026-05-25/README.md) |

## Quick navigation

- **How to reproduce these benchmarks**: [`2026-05-25/HOW-TO-RUN.md`](./2026-05-25/HOW-TO-RUN.md)
- **Headline result**: Queen sustains ~100 k msg/s peak with batch=100 on a single node, ~28-39 k msg/s sustained at production-realistic batch=10. Postgres + Queen is the simplest "real" message queue I've benchmarked at this throughput class. See [`2026-05-25/README.md`](./2026-05-25/README.md) for the full breakdown.
