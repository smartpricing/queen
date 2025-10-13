# Queen Performance Optimization Plan

**Document Version:** 1.0  
**Date:** October 11, 2025  
**Current Performance Baseline:** 161,000 msg/s (single worker)  
**Target:** 1,500,000+ msg/s (multi-worker)

---

## 📊 Current Performance Baseline

### Benchmark Results (100,000 messages, 10 consumers, 10 partitions)
```
Throughput: 161,290 msg/s
Pop Time:   290-880ms average
ACK Time:   ~0ms (negligible)
Test Pass:  46/46 (100%)
```

### Performance Breakdown (Per Pop Operation)
```
Total: 290-880ms
├─ Database Query:  50-500ms  (85-90%)  ← PRIMARY BOTTLENECK
├─ Lease Update:    10-30ms   (3-5%)
├─ HTTP Overhead:    5-10ms   (1-2%)
├─ Lease Acquire:    1-5ms    (0.5-1%)
└─ Decrypt/Format:   1-5ms    (0.5-1%)
```

### Recent Optimizations Completed ✅
1. **Removed status update on POP** - Eliminated 700-850ms overhead
2. **Hybrid counter approach** - O(1) acknowledgments instead of O(n²)
3. **Partition-level locking** - Proper FIFO ordering with namespace filtering
4. **Message batch tracking** - Prevents duplicate consumption

---

## 🎯 Optimization Opportunities (Prioritized by ROI)

### Legend
- 🏆 **Critical** - Highest ROI, implement immediately
- ⭐ **High** - Significant gains, implement soon
- 💡 **Medium** - Good gains, implement when resources available
- 🔬 **Low** - Marginal gains, consider if needed

---

## 🏆 TIER 1: CRITICAL (Implement This Week)

### 1.1 Worker Thread Clustering
**Priority:** 🏆🏆🏆 CRITICAL  
**Effort:** 2-4 hours  
**Expected Gain:** **10x throughput (1.6M msg/s)**  
**Complexity:** Low

#### Current State
- Single Node.js thread using 1 CPU core
- 10 cores available, 9 idle
- Current: 161k msg/s on 1 core

#### Implementation
1. Create `src/cluster-server.js`:
```javascript
import cluster from 'cluster';
import os from 'os';

const numWorkers = os.cpus().length;

if (cluster.isPrimary) {
  console.log(`🚀 Master starting ${numWorkers} workers...`);
  
  for (let i = 0; i < numWorkers; i++) {
    cluster.fork({ 
      WORKER_ID: `queen-worker-${i}`,
      WORKER_INDEX: i 
    });
  }
  
  cluster.on('exit', (worker, code, signal) => {
    console.log(`⚠️  Worker ${worker.process.pid} died. Restarting...`);
    cluster.fork();
  });
  
} else {
  // Each worker runs the normal server
  await import('./server.js');
}
```

2. Update `package.json`:
```json
{
  "scripts": {
    "start": "node src/server.js",
    "start:cluster": "node src/cluster-server.js"
  }
}
```

3. Adjust database pool size:
```javascript
// In config.js
POOL_SIZE: 50  // From 20 (10 workers × 5 connections each)
```

#### Expected Results
- **Throughput:** 161k → 1.6M msg/s (10x improvement)
- **CPU Utilization:** 10% → 100% (use all cores)
- **Fault Tolerance:** Worker crashes don't kill server

#### Caveats
- Each worker has separate in-memory cache
  - ✅ Already handled via SystemEventManager cache invalidation
- Database connection pool shared across workers
  - Increase POOL_SIZE proportionally
- Port sharing handled automatically by OS/cluster module

#### Testing
```bash
# Baseline single worker
node src/benchmark/producer.js
node src/benchmark/consumer.js

# Clustered workers
node src/cluster-server.js &
node src/benchmark/producer.js
node src/benchmark/consumer.js
```

---

### 1.2 Database Connection Pool Optimization
**Priority:** 🏆🏆 CRITICAL  
**Effort:** 5 minutes  
**Expected Gain:** 10-30% throughput  
**Complexity:** Trivial

#### Current State
```javascript
POOL_SIZE: 20 connections
Clustered workers: 10
Total needed: 10 × 5 = 50 minimum
```

#### Implementation
```javascript
// In src/config.js
export const DATABASE = {
  POOL_SIZE: parseInt(process.env.DB_POOL_SIZE) || 50,  // From 20
  IDLE_TIMEOUT: 10000,
  CONNECTION_TIMEOUT: 3000
};
```

