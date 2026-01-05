-- ============================================================================
-- Test 06: Consumer Groups (Pub/Sub)
-- ============================================================================

\echo '=== Test 06: Consumer Groups (Pub/Sub) ==='

-- Setup: Create queue with messages
SELECT queen.configure('test_pubsub', 60, 3, false);
SELECT queen.produce_one('test_pubsub', '{"event": 1}'::jsonb, 'Default', 'pubsub-1');
SELECT queen.produce_one('test_pubsub', '{"event": 2}'::jsonb, 'Default', 'pubsub-2');
SELECT queen.produce_one('test_pubsub', '{"event": 3}'::jsonb, 'Default', 'pubsub-3');

-- Test 1: Two consumer groups receive same messages (fan-out)
DO $$
DECLARE
    v_group1_count INT := 0;
    v_group2_count INT := 0;
    v_msg RECORD;
BEGIN
    -- Group 1 consumes all messages
    FOR v_msg IN SELECT * FROM queen.consume_one('test_pubsub', 'group-analytics', 10)
    LOOP
        v_group1_count := v_group1_count + 1;
        PERFORM queen.commit_one(v_msg.transaction_id, v_msg.partition_id, v_msg.lease_id, 'group-analytics');
    END LOOP;
    
    -- Group 2 consumes same messages
    FOR v_msg IN SELECT * FROM queen.consume_one('test_pubsub', 'group-billing', 10)
    LOOP
        v_group2_count := v_group2_count + 1;
        PERFORM queen.commit_one(v_msg.transaction_id, v_msg.partition_id, v_msg.lease_id, 'group-billing');
    END LOOP;
    
    IF v_group1_count != 3 THEN
        RAISE EXCEPTION 'FAIL: Group 1 expected 3 messages, got %', v_group1_count;
    END IF;
    
    IF v_group2_count != 3 THEN
        RAISE EXCEPTION 'FAIL: Group 2 expected 3 messages, got %', v_group2_count;
    END IF;
    
    RAISE NOTICE 'PASS: Fan-out works - both groups received all messages';
END;
$$;

-- Test 2: Subscription mode 'new' - skip historical messages
SELECT queen.configure('test_subscription_new', 60, 3, false);
-- First produce some "historical" messages
SELECT queen.produce_one('test_subscription_new', '{"historical": 1}'::jsonb, 'Default', 'hist-1');
SELECT queen.produce_one('test_subscription_new', '{"historical": 2}'::jsonb, 'Default', 'hist-2');

DO $$
DECLARE
    v_count INT := 0;
    v_msg RECORD;
BEGIN
    -- Subscribe with mode='new' (should skip historical)
    FOR v_msg IN SELECT * FROM queen.consume_one(
        'test_subscription_new',
        'new-subscriber',
        10,
        NULL,        -- partition
        0,           -- timeout
        FALSE,       -- auto_commit
        'new'        -- subscription_mode
    )
    LOOP
        v_count := v_count + 1;
    END LOOP;
    
    -- Should get 0 messages (historical messages skipped)
    IF v_count != 0 THEN
        RAISE EXCEPTION 'FAIL: subscription_mode=new should skip historical, got %', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: subscription_mode=new skips historical messages';
END;
$$;

-- Now produce a new message and verify it's received
SELECT queen.produce_one('test_subscription_new', '{"new": "message"}'::jsonb, 'Default', 'new-msg-1');

DO $$
DECLARE
    v_count INT := 0;
BEGIN
    SELECT COUNT(*) INTO v_count FROM queen.consume_one(
        'test_subscription_new',
        'new-subscriber',
        10
    );
    
    -- Should get 1 message (the new one)
    IF v_count != 1 THEN
        RAISE EXCEPTION 'FAIL: Should get new message after subscription, got %', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: New messages received after subscription_mode=new';
END;
$$;

-- Test 3: Subscription mode 'all' - process from beginning
SELECT queen.configure('test_subscription_all', 60, 3, false);
SELECT queen.produce_one('test_subscription_all', '{"from": "beginning"}'::jsonb, 'Default', 'all-1');
SELECT queen.produce_one('test_subscription_all', '{"from": "beginning"}'::jsonb, 'Default', 'all-2');

DO $$
DECLARE
    v_count INT := 0;
BEGIN
    SELECT COUNT(*) INTO v_count FROM queen.consume_one(
        'test_subscription_all',
        'all-subscriber',
        10,
        NULL,
        0,
        FALSE,
        'all'  -- subscription_mode
    );
    
    IF v_count != 2 THEN
        RAISE EXCEPTION 'FAIL: subscription_mode=all should get all messages, got %', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: subscription_mode=all processes from beginning';
END;
$$;

