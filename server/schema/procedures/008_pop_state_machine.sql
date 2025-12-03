-- ============================================================================
-- POP State Machine Procedures
-- ============================================================================
-- These procedures support parallel POP execution via the C++ state machine.
-- Each procedure is simple, focused, and optimized for single-partition operations.
-- 
-- Procedures:
--   1. pop_sm_resolve  - Resolve wildcard to specific partition
--   2. pop_sm_lease    - Acquire lease on specific partition  
--   3. pop_sm_fetch    - Fetch messages from partition
--   4. pop_sm_release  - Release lease when no messages found
-- ============================================================================

-- Drop existing functions to allow return type changes
DROP FUNCTION IF EXISTS queen.pop_sm_resolve(TEXT, TEXT);
DROP FUNCTION IF EXISTS queen.pop_sm_lease(TEXT, TEXT, TEXT, INT, TEXT, INT, TEXT, TEXT);
DROP FUNCTION IF EXISTS queen.pop_sm_fetch(UUID, TIMESTAMPTZ, UUID, INT, INT, INT);
DROP FUNCTION IF EXISTS queen.pop_sm_release(UUID, TEXT, TEXT);
DROP FUNCTION IF EXISTS queen.pop_sm_update_cursor(UUID, TEXT, TEXT, UUID, TIMESTAMPTZ, INT);

-- ============================================================================
-- 1. pop_sm_resolve: Resolve wildcard to specific partition
-- ============================================================================
-- Used when partition_name is empty (wildcard POP).
-- Finds the best available partition using SKIP LOCKED to avoid conflicts.
--
-- Parameters:
--   p_queue_name     - Queue name
--   p_consumer_group - Consumer group name
--
-- Returns: Single row with partition info, or empty if no partition available
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_sm_resolve(
    p_queue_name TEXT,
    p_consumer_group TEXT
) RETURNS TABLE (
    out_partition_id UUID,
    out_partition_name TEXT,
    out_queue_id UUID,
    out_lease_time INT,
    out_window_buffer INT,
    out_delayed_processing INT
) LANGUAGE plpgsql AS $$
DECLARE
    v_now TIMESTAMPTZ := NOW();
BEGIN
    -- Find an available partition for wildcard POP
    -- Note: We don't lock here - the lease query handles contention atomically
    -- This allows multiple concurrent wildcards to try the same partition,
    -- but only one will succeed in acquiring the lease
    RETURN QUERY
    SELECT 
        pl.partition_id,
        p.name::TEXT,
        q.id,
        q.lease_time,
        q.window_buffer,
        q.delayed_processing
    FROM queen.partition_lookup pl
    JOIN queen.partitions p ON p.id = pl.partition_id
    JOIN queen.queues q ON q.id = p.queue_id
    LEFT JOIN queen.partition_consumers pc 
        ON pc.partition_id = pl.partition_id 
        AND pc.consumer_group = p_consumer_group
    WHERE pl.queue_name = p_queue_name
      -- Partition must be free (no lease or expired)
      AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
      -- Partition must have unconsumed messages
      AND (pc.last_consumed_created_at IS NULL 
           OR pl.last_message_created_at > pc.last_consumed_created_at
           OR (pl.last_message_created_at = pc.last_consumed_created_at 
               AND pl.last_message_id > pc.last_consumed_id))
    -- Prefer partitions not recently consumed (fair distribution)
    ORDER BY pc.last_consumed_at ASC NULLS FIRST
    LIMIT 1;
END;
$$;

-- ============================================================================
-- 2. pop_sm_lease: Acquire lease on specific partition
-- ============================================================================
-- Attempts to acquire a lease on a specific partition.
-- Returns cursor position and queue config if successful.
--
-- Parameters:
--   p_queue_name     - Queue name
--   p_partition_name - Partition name (must be specific, not empty)
--   p_consumer_group - Consumer group name
--   p_lease_seconds  - Lease duration (0 = use queue default)
--   p_worker_id      - Unique worker/lease ID
--   p_batch_size     - Number of messages to fetch
--   p_sub_mode       - Subscription mode (all, new)
--   p_sub_from       - Subscription from timestamp
--
-- Returns: Single row if lease acquired, empty if lease already held
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_sm_lease(
    p_queue_name TEXT,
    p_partition_name TEXT,
    p_consumer_group TEXT,
    p_lease_seconds INT,
    p_worker_id TEXT,
    p_batch_size INT,
    p_sub_mode TEXT DEFAULT 'all',
    p_sub_from TEXT DEFAULT ''
) RETURNS TABLE (
    out_partition_id UUID,
    out_cursor_id UUID,
    out_cursor_ts TIMESTAMPTZ,
    out_queue_lease_time INT,
    out_window_buffer INT,
    out_delayed_processing INT
) LANGUAGE plpgsql AS $$
DECLARE
    v_partition_id UUID;
    v_queue_id UUID;
    v_lease_time INT;
    v_window_buffer INT;
    v_delayed_processing INT;
    v_effective_lease INT;
    v_now TIMESTAMPTZ := NOW();
    v_cursor_id UUID;
    v_cursor_ts TIMESTAMPTZ;
    v_lease_acquired BOOLEAN := FALSE;
    v_available_count INT;
    v_effective_batch_size INT;
