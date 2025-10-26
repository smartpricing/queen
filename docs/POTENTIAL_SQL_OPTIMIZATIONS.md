# Potential SQL Optimizations Analysis

**Date:** October 24, 2025  
**Status:** Analysis - Not Yet Implemented  
**Current Performance:** Push 60k msg/s, Consume 124k msg/s

---

## 🔍 Analysis Methodology

Analyzed the C++ codebase (`queue_manager.cpp`, `database.cpp`, schema initialization) to identify optimization opportunities beyond the three already implemented.

---

## 📋 Current State Assessment

### ✅ Already Optimized
- ✅ 30 comprehensive indexes created (lines 2450-2479)
- ✅ Batch UNNEST INSERT operations
- ✅ FOR UPDATE SKIP LOCKED for concurrency
- ✅ Connection pooling with timeout recovery
- ✅ Batch capacity checks with ANY()
- ✅ Proactive duplicate detection
- ✅ Triggers for auto-updating partition activity and pending estimates

---

## 🎯 Potential Optimizations (Ranked by Impact)

### 🟢 **HIGH IMPACT** - Worth Implementing

#### 1. **Prepared Statements for Frequent Queries** ⚡

**Current Issue:**
```cpp
// Every pop operation builds and parses the same query structure
auto query_result = QueryResult(conn->exec_params(sql, params));
```

**Problem:** PostgreSQL parses and plans the query every time, even though the structure is identical.

**Solution:** Use `PQprepare()` and `PQexecPrepared()` for:
- POP queries (executed millions of times)
- ACK updates
- Lease acquisition
- Capacity checks

**Expected Gain:** 5-15% reduction in query latency

**Implementation Complexity:** Medium
- Need to manage prepared statement lifecycle
- Store prepared statement names
- Handle connection pool with prepared statements

**Code Impact:**
```cpp
// In DatabaseConnection class
std::map<std::string, bool> prepared_statements_;

// Prepare once per connection
PGresult* prepare_statement(const std::string& name, const std::string& query);

// Execute many times
PGresult* exec_prepared(const std::string& name, const std::vector<std::string>& params);
```

**Risk:** Low - PostgreSQL native feature

---

#### 2. **JSONB GIN Index for Payload Queries** 🔍

**Current Issue:** No index on `queen.messages.payload` column

**Observation:**
```sql
CREATE TABLE queen.messages (
    ...
    payload JSONB NOT NULL,  -- No index!
    ...
);
```

**Problem:** If analytics or filtering ever queries message payloads, it's a full table scan.

**Solution:** Add GIN index:
```sql
CREATE INDEX IF NOT EXISTS idx_messages_payload_gin 
ON queen.messages USING GIN (payload jsonb_path_ops);
```

**Expected Gain:** 
- If payload queries are used: 100-1000x faster
- If not used: Minimal overhead (writes slightly slower)

**When Useful:**
- Analytics queries filtering by payload content
- Dead letter queue investigations
- Message search features

**Cost:** ~20% write overhead for messages table (INSERT becomes slightly slower)

**Risk:** Low - Only helps if payload queries exist

---

#### 3. **Optimize Trigger Overhead on INSERT** ⚙️

**Current Issue:** Two triggers fire on EVERY message insert

```sql
-- Trigger 1: Update partition last_activity
CREATE TRIGGER trigger_update_partition_activity
AFTER INSERT ON queen.messages
FOR EACH ROW
EXECUTE FUNCTION update_partition_last_activity();

-- Trigger 2: Increment pending estimate
CREATE TRIGGER trigger_update_pending_on_push
AFTER INSERT ON queen.messages
FOR EACH ROW
EXECUTE FUNCTION update_pending_on_push();
```

**Problem:** 
- Each INSERT causes 2 additional UPDATE operations
- At 60k msg/s = 120k extra UPDATE operations per second
- Creates write contention on `queen.partitions` and `queen.partition_consumers`

**Solutions:**

**Option A: Statement-level triggers instead of row-level** (Recommended)
```sql
-- Fire once per INSERT statement, not per row
CREATE TRIGGER trigger_update_pending_on_push
AFTER INSERT ON queen.messages
FOR EACH STATEMENT  -- Instead of FOR EACH ROW
EXECUTE FUNCTION update_pending_on_push_batch();
```

**Option B: Update pending estimate in application code**
- Remove trigger entirely
- Update `pending_estimate` in the same transaction as INSERT
- Batch update: `UPDATE ... SET pending_estimate = pending_estimate + $1`

**Expected Gain:** 10-20% improvement in INSERT throughput

**Risk:** Medium - Need careful testing to ensure counts remain accurate

---

#### 4. **Batch DELETE for Completed Messages** 🗑️

**Current Issue:** No visible message cleanup/retention implementation in C++ code

**Observation:** `retention_seconds` and `completed_retention_seconds` exist in schema but cleanup isn't shown.

