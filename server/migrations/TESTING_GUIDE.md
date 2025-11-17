# Partition Lookup Testing Guide

## Quick Verification

### 1. Run Test Script
```bash
psql $DATABASE_URL -f server/migrations/test_partition_lookup.sql
```

All checks should show `✓ PASS`.

### 2. Test Trigger Functionality

```sql
-- Create a test queue and partition
INSERT INTO queen.queues (name) VALUES ('test-lookup-queue');
INSERT INTO queen.partitions (queue_id, name) 
SELECT id, 'test-partition' FROM queen.queues WHERE name = 'test-lookup-queue';

-- Insert a test message (trigger should fire)
INSERT INTO queen.messages (transaction_id, partition_id, payload)
SELECT 
    'test-txn-' || gen_random_uuid()::text,
    p.id,
    '{"test": "data"}'::jsonb
FROM queen.partitions p
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'test-lookup-queue' AND p.name = 'test-partition';

-- Verify lookup table was updated
SELECT 
    pl.queue_name,
    p.name as partition_name,
    pl.last_message_id,
    pl.last_message_created_at,
    pl.updated_at
FROM queen.partition_lookup pl
JOIN queen.partitions p ON p.id = pl.partition_id
WHERE pl.queue_name = 'test-lookup-queue';

-- Should show one row with recent timestamp
```

### 3. Test Batch Insert (Trigger Efficiency)

```sql
-- Insert 10 messages in one batch
WITH test_data AS (
    SELECT 
        p.id as partition_id,
        'batch-txn-' || i as txn_id
    FROM queen.partitions p
    JOIN queen.queues q ON q.id = p.queue_id
    CROSS JOIN generate_series(1, 10) i
    WHERE q.name = 'test-lookup-queue' AND p.name = 'test-partition'
)
INSERT INTO queen.messages (transaction_id, partition_id, payload)
SELECT txn_id, partition_id, '{"batch": "test"}'::jsonb
FROM test_data;

-- Verify lookup table has the LAST message (not all 10)
SELECT 
    pl.last_message_id,
    pl.last_message_created_at,
    pl.updated_at,
    -- Count how many messages exist with this exact created_at
    (SELECT COUNT(*) FROM queen.messages m 
     WHERE m.partition_id = pl.partition_id 
       AND m.created_at = pl.last_message_created_at) as messages_at_same_time
FROM queen.partition_lookup pl
WHERE pl.queue_name = 'test-lookup-queue';

-- messages_at_same_time might be 10 (batch inserted with same NOW())
-- But last_message_id should be the newest UUIDv7
```

### 4. Test Partition Selection Query

```sql
-- Test the new partition selection query
EXPLAIN ANALYZE
SELECT p.id, p.name
FROM queen.partition_lookup pl
JOIN queen.partitions p ON p.id = pl.partition_id
LEFT JOIN queen.partition_consumers pc ON pc.partition_id = pl.partition_id 
    AND pc.consumer_group = '__QUEUE_MODE__'
WHERE pl.queue_name = 'test-lookup-queue'
  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
  AND (pc.last_consumed_created_at IS NULL 
       OR pl.last_message_created_at > pc.last_consumed_created_at
       OR (pl.last_message_created_at = pc.last_consumed_created_at 
           AND pl.last_message_id > pc.last_consumed_id))
ORDER BY pc.last_consumed_at ASC NULLS FIRST, pl.last_message_created_at DESC
LIMIT 10;

-- Expected: 
-- - Execution time: <5ms
-- - Plan should use Index Scan on partition_lookup
-- - NO Seq Scan on messages table
```

### 5. Test Load Balancing Behavior

```sql
-- Create multiple partitions with messages
INSERT INTO queen.partitions (queue_id, name) 
SELECT id, 'partition-' || i 
FROM queen.queues 
CROSS JOIN generate_series(1, 5) i
WHERE name = 'test-lookup-queue';

-- Insert messages to each partition
WITH partitions_list AS (
    SELECT p.id as partition_id, p.name
    FROM queen.partitions p
    JOIN queen.queues q ON q.id = p.queue_id
    WHERE q.name = 'test-lookup-queue'
)
INSERT INTO queen.messages (transaction_id, partition_id, payload)
SELECT 
    'multi-' || pl.name || '-' || gen_random_uuid()::text,
    pl.partition_id,
    jsonb_build_object('partition', pl.name)
FROM partitions_list pl;

-- Check partition selection order (should prioritize by last_consumed_at)
SELECT p.id, p.name, pc.last_consumed_at, pl.last_message_created_at
FROM queen.partition_lookup pl
JOIN queen.partitions p ON p.id = pl.partition_id
LEFT JOIN queen.partition_consumers pc ON pc.partition_id = pl.partition_id 
    AND pc.consumer_group = '__QUEUE_MODE__'
WHERE pl.queue_name = 'test-lookup-queue'
  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
  AND (pc.last_consumed_created_at IS NULL 
       OR pl.last_message_created_at > pc.last_consumed_created_at
       OR (pl.last_message_created_at = pc.last_consumed_created_at 
           AND pl.last_message_id > pc.last_consumed_id))
ORDER BY pc.last_consumed_at ASC NULLS FIRST, pl.last_message_created_at DESC;

-- Expected: Partitions with NULL last_consumed_at first, then oldest consumed
```

