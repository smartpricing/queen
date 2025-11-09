# 🎯 Implementation Plan: Fix NEW Subscription Mode First-Message Skip

## Overview
Fix the bug where the first message in a partition is skipped when using `subscriptionMode('new')` by adding a `subscription_started_at` timestamp field to track when consumer groups are created.

---

## 📋 Implementation Checklist

### Phase 1: Database Schema ⭐ CRITICAL

#### File: `server/src/managers/async_queue_manager.cpp`
**Location:** Lines 195-220 (table creation in `initialize_schema`)

**Change:** Add `subscription_started_at` column directly to CREATE TABLE

```cpp
// BEFORE (line 197-198):
last_consumed_id UUID DEFAULT '00000000-0000-0000-0000-000000000000',
last_consumed_created_at TIMESTAMPTZ,

// AFTER:
last_consumed_id UUID DEFAULT '00000000-0000-0000-0000-000000000000',
last_consumed_created_at TIMESTAMPTZ,
subscription_started_at TIMESTAMPTZ DEFAULT NULL,  // NEW FIELD
```

**Purpose:** Store the exact timestamp when a consumer group with NEW mode is created.

**Backward Compatibility:** 
- NULL = traditional mode (use message cursor)
- NOT NULL = NEW mode (use subscription timestamp)

---

### Phase 2: Lease Acquisition Logic ⭐ CRITICAL

#### File: `server/src/managers/async_queue_manager.cpp`
**Location:** Lines 1718-1786 (`acquire_partition_lease` function)

**Current behavior:** For NEW mode, queries latest message and sets cursor TO that message (causing skip)

**Changes needed:**

1. **Lines 1695-1697:** Add subscription time tracking variable
```cpp
// CURRENT:
std::string initial_cursor_id = "00000000-0000-0000-0000-000000000000";
std::string initial_cursor_timestamp_sql = "NULL";

// ADD AFTER LINE 1697:
std::string subscription_time_sql = "NULL";
bool is_new_subscription_mode = false;
```

2. **Lines 1718-1738:** Change NEW mode logic to NOT use message cursor
```cpp
// CURRENT:
if (sub_mode == "new" || sub_mode == "new-only" || sub_from == "now") {
    std::string latest_sql = R"(...)";
    sendQueryParamsAsync(conn, latest_sql, {queue_name, partition_name});
    auto latest_result = getTuplesResult(conn);
    
    if (PQntuples(latest_result.get()) > 0) {
        initial_cursor_id = PQgetvalue(latest_result.get(), 0, 0);  // ❌ CAUSES BUG
        initial_cursor_timestamp_sql = "'" + std::string(PQgetvalue(latest_result.get(), 0, 1)) + "'";
        spdlog::debug("Subscription mode '{}' - starting from latest message: {}", sub_mode, initial_cursor_id);
    }
}

// REPLACE WITH:
if (sub_mode == "new" || sub_mode == "new-only" || sub_from == "now") {
    is_new_subscription_mode = true;
    subscription_time_sql = "NOW()";
    // Don't set message cursor for NEW mode - use timestamp instead
    initial_cursor_id = "00000000-0000-0000-0000-000000000000";
    initial_cursor_timestamp_sql = "NULL";
    spdlog::debug("Subscription mode '{}' - starting from NOW() (subscription timestamp)", sub_mode);
}
```

3. **Lines 1765-1777:** Update INSERT query to include subscription_started_at
```cpp
// CURRENT:
sql = R"(
    INSERT INTO queen.partition_consumers (
        partition_id, consumer_group, lease_expires_at, lease_acquired_at, worker_id,
        last_consumed_id, last_consumed_created_at
    )
    SELECT p.id, $1, NOW() + INTERVAL '1 second' * $2, NOW(), $3, 
           $6::uuid, )" + initial_cursor_timestamp_sql + R"(
    FROM queen.partitions p
    JOIN queen.queues q ON q.id = p.queue_id
    WHERE q.name = $4 AND p.name = $5
    ON CONFLICT (partition_id, consumer_group) DO NOTHING
    RETURNING worker_id
)";

// REPLACE WITH:
sql = R"(
    INSERT INTO queen.partition_consumers (
        partition_id, consumer_group, lease_expires_at, lease_acquired_at, worker_id,
        last_consumed_id, last_consumed_created_at, subscription_started_at
    )
    SELECT p.id, $1, NOW() + INTERVAL '1 second' * $2, NOW(), $3, 
           $6::uuid, )" + initial_cursor_timestamp_sql + R"(, )" + subscription_time_sql + R"(
    FROM queen.partitions p
    JOIN queen.queues q ON q.id = p.queue_id
    WHERE q.name = $4 AND p.name = $5
    ON CONFLICT (partition_id, consumer_group) DO NOTHING
    RETURNING worker_id
)";
```

---

