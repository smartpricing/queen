# SMART_POLL: Intelligent Pre-Flight Partition Availability Check

## 🎯 Executive Summary

**Problem**: Current long-polling architecture executes expensive partition availability queries (46ms avg) for EVERY waiting client, resulting in 60% wasted queries that return 0 rows.

**Solution**: Execute **ONE SINGLE pre-flight query per poll worker cycle** that checks ALL queues, ALL partitions, and ALL consumer groups simultaneously for ALL waiting intentions. Then intelligently decide which clients should attempt to pop messages, respecting the fundamental constraint: **one partition + one consumer group = one lease at a time**.

**Key Innovation**: Instead of N queries for N groups, execute 1 query for ALL intentions using SQL array parameters (`ANY($array)`) - the database checks all combinations in a single scan.

**Impact**: 
- Reduce query volume by 31% (25k → 17k queries/hour)
- Reduce DB time by 53-56% (19.2min → 8.5-9min per hour)
- Eliminate 99% of expensive "empty result" queries
- Faster response times for clients (no wasted attempts)
- Deterministic partition assignment eliminates race conditions

**Architecture**: No feature flags - this is a direct replacement of the current approach with full commitment to the new design.

---

## 📊 Current Architecture Problems

### Issue 1: Redundant Queries
```
Group: "orders:*:__QUEUE_MODE__" with 3 waiting clients
Current behavior:
├─ Client 1: Execute expensive query → finds 2 partitions available → gets partition-0
├─ Client 2: Execute expensive query → finds 1 partition available → gets partition-1  
└─ Client 3: Execute expensive query → finds 0 partitions available → returns empty ❌

Result: 3 queries @ 46ms = 138ms DB time
Waste: 1 completely unnecessary query (33%)
```

### Issue 2: Blind Execution
- Poll workers have no visibility into partition availability before attempting pop
- Each client independently discovers the same information
- No coordination between intentions in the same group
- Results in race conditions and wasted cycles

### Issue 3: High Empty Query Rate
- **99.8% of queries return 0 rows** (avg 0.002 rows returned)
- These queries are still expensive (46ms avg) due to:
  - 4-way JOIN (queues, partitions, messages, partition_consumers)
  - COUNT aggregation
  - GROUP BY + HAVING + ORDER BY
  - All for empty result sets

---

## 🚀 Proposed Solution: SMART_POLL

### Core Concept

Before executing any `pop_messages_from_queue()` or `pop_messages_from_partition()` calls, execute **ONE SINGLE pre-flight query per poll worker cycle** that returns partition availability for **ALL intentions across ALL groups** at once.

**Why one query for everything?**
- The SQL query already accepts arrays: `ANY($2::text[])` for consumer groups, `ANY($3::text[])` for queue names
- Database can check all combinations in a single scan
- Result set is small (only viable partitions, not all messages)
- More efficient than N separate queries for N groups

### Key Principles

1. **Single Query Per Poll Cycle**: ONE pre-flight query for ALL intentions, not per group or per intention
2. **Lease Semantics Preservation**: NEVER assign same partition+consumer_group to multiple requests
3. **Deterministic Assignment**: For "any partition" requests, assign specific partition based on availability
4. **Skip Unviable Requests**: Don't attempt pop if pre-flight shows no available partitions
5. **Full Commitment**: No feature flags - this is the new architecture

---

## 🏗️ Architecture Overview

### Current Flow
```
Poll Worker → Group Intentions → For Each Group:
                                   └─ For Each Intention in Group:
                                       ├─ Execute pop_messages_from_queue()
                                       │  └─ SQL: Find available partitions (46ms)
                                       │  └─ SQL: Acquire lease & fetch messages
                                       └─ Result: messages or empty
                                   
Result: N intentions = N expensive queries (many return 0 rows)
```

