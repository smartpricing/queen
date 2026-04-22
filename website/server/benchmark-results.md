# Production Benchmark Results

Real-world performance testing of Queen MQ under sustained production-like workloads.

## Test Environment

- **Hardware**: 32 cores, 64GB RAM, 2TB NVMe disk
- **Deployment**: Docker containers (Queen + PostgreSQL)
- **Network**: Docker bridge network, no CPU pinning
- **Queen Version**: 0.11.0 (historical results) / `nagle` branch @ 86c1547 (April 2026 refresh)

## Summary

| Test | Throughput | Duration | Resources |
|------|------------|----------|-----------|
| **Sustained Load** | ~10,000 req/s | 3 days continuous | Queen: 133 MB RAM, ~3 cores |
| **Batch Push (0.11.0)** | ~31,000 msg/s | Sustained | Disk: ~1 GB/s write |
| **Batch Push (2026-04 refresh)** | **47,300 msg/s** (batch=100, 1 KB) | 60 s, zero errors | PG at 36-40 % of 24 cores |
| **High-payload ingestion** | **19,005 msg/s = 194 MB/s** (batch=10, 10 KB) | 60 s, zero errors | PG at ~69 % of 16 cores |
| **Consumer Groups** | 60,000 msg/s (10 groups) | Sustained | Queen: 1 GB RAM, ~3 cores |

---

## 0. 2026-04 Refresh — Server-Box Saturation Campaign

**Goal**: re-measure peak throughput on a dedicated 32-core box after the libqueen refactor (per-type queues, Triton-style BatchPolicy, Vegas adaptive concurrency, event-driven drain with submit-kick / slot-free-kick).

### Configuration

- **Host**: dedicated 32-core / 62 GB Ubuntu 24.04 bench box
- **PostgreSQL**: default tuning, `synchronous_commit=on` (note: stricter than the 0.11.0 run below which used `synchronous_commit=off`)
- **Queen**: `NUM_WORKERS=10`, `SIDECAR_POOL_SIZE=250`, `DB_POOL_SIZE=50`, libqueen defaults (`QUEEN_PUSH_MAX_CONCURRENT=24`, `QUEEN_VEGAS_MAX_LIMIT=32`, `QUEEN_VEGAS_BETA=12`)
- **Harness**: custom `test-perf/scripts/sweep-cores-payload.sh` sweep over PG cores × push_batch × payload, 60 s per run
- **Ground truth**: PG `n_tup_ins` diff over the measurement window

### Peak Throughput Results

All rows at `PG_CORES=24`, zero errors, zero non-2xx across all runs, buffer hit ratio 100 %:

| Queen workers | push_batch | payload | **msg/s** | bytes/s | msgs/commit | PG util |
|---------------|------------|---------|-----------|---------|-------------|---------|
| 2 | 10 | 1 KB | 24,103 | 24.1 MB/s | 163 | 13 % |
| 10 | 10 | 1 KB | 35,234 | 35.2 MB/s | 54 | 27 % |
| 16 | 10 | 1 KB | 35,932 | 35.9 MB/s | 32 | 29 % |
| **10** | **100** | **1 KB** | **47,300** | **47.3 MB/s** | **110** | **40 %** |
| 10 | 10 | 10 KB | 18,087 | **185 MB/s** | 40 | ~30 % |
| 16 | 10 | 10 KB | **19,005** | **194 MB/s** | 26 | 69 % |

### Scaling Behaviour (PG cores sweep, 10 queen workers)

| PG cores | batch=10 / 1 KB | batch=10 / 10 KB (MB/s) |
|----------|-----------------|--------------------------|
| 1 | 7,233 | 32.7 |
| 2 | 11,903 | 58.4 |
| 4 | 20,613 | 91.1 |
| 8 | 29,333 | 138.8 |
| 16 | 33,783 | 184.8 |
| 24 | **35,234** | **185.2** |

### Bottleneck Hierarchy (this hardware)

```
  Producer (autocannon 10w × 200c)  ← THE CEILING
      ~3.6 k req/s @ batch=10
      ~490 req/s   @ batch=100
                ↓  HTTP
  Queen server (10 workers)
      Observed: 44-73 % of max CPU — 2-5 cores always idle
                ↓  libpq
  PostgreSQL (24 cores, default tuning)
      Observed: 27-40 % of max — 14+ cores always idle
```

Raising Queen workers 10 → 16 yielded only a 2-6 % gain; PG utilization stayed below 70 %. **The HTTP producer is now the limiting factor**, not Queen or PostgreSQL.

### Campaign-over-Campaign Improvement (120 s, 3-scenario canonical harness)

Comparing the `nagle` branch against the `master` baseline (same hardware, same 3 scenarios, PG ground truth):

| Scenario | master `pg_ins/s` | nagle `pg_ins/s` | Δ | nagle push p99 | Δ p99 |
|----------|-------------------|-------------------|------|----------------|-------|
| S0 (2c PG, 1 queen worker) | 5,214 | **9,026** | **+73 %** | 473 ms | **−60 %** |
| S1 (2c PG, 2 queen workers) | 3,165 | **7,052** | **+123 %** | 618 ms | **−61 %** |
| S3 (4c PG, 2 queen workers, 2× load) | 2,686 | **7,221** | **+169 %** | 1,156 ms | **−83 %** |

