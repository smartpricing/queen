# QoS 0 Quick Start Guide

## 🚀 Setup & Build

### 1. Build Server

**No setup needed!** The buffer directory is automatically created on first run.

Platform-specific defaults:
- **macOS**: `/tmp/queen` (auto-created)
- **Linux**: `/var/lib/queen/buffers` (auto-created)

Custom directory:
```bash
export FILE_BUFFER_DIR=/custom/path
```

```bash
cd server
make clean
make build-only
```

### 2. Start Server

```bash
# Start with defaults (macOS: /tmp/queen, Linux: /var/lib/queen/buffers)
./bin/queen-server

# Or use custom directory
FILE_BUFFER_DIR=/custom/path ./bin/queen-server

# Watch for startup logs - you should see:
# [Worker 0] Creating file buffer manager (dir=/tmp/queen)...
# FileBufferManager initializing: dir=/tmp/queen
# Creating buffer directory: /tmp/queen
# Buffer directories ready: /tmp/queen
# Starting recovery of buffered events...
# Recovery completed in 0ms: failover=0, qos0=0
# [Worker 0] File buffer ready - Pending: 0, Failed: 0, DB: healthy
```

---

## 🧪 Quick Test

### Test 1: QoS 0 Buffering

```bash
# Push with buffering
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "items": [
      {"queue": "events", "payload": {"type": "test", "value": 123}}
    ],
    "bufferMs": 100,
    "bufferMax": 100
  }'

# Response should show:
# {
#   "pushed": true,
#   "qos0": true,
#   "dbHealthy": true,
#   "messages": [...]
# }

# Check buffer stats
curl http://localhost:6632/api/v1/status/buffers

# Monitor buffer files
ls -lh /var/lib/queen/buffers/
```

### Test 2: Auto-Ack

```bash
# First, push a message normally
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{"items": [{"queue": "test-autoack", "payload": {"msg": "hello"}}]}'

# Pop with auto-ack
curl "http://localhost:6632/api/v1/pop/queue/test-autoack?autoAck=true&batch=1"

# Try to pop again - should get nothing (auto-acked)
curl "http://localhost:6632/api/v1/pop/queue/test-autoack?wait=false"
```

### Test 3: Consumer Groups with Auto-Ack

```bash
# Push once
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{"items": [{"queue": "cg-test", "payload": {"value": 42}}]}'

# Pop from group A with auto-ack
curl "http://localhost:6632/api/v1/pop/queue/cg-test?consumerGroup=groupA&autoAck=true"

# Pop from group B with auto-ack (should get same message)
curl "http://localhost:6632/api/v1/pop/queue/cg-test?consumerGroup=groupB&autoAck=true"

# Both groups auto-acked - trying again should return nothing
curl "http://localhost:6632/api/v1/pop/queue/cg-test?consumerGroup=groupA&wait=false"
curl "http://localhost:6632/api/v1/pop/queue/cg-test?consumerGroup=groupB&wait=false"
```

---

## 📊 Run Example

```bash
cd examples
node 09-event-streaming.js
```

Expected output:
- QoS 0 buffering demo
- Consumer groups demo
- PostgreSQL failover demo
- Performance comparison

---

## 🧪 Run Tests

```bash
cd client-js/test

# Run QoS 0 tests
node --experimental-modules -e "
import { Queen } from '../client/index.js';
import { runQoS0Tests } from './qos0-tests.js';

const client = new Queen({ baseUrls: ['http://localhost:6632'] });
await runQoS0Tests(client);
"
```

Or integrate into existing test suite:

```javascript
// In client-js/test/test-new.js
import { runQoS0Tests } from './qos0-tests.js';

// After other tests
await runQoS0Tests(client);
```

---

## 📈 Monitor

### Check Buffer Stats

```bash
# API endpoint
curl http://localhost:6632/api/v1/status/buffers

# Response:
# {
#   "pending": 0,      # Events in buffer waiting to flush
#   "failed": 0,       # Events in failed directory
#   "dbHealthy": true  # PostgreSQL connection status
# }
```

### Watch Buffer Files

```bash
# Real-time monitoring
watch -n 1 'ls -lh /var/lib/queen/buffers/'

# Files you might see:
# qos0.buf              - Active QoS 0 buffer
# failover.buf          - Active failover buffer
# qos0_processing.buf   - Being processed
# failover_processing.buf - Being processed
# failed/*.buf          - Failed batches (DB was down)
```

### Server Logs

