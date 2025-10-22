# PostgreSQL Failover Testing Guide

## How Failover Works

### First Request When DB Goes Down
1. Client pushes messages
2. Server tries `push_messages()`
3. PostgreSQL is down → waits for **statement_timeout** (default: 30 seconds)
4. Timeout triggers → exception caught
5. Messages written to file buffer
6. `db_healthy_` flag set to `false`

### Subsequent Requests
1. Client pushes messages
2. Server checks `db_healthy_` flag → **false**
3. **Immediately** writes to file buffer (no DB attempt)
4. Instant response (~10μs)

### When DB Comes Back
1. Background processor attempts to flush
2. Success → `db_healthy_` flag set to `true`
3. Normal operation resumes

---

## ⚡ Fast Failover Configuration

### Recommended: Reduce Statement Timeout

For **faster failover detection** (2 seconds instead of 30):

```bash
DB_STATEMENT_TIMEOUT=2000 ./bin/queen-server
```

**Trade-offs:**
- ✅ Faster failover detection (2s instead of 30s)
- ⚠️ May timeout on slow queries (adjust based on your workload)

### Production Settings

```bash
# Fast failover for critical systems
DB_STATEMENT_TIMEOUT=2000 \
DB_CONNECTION_TIMEOUT=1000 \
FILE_BUFFER_FLUSH_MS=50 \
./bin/queen-server
```

---

## 🧪 Testing PostgreSQL Failover

### Test 1: Stop PostgreSQL

```bash
# Terminal 1: Start server with fast timeout
cd server
DB_STATEMENT_TIMEOUT=2000 ./bin/queen-server

# Terminal 2: Stop PostgreSQL
sudo systemctl stop postgresql
# Or on macOS: brew services stop postgresql

# Terminal 3: Push messages
node client-js/benchmark/producer.js

# Watch Terminal 1 logs - you should see:
# [Worker 0] >>> PUSH: 1000 items, qos0=false, dbHealthy=true
# ... (waits 2 seconds)
# [Worker 0] PostgreSQL unavailable, using file buffer for failover
# [Worker 0] >>> PUSH: 1000 items, qos0=false, dbHealthy=false  ← Fast now!
# [Worker 1] DB known to be down, using file buffer immediately  ← Instant!
```

### Test 2: Check Buffer Files

```bash
# Check buffer stats
curl http://localhost:6632/api/v1/status/buffers

# Response:
# {
#   "pending": 10000,     # Events in buffer
#   "failed": 0,
#   "dbHealthy": false    # DB is down
# }

# Check failover file
ls -lh /tmp/queen/failover.buf
# Should show file with data
```

### Test 3: Recovery

```bash
# Start PostgreSQL
sudo systemctl start postgresql
# Or on macOS: brew services start postgresql

# Wait 1-2 seconds for background processor

# Check buffer stats again
curl http://localhost:6632/api/v1/status/buffers

# Response:
# {
#   "pending": 0,         # All processed!
#   "failed": 0,
#   "dbHealthy": true     # DB recovered
# }

# Verify messages are in database
curl "http://localhost:6632/api/v1/pop/queue/benchmark-queue-001?batch=10"
```

---

## 📊 Failover Performance

### Timeline Example (with DB_STATEMENT_TIMEOUT=2000)

```
T+0s    Push 1: Try DB → timeout after 2s → failover
T+2s    Push 2: Check db_healthy (false) → instant failover
T+2s    Push 3: Check db_healthy (false) → instant failover
...
T+10s   DB comes back
T+10s   Background processor detects → starts replay
T+11s   All 10,000 messages replayed (FIFO order preserved)
```

### Timeline Example (with default DB_STATEMENT_TIMEOUT=30000)

```
T+0s    Push 1: Try DB → timeout after 30s → failover
T+30s   Push 2: Check db_healthy (false) → instant failover
T+30s   Push 3: Check db_healthy (false) → instant failover
...
```

**Recommendation:** Use `DB_STATEMENT_TIMEOUT=2000` for production systems that need fast failover.

---

## 🔍 Debugging Failover

### Check DB Health Flag

```bash
curl http://localhost:6632/api/v1/status/buffers
```

If `dbHealthy: false`, all subsequent pushes skip DB and go straight to file buffer.

### Monitor Logs

```bash
tail -f /var/log/queen/server.log | grep -E "PostgreSQL unavailable|DB known to be down|File buffer|Flushed"
```

Key messages:
- `PostgreSQL unavailable, using file buffer for failover` - First failure detected
- `DB known to be down, using file buffer immediately` - Fast path triggered
- `Flushed N events for queue 'X'` - Background processor working

### Check Timeout Configuration

```bash
# Inside PostgreSQL (if it's running)
psql -U postgres -c "SHOW statement_timeout;"

# Should match your DB_STATEMENT_TIMEOUT setting
```

---

## ⚠️ Current Behavior

**First push after DB goes down:**
- Waits for `statement_timeout` (default: 30 seconds)
- This is **by design** - PostgreSQL client needs to detect connection failure

**Subsequent pushes:**
- **Instant** - health check skips DB attempt
- Goes straight to file buffer

**Solution:** Reduce `DB_STATEMENT_TIMEOUT` for faster detection.

---

## 💡 Recommended Production Settings

```bash
# Fast failover detection
export DB_STATEMENT_TIMEOUT=2000
export DB_CONNECTION_TIMEOUT=1000
export DB_LOCK_TIMEOUT=2000

# File buffer settings
export FILE_BUFFER_DIR=/data/queen/buffers
export FILE_BUFFER_FLUSH_MS=50
export FILE_BUFFER_MAX_BATCH=500

./bin/queen-server
```

---

## 🎯 Testing Checklist

- [ ] Reduce DB_STATEMENT_TIMEOUT to 2000ms
- [ ] Stop PostgreSQL
- [ ] Push messages (first batch waits 2s, rest instant)
- [ ] Check `curl http://localhost:6632/api/v1/status/buffers`
- [ ] Verify files in `/tmp/queen/failover.buf`
- [ ] Start PostgreSQL
- [ ] Wait 1-2 seconds
- [ ] Check buffer stats (should be 0)
- [ ] Pop messages to verify they were saved

---

## 🚀 Quick Test Script

```bash
#!/bin/bash

echo "Testing PostgreSQL Failover..."
echo ""

# Start server with fast timeout
echo "1. Starting server with 2s timeout..."
DB_STATEMENT_TIMEOUT=2000 ./bin/queen-server &
SERVER_PID=$!
sleep 3

# Stop PostgreSQL
echo "2. Stopping PostgreSQL..."
brew services stop postgresql

# Push messages
echo "3. Pushing 1000 messages (first will wait 2s, rest instant)..."
node client-js/benchmark/producer.js

# Check buffer
echo "4. Checking buffer stats..."
curl -s http://localhost:6632/api/v1/status/buffers | jq

# Check files
echo "5. Checking buffer files..."
ls -lh /tmp/queen/

# Start PostgreSQL
echo "6. Starting PostgreSQL..."
brew services start postgresql
sleep 2

# Check recovery
echo "7. Checking buffer stats after recovery..."
curl -s http://localhost:6632/api/v1/status/buffers | jq

# Cleanup
kill $SERVER_PID
```

Try this and you'll see much faster failover! 🚀

