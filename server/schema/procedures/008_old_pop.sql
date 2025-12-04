-- ============================================================================
-- pop_messages_v2: Single partition POP with two-phase locking
-- ============================================================================
-- NOTE: Using CREATE OR REPLACE (no DROP) for zero-downtime deployments

CREATE OR REPLACE FUNCTION queen.pop_messages_v2(
    p_queue_name TEXT,
    p_partition_name TEXT DEFAULT NULL,
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
    v_lease_id TEXT;
    v_messages JSONB;
    v_cursor_id UUID;
    v_cursor_ts TIMESTAMPTZ;
    v_delayed_processing INT;
    v_window_buffer INT;
    v_queue_lease_time INT;
    v_effective_lease_time INT;
    v_lease_acquired BOOLEAN := false;
    v_stored_sub_mode TEXT;
    v_stored_sub_timestamp TIMESTAMPTZ;
    v_is_new_consumer BOOLEAN := false;
BEGIN
    v_lease_id := gen_random_uuid()::text;
    
    -- PHASE 1: Find partition and get queue config
    IF p_partition_name IS NOT NULL AND p_partition_name != '' THEN
        SELECT p.id, q.id, q.name, p.name, q.delayed_processing, q.window_buffer, q.lease_time
        INTO v_partition_id, v_queue_id, v_queue_name, v_partition_name, v_delayed_processing, v_window_buffer, v_queue_lease_time
        FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name = p_queue_name AND p.name = p_partition_name;
    ELSE
        SELECT pl.partition_id, q.id, q.name, p.name, q.delayed_processing, q.window_buffer, q.lease_time
        INTO v_partition_id, v_queue_id, v_queue_name, v_partition_name, v_delayed_processing, v_window_buffer, v_queue_lease_time
        FROM queen.partition_lookup pl
        JOIN queen.partitions p ON p.id = pl.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = pl.partition_id 
            AND pc.consumer_group = p_consumer_group
        WHERE pl.queue_name = p_queue_name
          AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
          AND (pc.last_consumed_created_at IS NULL 
               OR pl.last_message_created_at > pc.last_consumed_created_at
               OR (pl.last_message_created_at = pc.last_consumed_created_at 
                   AND pl.last_message_id > pc.last_consumed_id))
        ORDER BY pc.last_consumed_at ASC NULLS FIRST, pl.last_message_created_at DESC
        LIMIT 1;
    END IF;
    
    v_effective_lease_time := COALESCE(
        NULLIF(v_queue_lease_time, 0),
        NULLIF(p_lease_time_seconds, 0),
        300
    );
    
    IF v_partition_id IS NULL THEN
        RETURN jsonb_build_object('messages', '[]'::jsonb, 'leaseId', NULL);
    END IF;
    
    -- PHASE 2: Reclaim expired leases
    UPDATE queen.partition_consumers
    SET lease_expires_at = NULL,
        lease_acquired_at = NULL,
        message_batch = NULL,
        batch_size = 0,
        acked_count = 0,
        worker_id = NULL
    WHERE lease_expires_at IS NOT NULL
      AND lease_expires_at < NOW();
    
    -- PHASE 3: Acquire partition lease
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
        RETURN jsonb_build_object('messages', '[]'::jsonb, 'leaseId', NULL);
    END IF;
    
    -- PHASE 4: Get cursor position
    SELECT last_consumed_id, last_consumed_created_at 
    INTO v_cursor_id, v_cursor_ts
    FROM queen.partition_consumers
    WHERE partition_id = v_partition_id AND consumer_group = p_consumer_group;
    
    v_is_new_consumer := (v_cursor_ts IS NULL);
    
    -- Handle subscription mode for NEW consumers
    IF v_is_new_consumer THEN
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
            
            IF v_stored_sub_timestamp IS NOT NULL THEN
                v_cursor_ts := v_stored_sub_timestamp;
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            ELSIF v_stored_sub_mode IN ('new', 'new-only') THEN
                v_cursor_ts := NOW();
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            END IF;
        END IF;
        
        IF v_cursor_ts IS NULL THEN
            IF p_subscription_from IS NOT NULL AND p_subscription_from != '' THEN
                IF p_subscription_from = 'now' THEN
                    v_cursor_ts := NOW();
                    v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                ELSE
                    BEGIN
                        v_cursor_ts := p_subscription_from::timestamptz;
                        v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                    EXCEPTION WHEN OTHERS THEN
                        NULL;
                    END;
                END IF;
            ELSIF p_subscription_mode = 'new' THEN
                v_cursor_ts := NOW();
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            END IF;
        END IF;
    END IF;
    
    -- PHASE 5: Fetch messages
    WITH candidates AS (
        SELECT m.id, m.created_at
        FROM queen.messages m
        WHERE m.partition_id = v_partition_id
          AND (v_cursor_ts IS NULL 
               OR m.created_at > v_cursor_ts 
               OR (m.created_at = v_cursor_ts AND m.id > v_cursor_id))
          AND (v_delayed_processing IS NULL OR v_delayed_processing = 0
               OR m.created_at <= NOW() - (v_delayed_processing || ' seconds')::interval)
          AND (v_window_buffer IS NULL OR v_window_buffer = 0
               OR m.created_at <= NOW() - (v_window_buffer || ' seconds')::interval)
        ORDER BY m.created_at, m.id
        LIMIT p_batch_size * 2
    ),
    locked_messages AS (
        SELECT m.id, m.transaction_id, m.payload, m.trace_id, m.created_at, m.is_encrypted
        FROM queen.messages m
        WHERE m.id IN (SELECT id FROM candidates)
        FOR UPDATE SKIP LOCKED
    ),
    ordered_messages AS (
        SELECT lm.*
        FROM locked_messages lm
        ORDER BY lm.created_at, lm.id
        LIMIT p_batch_size
    )
    -- NOTE: Using .US (microseconds) for full timestamp precision
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'id', om.id::text,
            'transactionId', om.transaction_id,
            'traceId', om.trace_id::text,
            'data', om.payload,
            'retryCount', 0,
            'priority', 0,
            'createdAt', to_char(om.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
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
    
    -- PHASE 6: Update batch size
    IF jsonb_array_length(v_messages) > 0 THEN
        UPDATE queen.partition_consumers
        SET batch_size = jsonb_array_length(v_messages),
            acked_count = 0
        WHERE partition_id = v_partition_id
          AND consumer_group = p_consumer_group
          AND worker_id = v_lease_id;
    ELSE
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

GRANT EXECUTE ON FUNCTION queen.pop_messages_v2(text, text, text, int, int, text, text) TO PUBLIC;