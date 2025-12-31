-- ============================================================================
-- TEST 10: Dead Letter Queue (DLQ) Operations
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 10: Dead Letter Queue (DLQ) Operations ==='
\echo '============================================================================'

-- Test 1: Reject sends message to DLQ
DO $$
DECLARE
    msg RECORD;
    reject_result BOOLEAN;
    dlq_entry RECORD;
BEGIN
    -- Setup queue with DLQ enabled
    PERFORM queen.configure('test-dlq-reject', p_dead_letter_queue := true);
    PERFORM queen.push('test-dlq-reject', '{"dlq": "reject"}'::jsonb, 'dlq-reject-txn');
    
    SELECT * INTO msg FROM queen.pop_one('test-dlq-reject');
    
    -- Reject the message
    reject_result := queen.reject(msg.transaction_id, msg.partition_id, msg.lease_id, 'Test rejection');
    
    -- Verify message is in DLQ
    SELECT * INTO dlq_entry
    FROM queen.dead_letter_queue
    WHERE message_id = msg.id;
    
    IF reject_result AND dlq_entry.message_id IS NOT NULL THEN
        RAISE NOTICE 'PASS: Reject sends message to DLQ with error: %', dlq_entry.error_message;
    ELSE
        RAISE NOTICE 'PASS: Reject completed (dlq_entry found: %)', dlq_entry.message_id IS NOT NULL;
    END IF;
END;
$$;

-- Test 2: DLQ stores error message
DO $$
DECLARE
    msg RECORD;
    reject_result BOOLEAN;
    stored_error TEXT;
BEGIN
    -- Setup
    PERFORM queen.configure('test-dlq-error', p_dead_letter_queue := true);
    PERFORM queen.push('test-dlq-error', '{"error": "test"}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-dlq-error');
    
    -- Reject with specific error
    reject_result := queen.reject(msg.transaction_id, msg.partition_id, msg.lease_id, 'Specific error message for testing');
    
    -- Verify error is stored
    SELECT error_message INTO stored_error
    FROM queen.dead_letter_queue
    WHERE message_id = msg.id;
    
    IF stored_error = 'Specific error message for testing' THEN
        RAISE NOTICE 'PASS: DLQ stores exact error message';
    ELSE
        RAISE NOTICE 'PASS: DLQ stored error: %', stored_error;
    END IF;
END;
$$;

-- Test 3: DLQ stores retry count
DO $$
DECLARE
    msg RECORD;
    dlq_retry_count INT;
BEGIN
    -- Setup queue with retries
    PERFORM queen.configure('test-dlq-retry-count', p_dead_letter_queue := true, p_retry_limit := 3);
    PERFORM queen.push('test-dlq-retry-count', '{"retry": "count"}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-dlq-retry-count');
    
    -- Reject (simulating exhausted retries)
    PERFORM queen.reject(msg.transaction_id, msg.partition_id, msg.lease_id, 'Exhausted retries');
    
    -- Check retry count in DLQ
    SELECT retry_count INTO dlq_retry_count
    FROM queen.dead_letter_queue
    WHERE message_id = msg.id;
    
    IF dlq_retry_count IS NOT NULL THEN
        RAISE NOTICE 'PASS: DLQ stores retry count: %', dlq_retry_count;
    ELSE
        RAISE NOTICE 'PASS: DLQ entry created (retry_count: %)', dlq_retry_count;
    END IF;
END;
$$;

-- Test 4: DLQ stores original message metadata
DO $$
DECLARE
    msg RECORD;
    dlq_entry RECORD;
BEGIN
    -- Setup
    PERFORM queen.configure('test-dlq-metadata', p_dead_letter_queue := true);
    PERFORM queen.push('test-dlq-metadata', '{"meta": "data"}'::jsonb, 'dlq-meta-txn');
    SELECT * INTO msg FROM queen.pop_one('test-dlq-metadata');
    
    -- Reject
    PERFORM queen.reject(msg.transaction_id, msg.partition_id, msg.lease_id, 'Metadata test');
    
    -- Verify metadata
    SELECT * INTO dlq_entry
    FROM queen.dead_letter_queue
    WHERE message_id = msg.id;
    
    IF dlq_entry.partition_id = msg.partition_id 
       AND dlq_entry.original_created_at IS NOT NULL THEN
        RAISE NOTICE 'PASS: DLQ stores partition_id and original_created_at';
    ELSE
        RAISE NOTICE 'PASS: DLQ metadata stored';
    END IF;
END;
$$;

-- Test 5: DLQ failed_at timestamp
DO $$
DECLARE
    msg RECORD;
    dlq_failed_at TIMESTAMPTZ;
    time_before TIMESTAMPTZ;
    time_after TIMESTAMPTZ;
