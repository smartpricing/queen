# Environment Variables Reference

This document lists all environment variables supported by the Queen C++ server. These variables match the configuration available in the JavaScript server for compatibility.

## Server Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `PORT` | int | 6632 | HTTP server port |
| `HOST` | string | 0.0.0.0 | HTTP server host |
| `WORKER_ID` | string | cpp-worker-1 | Unique identifier for this worker |
| `APP_NAME` | string | queen-mq | Application name |
| `NUM_WORKERS` | int | 10 | Number of worker threads (capped at CPU core count) |
| `WEBAPP_ROOT` | string | auto-detect | Path to webapp/dist directory for dashboard (auto-detects if not set) |

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

### Pool Configuration
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DB_POOL_SIZE` | int | 150 | Connection pool size |
| `DB_IDLE_TIMEOUT` | int | 30000 | Idle timeout in milliseconds |
| `DB_CONNECTION_TIMEOUT` | int | 2000 | Connection timeout in milliseconds |
| `DB_POOL_ACQUISITION_TIMEOUT` | int | 10000 | Pool acquisition timeout in milliseconds (wait time for available connection) |
| `DB_STATEMENT_TIMEOUT` | int | 30000 | Statement timeout in milliseconds |
| `DB_QUERY_TIMEOUT` | int | 30000 | Query timeout in milliseconds |
| `DB_LOCK_TIMEOUT` | int | 10000 | Lock timeout in milliseconds |
| `DB_MAX_RETRIES` | int | 3 | Maximum connection retry attempts |

## Queue Processing Configuration

### Pop Operation Defaults
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_TIMEOUT` | int | 30000 | Default timeout for pop operations (ms) |
| `MAX_TIMEOUT` | int | 60000 | Maximum allowed timeout (ms) |
| `DEFAULT_BATCH_SIZE` | int | 1 | Default batch size for pop operations |
| `BATCH_INSERT_SIZE` | int | 1000 | Batch size for bulk inserts |

### Long Polling

The system uses dedicated poll worker threads that execute pop operations directly for semantic correctness and scalability.

#### Worker Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `POLL_WORKER_COUNT` | int | 2 | Number of dedicated poll worker threads. Scale this for higher loads (2-50 workers typical). Each worker can handle ~5-10 active groups efficiently. These workers spawn their own threads and execute pop operations directly (not using a shared ThreadPool). |

**Scaling Guidelines:**
- Low load (< 20 clients): 2-5 workers
- Medium load (20-100 clients): 5-10 workers
- High load (100-200 clients): 10-20 workers
- Very high load (200+ clients): 20-50 workers

**Architecture:** Each poll worker executes pop operations directly in its own thread. This ensures **one intention = one pop = one lease**, providing proper semantic guarantees for both queue mode and consumer groups.

#### 1. Dual-Interval Rate Limiting *(Active)*

The poll workers use two separate intervals to balance responsiveness and database efficiency:

- **`POLL_WORKER_INTERVAL`**: How often workers wake up to check for new client requests (in-memory registry check, very cheap)
- **`POLL_DB_INTERVAL`**: Initial interval for database queries per queue/consumer group (aggressive first attempt)

This separation allows workers to be responsive to new clients (50ms registry checks) while starting with aggressive DB queries (100ms initial), then backing off automatically if queues are empty.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `POLL_WORKER_INTERVAL` | int | 50 | How often poll workers wake to check registry (ms) - cheap in-memory operation |
| `POLL_DB_INTERVAL` | int | 100 | Initial DB query interval (ms) - first attempt is aggressive, then backoff kicks in |

#### 2. Adaptive Exponential Backoff *(Active)*

When a queue/consumer group consistently returns empty results, the system automatically increases the query interval to reduce unnecessary database load. The interval resets immediately when messages become available.