See [`test-perf/results.md`](https://github.com/smartpricing/queen/blob/master/test-perf/results.md) for the full methodology, harness evolution, and per-campaign analysis.

---

## 1. Sustained Load Test

**Goal**: Validate Queen can handle continuous high-throughput workloads without degradation.

### Configuration

**Queen Server**:
```bash
docker run -d --ulimit nofile=65535:65535 \
  --name queen -p 6632:6632 --network queen \
  -e PG_HOST=postgres \
  -e PG_PASSWORD=postgres \
  -e NUM_WORKERS=10 \
  -e DB_POOL_SIZE=50 \
  -e SIDECAR_POOL_SIZE=250 \
  -e SIDECAR_MICRO_BATCH_WAIT_MS=20 \
  smartnessai/queen-mq:0.11.0
```

**PostgreSQL**:
```bash
docker run -d --ulimit nofile=65535:65535 \
  --name postgres --network queen \
  -e POSTGRES_PASSWORD=postgres -p 5432:5432 \
  postgres \
  -c shared_buffers=4GB \
  -c max_connections=300 \
  -c max_wal_size=16GB \
  -c synchronous_commit=off \
  -c autovacuum_vacuum_cost_limit=2000
```

### Workload

| Component | Configuration |
|-----------|--------------|
| Queue | 1 queue, 1000 partitions |
| Producers | 10 workers, 1000 total connections, 1 msg/request |
| Consumer | 1 worker, 50 connections, batch size 50, autoAck |

### Results

**2+ billion messages processed over 3 days at ~10,000 msg/s**

![Sustained Operations](/learn/operations-1.png)
*Stable throughput between 9,500-10,500 req/s over the entire test duration*

![First Billion Messages](/learn/1billion.png)
*First 1.322 billion messages processed*

### Resource Usage

After 2.5 days of continuous operation:

| Container | CPU | Memory | Network I/O | Disk I/O |
|-----------|-----|--------|-------------|----------|
| Queen | 325% | 146.5 MB | 2.55 TB / 6.64 TB | 14.3 MB / 0 B |
| PostgreSQL | 1001% | 46.82 GB | 456 GB / 982 GB | 30.1 GB / 43.3 TB |

**Key findings**:
- Queen memory usage remained constant at ~133 MB throughout the test
- Disk usage stabilized at 80 GB with aggressive retention (30-minute message lifetime)
- No performance degradation observed over the test duration

---

## 2. Batch Push Performance

**Goal**: Test maximum throughput with batched message production.

### Configuration

- **Batch size**: 1,000 messages per request
- **Message payload**: Standard JSON payload

### PostgreSQL Tuning

```ini
shared_buffers=1GB 
temp_buffers=64MB
work_mem=64MB
max_wal_size=4GB
min_wal_size=1GB
```

```sql
ALTER TABLE queen.messages SET (
    autovacuum_vacuum_scale_factor = 0.001, 
    autovacuum_vacuum_threshold = 100,
    autovacuum_vacuum_cost_delay = 0,
    autovacuum_vacuum_cost_limit = 10000
);
```

### Results

**31,170 msg/s sustained throughput**

![Batch Throughput](/learn/30k.png)
*Consistent 30K+ msg/s throughput*

![Disk Write Speed](/learn/10gb.png)
*Disk write speed approaching 1 GB/s (10 Gbps)*

| Metric | Value |
|--------|-------|
| Throughput | 31,170 msg/s |
| Disk Write | ~1 GB/s |
| Messages/minute | 2M+ |

---

## 3. Consumer Groups (Pub/Sub)

**Goal**: Test scalability of consumer group feature with multiple parallel consumers.

### Configuration

| Component | Configuration |
|-----------|--------------|
| Queue | 1 queue, 1000 partitions |
| Producer | 1 producer, 100 connections, batch size 10 |
| Consumers | 10 consumer groups, 5 workers each, 50 connections per worker |

### Results

**60,000 msg/s consumption rate** (6K push × 10 consumer groups)

![Consumer Group Peak](/learn/cg1.png)
*Peak performance: 250K msg/s pop throughput during catch-up phase*

![Consumer Group Steady State](/learn/cg2.png)
*Steady state: 6K push / 60K pop msg/s with ~3 cores and 1 GB RAM*

| Phase | Push Rate | Pop Rate | CPU Usage | Memory |
|-------|-----------|----------|-----------|--------|
| Catch-up | N/A | 250,000 msg/s | 10 cores | 1 GB |
| Steady state | 6,000 msg/s | 60,000 msg/s | 3 cores | 1 GB |

---

## Performance Optimization Notes

### PostgreSQL Tuning

For sustained high-throughput workloads:

1. **WAL Configuration**: Increase `max_wal_size` to reduce checkpoint frequency
2. **Autovacuum**: Aggressive settings prevent dead tuple accumulation
3. **Synchronous Commit**: `off` for maximum write performance
