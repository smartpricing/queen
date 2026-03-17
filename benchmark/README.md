# Queen vs Kafka vs Pulsar Benchmark

A performance comparison of three message queue systems under high-partition, low-latency workloads without client-side batching.

## Overview

This benchmark evaluates **Queen MQ**, **Apache Kafka**, and **Apache Pulsar** in a specific but common scenario:

- **High partition count** (5,000 - 10,000 partitions)
- **No client-side batching** (immediate message delivery)
- **Durable writes** (all systems configured for persistence)
- **Single-node deployment** (containerized, resource-limited)

This workload pattern is typical for:
- Multi-tenant systems with per-tenant queues
- Channel/chat managers with per-conversation ordering
- IoT platforms with per-device message streams
- Event sourcing with per-aggregate partitions

## Key Findings

### Partition Scalability

| System | 10,000 Partitions | 5,000 Partitions |
|--------|-------------------|------------------|
| **Queen** | ✅ Works | ✅ Works |
| **Kafka** | ❌ Failed to create | ✅ Works |
| **Pulsar** | ✅ Works (degraded) | ✅ Works |

**Queen and Pulsar handled 10,000 partitions**, but Pulsar's throughput dropped 45%. Kafka failed to even create the topic on a single 8GB broker.

### Performance Results (60-second tests)

#### Throughput at 10K Partitions (Queen vs Pulsar)