### New SMART_POLL Flow
```
Poll Worker → Get All Active Intentions → Execute SINGLE Pre-Flight Query (15-20ms)
                                           └─ Checks ALL queues + partitions + groups at once
                                           └─ Returns: Complete partition availability map
                                       → Decision Engine:
                                           ├─ Match intentions to available partitions
                                           ├─ Assign partitions deterministically
                                           └─ Filter out unviable requests
                                       → For Each Viable Intention:
                                           └─ Execute targeted pop operation
                                              └─ Skip expensive "find partition" step
```

---

## 📝 Implementation Plan

### Phase 1: Pre-Flight Query Implementation

#### 1.1: Create Pre-Flight Query Function

**New Function**: `AsyncQueueManager::check_partition_availability()`

**Input**: Vector of intentions to check
**Output**: PartitionAvailabilityMap structure

```cpp
struct PartitionAvailability {
    std::string queue_name;
    int partition_id;
    std::string partition_name;
    std::string consumer_group;
    int message_count;
    bool lease_available;
    bool not_in_window_buffer;
    bool has_unconsumed_messages;
};

struct AvailabilityResult {
    std::vector<PartitionAvailability> partitions;
    std::map<std::string, int> available_count_by_group;
};

AvailabilityResult check_partition_availability(
    const std::vector<PollIntention>& intentions
);
```

#### 1.2: Pre-Flight SQL Query

```sql
-- Single query that checks ALL relevant partitions for ALL intentions
SELECT 
    q.name AS queue_name,
    p.id AS partition_id,
    p.name AS partition_name,
    COALESCE(pc.consumer_group, $1) AS consumer_group,
    COUNT(m.id) AS message_count,
    CASE 
        WHEN pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW() 
        THEN true 
        ELSE false 
    END AS lease_available,
    CASE 
        WHEN q.window_buffer > 0 AND EXISTS(
            SELECT 1 FROM queen.messages m2
            WHERE m2.partition_id = p.id
              AND m2.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
        ) THEN false
        ELSE true
    END AS not_in_window_buffer,
    CASE
        WHEN pc.last_consumed_created_at IS NULL THEN true
        WHEN EXISTS(
            SELECT 1 FROM queen.messages m3
            WHERE m3.partition_id = p.id
              AND (m3.created_at > pc.last_consumed_created_at
                   OR (m3.created_at = pc.last_consumed_created_at 
                       AND m3.id > pc.last_consumed_id))
        ) THEN true
        ELSE false
    END AS has_unconsumed_messages
FROM queen.queues q
JOIN queen.partitions p ON p.queue_id = q.id
LEFT JOIN queen.partition_consumers pc ON 
    pc.partition_id = p.id 
    AND pc.consumer_group = ANY($2::text[])
LEFT JOIN queen.messages m ON 
    m.partition_id = p.id
    AND (pc.last_consumed_created_at IS NULL
         OR m.created_at > pc.last_consumed_created_at
         OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
WHERE 
    q.name = ANY($3::text[])
    AND (
        $4::text[] IS NULL
        OR p.name = ANY($4::text[])
    )
GROUP BY 
    q.name, q.window_buffer, p.id, p.name, 
    pc.consumer_group, pc.lease_expires_at, 
    pc.last_consumed_created_at, pc.last_consumed_id
ORDER BY 
    q.name, pc.consumer_group, COUNT(m.id) DESC, p.name;
```

**Parameters**:
- `$1`: Default consumer group (fallback when no consumer exists yet)
- `$2`: Array of consumer_groups from intentions `["__QUEUE_MODE__", "analytics"]`
- `$3`: Array of queue_names from intentions `["orders", "notifications"]`
- `$4`: Array of specific partition_names (or NULL for all) `["partition-0", "partition-1"]`

**Expected Performance**: 10-15ms (vs 46ms × N current)

---

### Phase 2: Decision Engine Implementation

#### 2.1: Create Smart Decision Engine

**Function**: `decide_viable_intentions()`

**Input**: 
- Intentions grouped by key
- PartitionAvailabilityMap from pre-flight

