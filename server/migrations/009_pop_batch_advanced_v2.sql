-- Migration: Replace pop_messages_batch_v2 with true batched POP implementation
-- Key Improvement: Pre-allocate unique partitions, acquire all leases atomically
--
-- FIXED: Partition allocation now ensures each partition goes to AT MOST one request
-- FIXED: Lease confirmation now correctly identifies which requests got their lease

-- Drop old version
DROP FUNCTION IF EXISTS queen.pop_messages_batch_v2(jsonb);

-- ============================================================================
-- pop_messages_batch_v2: True batched POP with partition pre-allocation
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_messages_batch_v2(
    p_requests JSONB
    -- Expected format: [{idx, queue, partition, consumerGroup, batch, leaseTime, subMode, subFrom}, ...]
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_results JSONB := '[]'::jsonb;
    v_now TIMESTAMPTZ := NOW();
BEGIN
    -- ============================================================
    -- STEP 0: Create temporary tables for processing
    -- ============================================================
    
    -- Parse requests into temp table
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
    
    -- Create index for faster joins
    CREATE INDEX ON batch_requests(queue_name, consumer_group);
    CREATE INDEX ON batch_requests(idx);
    
    -- ============================================================
    -- STEP 1: Handle specific partition requests
    -- These don't participate in allocation - just validate and assign
    -- ============================================================
    
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
    
    -- ============================================================
    -- STEP 2: Allocate partitions for wildcard requests
    -- KEY FIX: Each partition can only be allocated to ONE request
    -- ============================================================
    
    WITH 
    -- Get distinct (queue, consumer_group) combinations we need to allocate
    request_groups AS (
        SELECT DISTINCT queue_name, consumer_group
        FROM batch_requests
        WHERE partition_name IS NULL
          AND allocated_partition_id IS NULL
    ),
    -- Find available partitions for each (queue, consumer_group)
    -- Rank them for fair distribution
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
            -- Rank partitions within each (queue, consumer_group)
            ROW_NUMBER() OVER (
                PARTITION BY rg.queue_name, rg.consumer_group
                ORDER BY 
                    pc.last_consumed_at ASC NULLS FIRST,  -- Fairness: least recently used
                    pl.last_message_created_at DESC        -- Then: newest messages first
            ) AS partition_rank
        FROM request_groups rg
        JOIN queen.partition_lookup pl ON pl.queue_name = rg.queue_name
        JOIN queen.partitions p ON p.id = pl.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        LEFT JOIN queen.partition_consumers pc 
            ON pc.partition_id = pl.partition_id
            AND pc.consumer_group = rg.consumer_group
        WHERE 
            -- Lease must be free or expired
            (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
            -- Has pending messages (newer than cursor)
            AND (pc.last_consumed_created_at IS NULL 
                 OR pl.last_message_created_at > pc.last_consumed_created_at
                 OR (pl.last_message_created_at = pc.last_consumed_created_at 
                     AND pl.last_message_id > pc.last_consumed_id))
    ),
    -- Rank requests within each (queue, consumer_group)
    ranked_requests AS (
        SELECT 
            br.idx,
            br.queue_name,
            br.consumer_group,
            ROW_NUMBER() OVER (
                PARTITION BY br.queue_name, br.consumer_group
                ORDER BY br.idx  -- Deterministic ordering
            ) AS request_rank
        FROM batch_requests br
        WHERE br.partition_name IS NULL
          AND br.allocated_partition_id IS NULL
    )
    -- Match requests to partitions by rank
    -- Request with rank=1 gets partition with rank=1, etc.
    -- If there are more requests than partitions, extras get nothing
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
        AND ap.partition_rank = rr.request_rank  -- KEY: rank must match!
    WHERE br.idx = rr.idx;
    
    -- ============================================================
    -- STEP 3: Generate lease IDs for all allocated requests
    -- Each request gets its OWN unique lease ID
    -- ============================================================
    
    UPDATE batch_requests
    SET lease_id = gen_random_uuid()::TEXT
    WHERE allocated_partition_id IS NOT NULL;
    
    -- ============================================================
    -- STEP 4: Acquire leases atomically
    -- ============================================================
    
    -- First ensure partition_consumers entries exist
    INSERT INTO queen.partition_consumers (partition_id, consumer_group)
    SELECT DISTINCT br.allocated_partition_id, br.consumer_group
    FROM batch_requests br
    WHERE br.allocated_partition_id IS NOT NULL
    ON CONFLICT (partition_id, consumer_group) DO NOTHING;
    
    -- Acquire leases - try to set worker_id for each allocated partition
    -- The WHERE clause ensures we only acquire if lease is free
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
    
    -- Check which requests actually got their lease
    -- (Race condition: another sidecar might have acquired it first)
    -- Clear lease_id for requests that didn't get the lease
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
    
    -- ============================================================
    -- STEP 5: Fetch messages for all acquired partitions
    -- ============================================================
    
    CREATE TEMP TABLE batch_messages ON COMMIT DROP AS
    WITH request_cursors AS (
        -- Get cursor position for each successful request
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
            pc.last_consumed_id,
            pc.last_consumed_created_at
        FROM batch_requests br
        JOIN queen.partition_consumers pc 
            ON pc.partition_id = br.allocated_partition_id
            AND pc.consumer_group = br.consumer_group
        WHERE br.lease_id IS NOT NULL
          AND pc.worker_id = br.lease_id  -- Only for requests that got the lease
    ),
    candidate_messages AS (
        -- Find candidate messages for each partition
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
            -- Messages after cursor position
            (rc.last_consumed_created_at IS NULL 
             OR m.created_at > rc.last_consumed_created_at 
             OR (m.created_at = rc.last_consumed_created_at AND m.id > rc.last_consumed_id))
            -- Apply delayed_processing filter
            AND (rc.delayed_processing IS NULL OR rc.delayed_processing = 0
                 OR m.created_at <= v_now - (rc.delayed_processing || ' seconds')::interval)
            -- Apply window_buffer filter
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
    
    -- ============================================================
    -- STEP 6: Update batch sizes and release empty leases
    -- ============================================================
    
    -- Update batch size for requests that got messages
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
    
    -- Release leases for requests that got no messages
    UPDATE queen.partition_consumers pc
    SET lease_expires_at = NULL,
        lease_acquired_at = NULL,
        worker_id = NULL
    FROM batch_requests br
    WHERE pc.partition_id = br.allocated_partition_id
      AND pc.consumer_group = br.consumer_group
      AND pc.worker_id = br.lease_id
      AND NOT EXISTS (SELECT 1 FROM batch_messages bm WHERE bm.idx = br.idx);
    
    -- Clear lease_id for requests that got no messages
    UPDATE batch_requests br
    SET lease_id = NULL
    WHERE br.allocated_partition_id IS NOT NULL
      AND br.lease_id IS NOT NULL
      AND NOT EXISTS (SELECT 1 FROM batch_messages bm WHERE bm.idx = br.idx);
    
    -- ============================================================
    -- STEP 7: Build result JSON
    -- ============================================================
    
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'idx', br.idx,
            'result', jsonb_build_object(
                'messages', COALESCE(
                    (SELECT jsonb_agg(
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
                            'consumerGroup', CASE WHEN br.consumer_group = '__QUEUE_MODE__' THEN NULL ELSE br.consumer_group END,
                            'leaseId', bm.lease_id
                        )
                        ORDER BY bm.created_at, bm.message_id
                    )
                    FROM batch_messages bm
                    WHERE bm.idx = br.idx
                    ), '[]'::jsonb
                ),
                'leaseId', br.lease_id
            )
        )
        ORDER BY br.idx
    ), '[]'::jsonb)
    INTO v_results
    FROM batch_requests br;
    
    RETURN v_results;
END;
$$;

-- Grant execute permissions
GRANT EXECUTE ON FUNCTION queen.pop_messages_batch_v2(jsonb) TO PUBLIC;

COMMENT ON FUNCTION queen.pop_messages_batch_v2 IS 
'True batched POP with partition pre-allocation for optimal concurrency.

Key features:
1. Pre-allocates unique partitions to each request (no duplicates!)
2. Each partition goes to AT MOST one request per batch
3. Acquires leases atomically with race condition handling
4. Fetches all messages in one query (I/O efficiency)
5. Returns results keyed by original index

Algorithm:
- Ranks available partitions per (queue, consumer_group)
- Ranks requests per (queue, consumer_group)
- Matches by rank: request #1 gets partition #1, request #2 gets partition #2, etc.
- If more requests than partitions, extras return empty

Parameters:
  p_requests: Array of {idx, queue, partition, consumerGroup, batch, leaseTime, subMode, subFrom}

Returns: Array of {idx, result: {messages: [...], leaseId: "..."}} sorted by idx

Performance: O(1) round-trips regardless of batch size
';