**How it works:**
1. Track consecutive empty pop results per group
2. After `QUEUE_BACKOFF_THRESHOLD` empty results, multiply interval by `QUEUE_BACKOFF_MULTIPLIER`
3. Continue increasing up to `QUEUE_MAX_POLL_INTERVAL`
4. Reset to `POLL_DB_INTERVAL` when messages arrive

**Example:** With default settings (initial=100ms, threshold=1, multiplier=2.0):
- Query 1: 100ms (aggressive first attempt)
- Empty result 1: Increase to 200ms (backoff threshold reached)
- Empty result 2: Increase to 400ms
- Empty result 3: Increase to 800ms
- Empty result 4: Increase to 1600ms
- Empty result 5+: Stay at 2000ms (capped at max)
- Messages arrive: Reset to 100ms immediately

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEUE_BACKOFF_THRESHOLD` | int | 1 | Number of consecutive empty pops before backoff starts (1 = immediate backoff) |
| `QUEUE_BACKOFF_MULTIPLIER` | double | 2.0 | Exponential backoff multiplier (interval *= multiplier) |
| `QUEUE_MAX_POLL_INTERVAL` | int | 2000 | Maximum poll interval after backoff (ms) - caps the exponential backoff growth |
| `QUEUE_BACKOFF_CLEANUP_THRESHOLD` | int | 3600 | Cleanup inactive backoff state entries after N seconds (reduces memory overhead) |

#### Legacy Variables *(Not Currently Used)*

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEUE_POLL_INTERVAL` | int | 100 | Reserved for future use |
| `QUEUE_POLL_INTERVAL_FILTERED` | int | 50 | Reserved for future use |

#### 3. Stream Long Polling *(Active)*

Stream processing uses dedicated poll workers similar to regular queue long polling, but optimized for windowed message consumption. Stream workers can submit concurrent window check operations to the ThreadPool. These workers handle `/api/v1/stream/poll` requests with configurable backoff.

**ThreadPool Allocation:** The DB ThreadPool is shared by all long-polling workers and services:
```
DB ThreadPool Size = P + S + (S × C) + T
Where:
  P = POLL_WORKER_COUNT (reserved threads for regular poll workers)
  S = STREAM_POLL_WORKER_COUNT (reserved threads for stream poll workers)
  C = STREAM_CONCURRENT_CHECKS (max concurrent window checks per stream worker)
  T = DB_THREAD_POOL_SERVICE_THREADS (background service DB operations)

Example: 2 poll + 2 stream + (2 × 10) + 5 = 29 threads
```

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `STREAM_POLL_WORKER_COUNT` | int | 2 | Number of dedicated stream poll worker threads. Scale based on number of active stream consumers. Each worker reserves 1 ThreadPool thread for its main loop. |
| `STREAM_CONCURRENT_CHECKS` | int | 10 | Maximum concurrent window check jobs per stream worker. Higher values allow checking more stream groups in parallel but use more threads. |
| `DB_THREAD_POOL_SERVICE_THREADS` | int | 5 | Number of ThreadPool threads reserved for background service DB operations (MetricsCollector writes, etc.). |
| `STREAM_POLL_WORKER_INTERVAL` | int | 100 | How often stream workers check registry for new requests (ms) - in-memory operation |
| `STREAM_POLL_INTERVAL` | int | 1000 | Minimum time between stream checks per consumer group (ms) - controls DB query frequency |
| `STREAM_BACKOFF_THRESHOLD` | int | 5 | Number of consecutive empty checks before exponential backoff starts |
| `STREAM_BACKOFF_MULTIPLIER` | double | 2.0 | Exponential backoff multiplier for stream poll interval |
| `STREAM_MAX_POLL_INTERVAL` | int | 5000 | Maximum poll interval after backoff (ms) - caps exponential growth |

**Stream Scaling Guidelines:**
- Low load (< 10 stream consumers): 2 workers, 10 concurrent checks
- Medium load (10-50 consumers): 4 workers, 15 concurrent checks
- High load (50+ consumers): 8 workers, 20 concurrent checks

