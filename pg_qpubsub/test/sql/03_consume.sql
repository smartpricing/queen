-- ============================================================================
-- Test 03: Consume Operations
-- ============================================================================

\echo '=== Test 03: Consume Operations ==='

-- Setup: Create fresh queue and produce messages
SELECT queen.configure('test_consume_queue', 60, 3, false);

-- Produce test messages
SELECT queen.produce_one('test_consume_queue', '{"order": 1}'::jsonb, 'Default', 'consume-txn-1');
SELECT queen.produce_one('test_consume_queue', '{"order": 2}'::jsonb, 'Default', 'consume-txn-2');
SELECT queen.produce_one('test_consume_queue', '{"order": 3}'::jsonb, 'Default', 'consume-txn-3');

-- Test 1: Basic consume (single message, queue mode)
DO $$
DECLARE
    v_msg RECORD;
    v_count INT := 0;
BEGIN
    FOR v_msg IN SELECT * FROM queen.consume_one('test_consume_queue')
    LOOP
        v_count := v_count + 1;
        IF v_msg.partition_id IS NULL THEN
            RAISE EXCEPTION 'FAIL: partition_id is NULL';
        END IF;
        IF v_msg.id IS NULL THEN
            RAISE EXCEPTION 'FAIL: id is NULL';
        END IF;
        IF v_msg.lease_id IS NULL THEN
            RAISE EXCEPTION 'FAIL: lease_id is NULL';
        END IF;
    END LOOP;
    
    IF v_count != 1 THEN
        RAISE EXCEPTION 'FAIL: Expected 1 message, got %', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: Basic consume returns single message';
END;
$$;

-- Test 2: Consume with batch_size (in new queue to avoid state from test 1)
SELECT queen.configure('test_consume_batch', 60, 3, false);
SELECT queen.produce_one('test_consume_batch', '{"batch": 1}'::jsonb);
SELECT queen.produce_one('test_consume_batch', '{"batch": 2}'::jsonb);
SELECT queen.produce_one('test_consume_batch', '{"batch": 3}'::jsonb);

DO $$
DECLARE
    v_count INT := 0;
BEGIN
    SELECT COUNT(*) INTO v_count FROM queen.consume_one('test_consume_batch', '__QUEUE_MODE__', 5);
    
    IF v_count != 3 THEN
        RAISE EXCEPTION 'FAIL: Batch consume expected 3 messages, got %', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: Batch consume works (got % messages)', v_count;
END;
$$;

-- Setup: Fresh queue for consumer group tests
SELECT queen.configure('test_consume_cg', 60, 3, false);
SELECT queen.produce_one('test_consume_cg', '{"cg": 1}'::jsonb, 'Default', 'cg-txn-1');
SELECT queen.produce_one('test_consume_cg', '{"cg": 2}'::jsonb, 'Default', 'cg-txn-2');

-- Test 3: Consume with consumer group
DO $$
DECLARE
    v_msg RECORD;
    v_count INT := 0;
BEGIN
    FOR v_msg IN SELECT * FROM queen.consume_one('test_consume_cg', 'test-consumer-group', 10)
    LOOP
        v_count := v_count + 1;
    END LOOP;
    
    IF v_count != 2 THEN
        RAISE EXCEPTION 'FAIL: Consumer group consume expected 2 messages, got %', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: Consumer group consume works';
END;
$$;

-- Test 4: Auto-commit mode
SELECT queen.configure('test_consume_autocommit', 60, 3, false);
SELECT queen.produce_one('test_consume_autocommit', '{"auto": "commit"}'::jsonb, 'Default', 'auto-txn-1');

DO $$
DECLARE
    v_msg RECORD;
    v_count_before BIGINT;
    v_count_after BIGINT;
BEGIN
    SELECT queen.lag('test_consume_autocommit') INTO v_count_before;
    
    -- Consume with auto_commit = true
    FOR v_msg IN SELECT * FROM queen.consume_one(
        'test_consume_autocommit',
        '__QUEUE_MODE__',
        1,           -- batch_size
        NULL,        -- partition
        0,           -- timeout
        TRUE         -- auto_commit
    )
    LOOP
        NULL; -- Just consume
    END LOOP;
    
    SELECT queen.lag('test_consume_autocommit') INTO v_count_after;
    
    IF v_count_after >= v_count_before THEN
        RAISE EXCEPTION 'FAIL: Auto-commit did not reduce queue depth';
    END IF;
    
    RAISE NOTICE 'PASS: Auto-commit mode works';
END;
$$;

