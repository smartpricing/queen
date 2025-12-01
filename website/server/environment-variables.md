# Environment Variables Reference

Complete configuration reference for Queen MQ server.

## Quick Reference

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | 6632 | HTTP server port |
| `NUM_WORKERS` | 10 | Worker threads |
| `DB_POOL_SIZE` | 150 | **CRITICAL** - Connection pool size |
| `PG_HOST` | localhost | PostgreSQL host |
| `LOG_LEVEL` | info | Log level (debug, info, warn, error) |

## Server Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `PORT` | int | 6632 | HTTP server port |
| `HOST` | string | 0.0.0.0 | HTTP server host |
| `WORKER_ID` | string | cpp-worker-1 | Unique identifier for this worker |
| `NUM_WORKERS` | int | 10 | Number of worker threads |

**Example:**
```bash
export PORT=6632
export HOST=0.0.0.0
export NUM_WORKERS=10
./bin/queen-server
```

## Database Configuration

### Connection Settings

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `PG_USER` | string | postgres | PostgreSQL username |
| `PG_HOST` | string | localhost | PostgreSQL host |
| `PG_DB` | string | postgres | PostgreSQL database name |
| `PG_PASSWORD` | string | postgres | PostgreSQL password |
| `PG_PORT` | string | 5432 | PostgreSQL port |

### SSL Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `PG_USE_SSL` | bool | false | Enable SSL connection |
| `PG_SSL_REJECT_UNAUTHORIZED` | bool | true | Reject unauthorized SSL certificates |

**SSL Mode Mapping:**
- `PG_USE_SSL=true` + `PG_SSL_REJECT_UNAUTHORIZED=true` → `sslmode=require`
- `PG_USE_SSL=true` + `PG_SSL_REJECT_UNAUTHORIZED=false` → `sslmode=prefer`
- `PG_USE_SSL=false` → `sslmode=disable`

**Example for Production (Cloud SQL, RDS, Azure):**
```bash
export PG_HOST=your-db.cloud.provider.com
export PG_PORT=5432
export PG_USER=queen
export PG_PASSWORD=secure_password
export PG_DB=queen_production
export PG_USE_SSL=true
export PG_SSL_REJECT_UNAUTHORIZED=true
```

### Pool Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DB_POOL_SIZE` | int | 150 | **CRITICAL** - Total connection pool size |
| `DB_IDLE_TIMEOUT` | int | 30000 | Idle timeout in milliseconds |
| `DB_CONNECTION_TIMEOUT` | int | 2000 | Connection timeout in milliseconds |
| `DB_STATEMENT_TIMEOUT` | int | 30000 | Statement timeout in milliseconds |
| `DB_LOCK_TIMEOUT` | int | 10000 | Lock timeout in milliseconds |

::: warning CRITICAL: DB_POOL_SIZE
This is the **most important tuning parameter**. The pool is split:
- **AsyncDbPool**: 95% (142 connections at default) - for PUSH/POP/ACK/TRANSACTION
- **Background Pool**: 5% (8 connections at default) - for metrics, retention, eviction

Scale with workers: `DB_POOL_SIZE = NUM_WORKERS × 15` is a good starting point.
:::

**Example Configurations:**
```bash
# Light load
export DB_POOL_SIZE=50
export NUM_WORKERS=4

# Medium load (default)
export DB_POOL_SIZE=150
export NUM_WORKERS=10

# High load
export DB_POOL_SIZE=300
export NUM_WORKERS=20
```

## Queue Processing