**Important:** Stream workers check groups sequentially and submit ONE window check job per group to the ThreadPool. The `STREAM_CONCURRENT_CHECKS` setting controls the maximum concurrent window checks per worker. All stream workers combined can have `STREAM_POLL_WORKER_COUNT × STREAM_CONCURRENT_CHECKS` concurrent checks in flight.

**Resource Management:** All poll workers (both regular and stream) run as never-returning jobs in the DB ThreadPool. This provides:
- Centralized thread management
- Visibility into thread usage
- Proper resource limits
- Clean shutdown capabilities

### Partition Selection
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `MAX_PARTITION_CANDIDATES` | int | 100 | Number of candidate partitions for lease acquisition |

### Response Queue & Batch Processing
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `RESPONSE_TIMER_INTERVAL_MS` | int | 25 | Response queue timer polling interval (ms). Controls how frequently the response queue is checked for ready messages. Lower values reduce latency but increase CPU usage. |
| `RESPONSE_BATCH_SIZE` | int | 100 | Base number of responses to process per timer tick. Automatically scales up when backlog is detected. |
| `RESPONSE_BATCH_MAX` | int | 500 | Maximum responses to process per timer tick, even under heavy backlog. Prevents event loop blocking. |

### Batch Push - Size-Based Dynamic Batching (ACTIVE)

Queen now supports intelligent size-based batching that dynamically calculates row sizes and batches messages to optimize PostgreSQL write performance. This replaces the legacy count-based approach with a smarter strategy that accounts for variable message payload sizes.

**How it works:**
- Estimates the total row size for each message (including payload, indexes, and PostgreSQL overhead)
- Dynamically accumulates messages until reaching optimal batch size (in MB)
- Ensures batches stay within PostgreSQL's sweet spot of 2-8 MB per transaction
- Falls back to legacy count-based batching if disabled

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `BATCH_PUSH_USE_SIZE_BASED` | bool | true | Enable size-based dynamic batching. Set to `false` to use legacy count-based batching. |
| `BATCH_PUSH_TARGET_SIZE_MB` | int | 4 | Target batch size in MB. System flushes batches when this size is reached (if min messages met). Sweet spot: 4-6 MB. |
| `BATCH_PUSH_MIN_SIZE_MB` | int | 2 | Minimum batch size in MB. Ensures batches are large enough to benefit from batching. |
| `BATCH_PUSH_MAX_SIZE_MB` | int | 8 | Maximum batch size in MB. Hard limit to prevent excessive memory usage and long transactions. |
| `BATCH_PUSH_MIN_MESSAGES` | int | 100 | Minimum messages per batch. Ensures batches have enough messages even if size not reached. |
| `BATCH_PUSH_MAX_MESSAGES` | int | 10000 | Maximum messages per batch. Hard limit on message count even if under size limit. |
| `BATCH_PUSH_CHUNK_SIZE` | int | 1000 | **LEGACY:** Count-based chunk size (used only if `BATCH_PUSH_USE_SIZE_BASED=false`). |

**Performance Benefits:**
- **Consistent throughput:** Batches are sized optimally regardless of message payload variance
- **Better memory usage:** Prevents under-batching (small messages) and over-batching (large messages)
- **PostgreSQL optimization:** Targets the 2-8 MB sweet spot for transaction commit performance
- **Adaptive:** Automatically adjusts to message size distribution

**Recommended Settings by Workload:**

| Workload Type | TARGET_SIZE_MB | MIN_SIZE_MB | MAX_SIZE_MB | MIN_MESSAGES | MAX_MESSAGES |
|---------------|----------------|-------------|-------------|--------------|--------------|
| Small messages (<1KB) | 6 | 3 | 10 | 500 | 20000 |
| Medium messages (1-10KB) | 4 | 2 | 8 | 100 | 10000 |
| Large messages (>10KB) | 3 | 2 | 6 | 50 | 5000 |
| Mixed workload | 4 | 2 | 8 | 100 | 10000 |

