-- Migration: Add pop_messages_v2 and pop_messages_batch_v2 stored procedures
-- For high-performance async pop operations via sidecar pattern
--
-- Queen Architecture Notes:
-- - Messages table is immutable (no status column)
-- - Consumption is tracked via partition_consumers.last_consumed_id/created_at cursor
-- - Leases are at partition level (partition_consumers.lease_expires_at, worker_id)
-- - "Pending" = messages newer than the cursor position

-- Drop if exists (for development iteration)
DROP FUNCTION IF EXISTS queen.pop_messages_v2(text, text, text, int, int, text, text);
DROP FUNCTION IF EXISTS queen.pop_messages_batch_v2(jsonb);

-- ============================================================================
-- pop_messages_v2: Single partition POP with two-phase locking optimization
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_messages_v2(
    p_queue_name TEXT,
    p_partition_name TEXT DEFAULT NULL,  -- NULL = any partition
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INT DEFAULT 10,
    p_lease_time_seconds INT DEFAULT 300,
    p_subscription_mode TEXT DEFAULT 'all',
    p_subscription_from TEXT DEFAULT NULL
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_partition_id UUID;
    v_queue_id UUID;
    v_queue_name TEXT;
    v_partition_name TEXT;
    v_lease_id TEXT;  -- UUID stored as worker_id
    v_messages JSONB;
    v_cursor_id UUID;
    v_cursor_ts TIMESTAMPTZ;
    v_delayed_processing INT;
    v_window_buffer INT;
    v_queue_lease_time INT;  -- Queue's configured lease time
    v_effective_lease_time INT;  -- Actual lease time to use
    v_lease_acquired BOOLEAN := false;
    v_stored_sub_mode TEXT;
    v_stored_sub_timestamp TIMESTAMPTZ;
    v_is_new_consumer BOOLEAN := false;
BEGIN
    -- Generate lease ID (used as worker_id)
    v_lease_id := gen_random_uuid()::text;
    
    -- ============================================================
    -- PHASE 1: Find partition and get queue config
    -- ============================================================
    -- NOTE: Empty string is treated as NULL (for C++ compatibility)
    IF p_partition_name IS NOT NULL AND p_partition_name != '' THEN
        SELECT p.id, q.id, q.name, p.name, q.delayed_processing, q.window_buffer, q.lease_time
        INTO v_partition_id, v_queue_id, v_queue_name, v_partition_name, v_delayed_processing, v_window_buffer, v_queue_lease_time
        FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name = p_queue_name AND p.name = p_partition_name;
    ELSE
        -- Find partition with available messages using partition_lookup
        SELECT pl.partition_id, q.id, q.name, p.name, q.delayed_processing, q.window_buffer, q.lease_time
        INTO v_partition_id, v_queue_id, v_queue_name, v_partition_name, v_delayed_processing, v_window_buffer, v_queue_lease_time
        FROM queen.partition_lookup pl
        JOIN queen.partitions p ON p.id = pl.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = pl.partition_id 
            AND pc.consumer_group = p_consumer_group
        WHERE pl.queue_name = p_queue_name
          -- Lease must be free or expired
          AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
          -- Has pending messages (newer than cursor)
          AND (pc.last_consumed_created_at IS NULL 
               OR pl.last_message_created_at > pc.last_consumed_created_at
               OR (pl.last_message_created_at = pc.last_consumed_created_at 
                   AND pl.last_message_id > pc.last_consumed_id))
        ORDER BY pc.last_consumed_at ASC NULLS FIRST, pl.last_message_created_at DESC
        LIMIT 1;
    END IF;
    
    -- Determine effective lease time:
    -- 1. If queue has configured lease_time, use it
    -- 2. Else if p_lease_time_seconds > 0, use it  
    -- 3. Else use 300 seconds (5 minutes) as default
    v_effective_lease_time := COALESCE(
        NULLIF(v_queue_lease_time, 0),  -- Queue's configured lease_time (if not 0)
        NULLIF(p_lease_time_seconds, 0), -- Parameter (if not 0)
        300                              -- Default: 5 minutes
    );
    
    IF v_partition_id IS NULL THEN
        RETURN jsonb_build_object('messages', '[]'::jsonb, 'leaseId', NULL);
    END IF;
    
    -- ============================================================
    -- PHASE 2: Reclaim expired leases globally (cleanup)
    -- ============================================================
    UPDATE queen.partition_consumers
    SET lease_expires_at = NULL,
        lease_acquired_at = NULL,
        message_batch = NULL,
        batch_size = 0,
        acked_count = 0,
        worker_id = NULL
    WHERE lease_expires_at IS NOT NULL
      AND lease_expires_at < NOW();
    
    -- ============================================================
    -- PHASE 3: Acquire partition lease
    -- ============================================================
    -- Try to acquire lease (upsert into partition_consumers)
    -- Use v_effective_lease_time which respects queue's configured lease_time
    INSERT INTO queen.partition_consumers (
        partition_id, consumer_group, lease_expires_at, lease_acquired_at, worker_id
    )
    VALUES (
        v_partition_id, 
        p_consumer_group, 
        NOW() + (v_effective_lease_time || ' seconds')::interval,
        NOW(),
        v_lease_id
    )
    ON CONFLICT (partition_id, consumer_group) DO UPDATE SET
        lease_expires_at = NOW() + (v_effective_lease_time || ' seconds')::interval,
        lease_acquired_at = NOW(),
        worker_id = v_lease_id
    WHERE queen.partition_consumers.lease_expires_at IS NULL 
       OR queen.partition_consumers.lease_expires_at <= NOW()
    RETURNING worker_id = v_lease_id INTO v_lease_acquired;
    
    IF NOT COALESCE(v_lease_acquired, false) THEN
        -- Failed to acquire lease (another consumer has it)
        RETURN jsonb_build_object('messages', '[]'::jsonb, 'leaseId', NULL);
    END IF;
    
    -- ============================================================
    -- PHASE 4: Get cursor position
    -- ============================================================
    SELECT last_consumed_id, last_consumed_created_at 
    INTO v_cursor_id, v_cursor_ts
    FROM queen.partition_consumers
    WHERE partition_id = v_partition_id AND consumer_group = p_consumer_group;
    
    -- Check if this is a new consumer (no cursor yet)
    v_is_new_consumer := (v_cursor_ts IS NULL);
    
    -- Handle subscription mode for NEW consumers (first-time consumers only)
    IF v_is_new_consumer THEN
        -- First, check for stored subscription metadata for this consumer group
        -- This handles the case where the consumer first popped with subscriptionFrom('now')
        -- and now we need to honor that on subsequent partitions
        IF p_consumer_group != '__QUEUE_MODE__' THEN
            SELECT subscription_mode, subscription_timestamp
            INTO v_stored_sub_mode, v_stored_sub_timestamp
            FROM queen.consumer_groups_metadata
            WHERE consumer_group = p_consumer_group
              AND (
                  (queue_name = v_queue_name AND partition_name = v_partition_name)
                  OR (queue_name = v_queue_name AND partition_name = '')
              )
            ORDER BY 
                CASE 
                    WHEN partition_name != '' THEN 1
                    WHEN queue_name != '' THEN 2
                    ELSE 3
                END
            LIMIT 1;
            
            -- If we found stored subscription settings, use them
            IF v_stored_sub_timestamp IS NOT NULL THEN
                v_cursor_ts := v_stored_sub_timestamp;
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            ELSIF v_stored_sub_mode IN ('new', 'new-only') THEN
                -- If mode was 'new' but no timestamp stored, use current time
                v_cursor_ts := NOW();
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            END IF;
        END IF;
        
        -- If no stored metadata, check explicit parameters
        IF v_cursor_ts IS NULL THEN
            -- Check subscription_from parameter first (takes precedence)
            IF p_subscription_from IS NOT NULL AND p_subscription_from != '' THEN
                IF p_subscription_from = 'now' THEN
                    -- Start from current time (skip all historical messages)
                    v_cursor_ts := NOW();
                    v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                ELSE
                    -- Try to parse as timestamp
                    BEGIN
                        v_cursor_ts := p_subscription_from::timestamptz;
                        v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                    EXCEPTION WHEN OTHERS THEN
                        -- Invalid timestamp, ignore and use default behavior
                        NULL;
                    END;
                END IF;
            ELSIF p_subscription_mode = 'new' THEN
                -- For new mode, start from now (skip existing messages)
                v_cursor_ts := NOW();
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            END IF;
        END IF;
        -- 'all' or 'from_beginning' mode: v_cursor_ts stays NULL, so all messages are returned
    END IF;
    
    -- ============================================================
    -- PHASE 5: Fetch messages with FOR UPDATE SKIP LOCKED
    -- CRITICAL: Order must be preserved! LIMIT must come AFTER ORDER BY.
    -- ============================================================
    WITH candidates AS (
        -- Find messages newer than cursor position
        SELECT m.id, m.created_at
        FROM queen.messages m
        WHERE m.partition_id = v_partition_id
          AND (v_cursor_ts IS NULL 
               OR m.created_at > v_cursor_ts 
               OR (m.created_at = v_cursor_ts AND m.id > v_cursor_id))
          -- Apply delayed_processing filter
          AND (v_delayed_processing IS NULL OR v_delayed_processing = 0
               OR m.created_at <= NOW() - (v_delayed_processing || ' seconds')::interval)
          -- Apply window_buffer filter (messages must age before becoming available)
          AND (v_window_buffer IS NULL OR v_window_buffer = 0
               OR m.created_at <= NOW() - (v_window_buffer || ' seconds')::interval)
        ORDER BY m.created_at, m.id
        LIMIT p_batch_size * 2  -- Overfetch to handle locked rows
    ),
    locked_messages AS (
        -- Lock all candidates we can get (no LIMIT here - it breaks ordering!)
        SELECT m.id, m.transaction_id, m.payload, m.trace_id, m.created_at, m.is_encrypted
        FROM queen.messages m
        WHERE m.id IN (SELECT id FROM candidates)
        FOR UPDATE SKIP LOCKED
    ),
    -- Apply ORDER BY and LIMIT after locking to preserve message order
    ordered_messages AS (
        SELECT lm.*
        FROM locked_messages lm
        ORDER BY lm.created_at, lm.id
        LIMIT p_batch_size
    )
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'id', om.id::text,
            'transactionId', om.transaction_id,
            'traceId', om.trace_id::text,
            'data', om.payload,
            'retryCount', 0,  -- No per-message retry tracking in current schema
            'priority', 0,    -- Priority is at queue level
            'createdAt', to_char(om.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
            'queue', v_queue_name,
            'partition', v_partition_name,
            'partitionId', v_partition_id::text,
            'consumerGroup', CASE WHEN p_consumer_group = '__QUEUE_MODE__' THEN NULL ELSE p_consumer_group END,
            'leaseId', v_lease_id
        )
        ORDER BY om.created_at, om.id
    ), '[]'::jsonb)
    INTO v_messages
    FROM ordered_messages om;
    
    -- ============================================================
    -- PHASE 6: Update batch size for ACK tracking
    -- ============================================================
    IF jsonb_array_length(v_messages) > 0 THEN
        UPDATE queen.partition_consumers
        SET batch_size = jsonb_array_length(v_messages),
            acked_count = 0
        WHERE partition_id = v_partition_id
          AND consumer_group = p_consumer_group
          AND worker_id = v_lease_id;
    ELSE
        -- No messages found - release lease
        UPDATE queen.partition_consumers
        SET lease_expires_at = NULL,
            lease_acquired_at = NULL,
            worker_id = NULL
        WHERE partition_id = v_partition_id
          AND consumer_group = p_consumer_group
          AND worker_id = v_lease_id;
        
        v_lease_id := NULL;
    END IF;
    
    RETURN jsonb_build_object(
        'messages', v_messages,
        'leaseId', v_lease_id
    );
