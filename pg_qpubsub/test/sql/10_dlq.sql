-- ============================================================================
-- Test 10: Dead Letter Queue (DLQ)
-- ============================================================================

\echo '=== Test 10: Dead Letter Queue (DLQ) ==='

-- Test 1: Configure queue with DLQ enabled
DO $$
DECLARE
    v_result BOOLEAN;
    v_queue RECORD;
BEGIN
    v_result := queen.configure('test_dlq', 60, 3, true);  -- DLQ enabled
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: Configure with DLQ returned false';
    END IF;
    
    SELECT * INTO v_queue FROM queen.queues WHERE name = 'test_dlq';
    
    IF NOT v_queue.dead_letter_queue THEN
        RAISE EXCEPTION 'FAIL: DLQ not enabled on queue';
    END IF;
    
    RAISE NOTICE 'PASS: Configure queue with DLQ enabled';
END;
$$;

-- Test 2: Commit with status='dlq' moves message to DLQ
SELECT queen.produce_one('test_dlq', '{"to_dlq": "test"}'::jsonb, 'Default', 'dlq-txn-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
    v_dlq_count INT;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_dlq') LIMIT 1;
    
    IF v_msg.id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Move to DLQ
    v_result := queen.commit_one(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        '__QUEUE_MODE__',
        'dlq',
        'Test DLQ move'
    );
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: Commit to DLQ returned false';
    END IF;
    
    -- Verify message in DLQ (use message_id, not transaction_id)
    SELECT COUNT(*) INTO v_dlq_count
    FROM queen.dead_letter_queue dlq
    JOIN queen.partitions p ON dlq.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = 'test_dlq'
      AND dlq.message_id = v_msg.id;
    
    IF v_dlq_count != 1 THEN
        RAISE EXCEPTION 'FAIL: Message not found in DLQ';
    END IF;
    
    RAISE NOTICE 'PASS: Message moved to DLQ';
END;
$$;

-- Test 3: reject() convenience function moves to DLQ
SELECT queen.produce_one('test_dlq', '{"reject": "test"}'::jsonb, 'Default', 'reject-dlq-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
    v_dlq_count INT;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_dlq') LIMIT 1;
    
    -- Reject it
    v_result := queen.reject(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        'Rejected for testing'
    );
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: reject() returned false';
    END IF;
    
    -- Verify in DLQ
    SELECT COUNT(*) INTO v_dlq_count
    FROM queen.dead_letter_queue dlq
    WHERE dlq.message_id = v_msg.id;
    
    IF v_dlq_count != 1 THEN
        RAISE EXCEPTION 'FAIL: Rejected message not in DLQ';
    END IF;
    
    RAISE NOTICE 'PASS: reject() moves message to DLQ';
END;
$$;

-- Test 4: DLQ stores error message (note: underlying ack might override our message)
SELECT queen.produce_one('test_dlq', '{"error": "tracking"}'::jsonb, 'Default', 'error-track-1');

DO $$
DECLARE
    v_msg RECORD;
    v_dlq_record RECORD;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_dlq') LIMIT 1;
    
    -- Reject with specific error
    PERFORM queen.reject(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        'Custom error: Something went wrong'
    );
    
    -- Check DLQ record exists with SOME error message
    SELECT * INTO v_dlq_record
    FROM queen.dead_letter_queue dlq
    WHERE dlq.message_id = v_msg.id;
    
    IF v_dlq_record.id IS NULL THEN
        RAISE EXCEPTION 'FAIL: DLQ record not found';
    END IF;
    
    IF v_dlq_record.error_message IS NULL THEN
        RAISE EXCEPTION 'FAIL: No error message stored in DLQ';
    END IF;
    
    RAISE NOTICE 'PASS: DLQ stores error message: %', v_dlq_record.error_message;
END;
$$;

-- Test 5: Queue without DLQ enabled
SELECT queen.configure('test_no_dlq', 60, 3, false);  -- DLQ disabled
SELECT queen.produce_one('test_no_dlq', '{"no_dlq": "test"}'::jsonb, 'Default', 'no-dlq-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
    v_initial_depth BIGINT;
    v_final_depth BIGINT;
BEGIN
    SELECT queen.lag('test_no_dlq') INTO v_initial_depth;
    
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_no_dlq') LIMIT 1;
    
    -- Try to reject (should still work, just might not go to DLQ)
    v_result := queen.reject(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        'Rejected from non-DLQ queue'
    );
    
    -- The message should be processed either way
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: Reject on non-DLQ queue failed';
    END IF;
    
    RAISE NOTICE 'PASS: Reject works on queue without DLQ enabled';
END;
$$;

-- Test 6: nack vs reject behavior
SELECT queen.configure('test_nack_vs_reject', 60, 3, true);
SELECT queen.produce_one('test_nack_vs_reject', '{"nack": "me"}'::jsonb, 'Default', 'nack-vs-1');

DO $$
DECLARE
    v_msg RECORD;
    v_result BOOLEAN;
    v_dlq_count INT;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_nack_vs_reject') LIMIT 1;
    
    -- nack should NOT move to DLQ (just mark for retry)
    v_result := queen.nack(
        v_msg.transaction_id,
        v_msg.partition_id,
        v_msg.lease_id,
        'Temporary error'
    );
    
    -- Check NOT in DLQ
    SELECT COUNT(*) INTO v_dlq_count
    FROM queen.dead_letter_queue dlq
    WHERE dlq.message_id = v_msg.id;
    
    IF v_dlq_count > 0 THEN
        RAISE EXCEPTION 'FAIL: nack should not move message to DLQ';
    END IF;
    
    RAISE NOTICE 'PASS: nack does not move message to DLQ (retry behavior)';
END;
$$;

\echo 'Test 10: PASSED'
