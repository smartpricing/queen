# 🎯 Implementation Plan: Consumer Groups Subscription Metadata Table

## 🐛 Problem Statement

**Current Bug:**
```javascript
// Timeline:
10:00:00 - Consumer group 'cg1' created, subscribes with mode='new'
10:00:00 - Processes partition P1 for 10 minutes...
10:10:00 - New partition P2 is created, messages M1, M2, M3 arrive
10:15:00 - Consumer tries to pop from P2 (first time on this partition)
          - Current code: cursor = NOW() - 4s = 10:14:56
          - Messages M1, M2, M3 from 10:10:00 are SKIPPED! ❌
```

**Root Cause:**
- Current implementation uses `NOW() - (max_poll_interval * 2)` at **lease acquisition time**
- This is wrong for existing consumer groups discovering new partitions
- We need to use the **original subscription time** when the consumer group was **first created**

---

## ✅ Solution: Consumer Groups Metadata Table

Create a new table that tracks when a consumer group first subscribes, storing:
- Consumer group identity (name, queue, partition, namespace, task)
- Subscription mode ('new', 'all', 'timestamp')
- Subscription timestamp (the "birth time" of this consumer group)

**Key Insight:** The subscription timestamp is set **once** when the consumer group first appears, and reused for all future partition discoveries.

---

## 📊 Database Schema

### New Table: `consumer_groups_metadata`

```sql
CREATE TABLE IF NOT EXISTS queen.consumer_groups_metadata (
    consumer_group TEXT NOT NULL,
    queue_name TEXT,              -- NULL for wildcard
    partition_name TEXT,           -- NULL for all partitions
    namespace TEXT,                -- NULL for no namespace filter
    task TEXT,                     -- NULL for no task filter
    subscription_mode TEXT NOT NULL,  -- 'new', 'all', 'timestamp'
    subscription_timestamp TIMESTAMPTZ NOT NULL,  -- When this CG first subscribed
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    PRIMARY KEY (consumer_group, COALESCE(queue_name, ''), COALESCE(partition_name, ''), 
                 COALESCE(namespace, ''), COALESCE(task, ''))
);

CREATE INDEX idx_consumer_groups_metadata_lookup ON queen.consumer_groups_metadata(
    consumer_group, queue_name, namespace, task
);
```

**Why this schema?**

1. **Primary Key Design:**
   - Composite key includes all dimensions (group, queue, partition, namespace, task)
   - `COALESCE(..., '')` handles NULLs in primary key (PostgreSQL requirement)
   - Each unique combination is tracked separately

2. **Partition Handling:**
   - `partition_name = NULL` means "all partitions in this queue"
   - `partition_name = 'p1'` means "only partition p1"
   - When looking up, we search for NULL partition first (most general), then specific partition

3. **Wildcard Support:**
   - `queue_name = NULL, namespace = 'ns'` means "all queues in namespace 'ns'"
   - `queue_name = NULL, task = 't'` means "all queues with task 't'"

---

## 🔄 Flow Changes

### Phase 1: Record Subscription at Pop Time

**Location:** Before calling `acquire_partition_lease` in pop flow

**When:** Every pop request with a consumer group

**Logic:**
```sql
-- UPSERT with DO NOTHING
INSERT INTO queen.consumer_groups_metadata (
    consumer_group, queue_name, partition_name, namespace, task,
    subscription_mode, subscription_timestamp, created_at
) VALUES (
    $1,  -- consumer_group
    $2,  -- queue_name (or NULL)
    $3,  -- partition_name (or NULL if popping from whole queue)
    $4,  -- namespace (or NULL)
    $5,  -- task (or NULL)
    $6,  -- subscription_mode ('new', 'all', 'timestamp')
    $7,  -- subscription_timestamp (calculated based on mode)
    NOW()
) ON CONFLICT (consumer_group, COALESCE(queue_name, ''), COALESCE(partition_name, ''), 
               COALESCE(namespace, ''), COALESCE(task, ''))
DO NOTHING;
```

**Subscription Timestamp Calculation:**
- If `subscription_mode = 'new'`: `NOW() - INTERVAL '(max_poll_interval * 2) milliseconds'`
- If `subscription_mode = 'all'`: `'1970-01-01 00:00:00+00'` (epoch, means process all)
- If `subscription_mode = 'timestamp'`: Use provided `subscriptionFrom` value

