-- =============================================================================
-- TEST: Watermark optimization for wildcard POP
-- Run with: PG_PASSWORD=postgres psql -h localhost -U postgres -d queen -f 10_test_watermark.sql
-- =============================================================================

\echo '============================================='
\echo 'SETUP: Create test queue and partitions'
\echo '============================================='

-- Clean up any previous test data
DELETE FROM queen.consumer_watermarks WHERE queue_name = 'watermark_test_queue';
DELETE FROM queen.messages WHERE partition_id IN (
    SELECT id FROM queen.partitions WHERE queue_id = (
        SELECT id FROM queen.queues WHERE name = 'watermark_test_queue'
    )
);
DELETE FROM queen.partition_consumers WHERE partition_id IN (
    SELECT id FROM queen.partitions WHERE queue_id = (
        SELECT id FROM queen.queues WHERE name = 'watermark_test_queue'
    )
);
DELETE FROM queen.partition_lookup WHERE queue_name = 'watermark_test_queue';
DELETE FROM queen.partitions WHERE queue_id = (
    SELECT id FROM queen.queues WHERE name = 'watermark_test_queue'
);
DELETE FROM queen.queues WHERE name = 'watermark_test_queue';

-- Create test queue
INSERT INTO queen.queues (name, lease_time) VALUES ('watermark_test_queue', 30);

-- Create 15000 partitions with OLD updated_at (simulating stale data)
DO $$
DECLARE
    v_queue_id UUID;
    v_partition_id UUID;
BEGIN
    SELECT id INTO v_queue_id FROM queen.queues WHERE name = 'watermark_test_queue';
    
    FOR i IN 1..15000 LOOP
        INSERT INTO queen.partitions (queue_id, name)
        VALUES (v_queue_id, 'partition_' || i)
        RETURNING id INTO v_partition_id;
        
        -- Insert partition_lookup with OLD updated_at (1 hour ago)
        INSERT INTO queen.partition_lookup (
            queue_name, partition_id,
            last_message_id, last_message_created_at, updated_at
        ) VALUES (
            'watermark_test_queue', v_partition_id,
            gen_random_uuid(), NOW() - interval '1 hour', NOW() - interval '1 hour'
        );
    END LOOP;
    
    RAISE NOTICE 'Created 15000 partitions with updated_at = 1 hour ago';
END $$;

\echo ''
\echo '============================================='
\echo 'TEST 1: Mark all partitions as CONSUMED (simulate up-to-date consumer)'
\echo '============================================='

-- Mark all partitions as fully consumed by the test_consumer
INSERT INTO queen.partition_consumers (partition_id, consumer_group, last_consumed_id, last_consumed_created_at)
SELECT pl.partition_id, 'test_consumer', pl.last_message_id, pl.last_message_created_at
FROM queen.partition_lookup pl
WHERE pl.queue_name = 'watermark_test_queue'
ON CONFLICT (partition_id, consumer_group) DO UPDATE 
SET last_consumed_id = EXCLUDED.last_consumed_id,
    last_consumed_created_at = EXCLUDED.last_consumed_created_at;

SELECT COUNT(*) AS partitions_marked_consumed FROM queen.partition_consumers 
WHERE consumer_group = 'test_consumer';

\echo ''
\echo '============================================='
\echo 'TEST 2: Up-to-date consumer (first poll) - scans all, finds nothing, creates watermark'
\echo '============================================='

-- Verify no watermark exists
SELECT 'Watermark before:' AS label, * FROM queen.consumer_watermarks 
WHERE queue_name = 'watermark_test_queue' AND consumer_group = 'test_consumer';

-- Call pop_unified_batch - up-to-date consumer should scan all but find nothing
\echo 'Calling pop_unified_batch (first poll, no watermark yet)...'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'watermark_test_queue',
            'consumer_group', 'test_consumer',
            'batch_size', 1
        )
    )
);

-- Check if watermark was created (should be NOW since no data was found)
SELECT 'Watermark after (should be ~NOW):' AS label, 
       last_empty_scan_at,
       NOW() - last_empty_scan_at AS age
FROM queen.consumer_watermarks 
WHERE queue_name = 'watermark_test_queue' AND consumer_group = 'test_consumer';

\echo ''
\echo '============================================='
\echo 'TEST 3: Up-to-date consumer (second poll) - should be FAST (only checks recent)'
\echo '============================================='

-- Wait a moment
SELECT pg_sleep(0.1);

-- Call again - now should only scan partitions updated since watermark (~0 partitions)
\echo 'Calling pop_unified_batch (second poll, watermark exists)...'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'watermark_test_queue',
            'consumer_group', 'test_consumer',
            'batch_size', 1
        )
    )
);

\echo ''
\echo '============================================='
\echo 'TEST 4: New message arrives - consumer should find it quickly'
\echo '============================================='

-- Add a NEW message to one partition (trigger updates partition_lookup.updated_at)
DO $$
DECLARE
    v_partition_id UUID;
    v_msg_id UUID := gen_random_uuid();
    v_txn_id VARCHAR(255) := 'test_txn_' || gen_random_uuid()::text;
