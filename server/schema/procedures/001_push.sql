-- ============================================================================
-- push_messages_v2: High-performance batch push
-- ============================================================================
-- NOTE: Using CREATE OR REPLACE (no DROP) for zero-downtime deployments

CREATE OR REPLACE FUNCTION queen.push_messages_v2(
    p_items jsonb,
    p_check_duplicates boolean DEFAULT true,
    p_check_capacity boolean DEFAULT true
)
RETURNS jsonb
LANGUAGE plpgsql
AS $$
DECLARE
    v_item jsonb;
    v_queue_id uuid;
    v_partition_id uuid;
    v_queue_name text;
    v_partition_name text;
    v_namespace text;
    v_task text;
    v_max_queue_size int;
    v_current_depth int;
    v_batch_size int;
    v_results jsonb := '[]'::jsonb;
    v_partition_key text;
    v_partition_keys text[];
    v_partition_map jsonb := '{}'::jsonb;
    v_queue_map jsonb := '{}'::jsonb;
    v_capacity_map jsonb := '{}'::jsonb;
    v_message_ids uuid[];
    v_transaction_ids text[];
    v_partition_ids uuid[];
    v_payloads jsonb[];
    v_trace_ids uuid[];
    v_is_encrypted boolean[];
    v_item_indices int[];
    v_dup_txn_ids text[];
    v_existing_dups jsonb;
    v_txn_id text;
    v_trace_id text;
    v_payload jsonb;
    v_msg_id uuid;
    v_idx int;
    v_insert_result record;