### Pop Operation Defaults

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_TIMEOUT` | int | 30000 | Default timeout for pop operations (ms) |
| `DEFAULT_BATCH_SIZE` | int | 1 | Default batch size for pop operations |

### Stream Long Polling

Stream processing uses dedicated poll workers optimized for windowed message consumption.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `STREAM_POLL_WORKER_COUNT` | int | 1 | Number of dedicated stream poll worker threads |
| `STREAM_POLL_WORKER_INTERVAL` | int | 100 | How often stream workers check registry (ms) |
| `STREAM_POLL_INTERVAL` | int | 1000 | Min time between stream checks per group (ms) |
| `STREAM_BACKOFF_THRESHOLD` | int | 5 | Consecutive empty checks before backoff |
| `STREAM_BACKOFF_MULTIPLIER` | double | 2.0 | Exponential backoff multiplier |
| `STREAM_MAX_POLL_INTERVAL` | int | 5000 | Max poll interval after backoff (ms) |
| `STREAM_CONCURRENT_CHECKS` | int | 2 | Max concurrent window check jobs per worker |

**Scaling Guidelines:**
- Low load (< 10 stream consumers): 1 worker, 2 concurrent checks
- Medium load (10-50 consumers): 2-4 workers, 4 concurrent checks
- High load (50+ consumers): 4-8 workers, 8 concurrent checks

### ThreadPool Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DB_THREAD_POOL_SERVICE_THREADS` | int | 5 | Threads for background service DB operations |
| `QUEUE_BACKOFF_CLEANUP_THRESHOLD` | int | 3600 | Cleanup inactive backoff state entries (seconds) |

### POP_WAIT Backoff (Sidecar Long-Polling)

These settings control the backoff behavior for sidecar POP_WAIT (long-polling) requests.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `POP_WAIT_INITIAL_INTERVAL_MS` | int | 100 | Initial poll interval for POP_WAIT (ms) |
| `POP_WAIT_BACKOFF_THRESHOLD` | int | 3 | Consecutive empty checks before backoff starts |
| `POP_WAIT_BACKOFF_MULTIPLIER` | double | 2.0 | Exponential backoff multiplier |
| `POP_WAIT_MAX_INTERVAL_MS` | int | 1000 | Max poll interval after backoff (ms) |

**Backoff sequence example** (with defaults):
```
Check 1: 100ms (initial)
Check 2: 100ms
Check 3: 100ms (3rd empty → backoff starts)
Check 4: 200ms
Check 5: 400ms
Check 6: 800ms
Check 7+: 1000ms (capped at max)

Message arrives → Reset to 100ms immediately
```

::: tip Low Latency vs Resource Usage
- Lower `POP_WAIT_INITIAL_INTERVAL_MS` and `POP_WAIT_MAX_INTERVAL_MS` = faster response, more DB queries
- Higher values = slower response to messages, fewer DB queries when idle
:::

### Response Queue

Response processing uses an adaptive batching system that automatically scales up under load.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `RESPONSE_TIMER_INTERVAL_MS` | int | 25 | Response queue polling interval (ms) |
| `RESPONSE_BATCH_SIZE` | int | 100 | Base responses per timer tick |
| `RESPONSE_BATCH_MAX` | int | 500 | Maximum responses per tick under heavy load |

**Throughput at default settings (25ms interval):**
- Normal: 4,000 responses/sec per worker
- Maximum: 20,000 responses/sec per worker

### Sidecar Pool

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `SIDECAR_POOL_SIZE` | int | 50 | Number of connections in sidecar pool |
| `SIDECAR_MICRO_BATCH_WAIT_MS` | int | 5 | Micro-batch cycle time (ms) |
| `SIDECAR_MAX_ITEMS_PER_TX` | int | 1000 | Max items per database transaction |
| `SIDECAR_MAX_BATCH_SIZE` | int | 1000 | Max requests per micro-batch |
| `SIDECAR_MAX_PENDING_COUNT` | int | 50 | Max pending before immediate send |

### Consumer Group Subscription

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_SUBSCRIPTION_MODE` | string | "" | Default subscription mode for new consumer groups |

**Options:**
- `""` (empty) - Process all messages including historical
- `"new"` - Skip historical messages, only process messages after subscription
- `"new-only"` - Same as "new"

**Use Cases:**
```bash
# Real-time systems - skip historical backlog
export DEFAULT_SUBSCRIPTION_MODE="new"

