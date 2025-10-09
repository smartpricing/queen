# Queue-Only Configuration Modification Plan

## Overview
This document outlines the plan to simplify the Queen message queue system by moving all configuration options to the queue level, making partitions pure FIFO containers without individual settings.

## Current Problem
Configuration options are currently split between queues and partitions, creating:
- Confusion about where to set options
- Inconsistent API behavior
- Unused configuration values (e.g., leaseTime is configured but hardcoded to 300s)
- Complex mental model

## Solution: Queue-Only Configuration
Move ALL configuration options to the queue level. Partitions become simple FIFO containers with only a name and reference to their parent queue.

---

## 1. Database Schema Changes

### 1.1 Modify `queen.queues` Table
Add all configuration columns currently stored in partition options:

```sql
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS lease_time INTEGER DEFAULT 300;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS retry_limit INTEGER DEFAULT 3;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS retry_delay INTEGER DEFAULT 1000;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS max_size INTEGER DEFAULT 10000;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS ttl INTEGER DEFAULT 3600;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS dead_letter_queue BOOLEAN DEFAULT FALSE;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS dlq_after_max_retries BOOLEAN DEFAULT FALSE;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS delayed_processing INTEGER DEFAULT 0;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS window_buffer INTEGER DEFAULT 0;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS retention_seconds INTEGER DEFAULT 0;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS completed_retention_seconds INTEGER DEFAULT 0;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS retention_enabled BOOLEAN DEFAULT FALSE;

-- Note: These already exist:
-- priority INTEGER DEFAULT 0
-- encryption_enabled BOOLEAN DEFAULT FALSE
-- max_wait_time_seconds INTEGER DEFAULT 0
```

### 1.2 Simplify `queen.partitions` Table
Remove configuration-related columns:

```sql
ALTER TABLE queen.partitions 
  DROP COLUMN IF EXISTS options,
  DROP COLUMN IF EXISTS priority;

-- Final structure:
-- id UUID PRIMARY KEY
-- queue_id UUID REFERENCES queen.queues(id)
-- name VARCHAR(255) NOT NULL DEFAULT 'Default'
-- created_at TIMESTAMP DEFAULT NOW()
-- last_activity TIMESTAMP DEFAULT NOW()
```

---

## 2. Code Modifications Required

### 2.1 Core Manager Changes

#### `src/managers/queueManagerOptimized.js` (MAJOR CHANGES)

**Line 18-57: ensureResources function**
- Remove `options` from partition query (line 40)
- Remove `partitionOptions` from cache structure (line 48)
- Update to fetch queue configuration instead

**Lines 200-364: popMessages function**
- Lines 214-239: Update partition-specific pop query to use queue options
- Line 318: Use queue's `lease_time` instead of hardcoded 300
- Line 355: Remove options from message response

**Lines 366-431: popMessagesWithFilters function**
- Line 420: Use queue's `lease_time` instead of hardcoded 300

**Lines 483-539: acknowledgeMessage function**
- Lines 500-509: Query queue options instead of partition options for retry logic

**Lines 586-692: Configuration functions**
- Remove `configureQueueOnly` function (no longer needed)
- Rewrite `configureQueue` to only update queue-level settings
- Remove partition parameter handling

### 2.2 Route Changes

#### `src/routes/configure.js` (COMPLETE REWRITE)
```javascript
export const createConfigureRoute = (queueManager) => {
  return async (body) => {
    const { queue, namespace, task, options = {} } = body;
    
    if (!queue) {
      throw new Error('queue is required');
    }
    
    const validOptions = {
      leaseTime: options.leaseTime || 300,
      maxSize: options.maxSize || 10000,
      ttl: options.ttl || 3600,
      retryLimit: options.retryLimit || 3,
      retryDelay: options.retryDelay || 1000,
      deadLetterQueue: options.deadLetterQueue || false,
      dlqAfterMaxRetries: options.dlqAfterMaxRetries || false,
      priority: options.priority || 0,
      delayedProcessing: options.delayedProcessing || 0,
      windowBuffer: options.windowBuffer || 0,
      retentionSeconds: options.retentionSeconds || 0,
      completedRetentionSeconds: options.completedRetentionSeconds || 0,
      retentionEnabled: options.retentionEnabled || false,
      encryptionEnabled: options.encryptionEnabled,
      maxWaitTimeSeconds: options.maxWaitTimeSeconds
    };
    
    const result = await queueManager.configureQueue(queue, validOptions, namespace, task);
    
    return {
      queue,
      namespace,
      task,
      configured: true,
      options: result.options
    };
  };
};
```

#### `src/routes/resources.js` (MINOR CHANGES)
- Line 152: Remove `p.options` from partition query
- Line 191: Remove `options` from partition response