**Output**: 
- Map of intention → assigned partition (or SKIP)

**Decision Logic**:

```
For each group of intentions:

1. Extract available partitions for this group from pre-flight results
   - Filter by: lease_available = true
   - Filter by: not_in_window_buffer = true
   - Filter by: has_unconsumed_messages = true
   - Sort by: message_count DESC (most messages first)

2. Categorize intentions:
   a) Specific partition requests: partition_name.has_value()
   b) Any partition requests: !partition_name.has_value()

3. Process SPECIFIC partition requests first:
   For each specific partition request (in order of deadline):
   - Check if requested partition is available
   - If yes: Mark as VIABLE, reserve partition
   - If no: Mark as SKIP
   - Remove partition from available pool

4. Process ANY partition requests:
   For each any-partition request (in order of deadline):
   - If available partitions remain:
     - Assign highest-count partition (most likely to succeed)
     - Mark as VIABLE with assigned_partition_name
     - Remove partition from available pool
   - If no partitions remain:
     - Mark as SKIP

5. Return decision map
```

**CRITICAL CONSTRAINT**: Once a partition is assigned to a request, it MUST NOT be assigned to another request in the same group. This preserves lease semantics.

#### 2.2: Decision Data Structure

```cpp
enum class IntentionDecision {
    SKIP_NO_PARTITIONS,           // No available partitions
    SKIP_PARTITION_UNAVAILABLE,   // Specific partition not available
    SKIP_LEASE_TAKEN,             // Partition lease already taken
    SKIP_WINDOW_BUFFER,           // Partition in window_buffer cooldown
    VIABLE_ASSIGNED,              // Viable, partition assigned
    VIABLE_ANY                    // Viable, pop any partition (legacy)
};

struct IntentionExecutionPlan {
    std::string request_id;
    IntentionDecision decision;
    std::optional<std::string> assigned_partition_name;
    std::string reason;
};

std::vector<IntentionExecutionPlan> decide_viable_intentions(
    const std::vector<PollIntention>& intentions,
    const AvailabilityResult& availability
);
```

---

### Phase 3: Optimized Pop Execution

#### 3.1: Modified Poll Worker Logic

**Current** (`poll_worker.cpp` line 170-340):
```cpp
// Get all intentions
auto all_intentions = registry->get_active_intentions();

// Group by (queue:partition:consumer_group)
auto grouped = group_intentions(my_intentions);

// Process each group
for (auto& [key, batch] : grouped) {
    // ... rate limiting checks ...
    
    // Process ALL intentions in this group sequentially
    for (const auto& intention : batch) {
        // Each intention executes expensive query
        result = async_queue_manager->pop_messages_from_queue(...);
    }
}
```