BEGIN
    -- PHASE 1: Collect unique queues and partitions
    FOR v_idx IN 0..jsonb_array_length(p_items) - 1 LOOP
        v_item := p_items->v_idx;
        v_queue_name := v_item->>'queue';
        v_partition_name := COALESCE(v_item->>'partition', 'Default');
        v_partition_key := v_queue_name || ':' || v_partition_name;
        
        IF NOT v_partition_map ? v_partition_key THEN
            v_partition_keys := array_append(v_partition_keys, v_partition_key);
            v_partition_map := v_partition_map || jsonb_build_object(v_partition_key, null);
        END IF;
        
        IF NOT v_queue_map ? v_queue_name THEN
            v_queue_map := v_queue_map || jsonb_build_object(v_queue_name, null);
        END IF;
    END LOOP;
    
    -- PHASE 2: Ensure all queues exist
    FOR v_queue_name IN SELECT jsonb_object_keys(v_queue_map) LOOP
        SELECT p_items->idx INTO v_item
        FROM generate_series(0, jsonb_array_length(p_items) - 1) idx
        WHERE p_items->idx->>'queue' = v_queue_name
        LIMIT 1;
        
        v_namespace := COALESCE(v_item->>'namespace', split_part(v_queue_name, '.', 1));
        v_task := COALESCE(v_item->>'task', 
                          CASE WHEN position('.' in v_queue_name) > 0 
                               THEN split_part(v_queue_name, '.', 2) 
                               ELSE '' END);
        
        INSERT INTO queen.queues (name, namespace, task)
        VALUES (v_queue_name, v_namespace, v_task)
        ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name
        RETURNING id, max_queue_size INTO v_queue_id, v_max_queue_size;
        
        v_queue_map := jsonb_set(v_queue_map, ARRAY[v_queue_name], 
            jsonb_build_object('id', v_queue_id, 'max_queue_size', v_max_queue_size));
    END LOOP;
    
    -- PHASE 3: Ensure all partitions exist
    FOREACH v_partition_key IN ARRAY v_partition_keys LOOP
        v_queue_name := split_part(v_partition_key, ':', 1);
        v_partition_name := split_part(v_partition_key, ':', 2);
        v_queue_id := (v_queue_map->v_queue_name->>'id')::uuid;
        
        INSERT INTO queen.partitions (queue_id, name)
        VALUES (v_queue_id, v_partition_name)
        ON CONFLICT (queue_id, name) DO UPDATE SET name = EXCLUDED.name
        RETURNING id INTO v_partition_id;
        
        v_partition_map := jsonb_set(v_partition_map, ARRAY[v_partition_key], to_jsonb(v_partition_id::text));
    END LOOP;
    
    -- PHASE 4: Check capacity
    IF p_check_capacity THEN
        FOR v_queue_name IN SELECT jsonb_object_keys(v_queue_map) LOOP
            v_max_queue_size := (v_queue_map->v_queue_name->>'max_queue_size')::int;
            
            IF v_max_queue_size IS NOT NULL AND v_max_queue_size > 0 THEN
                v_queue_id := (v_queue_map->v_queue_name->>'id')::uuid;
                
                SELECT COUNT(*)::int INTO v_current_depth
                FROM queen.messages m
                JOIN queen.partitions p ON p.id = m.partition_id
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                    AND pc.consumer_group = '__QUEUE_MODE__'
                WHERE p.queue_id = v_queue_id
                  AND (pc.last_consumed_created_at IS NULL 
                       OR m.created_at > pc.last_consumed_created_at
                       OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id));
                
                SELECT COUNT(*)::int INTO v_batch_size
                FROM jsonb_array_elements(p_items) item
                WHERE item->>'queue' = v_queue_name;
                
                IF v_current_depth + v_batch_size > v_max_queue_size THEN
                    v_capacity_map := jsonb_set(v_capacity_map, ARRAY[v_queue_name],
                        jsonb_build_object(
                            'over_capacity', true,
                            'max_size', v_max_queue_size,
                            'current_depth', v_current_depth,
                            'batch_size', v_batch_size
                        ));
                END IF;
            END IF;
        END LOOP;
    END IF;
    
    -- PHASE 5: Build insert arrays
    v_message_ids := ARRAY[]::uuid[];
    v_transaction_ids := ARRAY[]::text[];
    v_partition_ids := ARRAY[]::uuid[];
    v_payloads := ARRAY[]::jsonb[];
    v_trace_ids := ARRAY[]::uuid[];
    v_is_encrypted := ARRAY[]::boolean[];
    v_item_indices := ARRAY[]::int[];
    
    FOR v_idx IN 0..jsonb_array_length(p_items) - 1 LOOP
        v_item := p_items->v_idx;
        v_queue_name := v_item->>'queue';
        v_partition_name := COALESCE(v_item->>'partition', 'Default');
        v_partition_key := v_queue_name || ':' || v_partition_name;
        v_txn_id := COALESCE(v_item->>'transactionId', gen_random_uuid()::text);
        v_trace_id := v_item->>'traceId';
        v_payload := COALESCE(v_item->'payload', '{}'::jsonb);
        
        IF v_capacity_map ? v_queue_name THEN
            v_results := v_results || jsonb_build_object(
                'index', v_idx,
                'transaction_id', v_txn_id,
                'status', 'rejected',
                'error', format('Queue ''%s'' would exceed max capacity (%s). Current: %s, Batch: %s',
                    v_queue_name,
                    v_capacity_map->v_queue_name->>'max_size',
                    v_capacity_map->v_queue_name->>'current_depth',
                    v_capacity_map->v_queue_name->>'batch_size')
            );
            CONTINUE;
        END IF;
        
        v_msg_id := COALESCE((v_item->>'messageId')::uuid, gen_random_uuid());
        v_message_ids := array_append(v_message_ids, v_msg_id);
        v_transaction_ids := array_append(v_transaction_ids, v_txn_id);
        v_partition_ids := array_append(v_partition_ids, (v_partition_map->>v_partition_key)::uuid);
        v_payloads := array_append(v_payloads, v_payload);
        v_trace_ids := array_append(v_trace_ids, 
            CASE WHEN v_trace_id IS NOT NULL AND v_trace_id != '' 
                 THEN v_trace_id::uuid 
                 ELSE NULL END);
        v_is_encrypted := array_append(v_is_encrypted, false);
        v_item_indices := array_append(v_item_indices, v_idx);
    END LOOP;
    
    -- PHASE 6: Check duplicates
    IF p_check_duplicates AND array_length(v_transaction_ids, 1) > 0 THEN
        SELECT jsonb_object_agg(m.transaction_id, m.id::text)
        INTO v_existing_dups
        FROM queen.messages m
        WHERE m.transaction_id = ANY(v_transaction_ids)
          AND m.partition_id = ANY(v_partition_ids);
        
        IF v_existing_dups IS NOT NULL THEN
            DECLARE
                v_new_message_ids uuid[] := ARRAY[]::uuid[];
                v_new_transaction_ids text[] := ARRAY[]::text[];
                v_new_partition_ids uuid[] := ARRAY[]::uuid[];
                v_new_payloads jsonb[] := ARRAY[]::jsonb[];
                v_new_trace_ids uuid[] := ARRAY[]::uuid[];
                v_new_is_encrypted boolean[] := ARRAY[]::boolean[];
                v_new_item_indices int[] := ARRAY[]::int[];
                v_i int;
            BEGIN
                FOR v_i IN 1..array_length(v_transaction_ids, 1) LOOP
                    IF v_existing_dups ? v_transaction_ids[v_i] THEN
                        v_results := v_results || jsonb_build_object(
                            'index', v_item_indices[v_i],
                            'transaction_id', v_transaction_ids[v_i],
                            'status', 'duplicate',
                            'message_id', v_existing_dups->>v_transaction_ids[v_i]
                        );
                    ELSE
                        v_new_message_ids := array_append(v_new_message_ids, v_message_ids[v_i]);
                        v_new_transaction_ids := array_append(v_new_transaction_ids, v_transaction_ids[v_i]);
                        v_new_partition_ids := array_append(v_new_partition_ids, v_partition_ids[v_i]);
                        v_new_payloads := array_append(v_new_payloads, v_payloads[v_i]);
                        v_new_trace_ids := array_append(v_new_trace_ids, v_trace_ids[v_i]);
                        v_new_is_encrypted := array_append(v_new_is_encrypted, v_is_encrypted[v_i]);
                        v_new_item_indices := array_append(v_new_item_indices, v_item_indices[v_i]);
                    END IF;
                END LOOP;
                
                v_message_ids := v_new_message_ids;
                v_transaction_ids := v_new_transaction_ids;
                v_partition_ids := v_new_partition_ids;
                v_payloads := v_new_payloads;
                v_trace_ids := v_new_trace_ids;
                v_is_encrypted := v_new_is_encrypted;
                v_item_indices := v_new_item_indices;
            END;
        END IF;
    END IF;
    
    -- PHASE 7: Batch INSERT
    IF array_length(v_message_ids, 1) > 0 THEN
        FOR v_insert_result IN
            INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id, is_encrypted)
            SELECT 
                unnest(v_message_ids),
                unnest(v_transaction_ids),
                unnest(v_partition_ids),
                unnest(v_payloads),
                unnest(v_trace_ids),
                unnest(v_is_encrypted)
            RETURNING id, transaction_id, partition_id, trace_id
        LOOP
            FOR v_i IN 1..array_length(v_transaction_ids, 1) LOOP
                IF v_transaction_ids[v_i] = v_insert_result.transaction_id THEN
                    v_results := v_results || jsonb_build_object(
                        'index', v_item_indices[v_i],
                        'transaction_id', v_insert_result.transaction_id,
                        'status', 'queued',
                        'message_id', v_insert_result.id::text,
                        'partition_id', v_insert_result.partition_id::text,
                        'trace_id', v_insert_result.trace_id::text
                    );
                    EXIT;
                END IF;
            END LOOP;
        END LOOP;
    END IF;
    
    SELECT COALESCE(jsonb_agg(item ORDER BY (item->>'index')::int), '[]'::jsonb)
    INTO v_results
    FROM jsonb_array_elements(v_results) item;
    
    RETURN v_results;
END;
$$;

GRANT EXECUTE ON FUNCTION queen.push_messages_v2(jsonb, boolean, boolean) TO PUBLIC;

