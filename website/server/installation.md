# Server Installation

Build and install Queen MQ server from source.

## Quick Build

```bash
cd server
make all
DB_POOL_SIZE=10 DEFAULT_SUBSCRIPTION_MODE=new LOG_LEVEL=info NUM_WORKERS=4 ./bin/queen-server
```

## Prerequisites

**Ubuntu/Debian:**
```bash
sudo apt-get install build-essential libpq-dev libssl-dev unzip zlib1g-dev cmake
```

**macOS:**
```bash
brew install postgresql openssl cmake
```

## Build Commands

```bash
make all          # Download deps and build
make build-only   # Build without deps
make clean        # Remove build artifacts
```

## Configuration

```bash
export PG_HOST=localhost
export PG_PORT=5432
export PG_USER=queen
export PG_PASSWORD=password
export PG_DB=queen
export DB_POOL_SIZE=10
export DEFAULT_SUBSCRIPTION_MODE=new
export LOG_LEVEL=info
export RETENTION_INTERVAL=10000
export RETENTION_BATCH_SIZE=50000
export QUEEN_SYNC_ENABLED=true
export QUEEN_UDP_PEERS=localhost:6634,localhost:6635
export QUEEN_UDP_NOTIFY_PORT=6634
export FILE_BUFFER_DIR=/tmp/queen/s1
export SIDECAR_POOL_SIZE=70
export SIDECAR_MICRO_BATCH_WAIT_MS=10
export NUM_WORKERS=4
export POP_WAIT_INITIAL_INTERVAL_MS=10
export POP_WAIT_MAX_INTERVAL_MS=1000
export POP_WAIT_BACKOFF_MULTIPLIER=2
export POP_WAIT_BACKOFF_THRESHOLD=1
export DB_STATEMENT_TIMEOUT=300000
export STATS_RECONCILE_INTERVAL_MS=30000

./bin/queen-server
```

[Complete guide](/guide/installation) | [GitHub](https://github.com/smartpricing/queen/tree/master/server)