# Batch processing - process everything
export DEFAULT_SUBSCRIPTION_MODE=""
```

::: tip
Only applies when client doesn't explicitly specify `.subscriptionMode()`. Existing consumer groups are not affected.
:::

## Background Services

### Metrics Collector

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `METRICS_SAMPLE_INTERVAL_MS` | int | 1000 | How often to sample metrics (ms) |
| `METRICS_AGGREGATE_INTERVAL_S` | int | 60 | How often to write to database (seconds) |

### Retention Service

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `RETENTION_INTERVAL` | int | 300000 | Retention service interval (ms) - 5 minutes |
| `RETENTION_BATCH_SIZE` | int | 1000 | Retention batch size |
| `PARTITION_CLEANUP_DAYS` | int | 30 | Days before partition cleanup |
| `METRICS_RETENTION_DAYS` | int | 90 | Days to keep metrics data |

### Eviction Service

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `EVICTION_INTERVAL` | int | 60000 | Eviction service interval (ms) - 1 minute |
| `EVICTION_BATCH_SIZE` | int | 1000 | Eviction batch size |

## File Buffer Configuration (QoS 0 & Failover)

Queen provides zero-message-loss failover using file-based buffers when PostgreSQL is unavailable.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `FILE_BUFFER_DIR` | string | Platform-specific* | Directory for file buffers |
| `FILE_BUFFER_FLUSH_MS` | int | 100 | How often to scan for complete buffer files (ms) |
| `FILE_BUFFER_MAX_BATCH` | int | 100 | Maximum events per database transaction |
| `FILE_BUFFER_EVENTS_PER_FILE` | int | 10000 | Create new buffer file after N events |

**Platform-specific defaults:**
- **macOS**: `/tmp/queen`
- **Linux**: `/var/lib/queen/buffers`

**How it works:**
```
1. Client pushes with { buffer: true } or DB is down
2. Event written to: qos0_<uuid>.buf.tmp (active file)
3. File finalized when EITHER:
   - Reaches 10,000 events (high throughput), OR
   - 200ms passes with no new writes (low throughput)
4. Atomic rename: .tmp → .buf
5. Background processor picks up .buf files every 100ms
6. Processes in batches of 100 events per DB transaction
7. Deletes file when complete
```

**Configuration Examples:**
```bash
# High-throughput configuration
export FILE_BUFFER_DIR=/data/queen/buffers
export FILE_BUFFER_FLUSH_MS=50
export FILE_BUFFER_MAX_BATCH=1000
export FILE_BUFFER_EVENTS_PER_FILE=50000

# Low-latency configuration
export FILE_BUFFER_DIR=/data/queen/buffers
export FILE_BUFFER_FLUSH_MS=10
export FILE_BUFFER_MAX_BATCH=50
export FILE_BUFFER_EVENTS_PER_FILE=1000
```

## Encryption Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_ENCRYPTION_KEY` | string | - | Encryption key (64 hex characters for AES-256) |

**Generate a secure key:**
```bash
openssl rand -hex 32
```

**Example:**
```bash
export QUEEN_ENCRYPTION_KEY=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
```

::: warning Security
- Uses AES-256-GCM encryption
- Key must be exactly 64 hexadecimal characters (32 bytes)
- Keep key secure and never commit to version control
:::

## Logging Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `LOG_LEVEL` | string | info | Log level |

**Log Levels:**
- `trace` - Extremely verbose (development only)
- `debug` - Detailed debugging information
- `info` - General information (recommended for production)
- `warn` - Warnings only
- `error` - Errors only

**Examples:**
```bash
# Development
export LOG_LEVEL=debug

# Production
export LOG_LEVEL=info
```

## Inter-Instance Communication (Clustered Deployments)

