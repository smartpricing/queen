-- ============================================================================
-- pop_messages_batch_v2: True batched POP with partition pre-allocation
-- ============================================================================
-- NOTE: Using CREATE OR REPLACE (no DROP) for zero-downtime deployments
-- OPTIMIZED: Pre-resolve partition IDs and sort by them to prevent cross-transaction deadlocks

CREATE OR REPLACE FUNCTION queen.pop_messages_batch_v2(
    p_requests JSONB
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_results JSONB := '[]'::jsonb;
    v_now TIMESTAMPTZ := NOW();
    v_request RECORD;
    v_lease_id TEXT;
    v_cursor_id UUID;
    v_cursor_ts TIMESTAMPTZ;
    v_messages JSONB;
    v_result JSONB;
BEGIN
    -- PHASE 1: Pre-resolve all partition IDs and sort by partition_id to ensure 
    -- globally consistent lock ordering across all concurrent transactions.
    -- This prevents deadlocks when multiple batches run concurrently.
    FOR v_request IN 
        WITH parsed_requests AS (
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
        ),
        -- Resolve partition IDs for specific partition requests
        specific_partitions AS (
            SELECT 
                pr.*,
                p.id AS resolved_partition_id,
                q.id AS queue_id,
                p.name AS resolved_partition_name,
                q.lease_time AS queue_lease_time,
                q.window_buffer,
                q.delayed_processing
            FROM parsed_requests pr
            JOIN queen.queues q ON q.name = pr.queue_name
            JOIN queen.partitions p ON p.queue_id = q.id AND p.name = pr.partition_name
            WHERE pr.partition_name IS NOT NULL
        ),
        -- For wildcard requests, we need to handle them differently
        -- We'll resolve them in the main loop since they use SKIP LOCKED
        wildcard_requests AS (
            SELECT 
                pr.*,
                NULL::uuid AS resolved_partition_id,
                NULL::uuid AS queue_id,
                NULL::text AS resolved_partition_name,
                NULL::int AS queue_lease_time,
                NULL::int AS window_buffer,
                NULL::int AS delayed_processing
            FROM parsed_requests pr
            WHERE pr.partition_name IS NULL
        ),
        all_requests AS (
            SELECT * FROM specific_partitions
            UNION ALL
            SELECT * FROM wildcard_requests
        )
        -- Sort by resolved_partition_id (NULLs last for wildcard), then consumer_group
        -- This ensures globally consistent lock ordering
        SELECT * FROM all_requests
        ORDER BY resolved_partition_id NULLS LAST, consumer_group, idx
    LOOP
        v_messages := '[]'::jsonb;
        v_lease_id := NULL;
    
        -- Handle wildcard partition (needs runtime resolution)
        IF v_request.resolved_partition_id IS NULL THEN
            SELECT pl.partition_id, q.id, p.name, q.lease_time, q.window_buffer, q.delayed_processing
            INTO v_request.resolved_partition_id, v_request.queue_id, v_request.resolved_partition_name,
                 v_request.queue_lease_time, v_request.window_buffer, v_request.delayed_processing
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
        IF v_request.resolved_partition_id IS NULL THEN
            v_result := jsonb_build_object(
                'idx', v_request.idx,
                'result', jsonb_build_object('messages', '[]'::jsonb, 'leaseId', NULL)
            );
            v_results := v_results || v_result;
            CONTINUE;
        END IF;
    
        -- Generate lease ID
        v_lease_id := gen_random_uuid()::TEXT;
    
        -- Acquire advisory lock to serialize access to this (partition_id, consumer_group)
        -- Since we're iterating in partition_id order, all transactions acquire locks
        -- in the same global order, preventing deadlocks
        PERFORM pg_advisory_xact_lock(
            ('x' || substr(md5(v_request.resolved_partition_id::text || v_request.consumer_group), 1, 16))::bit(64)::bigint
        );
    
        -- Ensure partition_consumers row exists
    INSERT INTO queen.partition_consumers (partition_id, consumer_group)
        VALUES (v_request.resolved_partition_id, v_request.consumer_group)
    ON CONFLICT (partition_id, consumer_group) DO NOTHING;
    
        -- Try to acquire lease
    UPDATE queen.partition_consumers pc
    SET 
        lease_acquired_at = v_now,
        lease_expires_at = v_now + (
            COALESCE(
                    NULLIF(v_request.queue_lease_time, 0),
                    NULLIF(v_request.lease_time_seconds, 0),
                300
            ) * INTERVAL '1 second'
        ),
            worker_id = v_lease_id,
            batch_size = v_request.batch_size,
        acked_count = 0,
        batch_retry_count = COALESCE(pc.batch_retry_count, 0)
        WHERE pc.partition_id = v_request.resolved_partition_id
          AND pc.consumer_group = v_request.consumer_group
          AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now);
    
        -- Check if lease was acquired
        IF NOT FOUND THEN
            -- Lease already taken
            v_result := jsonb_build_object(
                'idx', v_request.idx,
                'result', jsonb_build_object('messages', '[]'::jsonb, 'leaseId', NULL)
            );
            v_results := v_results || v_result;
            CONTINUE;
        END IF;
        
        -- Get cursor position
        SELECT pc.last_consumed_id, pc.last_consumed_created_at
        INTO v_cursor_id, v_cursor_ts
        FROM queen.partition_consumers pc
        WHERE pc.partition_id = v_request.resolved_partition_id
          AND pc.consumer_group = v_request.consumer_group;
        
        -- Handle subscription modes for new consumers
        IF v_cursor_ts IS NULL AND v_request.consumer_group != '__QUEUE_MODE__' THEN
            SELECT cgm.subscription_timestamp
            INTO v_cursor_ts
            FROM queen.consumer_groups_metadata cgm
            WHERE cgm.consumer_group = v_request.consumer_group
              AND cgm.queue_name = v_request.queue_name
              AND (cgm.partition_name = v_request.resolved_partition_name OR cgm.partition_name = '')
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
        
        -- Fetch messages
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
                'partition', v_request.resolved_partition_name,
                'partitionId', v_request.resolved_partition_id::text,
                'consumerGroup', CASE WHEN v_request.consumer_group = '__QUEUE_MODE__' THEN NULL ELSE v_request.consumer_group END,
                'leaseId', v_lease_id
            )
                ORDER BY m.created_at, m.id
        ), '[]'::jsonb)
        INTO v_messages
        FROM (
            SELECT msg.id, msg.transaction_id, msg.trace_id, msg.payload, msg.created_at
            FROM queen.messages msg
            WHERE msg.partition_id = v_request.resolved_partition_id
              AND (v_cursor_ts IS NULL OR (msg.created_at, msg.id) > (v_cursor_ts, v_cursor_id))
              AND (v_request.delayed_processing IS NULL OR v_request.delayed_processing = 0
                   OR msg.created_at <= v_now - (v_request.delayed_processing || ' seconds')::interval)
              AND (v_request.window_buffer IS NULL OR v_request.window_buffer = 0
                   OR msg.created_at <= v_now - (v_request.window_buffer || ' seconds')::interval)
            ORDER BY msg.created_at, msg.id
            LIMIT v_request.batch_size
        ) m;
    
        -- Update batch size or release lease
        IF jsonb_array_length(v_messages) > 0 THEN
            UPDATE queen.partition_consumers
            SET batch_size = jsonb_array_length(v_messages),
        acked_count = 0
            WHERE partition_id = v_request.resolved_partition_id
              AND consumer_group = v_request.consumer_group
              AND worker_id = v_lease_id;
        ELSE
            -- No messages - release lease
            UPDATE queen.partition_consumers
    SET lease_expires_at = NULL,
        lease_acquired_at = NULL,
        worker_id = NULL
            WHERE partition_id = v_request.resolved_partition_id
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
