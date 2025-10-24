# SQL Performance Parity Plan: C++ → Node.js Optimizations

## Executive Summary

This document outlines the specific SQL query optimizations needed to bring the C++ implementation's database performance to parity with the Node.js version. The Node.js version has several critical optimizations that are missing in the C++ port.

**Impact:** These optimizations could improve throughput by 20-40% and reduce database load significantly.

---

## ⚠️ CRITICAL ISSUES

### Issue #1: Repeated Partition Subquery Anti-Pattern

**Severity:** 🔴 **CRITICAL**  
**Impact:** Extra JOIN on every UPDATE/INSERT operation  
**Locations:** 15+ occurrences across `queue_manager.cpp`

#### Problem

The C++ code repeatedly executes this subquery pattern:

```cpp
WHERE partition_id = (
    SELECT p.id FROM queen.partitions p
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = $2 AND p.name = $3
)
```

**Affected Lines in `queue_manager.cpp`:**
- Line 1147-1157: UPDATE batch_size
- Line 1298-1308: UPDATE cursor (auto-ack)
- Line 1330-1331: INSERT messages_consumed
- Line 1684-1706: INSERT DLQ
- Line 1901-1921: UPDATE partition_consumers
- Line 1992-2001: UPDATE partition_consumers (stats)
- Line 2136-2140: UPDATE partition_consumers (release)

#### Node.js Solution

```javascript
// Fetch partition_id ONCE and cache it
const resources = await ensureResources(client, queueName, partitionName);
const { partitionId } = resources;

// Use direct partition_id in all subsequent queries
Array(messageIds.length).fill(partitionId)
```

#### C++ Fix Strategy

**Option A: Fetch partition_id Once Per Operation** (Recommended)

Add a helper method to cache partition lookups:

```cpp
// In queue_manager.hpp
struct PartitionInfo {
    std::string partition_id;
    std::string queue_id;
    int lease_time;
    int retry_limit;
    // ... other config
};

std::optional<PartitionInfo> get_partition_info(
    pqxx::connection* conn,
    const std::string& queue_name,
    const std::string& partition_name
);
```

**Option B: Use CTE for Complex Operations**

For operations with multiple queries, use a Common Table Expression:

```sql
WITH partition_info AS (
    SELECT p.id as partition_id, q.id as queue_id
    FROM queen.partitions p
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = $1 AND p.name = $2
)
UPDATE queen.partition_consumers
SET batch_size = $3, acked_count = 0
WHERE partition_id = (SELECT partition_id FROM partition_info)
  AND consumer_group = $4
```

---

### Issue #2: No Batch Capacity Check

**Severity:** 🔴 **CRITICAL**  
**Impact:** N queries instead of 1 when pushing to multiple queues  
**Location:** Lines 573-634 in `queue_manager.cpp`

#### Problem

The C++ version checks queue capacity individually per queue:

```cpp
// In push_messages_internal, this happens per partition group
std::string capacity_sql = "SELECT id FROM queen.queues WHERE name = $1";
// ... then checks size per queue
```

#### Node.js Solution (Lines 305-321)

```javascript
// ⚡ OPTIMIZATION: Check ALL queues in a SINGLE query
const queuesToCheck = Object.keys(queueBatchSizes);
if (queuesToCheck.length > 0) {
    const capacityCheck = await client.query(`
      SELECT 
         q.name,
         q.max_queue_size,
         COALESCE(SUM(pc.pending_estimate), 0)::integer as current_depth
       FROM queen.queues q
       LEFT JOIN queen.partitions p ON p.queue_id = q.id
       LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
         AND pc.consumer_group = '__QUEUE_MODE__'
       WHERE q.name = ANY($1::varchar[])  -- ⚡ Batch check
         AND q.max_queue_size > 0
       GROUP BY q.name, q.max_queue_size
    `, [queuesToCheck]);
    
    // Check each queue's capacity
    for (const row of capacityCheck.rows) {
      const currentDepth = parseInt(row.current_depth);
      const batchSize = queueBatchSizes[row.name];
      // ...
    }
}
```

