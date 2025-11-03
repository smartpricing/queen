# Database Migrations

## Migration: optimize_triggers_batch_inserts.sql

**Date:** 2025-11-03  
**Priority:** HIGH - Performance Critical  
**Downtime Required:** No

### Problem

During high load with batch inserts (e.g., 1000 messages), the system was experiencing statement timeouts due to lock contention on three tables:
- `queen.partitions` (via `update_partition_last_activity` trigger)
- `queen.partition_consumers` (via `update_pending_on_push` trigger)  
- `queen.queue_watermarks` (via `update_queue_watermark` trigger)

The issue was that all three triggers were configured as `FOR EACH ROW` triggers, meaning:
- Inserting 1000 messages → 3000 trigger executions (3 triggers × 1000 rows)
- Each execution tries to update the same rows in the target tables
- Massive lock contention → statement timeouts

**Error Example:**
```
ERROR: canceling statement due to statement timeout
CONTEXT: while inserting index tuple (185,107) in relation "queue_watermarks"
```

### Solution

Converted all three triggers from `FOR EACH ROW` to `FOR EACH STATEMENT` using PostgreSQL's transition tables feature:

**Before (Row-Level):**
```sql
CREATE TRIGGER trigger_name
AFTER INSERT ON queen.messages
FOR EACH ROW  -- Runs N times for N inserts
EXECUTE FUNCTION function_name();
```

**After (Statement-Level):**
```sql
CREATE TRIGGER trigger_name
AFTER INSERT ON queen.messages
REFERENCING NEW TABLE AS new_messages  -- Access all inserted rows
FOR EACH STATEMENT  -- Runs once per batch
EXECUTE FUNCTION function_name();
```

### Performance Impact

| Batch Size | Before (Trigger Executions) | After (Trigger Executions) | Improvement |
|------------|----------------------------|---------------------------|-------------|
| 1 message  | 3 triggers                 | 3 triggers                | Same        |
| 100 messages | 300 triggers             | 3 triggers                | 100x faster |
| 1000 messages | 3000 triggers            | 3 triggers                | 1000x faster |

### How to Apply

#### Option 1: Automatic (New Deployments)
The migration is included in the schema initialization code. New deployments will automatically have the optimized triggers.

#### Option 2: Manual (Existing Databases)
Apply the migration to your existing database:

```bash
# Apply the migration
psql -h your-db-host -U your-user -d your-database -f optimize_triggers_batch_inserts.sql

# Verify the changes
psql -h your-db-host -U your-user -d your-database -f verify_triggers.sql
```

Or using environment variables:
```bash
psql $DATABASE_URL -f optimize_triggers_batch_inserts.sql
psql $DATABASE_URL -f verify_triggers.sql
```

### Verification

After applying the migration, run the verification script:

```bash
psql $DATABASE_URL -f verify_triggers.sql
```

Expected output:
```
            trigger_name             | action_timing | action_orientation 
-------------------------------------+---------------+--------------------
 trigger_update_partition_activity   | AFTER         | STATEMENT
 trigger_update_pending_on_push      | AFTER         | STATEMENT
 trigger_update_watermark            | AFTER         | STATEMENT
(3 rows)
```

If any trigger shows `ROW` instead of `STATEMENT`, the migration needs to be reapplied.

### Rollback (Not Recommended)

If you need to rollback to the old row-level triggers (not recommended):

```bash
psql $DATABASE_URL -f rollback_triggers.sql
```

### Testing

After applying the migration:

1. **Functional Test:**
   ```bash
   cd client-js/test-v2
   node push.js  # Test basic push operations
   node consume.js  # Test basic consume operations
   ```

2. **Performance Test:**
   ```bash
   cd benchmark
   ./bin/benchmark --batch-size 1000  # Should no longer timeout
   ```

3. **Monitor Logs:**
   Watch for the absence of timeout errors:
   ```
   # Before: You would see timeouts
   ERROR: canceling statement due to statement timeout
   
   # After: Clean batch processing
   [debug] Processing final batch: 1000 messages, ~1.14 MB
   ```

### Related Changes

- `server/src/managers/queue_manager.cpp` - Updated trigger definitions
- `docs/StreamingAlice.md` - Updated documentation

### Notes

- This migration is **backward compatible** - no changes to application code required
- The triggers maintain the exact same functionality, just optimized for batch operations
- Can be applied during production with zero downtime
- No data migration required - only trigger function updates

