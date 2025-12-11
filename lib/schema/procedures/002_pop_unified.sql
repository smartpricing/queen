-- ============================================================================
-- POP Unified Procedures - Single Round-Trip Operations
-- ============================================================================
-- These procedures perform complete POP operations (lease + fetch + cleanup)
-- in a SINGLE database call, eliminating the need for C++ state machines.
--
-- Two specialized procedures handle the fundamentally different cases:
--   1. pop_specific_batch  - Known partitions (deterministic, no races)
--   2. pop_discover_batch  - Wildcard discovery (uses SKIP LOCKED for safety)
--
-- Both return the same result format for uniform C++ handling.
-- ============================================================================

-- ============================================================================
-- 1. pop_specific_batch: Complete POP for KNOWN partitions
-- ============================================================================
-- Use when: partition_name is specified in all requests
-- Guarantees: Deterministic, no race conditions, deadlock-safe via ordering
--
-- Input JSONB array:
-- [
--   {"idx": 0, "queue_name": "q1", "partition_name": "p1", "consumer_group": "cg1", 
--    "lease_seconds": 60, "worker_id": "uuid1", "batch_size": 10, "sub_mode": "all", 
--    "sub_from": "", "auto_ack": false},
--   ...
-- ]
--
-- Output JSONB array:
-- [
--   {"idx": 0, "success": true, "partition": "p1", "leaseId": "uuid1", 
--    "messages": [{"id": "...", "data": {...}, ...}]},
--   {"idx": 1, "success": false, "error": "lease_held", "messages": []},
--   {"idx": 2, "success": true, "partition": "p2", "leaseId": null,  -- auto_ack=true
--    "messages": [...]},
--   ...
-- ]
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_specific_batch(p_requests JSONB)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_now TIMESTAMPTZ := NOW();
    v_results JSONB := '[]'::jsonb;
    v_req RECORD;
    v_partition_id UUID;
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
BEGIN
    -- Process requests in partition_id order to prevent deadlocks
    -- We use a cursor over the sorted requests
    FOR v_req IN 
        SELECT 
            (r->>'idx')::INT AS idx,
            (r->>'queue_name')::TEXT AS queue_name,
            (r->>'partition_name')::TEXT AS partition_name,
            COALESCE(r->>'consumer_group', '__QUEUE_MODE__') AS consumer_group,
            COALESCE((r->>'batch_size')::INT, 10) AS batch_size,
            COALESCE((r->>'lease_seconds')::INT, 0) AS lease_seconds,
            (r->>'worker_id')::TEXT AS worker_id,
            COALESCE(r->>'sub_mode', 'all') AS sub_mode,
            COALESCE(r->>'sub_from', '') AS sub_from
        FROM jsonb_array_elements(p_requests) r
        ORDER BY r->>'queue_name', r->>'partition_name'  -- Consistent ordering
    LOOP
        -- Reset per-iteration state
        v_partition_id := NULL;
        v_cursor_id := NULL;
        v_cursor_ts := NULL;
        v_messages := '[]'::jsonb;
        
        -- =====================================================================
        -- STEP 1: Resolve partition and get queue settings
        -- =====================================================================
        SELECT p.id, q.id, q.lease_time, q.window_buffer, q.delayed_processing
        INTO v_partition_id, v_queue_id, v_lease_time, v_window_buffer, v_delayed_processing
        FROM queen.queues q
        JOIN queen.partitions p ON p.queue_id = q.id
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
        
        -- =====================================================================
        -- STEP 2: Ensure consumer row exists & try to acquire lease (atomic)
        -- =====================================================================
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
                    'error', 'lease_held',
                    'messages', '[]'::jsonb
                )
            );
            v_results := v_results || v_result;
            CONTINUE;
        END IF;
        
        -- =====================================================================
        -- STEP 3: Handle subscription modes for new consumers
        -- =====================================================================
        IF v_cursor_ts IS NULL AND v_req.consumer_group != '__QUEUE_MODE__' THEN
            -- Check consumer_groups_metadata for pre-configured timestamp
            SELECT cgm.subscription_timestamp
            INTO v_sub_ts
            FROM queen.consumer_groups_metadata cgm
            WHERE cgm.consumer_group = v_req.consumer_group
              AND cgm.queue_name = v_req.queue_name
              AND (cgm.partition_name = v_req.partition_name OR cgm.partition_name = '')
            ORDER BY CASE WHEN cgm.partition_name != '' THEN 1 ELSE 2 END
            LIMIT 1;
            
            IF v_sub_ts IS NOT NULL THEN
                -- Use pre-recorded subscription timestamp
                v_cursor_ts := v_sub_ts;
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            ELSIF v_req.sub_from != '' AND v_req.sub_from != 'now' THEN
                -- Explicit timestamp provided: parse and record it
                BEGIN
                    v_cursor_ts := v_req.sub_from::timestamptz;
                    v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                    
                    -- Insert subscription metadata with explicit timestamp
                    INSERT INTO queen.consumer_groups_metadata (
                        consumer_group, queue_name, partition_name, subscription_mode, subscription_timestamp
                    ) VALUES (
                        v_req.consumer_group, v_req.queue_name, COALESCE(v_req.partition_name, ''), 'timestamp', v_cursor_ts
                    )
                    ON CONFLICT (consumer_group, queue_name, partition_name, namespace, task) DO NOTHING;
                EXCEPTION WHEN OTHERS THEN
                    -- Invalid timestamp format - fall through to default behavior
                    NULL;
                END;
            ELSIF v_req.sub_from = 'now' OR v_req.sub_mode = 'new' THEN
                -- New subscription with "new" mode: record timestamp so all workers share it
                v_cursor_ts := v_now;
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                
                -- Insert subscription metadata (idempotent - ON CONFLICT ignores duplicates)
                INSERT INTO queen.consumer_groups_metadata (
                    consumer_group, queue_name, partition_name, subscription_mode, subscription_timestamp
                ) VALUES (
                    v_req.consumer_group, v_req.queue_name, COALESCE(v_req.partition_name, ''), 'new', v_now
                )
                ON CONFLICT (consumer_group, queue_name, partition_name, namespace, task) DO NOTHING;
            END IF;
        END IF;
        
        -- =====================================================================
        -- STEP 4: Fetch messages
        -- =====================================================================
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
              AND (v_window_buffer IS NULL OR v_window_buffer = 0 
                   OR m.created_at <= v_now - (v_window_buffer || ' seconds')::interval)
              AND (v_delayed_processing IS NULL OR v_delayed_processing = 0 
                   OR m.created_at <= v_now - (v_delayed_processing || ' seconds')::interval)
            ORDER BY m.created_at, m.id
            LIMIT v_req.batch_size
        ) sub;
        
        v_msg_count := jsonb_array_length(v_messages);
        
        -- =====================================================================
        -- STEP 5: Cleanup - release lease if empty, or update batch_size
        -- =====================================================================
        IF v_msg_count = 0 THEN
            -- No messages: release lease immediately
            UPDATE queen.partition_consumers
            SET lease_expires_at = NULL, lease_acquired_at = NULL, worker_id = NULL
            WHERE partition_id = v_partition_id
              AND consumer_group = v_req.consumer_group
              AND worker_id = v_req.worker_id;
        ELSIF v_msg_count < v_req.batch_size THEN
            -- Partial batch: update batch_size so ACKing all messages releases lease
            UPDATE queen.partition_consumers
            SET batch_size = v_msg_count, acked_count = 0
            WHERE partition_id = v_partition_id
              AND consumer_group = v_req.consumer_group
              AND worker_id = v_req.worker_id;
        END IF;
        
        -- =====================================================================
        -- STEP 6: Build result (wrapped in 'result' object for C++ compatibility)
        -- =====================================================================
        v_result := jsonb_build_object(
            'idx', v_req.idx,
            'result', jsonb_build_object(
                'success', true,
                'queue', v_req.queue_name,
                'partition', v_req.partition_name,
                'partitionId', v_partition_id::text,
                'leaseId', v_req.worker_id,
                'consumerGroup', v_req.consumer_group,
                'messages', v_messages
            )
        );
        v_results := v_results || v_result;
    END LOOP;
    
    RETURN v_results;