#### C++ Fix Required

**Location:** `queue_manager.cpp`, lines 573-700 (push_messages_internal function)

**Before:**
```cpp
// Current: Individual check per queue (inefficient)
for (auto& [partition_key, items] : partition_groups) {
    // Query 1: Get queue_id
    auto queue_result = QueryResult(conn->exec_params(
        "SELECT id FROM queen.queues WHERE name = $1", 
        {queue_name}
    ));
    
    // Query 2: Check size
    std::string capacity_sql = R"(
        SELECT q.max_queue_size, COALESCE(COUNT(m.id), 0) as current_size
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        WHERE q.id = $1 AND q.max_queue_size > 0
        GROUP BY q.max_queue_size
    )";
    // ... per queue
}
```

**After:**
```cpp
// OPTIMIZATION: Batch capacity check BEFORE processing partition groups
std::set<std::string> unique_queues;
std::map<std::string, int> queue_batch_sizes;

// Build unique queue list and batch sizes
for (const auto& [partition_key, items] : partition_groups) {
    auto [queue_name, partition_name] = split_partition_key(partition_key);
    unique_queues.insert(queue_name);
    queue_batch_sizes[queue_name] += items.size();
}

// Single query to check ALL queues at once
if (!unique_queues.empty()) {
    std::string queues_array = build_pg_array(std::vector<std::string>(
        unique_queues.begin(), unique_queues.end()
    ));
    
    std::string capacity_check_sql = R"(
        SELECT 
          q.name,
          q.max_queue_size,
          COALESCE(SUM(pc.pending_estimate), 0)::integer as current_depth
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
          AND pc.consumer_group = '__QUEUE_MODE__'
        WHERE q.name = ANY($1::varchar[])
          AND q.max_queue_size > 0
        GROUP BY q.name, q.max_queue_size
    )";
    
    auto capacity_result = QueryResult(conn->exec_params(
        capacity_check_sql, 
        {queues_array}
    ));
    
    // Validate all queues
    for (int i = 0; i < capacity_result.num_rows(); ++i) {
        std::string queue_name = capacity_result.get_value(i, "name");
        int max_size = std::stoi(capacity_result.get_value(i, "max_queue_size"));
        int current_depth = std::stoi(capacity_result.get_value(i, "current_depth"));
        int batch_size = queue_batch_sizes[queue_name];
        
        if (current_depth + batch_size > max_size) {
            throw std::runtime_error(
                "Queue '" + queue_name + "' would exceed max capacity (" +
                std::to_string(max_size) + "). Current: " + 
                std::to_string(current_depth) + ", Batch: " + 
                std::to_string(batch_size)
            );
        }
    }
}
```

---

### Issue #3: Missing Proactive Duplicate Detection

**Severity:** 🟡 **MEDIUM**  
**Impact:** Relies on constraint violations instead of proactive checking  
**Location:** Push operations (lines 420-480, 775-825)

#### Problem

The C++ version doesn't have batch duplicate detection. It relies on database constraints to catch duplicates, which means:
- Wasted database round-trips for duplicates
- Less user-friendly error messages
- No ability to return mixed results (some queued, some duplicate)

#### Node.js Solution (Lines 409-418)

```javascript
// ⚡ OPTIMIZATION #1: Batch duplicate check - single query for all transaction IDs
const allTransactionIds = batch.map(item => item.transactionId || generateUUID());

const hasExplicitTxnIds = batch.some(item => item.transactionId);
let existingTxnIds = new Map();

if (hasExplicitTxnIds) {
    // Use efficient LEFT JOIN approach
    const dupCheck = await client.query(`
      SELECT t.txn_id as transaction_id, m.id
       FROM UNNEST($1::varchar[]) AS t(txn_id)
       LEFT JOIN queen.messages m ON m.transaction_id = t.txn_id
       WHERE m.id IS NOT NULL
    `, [allTransactionIds]);
    
    // Create a map for O(1) lookup
    existingTxnIds = new Map(dupCheck.rows.map(row => 
        [row.transaction_id, row.id]
    ));
}

// Then skip inserting duplicates and return them as 'duplicate' status
```