**New** (SMART_POLL):
```cpp
// Get all intentions from registry
auto all_intentions = registry->get_active_intentions();

if (all_intentions.empty()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_worker_interval_ms));
    continue;
}

// ========================================
// STEP 1: Execute SINGLE pre-flight check for ALL intentions
// ========================================
auto availability = async_queue_manager->check_partition_availability(all_intentions);

spdlog::debug("Poll worker {} pre-flight checked {} intentions, found {} available partitions",
             worker_id, all_intentions.size(), availability.partitions.size());

// ========================================
// STEP 2: Group intentions and create execution plan
// ========================================
auto grouped = group_intentions(all_intentions);
std::vector<IntentionExecutionPlan> execution_plans;

for (auto& [group_key, batch] : grouped) {
    // Rate limiting per group (still useful for pacing)
    if (!should_process_group(group_key, last_query_times, backoff_states)) {
        continue;
    }
    
    // Get execution plan for this group's intentions
    auto group_plans = decide_viable_intentions(batch, availability, group_key);
    execution_plans.insert(execution_plans.end(), group_plans.begin(), group_plans.end());
    
    // Update last query time
    last_query_times[group_key] = std::chrono::steady_clock::now();
}

// ========================================
// STEP 3: Execute only viable intentions
// ========================================
int successful_pops = 0;
int skipped_pops = 0;

for (const auto& plan : execution_plans) {
    if (plan.decision != IntentionDecision::VIABLE_ASSIGNED) {
        spdlog::debug("Skipping intention {} - {}", plan.request_id, plan.reason);
        skipped_pops++;
        continue;
    }
    
    // Find corresponding intention
    auto intention = find_intention_by_id(all_intentions, plan.request_id);
    
    PopResult result;
    
    if (plan.assigned_partition_name.has_value()) {
        // OPTIMIZATION: Deterministic partition assignment
        // Skip expensive "find available partition" query
        result = async_queue_manager->pop_messages_from_partition(
            intention.queue_name.value(),
            *plan.assigned_partition_name,
            intention.consumer_group,
            opts,
            true  // skip_availability_check = true
        );
    } else {
        // Should not happen with SMART_POLL, but handle gracefully
        spdlog::warn("No partition assigned for viable intention {}", plan.request_id);
        continue;
    }
    
    if (!result.messages.empty()) {
        send_to_single_client(intention, result, worker_response_queues);
        registry->remove_intention(intention.request_id);
        successful_pops++;
    }
}

spdlog::info("Poll worker {} processed {} intentions: {} successful, {} skipped",
            worker_id, execution_plans.size(), successful_pops, skipped_pops);
```

#### 3.2: Optimized pop_messages_from_partition()

When partition is pre-assigned from SMART_POLL decision engine, we can **skip** the expensive availability check in `pop_messages_from_partition()` because we already know:
- Partition exists
- Has messages
- Lease is available
- Not in window_buffer cooldown

Add parameter: `skip_availability_check=true` to bypass the query at lines 2048-2066 in `async_queue_manager.cpp`.

---

### Phase 4: Deterministic Partition Assignment (Option A)

For "any partition" requests, instead of executing `pop_messages_from_queue()` which internally runs the expensive "find available partition" query, we:

1. **Pre-flight identifies available partitions** with message counts
2. **Decision engine assigns specific partition** based on:
   - Highest message count (most likely to succeed)
   - Fair distribution if multiple requests
3. **Execute targeted** `pop_messages_from_partition(partition_name, skip_availability_check=true)`

**Benefits**:
- Eliminates the "find available partition" query entirely (lines 2290-2340)
- Deterministic behavior (no race conditions)
- Better load distribution across partitions

**Example**:
```
Pre-flight results for "orders:*:__QUEUE_MODE__":
  - partition-0: 100 messages
  - partition-1: 50 messages
  - partition-2: 10 messages

Intentions: [req1, req2, req3]

Assignment:
  req1 → partition-0 (100 msgs)
  req2 → partition-1 (50 msgs)
  req3 → partition-2 (10 msgs)

Each executes: pop_messages_from_partition(assigned_partition, skip_check=true)
```

---

## 🔒 Critical Constraints & Safety

### 1. Lease Semantics Preservation

**MUST NEVER**: Assign same `(partition_id, consumer_group)` to multiple requests.

**Implementation Safety**:
```cpp
// Track assigned partitions within batch processing
std::set<std::string> assigned_partition_keys;

for (auto& intention : sorted_intentions) {
    std::string partition_key = partition_name + ":" + consumer_group;
    
    if (assigned_partition_keys.count(partition_key)) {
        // Already assigned in this batch - SKIP
        continue;
    }
    
    // Assign partition
    assigned_partition_keys.insert(partition_key);
}
```

### 2. Window Buffer Respect

Pre-flight query MUST check window_buffer settings and exclude partitions that received messages within the buffer window.

### 3. Subscription Mode Support

Pre-flight query MUST respect subscription modes:
- `new`: Only messages after `subscription_from` timestamp
- `all`: All unconsumed messages
- `timestamp`: Messages after specific timestamp

### 4. Delayed Processing

