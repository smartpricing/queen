# Environment Variables Reference

This document lists all environment variables supported by the Queen C++ server.

## Server Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `PORT` | int | 6632 | HTTP server port |
| `HOST` | string | 0.0.0.0 | HTTP server host |
| `WORKER_ID` | string | cpp-worker-1 | Unique identifier for this worker |
| `NUM_WORKERS` | int | 10 | Number of worker threads |

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
| `DB_STATEMENT_TIMEOUT` | int | 30000 | Statement timeout in milliseconds |
| `DB_LOCK_TIMEOUT` | int | 10000 | Lock timeout in milliseconds |

## Queue Processing Configuration

### Pop Operation Defaults
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_TIMEOUT` | int | 30000 | Default timeout for pop operations (ms) |
| `DEFAULT_BATCH_SIZE` | int | 1 | Default batch size for pop operations |

### ThreadPool Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DB_THREAD_POOL_SERVICE_THREADS` | int | 5 | Threads for background service DB operations |
| `QUEUE_BACKOFF_CLEANUP_THRESHOLD` | int | 3600 | Cleanup inactive backoff state entries after N seconds |

### POP_WAIT Backoff (Sidecar Long-Polling)

These settings control the backoff behavior for sidecar POP_WAIT (long-polling) requests via SharedStateManager.

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

### Response Queue & Batch Processing
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `RESPONSE_TIMER_INTERVAL_MS` | int | 25 | Response queue timer polling interval (ms) |
| `RESPONSE_BATCH_SIZE` | int | 100 | Base number of responses to process per timer tick |
| `RESPONSE_BATCH_MAX` | int | 500 | Maximum responses per tick even under backlog |

### Sidecar Pool Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `SIDECAR_POOL_SIZE` | int | 50 | Number of connections in sidecar pool |
| `SIDECAR_MICRO_BATCH_WAIT_MS` | int | 5 | Target cycle time for micro-batching (ms) |
| `SIDECAR_MAX_ITEMS_PER_TX` | int | 1000 | Max items per database transaction |
| `SIDECAR_MAX_BATCH_SIZE` | int | 1000 | Max requests per micro-batch |
| `SIDECAR_MAX_PENDING_COUNT` | int | 50 | Max pending requests before forcing immediate send |

### Consumer Group Subscription
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `DEFAULT_SUBSCRIPTION_MODE` | string | "" | Default subscription mode for new consumer groups. Options: `""` (all messages), `"new"` (skip history), `"new-only"` (same as "new") |

## Background Jobs Configuration

### Metrics Collector
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `METRICS_SAMPLE_INTERVAL_MS` | int | 1000 | How often to sample system metrics (ms) |
| `METRICS_AGGREGATE_INTERVAL_S` | int | 60 | How often to aggregate and write to database (seconds) |

### Retention Service
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `RETENTION_INTERVAL` | int | 300000 | Retention service interval (ms) |
| `RETENTION_BATCH_SIZE` | int | 1000 | Retention batch size |
| `PARTITION_CLEANUP_DAYS` | int | 30 | Days before partition cleanup |
| `METRICS_RETENTION_DAYS` | int | 90 | Days to keep metrics data |

### Eviction Service
| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `EVICTION_INTERVAL` | int | 60000 | Eviction service interval (ms) |
| `EVICTION_BATCH_SIZE` | int | 1000 | Eviction batch size |

## Logging Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `LOG_LEVEL` | string | info | Log level (trace, debug, info, warn, error) |

## File Buffer Configuration (QoS 0 & Failover)

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `FILE_BUFFER_DIR` | string | Platform-specific* | Directory for file buffers |
| `FILE_BUFFER_FLUSH_MS` | int | 100 | How often to scan for complete buffer files (ms) |
| `FILE_BUFFER_MAX_BATCH` | int | 100 | Maximum events per database transaction |
| `FILE_BUFFER_EVENTS_PER_FILE` | int | 10000 | Create new buffer file after N events |

**Platform-specific defaults:**
- **macOS**: `/tmp/queen`
- **Linux**: `/var/lib/queen/buffers`

## Encryption Configuration

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_ENCRYPTION_KEY` | string | - | Encryption key (64 hex characters for AES-256) |

**Note:** Encryption uses AES-256-GCM algorithm with 32-byte keys and 16-byte IVs.

## Inter-Instance Communication (UDP Peers)

Queen servers can notify each other when messages are pushed or acknowledged, allowing poll workers on all instances to respond immediately.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_UDP_PEERS` | string | "" | Comma-separated UDP peers (e.g., `queen2:6633,queen3:6633`) |
| `QUEEN_UDP_NOTIFY_PORT` | int | 6633 | UDP port for peer notifications |

**Single server (default):**
```bash
# Local poll worker notification is automatic - no config needed
./bin/queen-server
```

**Cluster setup with UDP:**
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

**Kubernetes StatefulSet:**
```yaml
env:
  - name: QUEEN_UDP_PEERS
    value: "queen-mq-0.queen-mq-headless.ns.svc.cluster.local:6633,queen-mq-1.queen-mq-headless.ns.svc.cluster.local:6633"
  - name: QUEEN_UDP_NOTIFY_PORT
    value: "6633"
```

> **Note:** Self-detection is automatic. Each server excludes itself from the peer list.

## Distributed Cache (UDPSYNC)