**Concern:** If millions of messages accumulate:
- Table bloat
- Index degradation
- Slower queries over time

**Solution:** Implement efficient batch deletion:
```sql
-- Delete in chunks to avoid long locks
DELETE FROM queen.messages
WHERE id IN (
    SELECT id 
    FROM queen.messages m
    JOIN queen.partition_consumers pc ON m.partition_id = pc.partition_id
    WHERE m.created_at < NOW() - INTERVAL '7 days'
      AND m.id <= pc.last_consumed_id  -- Only delete consumed messages
    LIMIT 10000  -- Batch size
);
```

**Expected Gain:** 
- Prevents table bloat
- Maintains consistent query performance over time

**Risk:** Low - Standard maintenance pattern

---

### 🟡 **MEDIUM IMPACT** - Consider If Needed

#### 5. **Connection-Level Query Result Caching** 💾

**Current Observation:** Queue configuration fetched repeatedly

**Example:**
```cpp
// Line 1208: Gets queue config for every pop
auto config_result = QueryResult(conn->exec_params(
    "SELECT delayed_processing, max_wait_time_seconds, lease_time, window_buffer FROM queen.queues WHERE name = $1",
    {queue_name}
));
```

**Problem:** Same queue config fetched thousands of times per second

**Solution:** In-memory cache with TTL:
```cpp
// Cache queue config for 60 seconds
struct CachedQueueConfig {
    QueueConfig config;
    std::chrono::steady_clock::time_point expires_at;
};

std::map<std::string, CachedQueueConfig> queue_config_cache_;
std::mutex cache_mutex_;
```

**Expected Gain:** 5-10% reduction in database queries

**Trade-off:** Config changes take up to 60s to propagate

**Risk:** Low - Queue config rarely changes

---

#### 6. **Materialized Views for Analytics** 📊

**Current Issue:** Analytics queries in `analyticsManager.cpp` do complex JOINs and aggregations

**Problem:** Dashboard queries can be slow on large datasets

**Solution:** Create materialized views:
```sql
CREATE MATERIALIZED VIEW queen.queue_stats_mv AS
SELECT 
    q.name,
    q.namespace,
    q.task,
    COUNT(DISTINCT p.id) as partition_count,
    COALESCE(SUM(pc.pending_estimate), 0) as total_pending
FROM queen.queues q
LEFT JOIN queen.partitions p ON p.queue_id = q.id
LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
GROUP BY q.id, q.name, q.namespace, q.task;

-- Refresh periodically
REFRESH MATERIALIZED VIEW CONCURRENTLY queen.queue_stats_mv;
```

**Expected Gain:** 10-100x faster dashboard/analytics queries

**Cost:** Slightly stale data (refresh every 30-60s)

**Risk:** Low - Analytics data doesn't need to be real-time

---

#### 7. **Reduce Lease Acquisition Queries** 🔒

**Current Issue:** Lease acquisition does 2 queries

```cpp
// Line 1066-1074: Check if consumer exists
auto check_result = QueryResult(conn->exec_params(check_sql, ...));
bool consumer_exists = check_result.is_success() && check_result.num_rows() > 0;

// Then: INSERT ... ON CONFLICT DO UPDATE
```

**Problem:** Could be combined into one query

**Solution:** Use INSERT ... ON CONFLICT with RETURNING to detect if row existed:
```sql
INSERT INTO queen.partition_consumers (...)
VALUES (...)
ON CONFLICT (partition_id, consumer_group) DO UPDATE
    SET ... 
    WHERE (condition)
RETURNING 
    worker_id,
    (xmax = 0) AS was_inserted  -- True if inserted, false if updated
```

**Expected Gain:** 2 queries → 1 query for lease acquisition

**Risk:** Low - Standard PostgreSQL pattern

---

### 🔵 **LOW IMPACT** - Nice to Have

#### 8. **Partial Indexes for Active Leases** 📑

**Current:** Full index on lease_expires_at

```sql
CREATE INDEX idx_partition_consumers_expired_leases 
ON queen.partition_consumers(lease_expires_at) 
WHERE lease_expires_at IS NOT NULL;
```

**Optimization:** Already using partial indexes! ✅ (No change needed)

**Note:** This is already well-optimized with WHERE clauses on indexes.

---

#### 9. **BRIN Indexes for Time-Series Data** 📅

**Consideration:** For `created_at` columns in very large tables

**Current:** B-tree index on `messages.created_at`

**Alternative:** BRIN (Block Range Index):
```sql
CREATE INDEX idx_messages_created_at_brin 
ON queen.messages USING BRIN (created_at);
```

**Pros:**
- Much smaller index size (1-2% vs 10% of B-tree)
- Good for sequential time-series inserts

**Cons:**
- Slower for random access
- Only beneficial for tables >10GB

**Recommendation:** Only if messages table exceeds 10-100 million rows

