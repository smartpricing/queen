-- ============================================================================
-- Test 08: UUID v7 Functions
-- ============================================================================

\echo '=== Test 08: UUID v7 Functions ==='

-- Test 1: uuid_generate_v7 returns valid UUID
DO $$
DECLARE
    v_uuid UUID;
    v_uuid_text TEXT;
BEGIN
    v_uuid := queen.uuid_generate_v7();
    
    IF v_uuid IS NULL THEN
        RAISE EXCEPTION 'FAIL: uuid_generate_v7 returned NULL';
    END IF;
    
    -- Check version nibble (should be 7)
    v_uuid_text := v_uuid::TEXT;
    IF substring(v_uuid_text, 15, 1) != '7' THEN
        RAISE EXCEPTION 'FAIL: UUID version is not 7: %', v_uuid_text;
    END IF;
    
    -- Check variant (should be 8, 9, a, or b)
    IF substring(v_uuid_text, 20, 1) NOT IN ('8', '9', 'a', 'b') THEN
        RAISE EXCEPTION 'FAIL: UUID variant incorrect: %', v_uuid_text;
    END IF;
    
    RAISE NOTICE 'PASS: uuid_generate_v7 returns valid UUID: %', v_uuid;
END;
$$;

-- Test 2: Sequential UUIDs are ordered
DO $$
DECLARE
    v_uuid1 UUID;
    v_uuid2 UUID;
    v_uuid3 UUID;
BEGIN
    v_uuid1 := queen.uuid_generate_v7();
    v_uuid2 := queen.uuid_generate_v7();
    v_uuid3 := queen.uuid_generate_v7();
    
    IF NOT (v_uuid1 < v_uuid2 AND v_uuid2 < v_uuid3) THEN
        RAISE EXCEPTION 'FAIL: Sequential UUIDs not ordered: % < % < %', v_uuid1, v_uuid2, v_uuid3;
    END IF;
    
    RAISE NOTICE 'PASS: Sequential UUIDs are ordered';
END;
$$;

-- Test 3: uuid_v7_to_timestamptz extracts time
DO $$
DECLARE
    v_before TIMESTAMPTZ;
    v_uuid UUID;
    v_extracted TIMESTAMPTZ;
    v_after TIMESTAMPTZ;
BEGIN
    v_before := clock_timestamp();
    v_uuid := queen.uuid_generate_v7();
    v_after := clock_timestamp();
    
    v_extracted := queen.uuid_v7_to_timestamptz(v_uuid);
    
    IF v_extracted IS NULL THEN
        RAISE EXCEPTION 'FAIL: uuid_v7_to_timestamptz returned NULL';
    END IF;
    
    -- Extracted time should be between before and after (with 1 second tolerance)
    IF v_extracted < v_before - INTERVAL '1 second' OR v_extracted > v_after + INTERVAL '1 second' THEN
        RAISE EXCEPTION 'FAIL: Extracted time % not between % and %', v_extracted, v_before, v_after;
    END IF;
    
    RAISE NOTICE 'PASS: uuid_v7_to_timestamptz extracts correct time: %', v_extracted;
END;
$$;

-- Test 4: uuid_v7_boundary creates boundary UUID
DO $$
DECLARE
    v_time TIMESTAMPTZ;
    v_boundary UUID;
    v_boundary_text TEXT;
BEGIN
    v_time := NOW();
    v_boundary := queen.uuid_v7_boundary(v_time);
    
    IF v_boundary IS NULL THEN
        RAISE EXCEPTION 'FAIL: uuid_v7_boundary returned NULL';
    END IF;
    
    v_boundary_text := v_boundary::TEXT;
    
    -- Boundary UUID should have zeros in specific positions
    IF substring(v_boundary_text, 15, 1) != '7' THEN
        RAISE EXCEPTION 'FAIL: Boundary UUID version incorrect: %', v_boundary_text;
    END IF;
    
    RAISE NOTICE 'PASS: uuid_v7_boundary creates valid boundary: %', v_boundary;
END;
$$;

-- Test 5: uuid_generate_v7_at creates UUID at specific time
DO $$
DECLARE
    v_target_time TIMESTAMPTZ;
    v_uuid UUID;
    v_extracted TIMESTAMPTZ;
BEGIN
    v_target_time := '2025-01-01 00:00:00+00'::TIMESTAMPTZ;
    v_uuid := queen.uuid_generate_v7_at(v_target_time);
    v_extracted := queen.uuid_v7_to_timestamptz(v_uuid);
    
    -- Extracted time should match target (within 1 second)
    IF ABS(EXTRACT(EPOCH FROM v_extracted - v_target_time)) > 1 THEN
        RAISE EXCEPTION 'FAIL: uuid_generate_v7_at time mismatch (target: %, got: %)', v_target_time, v_extracted;
    END IF;
    
    RAISE NOTICE 'PASS: uuid_generate_v7_at creates UUID at specific time';
END;
$$;

-- Test 6: Range query using UUID v7 boundary
SELECT queen.configure('test_uuid_range', 60, 3, false);

-- Produce some messages
SELECT queen.produce_one('test_uuid_range', '{"seq": 1}'::jsonb, 'Default', 'uuid-range-1');
SELECT pg_sleep(0.1);
SELECT queen.produce_one('test_uuid_range', '{"seq": 2}'::jsonb, 'Default', 'uuid-range-2');
SELECT pg_sleep(0.1);
SELECT queen.produce_one('test_uuid_range', '{"seq": 3}'::jsonb, 'Default', 'uuid-range-3');

DO $$
DECLARE
    v_midpoint TIMESTAMPTZ;
    v_boundary UUID;
    v_count_before INT;
    v_count_after INT;
BEGIN
    -- Get midpoint time
    v_midpoint := NOW() - INTERVAL '50 milliseconds';
    v_boundary := queen.uuid_v7_boundary(v_midpoint);
    
    -- Count messages before and after boundary
    SELECT COUNT(*) INTO v_count_before
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = 'test_uuid_range'
      AND m.id < v_boundary;
    
    SELECT COUNT(*) INTO v_count_after
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.name = 'test_uuid_range'
      AND m.id >= v_boundary;
    
    IF v_count_before + v_count_after != 3 THEN
        RAISE EXCEPTION 'FAIL: Total count should be 3, got % + %', v_count_before, v_count_after;
    END IF;
    
    RAISE NOTICE 'PASS: Range queries with UUID v7 boundary work (before: %, after: %)', v_count_before, v_count_after;
END;
$$;

-- Test 7: High-volume UUID generation maintains ordering
DO $$
DECLARE
    v_uuids UUID[];
    v_i INT;
    v_prev UUID;
BEGIN
    -- Generate 100 UUIDs rapidly
    FOR v_i IN 1..100 LOOP
        v_uuids := array_append(v_uuids, queen.uuid_generate_v7());
    END LOOP;
    
    -- Verify all are in order
    v_prev := v_uuids[1];
    FOR v_i IN 2..100 LOOP
        IF v_uuids[v_i] <= v_prev THEN
            RAISE EXCEPTION 'FAIL: UUID at position % not greater than previous', v_i;
        END IF;
        v_prev := v_uuids[v_i];
    END LOOP;
    
    RAISE NOTICE 'PASS: High-volume UUID generation maintains strict ordering';
END;
$$;

\echo 'Test 08: PASSED'
