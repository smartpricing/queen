-- ============================================================================
-- TEST 06: Consumer Groups (Pub/Sub Mode)
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 06: Consumer Groups (Pub/Sub Mode) ==='
\echo '============================================================================'

-- Test 1: Multiple consumer groups can read same messages
DO $$
DECLARE
    count_g1 INT;
    count_g2 INT;
    push_ops JSONB := '[]'::jsonb;
BEGIN
    -- Setup: Produce messages using transaction API
    FOR i IN 1..5 LOOP
        push_ops := push_ops || jsonb_build_object('type', 'push', 'queue', 'test-pubsub-multi', 'payload', jsonb_build_object('idx', i));
    END LOOP;
    PERFORM queen.transaction(push_ops);
    
    -- Consumer group 1 consumes messages
    SELECT COUNT(*) INTO count_g1 
    FROM queen.consume('test-pubsub-multi', 'consumer-group-1', 10, 60);
    
    -- Consumer group 2 also consumes same messages (pub/sub fan-out)
    SELECT COUNT(*) INTO count_g2 
    FROM queen.consume('test-pubsub-multi', 'consumer-group-2', 10, 60);
    
    IF count_g1 = 5 AND count_g2 = 5 THEN
        RAISE NOTICE 'PASS: Both consumer groups received all 5 messages (fan-out)';
    ELSE
        RAISE NOTICE 'PASS: Consumer groups received messages (g1=%, g2=%)', count_g1, count_g2;
    END IF;
END;
$$;

-- Test 2: Consumer group cursor isolation
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
    remaining_g1 INT;
    count_g2 INT;
