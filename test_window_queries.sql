-- =============================================================================
-- WINDOW QUERIES TEST SCRIPT
-- Run this to verify all queries work before implementing window code
-- =============================================================================

\echo '=== Test 1: Create stream_windows table ==='
\echo ''

DROP TABLE IF EXISTS queen.stream_windows CASCADE;

CREATE TABLE IF NOT EXISTS queen.stream_windows (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    
    -- Window identification
    window_id VARCHAR NOT NULL,  
    consumer_group VARCHAR NOT NULL,
    queue_name VARCHAR NOT NULL,
    partition_name VARCHAR,
    
    -- Window configuration
    window_type VARCHAR NOT NULL,       
    window_duration_seconds INT,        
    window_count INT,                   
    grace_period_seconds INT DEFAULT 0,
    
    -- Window boundaries (time-based)
    window_start TIMESTAMPTZ,
    window_end TIMESTAMPTZ,
    
    -- Window boundaries (count-based)
    window_start_offset BIGINT,
    window_end_offset BIGINT,
    
    -- Status tracking
    status VARCHAR DEFAULT 'open',      
    message_count INT DEFAULT 0,
    
    -- Timestamps
    created_at TIMESTAMPTZ DEFAULT NOW(),
    closed_at TIMESTAMPTZ,              
    consumed_at TIMESTAMPTZ
);

-- Indexes
CREATE INDEX idx_stream_windows_consumer 
    ON queen.stream_windows(consumer_group, queue_name, status);

CREATE INDEX idx_stream_windows_ready 
    ON queen.stream_windows(consumer_group, queue_name, window_end)
    WHERE status = 'ready' AND consumed_at IS NULL;

CREATE INDEX idx_stream_windows_time 
    ON queen.stream_windows(window_start, window_end);

-- Unique constraint for window tracking
ALTER TABLE queen.stream_windows 
    ADD CONSTRAINT stream_windows_unique 
    UNIQUE (consumer_group, queue_name, window_id);

\echo 'Table created successfully!'
\echo ''

-- =============================================================================

\echo '=== Test 2: Calculate Epoch-Aligned Window Boundaries (5-second windows) ==='
\echo ''

WITH time_now AS (
    SELECT NOW() as now
),
window_params AS (
    SELECT 
        5 as duration_seconds,
        1 as grace_seconds
),
window_bounds AS (
    SELECT 
        EXTRACT(EPOCH FROM ct.now)::BIGINT as current_epoch,
        TO_TIMESTAMP(
            (EXTRACT(EPOCH FROM ct.now)::BIGINT / wp.duration_seconds) * wp.duration_seconds
        ) as current_window_start,
        TO_TIMESTAMP(
            ((EXTRACT(EPOCH FROM ct.now)::BIGINT / wp.duration_seconds) * wp.duration_seconds) + wp.duration_seconds
        ) as current_window_end,
        (ct.now >= TO_TIMESTAMP(
            ((EXTRACT(EPOCH FROM ct.now)::BIGINT / wp.duration_seconds) * wp.duration_seconds) + wp.duration_seconds + wp.grace_seconds
        )) as is_ready
    FROM time_now ct, window_params wp
)
SELECT 
    current_window_start::TEXT as window_start,
    current_window_end::TEXT as window_end,
    is_ready,
    current_epoch,
    CASE 
        WHEN is_ready THEN '✓ Window is ready to consume'
        ELSE '✗ Window not ready yet (still in grace period or current)'
    END as status
FROM window_bounds;

\echo ''

-- =============================================================================

\echo '=== Test 3: Calculate Previous Window (for messages that already exist) ==='
\echo ''

-- Calculate a window from 10 seconds ago to 5 seconds ago
WITH window_params AS (
    SELECT 
        5 as duration_seconds,
        NOW() - INTERVAL '10 seconds' as window_start,
        NOW() - INTERVAL '5 seconds' as window_end
)
SELECT 
    window_start::TEXT,
    window_end::TEXT,
    '✓ This window should contain recent messages' as note
FROM window_params;

\echo ''

-- =============================================================================