END;
$$;

-- ============================================================================
-- 2. pop_discover_batch: Complete POP with WILDCARD partition discovery
-- ============================================================================
-- Use when: partition_name is NULL/empty (discover available partition)
-- Guarantees: Uses FOR UPDATE SKIP LOCKED to avoid race conditions
--
-- Key difference from specific:
--   - Multiple workers calling simultaneously will each get DIFFERENT partitions
--   - SKIP LOCKED ensures no blocking on contested partitions
--   - If no available partition, returns success=false with error='no_available_partition'
--
-- Input/Output format: Same as pop_specific_batch
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_discover_batch(p_requests JSONB)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_now TIMESTAMPTZ := NOW();
    v_results JSONB := '[]'::jsonb;
    v_req RECORD;
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
    v_consumer_row_id UUID;
BEGIN
    -- Process each request - order doesn't matter for discovery since SKIP LOCKED
    FOR v_req IN 
        SELECT 
            (r->>'idx')::INT AS idx,
            (r->>'queue_name')::TEXT AS queue_name,
            COALESCE(r->>'consumer_group', '__QUEUE_MODE__') AS consumer_group,
            COALESCE((r->>'batch_size')::INT, 10) AS batch_size,
            COALESCE((r->>'lease_seconds')::INT, 0) AS lease_seconds,
            (r->>'worker_id')::TEXT AS worker_id,
            COALESCE(r->>'sub_mode', 'all') AS sub_mode,
            COALESCE(r->>'sub_from', '') AS sub_from
        FROM jsonb_array_elements(p_requests) r
        ORDER BY (r->>'idx')::INT
    LOOP
        -- Reset per-iteration state
        v_partition_id := NULL;
        v_partition_name := NULL;
        v_cursor_id := NULL;
        v_cursor_ts := NULL;
        v_messages := '[]'::jsonb;
        
        -- =====================================================================
        -- STEP 1: Discover available partition using SKIP LOCKED
        -- This is the key difference - atomically finds AND locks an available partition
        -- =====================================================================
        SELECT 
            pl.partition_id,
            p.name,
            q.id,
            q.lease_time,
            q.window_buffer,
            q.delayed_processing
        INTO v_partition_id, v_partition_name, v_queue_id, v_lease_time, v_window_buffer, v_delayed_processing
        FROM queen.partition_lookup pl
        JOIN queen.partitions p ON p.id = pl.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        LEFT JOIN queen.partition_consumers pc 
            ON pc.partition_id = pl.partition_id 
            AND pc.consumer_group = v_req.consumer_group
        WHERE pl.queue_name = v_req.queue_name
          -- Partition must be free (no lease or expired lease)
          AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= v_now)
          -- Partition must have unconsumed messages
          AND (pc.last_consumed_created_at IS NULL 
               OR pl.last_message_created_at > pc.last_consumed_created_at
               OR (pl.last_message_created_at = pc.last_consumed_created_at 
                   AND pl.last_message_id > pc.last_consumed_id))
        -- Fair distribution: prefer least-recently consumed partitions
        ORDER BY pc.last_consumed_at ASC NULLS FIRST
        LIMIT 1
        -- CRITICAL: FOR UPDATE SKIP LOCKED prevents race conditions
        -- Other concurrent workers will skip this row and get a different partition
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
        
        -- =====================================================================
        -- STEP 2: Ensure consumer row exists
        -- =====================================================================
        INSERT INTO queen.partition_consumers (partition_id, consumer_group)
        VALUES (v_partition_id, v_req.consumer_group)
        ON CONFLICT (partition_id, consumer_group) DO NOTHING;
        
        -- =====================================================================
        -- STEP 3: Acquire lease (should succeed since we have SKIP LOCKED)
        -- =====================================================================
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
            -- Edge case: lease was acquired between our check and update
            -- This shouldn't happen with proper SKIP LOCKED, but safety first
            v_result := jsonb_build_object(
                'idx', v_req.idx,
                'result', jsonb_build_object(
                    'success', false,
                    'error', 'lease_race_condition',
                    'messages', '[]'::jsonb
                )
            );
            v_results := v_results || v_result;
            CONTINUE;
        END IF;
        
        -- =====================================================================
        -- STEP 4: Handle subscription modes for new consumers
        -- =====================================================================
        IF v_cursor_ts IS NULL AND v_req.consumer_group != '__QUEUE_MODE__' THEN
            SELECT cgm.subscription_timestamp
            INTO v_sub_ts
            FROM queen.consumer_groups_metadata cgm
            WHERE cgm.consumer_group = v_req.consumer_group
              AND cgm.queue_name = v_req.queue_name
              AND (cgm.partition_name = v_partition_name OR cgm.partition_name = '')
            ORDER BY CASE WHEN cgm.partition_name != '' THEN 1 ELSE 2 END
            LIMIT 1;
            
            IF v_sub_ts IS NOT NULL THEN
                -- Use pre-recorded subscription timestamp
                v_cursor_ts := v_sub_ts;
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            ELSIF v_req.sub_from != '' AND v_req.sub_from != 'now' THEN
                -- Explicit timestamp provided: parse and record it
                BEGIN
                    v_cursor_ts := v_req.sub_from::timestamptz;
                    v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                    
                    -- Insert subscription metadata with explicit timestamp
                    INSERT INTO queen.consumer_groups_metadata (
                        consumer_group, queue_name, partition_name, subscription_mode, subscription_timestamp
                    ) VALUES (
                        v_req.consumer_group, v_req.queue_name, COALESCE(v_partition_name, ''), 'timestamp', v_cursor_ts
                    )
                    ON CONFLICT (consumer_group, queue_name, partition_name, namespace, task) DO NOTHING;
                EXCEPTION WHEN OTHERS THEN
                    -- Invalid timestamp format - fall through to default behavior
                    NULL;
                END;
            ELSIF v_req.sub_from = 'now' OR v_req.sub_mode = 'new' THEN
                -- New subscription with "new" mode: record timestamp so all workers share it
                v_cursor_ts := v_now;
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                
                -- Insert subscription metadata (idempotent - ON CONFLICT ignores duplicates)
                INSERT INTO queen.consumer_groups_metadata (
                    consumer_group, queue_name, partition_name, subscription_mode, subscription_timestamp
                ) VALUES (
                    v_req.consumer_group, v_req.queue_name, COALESCE(v_partition_name, ''), 'new', v_now
                )
                ON CONFLICT (consumer_group, queue_name, partition_name, namespace, task) DO NOTHING;
            END IF;
        END IF;
        
        -- =====================================================================
        -- STEP 5: Fetch messages
        -- =====================================================================
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
              AND (v_window_buffer IS NULL OR v_window_buffer = 0 
                   OR m.created_at <= v_now - (v_window_buffer || ' seconds')::interval)
              AND (v_delayed_processing IS NULL OR v_delayed_processing = 0 
                   OR m.created_at <= v_now - (v_delayed_processing || ' seconds')::interval)
            ORDER BY m.created_at, m.id
            LIMIT v_req.batch_size
        ) sub;
        
        v_msg_count := jsonb_array_length(v_messages);
        
        -- =====================================================================
        -- STEP 6: Cleanup
        -- =====================================================================
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
        
        -- =====================================================================
        -- STEP 7: Build result (wrapped in 'result' object for C++ compatibility)
        -- =====================================================================
        v_result := jsonb_build_object(
            'idx', v_req.idx,
            'result', jsonb_build_object(
                'success', true,
                'queue', v_req.queue_name,
                'partition', v_partition_name,
                'partitionId', v_partition_id::text,
                'leaseId', v_req.worker_id,
                'consumerGroup', v_req.consumer_group,
                'messages', v_messages
            )
        );
        v_results := v_results || v_result;
    END LOOP;
    
    RETURN v_results;