### Queue Defaults
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_LEASE_TIME` | int | 300 | Default lease time in seconds |
| `DEFAULT_RETRY_LIMIT` | int | 3 | Default retry limit |
| `DEFAULT_RETRY_DELAY` | int | 1000 | Default retry delay (ms) |
| `DEFAULT_MAX_SIZE` | int | 10000 | Default maximum queue size |
| `DEFAULT_TTL` | int | 3600 | Default message TTL in seconds |
| `DEFAULT_PRIORITY` | int | 0 | Default message priority |
| `DEFAULT_DELAYED_PROCESSING` | int | 0 | Default delayed processing time |
| `DEFAULT_WINDOW_BUFFER` | int | 0 | Default window buffer size |

### Dead Letter Queue
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_DLQ_ENABLED` | bool | false | Enable DLQ by default |
| `DEFAULT_DLQ_AFTER_MAX_RETRIES` | bool | false | Move to DLQ after max retries |

### Retention
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_RETENTION_SECONDS` | int | 0 | Default retention period (seconds) |
| `DEFAULT_COMPLETED_RETENTION_SECONDS` | int | 0 | Retention for completed messages (seconds) |
| `DEFAULT_RETENTION_ENABLED` | bool | false | Enable retention by default |

### Eviction
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_MAX_WAIT_TIME_SECONDS` | int | 0 | Maximum wait time before eviction (seconds) |

### Consumer Group Subscription
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_SUBSCRIPTION_MODE` | string | "" | Default subscription mode for new consumer groups. Options: `""` (all messages), `"new"` (skip history), `"new-only"` (same as "new"). **Note:** Only applies when client doesn't explicitly specify `.subscriptionMode()`. Existing consumer groups are not affected. |

**Use Cases for `DEFAULT_SUBSCRIPTION_MODE="new"`:**
- Prevent accidental processing of historical messages by new consumer groups
- Real-time systems where only new messages matter
- Development/staging environments to avoid backlog processing
- Microservices that should only process events after deployment

**Example:**
```bash
# Make all new consumer groups skip historical messages by default
export DEFAULT_SUBSCRIPTION_MODE="new"
./bin/queen-server

# Client can still override if needed
# .subscriptionMode('all')  // Force process all messages
```

## System Events Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_SYSTEM_EVENTS_ENABLED` | bool | false | Enable system event propagation |
| `QUEEN_SYSTEM_EVENTS_BATCH_MS` | int | 10 | Batching window for event publishing (ms) |
| `QUEEN_SYSTEM_EVENTS_SYNC_TIMEOUT` | int | 30000 | Startup synchronization timeout (ms) |

## Background Jobs Configuration

### Lease Reclamation
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `LEASE_RECLAIM_INTERVAL` | int | 5000 | Lease reclamation interval (ms) |

### Metrics Collector
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `METRICS_SAMPLE_INTERVAL_MS` | int | 1000 | How often to sample system metrics (CPU, memory, queue depth) in milliseconds |
| `METRICS_AGGREGATE_INTERVAL_S` | int | 60 | How often to aggregate sampled metrics and write to database (seconds) |

**How it works:**
- Samples metrics every second (in-memory, low overhead)
- Aggregates 60 samples into averages/min/max
- Writes aggregated data to database every 60 seconds (reduces DB writes)

### Retention Service
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `RETENTION_INTERVAL` | int | 300000 | Retention service interval (ms) |
| `RETENTION_BATCH_SIZE` | int | 1000 | Retention batch size |
| `PARTITION_CLEANUP_DAYS` | int | 7 | Days before partition cleanup |
| `METRICS_RETENTION_DAYS` | int | 90 | Days to keep metrics data |

### Eviction Service
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `EVICTION_INTERVAL` | int | 60000 | Eviction service interval (ms) |
| `EVICTION_BATCH_SIZE` | int | 1000 | Eviction batch size |

