# Queen Schema Refactoring - Migration Plan (COMMIT)

**Date:** 2025-10-14  
**Status:** DRAFT - Ready for Review  
**Breaking Changes:** YES (requires data migration)

---

## Executive Summary

### What We're Doing
Refactoring Queen's database schema to:
1. **Merge** `partition_cursors` + `partition_leases` → `partition_consumers` (unified cursor+lease)
2. **Remove** `messages_status` table (51 references across codebase)
3. **Remove** `consumer_groups` table (2 references, minimal impact)
4. **Result:** 8 tables → 5 tables (37.5% reduction)

### Why We're Doing It
- **50-75% fewer writes** on ACK operations (especially bus mode)
- **Atomic cursor+lease operations** (no split updates)
- **Cleaner conceptual model** (consumer state unified)
- **Simpler codebase** (fewer JOINs, less complexity)

### Key Risks
- ⚠️ **Breaking change** - requires data migration
- ⚠️ **Lost observability** - no per-message status history
- ⚠️ **Dashboard queries** - must use counters/estimates instead of exact counts
- ⚠️ **Testing effort** - 51 locations using `messages_status` to update

---

## Phase 1: Pre-Migration Analysis ✅ COMPLETE

### 1.1 Schema Analysis ✅
**Current tables:**
- ✅ `messages` (44 lines) - immutable, keep as-is
- ❌ `messages_status` (71 lines) - **REMOVE** (51 references in code)
- ❌ `partition_cursors` (114 lines) - **MERGE** (10 references)
- ❌ `partition_leases` (97 lines) - **MERGE** (12 references)
- ❌ `consumer_groups` (83 lines) - **REMOVE** (2 references)
- ✅ `dead_letter_queue` (126 lines) - keep as-is
- ✅ `queues` (31 lines) - keep as-is
- ✅ `partitions` (41 lines) - keep as-is
- ✅ `retention_history` (224 lines) - keep as-is (optional)

### 1.2 Code Impact Analysis ✅

**Files requiring changes:**

#### **Critical (Core Logic):**
1. `src/database/schema-v2.sql` - Schema definition
2. `src/managers/queueManagerOptimized.js` - Core queue operations (1421 lines)
   - Lines using `partition_cursors`: 532-541, 1099, 1242-1247
   - Lines using `partition_leases`: 486-495, 573-600, 629-640, 1066, 1101, 1182-1206, 1316-1320, 1333-1339
   - Lines using `consumer_groups`: 702, 708
   - Function `acknowledgeMessages()`: Lines 443-619
   - Function `uniquePop()`: Lines 989-1420
   - Function `reclaimExpiredLeases()`: Lines 622-683

#### **High Priority (API/Dashboard):**
3. `src/routes/status.js` - Dashboard status API (959 lines)
   - 10 queries using `messages_status`: lines 85, 122, 284, 379, 543, 555, 690, 713, 750, 905
   - 2 queries using `partition_cursors`: lines 100, 381
   - 2 queries using `partition_leases`: lines 137, 383

4. `src/routes/analytics.js` - Analytics API (812 lines)
   - 5 queries using `messages_status`: lines 387, 415, 442, 503, 551

5. `src/routes/resources.js` - Resource management
   - 8 queries using `messages_status`: lines 23, 93, 163, 211, 239, 263-266

6. `src/routes/messages.js` - Message operations
   - 3 queries using `messages_status`: lines 166, 186, 209

7. `src/websocket/wsServer.js` - Real-time updates
   - 3 queries using `messages_status`: lines 194-197

#### **Medium Priority (Services):**
8. `src/services/evictionService.js` - Message eviction
   - 2 queries using `messages_status`: lines 23, 27

