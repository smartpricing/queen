# Queen C++ Implementation

A high-performance C++ implementation of the Queen Message Queue system, built with uWebSockets and PostgreSQL.

## Features

- **High Performance**: Built with uWebSockets for maximum throughput
- **Multiple Scaling Patterns**: SO_REUSEPORT (Linux) or Acceptor/Worker (cross-platform)
- **PostgreSQL Backend**: Reliable, ACID-compliant message storage
- **Header-Only Dependencies**: Easy compilation and deployment
- **API Compatible**: Drop-in replacement for the Node.js version
- **C++17**: Modern C++ with clean, maintainable code

## Quick Start

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

### Build

```bash
# Clone and build
cd src-cpp
make deps    # Download header-only dependencies
make         # Build queen-server (SO_REUSEPORT pattern, default)

# Or build the acceptor/worker variant (cross-platform)
make acceptor   # Build queen-acceptor

# Or build both variants
make both

# Or do everything in one step
make all
```

**Two server variants are available:**

1. **`queen-server`** - SO_REUSEPORT pattern (best performance on Linux)
2. **`queen-acceptor`** - Acceptor/Worker pattern (works on all platforms)

See [SCALING_PATTERNS.md](SCALING_PATTERNS.md) for detailed comparison.

### Run

```bash
# Start the SO_REUSEPORT server (default, best for Linux)
./bin/queen-server

# Or start the Acceptor/Worker server (best for macOS/cross-platform)
./bin/queen-acceptor

# With custom settings
./bin/queen-server --port 6633 --host 0.0.0.0

# Development mode with debug logging
./bin/queen-server --dev

# Important: Set DB_POOL_SIZE to at least 2.5x the number of worker threads!
DB_POOL_SIZE=50 ./bin/queen-server
```

**Choosing which server to use:**
- **Linux production**: Use `queen-server` (SO_REUSEPORT) for maximum performance
- **macOS or cross-platform**: Use `queen-acceptor` (Acceptor/Worker) for true multi-threading

### Test with Existing Test Suite

The C++ server is designed to be compatible with the existing Node.js test suite:

```bash
# Run tests against C++ server (starts server on port 6633)
make test

# Or manually
./bin/queen-server --port 6633 &
cd .. && QUEEN_TEST_PORT=6633 node src/test/test-new.js
```

## Scaling Patterns

Queen C++ supports two scaling patterns:

### 1. SO_REUSEPORT (Default - Linux Only)

**File:** `bin/queen-server`

Multiple workers listen on the same port. The OS kernel automatically load balances connections.

```bash
DB_POOL_SIZE=50 ./bin/queen-server
```

**Pros:** Maximum performance via kernel load balancing  
**Cons:** Linux only (macOS falls back to 1 worker)

### 2. Acceptor/Worker (Cross-Platform)

**File:** `bin/queen-acceptor`

One acceptor app distributes sockets to worker apps in round-robin fashion.

```bash
DB_POOL_SIZE=50 ./bin/queen-acceptor
```

**Pros:** Works on all platforms (macOS, Linux, Windows)  
**Cons:** Slight overhead from socket transfer between threads

See **[SCALING_PATTERNS.md](SCALING_PATTERNS.md)** for detailed comparison and performance benchmarks.

## Configuration

The server uses the same environment variables as the Node.js version:

```bash
# Database configuration
export PG_HOST=localhost
export PG_PORT=5432
export PG_DB=postgres
export PG_USER=postgres
export PG_PASSWORD=postgres
export DB_POOL_SIZE=50  # CRITICAL: Must be 2.5x worker count!

# Server configuration  
export HOST=0.0.0.0
export PORT=6632
export WORKER_ID=cpp-worker-1

# Queue configuration
export DEFAULT_TIMEOUT=30000
export DEFAULT_BATCH_SIZE=1
```

### ⚠️ Critical: Database Pool Size

**`DB_POOL_SIZE` must be at least 2.5x the number of worker threads!**

