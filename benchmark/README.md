# 👑 Queen Benchmark Tool

High-performance benchmark utility for Queen Message Queue C++ client.

## Features

- ✅ **Producer Mode** - Measure push throughput
- ✅ **Consumer Mode** - Measure consume throughput  
- ✅ **Concurrent Threads** - Scale with multiple workers
- ✅ **Single-Queue Mode** - One queue with N partitions
- ✅ **Multi-Queue Mode** - N queues with one partition each
- ✅ **Client-Side Buffering** - Automatic batching for producers
- ✅ **Performance Metrics** - Messages/sec, MB/sec, latency

## Quick Start

### Build

```bash
cd benchmark
make
```

### Run Producer Benchmark

```bash
# Single queue with 4 partitions, 4 threads, 100K messages
./bin/benchmark producer --threads 4 --count 100000 --batch 500 --partitions 4 --mode single-queue
```

### Run Consumer Benchmark

```bash
# Consume from same setup
./bin/benchmark consumer --threads 4 --batch 100 --partitions 4 --mode single-queue
```

## Usage

```
./bin/benchmark <mode> [options]
```

### Modes

- `producer` - Push messages to queues
- `consumer` - Consume messages from queues

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `--threads N` | Number of concurrent threads | 1 |
| `--server URL` | Server URL | http://localhost:6632 |
| `--batch N` | Batch size | 100 |
| `--count N` | Total messages (producer only) | 10000 |
| `--partitions N` | Number of partitions/queues | 4 |
| `--mode MODE` | Queue mode: single-queue or multi-queue | single-queue |

## Queue Modes

### Single-Queue Mode

**Setup:**
- Creates **one queue** (`benchmark-queue`)
- Each thread targets a **different partition**
- Example: 4 threads → 4 partitions (partition-0, partition-1, partition-2, partition-3)

**Use case:** Test partition-based parallelism

**Example:**
```bash
# Producer
./bin/benchmark producer --threads 4 --count 100000 --partitions 4 --mode single-queue

# Consumer
./bin/benchmark consumer --threads 4 --batch 100 --partitions 4 --mode single-queue
```

### Multi-Queue Mode

**Setup:**
- Creates **N queues** (benchmark-queue-0, benchmark-queue-1, ...)
- Each thread targets a **different queue**
- Each queue has one default partition

**Use case:** Test multi-queue parallelism

**Example:**
```bash
# Producer
./bin/benchmark producer --threads 8 --count 100000 --partitions 8 --mode multi-queue

# Consumer  
./bin/benchmark consumer --threads 8 --batch 100 --partitions 8 --mode multi-queue
```

## Example Scenarios

### Scenario 1: High-Throughput Producer

```bash
# 8 threads pushing 1M messages with large batches
./bin/benchmark producer \
    --threads 8 \
    --count 1000000 \
    --batch 1000 \
    --partitions 8 \
    --mode single-queue
```

Expected output:
```
========================================
Benchmark Results
========================================
Total Messages:     1000000
Total Bytes:        300000000
Errors:             0
Elapsed Time:       5.23 seconds
Throughput:         191205 msg/sec
Bandwidth:          54.58 MB/sec
========================================
```

### Scenario 2: Multi-Consumer Performance

```bash
# 16 consumers across 16 queues
./bin/benchmark consumer \
    --threads 16 \
    --batch 500 \
    --partitions 16 \
    --mode multi-queue
```

### Scenario 3: Partition-Based Load

```bash
# Single queue, 4 partitions, heavy load
./bin/benchmark producer \
    --threads 4 \
    --count 500000 \
    --batch 500 \
    --partitions 4 \
    --mode single-queue

# Then consume with same config
./bin/benchmark consumer \
    --threads 4 \
    --batch 500 \
    --partitions 4 \
    --mode single-queue
```

## Performance Tips

### For Maximum Throughput

1. **Increase batch size** - Larger batches = fewer HTTP requests
   ```bash
   --batch 1000
   ```

2. **Use more threads** - Match your CPU core count
   ```bash
   --threads 8
   ```

3. **Client-side buffering** - Producer automatically uses buffering
   - Buffer triggers at batch size or 100ms timeout

4. **Multi-queue mode** - Better parallelism for independent streams
   ```bash
   --mode multi-queue --partitions 16
   ```

### For Latency Testing

1. **Smaller batches** - Reduce buffering delay
   ```bash
   --batch 1
   ```

2. **Single-queue mode** - Simpler setup
   ```bash
   --mode single-queue --partitions 1
   ```

## Interpreting Results

### Producer Results

```
Total Messages:     100000
Total Bytes:        30000000
Elapsed Time:       2.34 seconds
Throughput:         42735 msg/sec
Bandwidth:          12.21 MB/sec
```

- **Throughput** - Messages per second produced
- **Bandwidth** - Data throughput in MB/sec
- Higher is better

### Consumer Results

```
Total Messages:     100000
Total Bytes:        30000000
Elapsed Time:       3.12 seconds
Throughput:         32051 msg/sec
Bandwidth:          9.15 MB/sec
```

- **Throughput** - Messages per second consumed
- **Bandwidth** - Data throughput in MB/sec
- Consumer typically slower due to ACK overhead

## Workflow

### Complete Producer → Consumer Test

```bash
# Terminal 1: Start producer
./bin/benchmark producer --threads 4 --count 100000 --batch 500 --partitions 4

# Terminal 2: Start consumer (simultaneously or after)
./bin/benchmark consumer --threads 4 --batch 100 --partitions 4
```

## Advanced Usage

### Custom Server

```bash
./bin/benchmark producer \
    --server http://my-server:6632 \
    --threads 4 \
    --count 50000
```

### High Concurrency Test

```bash
# 32 threads, 32 partitions
./bin/benchmark producer \
    --threads 32 \
    --count 1000000 \
    --batch 500 \
    --partitions 32 \
    --mode single-queue
```

### Stress Test

```bash
# Push 10 million messages
./bin/benchmark producer \
    --threads 16 \
    --count 10000000 \
    --batch 1000 \
    --partitions 16 \
    --mode multi-queue
```

## Building from Source

```bash
cd benchmark
make clean
make
```

Binary location: `bin/benchmark`

## Cleanup

```bash
# Remove all benchmark queues manually if needed
# (Or let them accumulate for testing)
```

## Notes

- Producer uses **client-side buffering** for performance
- Consumer uses **long polling** with 5-second idle timeout
- Each thread processes messages independently
- Statistics are thread-safe (atomic counters)
- Optimized with `-O3 -march=native` for maximum performance

## Troubleshooting

### Connection Refused

**Problem:** Server not running  
**Solution:**
```bash
cd ../server
./bin/queen-server
```

### Low Throughput

**Try:**
- Increase `--batch` size
- Increase `--threads` count
- Use `--mode multi-queue` for better parallelism
- Check server resources (CPU, memory)

## Performance Expectations

Typical results on modern hardware:

- **Producer:** 50K - 200K msg/sec (depending on config)
- **Consumer:** 30K - 150K msg/sec (slower due to ACK)
- **Single message:** ~100-300 bytes (with overhead)

Actual performance depends on:
- CPU cores
- Network latency
- Server configuration
- Batch size
- Thread count

---

**Happy Benchmarking! 🚀**