#### C++ Fix Required

**Location:** `queue_manager.cpp`, before line 784 (batch insert)

Add duplicate check logic:

```cpp
// NEW: Batch duplicate detection
std::vector<std::string> explicit_txn_ids;
std::map<int, bool> has_explicit_txn; // index -> has_explicit

for (size_t i = 0; i < transaction_ids.size(); ++i) {
    if (items[i].transaction_id.has_value() && !items[i].transaction_id->empty()) {
        explicit_txn_ids.push_back(transaction_ids[i]);
        has_explicit_txn[i] = true;
    }
}

std::map<std::string, std::string> existing_txn_map;

if (!explicit_txn_ids.empty()) {
    std::string txn_array = build_pg_array(explicit_txn_ids);
    
    std::string dup_check_sql = R"(
        SELECT t.txn_id as transaction_id, m.id
        FROM UNNEST($1::varchar[]) AS t(txn_id)
        LEFT JOIN queen.messages m ON m.transaction_id = t.txn_id
        WHERE m.id IS NOT NULL
    )";
    
    auto dup_result = QueryResult(conn->exec_params(dup_check_sql, {txn_array}));
    
    for (int i = 0; i < dup_result.num_rows(); ++i) {
        std::string txn_id = dup_result.get_value(i, "transaction_id");
        std::string msg_id = dup_result.get_value(i, "id");
        existing_txn_map[txn_id] = msg_id;
    }
}

// Filter out duplicates from the insert
std::vector<std::string> filtered_message_ids;
std::vector<std::string> filtered_transaction_ids;
std::vector<std::string> filtered_partition_ids;
std::vector<std::string> filtered_payloads;
std::vector<std::string> filtered_trace_ids;
std::vector<std::string> filtered_encrypted_flags;

for (size_t i = 0; i < transaction_ids.size(); ++i) {
    auto it = existing_txn_map.find(transaction_ids[i]);
    if (it != existing_txn_map.end()) {
        // Duplicate - add to results but skip insert
        PushResult duplicate_result;
        duplicate_result.message_id = it->second;
        duplicate_result.transaction_id = transaction_ids[i];
        duplicate_result.status = "duplicate";
        results.push_back(duplicate_result);
        continue;
    }
    
    // Not duplicate - add to insert batch
    filtered_message_ids.push_back(message_ids[i]);
    filtered_transaction_ids.push_back(transaction_ids[i]);
    filtered_partition_ids.push_back(partition_ids[i]);
    filtered_payloads.push_back(payloads[i]);
    filtered_trace_ids.push_back(trace_ids[i]);
    filtered_encrypted_flags.push_back(encrypted_flags[i]);
}

// Only insert non-duplicates
if (!filtered_message_ids.empty()) {
    // Existing UNNEST insert code, but with filtered arrays
    // ...
}
```

---

### Issue #4: No Parallel Encryption

**Severity:** 🟡 **MEDIUM**  
**Impact:** Sequential encryption is slower than parallel  
**Location:** Push operations, encryption handling

#### Problem

The C++ version processes encryption sequentially per message.

#### Node.js Solution (Lines 420-440)

```javascript
// ⚡ OPTIMIZATION #2: Parallel encryption - encrypt all payloads concurrently
const encryptionTasks = batch.map(async (item, idx) => {
    // ... handle each item
    if (encryptionEnabled && encryption.isEncryptionEnabled()) {
      try {
        payload = await encryption.encryptPayload(item.payload);
        isEncrypted = true;
      } catch (error) {
        log(`ERROR: Encryption failed for transaction ${transactionId}:`, error);
      }
    }
    return { messageId, transactionId, payload, isEncrypted };
});

// Wait for all encryption tasks to complete in parallel
const encryptionResults = await Promise.all(encryptionTasks);
```

#### C++ Fix Required

**Note:** C++ doesn't have async/await like Node.js, but we can use thread pools for parallelism.

**Location:** `queue_manager.cpp`, encryption logic in push operations

**Strategy:**

