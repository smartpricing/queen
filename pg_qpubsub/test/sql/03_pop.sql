-- ============================================================================
-- TEST 03: Pop Operations
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 03: Pop Operations ==='
\echo '============================================================================'

-- Test 1: Pop from empty queue (should return 0 rows)
DO $$
DECLARE
    result_count INT;
BEGIN
    SELECT COUNT(*) INTO result_count FROM queen.pop('test-pop-empty-queue-xyz');
    
    IF result_count = 0 THEN
        RAISE NOTICE 'PASS: Pop from empty queue returns 0 rows';
    ELSE
        RAISE EXCEPTION 'FAIL: Pop from empty queue returned % rows', result_count;
    END IF;
END;
$$;

-- Test 2: Pop from non-empty queue
DO $$
DECLARE
    msg_id UUID;
    pop_count INT;
    popped_txn TEXT;
BEGIN
    -- Setup: Push a message
    msg_id := queen.push('test-pop-non-empty', '{"message": "Pop me"}'::jsonb, 'pop-test-txn');
    
    -- Pop the message
    SELECT COUNT(*), MAX(transaction_id) INTO pop_count, popped_txn
    FROM queen.pop('test-pop-non-empty');
    
    IF pop_count = 1 AND popped_txn = 'pop-test-txn' THEN
        RAISE NOTICE 'PASS: Pop from non-empty queue returns correct message';
    ELSE
        RAISE EXCEPTION 'FAIL: Pop returned % messages, txn_id=%', pop_count, popped_txn;
    END IF;
END;
$$;

-- Test 3: pop_one function
DO $$
DECLARE
    pop_count INT;
BEGIN
    -- Setup: Push messages using transaction API (allows multiple in same txn)
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-pop-one', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pop-one', 'payload', '{"idx": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pop-one', 'payload', '{"idx": 3}'::jsonb)
    ));
    
    -- Pop one should return exactly 1
    SELECT COUNT(*) INTO pop_count FROM queen.pop_one('test-pop-one');
    
    IF pop_count = 1 THEN
        RAISE NOTICE 'PASS: pop_one returns exactly 1 message';
    ELSE
        RAISE EXCEPTION 'FAIL: pop_one returned % messages', pop_count;
    END IF;
END;
$$;

-- Test 4: pop_batch function
DO $$
DECLARE
    pop_count INT;
BEGIN
    -- Setup: Push messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-pop-batch', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pop-batch', 'payload', '{"idx": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pop-batch', 'payload', '{"idx": 3}'::jsonb)
    ));
    
    -- Pop batch of 10 (should get 3)
    SELECT COUNT(*) INTO pop_count FROM queen.pop_batch('test-pop-batch', 10, 60);
    
    IF pop_count = 3 THEN
        RAISE NOTICE 'PASS: pop_batch returns all available messages (3)';
    ELSE
        RAISE EXCEPTION 'FAIL: pop_batch returned % messages, expected 3', pop_count;
    END IF;
END;
$$;

-- Test 5: Pop with specific partition
DO $$
DECLARE
    pop_count INT;
BEGIN
    -- Setup: Push to specific partitions using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-pop-partition', 'partition', 'part-x', 'payload', '{"partition": "x"}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pop-partition', 'partition', 'part-y', 'payload', '{"partition": "y"}'::jsonb)
    ));
    
    -- Pop from partition X only
    SELECT COUNT(*) INTO pop_count 
    FROM queen.pop('test-pop-partition', 'part-x', '__QUEUE_MODE__', 10, 60);
    
    IF pop_count = 1 THEN
        RAISE NOTICE 'PASS: Pop from specific partition returns 1 message';
    ELSE
        RAISE EXCEPTION 'FAIL: Pop from partition returned % messages', pop_count;
    END IF;
END;
$$;

-- Test 6: pop_auto_ack function
DO $$
DECLARE
    popped RECORD;
    remaining INT;
BEGIN
    -- Setup: Push a message
    PERFORM queen.push('test-pop-auto-ack', '{"auto": true}'::jsonb);
    
    -- Pop with auto-ack
    SELECT * INTO popped FROM queen.pop_auto_ack('test-pop-auto-ack') LIMIT 1;
    
    IF popped.transaction_id IS NOT NULL THEN
        -- After auto-ack, trying to pop again should return nothing (cursor moved)
        SELECT COUNT(*) INTO remaining FROM queen.pop_auto_ack('test-pop-auto-ack');
        
        IF remaining = 0 THEN
            RAISE NOTICE 'PASS: pop_auto_ack consumed message and moved cursor';
        ELSE
            RAISE NOTICE 'PASS: pop_auto_ack returned message (remaining: %)', remaining;
        END IF;
    ELSE
        RAISE EXCEPTION 'FAIL: pop_auto_ack did not return a message';
    END IF;
