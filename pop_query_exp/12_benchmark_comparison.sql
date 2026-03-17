-- =============================================================================
-- BENCHMARK: Original vs Watermark-optimized pop_unified_batch
-- =============================================================================

\echo '============================================='
\echo 'SETUP: Create test queue with 15k partitions'
\echo '============================================='

-- Clean up
DELETE FROM queen.consumer_watermarks WHERE queue_name = 'benchmark_queue';
DELETE FROM queen.messages WHERE partition_id IN (
    SELECT id FROM queen.partitions WHERE queue_id = (
        SELECT id FROM queen.queues WHERE name = 'benchmark_queue'
    )
);
DELETE FROM queen.partition_consumers WHERE partition_id IN (
    SELECT id FROM queen.partitions WHERE queue_id = (
        SELECT id FROM queen.queues WHERE name = 'benchmark_queue'
    )
);
DELETE FROM queen.partition_lookup WHERE queue_name = 'benchmark_queue';
DELETE FROM queen.partitions WHERE queue_id = (
    SELECT id FROM queen.queues WHERE name = 'benchmark_queue'
);
DELETE FROM queen.queues WHERE name = 'benchmark_queue';

-- Create test queue
INSERT INTO queen.queues (name, lease_time) VALUES ('benchmark_queue', 30);

-- Create 15000 partitions with OLD updated_at
DO $$
DECLARE
    v_queue_id UUID;
    v_partition_id UUID;
BEGIN
    SELECT id INTO v_queue_id FROM queen.queues WHERE name = 'benchmark_queue';
    
    FOR i IN 1..15000 LOOP
        INSERT INTO queen.partitions (queue_id, name)
        VALUES (v_queue_id, 'partition_' || i)
        RETURNING id INTO v_partition_id;
        
        INSERT INTO queen.partition_lookup (
            queue_name, partition_id,
            last_message_id, last_message_created_at, updated_at
        ) VALUES (
            'benchmark_queue', v_partition_id,
            gen_random_uuid(), NOW() - interval '1 hour', NOW() - interval '1 hour'
        );
    END LOOP;
    
    RAISE NOTICE 'Created 15000 partitions';
END $$;

-- Mark all partitions as CONSUMED (simulate up-to-date consumer)
INSERT INTO queen.partition_consumers (partition_id, consumer_group, last_consumed_id, last_consumed_created_at)
SELECT pl.partition_id, 'benchmark_consumer', pl.last_message_id, pl.last_message_created_at
FROM queen.partition_lookup pl
WHERE pl.queue_name = 'benchmark_queue';

SELECT COUNT(*) AS partitions_ready FROM queen.partition_consumers WHERE consumer_group = 'benchmark_consumer';

\echo ''
\echo '============================================='
\echo 'BENCHMARK: ORIGINAL (no watermark)'
\echo '============================================='

\echo 'Poll 1 (original):'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch_original(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'benchmark_queue',
            'consumer_group', 'benchmark_consumer',
            'batch_size', 1
        )
    )
);

\echo 'Poll 2 (original):'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch_original(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'benchmark_queue',
            'consumer_group', 'benchmark_consumer',
            'batch_size', 1
        )
    )
);

\echo 'Poll 3 (original):'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch_original(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'benchmark_queue',
            'consumer_group', 'benchmark_consumer',
            'batch_size', 1
        )
    )
);

\echo ''
\echo '============================================='
\echo 'BENCHMARK: OPTIMIZED (with watermark + cached EXISTS)'
\echo '============================================='

-- Clear any existing watermark
DELETE FROM queen.consumer_watermarks 
WHERE queue_name = 'benchmark_queue' AND consumer_group = 'benchmark_consumer';

\echo 'Poll 1 (optimized) - first poll, no watermark, runs EXISTS:'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'benchmark_queue',
            'consumer_group', 'benchmark_consumer',
            'batch_size', 1
        )
    )
);

-- Show watermark was created
SELECT 'Watermark created:' AS status, last_empty_scan_at, updated_at
FROM queen.consumer_watermarks 
WHERE queue_name = 'benchmark_queue' AND consumer_group = 'benchmark_consumer';

\echo 'Poll 2 (optimized) - within 30s, skips EXISTS (FAST!):'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'benchmark_queue',
            'consumer_group', 'benchmark_consumer',
            'batch_size', 1
        )
    )
);

\echo 'Poll 3 (optimized) - within 30s, skips EXISTS (FAST!):'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'benchmark_queue',
            'consumer_group', 'benchmark_consumer',
            'batch_size', 1
        )
    )
);

\echo 'Poll 4 (optimized) - within 30s, skips EXISTS (FAST!):'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'benchmark_queue',
            'consumer_group', 'benchmark_consumer',
            'batch_size', 1
        )
    )
);

\echo ''
\echo 'Waiting 31 seconds for EXISTS cache to expire...'
SELECT pg_sleep(31);

\echo 'Poll 5 (optimized) - after 30s, runs EXISTS again:'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'benchmark_queue',
            'consumer_group', 'benchmark_consumer',
            'batch_size', 1
        )
    )
);

\echo 'Poll 6 (optimized) - within 30s again, skips EXISTS (FAST!):'
EXPLAIN ANALYZE 
SELECT queen.pop_unified_batch(
    jsonb_build_array(
        jsonb_build_object(
            'idx', 0,
            'queue_name', 'benchmark_queue',
            'consumer_group', 'benchmark_consumer',
            'batch_size', 1
        )
    )
);

\echo ''
\echo '============================================='
\echo 'SUMMARY'
\echo '============================================='
\echo 'Compare execution times above:'
\echo '- ORIGINAL: Every poll scans all 15k partitions'
\echo '- OPTIMIZED Poll 1: Scans all + EXISTS check + creates watermark'
\echo '- OPTIMIZED Poll 2+: Only scans recent partitions (should be much faster)'
\echo ''

\echo '============================================='
\echo 'CLEANUP'
\echo '============================================='

DELETE FROM queen.consumer_watermarks WHERE queue_name = 'benchmark_queue';
DELETE FROM queen.partition_consumers WHERE consumer_group = 'benchmark_consumer';
DELETE FROM queen.partition_lookup WHERE queue_name = 'benchmark_queue';
DELETE FROM queen.partitions WHERE queue_id = (SELECT id FROM queen.queues WHERE name = 'benchmark_queue');
DELETE FROM queen.queues WHERE name = 'benchmark_queue';

\echo 'Benchmark complete!'
