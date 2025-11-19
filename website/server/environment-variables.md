# Environment Variables Reference

Complete configuration reference for Queen MQ server. These variables control all aspects of server behavior.

## Quick Reference

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | 6632 | HTTP server port |
| `NUM_WORKERS` | 10 | Worker threads (max: CPU cores) |
| `DB_POOL_SIZE` | 150 | **CRITICAL** - Connection pool size |
| `PG_HOST` | localhost | PostgreSQL host |
| `LOG_LEVEL` | info | Log level (debug, info, warn, error) |

## Server Configuration

### Basic Settings

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `PORT` | int | 6632 | HTTP server port |
| `HOST` | string | 0.0.0.0 | HTTP server host |
| `WORKER_ID` | string | cpp-worker-1 | Unique identifier for this worker |
| `APP_NAME` | string | queen-mq | Application name |
| `NUM_WORKERS` | int | 10 | Number of worker threads (capped at CPU core count) |
| `WEBAPP_ROOT` | string | auto-detect | Path to webapp/dist directory for dashboard |

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
| `DB_POOL_ACQUISITION_TIMEOUT` | int | 10000 | Pool acquisition timeout (wait for available connection) |
| `DB_STATEMENT_TIMEOUT` | int | 30000 | Statement timeout in milliseconds |
| `DB_QUERY_TIMEOUT` | int | 30000 | Query timeout in milliseconds |
| `DB_LOCK_TIMEOUT` | int | 10000 | Lock timeout in milliseconds |
| `DB_MAX_RETRIES` | int | 3 | Maximum connection retry attempts |

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

# Very high load
export DB_POOL_SIZE=500
export NUM_WORKERS=30
```

## Queue Processing

### Pop Operation Defaults

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_TIMEOUT` | int | 30000 | Default timeout for pop operations (ms) |
| `MAX_TIMEOUT` | int | 60000 | Maximum allowed timeout (ms) |
| `DEFAULT_BATCH_SIZE` | int | 1 | Default batch size for pop operations |
| `BATCH_INSERT_SIZE` | int | 1000 | Batch size for bulk inserts |

### Long Polling Configuration

The system uses dedicated poll worker threads for long-polling operations.

#### Worker Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `POLL_WORKER_COUNT` | int | 2 | Number of dedicated poll worker threads |

**Scaling Guidelines:**
- Low load (< 20 clients): 2-5 workers
- Medium load (20-100 clients): 5-10 workers
- High load (100-200 clients): 10-20 workers
- Very high load (200+ clients): 20-50 workers

Each poll worker can efficiently handle ~5-10 active consumer groups.

#### Polling Intervals

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `POLL_WORKER_INTERVAL` | int | 50 | How often workers check for new client requests (ms) - in-memory operation |
| `POLL_DB_INTERVAL` | int | 100 | Initial DB query interval (ms) - aggressive first attempt |

**How it works:**
1. Workers check registry every 50ms (cheap in-memory check)
2. First DB query at 100ms (aggressive)
3. If no messages, exponential backoff kicks in

#### Adaptive Exponential Backoff

When queues are consistently empty, the system automatically backs off to reduce DB load:

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEUE_BACKOFF_THRESHOLD` | int | 1 | Consecutive empty pops before backoff (1 = immediate) |
| `QUEUE_BACKOFF_MULTIPLIER` | double | 2.0 | Exponential backoff multiplier |
| `QUEUE_MAX_POLL_INTERVAL` | int | 2000 | Maximum poll interval after backoff (ms) |
| `QUEUE_BACKOFF_CLEANUP_THRESHOLD` | int | 3600 | Cleanup inactive backoff state entries after N seconds |

**Example backoff sequence (defaults):**
```
Query 1: 100ms  (aggressive first attempt)
Empty 1: 200ms  (backoff starts)
Empty 2: 400ms
Empty 3: 800ms
Empty 4: 1600ms
Empty 5+: 2000ms (capped)
Messages arrive: Reset to 100ms immediately
```

**Configuration Examples:**
```bash
# Low latency (higher CPU usage)
export POLL_WORKER_INTERVAL=25
export POLL_DB_INTERVAL=50
export QUEUE_MAX_POLL_INTERVAL=1000