Pre-flight should consider `delayed_processing` queue setting (though this is typically checked during actual pop).

### 5. Priority Ordering

When multiple requests compete for limited partitions, prioritize by:
1. Specific partition requests first (explicit intent)
2. Oldest intention first (fairness - line 913 `created_at`)
3. Highest message count partition (likelihood of success)

---

## 📈 Expected Performance Impact

### Current State (Real Data)
- **25,000 queries/hour**
- **46ms average execution time**
- **0.002 average rows returned** (99.8% empty)
- **Total DB time: 1,150 seconds/hour** (19.2 minutes)

### After SMART_POLL Implementation

#### With Single Pre-Flight Query Per Poll Cycle

Assumptions:
- Poll worker cycles every 50ms, but rate-limited per group to 500ms min
- Average 10-20 active intentions at any time across all groups
- One pre-flight query checks ALL intentions simultaneously

**Query Breakdown**:
- **Pre-flight queries: ~7,200/hour** (one every 500ms when intentions exist)
  - 15-20ms each (checking all queues/partitions/groups at once)
  - Total: 108-144 seconds/hour
- **Targeted pop queries: ~10,000/hour** (only viable intentions)
  - 40ms each (with skip_availability_check=true)
  - Total: 400 seconds/hour
- **Total DB time: 508-544 seconds/hour** (8.5-9 minutes)
- **Query volume: 17,200 total** (vs 25,000 current)

**Improvements**:
- **Query reduction: 31%** (25,000 → 17,200)
- **DB time reduction: 53-56%** (1,150s → 508-544s)
- **Empty query elimination: 99%** (nearly all skipped via pre-flight)
- **Per-intention efficiency**: 1 shared pre-flight + 1 targeted pop (vs 1-3 blind attempts current)

### Per-Request Latency Improvement
- **Current**: Wait → 46ms empty query → wait → 46ms empty query → ...
- **New**: Wait → 15ms pre-flight (group shared) → 40ms targeted pop
- **Improvement**: ~50-70ms faster per successful pop

---

## 🧪 Testing Strategy

### Unit Tests

1. **Test: Pre-flight query correctness**
   - Setup: Create queues, partitions, messages, consumer state
   - Verify: Query returns accurate availability

2. **Test: Decision engine lease safety**
   - Input: 5 intentions, 2 available partitions
   - Verify: Only 2 intentions marked viable, no partition assigned twice

3. **Test: Deterministic partition assignment**
   - Input: 3 any-partition intentions, partitions with 100/50/10 messages
   - Verify: Assigned in order: 100, 50, 10

4. **Test: Specific partition priority**
   - Input: Mix of specific and any-partition intentions
   - Verify: Specific requests processed first

5. **Test: Window buffer respect**
   - Setup: Queue with window_buffer=5, recent messages
   - Verify: Pre-flight excludes partitions in cooldown

### Integration Tests

1. **Test: End-to-end SMART_POLL flow**
   - Simulate 10 concurrent clients on same queue
   - Verify: Pre-flight executes once, correct number of pops

2. **Test: Mixed consumer groups**
   - Setup: Multiple consumer groups on same queue
   - Verify: Pre-flight returns separate availability per group

3. **Test: Race condition prevention**
   - Setup: 2 intentions for same specific partition
   - Verify: Only first succeeds, second skipped

4. **Test: Performance benchmark**
   - Measure: Query count before/after
   - Measure: DB time before/after
   - Measure: Client latency before/after

### Load Tests

1. **Test: High concurrency (1000 waiting clients)**
   - Verify: System remains stable
   - Measure: Query reduction percentage

2. **Test: Empty queue behavior**
   - Setup: No messages available
   - Verify: Pre-flight returns empty, all intentions skipped
   - Measure: Time savings vs current behavior

---

## 🚧 Implementation Phases

