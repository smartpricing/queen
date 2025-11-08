# Benchmarks

Queen MQ performance benchmarks.

## Test Environment

- **Hardware**: Apple M4 Air
- **Server**: 4 workers, 95 DB connections
- **Database**: PostgreSQL in Docker

## Results

| Test | Mode | Messages | Batch | Throughput | Bandwidth |
|------|------|----------|-------|------------|-----------|
| T4 | Producer | 10,000 | 1,000 | 85,862 msg/s | 24.57 MB/s |
| T4 | Consumer | 10,000 | 1,000 | **488,650 msg/s** | **247.87 MB/s** |
| T5 | Producer | 100,000 | 1,000 | **90,601 msg/s** | **25.92 MB/s** |
| T5 | Consumer | 100,000 | 1,000 | 84,530 msg/s | 42.96 MB/s |

## Key Observations

- ✅ Batch size matters: 1,000x better than batch=1
- ✅ Consumer peak: 488K msg/s
- ✅ Producer peak: 90K msg/s
- ✅ Scales with message volume

## Run Your Own

```bash
cd benchmark
make
./bin/benchmark producer --threads 10 --count 1000000 --batch 1000
./bin/benchmark consumer --threads 10 --batch 1000
```

[Complete benchmarks](https://github.com/smartpricing/queen/tree/master/benchmark)