# Balanced (default)
export POLL_WORKER_INTERVAL=50
export POLL_DB_INTERVAL=100
export QUEUE_MAX_POLL_INTERVAL=2000

# Conservative (lower CPU, higher latency)
export POLL_WORKER_INTERVAL=100
export POLL_DB_INTERVAL=200
export QUEUE_MAX_POLL_INTERVAL=5000
```

### Partition Selection

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `MAX_PARTITION_CANDIDATES` | int | 100 | Candidate partitions for lease acquisition |

**Tuning:**
- Low partition count (<50): Keep at 100
- High partition count (>200): Increase to 200-500
- Very high (>1000): Increase to 1000

### Batch Push - Size-Based Dynamic Batching

Queen uses intelligent size-based batching that dynamically calculates row sizes and optimizes for PostgreSQL performance.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `BATCH_PUSH_USE_SIZE_BASED` | bool | true | Enable size-based dynamic batching |
| `BATCH_PUSH_TARGET_SIZE_MB` | int | 4 | Target batch size in MB (sweet spot: 4-6 MB) |
| `BATCH_PUSH_MIN_SIZE_MB` | int | 2 | Minimum batch size in MB |
| `BATCH_PUSH_MAX_SIZE_MB` | int | 8 | Maximum batch size in MB (hard limit) |
| `BATCH_PUSH_MIN_MESSAGES` | int | 100 | Minimum messages per batch |
| `BATCH_PUSH_MAX_MESSAGES` | int | 10000 | Maximum messages per batch |
| `BATCH_PUSH_CHUNK_SIZE` | int | 1000 | **LEGACY**: Count-based chunk size (if size-based disabled) |

**How it works:**
- Estimates total row size for each message (payload + indexes + overhead)
- Dynamically accumulates messages until reaching optimal batch size
- Ensures batches stay within PostgreSQL's sweet spot of 2-8 MB
- Falls back to legacy count-based batching if disabled

**Recommended Settings by Workload:**

| Workload | TARGET_SIZE_MB | MIN_SIZE_MB | MAX_SIZE_MB | MIN_MESSAGES | MAX_MESSAGES |
|----------|----------------|-------------|-------------|--------------|--------------|
| Small messages (<1KB) | 6 | 3 | 10 | 500 | 20000 |
| Medium messages (1-10KB) | 4 | 2 | 8 | 100 | 10000 |
| Large messages (>10KB) | 3 | 2 | 6 | 50 | 5000 |
| Mixed workload | 4 | 2 | 8 | 100 | 10000 |

### Response Queue

Response processing uses an adaptive batching system that automatically scales up under load to prevent backlog buildup.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `RESPONSE_TIMER_INTERVAL_MS` | int | 25 | Response queue polling interval (ms) |
| `RESPONSE_BATCH_SIZE` | int | 100 | Base number of responses to process per timer tick |
| `RESPONSE_BATCH_MAX` | int | 500 | Maximum responses per tick under heavy backlog |

**How Adaptive Batching Works:**

Under normal load, the system processes `RESPONSE_BATCH_SIZE` responses every timer tick. When backlog is detected, it automatically scales up:

```
Normal (< 100 items):     100 responses per tick
Moderate (200 items):     200 responses per tick (2x)
High (300 items):         300 responses per tick (3x)
Heavy (500+ items):       500 responses per tick (capped at max)
```

The system logs warnings when backlog exceeds 2x the base batch size.

**Throughput Calculations:**

At default settings (25ms interval):
- Normal: 4,000 responses/sec per worker
- Maximum: 20,000 responses/sec per worker

**Configuration Examples:**

```bash
# Default balanced configuration
export RESPONSE_TIMER_INTERVAL_MS=25
export RESPONSE_BATCH_SIZE=100
export RESPONSE_BATCH_MAX=500

# Low latency (more responsive, higher CPU)
export RESPONSE_TIMER_INTERVAL_MS=10
export RESPONSE_BATCH_SIZE=50
export RESPONSE_BATCH_MAX=200

# High throughput (handles large backlogs)
export RESPONSE_TIMER_INTERVAL_MS=25
export RESPONSE_BATCH_SIZE=200
export RESPONSE_BATCH_MAX=1000