In clustered deployments, Queen servers can notify each other when messages are pushed or acknowledged, allowing poll workers on all instances to respond immediately.

### Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_UDP_PEERS` | string | "" | UDP peers: comma-separated `host:port` (e.g., `queen2:6633,queen3:6633`) |
| `QUEEN_UDP_NOTIFY_PORT` | int | 6633 | Default UDP port for notifications |

::: tip Self-Detection
Each server automatically excludes itself from the peer list based on hostname matching, so you can use the same peer list on all instances in a StatefulSet.
:::

### Single Server (Default)

Local poll worker notification is automatic - no configuration needed:

```bash
./bin/queen-server
```

### Cluster with UDP

```bash
# Server A
export QUEEN_UDP_PEERS="queen-b:6633,queen-c:6633"
export QUEEN_UDP_NOTIFY_PORT=6633
./bin/queen-server

# Server B
export QUEEN_UDP_PEERS="queen-a:6633,queen-c:6633"
export QUEEN_UDP_NOTIFY_PORT=6633
./bin/queen-server
```

### Kubernetes StatefulSet

```yaml
env:
  - name: QUEEN_UDP_PEERS
    value: "queen-mq-0.queen-mq-headless.ns.svc.cluster.local:6633,queen-mq-1.queen-mq-headless.ns.svc.cluster.local:6633,queen-mq-2.queen-mq-headless.ns.svc.cluster.local:6633"
  - name: QUEEN_UDP_NOTIFY_PORT
    value: "6633"
```

## Distributed Cache (UDPSYNC)