Queen includes a distributed cache layer that shares state between server instances.

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_SYNC_ENABLED` | bool | true | Enable/disable distributed cache sync |
| `QUEEN_SYNC_SECRET` | string | "" | HMAC-SHA256 secret for packet signing (64 hex chars) |
| `QUEEN_CACHE_PARTITION_MAX` | int | 10000 | Maximum partition IDs to cache (LRU eviction) |
| `QUEEN_CACHE_PARTITION_TTL_MS` | int | 300000 | Partition ID cache TTL (ms) |
| `QUEEN_CACHE_REFRESH_INTERVAL_MS` | int | 60000 | Queue config refresh interval from DB (ms) |
| `QUEEN_SYNC_HEARTBEAT_MS` | int | 1000 | Heartbeat interval (ms) |
| `QUEEN_SYNC_DEAD_THRESHOLD_MS` | int | 5000 | Server dead threshold (ms) |
| `QUEEN_SYNC_RECV_BUFFER_MB` | int | 8 | UDP receive buffer size (MB) |

### Security

For production deployments, set `QUEEN_SYNC_SECRET` to a 64-character hex string:

```bash
# Generate a secure secret
export QUEEN_SYNC_SECRET=$(openssl rand -hex 32)
```

## JWT Authentication Configuration

Queen supports optional JWT-based authentication for securing API endpoints.

### Basic Settings

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `JWT_ENABLED` | bool | `false` | Enable JWT authentication |
| `JWT_ALGORITHM` | string | `HS256` | Algorithm: `HS256`, `RS256`, or `auto` |
| `JWT_SECRET` | string | - | HS256 shared secret (required for HS256) |
| `JWT_JWKS_URL` | string | - | RS256 JWKS endpoint URL (for external IDPs) |
| `JWT_PUBLIC_KEY` | string | - | RS256 public key in PEM format |

### Token Validation

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `JWT_ISSUER` | string | - | Expected `iss` claim (empty = any issuer) |
| `JWT_AUDIENCE` | string | - | Expected `aud` claim (empty = any audience) |
| `JWT_CLOCK_SKEW` | int | `30` | Tolerance in seconds for time claims |
| `JWT_SKIP_PATHS` | string | `/health,/metrics,/` | Comma-separated paths to skip auth |

### JWKS Settings (RS256)

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `JWT_JWKS_REFRESH_INTERVAL` | int | `3600` | JWKS refresh interval in seconds |
| `JWT_JWKS_TIMEOUT_MS` | int | `5000` | Timeout for JWKS HTTP requests |

### Role-Based Access Control

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `JWT_ROLES_CLAIM` | string | `role` | Claim name containing role (single value) |
| `JWT_ROLES_ARRAY_CLAIM` | string | `roles` | Claim name containing roles array |
| `JWT_ROLE_ADMIN` | string | `admin` | Role value for admin access |
| `JWT_ROLE_READ_WRITE` | string | `read-write` | Role value for read-write access |
| `JWT_ROLE_READ_ONLY` | string | `read-only` | Role value for read-only access |

### Access Levels

Routes are protected based on access levels:

| Level | Description | Example Routes |
|-------|-------------|----------------|
| **PUBLIC** | No auth required | `/health`, `/metrics`, `/` (dashboard) |
| **READ_ONLY** | Any valid token | GET `/api/v1/status/*`, `/api/v1/resources/*` |
| **READ_WRITE** | `read-write` or `admin` role | POST `/api/v1/push`, GET `/api/v1/pop/*` |
| **ADMIN** | `admin` role only | `/api/v1/system/*`, DELETE operations |

### HS256 Example (Shared Secret)

For internal services or when using Queen Proxy:

```bash
export JWT_ENABLED=true
export JWT_ALGORITHM=HS256
export JWT_SECRET=your-256-bit-secret-key-here
```

### RS256 Example (External IDP)

For external identity providers (Okta, Auth0, Keycloak, etc.):

```bash
export JWT_ENABLED=true
export JWT_ALGORITHM=RS256
export JWT_JWKS_URL=https://your-idp.com/.well-known/jwks.json
export JWT_ISSUER=https://your-idp.com/
export JWT_AUDIENCE=queen-api
```

### Compatible with Queen Proxy

If using Queen Proxy for token generation, use the same `JWT_SECRET`:

```bash
# Both proxy and server use the same secret
export JWT_SECRET=same-secret-as-proxy
```

Tokens generated by the proxy include:
- `id`: User UUID
- `username`: Username
- `role`: One of `admin`, `read-write`, `read-only`

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
export QUEEN_ENCRYPTION_KEY=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
```

### High-Throughput Configuration
```bash
export DB_POOL_SIZE=300
export NUM_WORKERS=20
export SIDECAR_POOL_SIZE=100
export SIDECAR_MAX_ITEMS_PER_TX=2000
export RESPONSE_BATCH_SIZE=200
export RESPONSE_BATCH_MAX=1000
```

### Production with JWT Authentication
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

# JWT Authentication (HS256 with proxy)
export JWT_ENABLED=true
export JWT_ALGORITHM=HS256
export JWT_SECRET=your-production-secret-min-256-bits

# Or for external IDP (RS256)
# export JWT_ENABLED=true
# export JWT_ALGORITHM=RS256
# export JWT_JWKS_URL=https://your-idp.com/.well-known/jwks.json
# export JWT_ISSUER=https://your-idp.com/
```

## Notes

- **Boolean values**: Set to `"true"` to enable, any other value is treated as false
- **Integer values**: Must be valid integers, invalid values fall back to defaults
- **Encryption key**: Must be exactly 64 hexadecimal characters (32 bytes)
- All timeout values are in milliseconds unless specified as seconds
