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
NUM_WORKERS=10              # HTTP worker threads
DB_POOL_SIZE=150            # Database connections
POLL_WORKER_COUNT=10        # Long-polling worker threads (scale for load)
```

### Long-Polling Scaling

Scale poll workers based on concurrent waiting clients:

```bash
# Low load (< 20 clients)
POLL_WORKER_COUNT=2

# Medium load (20-100 clients)  
POLL_WORKER_COUNT=10

# High load (100-200 clients)
POLL_WORKER_COUNT=20

# Very high load (200+ clients)
POLL_WORKER_COUNT=50
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
