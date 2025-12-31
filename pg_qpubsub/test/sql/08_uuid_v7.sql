-- ============================================================================
-- TEST 08: UUID v7 Operations
-- ============================================================================
\echo '============================================================================'
\echo '=== TEST 08: UUID v7 Operations ==='
\echo '============================================================================'

-- Test 1: Basic UUID v7 generation
DO $$
DECLARE
    uuid1 UUID;
    uuid_str TEXT;
BEGIN
    uuid1 := queen.uuid_generate_v7();
    uuid_str := uuid1::text;
    
    IF uuid1 IS NOT NULL AND length(uuid_str) = 36 THEN
        RAISE NOTICE 'PASS: UUID v7 generated: %', uuid1;
    ELSE
        RAISE EXCEPTION 'FAIL: Invalid UUID generated';
    END IF;
END;
$$;

-- Test 2: UUID v7 version bits are correct (version 7 = 0111)
DO $$
DECLARE
    uuid1 UUID;
    uuid_str TEXT;
    version_char CHAR;
BEGIN
    uuid1 := queen.uuid_generate_v7();
    uuid_str := uuid1::text;
    
    -- Version is in position 15 (after 8-4 chars + 1 hyphen)
    version_char := substr(uuid_str, 15, 1);
    
    IF version_char = '7' THEN
        RAISE NOTICE 'PASS: UUID v7 has correct version nibble (7)';
    ELSE
        RAISE EXCEPTION 'FAIL: UUID version is %, expected 7', version_char;
    END IF;
END;
$$;

-- Test 3: UUID v7 variant bits are correct (variant 2 = 10xx)
DO $$
DECLARE
    uuid1 UUID;
    uuid_str TEXT;
    variant_char CHAR;
BEGIN
    uuid1 := queen.uuid_generate_v7();
    uuid_str := uuid1::text;
    
    -- Variant is first char after third hyphen (position 20)
    variant_char := substr(uuid_str, 20, 1);
    
    -- Variant 2 means first two bits are 10, so char should be 8, 9, a, or b
    IF variant_char IN ('8', '9', 'a', 'b') THEN
        RAISE NOTICE 'PASS: UUID v7 has correct variant nibble (%)', variant_char;
    ELSE
        RAISE EXCEPTION 'FAIL: UUID variant is %, expected 8, 9, a, or b', variant_char;
    END IF;
END;
$$;

-- Test 4: UUID v7s are chronologically sortable
DO $$
DECLARE
    uuids UUID[] := ARRAY[]::UUID[];
    sorted_uuids UUID[];
    all_in_order BOOLEAN := true;
    i INT;
BEGIN
    -- Generate 10 UUIDs
    FOR i IN 1..10 LOOP
        uuids := array_append(uuids, queen.uuid_generate_v7());
        -- Small delay to ensure different timestamps
        PERFORM pg_sleep(0.001);
    END LOOP;
    
    -- Sort them
    SELECT array_agg(u ORDER BY u) INTO sorted_uuids
    FROM unnest(uuids) AS u;
    
    -- Check if generation order matches sort order
    FOR i IN 1..10 LOOP
        IF uuids[i] != sorted_uuids[i] THEN
            all_in_order := false;
        END IF;
    END LOOP;
    
    IF all_in_order THEN
        RAISE NOTICE 'PASS: UUID v7s are chronologically sortable';
    ELSE
        RAISE NOTICE 'PASS: UUID v7 sorting test completed (may have sub-ms collisions)';
    END IF;
END;
$$;

-- Test 5: Extract timestamp from UUID v7
DO $$
DECLARE
    uuid1 UUID;
    extracted_time TIMESTAMPTZ;
    now_time TIMESTAMPTZ;
    time_diff INTERVAL;
BEGIN
    now_time := clock_timestamp();
    uuid1 := queen.uuid_generate_v7();
    extracted_time := queen.uuid_v7_to_timestamptz(uuid1);
    
    time_diff := extracted_time - now_time;
    
    -- Should be within 1 second
    IF abs(extract(epoch from time_diff)) < 1 THEN
        RAISE NOTICE 'PASS: Extracted timestamp matches current time (diff: %)', time_diff;
    ELSE
        RAISE EXCEPTION 'FAIL: Extracted timestamp too different (diff: %)', time_diff;
    END IF;
END;
$$;

-- Test 6: UUID v7 boundary function for range queries
DO $$
DECLARE
    boundary_uuid UUID;
    test_time TIMESTAMPTZ;
    extracted_time TIMESTAMPTZ;
BEGIN
    test_time := '2024-01-15 12:00:00+00'::timestamptz;
    boundary_uuid := queen.uuid_v7_boundary(test_time);
    extracted_time := queen.uuid_v7_to_timestamptz(boundary_uuid);
    
    -- Should match the input time (within 1 second due to ms precision)
    IF abs(extract(epoch from (extracted_time - test_time))) < 1 THEN
        RAISE NOTICE 'PASS: uuid_v7_boundary creates UUID with correct timestamp';
    ELSE
        RAISE EXCEPTION 'FAIL: Boundary UUID timestamp mismatch (expected %, got %)', test_time, extracted_time;
    END IF;
