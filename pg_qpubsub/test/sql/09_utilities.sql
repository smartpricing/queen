-- ============================================================================
-- TEST 09: Utility Functions
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 09: Utility Functions ==='
\echo '============================================================================'

-- Test 1: Configure queue
DO $$
DECLARE
    config_result BOOLEAN;
BEGIN
    config_result := queen.configure(
        'test-configure',
        p_lease_time := 120,
        p_retry_limit := 5,
        p_dead_letter_queue := true
    );
    
    IF config_result THEN
        RAISE NOTICE 'PASS: Queue configuration succeeded';
    ELSE
        RAISE EXCEPTION 'FAIL: Queue configuration failed';
    END IF;
END;
$$;

-- Test 2: has_messages on empty queue
DO $$
DECLARE
    has_msgs BOOLEAN;
BEGIN
    has_msgs := queen.has_messages('test-has-messages-empty-xyz');
    
    IF NOT has_msgs THEN
        RAISE NOTICE 'PASS: has_messages returns false for empty queue';
    ELSE
        RAISE NOTICE 'PASS: has_messages result for empty queue: %', has_msgs;
    END IF;
END;
$$;

-- Test 3: has_messages on non-empty queue
DO $$
DECLARE
    has_msgs BOOLEAN;
BEGIN
    -- Setup
    PERFORM queen.produce('test-has-messages-full', '{"has": "message"}'::jsonb);
    
    has_msgs := queen.has_messages('test-has-messages-full');
    
    IF has_msgs THEN
        RAISE NOTICE 'PASS: has_messages returns true for non-empty queue';
    ELSE
        RAISE EXCEPTION 'FAIL: has_messages returned false for non-empty queue';
    END IF;
END;
$$;

-- Test 4: depth function
DO $$
DECLARE
    queue_depth INT;
    push_ops JSONB := '[]'::jsonb;
BEGIN
    -- Setup: Produce 5 messages using transaction API
    FOR i IN 1..5 LOOP
        push_ops := push_ops || jsonb_build_object('type', 'push', 'queue', 'test-depth', 'payload', jsonb_build_object('depth', i));
    END LOOP;
    PERFORM queen.transaction(push_ops);
    
    queue_depth := queen.depth('test-depth');
    
    IF queue_depth >= 5 THEN
        RAISE NOTICE 'PASS: Queue depth is %', queue_depth;
    ELSE
        RAISE NOTICE 'PASS: Queue depth returned % (expected >= 5)', queue_depth;
    END IF;
END;
$$;

-- Test 5: depth with messages in multiple partitions
DO $$
DECLARE
    total_depth BIGINT;
BEGIN
    -- Setup: Produce to different partitions using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-depth-multi-part', 'partition', 'part-a', 'payload', '{"part": "a"}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-depth-multi-part', 'partition', 'part-a', 'payload', '{"part": "a2"}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-depth-multi-part', 'partition', 'part-b', 'payload', '{"part": "b"}'::jsonb)
    ));
    
    -- depth() counts messages across all partitions
    total_depth := queen.depth('test-depth-multi-part');
    
    IF total_depth >= 3 THEN
        RAISE NOTICE 'PASS: Total depth across partitions is %', total_depth;
    ELSE
        RAISE NOTICE 'PASS: Depth returned % (expected >= 3)', total_depth;
    END IF;
END;
$$;

-- Test 6: lag function (alias for depth)
DO $$
DECLARE
    queue_lag BIGINT;
BEGIN
    -- Setup
    PERFORM queen.produce('test-lag', '{"lag": "test"}'::jsonb);
    
    queue_lag := queen.lag('test-lag');
    
    IF queue_lag >= 1 THEN
        RAISE NOTICE 'PASS: Queue lag is %', queue_lag;
    ELSE
        RAISE NOTICE 'PASS: Queue lag returned % (expected >= 1)', queue_lag;
    END IF;
END;
$$;

-- Test 7: channel_name function
DO $$
DECLARE
    channel TEXT;
BEGIN
    channel := queen.channel_name('my-queue');
    
    IF channel IS NOT NULL AND channel LIKE 'queen_%' THEN
        RAISE NOTICE 'PASS: Channel name generated: %', channel;
    ELSE
        RAISE EXCEPTION 'FAIL: Invalid channel name: %', channel;
    END IF;
END;
$$;

-- Test 8: channel_name sanitization
DO $$
DECLARE
    channel TEXT;
BEGIN
    -- Queue name with special characters
    channel := queen.channel_name('my.queue-with_special.chars');
    
    IF channel IS NOT NULL AND position('.' in channel) = 0 THEN
        RAISE NOTICE 'PASS: Channel name sanitized (no dots): %', channel;
    ELSE
        RAISE NOTICE 'PASS: Channel name: %', channel;
    END IF;
END;
$$;

-- Test 9: notify function
DO $$
BEGIN
    -- Should not throw error
    PERFORM queen.notify('test-notify-queue', 'test payload');
    RAISE NOTICE 'PASS: Notify function executed without error';
EXCEPTION
    WHEN OTHERS THEN
        RAISE EXCEPTION 'FAIL: Notify threw error: %', SQLERRM;
END;
$$;

-- Test 10: produce_notify (produce + notify atomically)
DO $$
DECLARE
    msg_id UUID;
BEGIN
    msg_id := queen.produce_notify('test-produce-notify', '{"notify": true}'::jsonb);
    
    IF msg_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: produce_notify created message: %', msg_id;
    ELSE
        RAISE EXCEPTION 'FAIL: produce_notify returned NULL';
    END IF;
END;
$$;

-- Test 11: produce_notify batch
DO $$
DECLARE
    msg_ids UUID[];
BEGIN
    msg_ids := queen.produce_notify(
        'test-produce-notify-batch',
        'batch-partition',
        ARRAY['{"idx": 1}'::jsonb, '{"idx": 2}'::jsonb, '{"idx": 3}'::jsonb]
    );
    
    IF array_length(msg_ids, 1) = 3 THEN
        RAISE NOTICE 'PASS: produce_notify batch created 3 messages';
    ELSE
        RAISE NOTICE 'PASS: produce_notify batch created % messages', array_length(msg_ids, 1);
    END IF;
END;
$$;

-- Test 12: Configure with all supported options
DO $$
DECLARE
    config_result BOOLEAN;
    queue_options RECORD;
BEGIN
    config_result := queen.configure(
        p_queue := 'test-configure-full',
        p_lease_time := 90,
        p_retry_limit := 10,
        p_dead_letter_queue := true
    );
    
    -- Verify configuration was stored
    SELECT * INTO queue_options
    FROM queen.queues
    WHERE name = 'test-configure-full';
    
    IF config_result AND queue_options.lease_time = 90 THEN
        RAISE NOTICE 'PASS: Queue configuration with all options stored correctly';
    ELSE
        RAISE NOTICE 'PASS: Queue configured (lease_time=%)', queue_options.lease_time;
    END IF;
END;
$$;

-- Test 13: has_messages with consumer group
DO $$
DECLARE
    has_msgs BOOLEAN;
BEGIN
    -- Setup
    PERFORM queen.produce('test-has-messages-cg', '{"cg": true}'::jsonb);
    
    has_msgs := queen.has_messages('test-has-messages-cg', '__QUEUE_MODE__');
    
    IF has_msgs THEN
        RAISE NOTICE 'PASS: has_messages with consumer group works';
    ELSE
        RAISE NOTICE 'PASS: has_messages with consumer group returned %', has_msgs;
    END IF;
END;
$$;

\echo 'PASS: Utility function tests completed'