BEGIN
    SELECT pl.partition_id INTO v_partition_id 
    FROM queen.partition_lookup pl 
    WHERE pl.queue_name = 'watermark_test_queue' 
    LIMIT 1;
    
    -- Insert message (trigger will update partition_lookup automatically)
    INSERT INTO queen.messages (id, transaction_id, partition_id, payload, created_at)
    VALUES (v_msg_id, v_txn_id, v_partition_id, '{"test": "new message"}', NOW());
    
    RAISE NOTICE 'Added new message to partition %', v_partition_id;
END $$;

-- Call pop - should find the new message quickly (only scans recent partitions)
\echo 'Calling pop_unified_batch after new message arrives...'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'watermark_test_queue',
            'consumer_group', 'test_consumer',
            'batch_size', 1
        )
    )
);

-- Watermark should NOT advance (we found data)
SELECT 'Watermark (should NOT change - we found data):' AS label, 
       last_empty_scan_at,
       NOW() - last_empty_scan_at AS age
FROM queen.consumer_watermarks 
WHERE queue_name = 'watermark_test_queue' AND consumer_group = 'test_consumer';

\echo ''
\echo '============================================='
\echo 'TEST 5: Consume the message, poll again - watermark advances'
\echo '============================================='

-- Ack the message (advance the cursor)
DO $$
DECLARE
    v_msg RECORD;
BEGIN
    SELECT pl.partition_id, pl.last_message_id, pl.last_message_created_at 
    INTO v_msg
    FROM queen.partition_lookup pl 
    WHERE pl.queue_name = 'watermark_test_queue' 
      AND pl.updated_at > NOW() - interval '1 minute'
    LIMIT 1;
    
    -- Update partition_consumers to mark as consumed
    UPDATE queen.partition_consumers 
    SET last_consumed_id = v_msg.last_message_id,
        last_consumed_created_at = v_msg.last_message_created_at,
        lease_expires_at = NULL
    WHERE partition_id = v_msg.partition_id 
      AND consumer_group = 'test_consumer';
    
    RAISE NOTICE 'Consumed message from partition %', v_msg.partition_id;
END $$;

-- Poll again - should find nothing, watermark should advance
\echo 'Calling pop_unified_batch after consuming (watermark should advance)...'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'watermark_test_queue',
            'consumer_group', 'test_consumer',
            'batch_size', 1
        )
    )
);

-- Watermark should advance to NOW
SELECT 'Watermark after empty scan (should be fresh):' AS label, 
       last_empty_scan_at,
       NOW() - last_empty_scan_at AS age
FROM queen.consumer_watermarks 
WHERE queue_name = 'watermark_test_queue' AND consumer_group = 'test_consumer';

\echo ''
\echo '============================================='
\echo 'TEST 6: __QUEUE_MODE__ (no consumer group) - same behavior'
\echo '============================================='

-- Mark all partitions as consumed for __QUEUE_MODE__
INSERT INTO queen.partition_consumers (partition_id, consumer_group, last_consumed_id, last_consumed_created_at)
SELECT pl.partition_id, '__QUEUE_MODE__', pl.last_message_id, pl.last_message_created_at
FROM queen.partition_lookup pl
WHERE pl.queue_name = 'watermark_test_queue'
ON CONFLICT (partition_id, consumer_group) DO UPDATE 
SET last_consumed_id = EXCLUDED.last_consumed_id,
    last_consumed_created_at = EXCLUDED.last_consumed_created_at;

-- First poll - should find nothing, create watermark
\echo 'Calling pop_unified_batch with __QUEUE_MODE__ (first poll)...'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'watermark_test_queue',
            'batch_size', 1
        )
    )
);

SELECT 'QUEUE_MODE watermark:' AS label, last_empty_scan_at, NOW() - last_empty_scan_at AS age
FROM queen.consumer_watermarks 
WHERE queue_name = 'watermark_test_queue' AND consumer_group = '__QUEUE_MODE__';

-- Second poll - should be fast
\echo 'Calling pop_unified_batch with __QUEUE_MODE__ (second poll - should be fast)...'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'watermark_test_queue',
            'batch_size', 1
        )
    )
);

\echo ''
\echo '============================================='
\echo 'CLEANUP'
\echo '============================================='

DELETE FROM queen.consumer_watermarks WHERE queue_name = 'watermark_test_queue';
DELETE FROM queen.messages WHERE partition_id IN (
    SELECT id FROM queen.partitions WHERE queue_id = (
        SELECT id FROM queen.queues WHERE name = 'watermark_test_queue'
    )
);
DELETE FROM queen.partition_consumers WHERE partition_id IN (
    SELECT id FROM queen.partitions WHERE queue_id = (
        SELECT id FROM queen.queues WHERE name = 'watermark_test_queue'
    )
);
DELETE FROM queen.partition_lookup WHERE queue_name = 'watermark_test_queue';
DELETE FROM queen.partitions WHERE queue_id = (
    SELECT id FROM queen.queues WHERE name = 'watermark_test_queue'
);
DELETE FROM queen.queues WHERE name = 'watermark_test_queue';

\echo 'Test complete!'