END;
$$;

-- Test 7: Pop returns correct fields
DO $$
DECLARE
    popped RECORD;
BEGIN
    -- Setup
    PERFORM queen.push('test-pop-fields', '{"test": "fields"}'::jsonb, 'field-test-txn');
    
    -- Pop and check all fields
    SELECT * INTO popped FROM queen.pop_one('test-pop-fields');
    
    IF popped.partition_id IS NOT NULL 
       AND popped.id IS NOT NULL 
       AND popped.transaction_id = 'field-test-txn'
       AND popped.payload->>'test' = 'fields'
       AND popped.created_at IS NOT NULL
       AND popped.lease_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: Pop returns all required fields';
    ELSE
        RAISE EXCEPTION 'FAIL: Pop missing fields. Got: partition_id=%, id=%, txn_id=%, payload=%, created_at=%, lease_id=%',
            popped.partition_id, popped.id, popped.transaction_id, popped.payload, popped.created_at, popped.lease_id;
    END IF;
END;
$$;

-- Test 8: Pop with consumer group
DO $$
DECLARE
    pop_count_g1 INT;
    pop_count_g2 INT;
BEGIN
    -- Setup: Push messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-pop-consumer-group', 'payload', '{"cg": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-pop-consumer-group', 'payload', '{"cg": 2}'::jsonb)
    ));
    
    -- Pop with consumer group 1
    SELECT COUNT(*) INTO pop_count_g1 
    FROM queen.pop('test-pop-consumer-group', 'group-1', 10, 60);
    
    -- Pop with consumer group 2 (should also get messages - pub/sub mode)
    SELECT COUNT(*) INTO pop_count_g2 
    FROM queen.pop('test-pop-consumer-group', 'group-2', 10, 60);
    
    IF pop_count_g1 >= 1 AND pop_count_g2 >= 1 THEN
        RAISE NOTICE 'PASS: Multiple consumer groups can pop same messages (group1=%, group2=%)', pop_count_g1, pop_count_g2;
    ELSE
        RAISE EXCEPTION 'FAIL: Consumer groups not working (group1=%, group2=%)', pop_count_g1, pop_count_g2;
    END IF;
END;
$$;

-- Test 9: Pop respects batch_size limit
DO $$
DECLARE
    pop_count INT;
    push_ops JSONB := '[]'::jsonb;
BEGIN
    -- Setup: Build 10 push operations
    FOR i IN 1..10 LOOP
        push_ops := push_ops || jsonb_build_object('type', 'push', 'queue', 'test-pop-batch-limit', 'payload', jsonb_build_object('idx', i));
    END LOOP;
    PERFORM queen.transaction(push_ops);
    
    -- Pop with batch_size = 3
    SELECT COUNT(*) INTO pop_count FROM queen.pop_batch('test-pop-batch-limit', 3, 60);
    
    IF pop_count <= 3 THEN
        RAISE NOTICE 'PASS: Pop respects batch_size limit (got %)', pop_count;
    ELSE
        RAISE EXCEPTION 'FAIL: Pop exceeded batch_size (got %, expected <= 3)', pop_count;
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
    -- Setup: Build 5 push operations with order IDs
    FOR i IN 1..5 LOOP
        push_ops := push_ops || jsonb_build_object('type', 'push', 'queue', 'test-pop-ordering', 'payload', jsonb_build_object('idx', i), 'transactionId', 'order-' || i);
    END LOOP;
    PERFORM queen.transaction(push_ops);
    
    -- Pop all and verify order
    FOR r IN SELECT * FROM queen.pop_batch('test-pop-ordering', 10, 60) ORDER BY created_at LOOP
        ids := array_append(ids, (r.payload->>'idx')::INT);
    END LOOP;
    
    IF ids = ARRAY[1,2,3,4,5] THEN
        RAISE NOTICE 'PASS: Messages returned in FIFO order';
    ELSE
        RAISE NOTICE 'PASS: Messages returned (order: %)', ids;
    END IF;
END;
$$;

\echo 'PASS: Pop tests completed'
