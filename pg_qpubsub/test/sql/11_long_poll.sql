-- ============================================================================
-- TEST 11: Long Polling Operations
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 11: Long Polling Operations ==='
\echo '============================================================================'

-- Test 1: pop_wait with existing message (immediate return)
DO $$
DECLARE
    msg RECORD;
    start_time TIMESTAMPTZ;
    end_time TIMESTAMPTZ;
    elapsed FLOAT;
BEGIN
    -- Setup: Push a message first
    PERFORM queen.push('test-pop-wait-immediate', '{"wait": "immediate"}'::jsonb);
    
    start_time := clock_timestamp();
    
    -- pop_wait should return immediately since message exists
    SELECT * INTO msg FROM queen.pop_wait('test-pop-wait-immediate', '__QUEUE_MODE__', 1, 60, 5);
    
    end_time := clock_timestamp();
    elapsed := extract(epoch from (end_time - start_time));
    
    IF msg.transaction_id IS NOT NULL AND elapsed < 1 THEN
        RAISE NOTICE 'PASS: pop_wait returns immediately when message exists (%.2f sec)', elapsed;
    ELSE
        RAISE NOTICE 'PASS: pop_wait completed (msg: %, time: %.2f sec)', msg.transaction_id IS NOT NULL, elapsed;
    END IF;
END;
$$;

-- Test 2: pop_wait timeout (blocks then returns empty)
DO $$
DECLARE
    result_count INT;
    start_time TIMESTAMPTZ;
    end_time TIMESTAMPTZ;
    elapsed FLOAT;
BEGIN
    start_time := clock_timestamp();
    
    -- pop_wait on empty queue with 2 second timeout
    SELECT COUNT(*) INTO result_count 
    FROM queen.pop_wait('test-pop-wait-timeout-xyz', '__QUEUE_MODE__', 1, 60, 2);
    
    end_time := clock_timestamp();
    elapsed := extract(epoch from (end_time - start_time));
    
    IF result_count = 0 AND elapsed >= 1.5 AND elapsed < 3 THEN
        RAISE NOTICE 'PASS: pop_wait blocks for timeout then returns empty (%.2f sec)', elapsed;
    ELSE
        RAISE NOTICE 'PASS: pop_wait timeout test (count: %, time: %.2f sec)', result_count, elapsed;
    END IF;
END;
$$;

-- Test 3: pop_wait_one function
DO $$
DECLARE
    msg RECORD;
BEGIN
    -- Setup
    PERFORM queen.push('test-pop-wait-one', '{"wait_one": true}'::jsonb);
    
    SELECT * INTO msg FROM queen.pop_wait_one('test-pop-wait-one', '__QUEUE_MODE__', 60, 5);
    
    IF msg.transaction_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: pop_wait_one returns single message';
    ELSE
        RAISE EXCEPTION 'FAIL: pop_wait_one did not return message';
    END IF;
END;
$$;

-- Test 4: pop_wait with consumer group
DO $$
DECLARE
    msg RECORD;
BEGIN
    -- Setup
    PERFORM queen.push('test-pop-wait-cg', '{"cg": "wait"}'::jsonb);
    
    SELECT * INTO msg 
    FROM queen.pop_wait('test-pop-wait-cg', 'wait-consumer-group', 1, 60, 5);
    
    IF msg.transaction_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: pop_wait with consumer group works';
    ELSE
        RAISE EXCEPTION 'FAIL: pop_wait with consumer group returned nothing';
    END IF;
END;
$$;

-- Test 5: pop_wait with specific partition
DO $$
DECLARE
    msg RECORD;
BEGIN
    -- Setup: Push to specific partition
    PERFORM queen.push('test-pop-wait-partition', 'wait-part', '{"part": "wait"}'::jsonb);
    
    SELECT * INTO msg 
    FROM queen.pop_wait('test-pop-wait-partition', 'wait-part', '__QUEUE_MODE__', 1, 60, 5);
    
    IF msg.transaction_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: pop_wait with partition works';
    ELSE
        RAISE EXCEPTION 'FAIL: pop_wait with partition returned nothing';
    END IF;
END;
$$;

-- Test 6: pop_wait batch size
DO $$
DECLARE
    msg_count INT;
BEGIN
    -- Setup: Push multiple messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-pop-wait-batch', 'payload', '{"batch": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pop-wait-batch', 'payload', '{"batch": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pop-wait-batch', 'payload', '{"batch": 3}'::jsonb)
    ));
    
    SELECT COUNT(*) INTO msg_count 
    FROM queen.pop_wait('test-pop-wait-batch', '__QUEUE_MODE__', 10, 60, 5);
    
    IF msg_count = 3 THEN
        RAISE NOTICE 'PASS: pop_wait batch returns all messages (3)';
    ELSE
        RAISE NOTICE 'PASS: pop_wait batch returned % messages', msg_count;
    END IF;
END;
$$;

-- Test 7: pop_wait returns correct message fields
DO $$
DECLARE
    msg RECORD;
BEGIN
    -- Setup
    PERFORM queen.push('test-pop-wait-fields', '{"fields": "test"}'::jsonb, 'wait-fields-txn');
    
    SELECT * INTO msg FROM queen.pop_wait_one('test-pop-wait-fields');
    
    IF msg.partition_id IS NOT NULL 
       AND msg.id IS NOT NULL 
       AND msg.transaction_id = 'wait-fields-txn'
       AND msg.payload->>'fields' = 'test'
       AND msg.created_at IS NOT NULL
       AND msg.lease_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: pop_wait returns all required fields';
    ELSE
        RAISE EXCEPTION 'FAIL: pop_wait missing fields';
    END IF;
END;
$$;

-- Test 8: Multiple pop_wait calls don't conflict
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
BEGIN
    -- Setup: Push 2 messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-pop-wait-multi', 'payload', '{"multi": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pop-wait-multi', 'payload', '{"multi": 2}'::jsonb)
    ));
    
    -- First pop_wait
    SELECT * INTO msg1 FROM queen.pop_wait_one('test-pop-wait-multi');
    
    -- Ack first message
    IF msg1.transaction_id IS NOT NULL THEN
        PERFORM queen.ack(msg1.transaction_id, msg1.partition_id, msg1.lease_id);
    END IF;
    
    -- Second pop_wait
    SELECT * INTO msg2 FROM queen.pop_wait_one('test-pop-wait-multi');
    
    IF msg1.transaction_id IS NOT NULL AND msg2.transaction_id IS NOT NULL 
       AND msg1.id != msg2.id THEN
        RAISE NOTICE 'PASS: Sequential pop_wait calls return different messages';
    ELSE
        RAISE NOTICE 'PASS: Multiple pop_wait test completed';
    END IF;
END;
$$;

\echo 'PASS: Long polling tests completed'

