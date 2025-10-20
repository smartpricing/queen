# Queen C++ Quick Start Guide

## TL;DR - Just Want to Run It?

```bash
cd src-cpp
make                                    # Build queen-server
DB_POOL_SIZE=50 ./bin/queen-server     # Run with proper pool size
```

**Works on:** macOS, Linux, Windows ✅  
**Test Coverage:** 60/60 tests passing ✅  
**Performance:** 130k+ msg/s ✅

---

## Quick Commands

```bash
# Build
make           # Build queen-server
make clean     # Clean build artifacts
make help      # Show all options

# Run
DB_POOL_SIZE=50 ./bin/queen-server

# Test
curl http://localhost:6632/health      # Check if running
make test                              # Run full test suite (60 tests)
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

---

## Architecture

**Acceptor/Worker Pattern:**
- 1 acceptor thread accepts connections
- 10 worker threads process requests
- Round-robin load balancing
- Async non-blocking polling
- Exponential backoff (100ms → 2000ms)

**Benefits:**
- Cross-platform (macOS, Linux, Windows)
- No event loop blocking
- True parallelism
- Production ready (60/60 tests)

---

## Performance

**Benchmark: 1M messages, 10 concurrent consumers**

- **Throughput**: 129,232 msg/s average
- **Peak**: 147,775 msg/s
- **Latency**: Sub-millisecond ACKs
- **Stability**: Zero crashes

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

- [ ] Built: `cd src-cpp && make`
- [ ] Set `DB_POOL_SIZE` to at least 50 (2.5x worker count)
- [ ] Set encryption key: `QUEEN_ENCRYPTION_KEY`
- [ ] Database accessible and schema initialized
- [ ] Tested: `curl http://localhost:6632/health`
- [ ] Run tests: `make test` (should show 60/60)
- [ ] Monitoring/logging configured
- [ ] Systemd service or equivalent setup

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
**Fix:** Use batch=1000

### No messages delivered after lease expiry
**Fixed!** Acceptor now properly reclaims expired leases

---

## Learn More

- **[README.md](README.md)** - Full documentation
- **[SCALING_PATTERNS.md](SCALING_PATTERNS.md)** - Architecture deep dive
- **[CONFIGURATION.md](CONFIGURATION.md)** - Performance tuning

---

## Test Suite

Run the complete test suite:

```bash
# Start server
DB_POOL_SIZE=50 QUEEN_ENCRYPTION_KEY=your_key ./bin/queen-server

# In another terminal - run tests
QUEEN_ENCRYPTION_KEY=your_key node src/test/test-new.js
```

**Expected:** 60/60 tests passing 🎉

---

## One-Line Setup

```bash
cd src-cpp && make && DB_POOL_SIZE=50 QUEEN_ENCRYPTION_KEY=2e433dbbc61b88406530f4613ddd9ea4e5b575364029587ee829fbe285f8dbbc ./bin/queen-server
```

Done! 🚀

