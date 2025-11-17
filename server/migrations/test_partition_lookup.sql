-- ============================================================================
-- Test Script: Partition Lookup Optimization
-- Purpose: Verify the partition_lookup table and trigger are working correctly
-- ============================================================================

-- Step 1: Check if table exists
SELECT 
    'partition_lookup table exists' as check_name,
    CASE WHEN EXISTS (
        SELECT 1 FROM information_schema.tables 
        WHERE table_schema = 'queen' AND table_name = 'partition_lookup'
    ) THEN '✓ PASS' ELSE '✗ FAIL' END as status;

-- Step 2: Check if indexes exist
SELECT 
    'partition_lookup indexes' as check_name,
    CASE WHEN COUNT(*) = 3 THEN '✓ PASS (' || COUNT(*) || ' indexes)' 
         ELSE '✗ FAIL (expected 3, found ' || COUNT(*) || ')' END as status
FROM pg_indexes 
WHERE schemaname = 'queen' 
  AND tablename = 'partition_lookup'
  AND indexname IN (
      'idx_partition_lookup_queue_name',
      'idx_partition_lookup_partition_id', 
      'idx_partition_lookup_timestamp'
  );

-- Step 3: Check if trigger exists
SELECT 
    'partition_lookup trigger exists' as check_name,
    CASE WHEN EXISTS (
        SELECT 1 FROM pg_trigger 
        WHERE tgname = 'trg_update_partition_lookup'
    ) THEN '✓ PASS' ELSE '✗ FAIL' END as status;

-- Step 4: Check trigger is statement-level (not row-level)
SELECT 
    'trigger is statement-level' as check_name,
    CASE 
        WHEN tgtype & 1 = 1 THEN '✓ PASS (statement-level)'
        ELSE '✗ FAIL (row-level - should be statement-level)'
    END as status
FROM pg_trigger 
WHERE tgname = 'trg_update_partition_lookup';

-- Step 5: Verify lookup table has data (if messages exist)
SELECT 
    'lookup table populated' as check_name,
    CASE 
        WHEN (SELECT COUNT(*) FROM queen.messages) = 0 
            THEN '⚠ SKIP (no messages yet)'
        WHEN (SELECT COUNT(*) FROM queen.partition_lookup) > 0 
            THEN '✓ PASS (' || (SELECT COUNT(*) FROM queen.partition_lookup) || ' partitions tracked)'
        ELSE '✗ FAIL (messages exist but lookup table is empty)'
    END as status;

-- Step 6: Check for stale data (newer messages not in lookup)
SELECT 
    'lookup table consistency' as check_name,
    CASE 
        WHEN (SELECT COUNT(*) FROM queen.messages) = 0 
            THEN '⚠ SKIP (no messages yet)'
        WHEN NOT EXISTS (
            SELECT 1 
            FROM queen.partition_lookup pl
            WHERE EXISTS (
                SELECT 1 FROM queen.messages m 
                WHERE m.partition_id = pl.partition_id 
                  AND (m.created_at > pl.last_message_created_at
                       OR (m.created_at = pl.last_message_created_at AND m.id > pl.last_message_id))
            )
        ) THEN '✓ PASS (all partitions up-to-date)'
        ELSE '✗ FAIL (some partitions have stale data)'
    END as status;

-- Step 7: Show sample data from lookup table
SELECT 
    '--- Sample Lookup Data ---' as info,
    '' as details
UNION ALL
SELECT 
    pl.queue_name,
    'partition: ' || p.name || 
    ', last_msg: ' || pl.last_message_id::text || 
    ', updated: ' || pl.updated_at::text as details
FROM queen.partition_lookup pl
JOIN queen.partitions p ON p.id = pl.partition_id
ORDER BY pl.updated_at DESC
LIMIT 5;

-- Step 8: Performance comparison hint
SELECT 
    '--- Performance Test ---' as info,
    'Run EXPLAIN ANALYZE on the new partition selection query to verify performance' as details
UNION ALL
SELECT 
    'Expected: <5ms execution time, no sequential scans on messages table' as info,
    '' as details;