END;
$$;

-- ============================================================================
-- pop_messages_batch_v2: Batch POP for micro-batching multiple POP requests
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_messages_batch_v2(
    p_requests JSONB
    -- [{idx, queue, partition, consumerGroup, batch, leaseTime, subMode, subFrom}, ...]
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_req JSONB;
    v_results JSONB := '[]'::jsonb;
    v_pop_result JSONB;
BEGIN
    -- Sort by queue/partition to optimize I/O locality
    FOR v_req IN 
        SELECT value FROM jsonb_array_elements(p_requests)
        ORDER BY (value->>'queue'), (value->>'partition')
    LOOP
        -- Call single pop for each request
        v_pop_result := queen.pop_messages_v2(
            v_req->>'queue',
            NULLIF(v_req->>'partition', ''),
            COALESCE(v_req->>'consumerGroup', '__QUEUE_MODE__'),
            COALESCE((v_req->>'batch')::int, 10),
            COALESCE((v_req->>'leaseTime')::int, 300),
            COALESCE(v_req->>'subMode', 'all'),
            NULLIF(v_req->>'subFrom', '')
        );
        
        v_results := v_results || jsonb_build_object(
            'idx', (v_req->>'idx')::int,
            'result', v_pop_result
        );
    END LOOP;
    
    -- Sort results by original index for response routing
    SELECT COALESCE(jsonb_agg(item ORDER BY (item->>'idx')::int), '[]'::jsonb)
    INTO v_results
    FROM jsonb_array_elements(v_results) item;
    
    RETURN v_results;
END;
$$;

-- ============================================================================
-- Required Indexes for POP performance
-- ============================================================================

-- Index for message ordering within partition
CREATE INDEX IF NOT EXISTS idx_messages_partition_created 
ON queen.messages (partition_id, created_at, id);

-- Index for lease lookups by worker_id
CREATE INDEX IF NOT EXISTS idx_partition_consumers_worker 
ON queen.partition_consumers (worker_id)
WHERE worker_id IS NOT NULL;

-- Grant execute permissions
GRANT EXECUTE ON FUNCTION queen.pop_messages_v2(text, text, text, int, int, text, text) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.pop_messages_batch_v2(jsonb) TO PUBLIC;

COMMENT ON FUNCTION queen.pop_messages_v2 IS 
'High-performance single partition POP using cursor-based consumption.
Uses SKIP LOCKED with overfetch to avoid performance traps under high concurrency.

Architecture:
- Leases are at partition level (partition_consumers.worker_id)
- Consumption tracked via last_consumed_id/created_at cursor
- Messages table is immutable (no status column)

Parameters:
  p_queue_name: Queue name (required)
  p_partition_name: Specific partition or NULL for any
  p_consumer_group: Consumer group for cursor tracking
  p_batch_size: Maximum messages to return
  p_lease_time_seconds: Lease duration in seconds
  p_subscription_mode: "all" or "new" (skip existing messages)
  p_subscription_from: Timestamp for subscriptionMode=from

Returns: {messages: [...], leaseId: "..."}
';

COMMENT ON FUNCTION queen.pop_messages_batch_v2 IS 
'Batch POP for micro-batching multiple POP requests in sidecar pattern.
Executes multiple POPs in a single database call with I/O locality optimization.

Parameters:
  p_requests: Array of POP request objects

Returns: Array of {idx, result} objects sorted by original index
';