END;
$$;

-- ============================================================================
-- Grant Permissions
-- ============================================================================
GRANT EXECUTE ON FUNCTION queen.pop_specific_batch(JSONB) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.pop_discover_batch(JSONB) TO PUBLIC;

-- ============================================================================
-- 3. pop_unified_batch: Single entry point handling BOTH specific and wildcard
-- ============================================================================
-- Use when: You want a single C++ code path regardless of request type
-- Internally routes to appropriate logic based on partition_name presence
--
-- This is the RECOMMENDED procedure for simplest C++ integration.
-- It eliminates the need for any state machine - just fire and receive results.
--
-- AUTO-ACK SUPPORT (auto_ack: true):
--   When auto_ack=true, messages are automatically acknowledged in the same
--   transaction. The cursor is advanced to the last message and the lease is
--   released immediately. No separate ACK call is needed.
--   - Use case: Fire-and-forget processing, QoS 0 (at-most-once delivery)
--   - Response: leaseId will be NULL to signal no ACK is needed
-- ============================================================================

CREATE OR REPLACE FUNCTION queen.pop_unified_batch(p_requests JSONB)
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
    -- AutoAck support: advance cursor and release lease in single operation
    v_auto_ack BOOLEAN;
    v_last_msg JSONB;
    v_last_msg_id UUID;
    v_last_msg_ts TIMESTAMPTZ;