**Important:** This INSERT happens **before** acquiring the lease, so the timestamp is set on the **very first pop**.

---

### Phase 2: Query Subscription Metadata in Lease Acquisition

**Location:** `acquire_partition_lease()` function, before setting initial cursor

**When:** Consumer group doesn't exist yet (first time on this partition)

**Logic:**
```cpp
// Step 1: Check if consumer already exists for this partition
if (!consumer_exists) {
    // Step 2: Query subscription metadata
    // Try from most specific to most general
    std::string metadata_sql = R"(
        SELECT subscription_mode, subscription_timestamp
        FROM queen.consumer_groups_metadata
        WHERE consumer_group = $1
          AND (
              -- Exact match: specific queue + specific partition
              (queue_name = $2 AND partition_name = $3)
              OR
              -- Match: specific queue, all partitions
              (queue_name = $2 AND partition_name IS NULL)
              OR
              -- Match: namespace filter (if provided)
              ($4::text IS NOT NULL AND namespace = $4 AND queue_name IS NULL)
              OR
              -- Match: task filter (if provided)
              ($5::text IS NOT NULL AND task = $5 AND queue_name IS NULL)
          )
        ORDER BY 
            -- Prioritize most specific match
            CASE 
                WHEN partition_name IS NOT NULL THEN 1  -- Most specific
                WHEN queue_name IS NOT NULL THEN 2      -- Queue level
                WHEN namespace IS NOT NULL THEN 3       -- Namespace level
                WHEN task IS NOT NULL THEN 4            -- Task level
                ELSE 5
            END
        LIMIT 1
    )";
    
    // Execute query with: (consumer_group, queue_name, partition_name, namespace, task)
    
    // Step 3: Use metadata to set initial cursor
    if (metadata_found) {
        if (subscription_mode == 'new') {
            initial_cursor_id = "00000000-0000-0000-0000-000000000001";
            initial_cursor_timestamp_sql = "'" + subscription_timestamp + "'";
            // Use the ORIGINAL subscription time, not NOW()!
        } else if (subscription_mode == 'all') {
            // Keep default: (00..00, NULL) to process from beginning
        } else if (subscription_mode == 'timestamp') {
            initial_cursor_id = "00000000-0000-0000-0000-000000000001";
            initial_cursor_timestamp_sql = "'" + subscription_timestamp + "'";
        }
    }
}
```

---

## 🗂️ Files to Modify

### 1. Schema Creation (Database)

**File:** `server/src/managers/async_queue_manager.cpp`
**Location:** `initialize_schema()` function (around line 90-250)
**Action:** Add `consumer_groups_metadata` table creation

### 2. Pop Flow - Record Subscription

**File:** `server/src/managers/async_queue_manager.cpp`
**Function:** `pop_messages_from_queue()` (around line 2000-2400)
**Action:** 
- Before calling `acquire_partition_lease()`
- Insert into `consumer_groups_metadata` with DO NOTHING
- Calculate subscription_timestamp based on mode

**OR (better location):**

**Function:** `pop_messages_from_partition()` (around line 1900-2000)
**Action:** Same as above, but at partition level

### 3. Lease Acquisition - Query Subscription

**File:** `server/src/managers/async_queue_manager.cpp`  
**Function:** `acquire_partition_lease()` (around line 1670-1850)
**Action:**
- Remove current `NOW() - max_poll_interval * 2` logic
- Query `consumer_groups_metadata` table
- Use found `subscription_timestamp` instead

### 4. Header File

**File:** `server/include/queen/async_queue_manager.hpp`
**Action:** No changes needed (private functions)

---

## 🧩 Implementation Steps (Detailed)

### Step 1: Create Schema

