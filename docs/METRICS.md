# System Metrics Collection

Queen MQ automatically collects system metrics and stores them in the database for monitoring and analysis.

## Architecture

- **Sampling**: Metrics are collected every **1 second**
- **Aggregation**: Data is aggregated (min, max, avg, last) and saved every **60 seconds**
- **Storage**: Stored in JSONB format for flexibility
- **Background Task**: Runs in dedicated System ThreadPool (non-blocking)

## Table Schema

```sql
CREATE TABLE queen.system_metrics (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    timestamp TIMESTAMPTZ NOT NULL,          -- Start of the minute (truncated)
    hostname TEXT NOT NULL,                  -- Server hostname
    port INTEGER NOT NULL,                   -- Server port
    process_id INTEGER NOT NULL,             -- Process ID
    worker_id TEXT NOT NULL,                 -- Worker identifier
    sample_count INTEGER NOT NULL DEFAULT 60, -- Number of samples aggregated
    metrics JSONB NOT NULL,                  -- Aggregated metrics
    CONSTRAINT unique_metric_per_replica 
        UNIQUE (timestamp, hostname, port, process_id, worker_id)
);
```

## Metrics Collected

### CPU
- `cpu.user_us` - User CPU percentage × 100 (e.g., 1523 = 15.23%) (avg, min, max, last)
- `cpu.system_us` - System CPU percentage × 100 (avg, min, max, last)
- **Note:** Calculated as delta between consecutive samples: `(delta_cpu_time / delta_wall_time) × 100`

### Memory
- `memory.rss_bytes` - Resident Set Size in bytes (avg, min, max, last)
- `memory.virtual_bytes` - Virtual memory size in bytes (avg, min, max, last)

### Database Pool
- `database.pool_size` - Total connections in pool (avg, min, max, last)
- `database.pool_idle` - Idle connections (avg, min, max, last)
- `database.pool_active` - Active connections (avg, min, max, last)

### ThreadPools
- `threadpool.db.pool_size` - DB threadpool size (avg, min, max, last)
- `threadpool.db.queue_size` - DB operations queued (avg, min, max, last)
- `threadpool.system.pool_size` - System threadpool size (avg, min, max, last)
- `threadpool.system.queue_size` - System tasks queued (avg, min, max, last)

### Uptime
- `uptime_seconds` - Server uptime in seconds

## Example Queries

### Recent Metrics (Last Hour)
```sql
SELECT 
    timestamp,
    hostname,
    port,
    worker_id,
    sample_count,
    metrics->'cpu'->'user_us'->>'avg' as cpu_user_avg,
    metrics->'cpu'->'user_us'->>'max' as cpu_user_max,
    metrics->'memory'->'rss_bytes'->>'avg' as memory_avg_bytes,
    (metrics->'memory'->'rss_bytes'->>'avg')::bigint / 1024 / 1024 as memory_avg_mb,
    metrics->'database'->'pool_active'->>'avg' as db_active_avg,
    metrics->'database'->'pool_active'->>'max' as db_active_max,
    metrics->'threadpool'->'db'->'queue_size'->>'avg' as db_queue_avg,
    metrics->'threadpool'->'db'->'queue_size'->>'max' as db_queue_max
FROM queen.system_metrics
WHERE timestamp >= NOW() - INTERVAL '1 hour'
ORDER BY timestamp DESC;
```

### Find Performance Issues
```sql
-- Find minutes where DB threadpool queue was high
SELECT 
    timestamp,
    hostname,
    port,
    (metrics->'threadpool'->'db'->'queue_size'->>'max')::int as max_db_queue,
    (metrics->'database'->'pool_active'->>'max')::int as max_db_active,
    (metrics->'database'->'pool_size'->>'avg')::int as pool_size
FROM queen.system_metrics
WHERE 
    (metrics->'threadpool'->'db'->'queue_size'->>'max')::int > 50
    AND timestamp >= NOW() - INTERVAL '24 hours'
ORDER BY max_db_queue DESC
LIMIT 20;
```

