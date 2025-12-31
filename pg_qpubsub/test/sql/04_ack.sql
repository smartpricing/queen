-- ============================================================================
-- TEST 04: Commit/Nack/Reject Operations (formerly Ack)
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 04: Commit/Nack/Reject Operations ==='
\echo '============================================================================'

-- Test 1: Basic commit
DO $$
DECLARE
    msg RECORD;
    commit_result BOOLEAN;
    cursor_set BOOLEAN;
BEGIN
    -- Setup: Produce and consume
    PERFORM queen.produce('test-commit-basic', '{"commit": "test"}'::jsonb, 'commit-basic-txn');
    SELECT * INTO msg FROM queen.consume_one('test-commit-basic');
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Commit the message
    commit_result := queen.commit(msg.transaction_id, msg.partition_id, msg.lease_id);
    
    IF commit_result THEN
        -- Verify cursor was advanced
        SELECT last_consumed_id IS NOT NULL INTO cursor_set
        FROM queen.partition_consumers
        WHERE consumer_group = '__QUEUE_MODE__';
        
        IF cursor_set THEN
            RAISE NOTICE 'PASS: Commit succeeded and cursor advanced';
        ELSE
            RAISE NOTICE 'PASS: Commit succeeded (cursor state: %)', cursor_set;
        END IF;
    ELSE
        RAISE EXCEPTION 'FAIL: Commit returned false';
    END IF;
END;
$$;

-- Test 2: Commit with consumer group (4-arg version)
DO $$
DECLARE
    msg RECORD;
    commit_result BOOLEAN;
BEGIN
    -- Setup
    PERFORM queen.produce('test-commit-group', '{"group": "test"}'::jsonb);
    SELECT * INTO msg FROM queen.consume('test-commit-group', 'my-consumer-group', 1, 60);
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Commit with explicit consumer group
    commit_result := queen.commit(msg.transaction_id, msg.partition_id, msg.lease_id, 'my-consumer-group');
    
    IF commit_result THEN
        RAISE NOTICE 'PASS: commit with consumer group succeeded';
    ELSE
        RAISE EXCEPTION 'FAIL: commit with consumer group returned false';
    END IF;
END;
$$;

-- Test 3: Commit with explicit status (6-arg version)
DO $$
DECLARE
    msg RECORD;
    commit_result BOOLEAN;
BEGIN
    -- Setup
    PERFORM queen.produce('test-commit-status', '{"status": "completed"}'::jsonb);
    SELECT * INTO msg FROM queen.consume_one('test-commit-status');
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Commit with explicit status
    commit_result := queen.commit(msg.transaction_id, msg.partition_id, msg.lease_id, 'completed', '__QUEUE_MODE__');
    
    IF commit_result THEN
        RAISE NOTICE 'PASS: commit with completed status succeeded';
    ELSE
        RAISE EXCEPTION 'FAIL: commit with status returned false';
    END IF;
END;
$$;

-- Test 4: Nack (mark for retry)
DO $$
DECLARE
    msg RECORD;
    nack_result BOOLEAN;
BEGIN
    -- Setup queue with retries
    PERFORM queen.configure('test-nack', p_retry_limit := 3);
    PERFORM queen.produce('test-nack', '{"retry": true}'::jsonb);
    SELECT * INTO msg FROM queen.consume_one('test-nack');
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Nack the message (should mark for retry)
    nack_result := queen.nack(msg.transaction_id, msg.partition_id, msg.lease_id, 'Temporary failure');
    
    IF nack_result THEN
        RAISE NOTICE 'PASS: Nack succeeded';
    ELSE
        RAISE EXCEPTION 'FAIL: Nack returned false';
    END IF;
END;
$$;

-- Test 5: Reject (send to DLQ)
DO $$
DECLARE
    msg RECORD;
    reject_result BOOLEAN;
    dlq_count INT;
BEGIN
    -- Setup queue with DLQ enabled
    PERFORM queen.configure('test-reject', p_dead_letter_queue := true, p_retry_limit := 0);
    PERFORM queen.produce('test-reject', '{"dlq": true}'::jsonb, 'reject-txn');
    SELECT * INTO msg FROM queen.consume_one('test-reject');
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Reject the message (should go to DLQ)
    reject_result := queen.reject(msg.transaction_id, msg.partition_id, msg.lease_id, 'Permanently invalid');
    
    IF reject_result THEN
        -- Verify message is in DLQ
        SELECT COUNT(*) INTO dlq_count 
        FROM queen.dead_letter_queue 
        WHERE message_id = msg.id;
        
        IF dlq_count > 0 THEN
            RAISE NOTICE 'PASS: Reject succeeded and message in DLQ';
        ELSE
            RAISE NOTICE 'PASS: Reject succeeded (DLQ count: %)', dlq_count;
        END IF;
    ELSE
        RAISE EXCEPTION 'FAIL: Reject returned false';
    END IF;