#### Also Consider
- Monitor connection usage with metrics
- Increase if you see connection timeouts
- PostgreSQL `max_connections` must be > 50

---

### 1.3 Enable TCP_NODELAY for PostgreSQL
**Priority:** 🏆 CRITICAL  
**Effort:** 10 minutes  
**Expected Gain:** 5-15% latency reduction  
**Complexity:** Low

#### What It Does
Disables Nagle's algorithm, sending packets immediately instead of buffering.

#### Implementation

**Option A: Client-Side (Easiest)**
```javascript
// In src/database/connection.js
export const createPool = () => {
  const poolConfig = {
    // ... existing config ...
    options: '-c tcp_nodelay=on'  // ← Add this
  };
  return new Pool(poolConfig);
};
```

**Option B: Server-Side (Best)**
Edit `postgresql.conf`:
```conf
tcp_nodelay = on
```

Then restart PostgreSQL:
```bash
pg_ctl restart
```

#### Expected Results
- 5-15ms latency reduction per query
- Better for high-frequency, small queries
- Reference: [Stack Overflow - TCP_NODELAY for PostgreSQL](https://stackoverflow.com/questions/60634455/how-does-one-configure-tcp-nodelay-for-libpq-and-postgres-server)

---

## ⭐ TIER 2: HIGH PRIORITY (Next Week)

### 2.1 Backpressure Handling for Large Responses
**Priority:** ⭐⭐⭐ HIGH  
**Effort:** 4 hours  
**Expected Gain:** 5-10% throughput, prevents event loop stalling  
**Complexity:** Medium

#### Current Issue
When returning 10,000 messages (~5-10 MB JSON):
- `res.end(JSON.stringify(result))` is synchronous
- Blocks event loop if client is slow
- No backpressure mechanism

#### Implementation
Create `src/utils/streaming.js`:
```javascript
/**
 * Stream large JSON responses with backpressure handling
 * Based on uWebSockets.js best practices
 */
export const streamJSON = (res, data, aborted = { aborted: false }) => {
  const jsonStr = JSON.stringify(data);
  const CHUNK_SIZE = 128 * 1024; // 128KB chunks
  
  // Small responses - send directly
  if (jsonStr.length < CHUNK_SIZE) {
    res.cork(() => {
      res.end(jsonStr);
    });
    return;
  }
  
  // Large responses - stream with backpressure
  let offset = 0;
  
  const sendNextChunk = (writeOffset) => {
    if (aborted.aborted) return true;
    
    const chunk = jsonStr.slice(offset, offset + CHUNK_SIZE);
    const isLastChunk = offset + CHUNK_SIZE >= jsonStr.length;
    
    if (isLastChunk) {
      res.end(chunk);
      return true;
    }
    
    const ok = res.write(chunk);
    offset += CHUNK_SIZE;
    
    if (!ok) {
      // Buffer full - will call onWritable when ready
      return false;
    }
    
    // Continue sending
    return sendNextChunk(offset);
  };
  
  res.cork(() => {
    const ok = sendNextChunk(0);
    if (!ok) {
      // Set up backpressure handler
      res.onWritable(sendNextChunk);
    }
  });
};
```

Use in routes:
```javascript
// In pop route
import { streamJSON } from '../utils/streaming.js';

const result = await popRoute(scope, options);
if (aborted) return;

streamJSON(res, result, { aborted });  // Instead of res.end()
```

#### Expected Results
- Non-blocking for large responses
- Better handling of slow clients
- Prevents event loop stalls
- 5-10% throughput improvement

---

### 2.2 Prepared Statements for Hot Paths
**Priority:** ⭐⭐ HIGH  
**Effort:** 1 day  
**Expected Gain:** 5-10% query time reduction  
**Complexity:** Medium

#### What It Does
Reuses query execution plans instead of re-parsing SQL each time.

#### Implementation
```javascript
// In queueManagerOptimized.js
const preparedStatements = new Map();

const getPreparedStatement = async (client, name, sql) => {
  if (!preparedStatements.has(name)) {
    await client.query(`PREPARE ${name} AS ${sql}`);
    preparedStatements.set(name, true);
  }
  return name;
};

// Use in hot paths
const popMessages = async (scope, options) => {
  return withTransaction(pool, async (client) => {
    // Prepare statement once
    await getPreparedStatement(client, 'pop_messages', `
      SELECT m.id, m.transaction_id, ...
      FROM queen.messages m
      WHERE m.partition_id = $1 AND ...
    `);
    
    // Execute prepared statement
    const result = await client.query(
      `EXECUTE pop_messages($1, $2, ...)`,
      [partitionId, ...]
    );
  });
};
```

#### Expected Results
- 5-10ms saved per query (plan parsing overhead)
- Cumulative savings at high throughput
- Better with complex queries

---

### 2.3 Query Optimization via EXPLAIN ANALYZE
**Priority:** ⭐⭐ HIGH  
**Effort:** 2-4 hours  
**Expected Gain:** 10-50% query time (if indexes aren't optimal)  
**Complexity:** Medium

#### Process
1. **Profile hot queries:**
```sql
EXPLAIN ANALYZE
SELECT m.id, m.transaction_id, m.trace_id, m.payload, ...
FROM queen.messages m
JOIN queen.partitions p ON m.partition_id = p.id
JOIN queen.queues q ON p.queue_id = q.id
LEFT JOIN queen.messages_status ms ON m.id = ms.message_id 
  AND ms.consumer_group = '__QUEUE_MODE__'
LEFT JOIN queen.partition_leases pl ON pl.partition_id = p.id
  AND pl.consumer_group = '__QUEUE_MODE__'
WHERE m.partition_id = 'uuid'
  AND (ms.id IS NULL OR ms.status IN ('pending', 'failed'))
  AND ...
ORDER BY m.created_at ASC, m.id ASC
LIMIT 10000;
```

2. **Look for:**
   - Sequential scans (should be index scans)
   - High-cost operations
   - Missing indexes
   - Inefficient joins

3. **Add indexes if needed:**
```sql
-- Example: If partition_id + created_at scan is slow
CREATE INDEX IF NOT EXISTS idx_messages_partition_created_status 
ON queen.messages(partition_id, created_at, id) 
INCLUDE (transaction_id, trace_id, payload, is_encrypted);
```

#### Expected Results
- Identify inefficient query plans
- Add covering indexes if needed
- 10-50% query time reduction possible

---

## 💡 TIER 3: MEDIUM PRIORITY (When Resources Available)

### 3.1 Binary Protocol (MessagePack/Protocol Buffers)
**Priority:** 💡💡 MEDIUM  
**Effort:** 3-5 days  
**Expected Gain:** 10-15% throughput, 30% bandwidth  
**Complexity:** High

#### Benefits
- 50-70% smaller payloads
- 3-5x faster serialization (binary vs JSON)
- Better for network-constrained environments

#### Implementation Outline
1. Add MessagePack dependency:
```bash
npm install msgpack-lite
```

2. Create binary endpoints:
```javascript
import msgpack from 'msgpack-lite';

app.post('/api/v1/push/binary', (res, req) => {
  readBody(res, (buffer) => {
    const data = msgpack.decode(buffer);
    // ... process ...
    const response = msgpack.encode(result);
    res.writeHeader('Content-Type', 'application/msgpack');
    res.end(response);
  });
});
```

3. Update client to support binary mode

#### Trade-offs
- ✅ Faster serialization
- ✅ Smaller payloads
- ❌ Not human-readable
- ❌ Can't use curl/Postman for debugging
- ❌ Client changes required

#### When to Implement
- If network bandwidth is constrained
- If you're doing cross-region deployments
- After worker clustering is stable

---

### 3.2 Response Compression (Gzip)
**Priority:** 💡 MEDIUM  
**Effort:** 2 hours  
**Expected Gain:** Variable (good for WAN, bad for LAN)  
**Complexity:** Low

#### Implementation
```javascript
import { gzipSync } from 'zlib';

const sendCompressed = (res, data, threshold = 1024) => {
  const jsonStr = JSON.stringify(data);
  
  if (jsonStr.length > threshold) {
    const compressed = gzipSync(jsonStr);
    res.cork(() => {
      res.writeHeader('Content-Encoding', 'gzip');
      res.end(compressed);
    });
  } else {
    res.cork(() => {
      res.end(jsonStr);
    });
  }
};
```

#### Trade-offs
- ✅ 70-80% bandwidth reduction
- ✅ Faster over slow networks (WAN)
- ❌ 10-20ms CPU overhead for compression
- ❌ Slower on fast networks (localhost/LAN)

#### When to Use
- Only for WAN deployments
- Skip for localhost/LAN (your current setup)
- Client must support gzip

---

### 3.3 Database Read Replicas
**Priority:** 💡💡 MEDIUM  
**Effort:** 1 week (infrastructure + code)  
**Expected Gain:** 2-3x throughput  
**Complexity:** High

#### Architecture
```
┌─────────┐     Write     ┌─────────────┐
│ Client  │──────────────→│  Primary DB │
└─────────┘               └─────────────┘
                                 │
                          Replication
                                 ↓
                          ┌─────────────┐
                          │  Replica 1  │←─┐
                          ├─────────────┤  │
                          │  Replica 2  │←─┤ Read POPs
                          ├─────────────┤  │
                          │  Replica 3  │←─┘
                          └─────────────┘
```

#### Implementation
1. Setup PostgreSQL streaming replication
2. Create separate connection pools:
```javascript
const primaryPool = createPool(PRIMARY_CONFIG);  // For PUSH/ACK
const replicaPool = createPool(REPLICA_CONFIG);  // For POP
```

3. Route operations appropriately:
```javascript
// Writes go to primary
pushMessages() → primaryPool
acknowledgeMessages() → primaryPool

// Reads can use replicas
popMessages() → replicaPool
getQueueStats() → replicaPool
```

#### Expected Results
- 2-3x read throughput
- Reduced load on primary
- Better separation of concerns

#### Caveats
- Replication lag (typically <100ms)
- More complex infrastructure
- Failover handling needed

---

## 🔬 TIER 4: LOW PRIORITY (Future Optimization)

### 4.1 Custom TCP Protocol (L4)
**Priority:** 🔬 LOW  
**Effort:** 4 weeks  
**Expected Gain:** 1-2% throughput  
**Complexity:** Very High

#### Analysis
**Current HTTP overhead:** 5-10ms per request (1-2% of total)

**With TCP:**
- Save protocol parsing: ~2-3ms
- Save JSON overhead: ~3-5ms (if using binary)
- Total savings: ~5-10ms per pop

**But:**
- ❌ Very high implementation cost
- ❌ Loss of HTTP ecosystem tools
- ❌ Custom client required
- ❌ Only 1-2% improvement

#### Recommendation
**Skip this.** Worker clustering gives 10x for 2 hours of work.  
TCP gives 1-2% for 4 weeks of work. ROI is terrible.

---

### 4.2 HTTP/2 or HTTP/3
**Priority:** 🔬 LOW  
**Effort:** 1 day  
**Expected Gain:** 5-10% for concurrent requests  
**Complexity:** Low

#### Benefits
- Multiplexing (multiple requests over one connection)
- Header compression
- Server push capabilities

#### Implementation
```javascript
// uWebSockets.js doesn't support HTTP/2 yet
// Would need to switch to Node.js http2 module
// Not recommended - uWS is faster even with HTTP/1.1
```

#### Recommendation
**Skip for now.** uWebSockets.js HTTP/1.1 is faster than most HTTP/2 implementations.

---

## 📈 Implementation Roadmap

### Week 1: Critical Optimizations (🏆)
**Goal:** Achieve 1.5M+ msg/s

#### Day 1-2: Worker Clustering
- [ ] Create `cluster-server.js`
- [ ] Test with single worker baseline
- [ ] Test with clustered workers
- [ ] Measure throughput improvement
- [ ] **Expected: 161k → 1.6M msg/s**

#### Day 3: Database Optimization
- [ ] Increase pool size to 50
- [ ] Enable TCP_NODELAY
- [ ] Test connection usage
- [ ] **Expected: +10-20% improvement**

#### Day 4: Backpressure Implementation
- [ ] Create streaming utility
- [ ] Update pop routes
- [ ] Test with large batches
- [ ] **Expected: +5-10% improvement**

#### Day 5: Testing & Validation
- [ ] Run full test suite (46 tests)
- [ ] Run benchmark with various batch sizes
- [ ] Load test with multiple clients
- [ ] Monitor for regressions

---

### Week 2-3: High Priority Optimizations (⭐)

#### Query Optimization
- [ ] Profile with EXPLAIN ANALYZE
- [ ] Identify slow queries
- [ ] Add covering indexes if needed
- [ ] Test improvement

#### Prepared Statements
- [ ] Implement for pop queries
- [ ] Implement for push queries
- [ ] Implement for ack queries
- [ ] Measure query plan reuse

---

### Month 2+: Medium Priority (💡)

#### Read Replicas (if needed)
- [ ] Setup PostgreSQL replication
- [ ] Create replica pools
- [ ] Route reads to replicas
- [ ] Handle failover

#### Binary Protocol (if needed)
- [ ] Evaluate MessagePack vs Protocol Buffers
- [ ] Implement binary endpoints
- [ ] Update client library
- [ ] Benchmark vs JSON

---

## 🧪 Testing Strategy

### Performance Testing
```bash
# 1. Baseline (single worker)
node src/server.js &
SERVER_PID=$!
node src/benchmark/producer.js
node src/benchmark/consumer.js
kill $SERVER_PID

# 2. Clustered (10 workers)
node src/cluster-server.js &
CLUSTER_PID=$!
node src/benchmark/producer.js
node src/benchmark/consumer.js
kill $CLUSTER_PID

# 3. Compare results
echo "Single worker: XXX msg/s"
echo "Clustered: YYY msg/s"
echo "Improvement: Z.Zx"
```

### Regression Testing
```bash
# Run full test suite after each optimization
QUEEN_ENCRYPTION_KEY=2e433dbbc61b88406530f4613ddd9ea4e5b575364029587ee829fbe285f8dbbc \
  node src/test/test-new.js

# Expected: 46/46 tests passing
```

### Load Testing
```bash
# Simulate production load
# Multiple producers + consumers
for i in {1..5}; do
  node src/benchmark/producer.js &
done

for i in {1..20}; do
  node src/benchmark/consumer.js &
done

# Monitor server metrics
```

---

## 📊 Expected Final Results

### Target Performance (With All Tier 1 + Tier 2)

```
Metric                  Current    Target     Improvement
────────────────────────────────────────────────────────
Throughput              161k/s     1.8M/s     11.2x
Pop Latency (avg)       540ms      450ms      1.2x
CPU Utilization         10%        90%        9x
Database Connections    20         50         2.5x
Worker Processes        1          10         10x
Test Pass Rate          46/46      46/46      100%
```

### Performance by Implementation Phase

| Phase | Optimizations | Expected Throughput | Time to Implement |
|-------|--------------|-------------------|------------------|
| **Baseline** | Current state | 161k msg/s | - |
| **Phase 1** | Worker clustering | 1.6M msg/s | 2 hours |
| **Phase 2** | + TCP_NODELAY + Pool size | 1.75M msg/s | 15 min |
| **Phase 3** | + Backpressure | 1.85M msg/s | 4 hours |
| **Phase 4** | + Query optimization | 2.0M+ msg/s | 1 day |

---

## 🛡️ Safety & Monitoring

### Metrics to Monitor
```javascript
// Add to server.js
setInterval(() => {
  console.log(`📊 Performance Metrics:
    - Throughput: ${messagesProcessed / uptimeSeconds} msg/s
    - Active DB Connections: ${pool.totalCount}
    - Waiting DB Connections: ${pool.waitingCount}
    - Worker Memory: ${process.memoryUsage().heapUsed / 1024 / 1024} MB
    - Event Loop Lag: ${measureEventLoopLag()} ms
  `);
}, 10000);
```

### Health Checks
- Database connection pool health
- Worker process health
- Memory usage per worker
- Event loop lag
- Queue depth per partition

---

## 🎯 Success Criteria

### Phase 1 (Worker Clustering) - Success if:
- ✅ Throughput > 1.5M msg/s
- ✅ All 46 tests pass
- ✅ All CPU cores utilized (>80%)
- ✅ No worker crashes under load
- ✅ Cache invalidation works across workers

### Phase 2 (Database Optimization) - Success if:
- ✅ Throughput > 1.75M msg/s
- ✅ Query latency reduced by 10%+
- ✅ No connection pool exhaustion
- ✅ All tests still passing

### Phase 3 (Backpressure) - Success if:
- ✅ No event loop blocking (>50ms lag)
- ✅ Handles slow clients gracefully
- ✅ Memory usage stable under load
- ✅ Throughput maintained or improved

---

## 📚 References

### uWebSockets.js Best Practices
- [uWebSockets.js GitHub Examples](https://github.com/uNetworking/uWebSockets.js/tree/master/examples)
- Backpressure: Use `res.onWritable()` for large responses
- Clustering: Node.js cluster module with uWS
- Cork: Always wrap writes in `res.cork()` for batching

### PostgreSQL Optimization
- [TCP_NODELAY Configuration](https://stackoverflow.com/questions/60634455/how-does-one-configure-tcp-nodelay-for-libpq-and-postgres-server)
- [Pipelining and Batching](https://www.2ndquadrant.com/en/blog/postgresql-latency-pipelining-batching/)
- Connection pooling best practices
- Index optimization strategies

### Already Implemented ✅
- Status update removal (700-850ms saved)
- Hybrid counter approach (O(1) ACKs)
- Partition-level locking
- Message ordering guarantees
- Namespace/task filtering with ordering

---

## 🏁 Quick Start

### Immediate Actions (Next 30 minutes):

1. **Increase database pool size:**
```bash
export DB_POOL_SIZE=50
```

2. **Enable TCP_NODELAY:**
Add to `postgresql.conf`:
```conf
tcp_nodelay = on
```

3. **Create cluster server:**
```bash
# Create the file as shown in section 1.1
vi src/cluster-server.js

# Test it
node src/cluster-server.js
```

4. **Benchmark:**
```bash
node src/benchmark/producer.js
node src/benchmark/consumer.js
```

**Expected result after 30 minutes:** **1.6M+ msg/s** 🚀

---

## 💰 Cost/Benefit Summary

| Optimization | Time | Gain | Cost/Benefit |
|--------------|------|------|--------------|
| Worker Clustering | 2h | **10x** | 🏆 **5x gain per hour** |
| DB Pool + TCP_NODELAY | 15m | 15-20% | 🏆 **1x gain per minute** |
| Backpressure | 4h | 5-10% | ⭐ **0.02x gain per hour** |
| Prepared Statements | 1d | 5-10% | ⭐ **0.007x gain per hour** |
| Query Optimization | 4h | 10-50% | ⭐ **0.05x gain per hour** |
| Binary Protocol | 3d | 10-15% | 💡 **0.005x gain per hour** |
| Read Replicas | 1w | 2-3x | 💡 **0.03x gain per hour** |
| TCP L4 Protocol | 4w | 1-2% | 🔬 **0.0003x gain per hour** |

**Clear winner: Worker Clustering** 🏆

---

## 🎓 Lessons Learned

### What Worked
1. **Removing status updates on POP** - 4.5x improvement
2. **Hybrid counter approach** - 100x faster ACKs
3. **Already using uWebSockets.js** - Fastest HTTP server
4. **Batching 10,000 messages** - Amortizes overhead perfectly

### What to Avoid
1. ❌ Custom TCP protocol - Too much work for minimal gain
2. ❌ Compression on LAN - Adds CPU overhead for no benefit
3. ❌ Micro-optimizations - Focus on big wins first

### Key Insight
**The database query (50-500ms) is 90% of the time.**  
Optimize database access, not the protocol!

---

## 🚀 Next Steps

1. **This week:** Implement worker clustering (2 hours → 10x gain)
2. **Next week:** Profile and optimize database queries
3. **Month 2:** Consider read replicas if >1.5M msg/s isn't enough
4. **Future:** Binary protocol for cross-region deployments

---

## 📞 Decision Points

### Should we implement worker clustering?
**YES!** 10x improvement for 2 hours of work.

### Should we implement backpressure?
**YES!** Prevents event loop stalls, good engineering practice.

### Should we use binary protocol?
**MAYBE.** Only if network bandwidth is constrained or you need that extra 10-15%.

### Should we build custom TCP protocol?
**NO.** Terrible ROI. Focus on database and clustering instead.

---

## ✅ Checklist for Production Deployment

Before going to production with these optimizations:

- [ ] Worker clustering implemented and tested
- [ ] Database pool size increased appropriately
- [ ] TCP_NODELAY enabled
- [ ] All 46 tests passing
- [ ] Load testing completed (sustained load for 1 hour+)
- [ ] Monitoring and alerting in place
- [ ] Backpressure handling for large responses
- [ ] Worker restart/failover tested
- [ ] Database connection pool exhaustion handling
- [ ] Memory leak testing (24 hour run)
- [ ] Cache invalidation across workers verified
- [ ] Benchmarks documented with before/after

---

**Last Updated:** October 11, 2025  
**Status:** Ready for Implementation  
**Estimated Time to 1.5M+ msg/s:** 1 week

