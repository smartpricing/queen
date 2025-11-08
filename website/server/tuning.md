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

### 3. Poll Workers

```bash
NUM_POLL_WORKERS=4  # Default
```

For heavy long-polling: increase to 8-16

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