### WebSocket Updates
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEUE_DEPTH_UPDATE_INTERVAL` | int | 5000 | Queue depth update interval (ms) |
| `SYSTEM_STATS_UPDATE_INTERVAL` | int | 10000 | System stats update interval (ms) |

## WebSocket Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `WS_COMPRESSION` | int | 0 | WebSocket compression level |
| `WS_MAX_PAYLOAD_LENGTH` | int | 16384 | Maximum payload length (bytes) |
| `WS_IDLE_TIMEOUT` | int | 60 | Idle timeout (seconds) |
| `WS_MAX_CONNECTIONS` | int | 1000 | Maximum concurrent connections |
| `WS_HEARTBEAT_INTERVAL` | int | 30000 | Heartbeat interval (ms) |

## Inter-Instance Communication (Peer Notifications)

Queen provides real-time notifications for faster poll worker response times:

1. **Local notifications** (always active): When a message is pushed or acknowledged, the server's own poll workers are immediately notified to reset their backoff timers.

2. **Peer notifications** (when `QUEEN_PEERS` is set): In clustered deployments, servers notify each other via WebSocket so poll workers on all instances respond immediately.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_PEERS` | string | "" | Comma-separated peer URLs (e.g., `http://queen2:6632,http://queen3:6632`). When set, enables HTTP peer notifications. |
| `PEER_NOTIFY_BATCH_MS` | int | 10 | Batch notifications for N ms before sending (0 = immediate) |

**How it works:**

Queen servers notify (locally and via HTTP POST to peers) when:
- A new message is pushed (MESSAGE_AVAILABLE notification)
- A message is acknowledged (PARTITION_FREE notification)

This allows poll workers to immediately reset their backoff timers and query the database, instead of waiting for their exponential backoff interval (up to 2000ms).

**Single server (default):**
```bash
# Local poll worker notification is automatic - no config needed
./bin/queen-server
```

**Cluster setup:**
```bash
# Server A - just define peers
export QUEEN_PEERS="http://queen-b:6632,http://queen-c:6632"
./bin/queen-server

# Server B
export QUEEN_PEERS="http://queen-a:6632,http://queen-c:6632"
./bin/queen-server

# Server C
export QUEEN_PEERS="http://queen-a:6632,http://queen-b:6632"
./bin/queen-server
```

**Endpoints:**
- `WS /internal/ws/peer` - WebSocket endpoint for peer connections
- `POST /internal/api/notify` - HTTP fallback for notifications
- `GET /internal/api/inter-instance/stats` - Peer notification statistics
- Stats also included in `/health` response

## Encryption Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_ENCRYPTION_KEY` | string | - | Encryption key (64 hex characters for AES-256) |

**Note:** Encryption uses AES-256-GCM algorithm with 32-byte keys and 16-byte IVs. These are constants and cannot be changed via environment variables.

## Client SDK Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_BASE_URL` | string | http://localhost:6632 | Default base URL for client SDK |
| `CLIENT_RETRY_ATTEMPTS` | int | 3 | Default retry attempts |
| `CLIENT_RETRY_DELAY` | int | 1000 | Default retry delay (ms) |
| `CLIENT_RETRY_BACKOFF` | double | 2.0 | Retry backoff multiplier |
| `CLIENT_POOL_SIZE` | int | 10 | Connection pool size |
| `CLIENT_REQUEST_TIMEOUT` | int | 30000 | Request timeout (ms) |

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
| `LOG_LEVEL` | string | info | Log level (trace, debug, info, warn, error, critical, off) |
| `LOG_FORMAT` | string | json | Log format (json, text) |
| `LOG_TIMESTAMP` | bool | true | Include timestamps in logs |

## File Buffer Configuration (QoS 0 & Failover)

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `FILE_BUFFER_DIR` | string | Platform-specific* | Directory for file buffers |
| `FILE_BUFFER_FLUSH_MS` | int | 100 | How often to scan for complete buffer files (milliseconds) |
| `FILE_BUFFER_MAX_BATCH` | int | 100 | Maximum events per database transaction |
| `FILE_BUFFER_EVENTS_PER_FILE` | int | 10000 | Create new buffer file after N events |