### Phase 3: Pop Message Query ⭐ CRITICAL

#### File: `server/src/managers/async_queue_manager.cpp`
**Location:** Lines 1958-1965 (`pop_messages_from_partition` function)

**Current WHERE clause:** Only checks message cursor

**Change:** Add logic to check subscription_started_at OR message cursor

```cpp
// CURRENT:
std::string where_clause = R"(
    WHERE q.name = $1 AND p.name = $2 AND pc.consumer_group = $3
      AND pc.worker_id = $4 AND pc.lease_expires_at > NOW()
      AND (pc.last_consumed_created_at IS NULL 
           OR m.created_at > pc.last_consumed_created_at
           OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
)";

// REPLACE WITH:
std::string where_clause = R"(
    WHERE q.name = $1 AND p.name = $2 AND pc.consumer_group = $3
      AND pc.worker_id = $4 AND pc.lease_expires_at > NOW()
      AND (
          -- NEW mode: Use subscription timestamp
          (pc.subscription_started_at IS NOT NULL 
           AND m.created_at > pc.subscription_started_at)
          OR
          -- Traditional mode: Use message cursor
          (pc.subscription_started_at IS NULL 
           AND (pc.last_consumed_created_at IS NULL 
                OR m.created_at > pc.last_consumed_created_at
                OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id)))
      )
)";
```

**Impact:** This fixes the actual pop query to respect subscription timestamp for NEW mode consumers.

---

### Phase 4: Partition Selection Queries ⚠️ IMPORTANT

#### File: `server/src/managers/async_queue_manager.cpp`
**Locations:** 
- Lines 2167-2169 (with window_buffer)
- Lines 2187-2189 (without window_buffer)

**Function:** `pop_messages_from_queue` - finds which partition has available messages

**Change 1 - Lines 2167-2169:**
```cpp
// CURRENT:
AND (pc.last_consumed_created_at IS NULL
     OR m.created_at > pc.last_consumed_created_at
     OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))

// REPLACE WITH:
AND (
    (pc.subscription_started_at IS NOT NULL 
     AND m.created_at > pc.subscription_started_at)
    OR
    (pc.subscription_started_at IS NULL
     AND (pc.last_consumed_created_at IS NULL
          OR m.created_at > pc.last_consumed_created_at
          OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id)))
)
```

**Change 2 - Lines 2187-2189:** Same replacement as above

**Impact:** Ensures partition selection correctly counts available messages for NEW mode consumers.

---

### Phase 5: Analytics Queries ⚠️ IMPORTANT

#### File: `server/src/managers/analyticsManager.cpp`
**Multiple locations:** Used for dashboards, lag calculations, pending counts

**Pattern to find:** Search for all occurrences of:
```cpp
pc.last_consumed_created_at IS NULL 
OR m.created_at > pc.last_consumed_created_at
OR (... AND m.id > pc.last_consumed_id)
```

**Known locations:**
1. Lines 367-370 (`get_queues` - unconsumed_count)
2. Lines 378-381 (`get_queues` - processing_count)
3. Lines 563-566 (similar pattern)
4. Lines 574-577 (similar pattern)
5. Lines 635-638 (similar pattern)
6. Lines 646-649 (similar pattern)
7. Lines 704-707 (`get_system_overview` - unconsumed_messages)
8. Lines 715-718 (`get_system_overview` - processing_messages)
9. Lines 729-732 (`get_system_overview` - completed_messages - inverse logic)
10. Lines 747-750 (`get_system_overview` - unconsumed_time_lags)
11. Lines 761-764 (`get_system_overview` - partition_offset_lags)
12. Lines 2321-2324 (`get_consumer_groups` - offset_lag)
13. Lines 2332-2336 (`get_consumer_groups` - time_lag_seconds)

**Replacement pattern for all:**
```sql
-- INSTEAD OF:
pc.last_consumed_created_at IS NULL 
OR m.created_at > pc.last_consumed_created_at
OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
    AND m.id > pc.last_consumed_id)

-- USE:
(pc.subscription_started_at IS NOT NULL 
 AND m.created_at > pc.subscription_started_at)
OR
(pc.subscription_started_at IS NULL
 AND (pc.last_consumed_created_at IS NULL 
      OR m.created_at > pc.last_consumed_created_at
      OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
          AND m.id > pc.last_consumed_id)))
```

**Special case - Completed messages (lines 729-732):** Inverse logic
```sql
-- CURRENT (messages that ARE consumed):
dlq.message_id IS NULL
AND pc.last_consumed_created_at IS NOT NULL 
AND (m.created_at < pc.last_consumed_created_at
    OR (... AND m.id <= pc.last_consumed_id))

-- REPLACE WITH:
dlq.message_id IS NULL
AND (
    (pc.subscription_started_at IS NOT NULL
     AND pc.last_consumed_created_at IS NOT NULL
     AND m.created_at <= pc.last_consumed_created_at)
    OR
    (pc.subscription_started_at IS NULL
     AND pc.last_consumed_created_at IS NOT NULL 
     AND (m.created_at < pc.last_consumed_created_at
         OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
             AND m.id <= pc.last_consumed_id)))
)
```