9. `src/services/retentionService.js` - Retention policy
   - Uses status field on messages (but messages table doesn't have status!)
   - Lines 22-47: DELETE based on status (THIS IS A BUG - messages table has no status field!)

#### **Low Priority (Tests):**
10. `src/test/enterprise-tests.js` - 3 references
11. `src/test/advanced-pattern-tests.js` - 1 reference
12. `src/test/edge-case-tests.js` - 1 reference
13. `src/test/test.js` - 13 references
14. `src/test/bus-mode-tests.js` - 2 references

---

## Phase 2: Create New Schema (Migration Script)

### 2.1 Create Migration Directory Structure

```bash
mkdir -p migrations/v3
```

### 2.2 Create Migration Script: `migrations/v3/001_merge_cursor_lease.sql`

```sql
-- ════════════════════════════════════════════════════════════════
-- QUEEN V3 MIGRATION: Merge Cursor + Lease, Remove messages_status
-- ════════════════════════════════════════════════════════════════

BEGIN;

-- ────────────────────────────────────────────────────────────────
-- STEP 1: Create new partition_consumers table
-- ────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS queen.partition_consumers (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',
    
    -- CURSOR STATE (Persistent - consumption progress)
    last_consumed_id UUID DEFAULT '00000000-0000-0000-0000-000000000000',
    last_consumed_created_at TIMESTAMPTZ,
    total_messages_consumed BIGINT DEFAULT 0,
    total_batches_consumed BIGINT DEFAULT 0,
    last_consumed_at TIMESTAMPTZ,
    
    -- LEASE STATE (Ephemeral - active processing lock)
    lease_expires_at TIMESTAMPTZ,
    lease_acquired_at TIMESTAMPTZ,
    message_batch JSONB,
    batch_size INTEGER DEFAULT 0,
    acked_count INTEGER DEFAULT 0,
    worker_id VARCHAR(255),
    
    -- STATISTICS (for dashboard - avoids COUNT queries)
    pending_estimate BIGINT DEFAULT 0,
    last_stats_update TIMESTAMPTZ,
    
    -- METADATA
    created_at TIMESTAMPTZ DEFAULT NOW(),
    
    UNIQUE(partition_id, consumer_group),
    
    -- Constraint: cursor consistency
    CHECK (
        (last_consumed_id = '00000000-0000-0000-0000-000000000000' AND last_consumed_created_at IS NULL)
        OR (last_consumed_id != '00000000-0000-0000-0000-000000000000' AND last_consumed_created_at IS NOT NULL)
    )
);

-- Indexes for partition_consumers
CREATE INDEX idx_partition_consumers_lookup
ON queen.partition_consumers(partition_id, consumer_group);

CREATE INDEX idx_partition_consumers_active_leases
ON queen.partition_consumers(partition_id, consumer_group, lease_expires_at)
WHERE lease_expires_at IS NOT NULL;

CREATE INDEX idx_partition_consumers_expired_leases
ON queen.partition_consumers(lease_expires_at)
WHERE lease_expires_at IS NOT NULL AND lease_expires_at <= NOW();

CREATE INDEX idx_partition_consumers_progress
ON queen.partition_consumers(last_consumed_at DESC);

CREATE INDEX idx_partition_consumers_idle
ON queen.partition_consumers(partition_id, consumer_group)
WHERE lease_expires_at IS NULL;

-- ────────────────────────────────────────────────────────────────
-- STEP 2: Migrate data from partition_cursors + partition_leases
-- ────────────────────────────────────────────────────────────────
INSERT INTO queen.partition_consumers (
    partition_id,
    consumer_group,
    last_consumed_id,
    last_consumed_created_at,
    total_messages_consumed,
    total_batches_consumed,
    last_consumed_at,
    lease_expires_at,
    lease_acquired_at,
    message_batch,
    batch_size,
    acked_count,
    created_at,
    pending_estimate
)
SELECT 
    -- From partition_cursors (always exists)
    pc.partition_id,
    pc.consumer_group,
    COALESCE(pc.last_consumed_id, '00000000-0000-0000-0000-000000000000'),
    pc.last_consumed_created_at,
    pc.total_messages_consumed,
    pc.total_batches_consumed,
    pc.last_consumed_at,
    
    -- From partition_leases (may not exist)
    pl.lease_expires_at,
    pl.created_at as lease_acquired_at,
    pl.message_batch,
    pl.batch_size,
    pl.acked_count,
    
    -- Metadata
    pc.created_at,
    
    -- Initialize pending_estimate to 0 (will be calculated later)
    0
FROM queen.partition_cursors pc
LEFT JOIN queen.partition_leases pl 
    ON pl.partition_id = pc.partition_id 
    AND pl.consumer_group = pc.consumer_group
    AND pl.released_at IS NULL  -- Only active leases
ON CONFLICT (partition_id, consumer_group) DO NOTHING;

-- Also migrate leases without cursors (shouldn't happen, but be safe)
INSERT INTO queen.partition_consumers (
    partition_id,
    consumer_group,
    lease_expires_at,
    lease_acquired_at,
    message_batch,
    batch_size,
    acked_count,
    created_at,
    pending_estimate
)
SELECT 
    pl.partition_id,
    pl.consumer_group,
    pl.lease_expires_at,
    pl.created_at,
    pl.message_batch,
    pl.batch_size,
    pl.acked_count,
    pl.created_at,
    0
FROM queen.partition_leases pl
WHERE pl.released_at IS NULL
  AND NOT EXISTS (
      SELECT 1 FROM queen.partition_consumers pc
      WHERE pc.partition_id = pl.partition_id
        AND pc.consumer_group = pl.consumer_group
  )
ON CONFLICT (partition_id, consumer_group) DO NOTHING;

-- ────────────────────────────────────────────────────────────────
-- STEP 3: Calculate initial pending_estimate for each consumer
-- ────────────────────────────────────────────────────────────────
-- This may take time on large tables, run in background after migration
-- For now, set to 0 and let it calculate incrementally

-- ────────────────────────────────────────────────────────────────
-- STEP 4: Drop old tables (after verification!)
-- ────────────────────────────────────────────────────────────────
-- IMPORTANT: Only run after verifying migration succeeded!
-- Uncomment these lines after testing:

-- DROP TABLE IF EXISTS queen.messages_status CASCADE;
-- DROP TABLE IF EXISTS queen.partition_cursors CASCADE;
-- DROP TABLE IF EXISTS queen.partition_leases CASCADE;
-- DROP TABLE IF EXISTS queen.consumer_groups CASCADE;

-- ────────────────────────────────────────────────────────────────
-- STEP 5: Update indexes on messages table
-- ────────────────────────────────────────────────────────────────
-- Add composite index for cursor-based queries (if not exists)
CREATE INDEX IF NOT EXISTS idx_messages_partition_created_id
ON queen.messages(partition_id, created_at, id);

-- Drop old index if exists
DROP INDEX IF EXISTS queen.idx_messages_partition_created;

COMMIT;
```

### 2.3 Create Rollback Script: `migrations/v3/001_rollback.sql`

```sql
-- ════════════════════════════════════════════════════════════════
-- ROLLBACK: Restore partition_cursors + partition_leases
-- ════════════════════════════════════════════════════════════════

BEGIN;

-- Restore partition_cursors
CREATE TABLE IF NOT EXISTS queen.partition_cursors (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',
    last_consumed_created_at TIMESTAMPTZ,
    last_consumed_id UUID,
    total_messages_consumed BIGINT DEFAULT 0,
    total_batches_consumed BIGINT DEFAULT 0,
    last_consumed_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(partition_id, consumer_group)
);

-- Restore partition_leases
CREATE TABLE IF NOT EXISTS queen.partition_leases (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',
    lease_expires_at TIMESTAMPTZ NOT NULL,
    message_batch JSONB,
    batch_size INTEGER DEFAULT 0,
    acked_count INTEGER DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    released_at TIMESTAMPTZ,
    UNIQUE(partition_id, consumer_group)
);

-- Migrate data back
INSERT INTO queen.partition_cursors 
SELECT * FROM queen.partition_consumers;

INSERT INTO queen.partition_leases
SELECT * FROM queen.partition_consumers
WHERE lease_expires_at IS NOT NULL;

-- Restore indexes
CREATE INDEX idx_partition_cursors_lookup
ON queen.partition_cursors(partition_id, consumer_group);

CREATE INDEX idx_partition_leases_expires
ON queen.partition_leases(lease_expires_at) 
WHERE released_at IS NULL;

-- Drop new table
DROP TABLE IF EXISTS queen.partition_consumers CASCADE;

COMMIT;
```

---

## Phase 3: Code Refactoring

### 3.1 Update Core Manager (`src/managers/queueManagerOptimized.js`)

**Priority: CRITICAL**  
**Estimated Changes: 200-300 lines**

#### Changes Required:

**A. Update `acknowledgeMessages()` function (lines 443-619):**

```javascript
// OLD: Update cursor + lease separately
await client.query(`UPDATE queen.partition_cursors SET ...`);
await client.query(`UPDATE queen.partition_leases SET ...`);

// NEW: Single atomic update
await client.query(`
  UPDATE queen.partition_consumers
  SET 
      -- Advance cursor
      last_consumed_id = $1,
      last_consumed_created_at = $2,
      total_messages_consumed = total_messages_consumed + $3,
      total_batches_consumed = total_batches_consumed + 1,
      last_consumed_at = NOW(),
      
      -- Update pending estimate
      pending_estimate = GREATEST(0, pending_estimate - $3),
      
      -- Release lease
      acked_count = acked_count + $3,
      lease_expires_at = CASE 
          WHEN acked_count + $3 >= batch_size THEN NULL 
          ELSE lease_expires_at 
      END,
      message_batch = CASE 
          WHEN acked_count + $3 >= batch_size THEN NULL 
          ELSE message_batch 
      END
  WHERE partition_id = $4 AND consumer_group = $5
`, [newId, newTs, count, partId, group]);
```

**B. Update `uniquePop()` function (lines 989-1420):**

```javascript
// OLD: Acquire lease
await client.query(`INSERT INTO queen.partition_leases ...`);

// NEW: Unified cursor + lease
await client.query(`
  -- Initialize cursor if doesn't exist
  INSERT INTO queen.partition_consumers (partition_id, consumer_group, last_consumed_id)
  VALUES ($1, $2, '00000000-0000-0000-0000-000000000000')
  ON CONFLICT (partition_id, consumer_group) DO NOTHING;
  
  -- Acquire lease
  UPDATE queen.partition_consumers
  SET lease_expires_at = NOW() + INTERVAL '1 second' * $3,
      lease_acquired_at = NOW(),
      message_batch = NULL,
      batch_size = 0,
      acked_count = 0
  WHERE partition_id = $1 
    AND consumer_group = $2
    AND (lease_expires_at IS NULL OR lease_expires_at <= NOW())
  RETURNING last_consumed_id, last_consumed_created_at
`, [partitionId, consumerGroup, leaseTime]);
```

**C. Update `reclaimExpiredLeases()` function (lines 622-683):**

```javascript
// OLD: Update partition_leases
await client.query(`UPDATE queen.partition_leases SET released_at = NOW() ...`);

// NEW: Update partition_consumers
await client.query(`
  UPDATE queen.partition_consumers
  SET lease_expires_at = NULL,
      lease_acquired_at = NULL,
      message_batch = NULL,
      batch_size = 0,
      acked_count = 0
  WHERE lease_expires_at <= NOW()
    AND lease_expires_at IS NOT NULL
`);
```

**D. Update `getQueueStats()` function (lines 686-846):**

```javascript
// OLD: Join messages_status for counts
LEFT JOIN queen.messages_status ms ON ms.message_id = m.id

// NEW: Use pending_estimate from partition_consumers
SELECT 
    pc.consumer_group,
    pc.pending_estimate as pending,
    pc.total_messages_consumed as completed,
    (SELECT COUNT(*) FROM dead_letter_queue WHERE consumer_group = pc.consumer_group) as failed
FROM queen.partition_consumers pc
```

**E. Remove consumer_groups references (lines 702, 708):**

```javascript
// OLD: Count consumer groups from table
LEFT JOIN queen.consumer_groups cg ON cg.queue_id = q.id

// NEW: Count distinct consumer_groups from partition_consumers
SELECT COUNT(DISTINCT consumer_group) 
FROM partition_consumers
WHERE consumer_group != '__QUEUE_MODE__'
```

### 3.2 Update Dashboard API (`src/routes/status.js`)

**Priority: HIGH**  
**Estimated Changes: 100-150 lines**

#### Functions to update:

**A. `getStatus()` - lines 30-235:**
- Remove JOINs to `messages_status`
- Use `partition_consumers.pending_estimate` instead of COUNT(*)

**B. `getQueueDepths()` - lines 372-404:**
- Replace `messages_status` with `partition_consumers`
- Use `pending_estimate` field

**C. `getQueueLag()` - lines 505-607:**
- Remove `messages_status` references
- Calculate lag from cursor position vs latest message

**D. `getActiveLeasesCount()` - lines 134-141:**
```javascript
// OLD: FROM queen.partition_leases
// NEW:
SELECT COUNT(*) FROM queen.partition_consumers
WHERE lease_expires_at IS NOT NULL AND lease_expires_at > NOW()
```

### 3.3 Update Analytics API (`src/routes/analytics.js`)

**Priority: HIGH**  
**Estimated Changes: 50-100 lines**

Replace all `messages_status` JOINs with `partition_consumers` queries:

```javascript
// OLD:
LEFT JOIN queen.messages_status ms ON ms.message_id = m.id

// NEW: Use estimates from partition_consumers
SELECT 
    pc.pending_estimate,
    pc.total_messages_consumed,
    ...
FROM partition_consumers pc
```

### 3.4 Update Resources API (`src/routes/resources.js`)

**Priority: MEDIUM**  
**Estimated Changes: 30-50 lines**

Replace 8 occurrences of `messages_status` with `partition_consumers` queries.

### 3.5 Update Messages API (`src/routes/messages.js`)

**Priority: MEDIUM**  
**Estimated Changes: 20-30 lines**

Remove `messages_status` JOINs and status updates.

### 3.6 Update WebSocket Server (`src/websocket/wsServer.js`)

**Priority: MEDIUM**  
**Estimated Changes: 10-15 lines**

```javascript
// OLD: lines 194-197
(SELECT COUNT(*) FROM queen.messages_status WHERE status = 'pending')

// NEW:
(SELECT SUM(pending_estimate) FROM queen.partition_consumers WHERE consumer_group = '__QUEUE_MODE__')
```

### 3.7 Update Services

**A. `src/services/evictionService.js`:**
- Remove `messages_status` references
- Update `pending_estimate` counters

**B. `src/services/retentionService.js`:**
- **BUG FIX**: Currently queries messages by `status` field which doesn't exist!
- Rewrite to use cursor position:

```javascript
// Delete old unconsumed messages
DELETE FROM queen.messages m
WHERE NOT EXISTS (
    SELECT 1 FROM queen.partition_consumers pc
    WHERE pc.partition_id = m.partition_id
      AND (m.created_at, m.id) <= (pc.last_consumed_created_at, pc.last_consumed_id)
)
AND m.created_at < NOW() - INTERVAL '...'
```

### 3.8 Update Tests

**Priority: LOW (but must pass before release)**

Update all test files to use new schema:
- `src/test/enterprise-tests.js`
- `src/test/advanced-pattern-tests.js`
- `src/test/edge-case-tests.js`
- `src/test/test.js`
- `src/test/bus-mode-tests.js`

---

## Phase 4: Testing Plan

### 4.1 Unit Tests
- ✅ Test cursor initialization (lazy creation)
- ✅ Test lease acquisition
- ✅ Test ACK updates cursor + lease atomically
- ✅ Test pending_estimate increments/decrements
- ✅ Test expired lease reclamation

### 4.2 Integration Tests
- ✅ Test POP → ACK flow end-to-end
- ✅ Test bus mode (multiple consumer groups)
- ✅ Test partial ACK (batch with some failures)
- ✅ Test total batch failure (cursor doesn't advance)

### 4.3 Performance Tests
- ✅ Benchmark POP performance (should be same or better)
- ✅ Benchmark ACK performance (should be 50-75% faster)
- ✅ Test with 1M+ messages
- ✅ Test with 10+ consumer groups

### 4.4 Dashboard Tests
- ✅ Verify pending counts are accurate
- ✅ Verify statistics refresh correctly
- ✅ Test with slow queries (should be faster)

---

## Phase 5: Deployment Strategy

### 5.1 Pre-Deployment Checklist

```bash
# 1. Backup database
pg_dump -h localhost -U postgres -d queen > backup_pre_migration_$(date +%Y%m%d_%H%M%S).sql

# 2. Verify backup
pg_restore --list backup_pre_migration_*.sql | head -20

# 3. Test migration on staging database
psql -h staging -U postgres -d queen < migrations/v3/001_merge_cursor_lease.sql

# 4. Verify migration succeeded
psql -h staging -U postgres -d queen -c "SELECT COUNT(*) FROM queen.partition_consumers;"

# 5. Run code on staging
npm start

# 6. Run test suite
npm test

# 7. Monitor for errors
tail -f logs/queen.log
```

### 5.2 Deployment Steps

**Option A: Zero-Downtime (Recommended)**

1. Deploy new code WITHOUT dropping old tables
2. Code uses `partition_consumers` but old tables still exist
3. Monitor for 24-48 hours
4. If stable, run DROP TABLE commands
5. If issues, rollback to old code (old tables still there)

**Option B: Maintenance Window**

1. Stop Queen server
2. Run migration script
3. Drop old tables
4. Deploy new code
5. Start Queen server
6. Monitor closely

### 5.3 Rollback Procedure

```bash
# If issues detected within first 48 hours:

# 1. Stop Queen server
systemctl stop queen

# 2. Run rollback script
psql -h localhost -U postgres -d queen < migrations/v3/001_rollback.sql

# 3. Deploy old code version
git checkout v2.x
npm install
npm start

# 4. Verify functionality
npm test
```

---

## Phase 6: Post-Migration

### 6.1 Monitoring

Monitor these metrics for 2 weeks:

- ✅ POP latency (should be same or better)
- ✅ ACK latency (should be 50-75% faster)
- ✅ Database write throughput (should be lower)
- ✅ Pending count accuracy (compare with manual COUNT)
- ✅ Error rates
- ✅ Memory usage
- ✅ CPU usage

### 6.2 Documentation Updates

- [ ] Update README.md with new schema
- [ ] Update CURSOR_BASED_CONSUMPTION.md
- [ ] Update API documentation
- [ ] Add migration notes to CHANGELOG.md
- [ ] Update architecture diagrams

### 6.3 Cleanup

After 2 weeks of stable operation:

```sql
-- Final cleanup: drop old tables permanently
DROP TABLE IF EXISTS queen.messages_status CASCADE;
DROP TABLE IF EXISTS queen.partition_cursors CASCADE;
DROP TABLE IF EXISTS queen.partition_leases CASCADE;
DROP TABLE IF EXISTS queen.consumer_groups CASCADE;
```

---

## Risk Assessment

### High Risk
- ⚠️ **Data loss during migration** - Mitigation: Extensive testing, backup before migration
- ⚠️ **Query performance regression** - Mitigation: Benchmark before/after
- ⚠️ **Dashboard inaccuracy** - Mitigation: Pending estimates with periodic reconciliation

### Medium Risk
- ⚠️ **Test coverage gaps** - Mitigation: Update all tests before deployment
- ⚠️ **Third-party integrations** - Mitigation: Check if any external tools query messages_status

### Low Risk
- ⚠️ **Documentation out of date** - Mitigation: Update docs as part of deployment

---

## Timeline Estimate

| Phase | Duration | Parallel? |
|-------|----------|-----------|
| **Phase 2: Schema Migration** | 1 day | No |
| **Phase 3.1: Core Manager** | 3-4 days | No |
| **Phase 3.2-3.7: APIs & Services** | 2-3 days | Yes |
| **Phase 3.8: Tests** | 2-3 days | Yes |
| **Phase 4: Testing** | 3-5 days | No |
| **Phase 5: Deployment** | 1 day | No |
| **Phase 6: Monitoring** | 14 days | Ongoing |
| **TOTAL** | ~20 working days | |

---

## Success Criteria

### Must Have (Go/No-Go)
- ✅ All tests pass
- ✅ POP performance same or better
- ✅ ACK performance 30%+ faster
- ✅ Dashboard loads without errors
- ✅ Bus mode works correctly
- ✅ No data loss

### Nice to Have
- ✅ ACK performance 50%+ faster
- ✅ Pending counts within 5% accuracy
- ✅ Memory usage reduced
- ✅ Simplified codebase metrics

---

## Approval Required

**Technical Lead:** _________________  
**Database Admin:** _________________  
**QA Lead:** _________________  
**Product Owner:** _________________  

**Date Approved:** _________________

---

## Notes

### Discovered Issues During Analysis

1. **BUG in retentionService.js**: Lines 22-47 query `messages.status` which doesn't exist!
   - Current schema has no `status` field on messages table
   - This service is broken in current code
   - Must fix during migration

2. **Consumer groups barely used**: Only 2 references in entire codebase
   - Safe to remove
   - Can be derived from `partition_consumers`

3. **messages_status heavily used**: 51 references
   - Biggest refactoring effort
   - Most are dashboard queries
   - Can be replaced with `pending_estimate` counters

### Alternative Approaches Considered

1. **Keep messages_status for observability**
   - Pro: Easier migration
   - Con: Doesn't achieve write reduction goals
   - **Decision: REJECTED**

2. **Add audit log table instead**
   - Pro: Can log events without hot path impact
   - Con: Additional complexity
   - **Decision: FUTURE CONSIDERATION**

3. **Gradual migration** (support both schemas)
   - Pro: Lower risk
   - Con: Code complexity, longer timeline
   - **Decision: REJECTED** (not worth the complexity)

---

## Appendix A: File Modification Checklist

### Files to Modify (Priority Order)

#### P0 - Critical (Blocks deployment)
- [ ] `src/database/schema-v2.sql` - New schema
- [ ] `migrations/v3/001_merge_cursor_lease.sql` - Migration script
- [ ] `migrations/v3/001_rollback.sql` - Rollback script
- [ ] `src/managers/queueManagerOptimized.js` - Core logic

#### P1 - High (Required for basic functionality)
- [ ] `src/routes/status.js` - Dashboard API
- [ ] `src/routes/analytics.js` - Analytics API
- [ ] `src/routes/ack.js` - ACK endpoint
- [ ] `src/routes/pop.js` - POP endpoint

#### P2 - Medium (Required for full functionality)
- [ ] `src/routes/resources.js` - Resource management
- [ ] `src/routes/messages.js` - Message operations
- [ ] `src/websocket/wsServer.js` - Real-time updates
- [ ] `src/services/evictionService.js` - Eviction
- [ ] `src/services/retentionService.js` - Retention (fix bug!)

#### P3 - Low (Required for testing)
- [ ] `src/test/enterprise-tests.js`
- [ ] `src/test/advanced-pattern-tests.js`
- [ ] `src/test/edge-case-tests.js`
- [ ] `src/test/test.js`
- [ ] `src/test/bus-mode-tests.js`

#### P4 - Documentation
- [ ] `README.md`
- [ ] `CURSOR_BASED_CONSUMPTION.md`
- [ ] `CHANGELOG.md`
- [ ] API documentation

---

## Appendix B: Query Pattern Changes

### Pattern 1: Pending Count
```sql
-- OLD:
SELECT COUNT(*) FROM messages_status WHERE status = 'pending'

-- NEW:
SELECT SUM(pending_estimate) FROM partition_consumers WHERE consumer_group = '__QUEUE_MODE__'
```

### Pattern 2: Cursor + Lease Join
```sql
-- OLD:
FROM partition_cursors pc
LEFT JOIN partition_leases pl ON pl.partition_id = pc.partition_id

-- NEW:
FROM partition_consumers pc
-- Single table, no JOIN needed!
```

### Pattern 3: Message Status Check
```sql
-- OLD:
LEFT JOIN messages_status ms ON ms.message_id = m.id
WHERE ms.status = 'completed'

-- NEW:
-- Derive from cursor position
WHERE (m.created_at, m.id) <= (pc.last_consumed_created_at, pc.last_consumed_id)
```

---

**END OF MIGRATION PLAN**