BEGIN
    -- Resolve partition and queue info
    SELECT p.id, q.id, q.lease_time, q.window_buffer, q.delayed_processing
    INTO v_partition_id, v_queue_id, v_lease_time, v_window_buffer, v_delayed_processing
    FROM queen.queues q
    JOIN queen.partitions p ON p.queue_id = q.id
    WHERE q.name = p_queue_name AND p.name = p_partition_name;
    
    IF v_partition_id IS NULL THEN
        -- Partition not found
        RETURN;
    END IF;
    
    -- Calculate effective lease time
    v_effective_lease := COALESCE(NULLIF(p_lease_seconds, 0), NULLIF(v_lease_time, 0), 60);
    
    -- Ensure partition_consumers row exists
    INSERT INTO queen.partition_consumers (partition_id, consumer_group)
    VALUES (v_partition_id, p_consumer_group)
    ON CONFLICT (partition_id, consumer_group) DO NOTHING;
    
    -- Get current cursor position to count available messages
    SELECT pc.last_consumed_id, pc.last_consumed_created_at
    INTO v_cursor_id, v_cursor_ts
    FROM queen.partition_consumers pc
    WHERE pc.partition_id = v_partition_id
      AND pc.consumer_group = p_consumer_group;
    
    -- Count available messages (limited by requested batch_size for efficiency)
    SELECT COUNT(*)::INT INTO v_available_count
    FROM (
        SELECT 1 FROM queen.messages m
        WHERE m.partition_id = v_partition_id
          AND (v_cursor_ts IS NULL OR (m.created_at, m.id) > (v_cursor_ts, COALESCE(v_cursor_id, '00000000-0000-0000-0000-000000000000'::uuid)))
          AND (v_window_buffer IS NULL OR v_window_buffer = 0 
               OR m.created_at <= v_now - (v_window_buffer || ' seconds')::interval)
          AND (v_delayed_processing IS NULL OR v_delayed_processing = 0 
               OR m.created_at <= v_now - (v_delayed_processing || ' seconds')::interval)
        LIMIT p_batch_size
    ) sub;
    
    -- Use minimum of requested batch_size and available messages
    v_effective_batch_size := LEAST(p_batch_size, GREATEST(v_available_count, 1));
    
    -- Try to acquire lease (atomic check-and-update)
    UPDATE queen.partition_consumers pc
    SET 
        lease_acquired_at = v_now,
        lease_expires_at = v_now + (v_effective_lease * INTERVAL '1 second'),
        worker_id = p_worker_id,
        batch_size = v_effective_batch_size,
        acked_count = 0,
        batch_retry_count = COALESCE(pc.batch_retry_count, 0)
    WHERE pc.partition_id = v_partition_id
      AND pc.consumer_group = p_consumer_group
      AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
    RETURNING pc.last_consumed_id, pc.last_consumed_created_at
    INTO v_cursor_id, v_cursor_ts;
    
    IF NOT FOUND THEN
        -- Lease already taken
        RETURN;
    END IF;
    
    v_lease_acquired := TRUE;
    
    -- Handle subscription modes for new consumers
    IF v_cursor_ts IS NULL AND p_consumer_group != '__QUEUE_MODE__' THEN
        -- Check consumer_groups_metadata for subscription timestamp
        SELECT cgm.subscription_timestamp
        INTO v_cursor_ts
        FROM queen.consumer_groups_metadata cgm
        WHERE cgm.consumer_group = p_consumer_group
          AND cgm.queue_name = p_queue_name
          AND (cgm.partition_name = p_partition_name OR cgm.partition_name = '')
        ORDER BY CASE WHEN cgm.partition_name != '' THEN 1 ELSE 2 END
        LIMIT 1;
        
        IF v_cursor_ts IS NULL THEN
            IF p_sub_from = 'now' OR p_sub_mode = 'new' THEN
                v_cursor_ts := v_now;
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            END IF;
        ELSE
            v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
        END IF;
    END IF;
    
    RETURN QUERY SELECT 
        v_partition_id,
        v_cursor_id,
        v_cursor_ts,
        v_lease_time,
        v_window_buffer,
        v_delayed_processing;
END;
$$;