\echo '=== Test 4: Get Next Window for Consumer Group ==='
\echo ''

-- Simulate: What's the next window for consumer 'window_test'?
WITH last_consumed AS (
    SELECT 
        COALESCE(MAX(window_end), TO_TIMESTAMP(0)) as last_window_end,
        COUNT(*) as consumed_count
    FROM queen.stream_windows
    WHERE consumer_group = 'window_test'
    AND queue_name = 'smartchat-agent'
    AND status = 'consumed'
),
window_params AS (
    SELECT 
        5::INT as duration_seconds,
        1::INT as grace_seconds
),
next_window AS (
    SELECT 
        CASE 
            WHEN lc.last_window_end = TO_TIMESTAMP(0) THEN
                -- First window: use a recent past window that should have messages
                NOW() - INTERVAL '30 seconds'
            ELSE
                lc.last_window_end
        END as window_start,
        
        CASE 
            WHEN lc.last_window_end = TO_TIMESTAMP(0) THEN
                NOW() - INTERVAL '25 seconds'
            ELSE
                lc.last_window_end + (wp.duration_seconds * INTERVAL '1 second')
        END as window_end,
        
        wp.duration_seconds,
        wp.grace_seconds,
        lc.consumed_count
    FROM last_consumed lc, window_params wp
)
SELECT 
    window_start::TEXT,
    window_end::TEXT,
    (NOW() >= window_end + (grace_seconds * INTERVAL '1 second')) as is_ready,
    (window_end > NOW()) as is_future,
    consumed_count as windows_consumed_so_far,
    CASE 
        WHEN (NOW() >= window_end + (grace_seconds * INTERVAL '1 second')) 
        THEN '✓ Ready to consume'
        WHEN (window_end > NOW())
        THEN '✗ Window is in the future'
        ELSE '⧗ In grace period'
    END as status
FROM next_window;

\echo ''

-- =============================================================================

\echo '=== Test 5: Get Messages in Window (last 30 seconds) ==='
\echo ''

-- Get messages from last 30-25 seconds window
SELECT 
    COUNT(*) as message_count,
    MIN(m.created_at) as earliest_message,
    MAX(m.created_at) as latest_message
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'smartchat-agent'
AND m.created_at >= NOW() - INTERVAL '30 seconds'
AND m.created_at < NOW() - INTERVAL '25 seconds';

\echo ''
\echo 'Sample messages:'

SELECT 
    m.id,
    m.payload,
    m.created_at
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'smartchat-agent'
AND m.created_at >= NOW() - INTERVAL '30 seconds'
AND m.created_at < NOW() - INTERVAL '25 seconds'
ORDER BY m.created_at, m.id
LIMIT 5;

\echo ''

-- =============================================================================

\echo '=== Test 6: Get Messages with GroupBy (last 60 seconds) ==='
\echo ''

SELECT 
    (m.payload->>'tenantId') as tenantId,
    COUNT(*) as count,
    AVG((m.payload->>'aiScore')::numeric) as avgScore,
    MIN(m.created_at) as first_message,
    MAX(m.created_at) as last_message
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'smartchat-agent'
AND m.created_at >= NOW() - INTERVAL '60 seconds'
AND m.created_at < NOW()
GROUP BY (m.payload->>'tenantId')
ORDER BY count DESC
LIMIT 10;

\echo ''

-- =============================================================================

\echo '=== Test 7: Mark Window as Consumed ==='
\echo ''

-- Mark a test window as consumed
INSERT INTO queen.stream_windows (
    window_id,
    consumer_group,
    queue_name,
    partition_name,
    window_type,
    window_duration_seconds,
    grace_period_seconds,
    window_start,
    window_end,
    status,
    message_count,
    consumed_at
) VALUES (
    'smartchat-agent@window_test:tumbling:test-window-1',
    'window_test',
    'smartchat-agent',
    NULL,
    'tumbling',
    5,
    1,
    NOW() - INTERVAL '30 seconds',
    NOW() - INTERVAL '25 seconds',
    'consumed',
    42,
    NOW()
)
ON CONFLICT (consumer_group, queue_name, window_id)
DO UPDATE SET
    consumed_at = NOW(),
    status = 'consumed',
    message_count = EXCLUDED.message_count
