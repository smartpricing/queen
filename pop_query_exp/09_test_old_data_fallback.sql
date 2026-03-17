-- ============================================================================
-- Test: Verify Phase 2 fallback retrieves old uncommitted data
-- ============================================================================
-- This test ensures that data older than 10 minutes (outside Phase 1 window)
-- is still retrieved via the Phase 2 fallback.
--
-- Run with: PG_PASSWORD=postgres psql -h localhost -U postgres -f 09_test_old_data_fallback.sql
-- ============================================================================

\echo ''
\echo '╔══════════════════════════════════════════════════════════════════════════╗'
\echo '║   TEST: Old Uncommitted Data Retrieval (Phase 2 Fallback)                ║'
\echo '╚══════════════════════════════════════════════════════════════════════════╝'
\echo ''

-- ============================================================================
-- Setup: Create test queue and partition with OLD data (1 hour ago)
-- ============================================================================
\echo 'Setting up test data...'

-- Create test queue
INSERT INTO queen.queues (name, namespace, task, lease_time)
VALUES ('test_old_data_queue', 'test', 'fallback', 60)
ON CONFLICT (name) DO NOTHING;

-- Create partition
INSERT INTO queen.partitions (queue_id, name)
SELECT q.id, 'old_data_partition'
FROM queen.queues q
WHERE q.name = 'test_old_data_queue'
ON CONFLICT (queue_id, name) DO NOTHING;

-- Get partition ID
SELECT p.id AS partition_id 
FROM queen.partitions p
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'test_old_data_queue' AND p.name = 'old_data_partition' \gset

-- Insert message with created_at = 1 hour ago
INSERT INTO queen.messages (partition_id, transaction_id, payload, created_at)
VALUES (
    :'partition_id'::uuid,
    'test_old_txn_' || gen_random_uuid()::text,
    '{"test": "old_data_fallback", "age": "1 hour"}'::jsonb,
    NOW() - interval '1 hour'
);

-- Manually update partition_lookup to have updated_at = 1 hour ago
-- (Simulating a partition that hasn't received new messages recently)
UPDATE queen.partition_lookup
SET updated_at = NOW() - interval '1 hour',
    last_message_created_at = NOW() - interval '1 hour'
WHERE queue_name = 'test_old_data_queue';

\echo 'Test data created:'
SELECT 
    pl.queue_name,
    p.name AS partition_name,
    pl.updated_at,
    ROUND(EXTRACT(EPOCH FROM (NOW() - pl.updated_at)) / 60) AS minutes_old,
    pl.last_message_created_at
FROM queen.partition_lookup pl
JOIN queen.partitions p ON p.id = pl.partition_id
WHERE pl.queue_name = 'test_old_data_queue';

-- ============================================================================
-- Test: POP should retrieve the old data via Phase 2 fallback
-- ============================================================================
\echo ''
\echo 'Testing pop_unified_batch with old data (> 10 minutes old)...'
\echo ''

-- Call pop_unified_batch
\echo 'Calling pop_unified_batch:'
SELECT jsonb_pretty(queen.pop_unified_batch('[
    {
        "idx": 0,
        "queue_name": "test_old_data_queue",
        "consumer_group": "test_fallback_consumer",
        "batch_size": 10,
        "worker_id": "test-worker-1"
    }
]'::jsonb)) AS pop_result;

-- ============================================================================
-- Verify: Check if we got the message
-- ============================================================================
\echo ''
\echo 'Verification:'

DO $$
DECLARE
    v_result JSONB;
    v_success BOOLEAN;
    v_msg_count INT;
    v_msg_data JSONB;
BEGIN
    -- Call pop
    SELECT queen.pop_unified_batch('[
        {
            "idx": 0,
            "queue_name": "test_old_data_queue",
            "consumer_group": "test_fallback_consumer_2",
            "batch_size": 10,
            "worker_id": "test-worker-2"
        }
    ]'::jsonb) INTO v_result;
    
    -- Extract results
    v_success := (v_result->0->'result'->>'success')::boolean;
    v_msg_count := jsonb_array_length(v_result->0->'result'->'messages');
    
    IF v_success AND v_msg_count > 0 THEN
        v_msg_data := v_result->0->'result'->'messages'->0->'data';
        RAISE NOTICE '';
        RAISE NOTICE '✅ TEST PASSED: Old data retrieved successfully!';
        RAISE NOTICE '   - Success: %', v_success;
        RAISE NOTICE '   - Messages retrieved: %', v_msg_count;
        RAISE NOTICE '   - Message data: %', v_msg_data;
        RAISE NOTICE '';
        RAISE NOTICE '   Phase 2 fallback is working correctly.';
        RAISE NOTICE '   Data older than 10 minutes is NOT lost.';
    ELSE
        RAISE NOTICE '';
        RAISE NOTICE '❌ TEST FAILED: Old data was NOT retrieved!';
        RAISE NOTICE '   - Success: %', v_success;
        RAISE NOTICE '   - Messages: %', v_msg_count;
        RAISE NOTICE '   - Full result: %', v_result;
        RAISE NOTICE '';
        RAISE NOTICE '   This means Phase 2 fallback is broken!';
    END IF;
END $$;

-- ============================================================================
-- Cleanup
-- ============================================================================
\echo ''
\echo 'Cleaning up test data...'

DELETE FROM queen.queues WHERE name = 'test_old_data_queue';

\echo 'Cleanup complete.'
\echo ''
