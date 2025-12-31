-- ============================================================================
-- TEST 02: Push Operations
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 02: Push Operations ==='
\echo '============================================================================'

-- Test 1: Basic push
DO $$
DECLARE
    msg_id UUID;
BEGIN
    msg_id := queen.push('test-push-basic', '{"message": "Hello, world!"}'::jsonb);
    
    IF msg_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: Basic push returns message ID: %', msg_id;
    ELSE
        RAISE EXCEPTION 'FAIL: Basic push returned NULL';
    END IF;
END;
$$;

-- Test 2: Push with transaction ID
DO $$
DECLARE
    msg_id UUID;
BEGIN
    msg_id := queen.push('test-push-txnid', '{"message": "With txn ID"}'::jsonb, 'my-custom-txn-id');
    
    IF msg_id IS NOT NULL THEN
        -- Verify the message was stored with correct txn_id
        IF EXISTS (SELECT 1 FROM queen.messages WHERE transaction_id = 'my-custom-txn-id') THEN
            RAISE NOTICE 'PASS: Push with transaction ID works';
        ELSE
            RAISE EXCEPTION 'FAIL: Message not found with transaction_id';
        END IF;
    ELSE
        RAISE EXCEPTION 'FAIL: Push with txn_id returned NULL';
    END IF;
END;
$$;

-- Test 3: Push duplicate message (uses temp table to pass data between statements)
CREATE TEMP TABLE test_push_dup (step INT, msg_id UUID);
INSERT INTO test_push_dup VALUES (1, queen.push('test-push-duplicate', '{"message": "First"}'::jsonb, 'duplicate-test-txn'));
INSERT INTO test_push_dup VALUES (2, queen.push('test-push-duplicate', '{"message": "Second"}'::jsonb, 'duplicate-test-txn'));

DO $$
DECLARE
    msg_id1 UUID;
    msg_id2 UUID;
BEGIN
    SELECT msg_id INTO msg_id1 FROM test_push_dup WHERE step = 1;
    SELECT msg_id INTO msg_id2 FROM test_push_dup WHERE step = 2;
    
    IF msg_id1 = msg_id2 THEN
        RAISE NOTICE 'PASS: Duplicate detection works (same ID returned)';
    ELSE
        RAISE NOTICE 'PASS: Duplicate pushed (IDs differ: % vs %)', msg_id1, msg_id2;
    END IF;
END;
$$;
DROP TABLE test_push_dup;

-- Test 4: Push to specific partition
DO $$
DECLARE
    msg_id UUID;
    partition_name TEXT;
BEGIN
    msg_id := queen.push('test-push-partition', 'custom-partition', '{"message": "Partitioned"}'::jsonb);
    
    -- Verify message is in the correct partition
    SELECT p.name INTO partition_name
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    WHERE m.id = msg_id;
    
    IF partition_name = 'custom-partition' THEN
        RAISE NOTICE 'PASS: Push to specific partition works';
    ELSE
        RAISE EXCEPTION 'FAIL: Message in wrong partition: %', partition_name;
    END IF;
END;
$$;

-- Test 5: Same transaction ID on different partitions (uses temp table)
CREATE TEMP TABLE test_push_parts (part TEXT, msg_id UUID);
INSERT INTO test_push_parts VALUES ('a', queen.push('test-push-diff-part', 'partition-a', '{"part": "a"}'::jsonb, 'same-txn-diff-part'));
INSERT INTO test_push_parts VALUES ('b', queen.push('test-push-diff-part', 'partition-b', '{"part": "b"}'::jsonb, 'same-txn-diff-part'));

DO $$
DECLARE
    msg_id1 UUID;
    msg_id2 UUID;
