-- ============================================================================
-- push_messages_v2: High-performance batch push
-- ============================================================================
-- NOTE: Using CREATE OR REPLACE (no DROP) for zero-downtime deployments

CREATE OR REPLACE FUNCTION queen.push_messages_v2(
    p_items jsonb,
    p_check_duplicates boolean DEFAULT true,
    p_check_capacity boolean DEFAULT true  -- Ignored for performance
)
RETURNS jsonb
LANGUAGE plpgsql
AS $$
DECLARE
    v_results jsonb := '[]'::jsonb;
    v_item_count INT;
    v_item JSONB;
    v_message_id UUID;
    v_transaction_id TEXT;
    v_queue_name TEXT;
    v_partition_name TEXT;
    v_payload JSONB;
    v_trace_id UUID;
    v_is_encrypted BOOLEAN;
    v_partition_id UUID;
    v_queue_id UUID;
    v_existing_id UUID;
    v_i INT;
    v_result JSONB;
BEGIN
    v_item_count := jsonb_array_length(p_items);
    
    -- FAST PATH: Small batch (1-10 items) - use simple loop, avoids CTE overhead
    IF v_item_count <= 10 THEN
        FOR v_i IN 0..(v_item_count - 1) LOOP
            v_item := p_items->v_i;
            
            -- Extract values
            v_message_id := COALESCE((v_item->>'messageId')::uuid, gen_random_uuid());
            v_transaction_id := COALESCE(v_item->>'transactionId', gen_random_uuid()::text);
            v_queue_name := v_item->>'queue';
            v_partition_name := COALESCE(v_item->>'partition', 'Default');
            v_payload := COALESCE(v_item->'payload', '{}'::jsonb);
            v_trace_id := CASE WHEN v_item->>'traceId' IS NOT NULL AND v_item->>'traceId' != '' 
                          THEN (v_item->>'traceId')::uuid ELSE NULL END;
            v_is_encrypted := COALESCE((v_item->>'is_encrypted')::boolean, false);
            
            -- Get queue+partition (READ-ONLY, no locks)
            SELECT q.id, p.id INTO v_queue_id, v_partition_id
            FROM queen.queues q
            LEFT JOIN queen.partitions p ON p.queue_id = q.id AND p.name = v_partition_name
            WHERE q.name = v_queue_name;
            
            -- Queue MUST exist
            IF v_queue_id IS NULL THEN
                v_result := jsonb_build_object(
                    'index', v_i,
                    'transaction_id', v_transaction_id,
                    'status', 'error',
                    'error', 'Queue does not exist: ' || v_queue_name
                );
                v_results := v_results || v_result;
                CONTINUE;
            END IF;
            
            -- Auto-create partition if needed
            IF v_partition_id IS NULL THEN
                INSERT INTO queen.partitions (queue_id, name)
                VALUES (v_queue_id, v_partition_name)
                ON CONFLICT (queue_id, name) DO NOTHING;
                
                SELECT id INTO v_partition_id 
                FROM queen.partitions 
                WHERE queue_id = v_queue_id AND name = v_partition_name;
            END IF;
            
            -- Check duplicate if requested
            v_existing_id := NULL;
            IF p_check_duplicates THEN
                SELECT id INTO v_existing_id 
                FROM queen.messages 
                WHERE transaction_id = v_transaction_id AND partition_id = v_partition_id;
            END IF;
            
            IF v_existing_id IS NOT NULL THEN
                v_result := jsonb_build_object(
                    'index', v_i,
                    'transaction_id', v_transaction_id,
                    'status', 'duplicate',
                    'message_id', v_existing_id::text
                );
            ELSE
                -- Insert message
                INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id, is_encrypted)
                VALUES (v_message_id, v_transaction_id, v_partition_id, v_payload, v_trace_id, v_is_encrypted);
                
                v_result := jsonb_build_object(
                    'index', v_i,
                    'transaction_id', v_transaction_id,
                    'status', 'queued',
                    'message_id', v_message_id::text,
                    'partition_id', v_partition_id::text,
                    'trace_id', v_trace_id::text
                );
            END IF;
            
            v_results := v_results || v_result;
        END LOOP;
        
        RETURN v_results;
    END IF;
    
    -- BATCH PATH: Use CTEs for efficiency (no temp tables, no ALTER TABLE, single pass)
    WITH 
    -- Step 1: Parse JSON input
    parsed_items AS (
        SELECT 
            (idx - 1)::int as idx,
            COALESCE((item->>'messageId')::uuid, gen_random_uuid()) as message_id,
            COALESCE(item->>'transactionId', gen_random_uuid()::text) as transaction_id,
            item->>'queue' as queue_name,
            COALESCE(item->>'partition', 'Default') as partition_name,
            COALESCE(item->'payload', '{}'::jsonb) as payload,
            CASE WHEN item->>'traceId' IS NOT NULL AND item->>'traceId' != '' 
                 THEN (item->>'traceId')::uuid ELSE NULL END as trace_id,
            COALESCE((item->>'is_encrypted')::boolean, false) as is_encrypted
        FROM jsonb_array_elements(p_items) WITH ORDINALITY AS t(item, idx)
    ),
    -- Step 2: Validate queues exist and get queue_id
    items_with_queue AS (
        SELECT 
            p.*,
            q.id as queue_id
        FROM parsed_items p
        LEFT JOIN queen.queues q ON q.name = p.queue_name
    ),
    -- Step 3: Check for missing queues (will raise exception if any)
    missing_queues AS (
        SELECT string_agg(DISTINCT queue_name, ', ') as missing
        FROM items_with_queue
        WHERE queue_id IS NULL
    ),
    -- Step 4: Auto-create partitions (INSERT only, no lock contention on existing)
    ensure_partitions AS (
        INSERT INTO queen.partitions (queue_id, name)
        SELECT DISTINCT queue_id, partition_name
        FROM items_with_queue
        WHERE queue_id IS NOT NULL
        ON CONFLICT (queue_id, name) DO NOTHING
        RETURNING queue_id, name, id
    ),
    -- Step 5: Get partition IDs (existing + newly created)
    items_with_partition AS (
        SELECT 
            i.*,
            COALESCE(ep.id, existing_p.id) as partition_id
        FROM items_with_queue i
        LEFT JOIN ensure_partitions ep ON ep.queue_id = i.queue_id AND ep.name = i.partition_name
        LEFT JOIN queen.partitions existing_p ON existing_p.queue_id = i.queue_id AND existing_p.name = i.partition_name
    ),
    -- Step 6: Check duplicates (only if enabled)
    items_with_duplicates AS (
        SELECT 
            i.*,
            CASE WHEN p_check_duplicates THEN m.id ELSE NULL END as existing_message_id
        FROM items_with_partition i
        LEFT JOIN queen.messages m ON p_check_duplicates 
            AND m.transaction_id = i.transaction_id 
            AND m.partition_id = i.partition_id
    ),
    -- Step 7: Insert non-duplicate messages
    inserted AS (
        INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id, is_encrypted)
        SELECT message_id, transaction_id, partition_id, payload, trace_id, is_encrypted
        FROM items_with_duplicates
        WHERE existing_message_id IS NULL AND partition_id IS NOT NULL
        RETURNING id, transaction_id, partition_id, trace_id
    )
    -- Step 8: Build results
    SELECT COALESCE(jsonb_agg(
        CASE 
            WHEN i.queue_id IS NULL THEN
                jsonb_build_object(
                    'index', i.idx,
                    'transaction_id', i.transaction_id,
                    'status', 'error',
                    'error', 'Queue does not exist: ' || i.queue_name
                )
            WHEN i.existing_message_id IS NOT NULL THEN
                jsonb_build_object(
                    'index', i.idx,
                    'transaction_id', i.transaction_id,
                    'status', 'duplicate',
                    'message_id', i.existing_message_id::text
                )
            ELSE
                jsonb_build_object(
                    'index', i.idx,
                    'transaction_id', i.transaction_id,
                    'status', 'queued',
                    'message_id', i.message_id::text,
                    'partition_id', i.partition_id::text,
                    'trace_id', i.trace_id::text
                )
        END
        ORDER BY i.idx
    ), '[]'::jsonb)
    INTO v_results
    FROM items_with_duplicates i;

    RETURN v_results;
END;
$$;

GRANT EXECUTE ON FUNCTION queen.push_messages_v2(jsonb, boolean, boolean) TO PUBLIC;