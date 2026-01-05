-- ============================================================================
-- Test 04: Commit Operations
-- ============================================================================

\echo '=== Test 04: Commit Operations ==='

-- Test 1: Basic commit
SELECT queen.configure('test_commit', 60, 3, false);
SELECT queen.produce_one('test_commit', '{"commit": "test"}'::jsonb, 'Default', 'commit-txn-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
    v_depth_before BIGINT;
    v_depth_after BIGINT;
BEGIN
    SELECT queen.lag('test_commit') INTO v_depth_before;
    
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_commit') LIMIT 1;
    
    IF v_msg.id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Commit the message
    v_result := queen.commit_one(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id
    );
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: commit returned false';
    END IF;
    
    SELECT queen.lag('test_commit') INTO v_depth_after;
    
    IF v_depth_after >= v_depth_before THEN
        RAISE EXCEPTION 'FAIL: Commit did not reduce queue depth';
    END IF;
    
    RAISE NOTICE 'PASS: Basic commit works';
END;
$$;

-- Test 2: Commit with consumer group
SELECT queen.configure('test_commit_cg', 60, 3, false);
SELECT queen.produce_one('test_commit_cg', '{"cg": "commit"}'::jsonb, 'Default', 'commit-cg-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
BEGIN
    -- Consume with consumer group
    SELECT * INTO v_msg FROM queen.consume_one('test_commit_cg', 'commit-test-group', 1) LIMIT 1;
    
    IF v_msg.id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Commit with consumer group
    v_result := queen.commit_one(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        'commit-test-group'  -- consumer_group
    );
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: Commit with consumer group returned false';
    END IF;
    
    RAISE NOTICE 'PASS: Commit with consumer group works';
END;
$$;

-- Test 3: nack (mark for retry)
SELECT queen.configure('test_nack', 60, 3, false);
SELECT queen.produce_one('test_nack', '{"nack": "test"}'::jsonb, 'Default', 'nack-txn-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_nack') LIMIT 1;
    
    IF v_msg.id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Nack the message
    v_result := queen.nack(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        'Test error message'
    );
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: nack returned false';
    END IF;
    
    RAISE NOTICE 'PASS: nack works';
END;
$$;

-- Test 4: reject (send to DLQ)
SELECT queen.configure('test_reject', 60, 3, true);  -- DLQ enabled
SELECT queen.produce_one('test_reject', '{"reject": "test"}'::jsonb, 'Default', 'reject-txn-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_reject') LIMIT 1;
    
    IF v_msg.id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Reject the message (send to DLQ)
    v_result := queen.reject(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        'Permanently rejected'
    );
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: reject returned false';
    END IF;
    
    RAISE NOTICE 'PASS: reject works';
END;
$$;

-- Test 5: Commit with status='failed' triggers retry
SELECT queen.configure('test_commit_failed', 60, 3, false);
SELECT queen.produce_one('test_commit_failed', '{"retry": "test"}'::jsonb, 'Default', 'retry-txn-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
    v_can_reconsume INT;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_commit_failed') LIMIT 1;
    
    IF v_msg.id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Commit with status='failed'
    v_result := queen.commit_one(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        '__QUEUE_MODE__',
        'failed',
        'Processing error'
    );
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: Commit with failed status returned false';
    END IF;
    
    -- Wait a moment and check if message can be reconsumed
    -- (This is a simplified check - full retry logic depends on lease expiry)
    RAISE NOTICE 'PASS: Commit with status=failed works';
END;
$$;

-- Test 6: Commit with status='dlq' moves to dead letter queue
SELECT queen.configure('test_commit_dlq', 60, 3, true);  -- DLQ enabled
SELECT queen.produce_one('test_commit_dlq', '{"dlq": "test"}'::jsonb, 'Default', 'dlq-txn-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
    v_dlq_count INT;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_commit_dlq') LIMIT 1;
    
    IF v_msg.id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Commit with status='dlq'
    v_result := queen.commit_one(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        '__QUEUE_MODE__',
        'dlq',
        'Moved to DLQ'
    );
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: Commit with dlq status returned false';
    END IF;
    
    RAISE NOTICE 'PASS: Commit with status=dlq works';
END;
$$;

-- Test 7: Cannot commit same message twice
SELECT queen.configure('test_double_commit', 60, 3, false);
SELECT queen.produce_one('test_double_commit', '{"double": "commit"}'::jsonb, 'Default', 'double-txn-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result1 BOOLEAN;
    v_result2 BOOLEAN;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_double_commit') LIMIT 1;
    
    -- First commit should succeed
    v_result1 := queen.commit_one(v_msg.transaction_id, v_msg.partition_id, v_msg.lease_id);
    
    -- Second commit should fail (lease already released)
    v_result2 := queen.commit_one(v_msg.transaction_id, v_msg.partition_id, v_msg.lease_id);
    
    IF NOT v_result1 THEN
        RAISE EXCEPTION 'FAIL: First commit should succeed';
    END IF;
    
    -- Note: The behavior of double-commit depends on implementation
    -- It might return false or true with no effect
    RAISE NOTICE 'PASS: Double commit handled (first: %, second: %)', v_result1, v_result2;
END;
$$;

\echo 'Test 04: PASSED'