#### File: `server/src/managers/async_queue_manager.cpp`
**Location:** Lines 1009-1013 (queue depth calculation in `get_queues`)

**Same pattern replacement as analyticsManager.cpp**

---

### Phase 6: Documentation Updates 📝

#### File: `docs/SUBSCRIPTION_MODES.md`

**Add note about implementation details:**

After line 285 (in "Database Schema" section), add:

```markdown
**Implementation Note:**

For NEW subscription mode (`subscriptionMode('new')`), the system uses a `subscription_started_at` 
timestamp rather than a message cursor. This ensures the first message after subscription is not skipped.

- **Traditional mode:** `subscription_started_at IS NULL`, uses `last_consumed_id` cursor
- **NEW mode:** `subscription_started_at` set to consumer creation time, filters `m.created_at > subscription_started_at`

The `last_consumed_id/created_at` fields are still updated as messages are consumed, but the 
`subscription_started_at` timestamp remains constant as the "watermark" for what's considered "new".
```

---

## 🧪 Testing Plan

### Test 1: NEW Mode - Empty Partition
```javascript
// Setup
await client.queue('test-new-empty').partition('p1').create()

// Test
const consumer = client.queue('test-new-empty').partition('p1')
  .group('cg1').subscriptionMode('new')

// First pop - should return 0 messages
const messages1 = await consumer.pop()
assert(messages1.length === 0)

// Push message
await client.queue('test-new-empty').partition('p1').push([{data: 'M1'}])

// Second pop - should return M1 ✓
const messages2 = await consumer.pop()
assert(messages2.length === 1)
assert(messages2[0].data === 'M1')
```

### Test 2: NEW Mode - Partition With History
```javascript
// Setup
await client.queue('test-new-history').partition('p1').create()
await client.queue('test-new-history').partition('p1').push([
  {data: 'M1'}, {data: 'M2'}, {data: 'M3'}
])

// Test - create consumer after messages exist
const consumer = client.queue('test-new-history').partition('p1')
  .group('cg1').subscriptionMode('new')

// First pop - should return 0 messages (skip history)
const messages1 = await consumer.pop()
assert(messages1.length === 0)

// Push new message
await client.queue('test-new-history').partition('p1').push([{data: 'M4'}])

// Second pop - should return M4 only ✓
const messages2 = await consumer.pop()
assert(messages2.length === 1)
assert(messages2[0].data === 'M4')
```

### Test 3: Traditional Mode - Backward Compatibility
```javascript
// Test that traditional mode still works (no subscriptionMode specified)
await client.queue('test-traditional').partition('p1').create()
await client.queue('test-traditional').partition('p1').push([
  {data: 'M1'}, {data: 'M2'}
])

const consumer = client.queue('test-traditional').partition('p1')
  .group('cg-old')
  // No subscriptionMode = traditional

// Should get ALL messages from beginning
const messages = await consumer.batch(10).pop()
assert(messages.length === 2)
assert(messages[0].data === 'M1')
assert(messages[1].data === 'M2')
```

### Test 4: Server Default NEW Mode
```bash
# Start server with default
DEFAULT_SUBSCRIPTION_MODE="new" ./bin/queen-server
```

```javascript
// Consumer without explicit mode should use server default
await client.queue('test-default').partition('p1').create()
await client.queue('test-default').partition('p1').push([{data: 'M1'}])

const consumer = client.queue('test-default').partition('p1')
  .group('cg-default')
  // No subscriptionMode = uses server default (new)

const messages = await consumer.pop()
assert(messages.length === 0) // Should skip M1 (server default is NEW)

// New message should be consumed
await client.queue('test-default').partition('p1').push([{data: 'M2'}])
const messages2 = await consumer.pop()
assert(messages2.length === 1)
assert(messages2[0].data === 'M2')
```

### Test 5: Dashboard Metrics
```javascript
// Verify analytics show correct pending counts
await client.queue('test-metrics').partition('p1').create()
await client.queue('test-metrics').partition('p1').push([
  {data: 'M1'}, {data: 'M2'}, {data: 'M3'}
])

// Create NEW mode consumer
const consumer = client.queue('test-metrics').partition('p1')
  .group('cg-metrics').subscriptionMode('new')
await consumer.pop() // Creates consumer

// Check dashboard
const analytics = await client.getAnalytics()
// Should show 0 pending for this consumer (M1-M3 are "historical")
```