1. **Use existing ThreadPool** (if available) or create one
2. Submit encryption tasks to thread pool
3. Wait for all to complete

```cpp
// In queue_manager.cpp or encryption.cpp
#include <future>
#include <thread>

// Parallel encryption helper
std::vector<EncryptedPayload> encrypt_payloads_parallel(
    const std::vector<nlohmann::json>& payloads,
    bool encryption_enabled
) {
    if (!encryption_enabled) {
        // Return unencrypted
        std::vector<EncryptedPayload> results;
        for (const auto& payload : payloads) {
            results.push_back({payload, false});
        }
        return results;
    }
    
    // Launch parallel encryption tasks
    std::vector<std::future<EncryptedPayload>> futures;
    
    for (const auto& payload : payloads) {
        futures.push_back(std::async(std::launch::async, [payload]() {
            try {
                auto encrypted = encrypt_payload(payload);
                return EncryptedPayload{encrypted, true};
            } catch (const std::exception& e) {
                spdlog::warn("Encryption failed: {}", e.what());
                return EncryptedPayload{payload, false};
            }
        }));
    }
    
    // Collect results
    std::vector<EncryptedPayload> results;
    for (auto& future : futures) {
        results.push_back(future.get());
    }
    
    return results;
}
```

**Note:** This is lower priority since encryption is often disabled in production or uses hardware acceleration.

---

### Issue #5: Inefficient Pop Candidate Search

**Severity:** 🟢 **LOW**  
**Impact:** Multiple queries instead of CTE-based single query  
**Location:** Pop operations (lines 1030-1110)

#### Node.js Solution (Lines 700-750)

Uses a CTE to filter partitions efficiently:

```javascript
WITH eligible_partitions AS (
    SELECT DISTINCT q.id, p.id, ...
    FROM queen.queues q
    JOIN queen.partitions p ON p.queue_id = q.id
    WHERE (q.window_buffer = 0 OR NOT EXISTS (...))
      AND NOT EXISTS (locked check)
)
SELECT ep.*, MIN(m.created_at) as oldest_message
FROM queen.messages m
JOIN eligible_partitions ep ON m.partition_id = ep.partition_id
LEFT JOIN queen.partition_consumers pc ON ep.partition_id = pc.partition_id
WHERE m.id > COALESCE(pc.last_consumed_id, '00000000-0000-0000-0000-000000000000'::uuid)
GROUP BY ep.*
HAVING COUNT(*) > 0
ORDER BY ep.priority DESC, MIN(m.created_at) ASC
```

#### C++ Fix Required

**Location:** `queue_manager.cpp`, pop operations

Replace multiple queries with single CTE-based query similar to Node.js version.

---

## Implementation Plan

### Phase 1: Critical Fixes (Week 1)

**Priority:** 🔴 CRITICAL

1. **Fix #1: Cache Partition IDs**
   - [ ] Create `get_partition_info()` helper method
   - [ ] Refactor `push_messages_internal()` to fetch partition_id once
   - [ ] Refactor all UPDATE/INSERT queries to use cached partition_id
   - [ ] Estimated LOC: ~200 lines changed
   - [ ] Expected improvement: 15-25% reduction in DB queries

2. **Fix #2: Batch Capacity Check**
   - [ ] Add batch capacity check before push operations
   - [ ] Use `ANY($1::varchar[])` pattern
   - [ ] Estimated LOC: ~50 lines added
   - [ ] Expected improvement: N queries → 1 query for multi-queue pushes

### Phase 2: Medium Priority (Week 2)

**Priority:** 🟡 MEDIUM

3. **Fix #3: Proactive Duplicate Detection**
   - [ ] Add batch duplicate check before insert
   - [ ] Return mixed results (queued + duplicate)
   - [ ] Estimated LOC: ~100 lines added
   - [ ] Expected improvement: Better UX, fewer constraint violations

4. **Fix #4: Parallel Encryption** (Optional)
   - [ ] Implement thread-pool based parallel encryption
   - [ ] Only if encryption is actually used in production
   - [ ] Estimated LOC: ~50 lines added