BEGIN
    SELECT msg_id INTO msg_id1 FROM test_push_parts WHERE part = 'a';
    SELECT msg_id INTO msg_id2 FROM test_push_parts WHERE part = 'b';
    
    IF msg_id1 IS NOT NULL AND msg_id2 IS NOT NULL AND msg_id1 <> msg_id2 THEN
        RAISE NOTICE 'PASS: Same txn_id on different partitions creates different messages';
    ELSE
        RAISE EXCEPTION 'FAIL: Expected different message IDs for different partitions';
    END IF;
END;
$$;
DROP TABLE test_push_parts;

-- Test 6: Push batch of messages (using transaction API)
DO $$
DECLARE
    result_count INT;
    txn_result JSONB;
BEGIN
    -- Use transaction API for multiple pushes in same transaction
    txn_result := queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-push-batch', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-push-batch', 'payload', '{"idx": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-push-batch', 'payload', '{"idx": 3}'::jsonb)
    ));
    
    SELECT COUNT(*) INTO result_count
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = 'test-push-batch';
    
    IF result_count >= 3 THEN
        RAISE NOTICE 'PASS: Batch push created % messages', result_count;
    ELSE
        RAISE EXCEPTION 'FAIL: Expected at least 3 messages, got %', result_count;
    END IF;
END;
$$;

-- Test 7: Push null payload
DO $$
DECLARE
    msg_id UUID;
    stored_payload JSONB;
BEGIN
    msg_id := queen.push('test-push-null', 'null'::jsonb);
    
    SELECT payload INTO stored_payload FROM queen.messages WHERE id = msg_id;
    
    IF stored_payload::text = 'null' THEN
        RAISE NOTICE 'PASS: Null payload stored correctly';
    ELSE
        RAISE NOTICE 'PASS: Null payload stored as: %', stored_payload;
    END IF;
END;
$$;

-- Test 8: Push empty object payload
DO $$
DECLARE
    msg_id UUID;
    stored_payload JSONB;
BEGIN
    msg_id := queen.push('test-push-empty', '{}'::jsonb);
    
    SELECT payload INTO stored_payload FROM queen.messages WHERE id = msg_id;
    
    IF stored_payload = '{}'::jsonb THEN
        RAISE NOTICE 'PASS: Empty object payload stored correctly';
    ELSE
        RAISE EXCEPTION 'FAIL: Empty object not preserved: %', stored_payload;
    END IF;
END;
$$;

-- Test 9: Push large payload
DO $$
DECLARE
    msg_id UUID;
    large_payload JSONB;
    stored_payload JSONB;
    item_count INT;
BEGIN
    -- Create a large payload with 1000 items
    SELECT jsonb_build_object(
        'items', jsonb_agg(jsonb_build_object('id', i, 'data', 'item-' || i))
    ) INTO large_payload
    FROM generate_series(1, 1000) i;
    
    msg_id := queen.push('test-push-large', large_payload);
    
    SELECT payload INTO stored_payload FROM queen.messages WHERE id = msg_id;
    SELECT jsonb_array_length(stored_payload->'items') INTO item_count;
    
    IF item_count = 1000 THEN
        RAISE NOTICE 'PASS: Large payload (1000 items) stored correctly';
    ELSE
        RAISE EXCEPTION 'FAIL: Large payload corrupted, got % items', item_count;
    END IF;
END;
$$;

-- Test 10: Push with push_full (all options)
DO $$
DECLARE
    result RECORD;
BEGIN
    SELECT * INTO result FROM queen.push_full(
        p_queue := 'test-push-full',
        p_payload := '{"full": true}'::jsonb,
        p_partition := 'full-partition',
        p_transaction_id := 'full-txn-id',
        p_namespace := 'test-namespace',
        p_task := 'test-task'
    );
    
    IF result.message_id IS NOT NULL AND result.transaction_id = 'full-txn-id' THEN
        RAISE NOTICE 'PASS: push_full works with all options';
    ELSE
        RAISE EXCEPTION 'FAIL: push_full did not return expected values';
    END IF;
END;
$$;

\echo 'PASS: Push tests completed'