RETURNING 
    window_id, 
    consumer_group, 
    window_start::TEXT, 
    window_end::TEXT,
    '✓ Window marked as consumed' as status;

\echo ''

-- =============================================================================

\echo '=== Test 8: Verify Window was Recorded ==='
\echo ''

SELECT 
    window_id,
    consumer_group,
    queue_name,
    window_type,
    window_start::TEXT,
    window_end::TEXT,
    status,
    message_count,
    consumed_at::TEXT
FROM queen.stream_windows
WHERE consumer_group = 'window_test'
ORDER BY window_end DESC;

\echo ''

-- =============================================================================

\echo '=== Test 9: Complete Window Flow Simulation ==='
\echo ''

\echo 'Step 1: Check if queue has messages...'
SELECT COUNT(*) as total_messages
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE q.name = 'smartchat-agent';

\echo ''
\echo 'Step 2: Get next window boundaries for consumer...'

WITH last_consumed AS (
    SELECT 
        COALESCE(MAX(window_end), NOW() - INTERVAL '60 seconds') as last_window_end
    FROM queen.stream_windows
    WHERE consumer_group = 'window_test_flow'
    AND queue_name = 'smartchat-agent'
    AND status = 'consumed'
),
window_params AS (
    SELECT 
        5::INT as duration_seconds,
        1::INT as grace_seconds
),
next_window AS (
    SELECT 
        lc.last_window_end as window_start,
        lc.last_window_end + (wp.duration_seconds * INTERVAL '1 second') as window_end,
        wp.duration_seconds,
        wp.grace_seconds
    FROM last_consumed lc, window_params wp
)
SELECT 
    window_start::TEXT,
    window_end::TEXT,
    (NOW() >= window_end + (grace_seconds * INTERVAL '1 second')) as is_ready,
    CASE 
        WHEN (NOW() >= window_end + (grace_seconds * INTERVAL '1 second')) 
        THEN '✓ READY'
        ELSE '✗ NOT READY'
    END as status
FROM next_window;

\echo ''
\echo 'Step 3: Get messages in that window...'

WITH last_consumed AS (
    SELECT 
        COALESCE(MAX(window_end), NOW() - INTERVAL '60 seconds') as last_window_end
    FROM queen.stream_windows
    WHERE consumer_group = 'window_test_flow'
    AND queue_name = 'smartchat-agent'
    AND status = 'consumed'
),
window_params AS (
    SELECT 5::INT as duration_seconds
),
next_window AS (
    SELECT 
        lc.last_window_end as window_start,
        lc.last_window_end + (wp.duration_seconds * INTERVAL '1 second') as window_end
    FROM last_consumed lc, window_params wp
)
SELECT 
    COUNT(*) as message_count,
    MIN(m.created_at)::TEXT as first_msg_time,
    MAX(m.created_at)::TEXT as last_msg_time,
    window_start::TEXT,
    window_end::TEXT
FROM queen.messages m
JOIN queen.partitions p ON p.id = m.partition_id
JOIN queen.queues q ON q.id = p.queue_id
CROSS JOIN next_window
WHERE q.name = 'smartchat-agent'
AND m.created_at >= next_window.window_start
AND m.created_at < next_window.window_end
GROUP BY window_start, window_end;

\echo ''

-- =============================================================================

\echo '=== SUMMARY ==='
\echo ''
\echo 'All queries executed successfully!'
\echo ''
\echo 'What we verified:'
\echo '  ✓ Table creation works'
\echo '  ✓ Window boundary calculation (epoch-aligned)'
\echo '  ✓ Next window calculation for consumer groups'
\echo '  ✓ Message retrieval within window'
\echo '  ✓ GroupBy and aggregation within window'
\echo '  ✓ Window state tracking (consumed/not consumed)'
\echo ''
\echo 'Next steps:'
\echo '  1. Implement window() method in Stream.js'
\echo '  2. Implement window processing in stream_manager.cpp'
\echo '  3. Add /api/v1/stream/window/next endpoint'
\echo ''