### Test 6: Mixed Consumers
```javascript
// Same partition, different consumer groups, different modes
await client.queue('test-mixed').partition('p1').create()
await client.queue('test-mixed').partition('p1').push([{data: 'M1'}])

// Consumer A: NEW mode
const consumerA = client.queue('test-mixed').partition('p1')
  .group('cg-new').subscriptionMode('new')
const msgsA = await consumerA.pop()
assert(msgsA.length === 0) // Skips M1

// Consumer B: Traditional mode
const consumerB = client.queue('test-mixed').partition('p1')
  .group('cg-traditional').subscriptionMode('from_beginning')
const msgsB = await consumerB.pop()
assert(msgsB.length === 1) // Gets M1

// New message
await client.queue('test-mixed').partition('p1').push([{data: 'M2'}])

// Both should get M2
const msgsA2 = await consumerA.pop()
assert(msgsA2.length === 1 && msgsA2[0].data === 'M2')

const msgsB2 = await consumerB.pop()
assert(msgsB2.length === 1 && msgsB2[0].data === 'M2')
```

---

## ✅ Verification Steps

### 1. Database Schema Check
```sql
-- After server starts, verify column exists
\d queen.partition_consumers

-- Should show:
-- subscription_started_at | timestamp with time zone | | default NULL
```

### 2. Check NEW Mode Consumer Creation
```sql
-- Create a NEW mode consumer, then check:
SELECT 
    consumer_group,
    last_consumed_id,
    last_consumed_created_at,
    subscription_started_at
FROM queen.partition_consumers
WHERE consumer_group = 'test-new-mode';

-- For NEW mode, should see:
-- last_consumed_id = '00000000-0000-0000-0000-000000000000'
-- last_consumed_created_at = NULL
-- subscription_started_at = <timestamp of creation>
```

### 3. Check Traditional Mode Consumer
```sql
SELECT 
    consumer_group,
    subscription_started_at
FROM queen.partition_consumers
WHERE consumer_group = 'test-traditional';

-- For traditional mode, should see:
-- subscription_started_at = NULL
```

### 4. Monitor Server Logs
```bash
LOG_LEVEL=debug ./bin/queen-server

# Look for:
# "Subscription mode 'new' - starting from NOW() (subscription timestamp)"
# NOT: "Subscription mode 'new' - starting from latest message: <uuid>"
```

---

## 📦 Summary of Files to Modify

| File | Changes | Lines | Priority |
|------|---------|-------|----------|
| `server/src/managers/async_queue_manager.cpp` | Schema: Add column | ~198 | ⭐ CRITICAL |
| `server/src/managers/async_queue_manager.cpp` | Lease: NEW mode logic | 1718-1786 | ⭐ CRITICAL |
| `server/src/managers/async_queue_manager.cpp` | Pop: WHERE clause | 1958-1965 | ⭐ CRITICAL |
| `server/src/managers/async_queue_manager.cpp` | Selection: 2 locations | 2167-2169, 2187-2189 | ⚠️ HIGH |
| `server/src/managers/async_queue_manager.cpp` | Depth calc | 1009-1013 | ⚠️ HIGH |
| `server/src/managers/analyticsManager.cpp` | Analytics: ~13 locations | Various | ⚠️ HIGH |
| `docs/SUBSCRIPTION_MODES.md` | Documentation | ~285 | 📝 LOW |

**Total:** 2 files for code changes, 1 file for docs

---

## 🚀 Implementation Order

1. ✅ **Database schema** (add column to CREATE TABLE)
2. ✅ **Lease acquisition** (stop using message cursor for NEW mode)
3. ✅ **Pop WHERE clause** (respect subscription_started_at)
4. ✅ **Partition selection** (2 queries)
5. ✅ **Analytics queries** (~14 locations)
6. ✅ **Queue depth calculation** (1 location)
7. ✅ **Documentation** (SUBSCRIPTION_MODES.md)
8. ✅ **Testing** (run all 6 test scenarios)
9. ✅ **Verification** (SQL checks, log monitoring)

---

## 🐛 Bug Description

**Current behavior:** When using `subscriptionMode('new')`, the first message in a partition is skipped.

**Root cause:** In `acquire_partition_lease()`, when NEW mode is specified and a message exists in the partition:
1. The code queries for the latest message
2. Sets `last_consumed_id` TO that message's ID
3. Sets `last_consumed_created_at` TO that message's timestamp
4. The pop query then looks for messages where `m.id > last_consumed_id`
5. This excludes the message that was used as the cursor (skipped!)

**Why it happens:** The partition_consumer row is created on the first pop request. If a message triggered that first pop, the message itself becomes the cursor and is never consumed.

**Solution:** Use a `subscription_started_at` timestamp instead of a message cursor for NEW mode. This timestamp records WHEN the consumer was created, not WHICH message was latest. Messages are then filtered by `m.created_at > subscription_started_at`, which doesn't skip any messages.