```cpp
// In initialize_schema(), after partition_consumers table creation

std::string create_cg_metadata_sql = R"(
    CREATE TABLE IF NOT EXISTS queen.consumer_groups_metadata (
        consumer_group TEXT NOT NULL,
        queue_name TEXT,
        partition_name TEXT,
        namespace TEXT,
        task TEXT,
        subscription_mode TEXT NOT NULL,
        subscription_timestamp TIMESTAMPTZ NOT NULL,
        created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
        
        PRIMARY KEY (consumer_group, COALESCE(queue_name, ''), COALESCE(partition_name, ''), 
                     COALESCE(namespace, ''), COALESCE(task, ''))
    )
)";

sendQueryAsync(conn, create_cg_metadata_sql);
getCommandResult(conn);

// Create index
std::string create_cg_index_sql = R"(
    CREATE INDEX IF NOT EXISTS idx_consumer_groups_metadata_lookup 
    ON queen.consumer_groups_metadata(consumer_group, queue_name, namespace, task)
)";

sendQueryAsync(conn, create_cg_index_sql);
getCommandResult(conn);
```

### Step 2: Record Subscription in Pop

**Location:** At the start of `pop_messages_from_partition()` or `pop_messages_from_queue()`

```cpp
// Only for consumer groups (not __QUEUE_MODE__)
if (consumer_group != "__QUEUE_MODE__" && options.subscription_mode.has_value()) {
    std::string sub_mode = options.subscription_mode.value();
    std::string subscription_timestamp_sql;
    
    // Calculate subscription timestamp based on mode
    if (sub_mode == "new" || sub_mode == "new-only") {
        int lookback_ms = config_.max_poll_interval * 2;
        subscription_timestamp_sql = "NOW() - INTERVAL '" + std::to_string(lookback_ms) + " milliseconds'";
    } else if (sub_mode == "all" || sub_mode == "from_beginning") {
        subscription_timestamp_sql = "'1970-01-01 00:00:00+00'";  // Epoch
    } else {
        // timestamp mode - use subscriptionFrom value
        subscription_timestamp_sql = "'" + options.subscription_from.value() + "'";
    }
    
    // Upsert into metadata table (DO NOTHING on conflict)
    std::string upsert_sql = R"(
        INSERT INTO queen.consumer_groups_metadata (
            consumer_group, queue_name, partition_name, namespace, task,
            subscription_mode, subscription_timestamp, created_at
        ) VALUES (
            $1, $2, $3, $4, $5, $6, )" + subscription_timestamp_sql + R"(, NOW()
        ) ON CONFLICT (consumer_group, COALESCE(queue_name, ''), COALESCE(partition_name, ''), 
                       COALESCE(namespace, ''), COALESCE(task, ''))
        DO NOTHING
    )";
    
    std::vector<std::string> params = {
        consumer_group,
        queue_name.empty() ? "" : queue_name,      // NULL handling
        partition_name.empty() ? "" : partition_name,
        namespace_name.empty() ? "" : namespace_name,
        task_name.empty() ? "" : task_name,
        sub_mode
    };
    
    sendQueryParamsAsync(conn, upsert_sql, params);
    getCommandResult(conn);
}
```

### Step 3: Query Subscription in Lease Acquisition

**Location:** In `acquire_partition_lease()`, replace the current NEW mode logic

```cpp
// REMOVE OLD CODE (lines 1718-1728)
// if (sub_mode == "new" || sub_mode == "new-only" || sub_from == "now") {
//     initial_cursor_id = "00000000-0000-0000-0000-000000000001";
//     initial_cursor_timestamp_sql = "NOW() - INTERVAL '" + std::to_string(lookback_ms) + " milliseconds'";
// }

// NEW CODE:
if (consumer_group != "__QUEUE_MODE__") {
    // Query subscription metadata
    std::string metadata_sql = R"(
        SELECT subscription_mode, subscription_timestamp
        FROM queen.consumer_groups_metadata
        WHERE consumer_group = $1
          AND (
              (queue_name = $2 AND partition_name = $3)
              OR (queue_name = $2 AND partition_name IS NULL)
              OR ($4::text IS NOT NULL AND namespace = $4 AND queue_name IS NULL)
              OR ($5::text IS NOT NULL AND task = $5 AND queue_name IS NULL)
          )
        ORDER BY 
            CASE 
                WHEN partition_name IS NOT NULL THEN 1
                WHEN queue_name IS NOT NULL THEN 2
                WHEN namespace IS NOT NULL THEN 3
                WHEN task IS NOT NULL THEN 4
                ELSE 5
            END
        LIMIT 1
    )";
    
    std::vector<std::string> metadata_params = {
        consumer_group,
        queue_name,
        partition_name,
        // TODO: Get namespace from context
        // TODO: Get task from context
    };
    
    sendQueryParamsAsync(conn, metadata_sql, metadata_params);
    auto metadata_result = getTuplesResult(conn);
    
    if (PQntuples(metadata_result.get()) > 0) {
        std::string found_mode = PQgetvalue(metadata_result.get(), 0, 0);
        std::string found_timestamp = PQgetvalue(metadata_result.get(), 0, 1);
        
        if (found_mode == "new" || found_mode == "new-only") {
            initial_cursor_id = "00000000-0000-0000-0000-000000000001";
            initial_cursor_timestamp_sql = "'" + found_timestamp + "'";
            spdlog::debug("Using subscription timestamp from metadata: {}", found_timestamp);
        }
        // else: keep default behavior for 'all' mode
    }
}
```

