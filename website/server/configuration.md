# Server Configuration

Configure Queen server with environment variables.

## Essential Variables

```bash
# PostgreSQL
PG_HOST=localhost
PG_PORT=5432
PG_USER=queen
PG_PASSWORD=your-password
PG_DB=queen

# Server
PORT=6632
NUM_WORKERS=10
DB_POOL_SIZE=50
```

## Performance Tuning

```bash
NUM_WORKERS=10                      # HTTP worker threads (uWebSockets)
DB_POOL_SIZE=150                    # Database connections

# ThreadPool Configuration (all workers use managed pools)
POLL_WORKER_COUNT=2                 # Regular poll workers
STREAM_POLL_WORKER_COUNT=2          # Stream poll workers
STREAM_CONCURRENT_CHECKS=10         # Concurrent stream checks per worker
DB_THREAD_POOL_SERVICE_THREADS=5    # Service DB operations

# DB ThreadPool auto-calculated: P + S + (S × C) + T
# Default: 2 + 2 + 20 + 5 = 29 threads
```

### ThreadPool Architecture

All long-running worker threads use the **DB ThreadPool** for proper resource management:

```
DB ThreadPool Size = P + S + (S × C) + T

Where:
  P = POLL_WORKER_COUNT (regular poll workers)
  S = STREAM_POLL_WORKER_COUNT (stream poll workers)
  C = STREAM_CONCURRENT_CHECKS (concurrent checks per stream worker)
  T = DB_THREAD_POOL_SERVICE_THREADS (service operations)
```

### Long-Polling Scaling

Scale poll workers based on concurrent waiting clients:

```bash
# Low load (< 20 clients)
POLL_WORKER_COUNT=2
STREAM_POLL_WORKER_COUNT=2

# Medium load (20-100 clients)  
POLL_WORKER_COUNT=10
STREAM_POLL_WORKER_COUNT=4

# High load (100-200 clients)
POLL_WORKER_COUNT=20
STREAM_POLL_WORKER_COUNT=8

# Very high load (200+ clients)
POLL_WORKER_COUNT=50
STREAM_POLL_WORKER_COUNT=12
```

## Features

```bash
ENABLE_CORS=true
ENABLE_METRICS=true
ENABLE_TRACING=true
LOG_LEVEL=info
```

## Failover

```bash
FILE_BUFFER_DIR=/var/lib/queen/buffers
FILE_BUFFER_FLUSH_MS=100
FILE_BUFFER_MAX_BATCH=100
```

[Complete list](https://github.com/smartpricing/queen/blob/master/server/ENV_VARIABLES.md)
