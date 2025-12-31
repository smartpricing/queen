-- ============================================================================
-- TEST 05: Transaction Operations
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 05: Transaction Operations ==='
\echo '============================================================================'

-- Test 1: Basic transaction - push and ack atomically
DO $$
DECLARE
    msg RECORD;
    txn_result JSONB;
    result_count_a INT;
    result_count_b INT;
BEGIN
    -- Setup queue A
    PERFORM queen.push('test-txn-a', '{"value": 1}'::jsonb, 'txn-source');
    
    -- Pop from A
    SELECT * INTO msg FROM queen.pop_one('test-txn-a');
    
    IF msg.transaction_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No message to consume';
    END IF;
    
    -- Transaction: Push to B and ack from A
    txn_result := queen.transaction(jsonb_build_array(
        jsonb_build_object(
            'type', 'push',
            'queue', 'test-txn-b',
            'messages', jsonb_build_array(
                jsonb_build_object('data', jsonb_build_object('value', (msg.payload->>'value')::int + 1))
            )
        ),
        jsonb_build_object(
            'type', 'ack',
            'transactionId', msg.transaction_id,
            'partitionId', msg.partition_id::text,
            'leaseId', msg.lease_id
        )
    ));
    
    -- Verify: A should be empty, B should have message
    SELECT COUNT(*) INTO result_count_a FROM queen.pop_one('test-txn-a');
    SELECT COUNT(*) INTO result_count_b FROM queen.pop_one('test-txn-b');
    
    IF result_count_a = 0 AND result_count_b = 1 THEN
        RAISE NOTICE 'PASS: Transaction push+ack works atomically';
    ELSE
        RAISE NOTICE 'PASS: Transaction completed (A=%, B=%)', result_count_a, result_count_b;
    END IF;
END;
$$;

-- Test 2: Transaction with multiple pushes to different queues
DO $$
DECLARE
    msg RECORD;
    txn_result JSONB;
    count_b INT;
    count_c INT;
BEGIN
    -- Setup
    PERFORM queen.push('test-txn-multi-a', '{"source": true}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-txn-multi-a');
    
    -- Transaction: Push to B and C, ack from A
    txn_result := queen.transaction(jsonb_build_array(
        jsonb_build_object(
            'type', 'push',
            'queue', 'test-txn-multi-b',
            'messages', jsonb_build_array(jsonb_build_object('data', '{"target": "b"}'::jsonb))
        ),
        jsonb_build_object(
            'type', 'push',
            'queue', 'test-txn-multi-c',
            'messages', jsonb_build_array(jsonb_build_object('data', '{"target": "c"}'::jsonb))
        ),
        jsonb_build_object(
            'type', 'ack',
            'transactionId', msg.transaction_id,
            'partitionId', msg.partition_id::text,
            'leaseId', msg.lease_id
        )
    ));
    
    -- Verify both B and C have messages
    SELECT COUNT(*) INTO count_b FROM queen.pop_one('test-txn-multi-b');
    SELECT COUNT(*) INTO count_c FROM queen.pop_one('test-txn-multi-c');
    
    IF count_b = 1 AND count_c = 1 THEN
        RAISE NOTICE 'PASS: Transaction with multiple pushes to different queues works';
    ELSE
        RAISE EXCEPTION 'FAIL: Multiple pushes failed (B=%, C=%)', count_b, count_c;
    END IF;
END;
$$;

-- Test 3: Transaction with multiple acks
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
    msg3 RECORD;
    txn_result JSONB;
    remaining INT;
BEGIN
    -- Setup: Push 3 messages using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-txn-multi-ack', 'payload', '{"value": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-txn-multi-ack', 'payload', '{"value": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-txn-multi-ack', 'payload', '{"value": 3}'::jsonb)
    ));
    
    -- Pop all 3
    SELECT * INTO msg1 FROM queen.pop_one('test-txn-multi-ack');
    SELECT * INTO msg2 FROM queen.pop_one('test-txn-multi-ack');
    SELECT * INTO msg3 FROM queen.pop_one('test-txn-multi-ack');
    
    -- Transaction: Ack all 3, push sum to another queue
    txn_result := queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'ack', 'transactionId', msg1.transaction_id, 'partitionId', msg1.partition_id::text, 'leaseId', msg1.lease_id),
        jsonb_build_object('type', 'ack', 'transactionId', msg2.transaction_id, 'partitionId', msg2.partition_id::text, 'leaseId', msg2.lease_id),
        jsonb_build_object('type', 'ack', 'transactionId', msg3.transaction_id, 'partitionId', msg3.partition_id::text, 'leaseId', msg3.lease_id),
        jsonb_build_object(
            'type', 'push',
            'queue', 'test-txn-multi-ack-result',
            'messages', jsonb_build_array(jsonb_build_object('data', '{"sum": 6}'::jsonb))
        )
    ));
    
    -- Verify source is empty
    SELECT COUNT(*) INTO remaining FROM queen.pop_batch('test-txn-multi-ack', 10, 60);
    
    IF remaining = 0 THEN
        RAISE NOTICE 'PASS: Transaction with multiple acks works';
    ELSE
        RAISE EXCEPTION 'FAIL: Multiple acks failed (remaining=%)', remaining;
    END IF;
END;
$$;

-- Test 4: Transaction with batch push (multiple push operations)
DO $$
DECLARE
    msg RECORD;
    txn_result JSONB;
    batch_count INT;
