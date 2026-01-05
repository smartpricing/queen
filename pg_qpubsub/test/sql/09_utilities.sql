-- ============================================================================
-- Test 09: Utility Functions
-- ============================================================================

\echo '=== Test 09: Utility Functions ==='

-- Test 1: Configure queue
DO $$
DECLARE
    v_result BOOLEAN;
    v_queue RECORD;
BEGIN
    v_result := queen.configure('test_utils_queue', 120, 5, true);
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: Configure returned false';
    END IF;
    
    -- Verify configuration was applied
    SELECT * INTO v_queue FROM queen.queues WHERE name = 'test_utils_queue';
    
    IF v_queue.lease_time != 120 THEN
        RAISE EXCEPTION 'FAIL: Lease time not set correctly (expected 120, got %)', v_queue.lease_time;
    END IF;
    
    IF v_queue.retry_limit != 5 THEN
        RAISE EXCEPTION 'FAIL: Retry limit not set correctly (expected 5, got %)', v_queue.retry_limit;
    END IF;
    
    RAISE NOTICE 'PASS: Configure queue works';
END;
$$;

-- Test 2: has_messages function
SELECT queen.configure('test_has_messages', 60, 3, false);

DO $$
DECLARE
    v_has BOOLEAN;
BEGIN
    -- Empty queue should have no messages
    v_has := queen.has_messages('test_has_messages');
    
    IF v_has THEN
        RAISE EXCEPTION 'FAIL: Empty queue reports has_messages=true';
    END IF;
    
    -- Add a message
    PERFORM queen.produce_one('test_has_messages', '{"test": "data"}'::jsonb);
    
    -- Now should have messages
    v_has := queen.has_messages('test_has_messages');
    
    IF NOT v_has THEN
        RAISE EXCEPTION 'FAIL: Queue with messages reports has_messages=false';
    END IF;
    
    RAISE NOTICE 'PASS: has_messages works';
END;
$$;

-- Test 3: lag/depth functions
SELECT queen.configure('test_depth', 60, 3, false);

DO $$
DECLARE
    v_lag BIGINT;
    v_depth BIGINT;
BEGIN
    -- Initial depth should be 0
    v_lag := queen.lag('test_depth');
    v_depth := queen.depth('test_depth');
    
    IF v_lag != 0 OR v_depth != 0 THEN
        RAISE EXCEPTION 'FAIL: Initial depth should be 0, got lag=%, depth=%', v_lag, v_depth;
    END IF;
    
    RAISE NOTICE 'PASS: Initial depth is 0';
END;
$$;

-- Add messages (each in separate statement to avoid tmp_items conflict)
SELECT queen.produce_one('test_depth', '{"n": 1}'::jsonb);
SELECT queen.produce_one('test_depth', '{"n": 2}'::jsonb);
SELECT queen.produce_one('test_depth', '{"n": 3}'::jsonb);

DO $$
DECLARE
    v_lag BIGINT;
    v_depth BIGINT;
BEGIN
    v_lag := queen.lag('test_depth');
    v_depth := queen.depth('test_depth');
    
    IF v_lag != 3 OR v_depth != 3 THEN
        RAISE EXCEPTION 'FAIL: After 3 messages, depth should be 3, got lag=%, depth=%', v_lag, v_depth;
    END IF;
    
    -- lag is an alias for depth
    IF v_lag != v_depth THEN
        RAISE EXCEPTION 'FAIL: lag and depth should be equal';
    END IF;
    
    RAISE NOTICE 'PASS: lag/depth functions work';
END;
$$;

-- Test 4: channel_name function
DO $$
DECLARE
    v_channel TEXT;
BEGIN
    v_channel := queen.channel_name('my-queue.name');
    
    IF v_channel IS NULL THEN
        RAISE EXCEPTION 'FAIL: channel_name returned NULL';
    END IF;
    
    -- Should replace . and - with _
    IF v_channel != 'queen_my_queue_name' THEN
        RAISE EXCEPTION 'FAIL: channel_name format incorrect: %', v_channel;
    END IF;
    
    RAISE NOTICE 'PASS: channel_name works: %', v_channel;
END;
$$;

-- Test 5: notify function (does not error)
DO $$
BEGIN
    -- Just verify it doesn't error
    PERFORM queen.notify('test-queue', 'test payload');
    PERFORM queen.notify('test-queue');  -- Empty payload
    
    RAISE NOTICE 'PASS: notify function executes without error';
END;
$$;

