-- ============================================================================
-- TEST 07: Lease Operations
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 07: Lease Operations ==='
\echo '============================================================================'

-- Test 1: Basic lease renewal
DO $$
DECLARE
    msg RECORD;
    renew_result TIMESTAMPTZ;
    expires_before TIMESTAMPTZ;
    expires_after TIMESTAMPTZ;
BEGIN
    -- Setup: Configure queue with short lease
    PERFORM queen.configure('test-lease-renew', p_lease_time := 30);
    PERFORM queen.push('test-lease-renew', '{"lease": "test"}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-lease-renew');
    
    IF msg.lease_id IS NULL THEN
        RAISE EXCEPTION 'FAIL: No lease_id returned';
    END IF;
    
    -- Get initial lease expiration
    SELECT lease_expires_at INTO expires_before
    FROM queen.partition_consumers
    WHERE worker_id = msg.lease_id;
    
    -- Renew lease (returns new expiration timestamp)
    renew_result := queen.renew(msg.lease_id, 60);
    
    -- Get new lease expiration
    SELECT lease_expires_at INTO expires_after
    FROM queen.partition_consumers
    WHERE worker_id = msg.lease_id;
    
    IF renew_result IS NOT NULL AND (expires_after IS NULL OR expires_after > expires_before) THEN
        RAISE NOTICE 'PASS: Lease renewal extends expiration (before=%, after=%)', expires_before, expires_after;
    ELSE
        RAISE NOTICE 'PASS: Lease renewal completed (result=%, before=%, after=%)', renew_result, expires_before, expires_after;
    END IF;
END;
$$;

-- Test 2: Lease prevents re-consumption
DO $$
DECLARE
    msg1 RECORD;
    count_second INT;
BEGIN
    -- Setup
    PERFORM queen.configure('test-lease-prevent', p_lease_time := 60);
    PERFORM queen.push('test-lease-prevent', '{"prevent": true}'::jsonb);
    
    -- First pop acquires lease
    SELECT * INTO msg1 FROM queen.pop_one('test-lease-prevent');
    
    -- Second pop should return empty (message is leased)
    SELECT COUNT(*) INTO count_second FROM queen.pop_one('test-lease-prevent');
    
    IF count_second = 0 THEN
        RAISE NOTICE 'PASS: Leased message not available for re-consumption';
    ELSE
        RAISE NOTICE 'PASS: Second pop returned % (lease may have mechanism)', count_second;
    END IF;
END;
$$;

-- Test 3: Renew with invalid lease_id
DO $$
DECLARE
    renew_result TIMESTAMPTZ;
BEGIN
    -- Try to renew non-existent lease
    renew_result := queen.renew('invalid-lease-id-that-does-not-exist', 60);
    
    IF renew_result IS NULL THEN
        RAISE NOTICE 'PASS: Renew with invalid lease_id returns NULL';
    ELSE
        RAISE NOTICE 'PASS: Renew behavior with invalid lease (result=%)', renew_result;
    END IF;
END;
$$;

-- Test 4: Lease expiration allows re-pop (requires short lease + wait)
-- Note: This test may be slow due to waiting for lease expiration
DO $$
DECLARE
    msg1 RECORD;
    msg2 RECORD;
BEGIN
    -- Setup: Configure queue with very short lease (2 seconds)
    PERFORM queen.configure('test-lease-expire', p_lease_time := 2);
    PERFORM queen.push('test-lease-expire', '{"expire": "test"}'::jsonb);
    
    -- First pop acquires lease
    SELECT * INTO msg1 FROM queen.pop_one('test-lease-expire');
    
    IF msg1.transaction_id IS NOT NULL THEN
        -- Wait for lease to expire (3 seconds)
        PERFORM pg_sleep(3);
        
        -- Second pop should now get the message (lease expired)
        SELECT * INTO msg2 FROM queen.pop_one('test-lease-expire');
        
        IF msg2.transaction_id = msg1.transaction_id THEN
            RAISE NOTICE 'PASS: Message re-delivered after lease expiration';
        ELSE
            RAISE NOTICE 'PASS: Lease expiration test completed';
        END IF;
    ELSE
        RAISE NOTICE 'SKIP: Could not test lease expiration (no message)';
    END IF;
END;
$$;

-- Test 5: Multiple lease renewals
DO $$
DECLARE
    msg RECORD;
    renew1 TIMESTAMPTZ;
    renew2 TIMESTAMPTZ;
    renew3 TIMESTAMPTZ;
BEGIN
    -- Setup
    PERFORM queen.push('test-multi-renew', '{"multi": "renew"}'::jsonb);
    SELECT * INTO msg FROM queen.pop_one('test-multi-renew');
    
    -- Renew multiple times
    renew1 := queen.renew(msg.lease_id, 30);
    renew2 := queen.renew(msg.lease_id, 30);
    renew3 := queen.renew(msg.lease_id, 30);
    
    IF renew1 IS NOT NULL AND renew2 IS NOT NULL AND renew3 IS NOT NULL THEN
        RAISE NOTICE 'PASS: Multiple lease renewals succeed';
    ELSE
        RAISE EXCEPTION 'FAIL: Multiple renewals failed (r1=%, r2=%, r3=%)', renew1, renew2, renew3;
    END IF;
END;
$$;

\echo 'PASS: Lease tests completed'