---

## 🧪 Testing Strategy

### Test 1: Basic NEW Mode - Single Partition
```javascript
await queen.queue('test').partition('p1').create()
await queen.queue('test').partition('p1').push([{data: 'M1'}])

// First pop - creates subscription metadata
const cg = queen.queue('test').partition('p1').group('cg1').subscriptionMode('new')
await cg.pop()  // Gets M1 ✓

// Later, new message
await queen.queue('test').partition('p1').push([{data: 'M2'}])
await cg.pop()  // Gets M2 ✓
```

### Test 2: NEW Mode - New Partition Created Later (THE BUG FIX)
```javascript
// Consumer subscribes at T0
const cg = queen.queue('test').group('cg1').subscriptionMode('new')
await queen.queue('test').partition('p1').create()
await queen.queue('test').partition('p1').push([{data: 'M1'}])
await cg.pop()  // Gets M1, records subscription_timestamp = T0

// 10 minutes later... new partition created
await queen.queue('test').partition('p2').create()
await queen.queue('test').partition('p2').push([{data: 'M2'}])

// Consumer discovers new partition
await cg.pop()  // Should get M2! ✓ (uses T0, not NOW())
```

### Test 3: Namespace Wildcard
```javascript
// Subscribe to namespace
const cg = queen.queue().namespace('ns1').group('cg1').subscriptionMode('new')

// Create queue 1
await queen.queue('q1').namespace('ns1').create()
await queen.queue('q1').push([{data: 'M1'}])
await cg.pop()  // Records metadata for (cg1, namespace=ns1)

// Later, create queue 2 in same namespace
await queen.queue('q2').namespace('ns1').create()
await queen.queue('q2').push([{data: 'M2'}])
await cg.pop()  // Should use same subscription timestamp ✓
```

### Test 4: Multiple Consumer Groups
```javascript
// CG1: NEW mode
const cg1 = queen.queue('test').group('cg1').subscriptionMode('new')
await cg1.pop()  // Records CG1 metadata

// CG2: ALL mode
const cg2 = queen.queue('test').group('cg2')  // Default = all
await cg2.pop()  // Records CG2 metadata (different timestamp)

// They should have independent subscription timestamps
```

---

## ⚠️ Edge Cases & Considerations

### 1. Backward Compatibility

**Existing consumers (created before this change):**
- `consumer_groups_metadata` table won't have entries
- Query returns 0 rows
- Fall back to old behavior? Or default to 'all' mode?

**Solution:** If no metadata found, use default behavior (process from beginning)

### 2. Partition-Specific Subscriptions

```javascript
// User explicitly subscribes to specific partition with NEW mode
queue('q').partition('p1').group('cg').subscriptionMode('new')

// Later subscribes to different partition
queue('q').partition('p2').group('cg').subscriptionMode('new')
```

Should P2 use P1's timestamp? Or get its own?

**Decision:** Store both:
- `(cg, q, NULL)` - queue-level subscription
- `(cg, q, p1)` - partition-specific subscription
- Query logic prioritizes most specific match

### 3. Mixed Mode Subscriptions

```javascript
// First: Subscribe to Q1 with NEW
queue('q1').group('cg').subscriptionMode('new')

// Later: Subscribe to Q2 with ALL
queue('q2').group('cg')  // no mode = all
```

**Decision:** Each (consumer_group, queue) combo has its own entry. This is fine.

### 4. Subscription Mode Changes

```javascript
// First pop with NEW
queue('q1').group('cg').subscriptionMode('new')

// Later pop with ALL (oops, changed mind)
queue('q1').group('cg')  // no mode = all
```

