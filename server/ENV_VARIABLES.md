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
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEUE_POLL_INTERVAL` | int | 100 | Initial poll interval (ms) |
| `QUEUE_POLL_INTERVAL_FILTERED` | int | 50 | Poll interval for filtered consumers (ms) |
| `QUEUE_MAX_POLL_INTERVAL` | int | 2000 | Maximum poll interval after backoff (ms) |
| `QUEUE_BACKOFF_THRESHOLD` | int | 5 | Empty polls before backoff starts |
| `QUEUE_BACKOFF_MULTIPLIER` | double | 2.0 | Exponential backoff multiplier |

### Partition Selection
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `MAX_PARTITION_CANDIDATES` | int | 100 | Number of candidate partitions for lease acquisition |

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
- The C++ server now has **full parity** with the JavaScript server's configuration options