BEGIN
    -- ==========================================================================
    -- Process all requests in a single pass
    -- Order by: wildcards LAST (so specific partitions are locked first in 
    -- deterministic order, reducing contention window for wildcards)
    -- ==========================================================================
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
        -- Sort: specific partitions first (alphabetically for deadlock prevention),
        -- then wildcards (by idx to maintain request order)
        ORDER BY 
            CASE WHEN NULLIF(NULLIF(r->>'partition_name', ''), 'null') IS NULL THEN 1 ELSE 0 END,
            r->>'queue_name', 
            r->>'partition_name',
            (r->>'idx')::INT
    LOOP
        -- Reset per-iteration state
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
        
        -- =====================================================================
        -- BRANCH: Wildcard Discovery vs Specific Partition
        -- =====================================================================
        IF v_is_wildcard THEN
            -- =================================================================
            -- WILDCARD: Discover available partition with SKIP LOCKED
            -- =================================================================
            SELECT 
                pl.partition_id,
                p.name,
                q.id,
                q.lease_time,
                q.window_buffer,
                q.delayed_processing
            INTO v_partition_id, v_partition_name, v_queue_id, v_lease_time, v_window_buffer, v_delayed_processing
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
            ORDER BY pc.last_consumed_at ASC NULLS FIRST
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
            -- =================================================================
            -- SPECIFIC: Direct partition lookup
            -- =================================================================
            v_partition_name := v_req.partition_name;
            
            SELECT p.id, q.id, q.lease_time, q.window_buffer, q.delayed_processing
            INTO v_partition_id, v_queue_id, v_lease_time, v_window_buffer, v_delayed_processing
            FROM queen.queues q
            JOIN queen.partitions p ON p.queue_id = q.id
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
        END IF;
        
        -- =====================================================================
        -- COMMON PATH: Lease acquisition, fetch, cleanup
        -- =====================================================================
        
        -- Ensure consumer row exists
        INSERT INTO queen.partition_consumers (partition_id, consumer_group)
        VALUES (v_partition_id, v_req.consumer_group)
        ON CONFLICT (partition_id, consumer_group) DO NOTHING;
        
        -- Acquire lease
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
        
        -- Handle subscription modes
        IF v_cursor_ts IS NULL AND v_req.consumer_group != '__QUEUE_MODE__' THEN
            SELECT cgm.subscription_timestamp
            INTO v_sub_ts
            FROM queen.consumer_groups_metadata cgm
            WHERE cgm.consumer_group = v_req.consumer_group
              AND cgm.queue_name = v_req.queue_name
              AND (cgm.partition_name = v_partition_name OR cgm.partition_name = '')
            ORDER BY CASE WHEN cgm.partition_name != '' THEN 1 ELSE 2 END
            LIMIT 1;
            
            IF v_sub_ts IS NOT NULL THEN
                -- Use pre-recorded subscription timestamp
                v_cursor_ts := v_sub_ts;
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
            ELSIF v_req.sub_from != '' AND v_req.sub_from != 'now' THEN
                -- Explicit timestamp provided: parse and record it
                BEGIN
                    v_cursor_ts := v_req.sub_from::timestamptz;
                    v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                    
                    -- Insert subscription metadata with explicit timestamp
                    INSERT INTO queen.consumer_groups_metadata (
                        consumer_group, queue_name, partition_name, subscription_mode, subscription_timestamp
                    ) VALUES (
                        v_req.consumer_group, v_req.queue_name, COALESCE(v_partition_name, ''), 'timestamp', v_cursor_ts
                    )
                    ON CONFLICT (consumer_group, queue_name, partition_name, namespace, task) DO NOTHING;
                EXCEPTION WHEN OTHERS THEN
                    -- Invalid timestamp format - fall through to default behavior
                    NULL;
                END;
            ELSIF v_req.sub_from = 'now' OR v_req.sub_mode = 'new' THEN
                -- New subscription with "new" mode: record timestamp so all workers share it
                v_cursor_ts := v_now;
                v_cursor_id := '00000000-0000-0000-0000-000000000000'::uuid;
                
                -- Insert subscription metadata (idempotent - ON CONFLICT ignores duplicates)
                INSERT INTO queen.consumer_groups_metadata (
                    consumer_group, queue_name, partition_name, subscription_mode, subscription_timestamp
                ) VALUES (
                    v_req.consumer_group, v_req.queue_name, COALESCE(v_partition_name, ''), 'new', v_now
                )
                ON CONFLICT (consumer_group, queue_name, partition_name, namespace, task) DO NOTHING;
            END IF;
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
              AND (v_window_buffer IS NULL OR v_window_buffer = 0 
                   OR m.created_at <= v_now - (v_window_buffer || ' seconds')::interval)
              AND (v_delayed_processing IS NULL OR v_delayed_processing = 0 
                   OR m.created_at <= v_now - (v_delayed_processing || ' seconds')::interval)
            ORDER BY m.created_at, m.id
            LIMIT v_req.batch_size
        ) sub;
        
        v_msg_count := jsonb_array_length(v_messages);
        
        -- Cleanup / AutoAck handling
        IF v_auto_ack AND v_msg_count > 0 THEN
            -- =====================================================================
            -- AUTO-ACK: Advance cursor to last message AND release lease immediately
            -- This combines POP + ACK in a single atomic operation (QoS 0)
            -- Also updates consumption counters (same as explicit ACK would do)
            -- =====================================================================
            -- Extract last message ID from JSON (messages are ordered by created_at, id)
            v_last_msg := v_messages->(v_msg_count - 1);
            v_last_msg_id := (v_last_msg->>'id')::UUID;
            
            -- Get exact timestamp from database (preserves microsecond precision)
            -- This avoids precision loss from JSON string formatting
            SELECT created_at INTO v_last_msg_ts FROM queen.messages WHERE id = v_last_msg_id;
            
            -- Update cursor to last message AND release lease AND increment consumption counter
            UPDATE queen.partition_consumers
            SET 
                last_consumed_id = v_last_msg_id,
                last_consumed_created_at = v_last_msg_ts,
                last_consumed_at = v_now,
                lease_expires_at = NULL,
                lease_acquired_at = NULL,
                worker_id = NULL,
                batch_size = 0,
                acked_count = 0,
                -- INCREMENT consumption counter (same as ACK does)
                total_messages_consumed = COALESCE(total_messages_consumed, 0) + v_msg_count,
                total_batches_consumed = COALESCE(total_batches_consumed, 0) + 1
            WHERE partition_id = v_partition_id
              AND consumer_group = v_req.consumer_group
              AND worker_id = v_req.worker_id;
            
            -- Record consumption for throughput tracking (same as ACK does)
            INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, acked_at)
            VALUES (v_partition_id, v_req.consumer_group, v_msg_count, v_now);
        ELSIF v_msg_count = 0 THEN
            -- No messages: release lease immediately
            UPDATE queen.partition_consumers
            SET lease_expires_at = NULL, lease_acquired_at = NULL, worker_id = NULL
            WHERE partition_id = v_partition_id
              AND consumer_group = v_req.consumer_group
              AND worker_id = v_req.worker_id;
        ELSIF v_msg_count < v_req.batch_size THEN
            -- Partial batch: update batch_size so ACKing all messages releases lease
            UPDATE queen.partition_consumers
            SET batch_size = v_msg_count, acked_count = 0
            WHERE partition_id = v_partition_id
              AND consumer_group = v_req.consumer_group
              AND worker_id = v_req.worker_id;
        END IF;
        
        -- Build result (wrapped in 'result' object for compatibility with existing C++ code)
        -- For auto_ack: return empty string leaseId to signal no ACK is needed
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