# Conservative (lower CPU, can backlog under load)
export RESPONSE_TIMER_INTERVAL_MS=50
export RESPONSE_BATCH_SIZE=50
export RESPONSE_BATCH_MAX=200
```

::: tip Tuning Guidelines
- Increase `RESPONSE_BATCH_SIZE` if you frequently see backlog warnings
- Increase `RESPONSE_BATCH_MAX` for burst traffic handling
- Lower `RESPONSE_TIMER_INTERVAL_MS` for lower latency (increases CPU)
- The system automatically adapts between base and max based on queue depth
:::

## Queue Defaults

### Basic Defaults

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_LEASE_TIME` | int | 300 | Default lease time in seconds (5 minutes) |
| `DEFAULT_RETRY_LIMIT` | int | 3 | Default retry limit |
| `DEFAULT_RETRY_DELAY` | int | 1000 | Default retry delay (ms) |
| `DEFAULT_MAX_SIZE` | int | 10000 | Default maximum queue size |
| `DEFAULT_TTL` | int | 3600 | Default message TTL in seconds |
| `DEFAULT_PRIORITY` | int | 0 | Default message priority |
| `DEFAULT_DELAYED_PROCESSING` | int | 0 | Default delayed processing time (seconds) |
| `DEFAULT_WINDOW_BUFFER` | int | 0 | Default window buffer size (seconds) |

### Dead Letter Queue

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_DLQ_ENABLED` | bool | false | Enable DLQ by default |
| `DEFAULT_DLQ_AFTER_MAX_RETRIES` | bool | false | Auto-move to DLQ after max retries |

### Retention

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_RETENTION_SECONDS` | int | 0 | Default retention period (0 = disabled) |
| `DEFAULT_COMPLETED_RETENTION_SECONDS` | int | 0 | Retention for completed messages (0 = disabled) |
| `DEFAULT_RETENTION_ENABLED` | bool | false | Enable retention by default |

### Eviction

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_MAX_WAIT_TIME_SECONDS` | int | 0 | Maximum wait time before eviction (0 = disabled) |

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

### Lease Reclamation

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `LEASE_RECLAIM_INTERVAL` | int | 5000 | Lease reclamation interval (ms) |

### Retention Service

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `RETENTION_INTERVAL` | int | 300000 | Retention service interval (ms) - 5 minutes |
| `RETENTION_BATCH_SIZE` | int | 1000 | Retention batch size |
| `PARTITION_CLEANUP_DAYS` | int | 7 | Days before partition cleanup |
| `METRICS_RETENTION_DAYS` | int | 90 | Days to keep metrics data |

### Eviction Service

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `EVICTION_INTERVAL` | int | 60000 | Eviction service interval (ms) - 1 minute |
| `EVICTION_BATCH_SIZE` | int | 1000 | Eviction batch size |

### WebSocket Updates

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEUE_DEPTH_UPDATE_INTERVAL` | int | 5000 | Queue depth update interval (ms) |
| `SYSTEM_STATS_UPDATE_INTERVAL` | int | 10000 | System stats update interval (ms) |

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

## API Configuration

### General

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `MAX_BODY_SIZE` | int | 104857600 | Maximum request body size (bytes, default 100MB) |

### Pagination

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `API_DEFAULT_LIMIT` | int | 100 | Default pagination limit |
| `API_MAX_LIMIT` | int | 1000 | Maximum pagination limit |
| `API_DEFAULT_OFFSET` | int | 0 | Default pagination offset |

### CORS

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `CORS_MAX_AGE` | int | 86400 | CORS preflight cache duration (seconds) |
| `CORS_ALLOWED_ORIGINS` | string | * | Allowed CORS origins |
| `CORS_ALLOWED_METHODS` | string | GET, POST, PUT, DELETE, OPTIONS | Allowed HTTP methods |
| `CORS_ALLOWED_HEADERS` | string | Content-Type, Authorization | Allowed request headers |

## Analytics Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `ANALYTICS_RECENT_HOURS` | int | 24 | Hours for recent completion calculations |
| `ANALYTICS_MIN_COMPLETED` | int | 5 | Minimum completed messages for stats |
| `RECENT_MESSAGE_WINDOW` | int | 60 | Recent message window (seconds) |
| `RELATED_MESSAGE_WINDOW` | int | 3600 | Related message window (seconds) |
| `MAX_RELATED_MESSAGES` | int | 10 | Maximum related messages to return |