### Phase 1: Foundation (Week 1)
- [ ] Create `PartitionAvailability` data structures
- [ ] Implement `check_partition_availability()` function
- [ ] Write pre-flight SQL query
- [ ] Unit tests for pre-flight query
- [ ] Performance benchmark pre-flight vs current query

### Phase 2: Decision Engine (Week 2)
- [ ] Implement `decide_viable_intentions()` function
- [ ] Implement partition assignment logic
- [ ] Add lease safety checks
- [ ] Unit tests for decision engine
- [ ] Integration tests for assignment correctness

### Phase 3: Poll Worker Integration (Week 3)
- [ ] Modify `poll_worker_loop()` to use SMART_POLL
- [ ] Replace per-group processing with single pre-flight for all intentions
- [ ] Add `skip_availability_check` parameter to pop functions
- [ ] Update logging and metrics
- [ ] Integration tests for end-to-end flow

### Phase 4: Optimization (Week 4)
- [ ] Implement deterministic partition assignment (Option A)
- [ ] Optimize `pop_messages_from_partition()` with skip flag
- [ ] Add detailed metrics and monitoring
- [ ] Load testing with production-like traffic
- [ ] Documentation and runbook updates

### Phase 5: Deployment (Week 5)
- [ ] Deploy to staging environment
- [ ] Monitor query volume and latency
- [ ] Load testing with production-like traffic patterns
- [ ] Deploy to production (all instances simultaneously)
- [ ] Post-deployment monitoring and tuning
- [ ] Document performance improvements

---

## 🔧 Configuration

### New Environment Variables

```bash
# Pre-flight query timeout (ms)
SMART_POLL_PREFLIGHT_TIMEOUT=50

# Maximum intentions to process in single pre-flight
# (safety limit to prevent unbounded query size)
SMART_POLL_MAX_INTENTIONS=200

# Minimum interval between pre-flight queries (ms)
# Prevents excessive DB queries when intentions queue is active
SMART_POLL_MIN_INTERVAL=100
```

---

## 📊 Monitoring & Metrics

### New Metrics to Track

1. **smart_poll_preflight_queries_total** - Counter of pre-flight queries executed
2. **smart_poll_preflight_duration_ms** - Histogram of pre-flight query latency
3. **smart_poll_preflight_intentions_checked** - Histogram of intentions per pre-flight
4. **smart_poll_preflight_partitions_found** - Histogram of available partitions found
5. **smart_poll_intentions_skipped_total** - Counter by skip reason
6. **smart_poll_intentions_viable_total** - Counter of viable intentions executed
7. **smart_poll_partition_assignments_total** - Counter of deterministic assignments
8. **smart_poll_query_reduction_percentage** - Gauge of query reduction vs baseline
9. **smart_poll_double_assignment_errors** - Counter (should always be 0!)

### Alerting

1. **Alert**: Pre-flight query taking >50ms consistently
2. **Alert**: >80% of intentions being skipped (may indicate config issue)
3. **Alert**: smart_poll_double_assignment_errors > 0 (CRITICAL - lease safety violation)
4. **Alert**: Pre-flight query timing out (>50ms timeout)
5. **Alert**: Large number of intentions in single pre-flight (>150, may need sharding)

---

## 🎯 Success Criteria

### Must Have
- ✅ Query volume reduced by at least 25%
- ✅ No increase in failed pop attempts
- ✅ No lease conflicts or double-assignment bugs
- ✅ All tests passing
- ✅ Pre-flight query completes in <30ms for typical load

### Nice to Have
- ✅ Query volume reduced by 30-40%
- ✅ Average query latency reduced by 20%
- ✅ Client response time improved by 50ms
- ✅ Database CPU usage reduced by 15%

---

## 🚨 Risks & Mitigations

### Risk 1: Pre-flight query becomes bottleneck
**Mitigation**: 
- Timeout pre-flight at 50ms, log warning if exceeded
- Cap maximum intentions per pre-flight (safety limit: 200)
- Monitor pre-flight duration, alert if >30ms avg
- Optimize query with proper indexes on join columns
- Consider sharding pre-flight by queue if query grows too large