### Phase 3: Optimization (Week 3)

**Priority:** 🟢 LOW

5. **Fix #5: CTE-based Pop Queries**
   - [ ] Refactor pop candidate search to use CTE
   - [ ] Estimated LOC: ~80 lines changed

---

## Testing Strategy

### Unit Tests

For each optimization:

1. **Correctness Test**
   - Verify same results as before optimization
   - Test edge cases (empty batches, single items, etc.)

2. **Performance Test**
   - Measure query count reduction
   - Measure latency improvement

### Integration Tests

1. **Load Test Comparison**
   - Run before/after benchmarks
   - Target: 20-40% throughput improvement

2. **Database Load Test**
   - Monitor PostgreSQL query performance
   - Verify reduction in query count

---

## Success Metrics

| Metric | Current | Target | Verification |
|--------|---------|--------|--------------|
| Queries per PUSH | ~8-12 | ~4-6 | PostgreSQL query logs |
| Partition lookups | N per operation | 1 per operation | Code review + profiling |
| Capacity check queries | N queues | 1 query total | Query logs |
| Duplicate detection | Constraint-based | Proactive | Response format |
| Overall throughput | Baseline | +20-40% | Benchmark tests |

---

## Risk Mitigation

### Low Risk Changes
- ✅ Batch capacity check (pure optimization, no behavior change)
- ✅ Cache partition_id (simple caching, no semantic change)

### Medium Risk Changes
- ⚠️ Duplicate detection (changes response format)
  - **Mitigation:** Add feature flag, gradual rollout
- ⚠️ CTE-based queries (complex query changes)
  - **Mitigation:** Extensive testing, query plan analysis

### High Risk Changes
- 🔴 None - all changes are backwards compatible

---

## Files to Modify

| File | Lines | Priority | Changes |
|------|-------|----------|---------|
| `server/src/managers/queue_manager.cpp` | 573-700 | 🔴 Critical | Batch capacity check |
| `server/src/managers/queue_manager.cpp` | 1147-2140 | 🔴 Critical | Remove partition subqueries |
| `server/src/managers/queue_manager.cpp` | 775-825 | 🟡 Medium | Add duplicate detection |
| `server/include/queen/queue_manager.hpp` | New | 🔴 Critical | Add `get_partition_info()` |
| `server/src/services/encryption.cpp` | New | 🟡 Medium | Parallel encryption (optional) |

---

## Rollout Plan

### Step 1: Development (Week 1-2)
- Implement critical fixes in feature branch
- Add unit tests
- Code review

### Step 2: Staging (Week 3)
- Deploy to staging environment
- Run performance benchmarks
- Monitor PostgreSQL query patterns

### Step 3: Production (Week 4)
- Gradual rollout with monitoring
- Compare metrics to baseline
- Rollback plan ready

---

## Appendix: Query Pattern Comparison

### Pattern 1: Partition Lookup

**Node.js (Optimized):**
```javascript
const resources = await ensureResources(client, queueName, partitionName);
// 1 query, result cached
```

**C++ Current (Inefficient):**
```sql
-- Repeated 15+ times
SELECT p.id FROM queen.partitions p
JOIN queen.queues q ON p.queue_id = q.id
WHERE q.name = $1 AND p.name = $2
```

**C++ Target:**
```cpp
auto partition_info = get_partition_info(conn, queue_name, partition_name);
// 1 query, result reused
```

---

### Pattern 2: Capacity Check

**Node.js (Optimized):**
```sql
-- Single query for ALL queues
WHERE q.name = ANY($1::varchar[])
```

**C++ Current (Inefficient):**
```sql
-- N queries, one per queue
WHERE q.name = $1
```

**C++ Target:**
```sql
-- Match Node.js: single batch query
WHERE q.name = ANY($1::varchar[])
```

---

## Conclusion

Implementing these optimizations will bring the C++ version to performance parity with the Node.js version. The critical fixes alone should provide 20-30% improvement, with medium priority fixes adding another 10-15%.

**Estimated Total Impact:** 30-45% improvement in database efficiency and overall throughput.

