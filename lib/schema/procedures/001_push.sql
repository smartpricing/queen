
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
    v_results jsonb;
BEGIN
    -- Create temp table from input JSON (single pass, no array_append)
    CREATE TEMP TABLE tmp_items ON COMMIT DROP AS
    SELECT 
        idx,
        COALESCE((item->>'messageId')::uuid, gen_random_uuid()) as message_id,
        COALESCE(item->>'transactionId', gen_random_uuid()::text) as transaction_id,
        item->>'queue' as queue_name,
        COALESCE(item->>'partition', 'Default') as partition_name,
        COALESCE(item->>'namespace', split_part(item->>'queue', '.', 1)) as namespace,
        COALESCE(item->>'task', 
            CASE WHEN position('.' in item->>'queue') > 0 
                 THEN split_part(item->>'queue', '.', 2) 
                 ELSE '' END) as task,
        COALESCE(item->'payload', '{}'::jsonb) as payload,
        CASE WHEN item->>'traceId' IS NOT NULL AND item->>'traceId' != '' 
             THEN (item->>'traceId')::uuid ELSE NULL END as trace_id,
        COALESCE((item->>'is_encrypted')::boolean, false) as is_encrypted
    FROM jsonb_array_elements(p_items) WITH ORDINALITY AS t(item, idx);

    -- Ensure queues exist (batch upsert)
    INSERT INTO queen.queues (name, namespace, task)
    SELECT DISTINCT queue_name, namespace, task FROM tmp_items
    ON CONFLICT (name) DO NOTHING;

    -- Ensure partitions exist (batch upsert)
    INSERT INTO queen.partitions (queue_id, name)
    SELECT DISTINCT q.id, t.partition_name
    FROM tmp_items t
    JOIN queen.queues q ON q.name = t.queue_name
    ON CONFLICT (queue_id, name) DO NOTHING;

    -- Add partition_id to temp table
    ALTER TABLE tmp_items ADD COLUMN partition_id uuid;
    UPDATE tmp_items t
    SET partition_id = p.id
    FROM queen.partitions p
    JOIN queen.queues q ON q.id = p.queue_id
    WHERE q.name = t.queue_name AND p.name = t.partition_name;

    -- Handle duplicates if requested
    IF p_check_duplicates THEN
        ALTER TABLE tmp_items ADD COLUMN is_duplicate boolean DEFAULT false;
        ALTER TABLE tmp_items ADD COLUMN existing_message_id uuid;
        
        UPDATE tmp_items t
        SET is_duplicate = true,
            existing_message_id = m.id
        FROM queen.messages m
        WHERE m.transaction_id = t.transaction_id
          AND m.partition_id = t.partition_id;
    END IF;

    -- Insert non-duplicate messages
    INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id, is_encrypted)
    SELECT message_id, transaction_id, partition_id, payload, trace_id, is_encrypted
    FROM tmp_items
    WHERE NOT COALESCE(is_duplicate, false);

    -- Build results (single query, no loops)
    SELECT COALESCE(jsonb_agg(
        CASE 
            WHEN COALESCE(t.is_duplicate, false) THEN
                jsonb_build_object(
                    'index', t.idx - 1,
                    'transaction_id', t.transaction_id,
                    'status', 'duplicate',
                    'message_id', t.existing_message_id::text
                )
            ELSE
                jsonb_build_object(
                    'index', t.idx - 1,
                    'transaction_id', t.transaction_id,
                    'status', 'queued',
                    'message_id', t.message_id::text,
                    'partition_id', t.partition_id::text,
                    'trace_id', t.trace_id::text
                )
        END
        ORDER BY t.idx
    ), '[]'::jsonb)
    INTO v_results
    FROM tmp_items t;

    RETURN v_results;
END;
$$;

GRANT EXECUTE ON FUNCTION queen.push_messages_v2(jsonb, boolean, boolean) TO PUBLIC;
