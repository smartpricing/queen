-- ============================================================================
-- TEST 01: Extension Setup and Schema Verification
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 01: Extension Setup and Schema Verification ==='
\echo '============================================================================'

-- Verify schema exists
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_namespace WHERE nspname = 'queen') THEN
        RAISE NOTICE 'PASS: Schema "queen" exists';
    ELSE
        RAISE EXCEPTION 'FAIL: Schema "queen" does not exist';
    END IF;
END;
$$;

-- Verify core tables exist
DO $$
DECLARE
    required_tables TEXT[] := ARRAY[
        'queues', 'partitions', 'messages', 'partition_consumers', 
        'dead_letter_queue', 'stats', 'consumer_groups_metadata'
    ];
    t TEXT;
BEGIN
    FOREACH t IN ARRAY required_tables LOOP
        IF NOT EXISTS (
            SELECT 1 FROM pg_tables 
            WHERE schemaname = 'queen' AND tablename = t
        ) THEN
            RAISE EXCEPTION 'FAIL: Required table "queen.%" does not exist', t;
        END IF;
    END LOOP;
    RAISE NOTICE 'PASS: All required tables exist';
END;
$$;

-- Verify wrapper functions exist
DO $$
DECLARE
    required_functions TEXT[] := ARRAY[
        'push', 'pop', 'pop_batch', 'pop_one', 'pop_auto_ack', 
        'ack', 'ack_group', 'ack_status', 'nack', 'reject',
        'renew', 'forward', 'transaction', 'configure',
        'has_messages', 'depth', 'channel_name', 'notify',
        'push_notify', 'pop_wait', 'pop_wait_one',
        'uuid_generate_v7', 'uuid_v7_to_timestamptz', 'uuid_v7_boundary'
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

\echo 'PASS: Setup tests completed'
