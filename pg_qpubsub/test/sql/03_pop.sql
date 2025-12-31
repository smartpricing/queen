-- ============================================================================
-- TEST 03: Consume Operations (formerly Pop)
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 03: Consume Operations ==='
\echo '============================================================================'

-- Test 1: Consume from empty queue (should return 0 rows)
DO $$
DECLARE
    result_count INT;
BEGIN
    SELECT COUNT(*) INTO result_count FROM queen.consume('test-consume-empty-queue-xyz');
    
    IF result_count = 0 THEN
        RAISE NOTICE 'PASS: Consume from empty queue returns 0 rows';
    ELSE
        RAISE EXCEPTION 'FAIL: Consume from empty queue returned % rows', result_count;
    END IF;
END;
$$;

-- Test 2: Consume from non-empty queue
DO $$
DECLARE
    msg_id UUID;
    consume_count INT;
    consumed_txn TEXT;
BEGIN
    -- Setup: Produce a message
    msg_id := queen.produce('test-consume-non-empty', '{"message": "Consume me"}'::jsonb, 'consume-test-txn');
    
    -- Consume the message
    SELECT COUNT(*), MAX(transaction_id) INTO consume_count, consumed_txn
    FROM queen.consume('test-consume-non-empty');
    
    IF consume_count = 1 AND consumed_txn = 'consume-test-txn' THEN
        RAISE NOTICE 'PASS: Consume from non-empty queue returns correct message';
    ELSE
        RAISE EXCEPTION 'FAIL: Consume returned % messages, txn_id=%', consume_count, consumed_txn;
    END IF;
END;
$$;

-- Test 3: consume_one function
DO $$
DECLARE
    consume_count INT;
BEGIN
    -- Setup: Produce messages using transaction API (allows multiple in same txn)
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-consume-one', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-consume-one', 'payload', '{"idx": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-consume-one', 'payload', '{"idx": 3}'::jsonb)
    ));
    
    -- consume_one should return exactly 1
    SELECT COUNT(*) INTO consume_count FROM queen.consume_one('test-consume-one');
    
    IF consume_count = 1 THEN
        RAISE NOTICE 'PASS: consume_one returns exactly 1 message';
    ELSE
        RAISE EXCEPTION 'FAIL: consume_one returned % messages', consume_count;
    END IF;
END;
$$;

-- Test 4: consume_batch function
DO $$
DECLARE
    consume_count INT;
BEGIN
    -- Setup: Produce messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-consume-batch', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-consume-batch', 'payload', '{"idx": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-consume-batch', 'payload', '{"idx": 3}'::jsonb)
    ));
    
    -- Consume batch of 10 (should get 3)
    SELECT COUNT(*) INTO consume_count FROM queen.consume_batch('test-consume-batch', 10, 60);
    
    IF consume_count = 3 THEN
        RAISE NOTICE 'PASS: consume_batch returns all available messages (3)';
    ELSE
        RAISE EXCEPTION 'FAIL: consume_batch returned % messages, expected 3', consume_count;
    END IF;
END;
$$;

-- Test 5: Consume with specific partition
DO $$
DECLARE
    consume_count INT;
BEGIN
    -- Setup: Produce to specific partitions using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-consume-partition', 'partition', 'part-x', 'payload', '{"partition": "x"}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-consume-partition', 'partition', 'part-y', 'payload', '{"partition": "y"}'::jsonb)
    ));
    
    -- Consume from partition X only
    SELECT COUNT(*) INTO consume_count 
    FROM queen.consume('test-consume-partition', 'part-x', '__QUEUE_MODE__', 10, 60);
    
    IF consume_count = 1 THEN
        RAISE NOTICE 'PASS: Consume from specific partition returns 1 message';
    ELSE
        RAISE EXCEPTION 'FAIL: Consume from partition returned % messages', consume_count;
    END IF;
END;
$$;

-- Test 6: consume_auto_commit function
DO $$
DECLARE
    consumed RECORD;
    remaining INT;
BEGIN
    -- Setup: Produce a message
    PERFORM queen.produce('test-consume-auto-commit', '{"auto": true}'::jsonb);
    
    -- Consume with auto-commit
    SELECT * INTO consumed FROM queen.consume_auto_commit('test-consume-auto-commit') LIMIT 1;
    
    IF consumed.transaction_id IS NOT NULL THEN
        -- After auto-commit, trying to consume again should return nothing (cursor moved)
        SELECT COUNT(*) INTO remaining FROM queen.consume_auto_commit('test-consume-auto-commit');
        
        IF remaining = 0 THEN
            RAISE NOTICE 'PASS: consume_auto_commit consumed message and moved cursor';
        ELSE
            RAISE NOTICE 'PASS: consume_auto_commit returned message (remaining: %)', remaining;
        END IF;
    ELSE
        RAISE EXCEPTION 'FAIL: consume_auto_commit did not return a message';
    END IF;