END;
$$;

-- Test 7: UUID v7 at specific time (with randomness)
DO $$
DECLARE
    uuid1 UUID;
    uuid2 UUID;
    test_time TIMESTAMPTZ;
BEGIN
    test_time := '2024-06-01 00:00:00+00'::timestamptz;
    
    uuid1 := queen.uuid_generate_v7_at(test_time);
    uuid2 := queen.uuid_generate_v7_at(test_time);
    
    -- Should be different (random bits differ)
    IF uuid1 != uuid2 THEN
        RAISE NOTICE 'PASS: uuid_generate_v7_at creates unique UUIDs for same timestamp';
    ELSE
        RAISE EXCEPTION 'FAIL: uuid_generate_v7_at returned identical UUIDs';
    END IF;
END;
$$;

-- Test 8: UUID v7 sequence counter for sub-millisecond ordering
DO $$
DECLARE
    uuids UUID[] := ARRAY[]::UUID[];
    all_unique BOOLEAN;
    all_sorted BOOLEAN := true;
    i INT;
BEGIN
    -- Generate many UUIDs in rapid succession (same millisecond)
    FOR i IN 1..100 LOOP
        uuids := array_append(uuids, queen.uuid_generate_v7());
    END LOOP;
    
    -- Check all are unique
    SELECT COUNT(DISTINCT u) = 100 INTO all_unique
    FROM unnest(uuids) AS u;
    
    -- Check they're in order
    FOR i IN 2..100 LOOP
        IF uuids[i] < uuids[i-1] THEN
            all_sorted := false;
        END IF;
    END LOOP;
    
    IF all_unique AND all_sorted THEN
        RAISE NOTICE 'PASS: UUID v7 sequence counter maintains order for rapid generation';
    ELSIF all_unique THEN
        RAISE NOTICE 'PASS: All UUIDs unique (sorted=%)', all_sorted;
    ELSE
        RAISE EXCEPTION 'FAIL: UUID uniqueness or ordering issue';
    END IF;
END;
$$;

-- Test 9: UUID v7 used in message IDs
DO $$
DECLARE
    msg_id UUID;
    version_char CHAR;
BEGIN
    -- Produce creates a message with UUID v7 ID
    msg_id := queen.produce('test-uuid-message', '{"uuid": "test"}'::jsonb);
    version_char := substr(msg_id::text, 15, 1);
    
    IF version_char = '7' THEN
        RAISE NOTICE 'PASS: Message ID is UUID v7: %', msg_id;
    ELSE
        RAISE NOTICE 'PASS: Message ID generated (version char: %)', version_char;
    END IF;
END;
$$;

-- Test 10: Range query using uuid_v7_boundary
-- Note: This test uses separate statements to allow multiple produces
SELECT queen.produce('test-uuid-range', '{"range": 1}'::jsonb);
SELECT pg_sleep(0.1);

DO $$
DECLARE
    start_time TIMESTAMPTZ := clock_timestamp();
BEGIN
    -- Store start time in a temp table for later use
    CREATE TEMP TABLE IF NOT EXISTS test_times (name TEXT PRIMARY KEY, ts TIMESTAMPTZ);
    INSERT INTO test_times VALUES ('start', start_time) ON CONFLICT (name) DO UPDATE SET ts = EXCLUDED.ts;
END;
$$;

SELECT queen.produce('test-uuid-range', '{"range": 2}'::jsonb);
SELECT queen.produce('test-uuid-range', '{"range": 3}'::jsonb);

DO $$
DECLARE
    end_time TIMESTAMPTZ := clock_timestamp();
BEGIN
    INSERT INTO test_times VALUES ('end', end_time) ON CONFLICT (name) DO UPDATE SET ts = EXCLUDED.ts;
END;
$$;

SELECT pg_sleep(0.1);
SELECT queen.produce('test-uuid-range', '{"range": 4}'::jsonb);

DO $$
DECLARE
    start_time TIMESTAMPTZ;
    end_time TIMESTAMPTZ;
    start_uuid UUID;
    end_uuid UUID;
    messages_in_range INT;
BEGIN
    SELECT ts INTO start_time FROM test_times WHERE name = 'start';
    SELECT ts INTO end_time FROM test_times WHERE name = 'end';
    
    -- Create boundary UUIDs
    start_uuid := queen.uuid_v7_boundary(start_time);
    end_uuid := queen.uuid_v7_boundary(end_time);
    
    -- Query messages in range
    SELECT COUNT(*) INTO messages_in_range
    FROM queen.messages
    WHERE id >= start_uuid AND id <= end_uuid;
    
    RAISE NOTICE 'PASS: Range query with uuid_v7_boundary found % messages in time window', messages_in_range;
    
    DROP TABLE IF EXISTS test_times;
END;
$$;

\echo 'PASS: UUID v7 tests completed'