### Risk 2: Decision logic bug causes lease conflicts
**Mitigation**:
- Comprehensive unit tests for edge cases
- Extensive staging testing with production-like load
- Monitor for duplicate lease errors in production
- Add assertions in code to detect double-assignment
- Canary deployment to single instance first

### Risk 3: Deterministic assignment creates hot partitions
**Mitigation**:
- Round-robin assignment for partitions with similar message counts
- Monitor partition lease distribution
- Allow configuration to disable deterministic assignment

### Risk 4: Regression in existing functionality
**Mitigation**:
- Comprehensive integration tests covering all scenarios
- Staging environment load testing
- Gradual rollout (staging → single prod instance → all instances)
- Rollback plan: revert commit and redeploy previous version

---

## 📚 References

### Related Files
- `server/src/services/poll_worker.cpp` - Poll worker main loop
- `server/src/managers/async_queue_manager.cpp` - Pop operations (lines 2269-2368)
- `server/include/queen/poll_intention_registry.hpp` - Intention data structures
- `server/include/queen/async_queue_manager.hpp` - Queue manager interface

### Related Documentation
- `docs/LONG_POLLING.md` - Long polling architecture
- `docs/ARCHITECTURE.md` - Overall system architecture
- `server/ARCHITECTURE.md` - Server component details

---

## 📝 Open Questions

1. **Q**: Should pre-flight query consider `delayed_processing` setting?
   **A**: Yes, but as secondary filter during actual pop. Pre-flight focuses on availability.

2. **Q**: How to handle subscription modes in pre-flight?
   **A**: Pre-flight checks basic availability. Subscription filtering happens during actual pop.

3. **Q**: What if pre-flight shows available but pop fails (race condition)?
   **A**: Acceptable rare case. Pre-flight is optimization, not guarantee. Fall back to normal error handling. Pre-flight runs ~100-500ms before pop, so state can change.
   
6. **Q**: Why not use feature flags for safer rollout?
   **A**: Architectural decision - we're committing to this approach. Reduces code complexity and maintenance burden. Safety comes from comprehensive testing and gradual deployment (staging → canary → full prod).

4. **Q**: Should we cache pre-flight results briefly?
   **A**: Future optimization. Start without caching, add if pre-flight becomes bottleneck.

5. **Q**: Partition assignment fairness - should we round-robin?
   **A**: Start with message-count ordering. Add round-robin if monitoring shows unfair distribution.

---

## ✅ Implementation Checklist

### Code Changes
- [ ] `async_queue_manager.hpp` - Add function declarations
- [ ] `async_queue_manager.cpp` - Implement pre-flight query
- [ ] `async_queue_manager.cpp` - Add skip_availability_check parameter
- [ ] `poll_worker.cpp` - Integrate SMART_POLL decision engine
- [ ] `poll_worker.hpp` - Add helper functions for decision logic
- [ ] `config.hpp` - Add SMART_POLL configuration options

### Testing
- [ ] Unit tests for pre-flight query
- [ ] Unit tests for decision engine
- [ ] Integration tests for end-to-end flow
- [ ] Load tests with 1000+ concurrent clients
- [ ] Performance benchmarks

### Documentation
- [ ] Update `LONG_POLLING.md` with SMART_POLL details
- [ ] Update `ARCHITECTURE.md` with new flow diagrams
- [ ] Add inline code comments
- [ ] Update metrics documentation
- [ ] Create runbook for troubleshooting

### Deployment
- [ ] Staging deployment and testing
- [ ] Production monitoring dashboard
- [ ] Canary deployment (single instance)
- [ ] Rollout plan and rollback procedure
- [ ] Full production deployment
- [ ] Post-deployment validation

---

**Status**: 📋 PLANNING
**Owner**: TBD
**Target Completion**: 5 weeks from start
**Priority**: HIGH (Performance optimization with significant impact)

