-- ============================================================================
-- pop_messages_batch_v2: True batched POP with partition pre-allocation
-- ============================================================================
-- NOTE: Using CREATE OR REPLACE (no DROP) for zero-downtime deployments
-- OPTIMIZED: No temp tables to avoid catalog lock contention under high concurrency

CREATE OR REPLACE FUNCTION queen.pop_messages_batch_v2(
    p_requests JSONB
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_results JSONB := '[]'::jsonb;
    v_now TIMESTAMPTZ := NOW();
    v_request RECORD;
    v_partition_id UUID;
    v_queue_id UUID;
    v_partition_name TEXT;
    v_queue_lease_time INT;
    v_window_buffer INT;
    v_delayed_processing INT;
    v_lease_id TEXT;
    v_cursor_id UUID;
    v_cursor_ts TIMESTAMPTZ;
    v_messages JSONB;
    v_result JSONB;
    v_lease_acquired BOOLEAN;
BEGIN
    -- Process each request individually to minimize lock duration
    FOR v_request IN 
        SELECT 
            (r.value->>'idx')::INT AS idx,
            (r.value->>'queue')::TEXT AS queue_name,
            NULLIF(r.value->>'partition', '') AS partition_name,
            COALESCE(r.value->>'consumerGroup', '__QUEUE_MODE__') AS consumer_group,
            COALESCE((r.value->>'batch')::INT, 10) AS batch_size,
            COALESCE((r.value->>'leaseTime')::INT, 0) AS lease_time_seconds,
            COALESCE(r.value->>'subMode', 'all') AS subscription_mode,
            NULLIF(r.value->>'subFrom', '') AS subscription_from
        FROM jsonb_array_elements(p_requests) r
    LOOP
        v_lease_acquired := FALSE;
        v_messages := '[]'::jsonb;
        v_lease_id := NULL;
        
        -- Handle specific partition request
        IF v_request.partition_name IS NOT NULL THEN
            SELECT p.id, q.id, p.name, q.lease_time, q.window_buffer, q.delayed_processing
            INTO v_partition_id, v_queue_id, v_partition_name, v_queue_lease_time, v_window_buffer, v_delayed_processing
            FROM queen.partitions p
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = v_request.queue_name
              AND p.name = v_request.partition_name;
        ELSE
            -- Wildcard partition: find available partition with SKIP LOCKED
            SELECT pl.partition_id, q.id, p.name, q.lease_time, q.window_buffer, q.delayed_processing
            INTO v_partition_id, v_queue_id, v_partition_name, v_queue_lease_time, v_window_buffer, v_delayed_processing
            FROM queen.partition_lookup pl
            JOIN queen.partitions p ON p.id = pl.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            LEFT JOIN queen.partition_consumers pc 
                ON pc.partition_id = pl.partition_id
                AND pc.consumer_group = v_request.consumer_group
            WHERE pl.queue_name = v_request.queue_name
              AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
              AND (pc.last_consumed_created_at IS NULL 
                   OR pl.last_message_created_at > pc.last_consumed_created_at
                   OR (pl.last_message_created_at = pc.last_consumed_created_at 
                       AND pl.last_message_id > pc.last_consumed_id))
            ORDER BY pc.last_consumed_at ASC NULLS FIRST, pl.last_message_created_at DESC
            LIMIT 1;
        END IF;
        
        -- Skip if no partition found
        IF v_partition_id IS NULL THEN
            v_result := jsonb_build_object(
                'idx', v_request.idx,
                'result', jsonb_build_object('messages', '[]'::jsonb, 'leaseId', NULL)
            );
            v_results := v_results || v_result;
            CONTINUE;
        END IF;
        
        -- Generate lease ID
        v_lease_id := gen_random_uuid()::TEXT;
        
        -- Try to acquire lease with SKIP LOCKED to avoid blocking
        INSERT INTO queen.partition_consumers (partition_id, consumer_group)
        VALUES (v_partition_id, v_request.consumer_group)
        ON CONFLICT (partition_id, consumer_group) DO NOTHING;
        
        -- Update lease - use FOR UPDATE SKIP LOCKED pattern
        UPDATE queen.partition_consumers pc
        SET 
            lease_acquired_at = v_now,
            lease_expires_at = v_now + (
                COALESCE(
                    NULLIF(v_queue_lease_time, 0),
                    NULLIF(v_request.lease_time_seconds, 0),
                    300
                ) * INTERVAL '1 second'
            ),
            worker_id = v_lease_id,
            batch_size = v_request.batch_size,
            acked_count = 0,
            batch_retry_count = COALESCE(pc.batch_retry_count, 0)
        WHERE pc.partition_id = v_partition_id
          AND pc.consumer_group = v_request.consumer_group
          AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
          AND pc.ctid = (
              SELECT pc2.ctid FROM queen.partition_consumers pc2
              WHERE pc2.partition_id = v_partition_id
                AND pc2.consumer_group = v_request.consumer_group
              FOR UPDATE SKIP LOCKED
          );
        
        -- Check if lease was acquired
        IF NOT FOUND THEN
            -- Lease already taken by another transaction
            v_result := jsonb_build_object(
                'idx', v_request.idx,
                'result', jsonb_build_object('messages', '[]'::jsonb, 'leaseId', NULL)
            );
            v_results := v_results || v_result;
            CONTINUE;
        END IF;
        
        v_lease_acquired := TRUE;
        
        -- Get cursor position
        SELECT pc.last_consumed_id, pc.last_consumed_created_at
        INTO v_cursor_id, v_cursor_ts
        FROM queen.partition_consumers pc
        WHERE pc.partition_id = v_partition_id
          AND pc.consumer_group = v_request.consumer_group;
        
        -- Handle subscription modes for new consumers
        IF v_cursor_ts IS NULL AND v_request.consumer_group != '__QUEUE_MODE__' THEN
            -- Check for stored subscription metadata
            SELECT cgm.subscription_timestamp
            INTO v_cursor_ts
            FROM queen.consumer_groups_metadata cgm
            WHERE cgm.consumer_group = v_request.consumer_group
              AND cgm.queue_name = v_request.queue_name
              AND (cgm.partition_name = v_partition_name OR cgm.partition_name = '')
            ORDER BY CASE WHEN cgm.partition_name != '' THEN 1 ELSE 2 END
            LIMIT 1;
            
            IF v_cursor_ts IS NULL THEN
                IF v_request.subscription_from = 'now' OR v_request.subscription_mode = 'new' THEN
                    v_cursor_ts := v_now;
                    v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                END IF;
            ELSE
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            END IF;
        END IF;
        
        -- Fetch messages using efficient index scan with LIMIT
        SELECT COALESCE(jsonb_agg(
            jsonb_build_object(
                'id', m.id::text,
                'transactionId', m.transaction_id,
                'traceId', m.trace_id::text,
                'data', m.payload,
                'retryCount', 0,
                'priority', 0,
                'createdAt', to_char(m.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                'queue', v_request.queue_name,
                'partition', v_partition_name,
                'partitionId', v_partition_id::text,
                'consumerGroup', CASE WHEN v_request.consumer_group = '__QUEUE_MODE__' THEN NULL ELSE v_request.consumer_group END,
                'leaseId', v_lease_id
            )
            ORDER BY m.created_at, m.id
        ), '[]'::jsonb)
        INTO v_messages
        FROM (
            SELECT msg.id, msg.transaction_id, msg.trace_id, msg.payload, msg.created_at
            FROM queen.messages msg
            WHERE msg.partition_id = v_partition_id
              AND (v_cursor_ts IS NULL OR (msg.created_at, msg.id) > (v_cursor_ts, v_cursor_id))
              AND (v_delayed_processing IS NULL OR v_delayed_processing = 0
                   OR msg.created_at <= v_now - (v_delayed_processing || ' seconds')::interval)
              AND (v_window_buffer IS NULL OR v_window_buffer = 0
                   OR msg.created_at <= v_now - (v_window_buffer || ' seconds')::interval)
            ORDER BY msg.created_at, msg.id
            LIMIT v_request.batch_size
        ) m;
        
        -- Update batch size based on actual messages fetched
        IF jsonb_array_length(v_messages) > 0 THEN
            UPDATE queen.partition_consumers
            SET batch_size = jsonb_array_length(v_messages),
                acked_count = 0
            WHERE partition_id = v_partition_id
              AND consumer_group = v_request.consumer_group
              AND worker_id = v_lease_id;
        ELSE
            -- No messages found - release lease
            UPDATE queen.partition_consumers
            SET lease_expires_at = NULL,
                lease_acquired_at = NULL,
                worker_id = NULL
            WHERE partition_id = v_partition_id
              AND consumer_group = v_request.consumer_group
              AND worker_id = v_lease_id;
            v_lease_id := NULL;
        END IF;
        
        -- Build result
        v_result := jsonb_build_object(
            'idx', v_request.idx,
            'result', jsonb_build_object(
                'messages', v_messages,
                'leaseId', v_lease_id
            )
        );
        v_results := v_results || v_result;
    END LOOP;
    
    RETURN v_results;
END;
$$;

GRANT EXECUTE ON FUNCTION queen.pop_messages_batch_v2(jsonb) TO PUBLIC;