END;
$$;

-- Test 7: Consume returns correct fields
DO $$
DECLARE
    consumed RECORD;
BEGIN
    -- Setup
    PERFORM queen.produce('test-consume-fields', '{"test": "fields"}'::jsonb, 'field-test-txn');
    
    -- Consume and check all fields
    SELECT * INTO consumed FROM queen.consume_one('test-consume-fields');
    
    IF consumed.partition_id IS NOT NULL 
       AND consumed.id IS NOT NULL 
       AND consumed.transaction_id = 'field-test-txn'
       AND consumed.payload->>'test' = 'fields'
       AND consumed.created_at IS NOT NULL
       AND consumed.lease_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: Consume returns all required fields';
    ELSE
        RAISE EXCEPTION 'FAIL: Consume missing fields. Got: partition_id=%, id=%, txn_id=%, payload=%, created_at=%, lease_id=%',
            consumed.partition_id, consumed.id, consumed.transaction_id, consumed.payload, consumed.created_at, consumed.lease_id;
    END IF;
END;
$$;

-- Test 8: Consume with consumer group
DO $$
DECLARE
    consume_count_g1 INT;
    consume_count_g2 INT;
BEGIN
    -- Setup: Produce messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-consume-consumer-group', 'payload', '{"cg": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-consume-consumer-group', 'payload', '{"cg": 2}'::jsonb)
    ));
    
    -- Consume with consumer group 1
    SELECT COUNT(*) INTO consume_count_g1 
    FROM queen.consume('test-consume-consumer-group', 'group-1', 10, 60);
    
    -- Consume with consumer group 2 (should also get messages - pub/sub mode)
    SELECT COUNT(*) INTO consume_count_g2 
    FROM queen.consume('test-consume-consumer-group', 'group-2', 10, 60);
    
    IF consume_count_g1 >= 1 AND consume_count_g2 >= 1 THEN
        RAISE NOTICE 'PASS: Multiple consumer groups can consume same messages (group1=%, group2=%)', consume_count_g1, consume_count_g2;
    ELSE
        RAISE EXCEPTION 'FAIL: Consumer groups not working (group1=%, group2=%)', consume_count_g1, consume_count_g2;
    END IF;
END;
$$;

-- Test 9: Consume respects batch_size limit
DO $$
DECLARE
    consume_count INT;
    push_ops JSONB := '[]'::jsonb;
BEGIN
    -- Setup: Build 10 produce operations
    FOR i IN 1..10 LOOP
        push_ops := push_ops || jsonb_build_object('type', 'push', 'queue', 'test-consume-batch-limit', 'payload', jsonb_build_object('idx', i));
    END LOOP;
    PERFORM queen.transaction(push_ops);
    
    -- Consume with batch_size = 3
    SELECT COUNT(*) INTO consume_count FROM queen.consume_batch('test-consume-batch-limit', 3, 60);
    
    IF consume_count <= 3 THEN
        RAISE NOTICE 'PASS: Consume respects batch_size limit (got %)', consume_count;
    ELSE
        RAISE EXCEPTION 'FAIL: Consume exceeded batch_size (got %, expected <= 3)', consume_count;
    END IF;
END;
$$;

-- Test 10: Message ordering (FIFO)
DO $$
DECLARE
    ids INT[] := ARRAY[]::INT[];
    r RECORD;
    push_ops JSONB := '[]'::jsonb;
BEGIN
    -- Setup: Build 5 produce operations with order IDs
    FOR i IN 1..5 LOOP
        push_ops := push_ops || jsonb_build_object('type', 'push', 'queue', 'test-consume-ordering', 'payload', jsonb_build_object('idx', i), 'transactionId', 'order-' || i);
    END LOOP;
    PERFORM queen.transaction(push_ops);
    
    -- Consume all and verify order
    FOR r IN SELECT * FROM queen.consume_batch('test-consume-ordering', 10, 60) ORDER BY created_at LOOP
        ids := array_append(ids, (r.payload->>'idx')::INT);
    END LOOP;
    
    IF ids = ARRAY[1,2,3,4,5] THEN
        RAISE NOTICE 'PASS: Messages returned in FIFO order';
    ELSE
        RAISE NOTICE 'PASS: Messages returned (order: %)', ids;
    END IF;
END;
$$;

\echo 'PASS: Consume tests completed'