#### `src/routes/messages.js` (MINOR CHANGES)
- Line 97: Remove `p.options as partition_options` from query
- Line 127: Remove `partitionOptions` from response

### 2.3 Service Changes

#### `src/services/retentionService.js`
- Lines 83-88: Query queue options instead of partition options
```sql
SELECT q.id, q.name, q.retention_enabled, q.retention_seconds, 
       q.completed_retention_seconds
FROM queen.queues q
WHERE q.retention_enabled = true
```

### 2.4 Client Library Changes

#### `src/client/queenClient.js`
Update configure method signature:
```javascript
const configure = async (options = {}) => {
  const { queue, namespace, task, options: configOptions = {} } = options;
  // Remove partition parameter handling
  return withRetry(
    () => http.post('/api/v1/configure', { 
      queue, 
      namespace, 
      task, 
      options: configOptions 
    }),
    retryAttempts,
    retryDelay
  );
};
```

### 2.5 Cache Changes

#### `src/managers/resourceCache.js`
Remove partition options from cache structure:
```javascript
// Before:
resources.set(cacheKey, {
  queueId,
  queueName,
  partitionId,
  partitionOptions,  // Remove this
  encryptionEnabled,
  maxWaitTimeSeconds
});

// After:
resources.set(cacheKey, {
  queueId,
  queueName,
  partitionId,
  // All queue config fetched when needed
});
```

---

## 3. Migration Strategy

### 3.1 Migration Script: `migrations/002-queue-only-config.sql`

```sql
-- Step 1: Add new columns to queues table
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS lease_time INTEGER;
ALTER TABLE queen.queues ADD COLUMN IF NOT EXISTS retry_limit INTEGER;
-- ... (all other columns)

-- Step 2: Migrate existing partition options to queue level
-- For queues with single partition or all partitions having same config
UPDATE queen.queues q
SET 
  lease_time = COALESCE((p.options->>'leaseTime')::int, 300),
  retry_limit = COALESCE((p.options->>'retryLimit')::int, 3),
  retry_delay = COALESCE((p.options->>'retryDelay')::int, 1000),
  max_size = COALESCE((p.options->>'maxSize')::int, 10000),
  ttl = COALESCE((p.options->>'ttl')::int, 3600),
  dead_letter_queue = COALESCE((p.options->>'deadLetterQueue')::boolean, false),
  dlq_after_max_retries = COALESCE((p.options->>'dlqAfterMaxRetries')::boolean, false),
  delayed_processing = COALESCE((p.options->>'delayedProcessing')::int, 0),
  window_buffer = COALESCE((p.options->>'windowBuffer')::int, 0),
  retention_seconds = COALESCE((p.options->>'retentionSeconds')::int, 0),
  completed_retention_seconds = COALESCE((p.options->>'completedRetentionSeconds')::int, 0),
  retention_enabled = COALESCE((p.options->>'retentionEnabled')::boolean, false)
FROM (
  SELECT DISTINCT ON (queue_id) 
    queue_id, 
    options
  FROM queen.partitions
  WHERE name = 'Default' OR queue_id IN (
    SELECT queue_id FROM queen.partitions GROUP BY queue_id HAVING COUNT(*) = 1
  )
  ORDER BY queue_id, name = 'Default' DESC
) p
WHERE q.id = p.queue_id;

-- Step 3: Set defaults for any remaining nulls
UPDATE queen.queues
SET
  lease_time = COALESCE(lease_time, 300),
  retry_limit = COALESCE(retry_limit, 3),
  retry_delay = COALESCE(retry_delay, 1000),
  max_size = COALESCE(max_size, 10000),
  ttl = COALESCE(ttl, 3600),
  dead_letter_queue = COALESCE(dead_letter_queue, false),
  dlq_after_max_retries = COALESCE(dlq_after_max_retries, false),
  delayed_processing = COALESCE(delayed_processing, 0),
  window_buffer = COALESCE(window_buffer, 0),
  retention_seconds = COALESCE(retention_seconds, 0),
  completed_retention_seconds = COALESCE(completed_retention_seconds, 0),
  retention_enabled = COALESCE(retention_enabled, false);

-- Step 4: Drop partition columns
ALTER TABLE queen.partitions 
  DROP COLUMN IF EXISTS options,
  DROP COLUMN IF EXISTS priority;

-- Step 5: Log migration for queues with conflicting partition configs
CREATE TABLE IF NOT EXISTS queen.migration_log (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  queue_name VARCHAR(255),
  message TEXT,
  created_at TIMESTAMP DEFAULT NOW()
);

INSERT INTO queen.migration_log (queue_name, message)
SELECT q.name, 
  'Queue had multiple partitions with different configurations. Used Default partition or first partition settings.'
FROM queen.queues q
WHERE q.id IN (
  SELECT queue_id 
  FROM queen.partitions 
  GROUP BY queue_id 
  HAVING COUNT(DISTINCT options::text) > 1
);
```

