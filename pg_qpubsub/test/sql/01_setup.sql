-- ============================================================================
-- Test 01: Setup and Basic Verification
-- ============================================================================

\echo '=== Test 01: Setup and Basic Verification ==='

-- Verify schema exists
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_namespace WHERE nspname = 'queen') THEN
        RAISE EXCEPTION 'FAIL: Schema "queen" does not exist';
    END IF;
    RAISE NOTICE 'PASS: Schema "queen" exists';
END;
$$;

-- Verify wrapper functions exist (simplified API)
DO $$
DECLARE
    required_functions TEXT[] := ARRAY[
        -- Primary API (JSONB batch)
        'produce', 'consume', 'commit', 'renew', 'transaction',
        -- Convenience API (scalar)
        'produce_one', 'produce_full', 'produce_notify',
        'consume_one',
        'commit_one', 'nack', 'reject',
        'renew_one',
        -- Utilities
        'configure', 'seek', 'delete_consumer_group',
        'lag', 'depth', 'has_messages',
        'forward', 'channel_name', 'notify',
        -- UUID v7
        'uuid_generate_v7', 'uuid_v7_to_timestamptz', 'uuid_v7_boundary', 'uuid_generate_v7_at'
    ];
    f TEXT;
BEGIN
    FOREACH f IN ARRAY required_functions LOOP
        IF NOT EXISTS (
            SELECT 1 FROM pg_proc
            WHERE pronamespace = 'queen'::regnamespace
            AND proname = f
        ) THEN
            RAISE EXCEPTION 'FAIL: Required function "queen.%" does not exist', f;
        END IF;
    END LOOP;
    RAISE NOTICE 'PASS: All required wrapper functions exist';
END;
$$;

-- Verify core procedures exist
DO $$
DECLARE
    required_procedures TEXT[] := ARRAY[
        'push_messages_v2',
        'pop_unified_batch',
        'ack_messages_v2',
        'renew_lease_v2',
        'execute_transaction_v2',
        'has_pending_messages',
        'configure_queue_v1',
        'seek_consumer_group_v1',
        'delete_consumer_group_v1'
    ];
    p TEXT;
BEGIN
    FOREACH p IN ARRAY required_procedures LOOP
        IF NOT EXISTS (
            SELECT 1 FROM pg_proc
            WHERE pronamespace = 'queen'::regnamespace
            AND proname = p
        ) THEN
            RAISE EXCEPTION 'FAIL: Required procedure "queen.%" does not exist', p;
        END IF;
    END LOOP;
    RAISE NOTICE 'PASS: All required core procedures exist';
END;
$$;

-- Verify required tables exist
DO $$
DECLARE
    required_tables TEXT[] := ARRAY[
        'queues', 'partitions', 'messages', 'partition_consumers'
    ];
    t TEXT;
BEGIN
    FOREACH t IN ARRAY required_tables LOOP
        IF NOT EXISTS (
            SELECT 1 FROM pg_tables
            WHERE schemaname = 'queen'
            AND tablename = t
        ) THEN
            RAISE EXCEPTION 'FAIL: Required table "queen.%" does not exist', t;
        END IF;
    END LOOP;
    RAISE NOTICE 'PASS: All required tables exist';
END;
$$;

\echo 'Test 01: PASSED'
