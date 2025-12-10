-- ============================================================================
-- Traces Stored Procedures
-- ============================================================================
-- Async stored procedures for message tracing operations
-- ============================================================================

-- ============================================================================
-- queen.record_trace_v1: Record a trace event for a message
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.record_trace_v1(p_data JSONB)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_message_id UUID;
    v_trace_id UUID;
    v_transaction_id TEXT;
    v_partition_id UUID;
    v_consumer_group TEXT;
    v_event_type TEXT;
    v_trace_data JSONB;
    v_worker_id TEXT;
    v_trace_names TEXT[];
    v_name TEXT;
BEGIN
    -- Extract fields from input
    v_transaction_id := p_data->>'transactionId';
    v_partition_id := (p_data->>'partitionId')::uuid;
    v_consumer_group := COALESCE(p_data->>'consumerGroup', '__QUEUE_MODE__');
    v_event_type := COALESCE(p_data->>'eventType', 'info');
    v_trace_data := COALESCE(p_data->'data', '{}'::jsonb);
    v_worker_id := p_data->>'workerId';
    
    -- Parse traceNames array
    IF p_data->'traceNames' IS NOT NULL AND jsonb_typeof(p_data->'traceNames') = 'array' THEN
        SELECT array_agg(elem::text) INTO v_trace_names
        FROM jsonb_array_elements_text(p_data->'traceNames') elem;
    END IF;
    
    -- Get message_id from transaction_id + partition_id
    SELECT id INTO v_message_id
    FROM queen.messages 
    WHERE transaction_id = v_transaction_id AND partition_id = v_partition_id
    LIMIT 1;
    
    IF v_message_id IS NULL THEN
        RETURN jsonb_build_object(
            'success', false,
            'error', 'Message not found'
        );
    END IF;
    
    -- Insert main trace record
    INSERT INTO queen.message_traces 
        (message_id, partition_id, transaction_id, consumer_group, event_type, data, worker_id)
    VALUES 
        (v_message_id, v_partition_id, v_transaction_id, v_consumer_group, v_event_type, v_trace_data, v_worker_id)
    RETURNING id INTO v_trace_id;
    
    -- Insert trace names if any
    IF v_trace_names IS NOT NULL AND array_length(v_trace_names, 1) > 0 THEN
        FOREACH v_name IN ARRAY v_trace_names LOOP
            INSERT INTO queen.message_trace_names (trace_id, trace_name)
            VALUES (v_trace_id, v_name)
            ON CONFLICT (trace_id, trace_name) DO NOTHING;
        END LOOP;
    END IF;
    
    RETURN jsonb_build_object(
        'success', true,
        'traceId', v_trace_id
    );
END;
$$;

-- ============================================================================
-- queen.get_message_traces_v1: Get all traces for a message
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_message_traces_v1(p_partition_id UUID, p_transaction_id TEXT)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
BEGIN
    RETURN (
        SELECT COALESCE(jsonb_agg(
            jsonb_build_object(
                'id', mt.id,
                'eventType', mt.event_type,
                'data', mt.data,
                'consumerGroup', mt.consumer_group,
                'workerId', mt.worker_id,
                'createdAt', to_char(mt.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                'traceNames', COALESCE(
                    (SELECT jsonb_agg(mtn.trace_name ORDER BY mtn.trace_name) 
                     FROM queen.message_trace_names mtn WHERE mtn.trace_id = mt.id),
                    '[]'::jsonb
                )
            ) ORDER BY mt.created_at ASC
        ), '[]'::jsonb)
        FROM queen.message_traces mt
        WHERE mt.partition_id = p_partition_id AND mt.transaction_id = p_transaction_id
    );
END;
$$;

-- ============================================================================
-- queen.get_traces_by_name_v1: Get traces by trace name with message info
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_traces_by_name_v1(
    p_trace_name TEXT,
    p_limit INTEGER DEFAULT 100,
    p_offset INTEGER DEFAULT 0
)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
BEGIN
    RETURN (
        SELECT COALESCE(jsonb_agg(
            jsonb_build_object(
                'id', mt.id,
                'transactionId', mt.transaction_id,
                'partitionId', mt.partition_id,
                'eventType', mt.event_type,
                'data', mt.data,
                'consumerGroup', mt.consumer_group,
                'workerId', mt.worker_id,
                'createdAt', to_char(mt.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                'queueName', q.name,
                'partitionName', p.name,
                'messagePayload', m.payload,
                'traceNames', COALESCE(
                    (SELECT jsonb_agg(mtn2.trace_name ORDER BY mtn2.trace_name) 
                     FROM queen.message_trace_names mtn2 WHERE mtn2.trace_id = mt.id),
                    '[]'::jsonb
                )
            ) ORDER BY mt.created_at ASC
        ), '[]'::jsonb)
        FROM queen.message_trace_names mtn
        JOIN queen.message_traces mt ON mtn.trace_id = mt.id
        LEFT JOIN queen.messages m ON mt.message_id = m.id
        LEFT JOIN queen.partitions p ON mt.partition_id = p.id
        LEFT JOIN queen.queues q ON p.queue_id = q.id
        WHERE mtn.trace_name = p_trace_name
        LIMIT p_limit OFFSET p_offset
    );
END;
$$;

-- ============================================================================
-- queen.get_available_trace_names_v1: Get distinct trace names with counts
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_available_trace_names_v1(
    p_limit INTEGER DEFAULT 50,
    p_offset INTEGER DEFAULT 0
)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_names JSONB;
    v_total INTEGER;
BEGIN
    -- Get total count
    SELECT COUNT(DISTINCT trace_name) INTO v_total
    FROM queen.message_trace_names;
    
    -- Get trace names with stats
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'traceName', trace_name,
            'traceCount', trace_count,
            'messageCount', message_count,
            'lastSeen', last_seen
        ) ORDER BY last_seen DESC
    ), '[]'::jsonb) INTO v_names
    FROM (
        SELECT 
            mtn.trace_name,
            COUNT(DISTINCT mtn.trace_id) as trace_count,
            COUNT(DISTINCT mt.transaction_id) as message_count,
            to_char(MAX(mt.created_at), 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') as last_seen
        FROM queen.message_trace_names mtn
        JOIN queen.message_traces mt ON mtn.trace_id = mt.id
        GROUP BY mtn.trace_name
        ORDER BY MAX(mt.created_at) DESC
        LIMIT p_limit OFFSET p_offset
    ) subq;
    
    RETURN jsonb_build_object(
        'traceNames', v_names,
        'total', v_total,
        'pagination', jsonb_build_object(
            'limit', p_limit,
            'offset', p_offset
        )
    );
END;
$$;

-- Grant execute permissions
GRANT EXECUTE ON FUNCTION queen.record_trace_v1(JSONB) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_message_traces_v1(UUID, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_traces_by_name_v1(TEXT, INTEGER, INTEGER) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_available_trace_names_v1(INTEGER, INTEGER) TO PUBLIC;