---

## 4. Test Updates Required

### Files to Update:
1. `src/test/test.js` - Update all configuration tests
2. `src/test/comprehensive-test.js` - Update configuration tests
3. `src/test/core-features-test.js` - Update configuration tests
4. `examples/continuous-producer.js` - Remove partition from configure
5. `examples/continuous-consumer.js` - Update configuration
6. All other example files using configure

### Test Changes Example:
```javascript
// Before:
await client.configure({
  queue: 'test-queue',
  partition: 'test-partition',
  options: { leaseTime: 600, retryLimit: 5 }
});

// After:
await client.configure({
  queue: 'test-queue',
  options: { leaseTime: 600, retryLimit: 5 }
});
```

---

## 5. API Changes

### Configuration Endpoint

**Before:**
```javascript
POST /api/v1/configure
{
  "queue": "myqueue",
  "partition": "mypartition",  // Optional
  "namespace": "...",          // Optional
  "task": "...",               // Optional
  "options": {
    "leaseTime": 300,
    "retryLimit": 3,
    // etc.
  }
}
```

**After:**
```javascript
POST /api/v1/configure
{
  "queue": "myqueue",
  "namespace": "...",          // Optional
  "task": "...",               // Optional
  "options": {
    "leaseTime": 300,
    "retryLimit": 3,
    // etc.
  }
}
// Note: partition parameter is removed/ignored
```

---

## 6. Implementation Phases

### Phase 1: Database Migration (Day 1)
- [ ] Create migration script `002-queue-only-config.sql`
- [ ] Test migration on development database
- [ ] Create rollback script

### Phase 2: Core Code Changes (Day 2-3)
- [ ] Update `queueManagerOptimized.js`
- [ ] Update `resourceCache.js`
- [ ] Fix hardcoded lease time bug

### Phase 3: Route Updates (Day 3)
- [ ] Update `configure.js`
- [ ] Update `resources.js`
- [ ] Update `messages.js`

### Phase 4: Service Updates (Day 4)
- [ ] Update `retentionService.js`
- [ ] Verify `evictionService.js` (no changes needed)

### Phase 5: Client & Tests (Day 4-5)
- [ ] Update `queenClient.js`
- [ ] Update all test files
- [ ] Update example files

### Phase 6: Documentation (Day 5)
- [ ] Update API.md
- [ ] Update README.md
- [ ] Create migration guide

---

## 7. Rollback Plan

If issues arise, rollback strategy:

1. **Database Rollback:**
```sql
-- Re-add partition columns
ALTER TABLE queen.partitions 
  ADD COLUMN IF NOT EXISTS options JSONB DEFAULT '{"leaseTime": 300, "retryLimit": 3}',
  ADD COLUMN IF NOT EXISTS priority INTEGER DEFAULT 0;

-- Restore partition options from queue settings
UPDATE queen.partitions p
SET options = json_build_object(
  'leaseTime', q.lease_time,
  'retryLimit', q.retry_limit,
  -- ... other fields
)
FROM queen.queues q
WHERE p.queue_id = q.id;

-- Remove queue columns
ALTER TABLE queen.queues 
  DROP COLUMN IF EXISTS lease_time,
  DROP COLUMN IF EXISTS retry_limit;
  -- ... etc
```

2. **Code Rollback:**
- Revert git commits
- Restore previous deployment

---

## 8. Benefits After Implementation

1. **Simpler Mental Model**: Queue = configuration, Partition = FIFO container
2. **Cleaner API**: No confusion about where to set options
3. **Bug Fixes**: Actually use configured leaseTime instead of hardcoded value
4. **Reduced Complexity**: Less code, easier maintenance
5. **Better Performance**: Fewer joins, simpler queries
6. **Consistent Behavior**: All partitions in a queue behave the same way

---

## 9. Risks and Mitigations

### Risk 1: Data Loss During Migration
**Mitigation**: Full backup before migration, test on staging first

### Risk 2: Breaking Existing Clients
**Mitigation**: Temporarily accept but ignore partition parameter, deprecation period

### Risk 3: Performance Impact
**Mitigation**: Add proper indexes on new queue columns

### Risk 4: Different Partition Behaviors Lost
**Mitigation**: Document in migration log, provide manual override if needed

---

## 10. Success Criteria

- [ ] All tests pass with new configuration
- [ ] No hardcoded values in code
- [ ] API is backward compatible (ignores partition parameter)
- [ ] Migration completes without data loss
- [ ] Performance metrics remain stable or improve
- [ ] Documentation is updated

---

## Notes

- This is a breaking change for the internal architecture but can be made backward compatible at the API level
- The partition parameter in configure can be accepted but ignored for a transition period
- Consider adding a feature flag to enable/disable new behavior during rollout
