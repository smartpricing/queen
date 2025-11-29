DROP FUNCTION IF EXISTS queen.push_messages_v2(jsonb, boolean, boolean);

CREATE OR REPLACE FUNCTION queen.push_messages_v2(
    p_items jsonb,
    p_check_duplicates boolean DEFAULT true,
    p_check_capacity boolean DEFAULT true
)
RETURNS jsonb
LANGUAGE sql -- Note: LANGUAGE SQL allows the planner to optimize globally!
AS $$
WITH 
-- 1. Unpack JSON to a "Virtual Table" with ordinality (index)
raw_input AS (
    SELECT 
        idx - 1 as input_idx, -- 0-based index
        item->>'queue' as q_name,
        COALESCE(item->>'partition', 'Default') as p_name,
        COALESCE(item->>'namespace', split_part(item->>'queue', '.', 1)) as ns,
        COALESCE(item->>'task', split_part(item->>'queue', '.', 2)) as task,
        COALESCE(item->>'transactionId', gen_random_uuid()::text) as txn_id,
        (item->>'traceId')::uuid as trace_id,
        COALESCE(item->'payload', '{}'::jsonb) as payload
    FROM jsonb_array_elements(p_items) WITH ORDINALITY AS t(item, idx)
),

-- 2. Ensure Queues Exist (Bulk Insert)
unique_queues AS (
    SELECT DISTINCT q_name, ns, task FROM raw_input
),
upserted_queues AS (
    INSERT INTO queen.queues (name, namespace, task)
    SELECT q_name, ns, task FROM unique_queues
    ON CONFLICT (name) DO UPDATE SET name = EXCLUDED.name -- Dummy update for RETURNING
    RETURNING id, name, max_queue_size
),

-- 3. Ensure Partitions Exist (Bulk Insert)
unique_partitions AS (
    SELECT DISTINCT q.id as q_id, r.p_name
    FROM raw_input r
    JOIN upserted_queues q ON r.q_name = q.name
),
upserted_partitions AS (
    INSERT INTO queen.partitions (queue_id, name)
    SELECT q_id, p_name FROM unique_partitions
    ON CONFLICT (queue_id, name) DO UPDATE SET name = EXCLUDED.name
    RETURNING id, queue_id, name
),

-- 4. Calculate Capacity (Bulk Aggregate) - counts all messages in partition
queue_stats AS (
    SELECT 
        q.id as q_id,
        q.max_queue_size,
        COUNT(m.id) as current_depth
    FROM upserted_queues q
    LEFT JOIN queen.partitions p ON p.queue_id = q.id
    LEFT JOIN queen.messages m ON m.partition_id = p.id
    WHERE p_check_capacity AND q.max_queue_size > 0
    GROUP BY q.id, q.max_queue_size
),
batch_counts AS (
    SELECT q.id as q_id, COUNT(*) as batch_count
    FROM raw_input r
    JOIN upserted_queues q ON r.q_name = q.name
    GROUP BY q.id
),
over_capacity_queues AS (
    SELECT s.q_id
    FROM queue_stats s
    JOIN batch_counts b ON s.q_id = b.q_id
    WHERE (s.current_depth + b.batch_count) > s.max_queue_size
),

-- 5. Filter Valid Items (Remove capacity violations & Duplicates)
valid_items_step1 AS (
    SELECT r.*, p.id as part_id
    FROM raw_input r
    JOIN upserted_queues q ON r.q_name = q.name
    JOIN upserted_partitions p ON p.queue_id = q.id AND p.name = r.p_name
    WHERE NOT EXISTS (SELECT 1 FROM over_capacity_queues oc WHERE oc.q_id = q.id)
),
-- Filter Duplicates (if enabled)
final_items_to_insert AS (
    SELECT v.* FROM valid_items_step1 v
    WHERE NOT (
        p_check_duplicates 
        AND EXISTS (
            SELECT 1 FROM queen.messages m 
            WHERE m.transaction_id = v.txn_id AND m.partition_id = v.part_id
        )
    )
),

-- 6. Bulk Insert Messages (no status column)
inserted_msgs AS (
    INSERT INTO queen.messages (partition_id, transaction_id, trace_id, payload, is_encrypted)
    SELECT part_id, txn_id, trace_id, payload, false
    FROM final_items_to_insert
    RETURNING id, transaction_id, partition_id, trace_id
),

-- 7. Build Results
all_results AS (
    -- Successes
    SELECT 
        r.input_idx,
        r.txn_id,
        'queued' as status,
        m.id::text as message_id,
        m.partition_id::text,
        NULL as error
    FROM inserted_msgs m
    JOIN raw_input r ON m.transaction_id = r.txn_id
    
    UNION ALL
    
    -- Capacity Rejections
    SELECT 
        r.input_idx,
        r.txn_id,
        'rejected' as status,
        NULL, NULL,
        'Queue over capacity' as error
    FROM raw_input r
    JOIN upserted_queues q ON r.q_name = q.name
    JOIN over_capacity_queues oc ON oc.q_id = q.id

    UNION ALL

    -- Duplicate Rejections (Items in valid_items_step1 but NOT in final_items_to_insert)
    SELECT 
        v.input_idx,
        v.txn_id,
        'duplicate' as status,
        NULL, NULL,
        'Message already exists' as error
    FROM valid_items_step1 v
    WHERE NOT EXISTS (SELECT 1 FROM inserted_msgs m WHERE m.transaction_id = v.txn_id)
      AND NOT EXISTS (SELECT 1 FROM over_capacity_queues oc JOIN upserted_queues q ON q.id = oc.q_id WHERE q.name = v.q_name)
)

SELECT jsonb_agg(
    jsonb_build_object(
        'index', input_idx,
        'transaction_id', txn_id,
        'status', status,
        'message_id', message_id,
        'partition_id', partition_id,
        'error', error
    ) ORDER BY input_idx
) FROM all_results;
$$;
