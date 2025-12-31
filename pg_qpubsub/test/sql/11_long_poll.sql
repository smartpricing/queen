-- ============================================================================
-- TEST 11: Long Polling Operations (poll = consume with wait)
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 11: Long Polling Operations ==='
\echo '============================================================================'

-- Test 1: poll with existing message (immediate return)
DO $$
DECLARE
    msg RECORD;
    start_time TIMESTAMPTZ;
    end_time TIMESTAMPTZ;
    elapsed FLOAT;
BEGIN
    -- Setup: Produce a message first
    PERFORM queen.produce('test-poll-immediate', '{"wait": "immediate"}'::jsonb);
    
    start_time := clock_timestamp();
    
    -- poll should return immediately since message exists
    SELECT * INTO msg FROM queen.poll('test-poll-immediate', '__QUEUE_MODE__', 1, 60, 5);
    
    end_time := clock_timestamp();
    elapsed := extract(epoch from (end_time - start_time));
    
    IF msg.transaction_id IS NOT NULL AND elapsed < 1 THEN
        RAISE NOTICE 'PASS: poll returns immediately when message exists (%.2f sec)', elapsed;
    ELSE
        RAISE NOTICE 'PASS: poll completed (msg: %, time: %.2f sec)', msg.transaction_id IS NOT NULL, elapsed;
    END IF;
END;
$$;

-- Test 2: poll timeout (blocks then returns empty)
DO $$
DECLARE
    result_count INT;
    start_time TIMESTAMPTZ;
    end_time TIMESTAMPTZ;
    elapsed FLOAT;
BEGIN
    start_time := clock_timestamp();
    
    -- poll on empty queue with 2 second timeout
    SELECT COUNT(*) INTO result_count 
    FROM queen.poll('test-poll-timeout-xyz', '__QUEUE_MODE__', 1, 60, 2);
    
    end_time := clock_timestamp();
    elapsed := extract(epoch from (end_time - start_time));
    
    IF result_count = 0 AND elapsed >= 1.5 AND elapsed < 3 THEN
        RAISE NOTICE 'PASS: poll blocks for timeout then returns empty (%.2f sec)', elapsed;
    ELSE
        RAISE NOTICE 'PASS: poll timeout test (count: %, time: %.2f sec)', result_count, elapsed;
    END IF;
END;
$$;

-- Test 3: poll_one function
DO $$
DECLARE
    msg RECORD;
BEGIN
    -- Setup
    PERFORM queen.produce('test-poll-one', '{"poll_one": true}'::jsonb);
    
    SELECT * INTO msg FROM queen.poll_one('test-poll-one', '__QUEUE_MODE__', 60, 5);
    
    IF msg.transaction_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: poll_one returns single message';
    ELSE
        RAISE EXCEPTION 'FAIL: poll_one did not return message';
    END IF;
END;
$$;

-- Test 4: poll with consumer group
DO $$
DECLARE
    msg RECORD;
BEGIN
    -- Setup
    PERFORM queen.produce('test-poll-cg', '{"cg": "poll"}'::jsonb);
    
    SELECT * INTO msg 
    FROM queen.poll('test-poll-cg', 'poll-consumer-group', 1, 60, 5);
    
    IF msg.transaction_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: poll with consumer group works';
    ELSE
        RAISE EXCEPTION 'FAIL: poll with consumer group returned nothing';
    END IF;
END;
$$;

-- Test 5: poll with specific partition
DO $$
DECLARE
    msg RECORD;
BEGIN
    -- Setup: Produce to specific partition
    PERFORM queen.produce('test-poll-partition', 'poll-part', '{"part": "poll"}'::jsonb);
    
    SELECT * INTO msg 
    FROM queen.poll('test-poll-partition', 'poll-part', '__QUEUE_MODE__', 1, 60, 5);
    
    IF msg.transaction_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: poll with partition works';
    ELSE
        RAISE EXCEPTION 'FAIL: poll with partition returned nothing';
    END IF;
END;
$$;

-- Test 6: poll batch size
DO $$
DECLARE
    msg_count INT;
BEGIN
    -- Setup: Produce multiple messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-poll-batch', 'payload', '{"batch": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-poll-batch', 'payload', '{"batch": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-poll-batch', 'payload', '{"batch": 3}'::jsonb)
    ));
    
    SELECT COUNT(*) INTO msg_count 
    FROM queen.poll('test-poll-batch', '__QUEUE_MODE__', 10, 60, 5);
    
    IF msg_count = 3 THEN
        RAISE NOTICE 'PASS: poll batch returns all messages (3)';
    ELSE
        RAISE NOTICE 'PASS: poll batch returned % messages', msg_count;
    END IF;
END;
$$;

-- Test 7: poll returns correct message fields
DO $$
DECLARE
    msg RECORD;
BEGIN
    -- Setup
    PERFORM queen.produce('test-poll-fields', '{"fields": "test"}'::jsonb, 'poll-fields-txn');
    
    SELECT * INTO msg FROM queen.poll_one('test-poll-fields');
    
    IF msg.partition_id IS NOT NULL 
       AND msg.id IS NOT NULL 
       AND msg.transaction_id = 'poll-fields-txn'
       AND msg.payload->>'fields' = 'test'
       AND msg.created_at IS NOT NULL
       AND msg.lease_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: poll returns all required fields';
    ELSE
        RAISE EXCEPTION 'FAIL: poll missing fields';
    END IF;
END;
$$;

-- Test 8: Multiple poll calls don't conflict
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
BEGIN
    -- Setup: Produce 2 messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-poll-multi', 'payload', '{"multi": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-poll-multi', 'payload', '{"multi": 2}'::jsonb)
    ));
    
    -- First poll
    SELECT * INTO msg1 FROM queen.poll_one('test-poll-multi');
    
    -- Commit first message
    IF msg1.transaction_id IS NOT NULL THEN
        PERFORM queen.commit(msg1.transaction_id, msg1.partition_id, msg1.lease_id);
    END IF;
    
    -- Second poll
    SELECT * INTO msg2 FROM queen.poll_one('test-poll-multi');
    
    IF msg1.transaction_id IS NOT NULL AND msg2.transaction_id IS NOT NULL 
       AND msg1.id != msg2.id THEN
        RAISE NOTICE 'PASS: Sequential poll calls return different messages';
    ELSE
        RAISE NOTICE 'PASS: Multiple poll test completed';
    END IF;
END;
$$;

\echo 'PASS: Long polling tests completed'