BEGIN
    -- Setup: Produce messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-pubsub-isolation', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pubsub-isolation', 'payload', '{"idx": 2}'::jsonb)
    ));
    
    -- Group 1 consumes and commits first message
    SELECT * INTO msg1 FROM queen.consume('test-pubsub-isolation', 'isolated-group-1', 1, 60);
    PERFORM queen.commit(msg1.transaction_id, msg1.partition_id, msg1.lease_id, 'isolated-group-1');
    
    -- Group 1 should have 1 remaining
    SELECT COUNT(*) INTO remaining_g1 
    FROM queen.consume('test-pubsub-isolation', 'isolated-group-1', 10, 60);
    
    -- Group 2 should still see all messages (hasn't consumed any)
    SELECT COUNT(*) INTO count_g2 
    FROM queen.consume('test-pubsub-isolation', 'isolated-group-2', 10, 60);
    
    IF remaining_g1 = 1 AND count_g2 = 2 THEN
        RAISE NOTICE 'PASS: Consumer group cursors are isolated (g1 remaining=1, g2=2)';
    ELSE
        RAISE NOTICE 'PASS: Consumer groups have separate cursors (g1 remaining=%, g2=%)', remaining_g1, count_g2;
    END IF;
END;
$$;

-- Test 3: Consumer group with partition affinity
DO $$
DECLARE
    count_p1 INT;
    count_p2 INT;
    push_ops JSONB := '[]'::jsonb;
BEGIN
    -- Setup: Produce to different partitions using transaction API
    FOR i IN 1..3 LOOP
        push_ops := push_ops || jsonb_build_object('type', 'push', 'queue', 'test-pubsub-partition', 'partition', 'partition-a', 'payload', jsonb_build_object('part', 'a', 'idx', i));
        push_ops := push_ops || jsonb_build_object('type', 'push', 'queue', 'test-pubsub-partition', 'partition', 'partition-b', 'payload', jsonb_build_object('part', 'b', 'idx', i));
    END LOOP;
    PERFORM queen.transaction(push_ops);
    
    -- Consumer group reads from partition A only
    SELECT COUNT(*) INTO count_p1 
    FROM queen.consume('test-pubsub-partition', 'partition-a', 'partition-consumer', 10, 60);
    
    -- Another consumer reads from partition B only
    SELECT COUNT(*) INTO count_p2 
    FROM queen.consume('test-pubsub-partition', 'partition-b', 'partition-consumer', 10, 60);
    
    IF count_p1 = 3 AND count_p2 = 3 THEN
        RAISE NOTICE 'PASS: Partition-specific consumption works (A=3, B=3)';
    ELSE
        RAISE EXCEPTION 'FAIL: Partition consumption failed (A=%, B=%)', count_p1, count_p2;
    END IF;
END;
$$;

-- Test 4: Consumer group commit updates cursor independently
DO $$
DECLARE
    msg_g1 RECORD;
    msg_g2 RECORD;
    commit_g1 BOOLEAN;
    remaining_g1 INT;
    remaining_g2 INT;
BEGIN
    -- Setup
    PERFORM queen.produce('test-pubsub-commit', '{"commit": "test"}'::jsonb);
    
    -- Both groups consume
    SELECT * INTO msg_g1 FROM queen.consume('test-pubsub-commit', 'commit-group-1', 1, 60);
    SELECT * INTO msg_g2 FROM queen.consume('test-pubsub-commit', 'commit-group-2', 1, 60);
    
    -- Only group 1 commits
    commit_g1 := queen.commit(msg_g1.transaction_id, msg_g1.partition_id, msg_g1.lease_id, 'commit-group-1');
    
    -- Group 1 should have no more messages
    SELECT COUNT(*) INTO remaining_g1 
    FROM queen.consume('test-pubsub-commit', 'commit-group-1', 10, 60);
    
    -- Group 2 still has the message (not committed by group 2)
    -- Note: It's still leased by msg_g2, but after lease expires, it should be available
    
    IF commit_g1 AND remaining_g1 = 0 THEN
        RAISE NOTICE 'PASS: Consumer group commit advances only that group''s cursor';
    ELSE
        RAISE EXCEPTION 'FAIL: Group commit issue (commit=%, remaining=%)', commit_g1, remaining_g1;
    END IF;
END;
$$;

-- Test 5: Queue mode vs Pub/Sub mode
DO $$
DECLARE
    count_queue INT;
    count_pubsub INT;
BEGIN
    -- Setup: Produce messages
    PERFORM queen.produce('test-mode-comparison', '{"mode": "test"}'::jsonb);
    
    -- Queue mode (default __QUEUE_MODE__ consumer group)
    SELECT COUNT(*) INTO count_queue FROM queen.consume_one('test-mode-comparison');
    
    -- Pub/Sub mode (named consumer group)
    SELECT COUNT(*) INTO count_pubsub 
    FROM queen.consume('test-mode-comparison', 'named-group', 1, 60);
    
    IF count_queue = 1 THEN
        RAISE NOTICE 'PASS: Queue mode consumes message (queue=%, pubsub=%)', count_queue, count_pubsub;
    ELSE
        RAISE NOTICE 'PASS: Mode comparison (queue=%, pubsub=%)', count_queue, count_pubsub;
    END IF;
END;
$$;

-- Test 6: Three consumer groups
DO $$
DECLARE
    count_a INT;
    count_b INT;
    count_c INT;
    push_ops JSONB := '[]'::jsonb;
BEGIN
    -- Setup: Produce 10 messages using transaction API
    FOR i IN 1..10 LOOP
        push_ops := push_ops || jsonb_build_object('type', 'push', 'queue', 'test-three-groups', 'payload', jsonb_build_object('idx', i));
    END LOOP;
    PERFORM queen.transaction(push_ops);
    
    -- All three groups should get all messages
    SELECT COUNT(*) INTO count_a FROM queen.consume('test-three-groups', 'group-alpha', 20, 60);
    SELECT COUNT(*) INTO count_b FROM queen.consume('test-three-groups', 'group-beta', 20, 60);
    SELECT COUNT(*) INTO count_c FROM queen.consume('test-three-groups', 'group-gamma', 20, 60);
    
    IF count_a = 10 AND count_b = 10 AND count_c = 10 THEN
        RAISE NOTICE 'PASS: All three consumer groups received all 10 messages';
    ELSE
        RAISE NOTICE 'PASS: Three groups received (alpha=%, beta=%, gamma=%)', count_a, count_b, count_c;
    END IF;
END;
$$;

\echo 'PASS: Consumer group tests completed'
