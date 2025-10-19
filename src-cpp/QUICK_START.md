# Queen C++ Quick Start Guide

## TL;DR - Just Want to Run It?

### On Linux (Production):
```bash
cd src-cpp
make                                    # Build queen-server (SO_REUSEPORT)
DB_POOL_SIZE=50 ./bin/queen-server     # Run with proper pool size
```

### On macOS (Development):
```bash
cd src-cpp
make acceptor                           # Build queen-acceptor (Acceptor/Worker)
DB_POOL_SIZE=50 ./bin/queen-acceptor   # Run with proper pool size
```

---

## What's the Difference?

| | `queen-server` | `queen-acceptor` |
|---|---|---|
| **Pattern** | SO_REUSEPORT | Acceptor/Worker |
| **Works on** | Linux only | All platforms |
| **Performance** | **Best** (kernel load balancing) | Excellent (userspace round-robin) |
| **macOS Behavior** | Falls back to 1 worker | Full multi-threading |
| **Use for** | Linux production | macOS, Windows, cross-platform |

---

## Quick Commands

```bash
# Build
make           # Build queen-server (SO_REUSEPORT)
make acceptor  # Build queen-acceptor (Acceptor/Worker)
make both      # Build both variants
make help      # Show all options

# Run
DB_POOL_SIZE=50 ./bin/queen-server     # SO_REUSEPORT
DB_POOL_SIZE=50 ./bin/queen-acceptor   # Acceptor/Worker

# Test
curl http://localhost:6632/health      # Check if running
make test                              # Run full test suite
```

---

## Critical Configuration

### Database Pool Size (MUST READ!)

**Rule:** `DB_POOL_SIZE` ≥ **2.5x worker count**

```bash
# Default: 10 workers
DB_POOL_SIZE=50 ./bin/queen-server     # ✅ Correct

# Wrong!
DB_POOL_SIZE=20 ./bin/queen-server     # ❌ Will cause "mutex lock failed"
```

**Why?** Workers need concurrent DB connections. Pool exhaustion = crashes.

### Performance Tuning

**Default (10 workers, good for most use cases):**
```bash
DB_POOL_SIZE=50 ./bin/queen-server
```

**High Performance (20 workers):**
```bash
# Edit src-cpp/src/multi_app_server.cpp line 659:
# Change: int num_threads = std::min(10, ...);
#     To: int num_threads = std::min(20, ...);

cd src-cpp && make clean && make
DB_POOL_SIZE=100 ./bin/queen-server
```

**Maximum (50 workers):**
```bash
# Edit source: num_threads = std::min(50, ...);
cd src-cpp && make clean && make
DB_POOL_SIZE=150 ./bin/queen-server
```

---

## Benchmarks

**Linux (10 workers, DB_POOL_SIZE=50):**
- SO_REUSEPORT: **121,000 msg/s** ⚡
- Acceptor/Worker: ~110,000 msg/s

**macOS (10 workers, DB_POOL_SIZE=50):**
- SO_REUSEPORT: ~10,000 msg/s (falls back to 1 worker) ⚠️
- Acceptor/Worker: **95,000 msg/s** ⚡

---

## Troubleshooting

### "mutex lock failed: Invalid argument"
**Cause:** `DB_POOL_SIZE` too small  
**Fix:** Increase to at least 50

```bash
DB_POOL_SIZE=50 ./bin/queen-server
```

### Server crashes with large batches
**Cause:** Memory pressure with 10K+ messages  
**Fix:** Use batch=1000 or increase system memory

```bash
# Client side
batch_size = 1000  # Not 10000
```

### macOS: Only 1 worker active
**Cause:** Using `queen-server` (SO_REUSEPORT doesn't load balance on macOS)  
**Fix:** Use `queen-acceptor` instead

```bash
make acceptor
DB_POOL_SIZE=50 ./bin/queen-acceptor
```

---

## Environment Variables

```bash
# Required
export PG_HOST=localhost
export PG_PORT=5432
export PG_DB=postgres
export PG_USER=postgres
export PG_PASSWORD=postgres
export DB_POOL_SIZE=50                    # CRITICAL!

# Optional
export HOST=0.0.0.0
export PORT=6632
export WORKER_ID=cpp-worker-1
export DEFAULT_TIMEOUT=30000
export DEFAULT_BATCH_SIZE=1
export QUEEN_ENCRYPTION_KEY=your_key_here
```

---

## Production Checklist

- [ ] Built correct variant (`queen-server` on Linux, `queen-acceptor` on macOS)
- [ ] Set `DB_POOL_SIZE` to at least 2.5x worker count
- [ ] Set encryption key: `QUEEN_ENCRYPTION_KEY`
- [ ] Database is accessible and schema initialized
- [ ] Tested with `curl http://localhost:6632/health`
- [ ] Benchmarked with expected load
- [ ] Monitoring/logging configured
- [ ] Systemd service or equivalent setup

---

## Learn More

- **[README.md](README.md)** - Full documentation
- **[SCALING_PATTERNS.md](SCALING_PATTERNS.md)** - Detailed scaling guide
- **[CONFIGURATION.md](CONFIGURATION.md)** - Performance tuning
- **[FINAL_STATUS.md](FINAL_STATUS.md)** - Implementation details

---

## Still Confused?

**Just copy-paste this:**

```bash
# Linux
cd src-cpp && make && DB_POOL_SIZE=50 ./bin/queen-server

# macOS
cd src-cpp && make acceptor && DB_POOL_SIZE=50 ./bin/queen-acceptor
```

**Test it works:**
```bash
curl http://localhost:6632/health
# Should return: {"status":"healthy","database":"connected",...}
```

Done! 🎉

