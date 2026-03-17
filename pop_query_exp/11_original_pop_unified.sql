-- Original pop_unified_batch for comparison benchmark
-- This is the version WITHOUT watermark optimization

CREATE OR REPLACE FUNCTION queen.pop_unified_batch_original(p_requests JSONB)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_now TIMESTAMPTZ := NOW();
    v_results JSONB := '[]'::jsonb;
    v_req RECORD;
    v_is_wildcard BOOLEAN;
    v_partition_id UUID;
    v_partition_name TEXT;
    v_queue_id UUID;
    v_lease_time INT;
    v_window_buffer INT;
    v_delayed_processing INT;
    v_effective_lease INT;
    v_cursor_id UUID;
    v_cursor_ts TIMESTAMPTZ;
    v_messages JSONB;
    v_msg_count INT;
    v_result JSONB;
    v_sub_ts TIMESTAMPTZ;
    v_auto_ack BOOLEAN;
    v_last_msg JSONB;
    v_last_msg_id UUID;
    v_last_msg_ts TIMESTAMPTZ;
    v_partition_last_msg_ts TIMESTAMPTZ;
    v_seeded INT;
BEGIN
    FOR v_req IN 
        SELECT 
            (r->>'idx')::INT AS idx,
            (r->>'queue_name')::TEXT AS queue_name,
            NULLIF(NULLIF(r->>'partition_name', ''), 'null') AS partition_name,
            COALESCE(r->>'consumer_group', '__QUEUE_MODE__') AS consumer_group,
            COALESCE((r->>'batch_size')::INT, 10) AS batch_size,
            COALESCE((r->>'lease_seconds')::INT, 0) AS lease_seconds,
            (r->>'worker_id')::TEXT AS worker_id,
            COALESCE(r->>'sub_mode', 'all') AS sub_mode,
            COALESCE(r->>'sub_from', '') AS sub_from,
            COALESCE((r->>'auto_ack')::BOOLEAN, false) AS auto_ack
        FROM jsonb_array_elements(p_requests) r
        ORDER BY 
            CASE WHEN NULLIF(NULLIF(r->>'partition_name', ''), 'null') IS NULL THEN 1 ELSE 0 END,
            r->>'queue_name', 
            r->>'partition_name',
            (r->>'idx')::INT
    LOOP
        v_partition_id := NULL;
        v_partition_name := NULL;
        v_cursor_id := NULL;
        v_cursor_ts := NULL;
        v_messages := '[]'::jsonb;
        v_is_wildcard := (v_req.partition_name IS NULL);
        v_auto_ack := v_req.auto_ack;
        v_last_msg := NULL;
        v_last_msg_id := NULL;
        v_last_msg_ts := NULL;
        
        IF v_is_wildcard THEN
            -- =================================================================
            -- ORIGINAL WILDCARD: No watermark optimization
            -- Uses ORDER BY pc.last_consumed_at ASC NULLS LAST
            -- =================================================================
            SELECT 
                pl.partition_id,
                p.name,
                q.id,
                q.lease_time,
                q.window_buffer,
                q.delayed_processing,
                pl.last_message_created_at
            INTO v_partition_id, v_partition_name, v_queue_id, v_lease_time, v_window_buffer, v_delayed_processing, v_partition_last_msg_ts
            FROM queen.partition_lookup pl
            JOIN queen.partitions p ON p.id = pl.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            LEFT JOIN queen.partition_consumers pc 
                ON pc.partition_id = pl.partition_id 
                AND pc.consumer_group = v_req.consumer_group
            WHERE pl.queue_name = v_req.queue_name
              AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
              AND (pc.last_consumed_created_at IS NULL 
                   OR pl.last_message_created_at > pc.last_consumed_created_at
                   OR (pl.last_message_created_at = pc.last_consumed_created_at 
                       AND pl.last_message_id > pc.last_consumed_id))
              AND (q.window_buffer IS NULL OR q.window_buffer = 0
                   OR pl.last_message_created_at <= v_now - (q.window_buffer || ' seconds')::interval)
            ORDER BY pc.last_consumed_at ASC NULLS LAST
            LIMIT 1
            FOR UPDATE OF pl SKIP LOCKED;
            
            IF v_partition_id IS NULL THEN
                v_result := jsonb_build_object(
                    'idx', v_req.idx,
                    'result', jsonb_build_object(
                        'success', false,
                        'error', 'no_available_partition',
                        'messages', '[]'::jsonb
                    )
                );
                v_results := v_results || v_result;
                CONTINUE;
            END IF;
        ELSE
            v_partition_name := v_req.partition_name;
            
            SELECT p.id, q.id, q.lease_time, q.window_buffer, q.delayed_processing, pl.last_message_created_at
            INTO v_partition_id, v_queue_id, v_lease_time, v_window_buffer, v_delayed_processing, v_partition_last_msg_ts
            FROM queen.queues q
            JOIN queen.partitions p ON p.queue_id = q.id
            LEFT JOIN queen.partition_lookup pl ON pl.partition_id = p.id
            WHERE q.name = v_req.queue_name AND p.name = v_req.partition_name;
        
            IF v_partition_id IS NULL THEN
                v_result := jsonb_build_object(
                    'idx', v_req.idx, 
                    'result', jsonb_build_object(
                        'success', false, 
                        'error', 'partition_not_found',
                        'messages', '[]'::jsonb
                    )
                );
                v_results := v_results || v_result;
                CONTINUE;
            END IF;
            
            IF v_window_buffer IS NOT NULL AND v_window_buffer > 0 
               AND v_partition_last_msg_ts IS NOT NULL
               AND v_partition_last_msg_ts > v_now - (v_window_buffer || ' seconds')::interval THEN
                v_result := jsonb_build_object(
                    'idx', v_req.idx,
                    'result', jsonb_build_object(
                        'success', true,
                        'queue', v_req.queue_name,
                        'partition', v_partition_name,
                        'partitionId', v_partition_id::text,
                        'leaseId', '',
                        'consumerGroup', v_req.consumer_group,
                        'messages', '[]'::jsonb,
                        'windowBufferActive', true
                    )
                );
                v_results := v_results || v_result;
                CONTINUE;
            END IF;
        END IF;
        
        -- Common path: lease acquisition
        INSERT INTO queen.partition_consumers (partition_id, consumer_group)
        VALUES (v_partition_id, v_req.consumer_group)
        ON CONFLICT (partition_id, consumer_group) DO NOTHING;
        
        v_effective_lease := COALESCE(NULLIF(v_req.lease_seconds, 0), NULLIF(v_lease_time, 0), 60);
        
        UPDATE queen.partition_consumers pc
        SET 
            lease_acquired_at = v_now,
            lease_expires_at = v_now + (v_effective_lease * INTERVAL '1 second'),
            worker_id = v_req.worker_id,
            batch_size = v_req.batch_size,
            acked_count = 0,
            batch_retry_count = COALESCE(pc.batch_retry_count, 0)
        WHERE pc.partition_id = v_partition_id
          AND pc.consumer_group = v_req.consumer_group
          AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
        RETURNING pc.last_consumed_id, pc.last_consumed_created_at
        INTO v_cursor_id, v_cursor_ts;
        
        IF NOT FOUND THEN
            v_result := jsonb_build_object(
                'idx', v_req.idx, 
                'result', jsonb_build_object(
                'success', false, 
                    'error', CASE WHEN v_is_wildcard THEN 'lease_race_condition' ELSE 'lease_held' END,
                    'messages', '[]'::jsonb
                )
            );
            v_results := v_results || v_result;
            CONTINUE;
        END IF;
        
        -- Fetch messages
        SELECT COALESCE(jsonb_agg(
            jsonb_build_object(
                'id', sub.id::text,
                'transactionId', sub.transaction_id,
                'traceId', sub.trace_id::text,
                'data', sub.payload,
                'createdAt', to_char(sub.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.US"Z"')
            ) ORDER BY sub.created_at, sub.id
        ), '[]'::jsonb)
        INTO v_messages
        FROM (
            SELECT m.id, m.transaction_id, m.trace_id, m.payload, m.created_at
            FROM queen.messages m
            WHERE m.partition_id = v_partition_id
              AND (v_cursor_ts IS NULL OR (m.created_at, m.id) > (v_cursor_ts, COALESCE(v_cursor_id, '00000000-0000-0000-0000-000000000000'::uuid)))
              AND (v_delayed_processing IS NULL OR v_delayed_processing = 0 
                   OR m.created_at <= v_now - (v_delayed_processing || ' seconds')::interval)
            ORDER BY m.created_at, m.id
            LIMIT v_req.batch_size
        ) sub;
        
        v_msg_count := jsonb_array_length(v_messages);
        
        -- Cleanup
        IF v_msg_count = 0 THEN
            UPDATE queen.partition_consumers
            SET lease_expires_at = NULL, lease_acquired_at = NULL, worker_id = NULL
            WHERE partition_id = v_partition_id
              AND consumer_group = v_req.consumer_group
              AND worker_id = v_req.worker_id;
        ELSIF v_msg_count < v_req.batch_size THEN
            UPDATE queen.partition_consumers
            SET batch_size = v_msg_count, acked_count = 0
            WHERE partition_id = v_partition_id
              AND consumer_group = v_req.consumer_group
              AND worker_id = v_req.worker_id;
        END IF;
        
        v_result := jsonb_build_object(
            'idx', v_req.idx,
            'result', jsonb_build_object(
                'success', true,
                'queue', v_req.queue_name,
                'partition', v_partition_name,
                'partitionId', v_partition_id::text,
                'leaseId', CASE WHEN v_auto_ack THEN '' ELSE v_req.worker_id END,
                'consumerGroup', v_req.consumer_group,
                'messages', v_messages
            )
        );
        v_results := v_results || v_result;
    END LOOP;
    
    RETURN v_results;
END;
$$;

GRANT EXECUTE ON FUNCTION queen.pop_unified_batch_original(JSONB) TO PUBLIC;