## Monitoring Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `ENABLE_REQUEST_COUNTING` | bool | true | Enable request counting |
| `ENABLE_MESSAGE_COUNTING` | bool | true | Enable message counting |
| `METRICS_ENDPOINT_ENABLED` | bool | true | Enable /metrics endpoint |
| `HEALTH_CHECK_ENABLED` | bool | true | Enable /health endpoint |

## Logging Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `ENABLE_LOGGING` | bool | true | Enable logging |
| `LOG_LEVEL` | string | info | Log level |
| `LOG_FORMAT` | string | json | Log format (json, text) |
| `LOG_TIMESTAMP` | bool | true | Include timestamps in logs |

**Log Levels:**
- `trace` - Extremely verbose (development only)
- `debug` - Detailed debugging information
- `info` - General information (recommended for production)
- `warn` - Warnings only
- `error` - Errors only
- `critical` - Critical errors only
- `off` - Disable logging

**Examples:**
```bash
# Development
export LOG_LEVEL=debug
export LOG_FORMAT=text

# Production
export LOG_LEVEL=info
export LOG_FORMAT=json
```

## System Events Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_SYSTEM_EVENTS_ENABLED` | bool | false | Enable system event propagation |
| `QUEEN_SYSTEM_EVENTS_BATCH_MS` | int | 10 | Batching window for event publishing (ms) |
| `QUEEN_SYSTEM_EVENTS_SYNC_TIMEOUT` | int | 30000 | Startup synchronization timeout (ms) |

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
export LOG_FORMAT=text
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
export LOG_FORMAT=json
export QUEEN_ENCRYPTION_KEY=your_64_char_hex_key_here
export FILE_BUFFER_DIR=/var/lib/queen/buffers
```

### High-Throughput Configuration

```bash
export DB_POOL_SIZE=500
export NUM_WORKERS=30
export BATCH_INSERT_SIZE=2000
export BATCH_PUSH_TARGET_SIZE_MB=6
export BATCH_PUSH_MAX_SIZE_MB=10
export POLL_WORKER_INTERVAL=25
export POLL_DB_INTERVAL=50
export MAX_PARTITION_CANDIDATES=500
export RESPONSE_TIMER_INTERVAL_MS=25
export RESPONSE_BATCH_SIZE=200
export RESPONSE_BATCH_MAX=1000
export RETENTION_BATCH_SIZE=2000
export EVICTION_BATCH_SIZE=2000
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
export POLL_WORKER_INTERVAL=25
export POLL_DB_INTERVAL=50
export QUEUE_MAX_POLL_INTERVAL=1000
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
export DB_POOL_ACQUISITION_TIMEOUT=20000  # More patience
```

### High CPU Usage

**Symptom:** CPU constantly high

**Solution:**
```bash
export POLL_WORKER_INTERVAL=100  # Reduce registry checks
export POLL_DB_INTERVAL=200  # Less aggressive polling
export QUEUE_MAX_POLL_INTERVAL=5000  # Higher backoff ceiling
```

### Slow Message Delivery

**Symptom:** Messages take long to be consumed

**Solution:**
```bash
export POLL_WORKER_INTERVAL=25  # More responsive
export POLL_DB_INTERVAL=50  # Faster initial query
export QUEUE_MAX_POLL_INTERVAL=1000  # Lower ceiling
```

### Database Connection Issues

**Symptom:** Can't connect to PostgreSQL

**Solution:**
```bash
export DB_CONNECTION_TIMEOUT=5000  # More time to connect
export DB_STATEMENT_TIMEOUT=10000  # More time for queries
export DB_MAX_RETRIES=5  # More retry attempts
```

### Response Queue Backlog

**Symptom:** "Response queue backlog detected" warnings in logs

**Solution:**
```bash
export RESPONSE_BATCH_SIZE=200  # Process more per tick
export RESPONSE_BATCH_MAX=1000  # Higher ceiling for bursts
export RESPONSE_TIMER_INTERVAL_MS=10  # Check more frequently (higher CPU)
```

## See Also

- [Server Configuration](/server/configuration) - General configuration guide
- [Performance Tuning](/server/tuning) - Tuning for different workloads
- [Deployment](/server/deployment) - Production deployment patterns
- [Troubleshooting](/server/troubleshooting) - Common issues and solutions

