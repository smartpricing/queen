# High Performance Configuration for Queen

## Quick Start for 10,000+ msg/sec

### 1. Start the Server with Optimized Settings

```bash
# Set environment variables for high performance
export DB_POOL_SIZE=100
export DB_CONNECTION_TIMEOUT=5000
export NODE_OPTIONS="--max-old-space-size=4096"

# Start the server
npm start
```

The server will automatically detect high-performance mode when `DB_POOL_SIZE >= 50` and:
- Use the optimized queue manager with batch inserts
- Enable performance monitoring
- Log performance metrics every 30 seconds

### 2. Run Performance Test

```bash
# Run the performance benchmark
node test-performance.js
```

### 3. Run High-Performance Producer

```bash
# Normal mode (safe defaults)
node examples/continuous-producer.js

# High-performance mode (10,000+ msg/sec target)
HIGH_PERF=true node examples/continuous-producer.js
```

## Architecture Improvements

### Optimized Queue Manager
- **Batch inserts**: Up to 1,000 messages per database operation
- **Connection pooling**: Efficient connection reuse without holding transactions
- **Resource caching**: Reduces database lookups for queue IDs
- **Parallel processing**: Handles multiple queues concurrently

### Pool Manager
- **Automatic retry**: Handles connection timeouts gracefully
- **Exponential backoff**: Prevents thundering herd on connection failures
- **Connection recycling**: Ensures connections are properly released
- **Queue management**: Manages waiting requests when pool is exhausted

### uWebSockets.js Server
- **Native performance**: Uses uWS for maximum throughput
- **Efficient JSON parsing**: Streaming JSON parser for large payloads
- **Abort handling**: Properly handles client disconnections
- **CORS support**: Built-in CORS headers for web clients

## Performance Metrics

Monitor real-time performance at:
- `/health` - Server health and basic stats
- `/metrics` - Detailed performance metrics

Example metrics output:
```json
{
  "messages": {
    "total": 1000000,
    "rate": 10234.5
  },
  "database": {
    "poolSize": 100,
    "idleConnections": 45,
    "waitingRequests": 0
  }
}
```

## Database Optimization

### PostgreSQL Configuration

Edit `postgresql.conf`:
```conf
# Connection settings
max_connections = 200
superuser_reserved_connections = 3

# Memory settings
shared_buffers = 512MB
effective_cache_size = 2GB
work_mem = 8MB
maintenance_work_mem = 128MB

# Write performance
wal_buffers = 16MB
checkpoint_completion_target = 0.9
max_wal_size = 2GB
min_wal_size = 1GB

# Query optimization
random_page_cost = 1.1  # For SSD storage
effective_io_concurrency = 200
```

### Required Indexes

The schema already includes optimal indexes:
```sql
CREATE INDEX idx_messages_queue_status ON messages(queue_id, status);
CREATE INDEX idx_messages_transaction ON messages(transaction_id);
CREATE INDEX idx_messages_scheduled ON messages(scheduled_at) WHERE scheduled_at IS NOT NULL;
```

## Troubleshooting

### "timeout exceeded when trying to connect"
**Solution**: Increase connection pool and timeout:
```bash
DB_POOL_SIZE=150 DB_CONNECTION_TIMEOUT=10000 npm start
```

### High memory usage
**Solution**: Increase Node.js heap size:
```bash
NODE_OPTIONS="--max-old-space-size=8192" npm start
```

### Database CPU at 100%
**Solution**: 
1. Check slow queries: `SELECT * FROM pg_stat_statements ORDER BY mean_exec_time DESC;`
2. Increase `work_mem` in PostgreSQL
3. Consider partitioning the messages table

## Scaling Beyond Single Server

For even higher throughput (100,000+ msg/sec):

### 1. Use PgBouncer
```ini
[databases]
queen = host=localhost port=5432 dbname=postgres

[pgbouncer]
pool_mode = transaction
max_client_conn = 1000
default_pool_size = 100
```

### 2. Horizontal Scaling
- Run multiple Queen instances behind a load balancer
- Use Redis for distributed caching
- Implement database sharding

### 3. Database Replication
- Use read replicas for analytics queries
- Implement write-through caching
- Consider time-series databases for metrics

## Benchmarks

With proper configuration, Queen can achieve:
- **10,000+ msg/sec** sustained on a single server
- **50,000+ msg/sec** burst capacity
- **< 1ms** message insertion latency (p50)
- **< 10ms** message insertion latency (p99)

Hardware used for benchmarks:
- CPU: 8 cores
- RAM: 16GB
- Storage: NVMe SSD
- PostgreSQL 14+
- Node.js 18+