Queen includes a distributed cache layer that shares state between server instances.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_SYNC_ENABLED` | bool | true | Enable/disable distributed cache sync |
| `QUEEN_SYNC_SECRET` | string | "" | HMAC-SHA256 secret for packet signing (64 hex chars) |
| `QUEEN_CACHE_PARTITION_MAX` | int | 10000 | Maximum partition IDs to cache (LRU eviction) |
| `QUEEN_CACHE_PARTITION_TTL_MS` | int | 300000 | Partition ID cache TTL (ms) - 5 minutes |
| `QUEEN_CACHE_REFRESH_INTERVAL_MS` | int | 60000 | Queue config refresh from DB (ms) - 60 seconds |
| `QUEEN_SYNC_HEARTBEAT_MS` | int | 1000 | Heartbeat interval (ms) |
| `QUEEN_SYNC_DEAD_THRESHOLD_MS` | int | 5000 | Server dead threshold (ms) - 5 missed heartbeats |
| `QUEEN_SYNC_RECV_BUFFER_MB` | int | 8 | UDP receive buffer size (MB) |

### Security

For production deployments, set `QUEEN_SYNC_SECRET` to a 64-character hex string:

```bash
# Generate a secure secret
export QUEEN_SYNC_SECRET=$(openssl rand -hex 32)
```

Without a secret, packets are NOT signed (insecure mode - acceptable for development only).

### Correctness Guarantees

The cache is **always advisory**. Even with stale or missing cache data:
- Messages are never lost
- Duplicate deliveries never occur
- Leases are always correct (database is authoritative)
- The only impact is slightly increased DB queries

## Configuration Examples

### Development Environment

```bash
export PORT=6632
export HOST=localhost
export PG_HOST=localhost
export PG_USER=postgres
export PG_PASSWORD=postgres
export PG_DB=queen_dev
export DB_POOL_SIZE=50
export NUM_WORKERS=4
export LOG_LEVEL=debug
```

### Production Environment

```bash
export PORT=6632
export HOST=0.0.0.0
export PG_HOST=db.production.example.com
export PG_PORT=5432
export PG_USER=queen_user
export PG_PASSWORD=secure_password
export PG_DB=queen_production
export PG_USE_SSL=true
export PG_SSL_REJECT_UNAUTHORIZED=true
export DB_POOL_SIZE=300
export NUM_WORKERS=20
export LOG_LEVEL=info
export QUEEN_ENCRYPTION_KEY=your_64_char_hex_key_here
export FILE_BUFFER_DIR=/var/lib/queen/buffers
```

### High-Throughput Configuration

```bash
export DB_POOL_SIZE=500
export NUM_WORKERS=30
export SIDECAR_POOL_SIZE=100
export SIDECAR_MAX_ITEMS_PER_TX=2000
export RESPONSE_BATCH_SIZE=200
export RESPONSE_BATCH_MAX=1000
export FILE_BUFFER_FLUSH_MS=50
export FILE_BUFFER_MAX_BATCH=1000
export FILE_BUFFER_EVENTS_PER_FILE=50000
```

### Real-Time System (Skip Historical Messages)

```bash
export PORT=6632
export HOST=0.0.0.0
export PG_HOST=db.production.example.com
export PG_USER=queen_user
export PG_PASSWORD=secure_password
export PG_DB=queen_production
export PG_USE_SSL=true
export DB_POOL_SIZE=200
export NUM_WORKERS=15
export DEFAULT_SUBSCRIPTION_MODE="new"  # New consumer groups skip historical
export LOG_LEVEL=info
```

### Low-Latency Configuration

```bash
export DB_POOL_SIZE=150
export NUM_WORKERS=10
export RESPONSE_TIMER_INTERVAL_MS=10
export RESPONSE_BATCH_SIZE=50
export RESPONSE_BATCH_MAX=200
export FILE_BUFFER_FLUSH_MS=10
```

## Notes

::: tip Boolean Values
Set to `"true"` to enable, any other value (including unset) is treated as false.
:::

::: tip Integer Values
Must be valid integers, invalid values will fall back to defaults.
:::

::: warning Encryption Key
Must be exactly 64 hexadecimal characters (32 bytes). Generate with: `openssl rand -hex 32`
:::

::: info Timeouts
All timeout values are in milliseconds unless specified as seconds.
:::

## Troubleshooting

### Pool Exhaustion

**Symptom:** "Database connection pool timeout" errors

**Solution:**
```bash
export DB_POOL_SIZE=300  # Increase pool
```

### High CPU Usage

**Symptom:** CPU constantly high

**Solution:**
```bash
export STREAM_POLL_WORKER_INTERVAL=200  # Less frequent checks
export STREAM_MAX_POLL_INTERVAL=10000   # Higher backoff ceiling
```

### Slow Message Delivery

**Symptom:** Messages take long to be consumed

**Solution for regular POP (with wait):**
```bash
export POP_WAIT_INITIAL_INTERVAL_MS=50   # Faster initial query
export POP_WAIT_MAX_INTERVAL_MS=500      # Lower backoff ceiling
```

**Solution for stream/consumer group polling:**
```bash
export STREAM_POLL_WORKER_INTERVAL=50   # More responsive
export STREAM_POLL_INTERVAL=500         # Faster initial query
export STREAM_MAX_POLL_INTERVAL=2000    # Lower ceiling
```

### Database Connection Issues

**Symptom:** Can't connect to PostgreSQL

**Solution:**
```bash
export DB_CONNECTION_TIMEOUT=5000  # More time to connect
export DB_STATEMENT_TIMEOUT=60000  # More time for queries
```

### Response Queue Backlog

**Symptom:** "Response queue backlog detected" warnings in logs

**Solution:**
```bash
export RESPONSE_BATCH_SIZE=200     # Process more per tick
export RESPONSE_BATCH_MAX=1000     # Higher ceiling for bursts
export RESPONSE_TIMER_INTERVAL_MS=10  # Check more frequently
```

## See Also

- [Server Configuration](/server/configuration) - General configuration guide
- [Performance Tuning](/server/tuning) - Tuning for different workloads
- [Deployment](/server/deployment) - Production deployment patterns
- [Troubleshooting](/server/troubleshooting) - Common issues and solutions