**Decision:** DO NOTHING on conflict = first mode wins. Mode cannot be changed without deleting the metadata.

### 5. Server Default Mode Changes

```bash
# Server starts with default = 'all'
DEFAULT_SUBSCRIPTION_MODE="all"

# Consumer subscribes (no explicit mode)
queue('q1').group('cg')  # Uses server default 'all'

# Server restarts with default = 'new'
DEFAULT_SUBSCRIPTION_MODE="new"

# Same consumer pops again
queue('q1').group('cg')  # Should still use 'all' (from metadata)
```

**Decision:** Metadata persists, so mode doesn't change. This is correct behavior.

---

## 📋 Implementation Checklist

- [ ] **Step 1:** Create `consumer_groups_metadata` table in `initialize_schema()`
- [ ] **Step 2:** Add subscription recording in pop flow (before lease acquisition)
- [ ] **Step 3:** Modify `acquire_partition_lease()` to query metadata instead of using NOW()
- [ ] **Step 4:** Handle namespace/task context (need to pass to acquire_partition_lease)
- [ ] **Step 5:** Test basic NEW mode (single partition)
- [ ] **Step 6:** Test NEW mode with new partition (the bug fix!)
- [ ] **Step 7:** Test namespace/task wildcards
- [ ] **Step 8:** Test multiple consumer groups
- [ ] **Step 9:** Test backward compatibility (no metadata)
- [ ] **Step 10:** Update documentation

---

## 📊 Database Migration

**For existing deployments:**

```sql
-- Run this migration
CREATE TABLE IF NOT EXISTS queen.consumer_groups_metadata (
    consumer_group TEXT NOT NULL,
    queue_name TEXT,
    partition_name TEXT,
    namespace TEXT,
    task TEXT,
    subscription_mode TEXT NOT NULL,
    subscription_timestamp TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    PRIMARY KEY (consumer_group, COALESCE(queue_name, ''), COALESCE(partition_name, ''), 
                 COALESCE(namespace, ''), COALESCE(task, ''))
);

CREATE INDEX IF NOT EXISTS idx_consumer_groups_metadata_lookup 
ON queen.consumer_groups_metadata(consumer_group, queue_name, namespace, task);

-- No data migration needed (table starts empty)
-- Existing consumers will work fine (no metadata = default behavior)
```

---

## 🚀 Rollout Plan

1. ✅ **Phase 1:** Implement and test locally
2. ✅ **Phase 2:** Deploy to staging with migration
3. ✅ **Phase 3:** Monitor for 24 hours
4. ✅ **Phase 4:** Deploy to production
5. ✅ **Phase 5:** Update documentation

---

## 📝 Documentation Updates Needed

- `docs/SUBSCRIPTION_MODES.md` - Update NEW mode behavior explanation
- `client-js/client-v2/README.md` - Update lookback window explanation to mention per-CG tracking
- `website/guide/consumer-groups.md` - Update NEW mode section
- `server/ENV_VARIABLES.md` - Update `QUEUE_MAX_POLL_INTERVAL` description (now only for first pop)

---

## ✅ Success Criteria

1. ✅ NEW mode consumer groups can discover new partitions without skipping messages
2. ✅ Subscription timestamp is consistent across all partitions for a consumer group
3. ✅ Namespace/task wildcards work correctly
4. ✅ Multiple consumer groups have independent timestamps
5. ✅ Backward compatibility maintained (existing consumers work)
6. ✅ No performance regression (one extra INSERT per pop, with DO NOTHING)
7. ✅ Clean, maintainable code

---

## 🎯 Expected Outcome

After implementation:

```javascript
// Timeline:
10:00:00 - Consumer group 'cg1' created, subscribes with mode='new'
          - subscription_timestamp recorded: 10:00:00 - 4s = 09:59:56
10:00:00 - Processes partition P1 for 10 minutes...
10:10:00 - New partition P2 is created, messages M1, M2, M3 arrive
10:15:00 - Consumer tries to pop from P2 (first time on this partition)
          - Queries metadata: finds subscription_timestamp = 09:59:56
          - Cursor set to: 09:59:56 (NOT NOW())
          - Messages M1, M2, M3 from 10:10:00 are CAPTURED! ✅
```

The bug is fixed! 🎉