-- ============================================================================
-- 3. pop_sm_fetch: Fetch messages from partition
-- ============================================================================
-- Fetches messages from a partition starting after the cursor position.
-- Respects window_buffer and delayed_processing settings.
--
-- Parameters:
--   p_partition_id       - Partition UUID
--   p_cursor_ts          - Cursor timestamp (NULL = from beginning)
--   p_cursor_id          - Cursor message ID (NULL = from beginning)
--   p_batch_size         - Max messages to return
--   p_window_buffer      - Window buffer seconds (0 = disabled)
--   p_delayed_processing - Delayed processing seconds (0 = disabled)
--
-- Returns: JSONB array of messages
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_sm_fetch(
    p_partition_id UUID,
    p_cursor_ts TIMESTAMPTZ,
    p_cursor_id UUID,
    p_batch_size INT,
    p_window_buffer INT DEFAULT 0,
    p_delayed_processing INT DEFAULT 0
) RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    v_messages JSONB;
    v_now TIMESTAMPTZ := NOW();
    v_effective_cursor_id UUID;
BEGIN
    -- Handle NULL cursor_id (use zero UUID for comparison)
    v_effective_cursor_id := COALESCE(p_cursor_id, '00000000-0000-0000-0000-000000000000'::uuid);
    
    -- NOTE: LIMIT must be inside the subquery, not after the aggregate!
    -- If LIMIT is after jsonb_agg(), it limits output rows (always 1), not input rows.
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'id', sub.id::text,
            'transactionId', sub.transaction_id,
            'traceId', sub.trace_id::text,
            'data', sub.payload,
            'createdAt', to_char(sub.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
        ) ORDER BY sub.created_at, sub.id
    ), '[]'::jsonb)
    INTO v_messages
    FROM (
        SELECT m.id, m.transaction_id, m.trace_id, m.payload, m.created_at
        FROM queen.messages m
        WHERE m.partition_id = p_partition_id
          -- After cursor position
          AND (p_cursor_ts IS NULL OR (m.created_at, m.id) > (p_cursor_ts, v_effective_cursor_id))
          -- Respect window_buffer
          AND (p_window_buffer IS NULL OR p_window_buffer = 0 
               OR m.created_at <= v_now - (p_window_buffer || ' seconds')::interval)
          -- Respect delayed_processing
          AND (p_delayed_processing IS NULL OR p_delayed_processing = 0 
               OR m.created_at <= v_now - (p_delayed_processing || ' seconds')::interval)
        ORDER BY m.created_at, m.id
        LIMIT p_batch_size
    ) sub;
    
    RETURN v_messages;
END;
$$;

-- ============================================================================
-- 4. pop_sm_release: Release lease when no messages found
-- ============================================================================
-- Releases a lease that was acquired but found no messages.
-- Only releases if the worker_id matches (prevents releasing someone else's lease).
--
-- Parameters:
--   p_partition_id   - Partition UUID
--   p_consumer_group - Consumer group name
--   p_worker_id      - Worker ID that acquired the lease
--
-- Returns: TRUE if released, FALSE if not found/not owned
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_sm_release(
    p_partition_id UUID,
    p_consumer_group TEXT,
    p_worker_id TEXT
) RETURNS BOOLEAN LANGUAGE plpgsql AS $$
BEGIN
    UPDATE queen.partition_consumers
    SET 
        lease_expires_at = NULL, 
        worker_id = NULL,
        lease_acquired_at = NULL
    WHERE partition_id = p_partition_id
      AND consumer_group = p_consumer_group
      AND worker_id = p_worker_id;
    
    RETURN FOUND;
END;
$$;

-- ============================================================================
-- 5. pop_sm_update_cursor: Update cursor position after successful fetch
-- ============================================================================
-- Updates the cursor position after messages have been fetched.
-- This is called after pop_sm_fetch returns messages.
--
-- Parameters:
--   p_partition_id   - Partition UUID
--   p_consumer_group - Consumer group name
--   p_worker_id      - Worker ID that holds the lease
--   p_last_id        - ID of last message in batch
--   p_last_ts        - Timestamp of last message in batch
--   p_batch_size     - Actual number of messages fetched
--
-- Returns: TRUE if updated, FALSE if lease not owned
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_sm_update_cursor(
    p_partition_id UUID,
    p_consumer_group TEXT,
    p_worker_id TEXT,
    p_last_id UUID,
    p_last_ts TIMESTAMPTZ,
    p_batch_size INT
) RETURNS BOOLEAN LANGUAGE plpgsql AS $$
BEGIN
    -- Note: We don't update last_consumed_id/last_consumed_created_at here
    -- because that's done on ACK. We only update batch_size.
    UPDATE queen.partition_consumers
    SET batch_size = p_batch_size,
        acked_count = 0
    WHERE partition_id = p_partition_id
      AND consumer_group = p_consumer_group
      AND worker_id = p_worker_id;
    
    RETURN FOUND;
END;
$$;

-- ============================================================================
-- Grant Permissions
-- ============================================================================
GRANT EXECUTE ON FUNCTION queen.pop_sm_resolve(TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.pop_sm_lease(TEXT, TEXT, TEXT, INT, TEXT, INT, TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.pop_sm_fetch(UUID, TIMESTAMPTZ, UUID, INT, INT, INT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.pop_sm_release(UUID, TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.pop_sm_update_cursor(UUID, TEXT, TEXT, UUID, TIMESTAMPTZ, INT) TO PUBLIC;