### 6. Test Concurrency Safety

```sql
-- Simulate out-of-order commits
BEGIN;
INSERT INTO queen.messages (id, transaction_id, partition_id, payload, created_at)
SELECT 
    '00000000-0000-7000-0000-000000000001'::uuid,  -- Older UUIDv7
    'old-txn',
    p.id,
    '{"test": "old"}'::jsonb,
    NOW() - INTERVAL '1 second'
FROM queen.partitions p
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'test-lookup-queue' AND p.name = 'test-partition';
-- Don't commit yet

-- In another session, insert a newer message and commit first
-- (Simulate this by committing a newer message in this session first)

ROLLBACK;

-- Insert newer message
INSERT INTO queen.messages (id, transaction_id, partition_id, payload)
SELECT 
    '00000000-0000-7999-ffff-ffffffffffff'::uuid,  -- Newer UUIDv7
    'new-txn',
    p.id,
    '{"test": "new"}'::jsonb
FROM queen.partitions p
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'test-lookup-queue' AND p.name = 'test-partition';

-- Check lookup table has the newer message
SELECT last_message_id, last_message_created_at
FROM queen.partition_lookup
WHERE queue_name = 'test-lookup-queue';

-- Should show the newer UUIDv7, even if older message commits later
```

### 7. Cleanup Test Data

```sql
-- Remove test data
DELETE FROM queen.messages 
WHERE transaction_id LIKE 'test-txn-%' 
   OR transaction_id LIKE 'batch-txn-%'
   OR transaction_id LIKE 'multi-%'
   OR transaction_id IN ('old-txn', 'new-txn');

DELETE FROM queen.partitions 
WHERE name LIKE 'test-partition%' OR name LIKE 'partition-%';

DELETE FROM queen.queues 
WHERE name = 'test-lookup-queue';

-- Verify cleanup
SELECT COUNT(*) FROM queen.partition_lookup WHERE queue_name = 'test-lookup-queue';
-- Should return 0 (cascade delete)
```

## Integration Testing with Client

```bash
# Navigate to client test directory
cd client-js/test-v2

# Push messages to a queue
node push.js

# Check lookup table updated
psql $DATABASE_URL -c "SELECT * FROM queen.partition_lookup ORDER BY updated_at DESC LIMIT 5;"

# Consume messages (should use fast query)
node consume.js

# Check logs for "Trying partition '...' (priority by last_consumed_at)"
# This indicates the new query is being used
```

## Performance Benchmarking

```sql
-- Compare old vs new query performance
-- OLD QUERY (for reference, don't run in production):
EXPLAIN ANALYZE
SELECT p.id, p.name, COUNT(m.id) as message_count
FROM queen.partitions p
JOIN queen.queues q ON p.queue_id = q.id
LEFT JOIN queen.messages m ON m.partition_id = p.id
LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
    AND pc.consumer_group = '__QUEUE_MODE__'
WHERE q.name = 'your-queue-name'
  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
  AND m.id IS NOT NULL
  AND (pc.last_consumed_created_at IS NULL
       OR m.created_at > pc.last_consumed_created_at
       OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
GROUP BY p.id, p.name
HAVING COUNT(m.id) > 0
ORDER BY COUNT(m.id) DESC
LIMIT 10;

-- NEW QUERY (should be much faster):
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

-- Compare:
-- - Execution Time: Should see 10-100x improvement
-- - Planning Time: Should be similar
-- - Row count: Should be similar
-- - Scan types: New query should avoid Seq Scan on messages
```

## Success Criteria

✅ All test script checks pass  
✅ Trigger fires on message insert  
✅ Lookup table updates automatically  
✅ Partition selection query uses lookup table  
✅ Query execution time <5ms  
✅ No sequential scans on messages table  
✅ Round-robin load balancing works  
✅ No stale data detected  
✅ Batch inserts handled correctly  
✅ Concurrency safety verified