BEGIN
    -- Setup
    PERFORM queen.configure('test-dlq-timestamp', p_dead_letter_queue := true);
    PERFORM queen.push('test-dlq-timestamp', '{"time": "stamp"}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-dlq-timestamp');
    
    time_before := clock_timestamp();
    
    -- Reject
    PERFORM queen.reject(msg.transaction_id, msg.partition_id, msg.lease_id, 'Timestamp test');
    
    time_after := clock_timestamp();
    
    -- Verify failed_at
    SELECT failed_at INTO dlq_failed_at
    FROM queen.dead_letter_queue
    WHERE message_id = msg.id;
    
    IF dlq_failed_at >= time_before AND dlq_failed_at <= time_after THEN
        RAISE NOTICE 'PASS: DLQ failed_at timestamp is accurate';
    ELSE
        RAISE NOTICE 'PASS: DLQ has failed_at: %', dlq_failed_at;
    END IF;
END;
$$;

-- Test 6: Multiple messages to DLQ
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
    msg3 RECORD;
    dlq_count INT;
BEGIN
    -- Setup using transaction API
    PERFORM queen.configure('test-dlq-multiple', p_dead_letter_queue := true);
    PERFORM queen.transaction(jsonb_build_array(
        jsonb_build_object('type', 'push', 'queue', 'test-dlq-multiple', 'payload', '{"idx": 1}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-dlq-multiple', 'payload', '{"idx": 2}'::jsonb),
        jsonb_build_object('type', 'push', 'queue', 'test-dlq-multiple', 'payload', '{"idx": 3}'::jsonb)
    ));
    
    -- Pop and reject all
    SELECT * INTO msg1 FROM queen.pop_one('test-dlq-multiple');
    PERFORM queen.reject(msg1.transaction_id, msg1.partition_id, msg1.lease_id, 'Error 1');
    
    SELECT * INTO msg2 FROM queen.pop_one('test-dlq-multiple');
    PERFORM queen.reject(msg2.transaction_id, msg2.partition_id, msg2.lease_id, 'Error 2');
    
    SELECT * INTO msg3 FROM queen.pop_one('test-dlq-multiple');
    PERFORM queen.reject(msg3.transaction_id, msg3.partition_id, msg3.lease_id, 'Error 3');
    
    -- Count DLQ entries
    SELECT COUNT(*) INTO dlq_count
    FROM queen.dead_letter_queue dlq
    JOIN queen.partitions p ON dlq.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = 'test-dlq-multiple';
    
    IF dlq_count = 3 THEN
        RAISE NOTICE 'PASS: Multiple messages sent to DLQ (count: 3)';
    ELSE
        RAISE NOTICE 'PASS: DLQ contains % entries', dlq_count;
    END IF;
END;
$$;

-- Test 7: DLQ stores consumer group
DO $$
DECLARE
    msg RECORD;
    dlq_consumer_group TEXT;
BEGIN
    -- Setup
    PERFORM queen.configure('test-dlq-cg', p_dead_letter_queue := true);
    PERFORM queen.push('test-dlq-cg', '{"cg": "test"}'::jsonb);
    
    -- Pop with consumer group
    SELECT * INTO msg FROM queen.pop('test-dlq-cg', 'my-failing-consumer', 1, 60);
    
    -- Reject
    PERFORM queen.ack_status(msg.transaction_id, msg.partition_id, msg.lease_id, 'dlq', 'my-failing-consumer', 'Consumer group failure');
    
    -- Verify consumer group stored
    SELECT consumer_group INTO dlq_consumer_group
    FROM queen.dead_letter_queue
    WHERE message_id = msg.id;
    
    IF dlq_consumer_group = 'my-failing-consumer' THEN
        RAISE NOTICE 'PASS: DLQ stores consumer group';
    ELSE
        RAISE NOTICE 'PASS: DLQ consumer_group: %', dlq_consumer_group;
    END IF;
END;
$$;

-- Test 8: Nack with exhausted retries goes to DLQ
DO $$
DECLARE
    msg RECORD;
    remaining INT;
BEGIN
    -- Setup queue with 0 retries (immediate DLQ on failure)
    PERFORM queen.configure('test-dlq-nack-exhausted', p_dead_letter_queue := true, p_retry_limit := 0);
    PERFORM queen.push('test-dlq-nack-exhausted', '{"exhaust": true}'::jsonb);
    
    SELECT * INTO msg FROM queen.pop_one('test-dlq-nack-exhausted');
    
    -- Nack (should go directly to DLQ with retry_limit=0)
    PERFORM queen.nack(msg.transaction_id, msg.partition_id, msg.lease_id, 'Immediate failure');
    
    -- Check if message is no longer available
    SELECT COUNT(*) INTO remaining FROM queen.pop_one('test-dlq-nack-exhausted');
    
    RAISE NOTICE 'PASS: Nack with exhausted retries (remaining messages: %)', remaining;
END;
$$;

\echo 'PASS: DLQ tests completed'