```bash
# 10 workers (default)
DB_POOL_SIZE=50   # Minimum 25, recommended 50

# 20 workers
DB_POOL_SIZE=100  # Minimum 50, recommended 100

# 50 workers  
DB_POOL_SIZE=150  # Minimum 125, recommended 150
```

**Why?** Each worker can have multiple concurrent requests, and each needs a DB connection. Pool exhaustion causes "mutex lock failed" errors.

See [CONFIGURATION.md](CONFIGURATION.md) for detailed tuning guide.

## API Compatibility

The C++ server implements the same HTTP API as the Node.js version:

- `POST /api/v1/configure` - Configure queues
- `POST /api/v1/push` - Push messages
- `GET /api/v1/pop/queue/:queue/partition/:partition` - Pop from specific partition
- `GET /api/v1/pop/queue/:queue` - Pop from queue (default partition)
- `POST /api/v1/ack` - Acknowledge messages
- `GET /health` - Health check

## Performance

Expected performance improvements over Node.js version:

- **Throughput**: 5-10x higher message/sec
- **Latency**: 2-5x lower response times  
- **Memory**: 3-5x lower memory usage
- **CPU**: 3-5x lower CPU usage

## Architecture

```
src-cpp/
├── Makefile              # Build system
├── include/queen/        # Header files
│   ├── config.hpp       # Configuration management
│   ├── database.hpp     # PostgreSQL connection pool
│   ├── queue_manager.hpp # Core queue operations
│   └── server.hpp       # HTTP server
├── src/                 # Implementation files
│   ├── database/        # Database layer
│   ├── managers/        # Queue management
│   ├── server.cpp       # HTTP server implementation
│   └── main.cpp         # Entry point
└── vendor/              # Header-only dependencies
    ├── uWebSockets/     # HTTP/WebSocket library
    ├── json.hpp         # JSON parsing
    └── spdlog/          # Logging
```

## Dependencies

All dependencies are header-only libraries automatically downloaded during build:

- **uWebSockets**: High-performance HTTP/WebSocket server
- **nlohmann/json**: JSON parsing and serialization  
- **spdlog**: Fast logging library
- **libpq**: PostgreSQL client library (system dependency)

## Development

### Build Options

```bash
make clean          # Clean build artifacts
make distclean      # Clean everything including dependencies
make dev            # Build and start in development mode
```

### Debugging

```bash
# Build with debug symbols
CXXFLAGS="-g -O0" make

# Run with GDB
gdb ./bin/queen-server
```

### Memory Checking

```bash
# Build with AddressSanitizer
CXXFLAGS="-fsanitize=address -g" make

# Run with Valgrind
valgrind --leak-check=full ./bin/queen-server
```

## Deployment

### Single Binary

```bash
# Build statically linked binary (if supported)
CXXFLAGS="-static" make

# Copy binary to target system
cp bin/queen-server /usr/local/bin/
```

### Docker

```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libpq5 libssl3
COPY bin/queen-server /usr/local/bin/
EXPOSE 6632
CMD ["queen-server"]
```

### Systemd Service

```ini
[Unit]
Description=Queen C++ Message Queue Server
After=postgresql.service

[Service]
Type=simple
User=queen
ExecStart=/usr/local/bin/queen-server
Restart=always
Environment=PG_HOST=localhost
Environment=PG_PASSWORD=your_password

[Install]
WantedBy=multi-user.target
```

## Troubleshooting

### Build Issues

```bash
# Missing PostgreSQL headers
sudo apt-get install libpq-dev

# Missing OpenSSL
sudo apt-get install libssl-dev

# Permission issues
chmod +x bin/queen-server
```

### Runtime Issues

```bash
# Check database connection
psql -h localhost -U postgres -d postgres -c "SELECT 1"

# Check server logs
./bin/queen-server --dev

# Test basic functionality
curl http://localhost:6632/health
```

## Contributing

1. Follow C++17 standards
2. Use header-only libraries when possible
3. Maintain API compatibility with Node.js version
4. Add tests for new features
5. Update documentation

## License

Apache 2.0 - Same as the main Queen project