### Memory Growth Detection
```sql
-- Detect memory growth over time
WITH memory_growth AS (
    SELECT 
        timestamp,
        hostname,
        port,
        (metrics->'memory'->'rss_bytes'->>'avg')::bigint / 1024 / 1024 as memory_mb,
        LAG((metrics->'memory'->'rss_bytes'->>'avg')::bigint) 
            OVER (PARTITION BY hostname, port ORDER BY timestamp) / 1024 / 1024 as prev_memory_mb
    FROM queen.system_metrics
    WHERE timestamp >= NOW() - INTERVAL '6 hours'
)
SELECT 
    hostname,
    port,
    COUNT(*) as samples,
    AVG(memory_mb - prev_memory_mb) as avg_growth_mb_per_minute,
    MAX(memory_mb) as max_memory_mb
FROM memory_growth
WHERE prev_memory_mb IS NOT NULL
GROUP BY hostname, port
ORDER BY avg_growth_mb_per_minute DESC;
```

### Compare Metrics Across Replicas
```sql
-- Compare DB connection usage across all replicas
SELECT 
    hostname,
    port,
    worker_id,
    timestamp,
    (metrics->'database'->'pool_active'->>'avg')::numeric as avg_active,
    (metrics->'database'->'pool_active'->>'max')::int as max_active,
    (metrics->'database'->'pool_size'->>'avg')::int as pool_size,
    ROUND(
        (metrics->'database'->'pool_active'->>'avg')::numeric / 
        NULLIF((metrics->'database'->'pool_size'->>'avg')::numeric, 0) * 100, 
        2
    ) as utilization_pct
FROM queen.system_metrics
WHERE timestamp >= NOW() - INTERVAL '1 hour'
ORDER BY timestamp DESC, hostname, port;
```

### CPU Usage Over Time
```sql
-- CPU usage trend (convert microseconds to seconds)
SELECT 
    timestamp,
    hostname,
    port,
    (metrics->'cpu'->'user_us'->>'avg')::bigint / 1000000.0 as cpu_user_seconds,
    (metrics->'cpu'->'system_us'->>'avg')::bigint / 1000000.0 as cpu_system_seconds,
    (metrics->'uptime_seconds')::int as uptime_seconds
FROM queen.system_metrics
WHERE 
    timestamp >= NOW() - INTERVAL '24 hours'
    AND hostname = 'your-hostname'
ORDER BY timestamp;
```

### Active Replicas
```sql
-- See all replicas that have reported metrics in last 5 minutes
SELECT DISTINCT 
    hostname,
    port,
    process_id,
    worker_id,
    MAX(timestamp) as last_seen,
    COUNT(*) as data_points
FROM queen.system_metrics
WHERE timestamp >= NOW() - INTERVAL '5 minutes'
GROUP BY hostname, port, process_id, worker_id
ORDER BY last_seen DESC;
```

## Performance Impact

- **CPU**: < 0.01% (negligible)
- **I/O**: ~2ms per minute for database write
- **Storage**: ~720KB per day (~22MB per month)
- **Memory**: Minimal (buffer holds max 120 samples)

## Retention

Consider setting up automatic cleanup:

```sql
-- Delete metrics older than 90 days (run daily)
DELETE FROM queen.system_metrics 
WHERE timestamp < NOW() - INTERVAL '90 days';
```

Or use PostgreSQL table partitioning for automatic cleanup:

```sql
-- Example: Create monthly partitions
CREATE TABLE queen.system_metrics_2025_01 
    PARTITION OF queen.system_metrics
    FOR VALUES FROM ('2025-01-01') TO ('2025-02-01');
```

## Monitoring Thresholds

### Warning Signs
- **DB Queue**: max > 100 → Increase DB pool or optimize queries
- **DB Active**: avg > 90% of pool size → Increase `DB_POOL_SIZE`
- **Memory Growth**: > 1MB/min sustained → Possible memory leak
- **System Queue**: max > 10 → Increase system threadpool size

### Healthy System
- DB Queue: < 20 tasks
- DB Active: < 70% of pool size
- Memory: Stable, not growing
- System Queue: 0-5 tasks

