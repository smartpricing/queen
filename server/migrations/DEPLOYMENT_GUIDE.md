# Partition Lookup Optimization - Deployment Guide

## 📋 Overview

This deployment adds a `partition_lookup` table with automatic trigger maintenance to optimize partition selection queries from 50-500ms to <5ms.

## ⚠️ Important Notes

- **Zero downtime:** Can be deployed without stopping the service
- **Safe to run multiple times:** All scripts are idempotent
- **Backward compatible:** Old code continues to work during deployment

## 🚀 Deployment Steps

### Step 1: Run Initial Migration (BEFORE code deploy)

This creates the table, indexes, trigger, then migrates existing data.

```bash
cd /Users/alice/Work/queen
psql $DATABASE_URL -f server/migrations/create_partition_lookup.sql
```

**What this does:**
- ✅ Creates `queen.partition_lookup` table
- ✅ Creates 3 indexes for performance
- ✅ Creates trigger function and trigger (captures new messages from this point forward)
- ✅ **Migrates all existing message data** into lookup table (old messages)

**Important:** The trigger is created BEFORE migration to eliminate any gap:
- Trigger captures all messages inserted DURING migration
- Migration captures all messages that existed BEFORE trigger was created
- No window between them = no resync needed!

**Expected output:**
```
CREATE TABLE
CREATE INDEX
CREATE INDEX
CREATE INDEX
INSERT 0 [N]  -- N = number of partitions with messages
CREATE FUNCTION
DROP TRIGGER
CREATE TRIGGER
COMMIT

 total_partitions | partitions_with_messages
------------------+-------------------------
              100 |                      85
```

**Verify it worked:**
```bash
psql $DATABASE_URL -f server/migrations/test_partition_lookup.sql
```

All checks should show `✓ PASS`.

---

### Step 2: Build and Deploy New Code

```bash
cd server
make clean && make

# Deploy the new binary (restart the server)
# The new binary uses the optimized partition selection query
```

**What changed in the code:**
- Partition selection query now uses `partition_lookup` table
- Schema initialization includes lookup table (for fresh installs)
- Load balancing changed to round-robin (`last_consumed_at ASC`)

---

### Step 3: (Optional) Run Post-Deploy Resync

**Note:** With the improved migration script, resync is now **optional** because the trigger is created before migration, eliminating any gap.

However, you can still run it as a verification step:

```bash
psql $DATABASE_URL -f server/migrations/resync_partition_lookup.sql
```

**What this does:**
- ✅ Verifies lookup table is in sync
- ✅ Provides peace of mind
- ✅ Safe to run anytime (idempotent)

**Expected output:**
```
BEGIN
INSERT 0 0  -- Should be 0 updates if migration worked correctly
COMMIT

[Sample of lookup data...]

 partitions_with_stale_lookup
------------------------------
                            0  -- Should always be 0!
```

**Run resync only if:**
- ❌ Test script shows stale data
- ❌ You want extra verification (harmless!)
- ❌ Any errors occurred during Step 1

---

## ✅ Verification

### 1. Check Trigger is Working

Push a test message and verify the lookup table updates:

```bash
# Push a message (use your client)
cd ../client-js/test-v2
node push.js

# Check lookup table was updated
psql $DATABASE_URL -c "
SELECT queue_name, last_message_id, last_message_created_at, updated_at 
FROM queen.partition_lookup 
ORDER BY updated_at DESC 
LIMIT 5;
"
```

The `updated_at` should be very recent (within seconds).

### 2. Check Query Performance

```sql
EXPLAIN ANALYZE
SELECT p.id, p.name
FROM queen.partition_lookup pl
JOIN queen.partitions p ON p.id = pl.partition_id
LEFT JOIN queen.partition_consumers pc ON pc.partition_id = pl.partition_id 
    AND pc.consumer_group = '__QUEUE_MODE__'
WHERE pl.queue_name = 'your-queue-name'
  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
  AND (pc.last_consumed_created_at IS NULL 
       OR pl.last_message_created_at > pc.last_consumed_created_at
       OR (pl.last_message_created_at = pc.last_consumed_created_at 
           AND pl.last_message_id > pc.last_consumed_id))
ORDER BY pc.last_consumed_at ASC NULLS FIRST
LIMIT 10;
```

