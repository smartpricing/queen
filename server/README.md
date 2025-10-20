# Queen C++ Server - Build & Tuning Guide

Complete guide for building, configuring, and tuning the Queen C++ message queue server.

## Table of Contents

- [Building the Server](#building-the-server)
- [Performance Tuning](#performance-tuning)
- [Database Configuration](#database-configuration)
- [Queue Optimization](#queue-optimization)
- [Monitoring & Debugging](#monitoring--debugging)

---

## Building the Server

### Prerequisites

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install build-essential libpq-dev libssl-dev zlib1g-dev curl unzip
```

**macOS:**
```bash
brew install postgresql openssl curl unzip
```

Or use the Makefile helpers:
```bash
# Ubuntu
make install-deps-ubuntu

# macOS
make install-deps-macos
```

### Quick Build

```bash
cd server

# Download dependencies and build
make all

# Or build without re-downloading dependencies
make build-only
```

The compiled binary will be at `bin/queen-server`.

### Build Targets

```bash
make all          # Download deps and build
make build-only   # Build without downloading deps
make deps         # Download dependencies only
make clean        # Remove build artifacts
make distclean    # Remove build artifacts and dependencies
make test         # Run test suite
make dev          # Build and run in development mode
make help         # Show all available targets
```

### Build Configuration

The Makefile automatically detects:
- Operating system (macOS/Linux)
- PostgreSQL installation path
- OpenSSL installation path
- Homebrew prefix (on macOS)

**Debug build paths:**
```bash
make debug-paths
```

**Show build statistics:**
```bash
make debug-objects
```

### Compiler Options

Default flags in `Makefile`:
```makefile
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pthread -DWITH_OPENSSL=1
```

**Custom optimization:**
```bash
# Maximum optimization
CXXFLAGS="-std=c++17 -O3 -march=native" make

# Debug build with symbols
CXXFLAGS="-std=c++17 -g -O0" make
```

---

## Performance Tuning

### Critical: Database Pool Size

**The most important tuning parameter.**

**Rule:** `DB_POOL_SIZE` ≥ **2.5× number of worker threads**

```bash
# Default: 10 worker threads
DB_POOL_SIZE=50 ./bin/queen-server          # Recommended: 50

# High load: 20 workers (modify in source)
DB_POOL_SIZE=100 ./bin/queen-server         # Minimum: 50, Recommended: 100
```

**Why?** Each worker can have multiple concurrent requests. Pool exhaustion causes `mutex lock failed` errors.

### Worker Thread Configuration

Edit `server/src/acceptor_server.cpp`:

```cpp
export NUM_WORKERS=10  # Configurable via environment (default: 10, max: CPU cores)
```

**Recommendations:**
- **Light load**: `NUM_WORKERS=4`, `DB_POOL_SIZE=25`
- **Medium load**: `NUM_WORKERS=10` (default), `DB_POOL_SIZE=50`
- **High load**: `NUM_WORKERS=20`, `DB_POOL_SIZE=100`
- **Maximum**: `NUM_WORKERS=50`, `DB_POOL_SIZE=150`

**Note:** The number of workers is automatically capped at your CPU core count.

### Architecture: Acceptor/Worker Pattern

```
Client → Acceptor (listens on port 6632)
           ↓ (round-robin distribution)
        Worker 1 (event loop + DB pool)
        Worker 2 (event loop + DB pool)
        Worker 3 (event loop + DB pool)
        ...
        Worker N (event loop + DB pool)
```

**Benefits:**
- ✅ True parallelism across CPU cores
- ✅ Non-blocking async I/O in each worker
- ✅ Cross-platform (macOS, Linux, Windows)
- ✅ No event loop blocking

### High-Throughput Configuration

```bash
export DB_POOL_SIZE=300
export BATCH_INSERT_SIZE=2000
export QUEUE_POLL_INTERVAL=50
export MAX_PARTITION_CANDIDATES=200
export RETENTION_BATCH_SIZE=2000
export EVICTION_BATCH_SIZE=2000
export DEFAULT_BATCH_SIZE=100

./bin/queen-server
```

**Expected Performance:**
- **Throughput**: 130,000+ msg/s sustained
- **Peak**: 148,000+ msg/s
- **Latency**: Sub-millisecond ACKs

### Long Polling Optimization

```bash
# Faster polling (lower latency, higher CPU)
export QUEUE_POLL_INTERVAL=50              # Initial: 50ms
export QUEUE_MAX_POLL_INTERVAL=1000        # Max: 1s

# Slower polling (lower CPU, higher latency)
export QUEUE_POLL_INTERVAL=100             # Initial: 100ms (default)
export QUEUE_MAX_POLL_INTERVAL=2000        # Max: 2s (default)

# Exponential backoff
export QUEUE_BACKOFF_THRESHOLD=5           # Empty polls before backoff
export QUEUE_BACKOFF_MULTIPLIER=2.0        # Backoff multiplier
```

**How it works:**
1. Initial poll at `QUEUE_POLL_INTERVAL` (100ms default)
2. If empty, wait again (exponential backoff)
3. Interval doubles each retry: 100ms → 200ms → 400ms → 800ms → 1600ms
4. Caps at `QUEUE_MAX_POLL_INTERVAL` (2000ms default)
5. Returns empty after timeout

---

## Database Configuration

### Connection Settings

```bash
export PG_HOST=localhost
export PG_PORT=5432
export PG_DB=postgres
export PG_USER=postgres
export PG_PASSWORD=your_password
```

### SSL Configuration

```bash
export PG_USE_SSL=true
export PG_SSL_REJECT_UNAUTHORIZED=true
```

### Pool Tuning

```bash
export DB_POOL_SIZE=50                     # Pool size (CRITICAL!)
export DB_IDLE_TIMEOUT=30000               # Idle timeout (ms)
export DB_CONNECTION_TIMEOUT=2000          # Connection timeout (ms)
export DB_POOL_ACQUISITION_TIMEOUT=10000   # Pool acquisition timeout (ms)
export DB_STATEMENT_TIMEOUT=30000          # Statement timeout (ms)
export DB_QUERY_TIMEOUT=30000              # Query timeout (ms)
export DB_LOCK_TIMEOUT=10000               # Lock timeout (ms)
export DB_MAX_RETRIES=3                    # Max retry attempts
```

### PostgreSQL Server Tuning

Recommended `postgresql.conf` settings for Queen:

```ini
# Connection settings
max_connections = 200                      # Should be > DB_POOL_SIZE × workers
shared_buffers = 4GB                       # 25% of RAM
effective_cache_size = 12GB                # 75% of RAM

# Write performance
wal_buffers = 16MB
checkpoint_completion_target = 0.9
max_wal_size = 4GB
min_wal_size = 1GB

# Query performance
random_page_cost = 1.1                     # For SSD
effective_io_concurrency = 200             # For SSD
work_mem = 64MB                            # Per operation
maintenance_work_mem = 512MB

# Parallelism
max_worker_processes = 8
max_parallel_workers_per_gather = 4
max_parallel_workers = 8

# Logging (production)
log_line_prefix = '%m [%p] %u@%d '
log_min_duration_statement = 1000          # Log slow queries > 1s
```

After changes:
```bash
sudo systemctl restart postgresql
```

---

## Queue Optimization

### Default Queue Settings

```bash
export DEFAULT_LEASE_TIME=300              # 5 minutes
export DEFAULT_RETRY_LIMIT=3               # Max retries
export DEFAULT_RETRY_DELAY=1000            # Retry delay (ms)
export DEFAULT_MAX_SIZE=10000              # Max queue size
export DEFAULT_BATCH_SIZE=1                # Default batch size
```

### Batch Processing

```bash
export BATCH_INSERT_SIZE=1000              # Bulk insert size
export DEFAULT_BATCH_SIZE=100              # Default pop batch
export MAX_TIMEOUT=60000                   # Max pop timeout (ms)
```

**For high throughput:**
- Set `BATCH_INSERT_SIZE=2000` (or higher)
- Use larger batch sizes when consuming (10-100 messages)
- Reduce `QUEUE_POLL_INTERVAL` to 50ms

### Partition Selection

```bash
export MAX_PARTITION_CANDIDATES=100        # Candidate partitions for lease
```

**Tuning:**
- **Low partition count** (<50): Keep at 100
- **High partition count** (>200): Increase to 200-500
- **Very high** (>1000): Increase to 1000

### Retention & Cleanup

```bash
# Retention settings
export DEFAULT_RETENTION_SECONDS=0         # 0 = disabled
export DEFAULT_COMPLETED_RETENTION_SECONDS=0
export RETENTION_INTERVAL=300000           # 5 minutes
export RETENTION_BATCH_SIZE=1000

# Partition cleanup
export PARTITION_CLEANUP_DAYS=7
export METRICS_RETENTION_DAYS=90
```

### Eviction Settings

```bash
export DEFAULT_MAX_WAIT_TIME_SECONDS=0     # 0 = disabled
export EVICTION_INTERVAL=60000             # 1 minute
export EVICTION_BATCH_SIZE=1000
```

---

## Monitoring & Debugging

### Logging Configuration

```bash
# Development
export LOG_LEVEL=debug
export LOG_FORMAT=text
export LOG_TIMESTAMP=true

# Production
export LOG_LEVEL=info
export LOG_FORMAT=json
export LOG_TIMESTAMP=true
```

**Log Levels:**
- `trace` - Extremely verbose
- `debug` - Detailed debugging
- `info` - General information (default)
- `warn` - Warnings only
- `error` - Errors only
- `critical` - Critical errors
- `off` - Disable logging

### Health & Metrics Endpoints

```bash
# Health check
curl http://localhost:6632/health

# Performance metrics
curl http://localhost:6632/metrics

# System overview
curl http://localhost:6632/api/v1/resources/overview

# Queue statistics
curl http://localhost:6632/api/v1/status/queues
```

### Request & Message Counting

```bash
export ENABLE_REQUEST_COUNTING=true
export ENABLE_MESSAGE_COUNTING=true
export METRICS_ENDPOINT_ENABLED=true
export HEALTH_CHECK_ENABLED=true
```

### Development Mode

```bash
# Start with debug logging
LOG_LEVEL=debug ./bin/queen-server --dev

# Or use make target
make dev
```

### Common Issues & Solutions

#### 1. "mutex lock failed: Invalid argument"
**Cause:** `DB_POOL_SIZE` too small  
**Fix:**
```bash
DB_POOL_SIZE=50 ./bin/queen-server
```

#### 2. High CPU usage
**Cause:** Polling interval too aggressive  
**Fix:**
```bash
export QUEUE_POLL_INTERVAL=200
export QUEUE_MAX_POLL_INTERVAL=3000
```

#### 3. Slow message delivery
**Cause:** Polling interval too conservative  
**Fix:**
```bash
export QUEUE_POLL_INTERVAL=50
export QUEUE_MAX_POLL_INTERVAL=1000
```

#### 4. "Database connection pool timeout"
**Cause:** Pool exhaustion during high concurrent long-polling operations  
**Symptoms:** Errors like "Database connection pool timeout (waited 10000ms)"  
**Fix:**
```bash
# Option 1: Increase pool size (recommended)
export DB_POOL_SIZE=200

# Option 2: Increase acquisition timeout for more patience
export DB_POOL_ACQUISITION_TIMEOUT=20000  # 20 seconds

# Option 3: Reduce POP timeout to release connections faster
export DEFAULT_TIMEOUT=10000  # 10 seconds instead of 30
```

**Note:** The server was optimized (v1.x+) to reuse connections within POP operations, reducing from 3 connections per POP to 1. If you still see this error, increase `DB_POOL_SIZE`.

#### 5. Database connection errors (general)
**Cause:** Pool exhaustion or PostgreSQL `max_connections`  
**Fix:**
```bash
# Increase pool
DB_POOL_SIZE=100 ./bin/queen-server

# And in postgresql.conf
max_connections = 200
```

#### 6. Build errors on macOS
**Cause:** PostgreSQL/OpenSSL not found  
**Fix:**
```bash
brew install postgresql openssl
make debug-paths  # Check detected paths
```

---

## Production Deployment

### Systemd Service

Create `/etc/systemd/system/queen-server.service`:

```ini
[Unit]
Description=Queen C++ Message Queue Server
After=postgresql.service
Requires=postgresql.service

[Service]
Type=simple
User=queen
Group=queen
WorkingDirectory=/opt/queen
ExecStart=/opt/queen/bin/queen-server
Restart=always
RestartSec=10

# Environment variables
Environment=PORT=6632
Environment=HOST=0.0.0.0
Environment=PG_HOST=localhost
Environment=PG_PORT=5432
Environment=PG_DB=queen_production
Environment=PG_USER=queen
Environment=PG_PASSWORD=secure_password
Environment=DB_POOL_SIZE=100
Environment=LOG_LEVEL=info
Environment=LOG_FORMAT=json
Environment=QUEEN_ENCRYPTION_KEY=your_64_char_hex_key_here

# Security
NoNewPrivileges=true
PrivateTmp=true

# Resource limits
LimitNOFILE=65536
LimitNPROC=4096

[Install]
WantedBy=multi-user.target
```

**Start the service:**
```bash
sudo systemctl daemon-reload
sudo systemctl enable queen-server
sudo systemctl start queen-server
sudo systemctl status queen-server
```

### Docker Deployment

See the root `Dockerfile` and `build.sh` for containerized deployment.

### Load Balancing

For horizontal scaling, run multiple Queen servers behind a load balancer:

```nginx
upstream queen_cluster {
    server queen1.local:6632;
    server queen2.local:6632;
    server queen3.local:6632;
}

server {
    listen 80;
    location / {
        proxy_pass http://queen_cluster;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }
}
```

---

## Benchmarking

### Built-in Benchmarks

Located in `client-js/benchmark/`:

```bash
cd client-js

# Producer benchmark
node benchmark/producer.js

# Consumer benchmark
node benchmark/consumer.js

# Multi-producer
node benchmark/producer_multi.js

# Multi-consumer
node benchmark/consumer_multi.js
```

### Expected Results

**Single server, NUM_WORKERS=10 (default), DB_POOL_SIZE=50:**
- Throughput: 129,000+ msg/s (sustained)
- Peak: 148,000+ msg/s
- ACK latency: < 1ms
- 1M messages: ~7.7 seconds

---

## Environment Variables Reference

See [ENV_VARIABLES.md](ENV_VARIABLES.md) for the complete list of all 100+ configuration options.

**Quick reference:**

| Variable | Default | Description |
|----------|---------|-------------|
| `NUM_WORKERS` | 10 | Number of worker threads (max: CPU cores) |
| `DB_POOL_SIZE` | 150 | **CRITICAL** - Pool size (2.5× workers) |
| `PORT` | 6632 | Server port |
| `HOST` | 0.0.0.0 | Server host |
| `LOG_LEVEL` | info | Log level (debug, info, warn, error) |
| `QUEUE_POLL_INTERVAL` | 100 | Initial poll interval (ms) |
| `BATCH_INSERT_SIZE` | 1000 | Bulk insert batch size |
| `QUEEN_ENCRYPTION_KEY` | - | Encryption key (64 hex chars) |

---

## Further Reading

- [QUICK_START.md](QUICK_START.md) - Quick start guide
- [ENV_VARIABLES.md](ENV_VARIABLES.md) - All environment variables
- [../README.md](../README.md) - Main project README

---

## Support

For issues or questions:
- Check logs: `journalctl -u queen-server -f`
- Enable debug logging: `LOG_LEVEL=debug`
- Review metrics: `curl http://localhost:6632/metrics`