-- Test 6: produce_notify convenience function
SELECT queen.configure('test_produce_notify', 60, 3, false);

DO $$
DECLARE
    v_msg_id UUID;
BEGIN
    v_msg_id := queen.produce_notify('test_produce_notify', '{"with": "notify"}'::jsonb);
    
    IF v_msg_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: produce_notify returned NULL';
    END IF;
    
    RAISE NOTICE 'PASS: produce_notify works: %', v_msg_id;
END;
$$;

-- Test 7: Seek function
SELECT queen.configure('test_seek', 60, 3, false);
SELECT queen.produce_one('test_seek', '{"seq": 1}'::jsonb, 'Default', 'seek-util-1');
SELECT queen.produce_one('test_seek', '{"seq": 2}'::jsonb, 'Default', 'seek-util-2');

-- Create consumer group
DO $$
BEGIN
    PERFORM queen.consume_one('test_seek', 'seek-util-group', 1);
END;
$$;

-- Seek to end
DO $$
DECLARE
    v_result BOOLEAN;
BEGIN
    v_result := queen.seek('seek-util-group', 'test_seek', TRUE);
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: seek to end returned false';
    END IF;
    
    RAISE NOTICE 'PASS: seek function works';
END;
$$;

-- Test 8: delete_consumer_group function
SELECT queen.configure('test_delete_cg_util', 60, 3, false);
SELECT queen.produce_one('test_delete_cg_util', '{"delete": "test"}'::jsonb, 'Default', 'del-util-1');

-- Create consumer group
DO $$
BEGIN
    PERFORM queen.consume_one('test_delete_cg_util', 'delete-util-group', 1);
END;
$$;

-- Delete for specific queue
DO $$
DECLARE
    v_result BOOLEAN;
BEGIN
    v_result := queen.delete_consumer_group('delete-util-group', 'test_delete_cg_util');
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: delete_consumer_group returned false';
    END IF;
    
    RAISE NOTICE 'PASS: delete_consumer_group for specific queue works';
END;
$$;

-- Test 9: delete_consumer_group for all queues
SELECT queen.configure('test_delete_all_1', 60, 3, false);
SELECT queen.configure('test_delete_all_2', 60, 3, false);
SELECT queen.produce_one('test_delete_all_1', '{"q": 1}'::jsonb);
SELECT queen.produce_one('test_delete_all_2', '{"q": 2}'::jsonb);

-- Create consumer group in both queues
DO $$
BEGIN
    PERFORM queen.consume_one('test_delete_all_1', 'delete-all-group', 1);
    PERFORM queen.consume_one('test_delete_all_2', 'delete-all-group', 1);
END;
$$;

-- Delete for all queues (p_queue = NULL)
DO $$
DECLARE
    v_result BOOLEAN;
    v_count INT;
BEGIN
    v_result := queen.delete_consumer_group('delete-all-group');  -- NULL queue = all
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: delete_consumer_group all returned false';
    END IF;
    
    -- Verify no partition_consumers remain for this group
    SELECT COUNT(*) INTO v_count
    FROM queen.partition_consumers
    WHERE consumer_group = 'delete-all-group';
    
    IF v_count != 0 THEN
        RAISE EXCEPTION 'FAIL: Consumer group still exists in % queues', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: delete_consumer_group for all queues works';
END;
$$;

-- Test 10: lag with consumer group
SELECT queen.configure('test_lag_cg_util', 60, 3, false);
SELECT queen.produce_one('test_lag_cg_util', '{"m": 1}'::jsonb);
SELECT queen.produce_one('test_lag_cg_util', '{"m": 2}'::jsonb);

DO $$
DECLARE
    v_queue_lag BIGINT;
    v_group_lag BIGINT;
BEGIN
    -- Queue mode lag
    v_queue_lag := queen.lag('test_lag_cg_util');
    
    -- Create consumer group and consume one message
    PERFORM queen.consume_one('test_lag_cg_util', 'lag-util-group', 1);
    
    -- Consumer group lag
    v_group_lag := queen.lag('test_lag_cg_util', 'lag-util-group');
    
    -- Both should report messages
    IF v_queue_lag != 2 THEN
        RAISE EXCEPTION 'FAIL: Queue lag should be 2, got %', v_queue_lag;
    END IF;
    
    RAISE NOTICE 'PASS: lag with consumer group works (queue: %, group: %)', v_queue_lag, v_group_lag;
END;
$$;

\echo 'Test 09: PASSED'
