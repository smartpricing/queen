-- ============================================================================
-- Test 02: Produce Operations
-- ============================================================================

\echo '=== Test 02: Produce Operations ==='

-- Setup: Configure test queue
SELECT queen.configure('test_produce_queue', 60, 3, true);

-- Test 1: Basic produce
DO $$
DECLARE
    v_msg_id UUID;
BEGIN
    v_msg_id := queen.produce_one('test_produce_queue', '{"test": "data"}'::jsonb);
    
    IF v_msg_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: Basic produce returned NULL';
    END IF;
    
    RAISE NOTICE 'PASS: Basic produce returns UUID: %', v_msg_id;
END;
$$;

-- Test 2: Produce with custom partition
DO $$
DECLARE
    v_msg_id UUID;
BEGIN
    v_msg_id := queen.produce_one('test_produce_queue', '{"partition": "custom"}'::jsonb, 'CustomPartition');
    
    IF v_msg_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: Produce with partition returned NULL';
    END IF;
    
    -- Verify partition was created
    IF NOT EXISTS (
        SELECT 1 FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name = 'test_produce_queue' AND p.name = 'CustomPartition'
    ) THEN
        RAISE EXCEPTION 'FAIL: Custom partition not created';
    END IF;
    
    RAISE NOTICE 'PASS: Produce with custom partition works';
END;
$$;

-- Test 3: Produce with transaction_id (idempotency key)
DO $$
DECLARE
    v_msg_id UUID;
BEGIN
    v_msg_id := queen.produce_one(
        'test_produce_queue',
        '{"with": "txn_id"}'::jsonb,
        'Default',
        'my-custom-transaction-123'
    );
    
    IF v_msg_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: Produce with transaction_id returned NULL';
    END IF;
    
    -- Verify transaction_id was stored
    IF NOT EXISTS (
        SELECT 1 FROM queen.messages
        WHERE transaction_id = 'my-custom-transaction-123'
    ) THEN
        RAISE EXCEPTION 'FAIL: Transaction ID not stored';
    END IF;
    
    RAISE NOTICE 'PASS: Produce with transaction_id works';
END;
$$;

-- Test 4: Produce with notify flag
DO $$
DECLARE
    v_msg_id UUID;
BEGIN
    v_msg_id := queen.produce_one(
        'test_produce_queue',
        '{"with": "notify"}'::jsonb,
        'Default',
        NULL,
        TRUE  -- p_notify = true
    );
    
    IF v_msg_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: Produce with notify returned NULL';
    END IF;
    
    RAISE NOTICE 'PASS: Produce with notify works';
END;
$$;

-- Test 5: produce_notify convenience function
DO $$
DECLARE
    v_msg_id UUID;
BEGIN
    v_msg_id := queen.produce_notify('test_produce_queue', '{"notify": "convenience"}'::jsonb);
    
    IF v_msg_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: produce_notify returned NULL';
    END IF;
    
    RAISE NOTICE 'PASS: produce_notify convenience function works';
END;
$$;

-- Test 6: produce_full with all options
DO $$
DECLARE
    v_msg_id UUID;
    v_txn_id TEXT;
BEGIN
    SELECT message_id, transaction_id INTO v_msg_id, v_txn_id
    FROM queen.produce_full(
        'test_produce_queue',
        '{"full": "options"}'::jsonb,
        'FullPartition',
        NULL,  -- auto transaction_id
        'test-namespace',  -- namespace
        'test-task'        -- task
    );
    
    IF v_msg_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: produce_full returned NULL message_id';
    END IF;
    
    IF v_txn_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: produce_full returned NULL transaction_id';
    END IF;
    
    RAISE NOTICE 'PASS: produce_full works with all options';
END;
$$;

-- Test 7: Multiple produces create multiple messages
-- Note: Each produce must be in a separate statement due to temp table lifecycle
SELECT queen.produce_one('test_produce_queue', '{"batch": 1}'::jsonb);
SELECT queen.produce_one('test_produce_queue', '{"batch": 2}'::jsonb);
SELECT queen.produce_one('test_produce_queue', '{"batch": 3}'::jsonb);

DO $$
DECLARE
    v_count BIGINT;
BEGIN
    SELECT COUNT(*) INTO v_count FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = 'test_produce_queue';
    
    -- We should have at least 3 batch messages plus the previous ones
    IF v_count < 3 THEN
        RAISE EXCEPTION 'FAIL: Expected at least 3 messages, got %', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: Multiple produces create multiple messages (total: %)', v_count;
END;
$$;

-- Test 8: Produce with complex JSON payload
DO $$
DECLARE
    v_msg_id UUID;
    v_stored JSONB;
BEGIN
    v_msg_id := queen.produce_one('test_produce_queue', '{
        "nested": {"deep": {"value": 123}},
        "array": [1, 2, 3],
        "boolean": true,
        "null_val": null,
        "string": "hello world"
    }'::jsonb);
    
    SELECT payload INTO v_stored FROM queen.messages WHERE id = v_msg_id;
    
    IF v_stored->'nested'->'deep'->>'value' != '123' THEN
        RAISE EXCEPTION 'FAIL: Complex JSON not stored correctly';
    END IF;
    
    RAISE NOTICE 'PASS: Complex JSON payload works';
END;
$$;

\echo 'Test 02: PASSED'