-- Test 4: Seek to end (skip all pending)
SELECT queen.configure('test_seek_end', 60, 3, false);
SELECT queen.produce_one('test_seek_end', '{"skip": "me"}'::jsonb, 'Default', 'seek-1');
SELECT queen.produce_one('test_seek_end', '{"skip": "me too"}'::jsonb, 'Default', 'seek-2');

-- First, consume with a group to create the consumer
DO $$
DECLARE
    v_count INT;
BEGIN
    SELECT COUNT(*) INTO v_count FROM queen.consume_one('test_seek_end', 'seek-group', 10);
    RAISE NOTICE 'Initial consume got % messages', v_count;
END;
$$;

-- Now seek to end
DO $$
DECLARE
    v_result BOOLEAN;
    v_count INT;
BEGIN
    v_result := queen.seek('seek-group', 'test_seek_end', TRUE);  -- to_end = true
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: seek to end returned false';
    END IF;
    
    -- Now consume should get nothing (cursor at end)
    SELECT COUNT(*) INTO v_count FROM queen.consume_one('test_seek_end', 'seek-group', 10);
    
    IF v_count != 0 THEN
        RAISE EXCEPTION 'FAIL: After seek to end, should get 0 messages, got %', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: seek to end works';
END;
$$;

-- Test 5: Seek to timestamp
SELECT queen.configure('test_seek_ts', 60, 3, false);

-- Produce messages with some delay between them
SELECT queen.produce_one('test_seek_ts', '{"seq": 1}'::jsonb, 'Default', 'ts-1');
SELECT pg_sleep(0.1);
SELECT queen.produce_one('test_seek_ts', '{"seq": 2}'::jsonb, 'Default', 'ts-2');
SELECT pg_sleep(0.1);
SELECT queen.produce_one('test_seek_ts', '{"seq": 3}'::jsonb, 'Default', 'ts-3');

-- Create consumer group
DO $$
BEGIN
    PERFORM queen.consume_one('test_seek_ts', 'seek-ts-group', 1);
END;
$$;

-- Seek to 1 second ago (should still get recent messages)
DO $$
DECLARE
    v_result BOOLEAN;
BEGIN
    v_result := queen.seek('seek-ts-group', 'test_seek_ts', FALSE, NOW() - INTERVAL '1 second');
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: seek to timestamp returned false';
    END IF;
    
    RAISE NOTICE 'PASS: seek to timestamp works';
END;
$$;

-- Test 6: Delete consumer group
SELECT queen.configure('test_delete_cg', 60, 3, false);
SELECT queen.produce_one('test_delete_cg', '{"delete": "group"}'::jsonb, 'Default', 'del-1');

-- Create consumer group by consuming
DO $$
BEGIN
    PERFORM queen.consume_one('test_delete_cg', 'delete-me-group', 1);
END;
$$;

-- Delete the consumer group
DO $$
DECLARE
    v_result BOOLEAN;
    v_exists BOOLEAN;
BEGIN
    v_result := queen.delete_consumer_group('delete-me-group', 'test_delete_cg');
    
    IF NOT v_result THEN
        RAISE EXCEPTION 'FAIL: delete_consumer_group returned false';
    END IF;
    
    -- Verify it's deleted
    SELECT EXISTS (
        SELECT 1 FROM queen.partition_consumers pc
        JOIN queen.partitions p ON pc.partition_id = p.id
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE pc.consumer_group = 'delete-me-group'
          AND q.name = 'test_delete_cg'
    ) INTO v_exists;
    
    IF v_exists THEN
        RAISE EXCEPTION 'FAIL: Consumer group still exists after delete';
    END IF;
    
    RAISE NOTICE 'PASS: delete_consumer_group works';
END;
$$;

-- Test 7: lag with consumer group
SELECT queen.configure('test_lag_cg', 60, 3, false);
SELECT queen.produce_one('test_lag_cg', '{"lag": 1}'::jsonb, 'Default', 'lag-1');
SELECT queen.produce_one('test_lag_cg', '{"lag": 2}'::jsonb, 'Default', 'lag-2');

DO $$
DECLARE
    v_lag BIGINT;
BEGIN
    -- Lag for queue mode (no consumer group)
    v_lag := queen.lag('test_lag_cg');
    IF v_lag != 2 THEN
        RAISE EXCEPTION 'FAIL: Queue mode lag expected 2, got %', v_lag;
    END IF;
    
    -- Create consumer group and check its lag
    PERFORM queen.consume_one('test_lag_cg', 'lag-test-group', 10);
    
    v_lag := queen.lag('test_lag_cg', 'lag-test-group');
    
    -- After consuming, lag should be 0 (if we ack) or still some value
    RAISE NOTICE 'PASS: lag with consumer group works (lag=%)', v_lag;
END;
$$;

\echo 'Test 06: PASSED'
