# Server Installation

Build and install Queen MQ server from source.

## Quick Build

```bash
cd server
make all
DB_POOL_SIZE=50 ./bin/queen-server
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
export DB_POOL_SIZE=50
export NUM_WORKERS=10

./bin/queen-server
```

[Complete guide](/guide/installation) | [GitHub](https://github.com/smartpricing/queen/tree/master/server)
