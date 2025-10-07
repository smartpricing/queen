# Performance Tuning Guide for Queen

## Database Connection Pool Optimization

When running Queen under high load, you may encounter database connection pool exhaustion errors:
```
Error: timeout exceeded when trying to connect
```

This happens when the rate of incoming requests exceeds the available database connections.

## Solution 1: Increase Connection Pool Size

Set these environment variables before starting the server:

```bash
# Increase the connection pool size (default is 20)
export DB_POOL_SIZE=50

# Increase connection timeout (default is 2000ms)
export DB_CONNECTION_TIMEOUT=5000

# Start the server
npm start
```

## Solution 2: Optimize PostgreSQL Settings

Edit your `postgresql.conf`:

```conf
# Increase maximum connections
max_connections = 200

# Connection pooling
shared_preload_libraries = 'pg_stat_statements'

# Memory settings for better performance
shared_buffers = 256MB
effective_cache_size = 1GB
work_mem = 4MB
maintenance_work_mem = 64MB
```

## Solution 3: Use Batch Inserts (Database Optimization)

The current implementation processes messages individually within a transaction. For better performance under high load, consider using batch inserts.

### Current Implementation (Sequential)
```javascript
// Process each item individually
for (const item of items) {
  // Individual INSERT for each message
}
```

### Optimized Implementation (Batch)
```sql
-- Use single INSERT with multiple VALUES
INSERT INTO queen.messages (transaction_id, queue_id, payload, status)
VALUES 
  ($1, $2, $3, 'pending'),
  ($4, $5, $6, 'pending'),
  ...
RETURNING id, transaction_id;
```

## Solution 4: Implement Connection Pooling with PgBouncer

For production environments with very high load, use PgBouncer as a connection pooler:

1. Install PgBouncer:
```bash
# macOS
brew install pgbouncer

# Linux
apt-get install pgbouncer
```

2. Configure PgBouncer (`/etc/pgbouncer/pgbouncer.ini`):
```ini
[databases]
queen = host=localhost port=5432 dbname=postgres

[pgbouncer]
listen_port = 6432
listen_addr = *
auth_type = md5
auth_file = /etc/pgbouncer/userlist.txt
pool_mode = transaction
max_client_conn = 1000
default_pool_size = 50
```

3. Update Queen to connect through PgBouncer:
```bash
export PG_PORT=6432  # PgBouncer port
npm start
```

## Recommended Production Settings

For a production environment handling high message throughput:

### Environment Variables
```bash
# Database
DB_POOL_SIZE=50
DB_CONNECTION_TIMEOUT=5000
DB_IDLE_TIMEOUT=30000

# Application
NODE_ENV=production
NODE_OPTIONS="--max-old-space-size=4096"

# Monitoring
LOG_LEVEL=warn
ENABLE_METRICS=true
```

### Example High-Load Configuration

For sustained load of 1000+ messages/second:

```bash
# Start server with optimized settings
DB_POOL_SIZE=100 \
DB_CONNECTION_TIMEOUT=10000 \
NODE_OPTIONS="--max-old-space-size=8192" \
npm start
```

## Testing Under Load

### Reasonable Load Test
```bash
# 10 messages per batch, every 100ms = ~100 msg/sec
BATCH_SIZE=10 INTERVAL=100 node examples/continuous-producer.js
```

### Medium Load Test
```bash
# 50 messages per batch, every 500ms = ~100 msg/sec
BATCH_SIZE=50 INTERVAL=500 node examples/continuous-producer.js
```

### High Load Test (requires tuning)
```bash
# First, increase DB pool
export DB_POOL_SIZE=100

# Then run high load producer
# 100 messages per batch, every 1000ms = ~100 msg/sec
BATCH_SIZE=100 INTERVAL=1000 node examples/continuous-producer.js
```

## Monitoring Performance

Monitor these metrics to identify bottlenecks:

1. **Database Connections**
```sql
-- Check active connections
SELECT count(*) FROM pg_stat_activity;

-- Check connections by state
SELECT state, count(*) 
FROM pg_stat_activity 
GROUP BY state;
```

2. **Connection Pool Status**
```javascript
// Add monitoring endpoint to server
app.get('/metrics/pool', (req, res) => {
  res.json({
    totalCount: pool.totalCount,
    idleCount: pool.idleCount,
    waitingCount: pool.waitingCount
  });
});
```

3. **Message Throughput**
```bash
# Monitor message insertion rate
watch -n 1 "psql -c 'SELECT count(*) FROM queen.messages WHERE created_at > NOW() - INTERVAL '\''1 minute'\'';'"
```

## Best Practices

1. **Start Conservative**: Begin with lower message rates and gradually increase
2. **Monitor Resources**: Watch CPU, memory, and database connections
3. **Use Batching**: Process messages in reasonable batches (10-100 per batch)
4. **Implement Backpressure**: Slow down producers when consumers can't keep up
5. **Scale Horizontally**: Run multiple Queen instances behind a load balancer
6. **Use Read Replicas**: For analytics and read-heavy operations

## Troubleshooting

### "timeout exceeded when trying to connect"
- Increase `DB_POOL_SIZE`
- Increase `DB_CONNECTION_TIMEOUT`
- Check PostgreSQL `max_connections`
- Consider using PgBouncer

### "Push request aborted"
- Client timeout is too short
- Increase client timeout in producer
- Reduce batch size

### High Memory Usage
- Reduce batch sizes
- Increase Node.js heap size with `--max-old-space-size`
- Implement message pagination

### Slow Message Processing
- Add database indexes
- Use batch operations
- Implement caching for frequently accessed data
- Consider partitioning large tables