```bash
# Watch for buffer events
tail -f /path/to/queen.log | grep -i "buffer\|recovery\|qos0\|failover"

# Key log messages:
# - "Creating file buffer manager..."
# - "Starting recovery of buffered events..."
# - "Recovery completed in Xms: failover=Y, qos0=Z"
# - "File buffer ready"
# - "Flushed N events for queue 'X'"
# - "PostgreSQL unavailable, using file buffer for failover"
```

---

## 🔄 Test PostgreSQL Failover

### Simulate DB Outage

```bash
# Terminal 1: Watch logs
tail -f /var/log/queen/server.log

# Terminal 2: Stop PostgreSQL
sudo systemctl stop postgresql

# Terminal 3: Push messages (will use failover buffer)
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{"items": [{"queue": "failover-test", "payload": {"critical": true}}]}'

# Check response - should show dbHealthy: false
# Check buffer stats
curl http://localhost:6632/api/v1/status/buffers

# Check failover file
ls -lh /var/lib/queen/buffers/failover.buf

# Terminal 2: Start PostgreSQL
sudo systemctl start postgresql

# Wait 1-2 seconds, then check buffer stats again
curl http://localhost:6632/api/v1/status/buffers
# Should show: pending: 0 (events replayed!)

# Verify message was saved
curl "http://localhost:6632/api/v1/pop/queue/failover-test"
```

---

## 🎯 Performance Testing

### Benchmark QoS 0

```bash
cd client-js/benchmark
node qos0_benchmark.js
```

Expected results:
- Normal: ~500-1000 events/sec
- QoS 0: ~5000-10000 events/sec (10x improvement)

### Load Testing

```bash
# High-frequency events
for i in {1..1000}; do
  curl -X POST http://localhost:6632/api/v1/push \
    -H "Content-Type: application/json" \
    -d "{\"items\":[{\"queue\":\"load-test\",\"payload\":{\"i\":$i}}],\"bufferMs\":100,\"bufferMax\":100}" &
done

wait

# Check how many batches were created
curl http://localhost:6632/api/v1/status/buffers
```

---

## 🐛 Troubleshooting

### Events Stuck in Buffer

```bash
# Check buffer stats
curl http://localhost:6632/api/v1/status/buffers

# Check if DB is down
curl http://localhost:6632/health

# Check buffer files
ls -lh /var/lib/queen/buffers/

# Check failed directory
ls -lh /var/lib/queen/buffers/failed/
```

### Slow Startup

Normal if you have many buffered events from previous crash/outage:

```bash
# Recovery time estimates:
# 100 events: ~100ms
# 1,000 events: ~1s
# 10,000 events: ~10s

# Watch logs for recovery progress
tail -f /var/log/queen/server.log | grep recovery
```

### Disk Space Issues

```bash
# Check buffer directory size
du -sh /var/lib/queen/buffers/

# Check individual files
du -h /var/lib/queen/buffers/*

# Clean up old failed files (careful!)
rm /var/lib/queen/buffers/failed/old-*.buf
```

---

## ✅ Verify Installation

```bash
# 1. Check server is running
curl http://localhost:6632/health

# 2. Check buffer stats endpoint works
curl http://localhost:6632/api/v1/status/buffers

# 3. Test QoS 0 push
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{"items":[{"queue":"verify","payload":{"test":true}}],"bufferMs":100}'

# 4. Test auto-ack
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{"items":[{"queue":"verify-ack","payload":{"test":true}}]}'

curl "http://localhost:6632/api/v1/pop/queue/verify-ack?autoAck=true"

# 5. Run example
cd examples
node 09-event-streaming.js

# If all succeed: ✅ QoS 0 is working!
```

---

## 📚 Next Steps

- Read [QOS0.md](../QOS0.md) for complete implementation details
- Read [FILE_BUFFER_INTEGRATION.md](FILE_BUFFER_INTEGRATION.md) for integration guide
- Check [examples/09-event-streaming.js](../examples/09-event-streaming.js) for usage patterns
- Run [client-js/test/qos0-tests.js](../client-js/test/qos0-tests.js) for comprehensive tests

---

## 💡 Usage Tips

**For High-Frequency Events:**
```javascript
await client.push('metrics', data, { bufferMs: 50, bufferMax: 200 });
```

**For Dashboard Updates:**
```javascript
for await (const event of client.take('updates', { autoAck: true })) {
  updateUI(event);
}
```

**For Critical Events (buffered but reliable):**
```javascript
await client.push('billing', data, { bufferMs: 100 });
for await (const event of client.take('billing')) {
  await process(event);
  await client.ack(event);  // Still ack for reliability
}
```

**PostgreSQL Failover:**
- Automatic - no configuration needed
- Just push normally
- Events buffered if DB is down
- Automatic replay when DB recovers