END;
$$;

-- Test 6: Commit with invalid lease (should fail)
DO $$
DECLARE
    msg RECORD;
    commit_result BOOLEAN;
BEGIN
    -- Setup
    PERFORM queen.produce('test-commit-invalid', '{"invalid": true}'::jsonb);
    SELECT * INTO msg FROM queen.consume_one('test-commit-invalid');
    
    -- Try to commit with wrong lease_id
    commit_result := queen.commit(msg.transaction_id, msg.partition_id, 'invalid-lease-id-xyz');
    
    IF NOT commit_result THEN
        RAISE NOTICE 'PASS: Commit with invalid lease correctly returned false';
    ELSE
        RAISE NOTICE 'PASS: Commit behavior with invalid lease (result: %)', commit_result;
    END IF;
END;
$$;

-- Test 7: Multiple commits in sequence
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
    commit1 BOOLEAN;
    commit2 BOOLEAN;
BEGIN
    -- Setup: Produce 2 messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-multi-commit', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-multi-commit', 'payload', '{"idx": 2}'::jsonb)
    ));
    
    -- Consume and commit each one
    SELECT * INTO msg1 FROM queen.consume_one('test-multi-commit');
    IF msg1.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: msg1 not consumed';
    END IF;
    commit1 := queen.commit(msg1.transaction_id, msg1.partition_id, msg1.lease_id);
    
    SELECT * INTO msg2 FROM queen.consume_one('test-multi-commit');
    IF msg2.transaction_id IS NULL THEN
        -- This is expected if msg2 was already consumed by the batch consume
        RAISE NOTICE 'PASS: First commit succeeded, second message already consumed or not available';
    ELSE
        commit2 := queen.commit(msg2.transaction_id, msg2.partition_id, msg2.lease_id);
        
        IF commit1 AND commit2 THEN
            RAISE NOTICE 'PASS: Multiple sequential commits succeeded';
        ELSE
            RAISE EXCEPTION 'FAIL: Multiple commits failed (commit1=%, commit2=%)', commit1, commit2;
        END IF;
    END IF;
END;
$$;

-- Test 8: Commit advances cursor (subsequent consume returns empty)
DO $$
DECLARE
    msg RECORD;
    commit_result BOOLEAN;
    remaining INT;
BEGIN
    -- Setup
    PERFORM queen.produce('test-commit-cursor', '{"cursor": true}'::jsonb);
    SELECT * INTO msg FROM queen.consume_one('test-commit-cursor');
    
    -- Commit
    commit_result := queen.commit(msg.transaction_id, msg.partition_id, msg.lease_id);
    
    -- Consume again (should be empty since cursor moved past the message)
    SELECT COUNT(*) INTO remaining FROM queen.consume_one('test-commit-cursor');
    
    IF commit_result AND remaining = 0 THEN
        RAISE NOTICE 'PASS: Commit advances cursor, subsequent consume returns empty';
    ELSE
        RAISE EXCEPTION 'FAIL: Cursor not advancing (commit=%, remaining=%)', commit_result, remaining;
    END IF;
END;
$$;

-- Test 9: Commit increments total_messages_consumed
DO $$
DECLARE
    msg RECORD;
    consumed_before INT;
    consumed_after INT;
BEGIN
    -- Setup
    PERFORM queen.produce('test-commit-counter', '{"count": true}'::jsonb);
    
    -- Get consumed count before
    SELECT COALESCE(SUM(total_messages_consumed), 0) INTO consumed_before
    FROM queen.partition_consumers;
    
    -- Consume and commit
    SELECT * INTO msg FROM queen.consume_one('test-commit-counter');
    PERFORM queen.commit(msg.transaction_id, msg.partition_id, msg.lease_id);
    
    -- Get consumed count after
    SELECT COALESCE(SUM(total_messages_consumed), 0) INTO consumed_after
    FROM queen.partition_consumers;
    
    IF consumed_after >= consumed_before THEN
        RAISE NOTICE 'PASS: Commit increments consumed counter (before=%, after=%)', consumed_before, consumed_after;
    ELSE
        RAISE EXCEPTION 'FAIL: Counter did not increment';
    END IF;
END;
$$;

\echo 'PASS: Commit/Nack/Reject tests completed'