-- Test 5: Produce to specific partition creates messages in correct partition
SELECT queen.configure('test_consume_partition', 60, 3, false);
SELECT queen.produce_one('test_consume_partition', '{"p": "A"}'::jsonb, 'PartitionA', 'part-a-1');
SELECT queen.produce_one('test_consume_partition', '{"p": "B"}'::jsonb, 'PartitionB', 'part-b-1');

DO $$
DECLARE
    v_partition_a_count INT;
    v_partition_b_count INT;
    v_msg RECORD;
BEGIN
    -- Debug: Check what's in the partitions
    SELECT COUNT(*) INTO v_partition_a_count
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = 'test_consume_partition'
      AND p.name = 'PartitionA';
    
    SELECT COUNT(*) INTO v_partition_b_count
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = 'test_consume_partition'
      AND p.name = 'PartitionB';
    
    IF v_partition_a_count != 1 THEN
        RAISE EXCEPTION 'FAIL: Expected 1 message in PartitionA, got %', v_partition_a_count;
    END IF;
    
    IF v_partition_b_count != 1 THEN
        RAISE EXCEPTION 'FAIL: Expected 1 message in PartitionB, got %', v_partition_b_count;
    END IF;
    
    RAISE NOTICE 'PASS: Messages correctly placed in specific partitions (A: %, B: %)', v_partition_a_count, v_partition_b_count;
END;
$$;

-- Test 6: Consume returns correct fields
SELECT queen.configure('test_consume_fields', 60, 3, false);
SELECT queen.produce_one('test_consume_fields', '{"field": "test"}'::jsonb, 'Default', 'fields-txn-1');

DO $$
DECLARE
    v_msg RECORD;
BEGIN
    FOR v_msg IN SELECT * FROM queen.consume_one('test_consume_fields')
    LOOP
        -- Verify all fields are present
        IF v_msg.partition_id IS NULL THEN
            RAISE EXCEPTION 'FAIL: partition_id is NULL';
        END IF;
        IF v_msg.id IS NULL THEN
            RAISE EXCEPTION 'FAIL: id is NULL';
        END IF;
        IF v_msg.transaction_id IS NULL THEN
            RAISE EXCEPTION 'FAIL: transaction_id is NULL';
        END IF;
        IF v_msg.payload IS NULL THEN
            RAISE EXCEPTION 'FAIL: payload is NULL';
        END IF;
        IF v_msg.created_at IS NULL THEN
            RAISE EXCEPTION 'FAIL: created_at is NULL';
        END IF;
        IF v_msg.lease_id IS NULL THEN
            RAISE EXCEPTION 'FAIL: lease_id is NULL';
        END IF;
        IF v_msg.lease_id NOT LIKE 'wrapper-%' THEN
            RAISE EXCEPTION 'FAIL: lease_id format incorrect: %', v_msg.lease_id;
        END IF;
    END LOOP;
    
    RAISE NOTICE 'PASS: Consume returns all correct fields';
END;
$$;

-- Test 7: Empty queue returns no messages
SELECT queen.configure('test_consume_empty', 60, 3, false);

DO $$
DECLARE
    v_count INT := 0;
BEGIN
    SELECT COUNT(*) INTO v_count FROM queen.consume_one('test_consume_empty');
    
    IF v_count != 0 THEN
        RAISE EXCEPTION 'FAIL: Empty queue returned % messages', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: Empty queue returns no messages';
END;
$$;

-- Test 8: Lease prevents double-consume in queue mode
SELECT queen.configure('test_consume_lease', 60, 3, false);
SELECT queen.produce_one('test_consume_lease', '{"lease": "test"}'::jsonb, 'Default', 'lease-txn-1');

DO $$
DECLARE
    v_count1 INT;
    v_count2 INT;
BEGIN
    -- First consume should get the message
    SELECT COUNT(*) INTO v_count1 FROM queen.consume_one('test_consume_lease');
    
    -- Second consume should get nothing (message is leased)
    SELECT COUNT(*) INTO v_count2 FROM queen.consume_one('test_consume_lease');
    
    IF v_count1 != 1 THEN
        RAISE EXCEPTION 'FAIL: First consume should get 1 message, got %', v_count1;
    END IF;
    
    IF v_count2 != 0 THEN
        RAISE EXCEPTION 'FAIL: Second consume should get 0 messages (leased), got %', v_count2;
    END IF;
    
    RAISE NOTICE 'PASS: Lease prevents double-consume';
END;
$$;

\echo 'Test 03: PASSED'