**Expected:**
- Execution Time: **< 5ms**
- Plan should use **Index Scan** on `partition_lookup`
- **NO Seq Scan** on `messages` table

### 3. Test Consumer Behavior

```bash
cd ../client-js/test-v2
node consume.js
```

Check server logs for:
```
Trying partition 'XXX' (priority by last_consumed_at)
```

This confirms the new query is being used.

### 4. Run Full Test Suite

```bash
psql $DATABASE_URL -f server/migrations/test_partition_lookup.sql
```

All checks should be `✓ PASS`.

---

## 📊 Monitoring

After deployment, monitor:

1. **Partition selection performance**
   ```sql
   -- Should be consistently <5ms
   EXPLAIN ANALYZE [partition selection query]
   ```

2. **Lookup table consistency**
   ```bash
   psql $DATABASE_URL -f server/migrations/test_partition_lookup.sql
   ```

3. **Trigger errors** (check PostgreSQL logs)
   ```bash
   # Should see no errors related to trg_update_partition_lookup
   tail -f /var/log/postgresql/postgresql.log | grep partition_lookup
   ```

---

## 🔄 Rollback Plan

If issues occur, you can rollback safely:

### Option 1: Rollback Code Only (Keep Table)

```bash
# Deploy previous code version
# The old query will continue to work (just slower)
# Lookup table will continue to update via trigger (harmless)
```

### Option 2: Full Rollback (Remove Table)

```sql
BEGIN;

-- Remove trigger
DROP TRIGGER IF EXISTS trg_update_partition_lookup ON queen.messages;
DROP FUNCTION IF EXISTS queen.update_partition_lookup_trigger();

-- Remove table
DROP TABLE IF EXISTS queen.partition_lookup;

COMMIT;
```

Then redeploy old code.

**Note:** Option 1 is safer - the lookup table is harmless if not used.

---

## 🐛 Troubleshooting

### Problem: Lookup table not updating

**Check trigger exists:**
```sql
SELECT tgname, tgenabled FROM pg_trigger 
WHERE tgname = 'trg_update_partition_lookup';
```

**Re-create trigger:**
```bash
psql $DATABASE_URL -f server/migrations/create_partition_lookup.sql
```

### Problem: Query still slow

**Check indexes exist:**
```sql
SELECT indexname FROM pg_indexes 
WHERE tablename = 'partition_lookup';
```

**Verify query is using lookup table:**
```sql
EXPLAIN [your partition selection query]
-- Should show "Index Scan on partition_lookup"
```

### Problem: Stale data detected

**Run resync:**
```bash
psql $DATABASE_URL -f server/migrations/resync_partition_lookup.sql
```

**If still stale, check trigger:**
```sql
-- Insert test message
INSERT INTO queen.messages (transaction_id, partition_id, payload)
SELECT 'test-' || gen_random_uuid(), partition_id, '{"test": true}'::jsonb
FROM queen.partitions LIMIT 1;

-- Check lookup updated
SELECT * FROM queen.partition_lookup 
ORDER BY updated_at DESC LIMIT 1;
```

---

## 📝 Summary Checklist

- [ ] Run `create_partition_lookup.sql` (migrates old data + creates trigger)
- [ ] Verify with `test_partition_lookup.sql` (all checks PASS)
- [ ] Build and deploy new code
- [ ] Run `resync_partition_lookup.sql` (catch any gap during deploy)
- [ ] Verify query performance (<5ms)
- [ ] Test push and consume operations
- [ ] Monitor for 24 hours

## 🎉 Success Criteria

- ✅ Partition selection queries < 5ms
- ✅ Trigger updating lookup table automatically
- ✅ No stale data detected
- ✅ Consumer groups finding partitions correctly
- ✅ Round-robin load balancing working

---

## 📞 Support

If issues persist:
1. Check PostgreSQL logs for trigger errors
2. Verify table/indexes with `test_partition_lookup.sql`
3. Run resync script
4. Check `lookup.md` for detailed documentation

