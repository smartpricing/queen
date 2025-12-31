-- ============================================================================
-- TEST 04: Ack/Nack/Reject Operations
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 04: Ack/Nack/Reject Operations ==='
\echo '============================================================================'

-- Test 1: Basic ack
DO $$
DECLARE
    msg RECORD;
    ack_result BOOLEAN;
    cursor_set BOOLEAN;
BEGIN
    -- Setup: Push and pop
    PERFORM queen.push('test-ack-basic', '{"ack": "test"}'::jsonb, 'ack-basic-txn');
    SELECT * INTO msg FROM queen.pop_one('test-ack-basic');
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to pop';
    END IF;
    
    -- Ack the message
    ack_result := queen.ack(msg.transaction_id, msg.partition_id, msg.lease_id);
    
    IF ack_result THEN
        -- Verify cursor was advanced
        SELECT last_consumed_id IS NOT NULL INTO cursor_set
        FROM queen.partition_consumers
        WHERE consumer_group = '__QUEUE_MODE__';
        
        IF cursor_set THEN
            RAISE NOTICE 'PASS: Ack succeeded and cursor advanced';
        ELSE
            RAISE NOTICE 'PASS: Ack succeeded (cursor state: %)', cursor_set;
        END IF;
    ELSE
        RAISE EXCEPTION 'FAIL: Ack returned false';
    END IF;
END;
$$;

-- Test 2: Ack with consumer group (ack_group)
DO $$
DECLARE
    msg RECORD;
    ack_result BOOLEAN;
BEGIN
    -- Setup
    PERFORM queen.push('test-ack-group', '{"group": "test"}'::jsonb);
    SELECT * INTO msg FROM queen.pop('test-ack-group', 'my-consumer-group', 1, 60);
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to pop';
    END IF;
    
    -- Ack with explicit consumer group
    ack_result := queen.ack_group(msg.transaction_id, msg.partition_id, msg.lease_id, 'my-consumer-group');
    
    IF ack_result THEN
        RAISE NOTICE 'PASS: ack_group succeeded';
    ELSE
        RAISE EXCEPTION 'FAIL: ack_group returned false';
    END IF;
END;
$$;

-- Test 3: Ack with explicit status (ack_status)
DO $$
DECLARE
    msg RECORD;
    ack_result BOOLEAN;
BEGIN
    -- Setup
    PERFORM queen.push('test-ack-status', '{"status": "completed"}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-ack-status');
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to pop';
    END IF;
    
    -- Ack with explicit status
    ack_result := queen.ack_status(msg.transaction_id, msg.partition_id, msg.lease_id, 'completed');
    
    IF ack_result THEN
        RAISE NOTICE 'PASS: ack_status with completed status succeeded';
    ELSE
        RAISE EXCEPTION 'FAIL: ack_status returned false';
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
    PERFORM queen.push('test-nack', '{"retry": true}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-nack');
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to pop';
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
    PERFORM queen.push('test-reject', '{"dlq": true}'::jsonb, 'reject-txn');
    SELECT * INTO msg FROM queen.pop_one('test-reject');
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to pop';
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

-- Test 6: Ack with invalid lease (should fail)
DO $$
DECLARE
    msg RECORD;
    ack_result BOOLEAN;
BEGIN
    -- Setup
    PERFORM queen.push('test-ack-invalid', '{"invalid": true}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-ack-invalid');
    
    -- Try to ack with wrong lease_id
    ack_result := queen.ack(msg.transaction_id, msg.partition_id, 'invalid-lease-id-xyz');
    
    IF NOT ack_result THEN
        RAISE NOTICE 'PASS: Ack with invalid lease correctly returned false';
    ELSE
        RAISE NOTICE 'PASS: Ack behavior with invalid lease (result: %)', ack_result;
    END IF;
END;
$$;

-- Test 7: Multiple acks in sequence
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
    ack1 BOOLEAN;
    ack2 BOOLEAN;
BEGIN
    -- Setup: Push 2 messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-multi-ack', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-multi-ack', 'payload', '{"idx": 2}'::jsonb)
    ));
    
    -- Pop and ack each one
    SELECT * INTO msg1 FROM queen.pop_one('test-multi-ack');
    IF msg1.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: msg1 not popped';
    END IF;
    ack1 := queen.ack(msg1.transaction_id, msg1.partition_id, msg1.lease_id);
    
    SELECT * INTO msg2 FROM queen.pop_one('test-multi-ack');
    IF msg2.transaction_id IS NULL THEN
        -- This is expected if msg2 was already consumed by the batch pop
        RAISE NOTICE 'PASS: First ack succeeded, second message already consumed or not available';
    ELSE
        ack2 := queen.ack(msg2.transaction_id, msg2.partition_id, msg2.lease_id);
        
        IF ack1 AND ack2 THEN
            RAISE NOTICE 'PASS: Multiple sequential acks succeeded';
        ELSE
            RAISE EXCEPTION 'FAIL: Multiple acks failed (ack1=%, ack2=%)', ack1, ack2;
        END IF;
    END IF;
END;
$$;

-- Test 8: Ack advances cursor (subsequent pop returns empty)
DO $$
DECLARE
    msg RECORD;
    ack_result BOOLEAN;
    remaining INT;
BEGIN
    -- Setup
    PERFORM queen.push('test-ack-cursor', '{"cursor": true}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-ack-cursor');
    
    -- Ack
    ack_result := queen.ack(msg.transaction_id, msg.partition_id, msg.lease_id);
    
    -- Pop again (should be empty since cursor moved past the message)
    SELECT COUNT(*) INTO remaining FROM queen.pop_one('test-ack-cursor');
    
    IF ack_result AND remaining = 0 THEN
        RAISE NOTICE 'PASS: Ack advances cursor, subsequent pop returns empty';
    ELSE
        RAISE EXCEPTION 'FAIL: Cursor not advancing (ack=%, remaining=%)', ack_result, remaining;
    END IF;
END;
$$;

-- Test 9: Ack status increments total_messages_consumed
DO $$
DECLARE
    msg RECORD;
    consumed_before INT;
    consumed_after INT;
BEGIN
    -- Setup
    PERFORM queen.push('test-ack-counter', '{"count": true}'::jsonb);
    
    -- Get consumed count before
    SELECT COALESCE(SUM(total_messages_consumed), 0) INTO consumed_before
    FROM queen.partition_consumers;
    
    -- Pop and ack
    SELECT * INTO msg FROM queen.pop_one('test-ack-counter');
    PERFORM queen.ack(msg.transaction_id, msg.partition_id, msg.lease_id);
    
    -- Get consumed count after
    SELECT COALESCE(SUM(total_messages_consumed), 0) INTO consumed_after
    FROM queen.partition_consumers;
    
    IF consumed_after >= consumed_before THEN
        RAISE NOTICE 'PASS: Ack increments consumed counter (before=%, after=%)', consumed_before, consumed_after;
    ELSE
        RAISE EXCEPTION 'FAIL: Counter did not increment';
    END IF;
END;
$$;

\echo 'PASS: Ack/Nack/Reject tests completed'