**Risk:** Low - Can coexist with B-tree index

---

#### 10. **Table Partitioning for Messages Table** 📦

**Consideration:** Partition `queen.messages` by time range

**Current:** Single large table

**Potential:**
```sql
CREATE TABLE queen.messages (
    id UUID,
    created_at TIMESTAMPTZ,
    ...
) PARTITION BY RANGE (created_at);

CREATE TABLE queen.messages_2025_10 PARTITION OF queen.messages
    FOR VALUES FROM ('2025-10-01') TO ('2025-11-01');
```

**Pros:**
- Faster queries with partition pruning
- Easier to drop old data (DROP TABLE instead of DELETE)
- Better vacuum performance

**Cons:**
- Complex to maintain
- Requires partition management automation
- Migration complexity

**Recommendation:** Only if messages table exceeds 100 million rows

**Risk:** High - Significant schema change

---

## 📊 Impact vs Effort Matrix

| Optimization | Impact | Effort | Risk | Priority |
|-------------|--------|--------|------|----------|
| 1. Prepared Statements | High | Medium | Low | **P0** |
| 2. JSONB GIN Index | Medium-High | Low | Low | **P1** |
| 3. Optimize Triggers | High | Medium | Medium | **P1** |
| 4. Batch DELETE | Medium | Low | Low | **P2** |
| 5. Config Caching | Medium | Low | Low | **P2** |
| 6. Materialized Views | Medium | Medium | Low | **P3** |
| 7. Lease Query Reduction | Low | Low | Low | **P3** |
| 8. Partial Indexes | ✅ Done | - | - | - |
| 9. BRIN Indexes | Low | Low | Low | **P4** |
| 10. Table Partitioning | Low-Medium | High | High | **P5** |

---

## 🎯 Recommended Implementation Order

### Phase 1: Quick Wins (1-2 weeks)
1. **Prepared Statements** - 5-15% latency reduction
2. **JSONB GIN Index** - Future-proof for payload queries
3. **Config Caching** - Reduce redundant queries

**Expected Cumulative Gain:** +10-20% throughput improvement

### Phase 2: Performance Tuning (2-3 weeks)
4. **Optimize Triggers** - Reduce write amplification
5. **Batch DELETE** - Prevent table bloat

**Expected Cumulative Gain:** +15-30% total improvement

### Phase 3: Scaling (If needed, based on growth)
6. **Materialized Views** - If analytics become slow
7. **BRIN Indexes** - If messages table > 10GB
8. **Table Partitioning** - If messages table > 100M rows

---

## 🧪 How to Validate Each Optimization

### Prepared Statements
```bash
# Before
EXPLAIN ANALYZE SELECT ...

# After - should show "prepared statement" in plan
EXPLAIN (ANALYZE, VERBOSE) EXECUTE prepared_pop_query(...)
```

### Trigger Optimization
```sql
-- Monitor trigger overhead
SELECT schemaname, tablename, n_tup_ins, n_tup_upd 
FROM pg_stat_user_tables 
WHERE tablename IN ('messages', 'partitions', 'partition_consumers');
```

### Cache Hit Rate
```sql
-- Check if queries hit cache vs disk
SELECT 
    relname, 
    heap_blks_read, 
    heap_blks_hit,
    heap_blks_hit::float / NULLIF(heap_blks_hit + heap_blks_read, 0) as cache_hit_ratio
FROM pg_statio_user_tables
WHERE relname LIKE 'queen%';
```

---

## ⚠️ What NOT to Do

### ❌ Don't Optimize Prematurely
- **Table partitioning** - Wait until >100M rows
- **BRIN indexes** - Only for time-series at scale
- **Async operations** - Current sync is fast enough (124k msg/s)

### ❌ Don't Break What Works
- **Current indexes are excellent** - Don't remove any
- **FOR UPDATE SKIP LOCKED** - Don't try to "optimize" this
- **UNNEST batch inserts** - Already optimal

### ❌ Don't Add Complexity Without Measurement
- Measure with EXPLAIN ANALYZE first
- Benchmark before and after
- Monitor production metrics

---

## 🎬 Conclusion

The codebase is **already well-optimized** with:
- Comprehensive indexing strategy
- Efficient batch operations
- Proper use of PostgreSQL features

**Next worthwhile optimizations** (in order):
1. ⚡ Prepared statements (5-15% gain)
2. ⚙️ Trigger optimization (10-20% gain)
3. 💾 Config caching (5-10% gain)

**Total potential:** +20-45% additional improvement on top of current 124k msg/s = **~150-180k msg/s** theoretical max.

However, at current performance (60k push, 124k consume), the system is **already performing excellently**. Focus should shift to:
- Monitoring and observability
- Horizontal scaling (more workers)
- Application-level optimizations

**Recommendation:** Implement Phase 1 (prepared statements) only if you need to push beyond 150k msg/s.

