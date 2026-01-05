-- ============================================================================
-- Test 07: Lease Management
-- ============================================================================

\echo '=== Test 07: Lease Management ==='

-- Test 1: Lease time comes from queue configuration
SELECT queen.configure('test_lease_config', 120, 3, false);  -- 120 second lease
SELECT queen.produce_one('test_lease_config', '{"lease": "config"}'::jsonb, 'Default', 'lease-cfg-1');

DO $$
DECLARE
    v_msg RECORD;
    v_lease_expires TIMESTAMPTZ;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_lease_config') LIMIT 1;
    
    -- Check lease expiry in partition_consumers
    SELECT lease_expires_at INTO v_lease_expires
    FROM queen.partition_consumers pc
    WHERE pc.worker_id = v_msg.lease_id;
    
    IF v_lease_expires IS NULL THEN
        RAISE EXCEPTION 'FAIL: No lease recorded';
    END IF;
    
    -- Lease should be approximately 120 seconds from now (within 5 seconds tolerance)
    IF v_lease_expires < NOW() + INTERVAL '115 seconds' OR v_lease_expires > NOW() + INTERVAL '125 seconds' THEN
        RAISE EXCEPTION 'FAIL: Lease expiry not close to 120 seconds: %', v_lease_expires;
    END IF;
    
    -- Cleanup
    PERFORM queen.commit_one(v_msg.transaction_id, v_msg.partition_id, v_msg.lease_id);
    
    RAISE NOTICE 'PASS: Lease time comes from queue configuration';
END;
$$;

-- Test 2: Renew lease
SELECT queen.configure('test_renew', 30, 3, false);  -- Short 30 second lease
SELECT queen.produce_one('test_renew', '{"renew": "test"}'::jsonb, 'Default', 'renew-1');

DO $$
DECLARE
    v_msg RECORD;
    v_original_expires TIMESTAMPTZ;
    v_new_expires TIMESTAMPTZ;
    v_result TIMESTAMPTZ;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_renew') LIMIT 1;
    
    -- Get original expiry
    SELECT lease_expires_at INTO v_original_expires
    FROM queen.partition_consumers pc
    WHERE pc.worker_id = v_msg.lease_id;
    
    -- Renew for 60 more seconds
    v_result := queen.renew_one(v_msg.lease_id, 60);
    
    IF v_result IS NULL THEN
        RAISE EXCEPTION 'FAIL: Renew returned NULL';
    END IF;
    
    -- Get new expiry
    SELECT lease_expires_at INTO v_new_expires
    FROM queen.partition_consumers pc
    WHERE pc.worker_id = v_msg.lease_id;
    
    IF v_new_expires <= v_original_expires THEN
        RAISE EXCEPTION 'FAIL: Lease was not extended (original: %, new: %)', v_original_expires, v_new_expires;
    END IF;
    
    -- Cleanup
    PERFORM queen.commit_one(v_msg.transaction_id, v_msg.partition_id, v_msg.lease_id);
    
    RAISE NOTICE 'PASS: Renew lease works (extended from % to %)', v_original_expires, v_new_expires;
END;
$$;

-- Test 3: Renew with default extension
SELECT queen.produce_one('test_renew', '{"renew": "default"}'::jsonb, 'Default', 'renew-2');

DO $$
DECLARE
    v_msg RECORD;
    v_result TIMESTAMPTZ;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_renew') LIMIT 1;
    
    -- Renew with default extension (60 seconds)
    v_result := queen.renew_one(v_msg.lease_id);
    
    IF v_result IS NULL THEN
        RAISE EXCEPTION 'FAIL: Renew with default returned NULL';
    END IF;
    
    -- Cleanup
    PERFORM queen.commit_one(v_msg.transaction_id, v_msg.partition_id, v_msg.lease_id);
    
    RAISE NOTICE 'PASS: Renew with default extension works';
END;
$$;

-- Test 4: Renew invalid lease returns NULL
DO $$
DECLARE
    v_result TIMESTAMPTZ;
BEGIN
    v_result := queen.renew_one('non-existent-lease-id', 60);
    
    IF v_result IS NOT NULL THEN
        RAISE EXCEPTION 'FAIL: Renew invalid lease should return NULL';
    END IF;
    
    RAISE NOTICE 'PASS: Renew invalid lease returns NULL';
END;
$$;

-- Test 5: Leased messages are not re-consumed (queue mode)
SELECT queen.configure('test_lease_protect', 300, 3, false);
SELECT queen.produce_one('test_lease_protect', '{"protected": "message"}'::jsonb, 'Default', 'protect-1');

DO $$
DECLARE
    v_msg1 RECORD;
    v_count INT;
BEGIN
    -- First consume takes the lease
    SELECT * INTO v_msg1 FROM queen.consume_one('test_lease_protect') LIMIT 1;
    
    IF v_msg1.id IS NULL THEN
        RAISE EXCEPTION 'FAIL: First consume got nothing';
    END IF;
    
    -- Second consume should get nothing (message is leased)
    SELECT COUNT(*) INTO v_count FROM queen.consume_one('test_lease_protect');
    
    IF v_count != 0 THEN
        RAISE EXCEPTION 'FAIL: Second consume should get 0 messages while leased';
    END IF;
    
    -- Cleanup
    PERFORM queen.commit_one(v_msg1.transaction_id, v_msg1.partition_id, v_msg1.lease_id);
    
    RAISE NOTICE 'PASS: Leased messages protected from re-consumption';
END;
$$;

-- Test 6: Lease released on commit
SELECT queen.produce_one('test_lease_protect', '{"release": "on commit"}'::jsonb, 'Default', 'release-1');

DO $$
DECLARE
    v_msg RECORD;
    v_lease_exists BOOLEAN;
BEGIN
    -- Consume message
    SELECT * INTO v_msg FROM queen.consume_one('test_lease_protect') LIMIT 1;
    
    -- Verify lease exists
    SELECT EXISTS (
        SELECT 1 FROM queen.partition_consumers
        WHERE worker_id = v_msg.lease_id
          AND lease_expires_at IS NOT NULL
    ) INTO v_lease_exists;
    
    IF NOT v_lease_exists THEN
        RAISE EXCEPTION 'FAIL: Lease not created';
    END IF;
    
    -- Commit
    PERFORM queen.commit_one(v_msg.transaction_id, v_msg.partition_id, v_msg.lease_id);
    
    -- Verify lease is released
    SELECT EXISTS (
        SELECT 1 FROM queen.partition_consumers
        WHERE worker_id = v_msg.lease_id
          AND lease_expires_at IS NOT NULL
    ) INTO v_lease_exists;
    
    IF v_lease_exists THEN
        RAISE EXCEPTION 'FAIL: Lease not released after commit';
    END IF;
    
    RAISE NOTICE 'PASS: Lease released on commit';
END;
$$;

\echo 'Test 07: PASSED'
