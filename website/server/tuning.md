# Performance Tuning

Optimize Queen server for your workload.

## Key Parameters

### 1. Database Pool Size

```bash
DB_POOL_SIZE=150  # Default
```

**Guidelines:**
- Light load: 20-50
- Medium load: 50-100
- Heavy load: 100-200

### 2. Worker Threads

```bash
NUM_WORKERS=10  # Default
```

**Guidelines:**
- Match CPU cores
- 10-20 for most workloads

### 3. Poll Workers (ThreadPool-Managed)

```bash
# Regular queue poll workers
POLL_WORKER_COUNT=2              # Default for regular long-polling

# Stream poll workers
STREAM_POLL_WORKER_COUNT=2       # Default for stream polling
STREAM_CONCURRENT_CHECKS=10      # Concurrent window checks per worker

# DB ThreadPool size is auto-calculated:
# = POLL_WORKER_COUNT + STREAM_POLL_WORKER_COUNT + 
#   (STREAM_POLL_WORKER_COUNT × STREAM_CONCURRENT_CHECKS) + 
#   DB_THREAD_POOL_SERVICE_THREADS (5)
# Default: 2 + 2 + 20 + 5 = 29 threads
```

**Scaling for Heavy Long-Polling:**
```bash
POLL_WORKER_COUNT=10             # More regular poll workers
STREAM_POLL_WORKER_COUNT=4       # More stream workers
STREAM_CONCURRENT_CHECKS=15      # More concurrent stream checks
```

## PostgreSQL Tuning

```sql
-- Connection limit
max_connections = 200

-- Shared buffers
shared_buffers = 2GB

-- Work memory
work_mem = 64MB
```

## Monitoring

```bash
curl http://localhost:6632/metrics
```

[Complete tuning guide](https://github.com/smartpricing/queen/blob/master/server/README.md)