GRANT EXECUTE ON FUNCTION queen.pop_unified_batch(JSONB) TO PUBLIC;

-- ============================================================================
-- Usage Examples
-- ============================================================================
/*
-- Example 1: Pop from known partitions (uses pop_specific_batch logic internally)
SELECT queen.pop_specific_batch('[
    {"idx": 0, "queue_name": "orders", "partition_name": "region-us", "consumer_group": "processor", "batch_size": 10, "worker_id": "worker-1"},
    {"idx": 1, "queue_name": "orders", "partition_name": "region-eu", "consumer_group": "processor", "batch_size": 10, "worker_id": "worker-2"}
]'::jsonb);

-- Example 2: Discover available partitions (uses pop_discover_batch logic internally)
SELECT queen.pop_discover_batch('[
    {"idx": 0, "queue_name": "orders", "consumer_group": "processor", "batch_size": 10, "worker_id": "worker-1"},
    {"idx": 1, "queue_name": "orders", "consumer_group": "processor", "batch_size": 10, "worker_id": "worker-2"}
]'::jsonb);
-- ^ Each will get a DIFFERENT partition due to SKIP LOCKED

-- Example 3: UNIFIED - Mix of specific and wildcard in one call
SELECT queen.pop_unified_batch('[
    {"idx": 0, "queue_name": "orders", "partition_name": "priority", "consumer_group": "processor", "batch_size": 10, "worker_id": "worker-1"},
    {"idx": 1, "queue_name": "orders", "consumer_group": "processor", "batch_size": 10, "worker_id": "worker-2"},
    {"idx": 2, "queue_name": "orders", "consumer_group": "processor", "batch_size": 10, "worker_id": "worker-3"}
]'::jsonb);
-- ^ idx=0 gets specific "priority" partition, idx=1 and idx=2 each discover different available partitions

-- Example 4: AUTO-ACK - Pop and immediately acknowledge (QoS 0, at-most-once delivery)
SELECT queen.pop_unified_batch('[
    {"idx": 0, "queue_name": "logs", "consumer_group": "analytics", "batch_size": 100, "worker_id": "worker-1", "auto_ack": true}
]'::jsonb);
-- ^ Messages are auto-acknowledged: cursor is advanced, lease is released, leaseId returned as NULL
-- ^ No separate ACK call needed - single round-trip for fire-and-forget processing
*/
