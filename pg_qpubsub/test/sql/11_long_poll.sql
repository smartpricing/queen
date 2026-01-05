-- ============================================================================
-- Test 11: Long Polling (consume with timeout)
-- ============================================================================

\echo '=== Test 11: Long Polling ==='

-- Test 1: Immediate return when messages available
SELECT queen.configure('test_longpoll', 60, 3, false);
SELECT queen.produce_one('test_longpoll', '{"immediate": "return"}'::jsonb, 'Default', 'longpoll-1');

DO $$
DECLARE
    v_start TIMESTAMPTZ;
    v_end TIMESTAMPTZ;
    v_count INT;
BEGIN
    v_start := clock_timestamp();
    
    -- Consume with 5 second timeout (should return immediately)
    SELECT COUNT(*) INTO v_count FROM queen.consume_one(
        'test_longpoll',
        '__QUEUE_MODE__',
        1,      -- batch_size
        NULL,   -- partition
        5       -- timeout_seconds
    );
    
    v_end := clock_timestamp();
    
    IF v_count != 1 THEN
        RAISE EXCEPTION 'FAIL: Expected 1 message, got %', v_count;
    END IF;
    
    -- Should complete in less than 1 second (not wait for timeout)
    IF EXTRACT(EPOCH FROM (v_end - v_start)) > 1 THEN
        RAISE EXCEPTION 'FAIL: Should return immediately when messages available';
    END IF;
    
    RAISE NOTICE 'PASS: Long poll returns immediately when messages available';
END;
$$;

-- Test 2: Timeout when no messages
SELECT queen.configure('test_longpoll_empty', 60, 3, false);

DO $$
DECLARE
    v_start TIMESTAMPTZ;
    v_end TIMESTAMPTZ;
    v_count INT;
    v_elapsed DOUBLE PRECISION;
BEGIN
    v_start := clock_timestamp();
    
    -- Consume with 2 second timeout on empty queue
    SELECT COUNT(*) INTO v_count FROM queen.consume_one(
        'test_longpoll_empty',
        '__QUEUE_MODE__',
        1,      -- batch_size
        NULL,   -- partition
        2       -- timeout_seconds (short for testing)
    );
    
    v_end := clock_timestamp();
    v_elapsed := EXTRACT(EPOCH FROM (v_end - v_start));
    
    IF v_count != 0 THEN
        RAISE EXCEPTION 'FAIL: Expected 0 messages, got %', v_count;
    END IF;
    
    -- Should wait approximately 2 seconds (allow 1-4 seconds tolerance)
    IF v_elapsed < 1 OR v_elapsed > 4 THEN
        RAISE EXCEPTION 'FAIL: Should wait ~2 seconds, waited % seconds', v_elapsed;
    END IF;
    
    RAISE NOTICE 'PASS: Long poll waits and times out correctly (waited % seconds)', v_elapsed;
END;
$$;

-- Test 3: Zero timeout = immediate return
DO $$
DECLARE
    v_start TIMESTAMPTZ;
    v_end TIMESTAMPTZ;
    v_count INT;
BEGIN
    v_start := clock_timestamp();
    
    -- Consume with 0 timeout
    SELECT COUNT(*) INTO v_count FROM queen.consume_one(
        'test_longpoll_empty',
        '__QUEUE_MODE__',
        1,
        NULL,
        0  -- timeout = 0
    );
    
    v_end := clock_timestamp();
    
    -- Should return immediately
    IF EXTRACT(EPOCH FROM (v_end - v_start)) > 0.5 THEN
        RAISE EXCEPTION 'FAIL: timeout=0 should return immediately';
    END IF;
    
    RAISE NOTICE 'PASS: timeout=0 returns immediately';
END;
$$;

-- Test 4: Long poll with consumer group
SELECT queen.configure('test_longpoll_cg', 60, 3, false);
SELECT queen.produce_one('test_longpoll_cg', '{"cg": "poll"}'::jsonb, 'Default', 'longpoll-cg-1');

DO $$
DECLARE
    v_start TIMESTAMPTZ;
    v_end TIMESTAMPTZ;
    v_count INT;
BEGIN
    v_start := clock_timestamp();
    
    -- Long poll with consumer group
    SELECT COUNT(*) INTO v_count FROM queen.consume_one(
        'test_longpoll_cg',
        'longpoll-group',
        1,
        NULL,
        5  -- timeout
    );
    
    v_end := clock_timestamp();
    
    IF v_count != 1 THEN
        RAISE EXCEPTION 'FAIL: Consumer group long poll expected 1, got %', v_count;
    END IF;
    
    -- Should return quickly (message was available)
    IF EXTRACT(EPOCH FROM (v_end - v_start)) > 1 THEN
        RAISE EXCEPTION 'FAIL: Should return quickly with available message';
    END IF;
    
    RAISE NOTICE 'PASS: Long poll works with consumer groups';
END;
$$;

-- Test 5: Default timeout (no timeout = immediate return)
DO $$
DECLARE
    v_start TIMESTAMPTZ;
    v_end TIMESTAMPTZ;
    v_count INT;
BEGIN
    v_start := clock_timestamp();
    
    -- Consume without timeout parameter (defaults to 0)
    SELECT COUNT(*) INTO v_count FROM queen.consume_one('test_longpoll_empty');
    
    v_end := clock_timestamp();
    
    -- Should return immediately (default timeout = 0)
    IF EXTRACT(EPOCH FROM (v_end - v_start)) > 0.5 THEN
        RAISE EXCEPTION 'FAIL: Default timeout should be immediate';
    END IF;
    
    RAISE NOTICE 'PASS: Default timeout is immediate return';
END;
$$;

-- Test 6: Batch consume with timeout
SELECT queen.configure('test_longpoll_batch', 60, 3, false);
SELECT queen.produce_one('test_longpoll_batch', '{"batch": 1}'::jsonb);
SELECT queen.produce_one('test_longpoll_batch', '{"batch": 2}'::jsonb);

DO $$
DECLARE
    v_count INT;
BEGIN
    -- Consume batch with timeout
    SELECT COUNT(*) INTO v_count FROM queen.consume_one(
        'test_longpoll_batch',
        '__QUEUE_MODE__',
        10,    -- batch_size
        NULL,
        3      -- timeout
    );
    
    IF v_count != 2 THEN
        RAISE EXCEPTION 'FAIL: Batch consume expected 2, got %', v_count;
    END IF;
    
    RAISE NOTICE 'PASS: Batch consume with timeout works (got % messages)', v_count;
END;
$$;

\echo 'Test 11: PASSED'