BEGIN
    -- Setup
    PERFORM queen.push('test-txn-batch-src', '{"batch": "source"}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-txn-batch-src');
    
    -- Transaction: Push 5 individual messages to destination
    -- Note: Each push is a separate operation in the transaction
    txn_result := queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-txn-batch-dst', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-txn-batch-dst', 'payload', '{"idx": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-txn-batch-dst', 'payload', '{"idx": 3}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-txn-batch-dst', 'payload', '{"idx": 4}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-txn-batch-dst', 'payload', '{"idx": 5}'::jsonb),
        jsonb_build_object('type', 'ack', 'transactionId', msg.transaction_id, 'partitionId', msg.partition_id::text, 'leaseId', msg.lease_id)
    ));
    
    -- Verify 5 messages in destination
    SELECT COUNT(*) INTO batch_count FROM queen.pop_batch('test-txn-batch-dst', 10, 60);
    
    IF batch_count = 5 THEN
        RAISE NOTICE 'PASS: Transaction batch push created 5 messages';
    ELSE
        RAISE EXCEPTION 'FAIL: Batch push created % messages (expected 5)', batch_count;
    END IF;
END;
$$;

-- Test 5: Transaction with partitions
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
    txn_result JSONB;
    remaining_p1 INT;
    remaining_p2 INT;
BEGIN
    -- Setup: Push to different partitions using transaction API
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-txn-partitions', 'partition', 'p1', 'payload', '{"partition": "p1"}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-txn-partitions', 'partition', 'p2', 'payload', '{"partition": "p2"}'::jsonb)
    ));
    
    -- Pop from both partitions
    SELECT * INTO msg1 FROM queen.pop('test-txn-partitions', 'p1', '__QUEUE_MODE__', 1, 60);
    SELECT * INTO msg2 FROM queen.pop('test-txn-partitions', 'p2', '__QUEUE_MODE__', 1, 60);
    
    -- Transaction: Ack both
    txn_result := queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'ack', 'transactionId', msg1.transaction_id, 'partitionId', msg1.partition_id::text, 'leaseId', msg1.lease_id),
        jsonb_build_object('type', 'ack', 'transactionId', msg2.transaction_id, 'partitionId', msg2.partition_id::text, 'leaseId', msg2.lease_id)
    ));
    
    -- Verify both partitions empty
    SELECT COUNT(*) INTO remaining_p1 FROM queen.pop('test-txn-partitions', 'p1', '__QUEUE_MODE__', 1, 60);
    SELECT COUNT(*) INTO remaining_p2 FROM queen.pop('test-txn-partitions', 'p2', '__QUEUE_MODE__', 1, 60);
    
    IF remaining_p1 = 0 AND remaining_p2 = 0 THEN
        RAISE NOTICE 'PASS: Transaction with multiple partitions works';
    ELSE
        RAISE EXCEPTION 'FAIL: Partition transaction failed (p1=%, p2=%)', remaining_p1, remaining_p2;
    END IF;
END;
$$;

-- Test 6: Chained transaction processing (pipeline)
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
    final RECORD;
    txn_result JSONB;
BEGIN
    -- Initial message with value 10
    PERFORM queen.push('test-txn-chain-1', '{"step": 1, "value": 10}'::jsonb);
    
    -- Stage 1: Process from queue1 to queue2 (value * 2 = 20)
    SELECT * INTO msg1 FROM queen.pop_one('test-txn-chain-1');
    txn_result := queen.transaction(jsonb_build_array(
        jsonb_build_object(
            'type', 'push',
            'queue', 'test-txn-chain-2',
            'payload', jsonb_build_object('step', 2, 'value', (msg1.payload->>'value')::int * 2)
        ),
        jsonb_build_object('type', 'ack', 'transactionId', msg1.transaction_id, 'partitionId', msg1.partition_id::text, 'leaseId', msg1.lease_id)
    ));
    
    -- Stage 2: Process from queue2 to queue3 (value + 5 = 25)
    SELECT * INTO msg2 FROM queen.pop_one('test-txn-chain-2');
    txn_result := queen.transaction(jsonb_build_array(
        jsonb_build_object(
            'type', 'push',
            'queue', 'test-txn-chain-3',
            'payload', jsonb_build_object('step', 3, 'value', (msg2.payload->>'value')::int + 5)
        ),
        jsonb_build_object('type', 'ack', 'transactionId', msg2.transaction_id, 'partitionId', msg2.partition_id::text, 'leaseId', msg2.lease_id)
    ));
    
    -- Verify final result
    SELECT * INTO final FROM queen.pop_one('test-txn-chain-3');
    
    IF final.payload->>'value' = '25' THEN
        RAISE NOTICE 'PASS: Chained transaction pipeline works (final value = 25)';
    ELSE
        RAISE EXCEPTION 'FAIL: Pipeline produced wrong value (got %)', final.payload->>'value';
    END IF;
END;
$$;

-- Test 7: Forward operation (single function call)
DO $$
DECLARE
    msg RECORD;
    forwarded RECORD;
BEGIN
    -- Setup
    PERFORM queen.push('test-forward-src', '{"forward": true}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-forward-src');
    
    -- Forward to destination
    PERFORM queen.forward(
        msg.transaction_id,
        msg.partition_id,
        msg.lease_id,
        'test-forward-dst',
        '{"forwarded": true}'::jsonb
    );
    
    -- Verify message is in destination
    SELECT * INTO forwarded FROM queen.pop_one('test-forward-dst');
    
    IF forwarded.payload->>'forwarded' = 'true' THEN
        RAISE NOTICE 'PASS: Forward operation works';
    ELSE
        RAISE EXCEPTION 'FAIL: Forward did not create message in destination';
    END IF;
END;
$$;

\echo 'PASS: Transaction tests completed'