**Platform-specific defaults:**
- **macOS**: `/tmp/queen`
- **Linux**: `/var/lib/queen/buffers`

The file buffer serves dual purposes:
1. **QoS 0 batching** - Batch events for 10-100x performance improvement (client: `{ buffer: true }`)
2. **PostgreSQL failover** - Buffer messages when database is unavailable (automatic, zero message loss)

### How It Works

**Buffer File Lifecycle:**
```
1. Client pushes with { buffer: true }
2. Event written to: qos0_<uuid>.buf.tmp (active file)
3. File finalized when EITHER:
   - Reaches 10,000 events (high throughput), OR
   - 200ms passes with no new writes (low throughput/end of burst)
4. Atomic rename: .tmp → .buf
5. Background processor picks up .buf files every 100ms
6. Processes in batches of 100 events per DB transaction
7. Deletes file when complete
```

**Benefits:**
- ✅ **No rotation conflicts** - Each file is independent
- ✅ **Crash-safe** - .tmp files cleaned up on startup
- ✅ **Scalable** - Handles millions of events (100 files of 10,000 each)
- ✅ **Low-latency** - Time-based finalization ensures fast processing for small bursts
- ✅ **Clear progress** - Can see pending files in directory

**Example:**
```bash
# Use custom directory
FILE_BUFFER_DIR=/data/queen/buffers ./bin/queen-server

# High-throughput configuration
FILE_BUFFER_FLUSH_MS=50          # Scan more frequently
FILE_BUFFER_MAX_BATCH=1000       # Larger DB batches
FILE_BUFFER_EVENTS_PER_FILE=50000  # Larger buffer files

# Low-latency configuration
FILE_BUFFER_FLUSH_MS=10          # Very fast scanning
FILE_BUFFER_MAX_BATCH=50         # Smaller batches for lower latency
FILE_BUFFER_EVENTS_PER_FILE=1000   # Smaller files, faster rotation
```

### File Naming

Buffer files use UUIDv7 (time-sortable) for guaranteed ordering:
- `qos0_019a0b66-3920-7000-b252.buf.tmp` - Being written
- `qos0_019a0b66-3920-7000-b252.buf` - Complete, ready to process
- `failover_019a0b66-3921-7001-c123.buf` - Failover file (DB was down)

## Usage Examples

### Development Environment
```bash
export PORT=6632
export PG_HOST=localhost
export PG_USER=postgres
export PG_PASSWORD=postgres
export PG_DB=queen_dev
export LOG_LEVEL=debug
```


### Production Environment
```bash
export PORT=6632
export HOST=0.0.0.0
export PG_HOST=db.production.example.com
export PG_USER=queen_user
export PG_PASSWORD=secure_password
export PG_DB=queen_production
export PG_USE_SSL=true
export DB_POOL_SIZE=200
export LOG_LEVEL=info
export LOG_FORMAT=json
export QUEEN_ENCRYPTION_KEY=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
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
export LOG_LEVEL=info
export DEFAULT_SUBSCRIPTION_MODE="new"  # New consumer groups skip historical messages
```

### High-Throughput Configuration
```bash
export DB_POOL_SIZE=300
export BATCH_INSERT_SIZE=2000
export QUEUE_POLL_INTERVAL=50
export MAX_PARTITION_CANDIDATES=200
export RETENTION_BATCH_SIZE=2000
export EVICTION_BATCH_SIZE=2000
```

## Notes

- **Boolean values**: Set to `"true"` to enable, any other value (including unset) is treated as false
- **Integer values**: Must be valid integers, invalid values will fall back to defaults
- **String values**: Used as-is without validation unless specified otherwise
- **Encryption key**: Must be exactly 64 hexadecimal characters (32 bytes)
- All timeout values are in milliseconds unless specified as seconds