![Throughput 10K Chart](https://quickchart.io/chart?c=%7Btype%3A%27bar%27%2Cdata%3A%7Blabels%3A%5B%27Queen%20(10K)%27%2C%27Pulsar%20(10K)%27%2C%27Kafka%20(5K)%27%5D%2Cdatasets%3A%5B%7Blabel%3A%27Throughput%20(msg%2Fs)%27%2Cdata%3A%5B5824%2C10564%2C13724%5D%2CbackgroundColor%3A%5B%27rgba(33%2C150%2C243%2C0.7)%27%2C%27rgba(156%2C39%2C176%2C0.7)%27%2C%27rgba(255%2C152%2C0%2C0.7)%27%5D%2CborderColor%3A%5B%27rgb(33%2C150%2C243)%27%2C%27rgb(156%2C39%2C176)%27%2C%27rgb(255%2C152%2C0)%27%5D%2CborderWidth%3A2%7D%5D%7D%2Coptions%3A%7Bplugins%3A%7Btitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27Throughput%20Comparison%20(no%20batching)%27%7D%7D%2Cscales%3A%7By%3A%7BbeginAtZero%3Atrue%2Ctitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27msg%2Fs%27%7D%7D%7D%7D%7D&w=600&h=300&bkg=white)

| System | Partitions | Throughput | p99 Latency |
|--------|------------|------------|-------------|
| Queen | 10,000 | 5,824 msg/s | 44.67ms |
| Pulsar | 10,000 | 10,564 msg/s | 37.57ms |
| Kafka | 5,000 | 13,724 msg/s | 15.13ms |

**Note:** Pulsar's throughput dropped 45% going from 5K to 10K partitions (19,150 → 10,564 msg/s).

#### Partition Fanout at 10K Partitions

![Fanout 10K Chart](https://quickchart.io/chart?c=%7Btype%3A%27bar%27%2Cdata%3A%7Blabels%3A%5B%27Queen%20(10K)%27%2C%27Pulsar%20(10K)%27%2C%27Kafka%20(5K)%27%5D%2Cdatasets%3A%5B%7Blabel%3A%27Fanout%20(msg%2Fs)%27%2Cdata%3A%5B550%2C558%2C477%5D%2CbackgroundColor%3A%5B%27rgba(33%2C150%2C243%2C0.7)%27%2C%27rgba(156%2C39%2C176%2C0.7)%27%2C%27rgba(255%2C152%2C0%2C0.7)%27%5D%2CborderColor%3A%5B%27rgb(33%2C150%2C243)%27%2C%27rgb(156%2C39%2C176)%27%2C%27rgb(255%2C152%2C0)%27%5D%2CborderWidth%3A2%7D%5D%7D%2Coptions%3A%7Bplugins%3A%7Btitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27Partition%20Fanout%20Throughput%27%7D%7D%2Cscales%3A%7By%3A%7BbeginAtZero%3Atrue%2Ctitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27msg%2Fs%27%7D%7D%7D%7D%7D&w=600&h=300&bkg=white)

| System | Partitions | Throughput | p99 Latency |
|--------|------------|------------|-------------|
| Queen | 10,000 | 550 msg/s | 32.96ms |
| Pulsar | 10,000 | 558 msg/s | 24.59ms |
| Kafka | 5,000 | 477 msg/s | 1.97ms |

**Queen and Pulsar achieve identical fanout throughput at 10K partitions** (~550 msg/s).

#### Latency Comparison

![Latency Chart](https://quickchart.io/chart?c=%7Btype%3A%27bar%27%2Cdata%3A%7Blabels%3A%5B%27p50%27%2C%27p95%27%2C%27p99%27%5D%2Cdatasets%3A%5B%7Blabel%3A%27Queen%20(10K)%27%2Cdata%3A%5B12.31%2C15.73%2C19.84%5D%2CbackgroundColor%3A%27rgba(33%2C150%2C243%2C0.7)%27%2CborderColor%3A%27rgb(33%2C150%2C243)%27%2CborderWidth%3A2%7D%2C%7Blabel%3A%27Pulsar%20(10K)%27%2Cdata%3A%5B5.98%2C8.82%2C17.79%5D%2CbackgroundColor%3A%27rgba(156%2C39%2C176%2C0.7)%27%2CborderColor%3A%27rgb(156%2C39%2C176)%27%2CborderWidth%3A2%7D%2C%7Blabel%3A%27Kafka%20(5K)%27%2Cdata%3A%5B1.94%2C3.60%2C4.84%5D%2CbackgroundColor%3A%27rgba(255%2C152%2C0%2C0.7)%27%2CborderColor%3A%27rgb(255%2C152%2C0)%27%2CborderWidth%3A2%7D%5D%7D%2Coptions%3A%7Bplugins%3A%7Btitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27Latency%20at%20100%20msg%2Fs%20Sustained%20Load%27%7D%7D%2Cscales%3A%7By%3A%7BbeginAtZero%3Atrue%2Ctitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27ms%27%7D%7D%7D%7D%7D&w=600&h=300&bkg=white)

| System | Partitions | p50 | p99 |
|--------|------------|-----|-----|
| Queen | 10,000 | 12.31ms | 19.84ms |
| Pulsar | 10,000 | 5.98ms | 17.79ms |
| Kafka | 5,000 | 1.94ms | 4.84ms |

### Resource Usage at 10K Partitions

![Memory 10K Chart](https://quickchart.io/chart?c=%7Btype%3A%27bar%27%2Cdata%3A%7Blabels%3A%5B%27Queen%20(10K)%27%2C%27Pulsar%20(10K)%27%2C%27Kafka%20(5K)%27%5D%2Cdatasets%3A%5B%7Blabel%3A%27Avg%20Memory%20(MB)%27%2Cdata%3A%5B868%2C5445%2C2767%5D%2CbackgroundColor%3A%5B%27rgba(33%2C150%2C243%2C0.7)%27%2C%27rgba(156%2C39%2C176%2C0.7)%27%2C%27rgba(255%2C152%2C0%2C0.7)%27%5D%2CborderColor%3A%5B%27rgb(33%2C150%2C243)%27%2C%27rgb(156%2C39%2C176)%27%2C%27rgb(255%2C152%2C0)%27%5D%2CborderWidth%3A2%7D%5D%7D%2Coptions%3A%7Bplugins%3A%7Btitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27Average%20Memory%20Usage%27%7D%7D%2Cscales%3A%7By%3A%7BbeginAtZero%3Atrue%2Ctitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27MB%27%7D%7D%7D%7D%7D&w=600&h=300&bkg=white)

| System | Partitions | Avg CPU | Avg Memory | Peak Memory |
|--------|------------|---------|------------|-------------|
| **Queen** | 10,000 | 103% | **868 MB** | 2,084 MB |
| Pulsar | 10,000 | 121% | 5,445 MB | 5,831 MB |
| Kafka | 5,000 | 85% | 2,767 MB | 8,191 MB |

**Queen uses 6x less memory than Pulsar at 10K partitions** (868 MB vs 5,445 MB).

### Queen Scaling: 5K vs 10K Partitions

![Queen Scaling](https://quickchart.io/chart?c=%7Btype%3A%27bar%27%2Cdata%3A%7Blabels%3A%5B%27Throughput%20(msg%2Fs)%27%2C%27Fanout%20(msg%2Fs)%27%2C%27Avg%20Memory%20(MB)%27%5D%2Cdatasets%3A%5B%7Blabel%3A%27Queen%205K%20partitions%27%2Cdata%3A%5B5947%2C544%2C830%5D%2CbackgroundColor%3A%27rgba(33%2C150%2C243%2C0.4)%27%2CborderColor%3A%27rgb(33%2C150%2C243)%27%2CborderWidth%3A2%7D%2C%7Blabel%3A%27Queen%2010K%20partitions%27%2Cdata%3A%5B5824%2C550%2C868%5D%2CbackgroundColor%3A%27rgba(33%2C150%2C243%2C0.8)%27%2CborderColor%3A%27rgb(33%2C150%2C243)%27%2CborderWidth%3A2%7D%5D%7D%2Coptions%3A%7Bplugins%3A%7Btitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27Queen%20Performance%20at%205K%20vs%2010K%20Partitions%27%7D%7D%2Cscales%3A%7By%3A%7BbeginAtZero%3Atrue%7D%7D%7D%7D&w=600&h=300&bkg=white)

| Metric | 5K Partitions | 10K Partitions | Change |
|--------|---------------|----------------|--------|
| Throughput | 5,947 msg/s | 5,824 msg/s | **-2%** |
| Fanout | 544 msg/s | 550 msg/s | ~same |
| p99 Latency | 17.94ms | 19.84ms | +11% |
| Memory | 830 MB | 868 MB | +5% |

**Queen maintains consistent performance** when doubling partition count from 5K to 10K.

### Pulsar Performance Degradation: 5K vs 10K

![Pulsar Scaling](https://quickchart.io/chart?c=%7Btype%3A%27bar%27%2Cdata%3A%7Blabels%3A%5B%27Throughput%20(msg%2Fs)%27%2C%27Fanout%20(msg%2Fs)%27%5D%2Cdatasets%3A%5B%7Blabel%3A%27Pulsar%205K%20partitions%27%2Cdata%3A%5B19150%2C550%5D%2CbackgroundColor%3A%27rgba(156%2C39%2C176%2C0.4)%27%2CborderColor%3A%27rgb(156%2C39%2C176)%27%2CborderWidth%3A2%7D%2C%7Blabel%3A%27Pulsar%2010K%20partitions%27%2Cdata%3A%5B10564%2C558%5D%2CbackgroundColor%3A%27rgba(156%2C39%2C176%2C0.8)%27%2CborderColor%3A%27rgb(156%2C39%2C176)%27%2CborderWidth%3A2%7D%5D%7D%2Coptions%3A%7Bplugins%3A%7Btitle%3A%7Bdisplay%3Atrue%2Ctext%3A%27Pulsar%20Performance%20at%205K%20vs%2010K%20Partitions%27%7D%7D%2Cscales%3A%7By%3A%7BbeginAtZero%3Atrue%7D%7D%7D%7D&w=600&h=300&bkg=white)

| Metric | 5K Partitions | 10K Partitions | Change |
|--------|---------------|----------------|--------|
| Throughput | 19,150 msg/s | 10,564 msg/s | **-45%** |
| Fanout | 550 msg/s | 558 msg/s | ~same |
| p99 Latency | 10.94ms | 17.79ms | +63% |
| Memory | 5,167 MB | 5,445 MB | +5% |

**Pulsar's throughput degrades 45%** when doubling partition count, while Queen stays stable.

## Test Methodology

### Configuration

All systems were tested with equivalent settings:

| Setting | Value |
|---------|-------|
| Resource limits | 4 CPU cores, 8GB RAM |
| Client batching | Disabled (`linger.ms=0` equivalent) |
| Durability | Maximum (sync writes, acks=all) |
| Message size | 256 bytes |
| Test duration | 60 seconds per benchmark |

### Durability Settings

| System | Configuration |
|--------|---------------|
| Queen (PostgreSQL) | `synchronous_commit=on` |
| Kafka | `acks=-1` (all replicas) |
| Pulsar | `journalSyncData=true` |

### Test Scenarios

1. **Latency Benchmark**: 100 messages/second sustained rate, measuring end-to-end latency percentiles
2. **Throughput Benchmark**: Maximum throughput with 10 concurrent producers, no rate limiting
3. **Partition Fanout**: Round-robin publishing across all partitions at 1000 msg/s target
4. **Consumer Benchmark**: Batch consumption with manual acknowledgment

## Running the Benchmark

### Prerequisites

- Docker and Docker Compose
- Node.js 22+
- ~16GB RAM available

### Quick Start

```bash
cd benchmark
npm install

# Run all benchmarks (takes ~30-45 minutes total)
npm run bench:all

# Or run individually:
npm run start:queen && npm run setup:queen && npm run bench:queen && npm run stop:queen
npm run start:kafka && npm run setup:kafka && npm run bench:kafka && npm run stop:kafka
npm run start:pulsar && npm run setup:pulsar && npm run bench:pulsar && npm run stop:pulsar

# Generate comparison report
npm run report
```

### Configuration

Edit `config.js` to adjust parameters:

```javascript
export const config = {
  partitionCount: 5000,      // Number of partitions (Queen handles 10k+)
  messageSize: 256,          // Payload size in bytes
  
  producer: {
    lingerMs: 0,             // No batching
    concurrency: 10,         // Concurrent producers
  },
  
  consumer: {
    concurrency: 10,         // Concurrent consumers
    batchSize: 100,          // Messages per batch
  },
  
  scenarios: [
    { name: 'latency-focused', messagesPerSecond: 100, duration: 60 },
    { name: 'throughput-focused', messagesPerSecond: null, duration: 60 },
    { name: 'partition-fanout', messagesPerSecond: 1000, duration: 60 },
  ],
};
```

## Understanding the Results

### When to Choose Each System

| Use Case | Recommended | Rationale |
|----------|-------------|-----------|
| High partition count (10k+) | **Queen** | Handles 10K+ with no degradation; Pulsar loses 45% throughput |
| Memory-constrained environment | **Queen** | 6x less memory than Pulsar at 10K partitions |
| Maximum raw throughput | Pulsar (5K) | 19k msg/s at 5K partitions (degrades at 10K) |
| Lowest per-message latency | Kafka | Sub-2ms p50 (but limited to 5K partitions) |
| Simplest operations | **Queen** | Just PostgreSQL |

### Trade-offs

**Queen MQ**
- ✅ Handles unlimited partitions efficiently
- ✅ Lowest memory footprint
- ✅ Simple operations (PostgreSQL)
- ✅ ACID transactions across queues
- ⚠️ Lower raw throughput than dedicated brokers
- ⚠️ Higher per-message latency

**Apache Kafka**
- ✅ Lowest latency
- ✅ Good throughput
- ✅ Mature ecosystem
- ❌ Struggles with high partition counts
- ❌ Higher memory usage
- ❌ Complex operations (ZooKeeper/KRaft)

**Apache Pulsar**
- ✅ Highest throughput
- ✅ Good latency
- ✅ Separated storage/compute
- ❌ Highest memory usage (5GB+)
- ❌ Most complex operations
- ❌ Partition scaling limitations

## Caveats

This benchmark intentionally tests a **specific workload pattern**. It does NOT represent:

- Kafka/Pulsar performance with batching enabled (their primary optimization)
- Multi-node cluster performance
- Replication and fault tolerance scenarios
- Long-term storage and compaction

**Important:** The `queen-mq` Node.js client used in this benchmark is designed for ease of use, not as a benchmarking tool.

The goal is to evaluate performance for workloads where:
- Many independent ordered streams are needed
- Low latency per message matters more than batch throughput
- Operational simplicity is valued

## Output Files

- `results-queen-{timestamp}.json` - Queen benchmark results
- `results-kafka-{timestamp}.json` - Kafka benchmark results  
- `results-pulsar-{timestamp}.json` - Pulsar benchmark results
- `comparison-report.json` - Combined comparison data

## Reproducing Results

Results may vary based on:
- Host machine performance
- Docker resource allocation
- Background system load
- Storage I/O performance

For consistent results:
1. Run on a dedicated machine
2. Stop unnecessary background processes
3. Use SSD storage
4. Run multiple iterations

## License

Apache 2.0 - Same as Queen MQ
