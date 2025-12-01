-- ============================================================================
-- pop_messages_batch_v2: True batched POP with partition pre-allocation
-- ============================================================================
-- NOTE: Using CREATE OR REPLACE (no DROP) for zero-downtime deployments

CREATE OR REPLACE FUNCTION queen.pop_messages_batch_v2(
    p_requests JSONB
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_results JSONB := '[]'::jsonb;
    v_now TIMESTAMPTZ := NOW();
    v_request_count INT;
BEGIN
    -- Check request count for optimization
    v_request_count := jsonb_array_length(p_requests);
    
    -- FAST PATH: Single request - delegate to simpler procedure (avoids temp table overhead)
    IF v_request_count = 1 THEN
        DECLARE
            v_req JSONB := p_requests->0;
            v_single_result JSONB;
        BEGIN
            SELECT queen.pop_messages_v2(
                v_req->>'queue',
                NULLIF(v_req->>'partition', ''),
                COALESCE(v_req->>'consumerGroup', '__QUEUE_MODE__'),
                COALESCE((v_req->>'batch')::INT, 10),
                COALESCE((v_req->>'leaseTime')::INT, 0),
                COALESCE(v_req->>'subMode', 'all'),
                NULLIF(v_req->>'subFrom', '')
            ) INTO v_single_result;
            
            RETURN jsonb_build_array(
                jsonb_build_object(
                    'idx', (v_req->>'idx')::INT,
                    'result', v_single_result
                )
            );
        END;
    END IF;
    
    -- BATCH PATH: Multiple requests - use temp tables for efficiency
    -- STEP 0: Create temporary tables
    CREATE TEMP TABLE batch_requests ON COMMIT DROP AS
    SELECT 
        (r.value->>'idx')::INT AS idx,
        (r.value->>'queue')::TEXT AS queue_name,
        NULLIF(r.value->>'partition', '') AS partition_name,
        COALESCE(r.value->>'consumerGroup', '__QUEUE_MODE__') AS consumer_group,
        COALESCE((r.value->>'batch')::INT, 10) AS batch_size,
        COALESCE((r.value->>'leaseTime')::INT, 0) AS lease_time_seconds,
        COALESCE(r.value->>'subMode', 'all') AS subscription_mode,
        NULLIF(r.value->>'subFrom', '') AS subscription_from,
        NULL::UUID AS allocated_partition_id,
        NULL::UUID AS queue_id,
        NULL::TEXT AS allocated_partition_name,
        NULL::TEXT AS lease_id,
        0 AS request_rank,
        NULL::INT AS queue_lease_time,
        NULL::INT AS window_buffer,
        NULL::INT AS delayed_processing
    FROM jsonb_array_elements(p_requests) r;
    
    -- Only create indexes for larger batches (overhead not worth it for small batches)
    IF v_request_count > 5 THEN
        CREATE INDEX ON batch_requests(queue_name, consumer_group);
        CREATE INDEX ON batch_requests(idx);
    END IF;
    
    -- STEP 1: Handle specific partition requests
    UPDATE batch_requests br
    SET allocated_partition_id = p.id,
        queue_id = q.id,
        allocated_partition_name = p.name,
        queue_lease_time = q.lease_time,
        window_buffer = q.window_buffer,
        delayed_processing = q.delayed_processing
    FROM queen.partitions p
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE br.partition_name IS NOT NULL
      AND q.name = br.queue_name
      AND p.name = br.partition_name;
    
    -- STEP 2: Allocate partitions for wildcard requests
    WITH 
    request_groups AS (
        SELECT DISTINCT queue_name, consumer_group
        FROM batch_requests
        WHERE partition_name IS NULL
          AND allocated_partition_id IS NULL
    ),
    available_partitions AS (
        SELECT 
            pl.partition_id,
            rg.queue_name,
            rg.consumer_group,
            p.name AS partition_name,
            q.id AS queue_id,
            q.lease_time AS queue_lease_time,
            q.window_buffer,
            q.delayed_processing,
            ROW_NUMBER() OVER (
                PARTITION BY rg.queue_name, rg.consumer_group
                ORDER BY 
                    pc.last_consumed_at ASC NULLS FIRST,
                    pl.last_message_created_at DESC
            ) AS partition_rank
        FROM request_groups rg
        JOIN queen.partition_lookup pl ON pl.queue_name = rg.queue_name
        JOIN queen.partitions p ON p.id = pl.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        LEFT JOIN queen.partition_consumers pc 
            ON pc.partition_id = pl.partition_id
            AND pc.consumer_group = rg.consumer_group
        WHERE 
            (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
            AND (pc.last_consumed_created_at IS NULL 
                 OR pl.last_message_created_at > pc.last_consumed_created_at
                 OR (pl.last_message_created_at = pc.last_consumed_created_at 
                     AND pl.last_message_id > pc.last_consumed_id))
    ),
    ranked_requests AS (
        SELECT 
            br.idx,
            br.queue_name,
            br.consumer_group,
            ROW_NUMBER() OVER (
                PARTITION BY br.queue_name, br.consumer_group
                ORDER BY br.idx
            ) AS request_rank
        FROM batch_requests br
        WHERE br.partition_name IS NULL
          AND br.allocated_partition_id IS NULL
    )
    UPDATE batch_requests br
    SET allocated_partition_id = ap.partition_id,
        queue_id = ap.queue_id,
        allocated_partition_name = ap.partition_name,
        queue_lease_time = ap.queue_lease_time,
        window_buffer = ap.window_buffer,
        delayed_processing = ap.delayed_processing,
        request_rank = rr.request_rank
    FROM ranked_requests rr
    JOIN available_partitions ap 
        ON ap.queue_name = rr.queue_name
        AND ap.consumer_group = rr.consumer_group
        AND ap.partition_rank = rr.request_rank
    WHERE br.idx = rr.idx;
    
    -- STEP 3: Generate lease IDs
    UPDATE batch_requests
    SET lease_id = gen_random_uuid()::TEXT
    WHERE allocated_partition_id IS NOT NULL;
    
    -- STEP 4: Acquire leases atomically
    INSERT INTO queen.partition_consumers (partition_id, consumer_group)
    SELECT DISTINCT br.allocated_partition_id, br.consumer_group
    FROM batch_requests br
    WHERE br.allocated_partition_id IS NOT NULL
    ON CONFLICT (partition_id, consumer_group) DO NOTHING;
    
    UPDATE queen.partition_consumers pc
    SET 
        lease_acquired_at = v_now,
        lease_expires_at = v_now + (
            COALESCE(
                NULLIF(br.queue_lease_time, 0),
                NULLIF(br.lease_time_seconds, 0),
                300
            ) * INTERVAL '1 second'
        ),
        worker_id = br.lease_id,
        batch_size = br.batch_size,
        acked_count = 0,
        batch_retry_count = COALESCE(pc.batch_retry_count, 0)
    FROM batch_requests br
    WHERE pc.partition_id = br.allocated_partition_id
      AND pc.consumer_group = br.consumer_group
      AND br.lease_id IS NOT NULL
      AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now);
    
    UPDATE batch_requests br
    SET lease_id = NULL,
        allocated_partition_id = NULL
    WHERE br.allocated_partition_id IS NOT NULL
      AND br.lease_id IS NOT NULL
      AND NOT EXISTS (
          SELECT 1 FROM queen.partition_consumers pc
          WHERE pc.partition_id = br.allocated_partition_id
            AND pc.consumer_group = br.consumer_group
            AND pc.worker_id = br.lease_id
      );
    
    -- STEP 5: Fetch messages with subscription handling
    CREATE TEMP TABLE batch_messages ON COMMIT DROP AS
    WITH raw_cursors AS (
        SELECT 
            br.idx,
            br.allocated_partition_id,
            br.consumer_group,
            br.batch_size,
            br.window_buffer,
            br.delayed_processing,
            br.allocated_partition_name,
            br.queue_name,
            br.lease_id,
            br.subscription_mode,
            br.subscription_from,
            pc.last_consumed_id,
            pc.last_consumed_created_at,
            (pc.last_consumed_created_at IS NULL) AS is_new_consumer
        FROM batch_requests br
        JOIN queen.partition_consumers pc 
            ON pc.partition_id = br.allocated_partition_id
            AND pc.consumer_group = br.consumer_group
        WHERE br.lease_id IS NOT NULL
          AND pc.worker_id = br.lease_id
    ),
    subscription_metadata AS (
        SELECT DISTINCT ON (rc.idx)
            rc.idx,
            cgm.subscription_mode AS stored_mode,
            cgm.subscription_timestamp AS stored_timestamp
        FROM raw_cursors rc
        JOIN queen.consumer_groups_metadata cgm
            ON cgm.consumer_group = rc.consumer_group
            AND cgm.queue_name = rc.queue_name
            AND (cgm.partition_name = rc.allocated_partition_name OR cgm.partition_name = '')
        WHERE rc.is_new_consumer
          AND rc.consumer_group != '__QUEUE_MODE__'
        ORDER BY rc.idx, 
            CASE WHEN cgm.partition_name != '' THEN 1 ELSE 2 END
    ),
    request_cursors AS (
        SELECT 
            rc.idx,
            rc.allocated_partition_id,
            rc.consumer_group,
            rc.batch_size,
            rc.window_buffer,
            rc.delayed_processing,
            rc.allocated_partition_name,
            rc.queue_name,
            rc.lease_id,
            CASE 
                WHEN NOT rc.is_new_consumer THEN rc.last_consumed_id
                WHEN sm.stored_timestamp IS NOT NULL THEN '00000000-0000-0000-0000-000000000000'::uuid
                WHEN sm.stored_mode IN ('new', 'new-only') THEN '00000000-0000-0000-0000-000000000000'::uuid
                WHEN rc.subscription_from = 'now' THEN '00000000-0000-0000-0000-000000000000'::uuid
                WHEN rc.subscription_mode = 'new' THEN '00000000-0000-0000-0000-000000000000'::uuid
                ELSE rc.last_consumed_id
            END AS effective_cursor_id,
            CASE 
                WHEN NOT rc.is_new_consumer THEN rc.last_consumed_created_at
                WHEN sm.stored_timestamp IS NOT NULL THEN sm.stored_timestamp
                WHEN sm.stored_mode IN ('new', 'new-only') THEN v_now
                WHEN rc.subscription_from = 'now' THEN v_now
                WHEN rc.subscription_mode = 'new' THEN v_now
                ELSE rc.last_consumed_created_at
            END AS effective_cursor_ts
        FROM raw_cursors rc
        LEFT JOIN subscription_metadata sm ON sm.idx = rc.idx
    ),
    candidate_messages AS (
        SELECT 
            rc.idx,
            rc.allocated_partition_id,
            rc.consumer_group,
            rc.batch_size,
            rc.allocated_partition_name,
            rc.queue_name,
            rc.lease_id,
            m.id AS message_id,
            m.transaction_id,
            m.payload,
            m.trace_id,
            m.created_at,
            m.is_encrypted,
            ROW_NUMBER() OVER (
                PARTITION BY rc.idx 
                ORDER BY m.created_at, m.id
            ) AS msg_rank
        FROM request_cursors rc
        JOIN queen.messages m ON m.partition_id = rc.allocated_partition_id
        WHERE 
            (rc.effective_cursor_ts IS NULL 
             OR m.created_at > rc.effective_cursor_ts 
             OR (m.created_at = rc.effective_cursor_ts AND m.id > rc.effective_cursor_id))
            AND (rc.delayed_processing IS NULL OR rc.delayed_processing = 0
                 OR m.created_at <= v_now - (rc.delayed_processing || ' seconds')::interval)
            AND (rc.window_buffer IS NULL OR rc.window_buffer = 0
                 OR m.created_at <= v_now - (rc.window_buffer || ' seconds')::interval)
    )
    SELECT 
        cm.idx,
        cm.allocated_partition_id,
        cm.consumer_group,
        cm.allocated_partition_name,
        cm.queue_name,
        cm.lease_id,
        cm.message_id,
        cm.transaction_id,
        cm.payload,
        cm.trace_id,
        cm.created_at,
        cm.is_encrypted
    FROM candidate_messages cm
    WHERE cm.msg_rank <= cm.batch_size;
    
    -- STEP 6: Update batch sizes and release empty leases
    WITH message_counts AS (
        SELECT idx, COUNT(*) AS msg_count
        FROM batch_messages
        GROUP BY idx
    )
    UPDATE queen.partition_consumers pc
    SET batch_size = mc.msg_count,
        acked_count = 0
    FROM batch_requests br
    JOIN message_counts mc ON mc.idx = br.idx
    WHERE pc.partition_id = br.allocated_partition_id
      AND pc.consumer_group = br.consumer_group
      AND pc.worker_id = br.lease_id;
    
    UPDATE queen.partition_consumers pc
    SET lease_expires_at = NULL,
        lease_acquired_at = NULL,
        worker_id = NULL
    FROM batch_requests br
    WHERE pc.partition_id = br.allocated_partition_id
      AND pc.consumer_group = br.consumer_group
      AND pc.worker_id = br.lease_id
      AND NOT EXISTS (SELECT 1 FROM batch_messages bm WHERE bm.idx = br.idx);
    
    UPDATE batch_requests br
    SET lease_id = NULL
    WHERE br.allocated_partition_id IS NOT NULL
      AND br.lease_id IS NOT NULL
      AND NOT EXISTS (SELECT 1 FROM batch_messages bm WHERE bm.idx = br.idx);
    
    -- STEP 7: Build result JSON (pre-aggregate to avoid O(n²) correlated subquery)
    WITH messages_by_idx AS (
        SELECT 
            bm.idx,
            bm.consumer_group,
            jsonb_agg(
                        jsonb_build_object(
                            'id', bm.message_id::text,
                            'transactionId', bm.transaction_id,
                            'traceId', bm.trace_id::text,
                            'data', bm.payload,
                            'retryCount', 0,
                            'priority', 0,
                            'createdAt', to_char(bm.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                            'queue', bm.queue_name,
                            'partition', bm.allocated_partition_name,
                            'partitionId', bm.allocated_partition_id::text,
                    'consumerGroup', CASE WHEN bm.consumer_group = '__QUEUE_MODE__' THEN NULL ELSE bm.consumer_group END,
                            'leaseId', bm.lease_id
                        )
                        ORDER BY bm.created_at, bm.message_id
            ) AS messages
                    FROM batch_messages bm
        GROUP BY bm.idx, bm.consumer_group
    )
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'idx', br.idx,
            'result', jsonb_build_object(
                'messages', COALESCE(mbi.messages, '[]'::jsonb),
                'leaseId', br.lease_id
            )
        )
        ORDER BY br.idx
    ), '[]'::jsonb)
    INTO v_results
    FROM batch_requests br
    LEFT JOIN messages_by_idx mbi ON mbi.idx = br.idx;
    
    RETURN v_results;
END;
$$;

GRANT EXECUTE ON FUNCTION queen.pop_messages_batch_v2(jsonb) TO PUBLIC;

