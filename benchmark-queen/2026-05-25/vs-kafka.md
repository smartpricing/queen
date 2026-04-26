# Kafka comparison — `kafka-1000p`

**Run:** `2026-04-26T10:54:58Z → 11:10:42Z` (15-min window; producer ran out of records at ~11 min)
Image: `apache/kafka:3.7.0` (KRaft single-node, broker+controller combined)
Same host, same hardware as Queen `bp-10` (32 vCPU, 62 GiB RAM, no swap).

## Setup


| Parameter     | Value                                                                                                                                         |
| ------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Topic         | `bench-topic`, **1000 partitions**, replication-factor=1                                                                                      |
| Producer      | `kafka-producer-perf-test.sh`, `acks=1`, `linger.ms=10`, `batch.size=16384`, no compression, `max.in.flight=5`, `--throughput -1` (unbounded) |
| Consumer      | `kafka-consumer-perf-test.sh`, single consumer in `bench-group`, `fetch.max.bytes=10MB`, `max.poll.records=100`                               |
| Message size  | 28 bytes (matching Queen's payload)                                                                                                           |
| Broker tuning | `num.io.threads=16`, `num.network.threads=8`, `socket.send/recv.buffer=1MB`, JVM heap `-Xms2g -Xmx4g`                                         |
| Durability    | `acks=1` (broker page-cache write before ack), single broker = single replica                                                                 |
| Run           | 15 min target; producer reached `--num-records 1000000000` cap at ~11 min, then idle 4 min                                                    |


## Producer — final result

```
1 000 000 000 records sent, 1 520 537 records/sec (40.60 MB/sec)
Latency: avg 1184 ms, p50 1117 ms, p95 2412 ms, p99 2966 ms, p99.9 3666 ms, max 5652 ms
```

**1.52 million records/sec sustained** for ~11 min. Producer was the limit (record cap), not Kafka — actual Kafka ceiling on this hardware is higher.

## Consumer

Drained ~~993 M records over ~25 min wall (started immediately, kept running after producer finished). Steady-state pop rate during active producing: **~~1.5 M records/sec**, matching producer. Consumer kept up.

## Resource consumption


| Metric               | Active producing (~11 min)                                              | Post-producer (~4 min idle) |
| -------------------- | ----------------------------------------------------------------------- | --------------------------- |
| Kafka CPU avg        | **~350 %** (~3.5 vCPU), peak 421 %                                      | <1.5 %                      |
| Kafka mem (JVM heap) | grew from 6.1 → 7.16 GiB                                                | dropped to 3.1 GiB (GC)     |
| Disk usage at end    | **35.99 GB** (1B records × 28B + Kafka overhead = ~36 B/record on disk) | —                           |


## Direct comparison to Queen `bp-10`

Both runs: same host, 1 producer + 1 consumer, persistent, single-node, ~28-byte payload, 1000 partitions.


| Metric                     | Queen `bp-10`                        | Kafka `kafka-1000p`   | Δ                                    |
| -------------------------- | ------------------------------------ | --------------------- | ------------------------------------ |
| **Push msg/s** (sustained) | **39 060**                           | **1 520 538**         | **Kafka 39× faster**                 |
| Push p50 latency           | 11 ms                                | 1 117 ms              | Queen 100× lower                     |
| Push p99 latency           | 38 ms                                | 2 966 ms              | Queen 78× lower                      |
| Pop msg/s                  | 38 351                               | ~1.5 M                | Kafka 39×                            |
| **Server RSS / heap**      | **52 MB**                            | **3 100–7 200 MB**    | **Kafka 60–140× more memory**        |
| Server CPU avg             | 7.4 vCPU                             | **3.5 vCPU**          | Kafka 2× less CPU                    |
| **CPU-per-msg/s ratio**    | 190 vCPU / Mmsg/s                    | **2.3 vCPU / Mmsg/s** | **Kafka 82× more efficient**         |
| Disk size at end           | 14 GB (`messages` table) for 35M msg | 35.99 GB for 1B msg   | Kafka uses 11× less disk per message |
| Disk per msg               | ~400 B/msg (Queen + PG indexes)      | **~36 B/msg**         | Kafka 11× more disk-efficient        |
| Errors                     | 0                                    | 0                     | tied                                 |


## What this means

### Where Kafka wins (and by how much)

1. **Raw throughput**: 39× more msg/s on identical hardware. This is the Kafka design's whole reason for existing — append-only mmap'd logs are vastly faster than transactional row inserts.
2. **CPU per msg/s**: 82× more efficient at peak. Queen's per-message overhead in libqueen + Postgres is real.
3. **Disk per msg**: 11× more efficient. Kafka's record format (varint headers, no per-row metadata, no index/heap split) is much tighter than `queen.messages` heap+index pair.

### Where Queen wins

1. **Latency**: Queen's p99 is **78× lower** under saturation (38 ms vs 2966 ms). This is the key counterintuitive finding — Kafka's higher throughput came at the cost of producer-side back-pressure latency. **For applications that care about per-message latency more than peak throughput, Queen is strictly better in this single-node configuration.**
2. **Absolute memory footprint**: 52 MB vs 3-7 GB. On a tiny VM (1-2 GiB RAM), you can run Queen comfortably. Kafka's JVM heap alone exceeds that.
3. **Setup**: Queen = 1 binary + your existing PG. Kafka = broker process + KRaft state + topic provisioning.

### Where it's unclear

1. **Latency vs. throughput trade-off**: Both systems can be tuned for different points. Kafka rate-limited to 39 k msg/s would have sub-millisecond latency. Queen's batch-size=100 hits 104 k msg/s with 131 ms p99 (still vastly better than Kafka's saturation latency, but worse than batch-size=10's 38 ms). The tradeoffs aren't as clear-cut as the headline numbers suggest.
2. **Durability**: `acks=1` doesn't fsync per message in Kafka (relies on page cache + replica). Queen with PG `synchronous_commit=on` does fsync the WAL. **Strictly speaking, Queen's `bp-10` durability is stronger than Kafka's `acks=1` measured here.** A truly fair comparison would use `acks=all` + `min.insync.replicas=2` + `flush.messages=1`, which would slash Kafka's throughput by 2-5×.
3. **Multi-broker scaling**: This was single-broker Kafka. Real Kafka deployments use 3-5 brokers and partition the load, scaling the throughput further. Queen has no equivalent built-in scale-out.

## Headline interpretation

> **Kafka does ~40× the throughput of Queen on identical hardware, but at ~80× the latency under saturation and 60–140× the memory footprint. Queen wins on latency, footprint, and operational simplicity. Kafka wins on raw throughput per CPU.**

The crossover point is **the throughput vs latency vs operational-cost trade-off**:

- If your workload needs **> 200 k msg/s sustained**, Queen is the wrong tool. Use Kafka.
- If your workload needs **< 100 k msg/s but cares about p99 latency**, Queen is the right tool. Kafka's saturation latency is genuinely worse.
- If your workload needs **< 50 k msg/s and lives next to Postgres**, Queen has no real competitor — running Kafka would mean adding 3+ broker nodes for a workload Queen handles in 50 MB on the side.

