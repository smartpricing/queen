-- ============================================================================
-- Analytics Stored Procedures
-- ============================================================================
-- High-performance stored procedures for analytics/dashboard operations
-- All return JSONB for consistent handling via libqueen
-- ============================================================================

-- ============================================================================
-- queen.get_queues_v1: List all queues with message stats
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_queues_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
BEGIN
    WITH queue_message_stats AS (
        SELECT
            q.id as queue_id,
            q.name as queue_name,
            q.namespace,
            q.task,
            q.created_at as queue_created_at,
            COUNT(DISTINCT p.id) as partition_count,
            COUNT(DISTINCT m.id) as message_count,
            COUNT(DISTINCT CASE 
                WHEN dlq.message_id IS NOT NULL THEN NULL
                WHEN pc.last_consumed_created_at IS NULL 
                    OR m.created_at > pc.last_consumed_created_at
                    OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                        AND m.id > pc.last_consumed_id)
                THEN m.id 
                ELSE NULL 
            END) as unconsumed_count,
            COUNT(DISTINCT CASE 
                WHEN pc.lease_expires_at IS NOT NULL 
                    AND pc.lease_expires_at > NOW()
                    AND (
                        pc.last_consumed_created_at IS NULL 
                        OR m.created_at > pc.last_consumed_created_at
                        OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                            AND m.id > pc.last_consumed_id)
                    )
                THEN m.id 
                ELSE NULL 
            END) as processing_count
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
        LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
        GROUP BY q.id, q.name, q.namespace, q.task, q.created_at
    )
    SELECT jsonb_build_object(
        'queues', COALESCE(jsonb_agg(
            jsonb_build_object(
                'id', queue_id,
                'name', queue_name,
                'namespace', namespace,
                'task', task,
                'createdAt', to_char(queue_created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                'partitions', partition_count,
                'messages', jsonb_build_object(
                    'total', message_count,
                    'pending', GREATEST(0, unconsumed_count - processing_count),
                    'processing', processing_count
                )
            ) ORDER BY queue_created_at DESC
        ), '[]'::jsonb)
    ) INTO v_result
    FROM queue_message_stats;
    
    RETURN v_result;
END;
$$;

-- ============================================================================
-- queen.get_queue_v1: Get single queue with partition details
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_queue_v1(p_queue_name TEXT)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_queue_id UUID;
    v_queue_info JSONB;
    v_partitions JSONB;
    v_totals JSONB;
BEGIN
    -- Get queue info
    SELECT q.id, jsonb_build_object(
        'id', q.id,
        'name', q.name,
        'namespace', q.namespace,
        'task', q.task,
        'createdAt', to_char(q.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
    )
    INTO v_queue_id, v_queue_info
    FROM queen.queues q
    WHERE q.name = p_queue_name;
    
    IF v_queue_id IS NULL THEN
        RETURN jsonb_build_object('error', 'Queue not found');
    END IF;
    
    -- Get partitions with stats
    WITH partition_stats AS (
        SELECT 
            p.id,
            p.name,
            p.created_at,
            COUNT(DISTINCT m.id) as message_count,
            COALESCE(pc.pending_estimate, 0) as pending,
            CASE WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 1 ELSE 0 END as processing,
            COALESCE(pc.total_messages_consumed, 0) as completed,
            0 as failed,
            (SELECT COUNT(*) FROM queen.dead_letter_queue dlq 
             WHERE dlq.partition_id = p.id 
             AND (dlq.consumer_group IS NULL OR dlq.consumer_group = '__QUEUE_MODE__')) as dead_letter,
            MIN(m.created_at) as oldest_message,
            MAX(m.created_at) as newest_message
        FROM queen.partitions p
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
        WHERE p.queue_id = v_queue_id
        GROUP BY p.id, p.name, p.created_at, pc.pending_estimate, pc.lease_expires_at, pc.total_messages_consumed
    )
    SELECT 
        COALESCE(jsonb_agg(
            jsonb_build_object(
                'id', id,
                'name', name,
                'createdAt', to_char(created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                'stats', jsonb_build_object(
                    'total', message_count,
                    'pending', pending,
                    'processing', processing,
                    'completed', completed,
                    'failed', failed,
                    'deadLetter', dead_letter
                ),
                'oldestMessage', CASE WHEN oldest_message IS NOT NULL 
                    THEN to_char(oldest_message, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END,
                'newestMessage', CASE WHEN newest_message IS NOT NULL 
                    THEN to_char(newest_message, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END
            ) ORDER BY name
        ), '[]'::jsonb),
        jsonb_build_object(
            'total', COALESCE(SUM(message_count), 0),
            'pending', COALESCE(SUM(pending), 0),
            'processing', COALESCE(SUM(processing), 0),
            'completed', COALESCE(SUM(completed), 0),
            'failed', COALESCE(SUM(failed), 0),
            'deadLetter', COALESCE(SUM(dead_letter), 0)
        )
    INTO v_partitions, v_totals
    FROM partition_stats;
    
    RETURN v_queue_info || jsonb_build_object(
        'partitions', v_partitions,
        'totals', v_totals
    );
END;
$$;

-- ============================================================================
-- queen.get_namespaces_v1: List all namespaces with stats
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_namespaces_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
BEGIN
    WITH namespace_stats AS (
        SELECT 
            q.namespace,
            COUNT(DISTINCT q.id) as queue_count,
            COUNT(DISTINCT p.id) as partition_count,
            COUNT(DISTINCT m.id) as message_count,
            COUNT(DISTINCT CASE 
                WHEN dlq.message_id IS NOT NULL THEN NULL
                WHEN pc.last_consumed_created_at IS NULL 
                    OR m.created_at > pc.last_consumed_created_at
                    OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                        AND m.id > pc.last_consumed_id)
                THEN m.id 
                ELSE NULL 
            END) as unconsumed_count,
            COUNT(DISTINCT CASE 
                WHEN pc.lease_expires_at IS NOT NULL 
                    AND pc.lease_expires_at > NOW()
                    AND (
                        pc.last_consumed_created_at IS NULL 
                        OR m.created_at > pc.last_consumed_created_at
                        OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                            AND m.id > pc.last_consumed_id)
                    )
                THEN m.id 
                ELSE NULL 
            END) as processing_count
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
        LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
        WHERE q.namespace IS NOT NULL AND q.namespace != ''
        GROUP BY q.namespace
    )
    SELECT jsonb_build_object(
        'namespaces', COALESCE(jsonb_agg(
            jsonb_build_object(
                'namespace', namespace,
                'queues', queue_count,
                'partitions', partition_count,
                'messages', jsonb_build_object(
                    'total', message_count,
                    'pending', GREATEST(0, unconsumed_count - processing_count)
                )
            ) ORDER BY namespace
        ), '[]'::jsonb)
    ) INTO v_result
    FROM namespace_stats;
    
    RETURN v_result;
END;
$$;

-- ============================================================================
-- queen.get_tasks_v1: List all tasks with stats
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_tasks_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
BEGIN
    WITH task_stats AS (
        SELECT 
            q.task,
            COUNT(DISTINCT q.id) as queue_count,
            COUNT(DISTINCT p.id) as partition_count,
            COUNT(DISTINCT m.id) as message_count,
            COUNT(DISTINCT CASE 
                WHEN dlq.message_id IS NOT NULL THEN NULL
                WHEN pc.last_consumed_created_at IS NULL 
                    OR m.created_at > pc.last_consumed_created_at
                    OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                        AND m.id > pc.last_consumed_id)
                THEN m.id 
                ELSE NULL 
            END) as unconsumed_count,
            COUNT(DISTINCT CASE 
                WHEN pc.lease_expires_at IS NOT NULL 
                    AND pc.lease_expires_at > NOW()
                    AND (
                        pc.last_consumed_created_at IS NULL 
                        OR m.created_at > pc.last_consumed_created_at
                        OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                            AND m.id > pc.last_consumed_id)
                    )
                THEN m.id 
                ELSE NULL 
            END) as processing_count
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
        LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
        WHERE q.task IS NOT NULL AND q.task != ''
        GROUP BY q.task
    )
    SELECT jsonb_build_object(
        'tasks', COALESCE(jsonb_agg(
            jsonb_build_object(
                'task', task,
                'queues', queue_count,
                'partitions', partition_count,
                'messages', jsonb_build_object(
                    'total', message_count,
                    'pending', GREATEST(0, unconsumed_count - processing_count)
                )
            ) ORDER BY task
        ), '[]'::jsonb)
    ) INTO v_result
    FROM task_stats;
    
    RETURN v_result;
END;
$$;

-- ============================================================================
-- queen.get_system_overview_v1: Dashboard system overview
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_system_overview_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
BEGIN
    WITH message_stats AS (
        SELECT
            COUNT(DISTINCT m.id) as total_messages,
            COUNT(DISTINCT CASE 
                WHEN dlq.message_id IS NOT NULL THEN NULL
                WHEN pc.last_consumed_created_at IS NULL 
                    OR m.created_at > pc.last_consumed_created_at
                    OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                        AND m.id > pc.last_consumed_id)
                THEN m.id 
                ELSE NULL 
            END) as unconsumed_messages,
            COUNT(DISTINCT CASE 
                WHEN pc.lease_expires_at IS NOT NULL 
                    AND pc.lease_expires_at > NOW()
                    AND (
                        pc.last_consumed_created_at IS NULL 
                        OR m.created_at > pc.last_consumed_created_at
                        OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                            AND m.id > pc.last_consumed_id)
                    )
                THEN m.id 
                ELSE NULL 
            END) as processing_messages,
            COUNT(DISTINCT CASE 
                WHEN dlq.message_id IS NOT NULL THEN m.id
                ELSE NULL 
            END) as dead_letter_messages,
            COUNT(DISTINCT CASE 
                WHEN dlq.message_id IS NULL
                    AND pc.last_consumed_created_at IS NOT NULL 
                    AND (m.created_at < pc.last_consumed_created_at
                        OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                            AND m.id <= pc.last_consumed_id))
                THEN m.id
                ELSE NULL
            END) as completed_messages
        FROM queen.messages m
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = m.partition_id
        LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
    ),
    unconsumed_time_lags AS (
        SELECT
            EXTRACT(EPOCH FROM (NOW() - m.created_at))::integer as lag_seconds
        FROM queen.messages m
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = m.partition_id
        LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
        WHERE dlq.message_id IS NULL
          AND (pc.last_consumed_created_at IS NULL 
               OR m.created_at > pc.last_consumed_created_at
               OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                   AND m.id > pc.last_consumed_id))
    ),
    partition_offset_lags AS (
        SELECT
            p.id as partition_id,
            COUNT(DISTINCT m.id) as unconsumed_count
        FROM queen.partitions p
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
        LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
        WHERE dlq.message_id IS NULL
          AND (pc.last_consumed_created_at IS NULL 
               OR m.created_at > pc.last_consumed_created_at
               OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                   AND m.id > pc.last_consumed_id))
        GROUP BY p.id
        HAVING COUNT(DISTINCT m.id) > 0
    )
    SELECT jsonb_build_object(
        'queues', (SELECT COUNT(*) FROM queen.queues),
        'partitions', (SELECT COUNT(*) FROM queen.partitions),
        'namespaces', (SELECT COUNT(DISTINCT namespace) FROM queen.queues WHERE namespace IS NOT NULL),
        'tasks', (SELECT COUNT(DISTINCT task) FROM queen.queues WHERE task IS NOT NULL),
        'messages', jsonb_build_object(
            'total', ms.total_messages,
            'pending', GREATEST(0, ms.unconsumed_messages - ms.processing_messages),
            'processing', ms.processing_messages,
            'completed', ms.completed_messages,
            'failed', 0,
            'deadLetter', ms.dead_letter_messages
        ),
        'lag', jsonb_build_object(
            'time', jsonb_build_object(
                'avg', COALESCE((SELECT AVG(lag_seconds)::integer FROM unconsumed_time_lags), 0),
                'median', COALESCE((SELECT PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY lag_seconds)::integer FROM unconsumed_time_lags), 0),
                'min', COALESCE((SELECT MIN(lag_seconds)::integer FROM unconsumed_time_lags), 0),
                'max', COALESCE((SELECT MAX(lag_seconds)::integer FROM unconsumed_time_lags), 0)
            ),
            'offset', jsonb_build_object(
                'avg', COALESCE((SELECT AVG(unconsumed_count)::integer FROM partition_offset_lags), 0),
                'median', COALESCE((SELECT PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY unconsumed_count)::integer FROM partition_offset_lags), 0),
                'min', COALESCE((SELECT MIN(unconsumed_count)::integer FROM partition_offset_lags), 0),
                'max', COALESCE((SELECT MAX(unconsumed_count)::integer FROM partition_offset_lags), 0)
            )
        ),
        'timestamp', to_char(NOW(), 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
    ) INTO v_result
    FROM message_stats ms;
    
    RETURN v_result;
END;
$$;

-- ============================================================================
-- queen.delete_queue_v1: Delete queue with all related data
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.delete_queue_v1(p_queue_name TEXT)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_existed BOOLEAN;
BEGIN
    -- Delete partition consumers first
    DELETE FROM queen.partition_consumers
    WHERE partition_id IN (
        SELECT p.id FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name = p_queue_name
    );
    
    -- Delete queue (CASCADE handles partitions and messages)
    WITH deleted AS (
        DELETE FROM queen.queues WHERE name = p_queue_name
        RETURNING id
    )
    SELECT EXISTS (SELECT 1 FROM deleted) INTO v_existed;
    
    RETURN jsonb_build_object(
        'deleted', true,
        'queue', p_queue_name,
        'existed', v_existed
    );
END;
$$;

-- Grant execute permissions
GRANT EXECUTE ON FUNCTION queen.get_queues_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_queue_v1(TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_namespaces_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_tasks_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_system_overview_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.delete_queue_v1(TEXT) TO PUBLIC;

