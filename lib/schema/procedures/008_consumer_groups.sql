-- ============================================================================
-- Consumer Group Stored Procedures
-- ============================================================================
-- Async stored procedures for consumer group management and analytics
-- ============================================================================

-- ============================================================================
-- queen.get_consumer_groups_v1: List all consumer groups with stats
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_consumer_groups_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
BEGIN
    WITH consumer_data AS (
        SELECT 
            pc.consumer_group,
            q.name as queue_name,
            p.name as partition_name,
            pc.worker_id,
            pc.last_consumed_at,
            pc.last_consumed_id,
            pc.last_consumed_created_at,
            pc.total_messages_consumed,
            pc.total_batches_consumed,
            pc.lease_expires_at,
            pc.lease_acquired_at,
            -- Calculate lag: count messages after last consumed
            (
                SELECT COUNT(*)
                FROM queen.messages m
                WHERE m.partition_id = pc.partition_id
                  AND (
                      pc.last_consumed_created_at IS NULL
                      OR m.created_at > pc.last_consumed_created_at
                      OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                          AND m.id > pc.last_consumed_id)
                  )
            )::integer as offset_lag,
            -- Calculate time lag: age of oldest unprocessed message
            (
                SELECT EXTRACT(EPOCH FROM (NOW() - m.created_at))::integer
                FROM queen.messages m
                WHERE m.partition_id = pc.partition_id
                  AND (
                      pc.last_consumed_created_at IS NULL
                      OR m.created_at > pc.last_consumed_created_at
                      OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                          AND m.id > pc.last_consumed_id)
                  )
                ORDER BY m.created_at ASC
                LIMIT 1
            )::integer as time_lag_seconds,
            -- Join subscription metadata
            cgm.subscription_mode,
            cgm.subscription_timestamp,
            cgm.created_at as subscription_created_at
        FROM queen.partition_consumers pc
        JOIN queen.partitions p ON p.id = pc.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        LEFT JOIN queen.consumer_groups_metadata cgm 
            ON cgm.consumer_group = pc.consumer_group
            AND (
                (cgm.queue_name = q.name AND cgm.partition_name = p.name)
                OR (cgm.queue_name = q.name AND cgm.partition_name = '')
                OR (cgm.queue_name = '' AND cgm.namespace = q.namespace)
                OR (cgm.queue_name = '' AND cgm.task = q.task)
            )
    ),
    aggregated AS (
        SELECT 
            consumer_group,
            queue_name,
            COUNT(*) as member_count,
            COALESCE(SUM(offset_lag), 0) as total_lag,
            COALESCE(MAX(time_lag_seconds), 0) as max_time_lag,
            MAX(subscription_mode) as subscription_mode,
            MAX(subscription_timestamp) as subscription_timestamp,
            MAX(subscription_created_at) as subscription_created_at,
            -- State determination
            CASE 
                WHEN MAX(time_lag_seconds) > 300 THEN 'Lagging'
                WHEN MAX(CASE WHEN last_consumed_at IS NOT NULL THEN 1 ELSE 0 END) = 1 THEN 'Stable'
                ELSE 'Dead'
            END as state
        FROM consumer_data
        GROUP BY consumer_group, queue_name
    )
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'name', consumer_group,
            'topics', jsonb_build_array(queue_name),
            'queueName', queue_name,
            'members', member_count,
            'totalLag', total_lag,
            'maxTimeLag', max_time_lag,
            'state', state,
            'subscriptionMode', subscription_mode,
            'subscriptionTimestamp', CASE WHEN subscription_timestamp IS NOT NULL 
                THEN to_char(subscription_timestamp, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END,
            'subscriptionCreatedAt', CASE WHEN subscription_created_at IS NOT NULL 
                THEN to_char(subscription_created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END
        ) ORDER BY consumer_group, queue_name
    ), '[]'::jsonb) INTO v_result
    FROM aggregated;
    
    RETURN v_result;
END;
$$;

-- ============================================================================
-- queen.get_consumer_group_details_v1: Get detailed info for a consumer group
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_consumer_group_details_v1(p_consumer_group TEXT)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
BEGIN
    WITH partition_data AS (
        SELECT 
            q.name as queue_name,
            p.name as partition_name,
            pc.worker_id,
            pc.last_consumed_at,
            pc.total_messages_consumed,
            pc.lease_expires_at,
            -- Calculate lag
            (
                SELECT COUNT(*)
                FROM queen.messages m
                WHERE m.partition_id = pc.partition_id
                  AND (
                      pc.last_consumed_created_at IS NULL
                      OR m.created_at > pc.last_consumed_created_at
                      OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                          AND m.id > pc.last_consumed_id)
                  )
            )::integer as offset_lag,
            -- Calculate time lag
            (
                SELECT EXTRACT(EPOCH FROM (NOW() - m.created_at))::integer
                FROM queen.messages m
                WHERE m.partition_id = pc.partition_id
                  AND (
                      pc.last_consumed_created_at IS NULL
                      OR m.created_at > pc.last_consumed_created_at
                      OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                          AND m.id > pc.last_consumed_id)
                  )
                ORDER BY m.created_at ASC
                LIMIT 1
            )::integer as time_lag_seconds
        FROM queen.partition_consumers pc
        JOIN queen.partitions p ON p.id = pc.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        WHERE pc.consumer_group = p_consumer_group
        ORDER BY q.name, p.name
    )
    SELECT jsonb_object_agg(
        queue_name,
        (SELECT jsonb_build_object(
            'partitions', jsonb_agg(
                jsonb_build_object(
                    'partition', partition_name,
                    'workerId', worker_id,
                    'lastConsumedAt', CASE WHEN last_consumed_at IS NOT NULL 
                        THEN to_char(last_consumed_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END,
                    'totalConsumed', total_messages_consumed,
                    'offsetLag', COALESCE(offset_lag, 0),
                    'timeLagSeconds', COALESCE(time_lag_seconds, 0),
                    'leaseActive', (lease_expires_at IS NOT NULL AND lease_expires_at > NOW())
                ) ORDER BY partition_name
            )
        ) FROM partition_data pd2 WHERE pd2.queue_name = pd.queue_name)
    ) INTO v_result
    FROM (SELECT DISTINCT queue_name FROM partition_data) pd;
    
    RETURN COALESCE(v_result, '{}'::jsonb);
END;
$$;

-- ============================================================================
-- queen.get_lagging_partitions_v1: Get partitions with significant lag
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.get_lagging_partitions_v1(p_min_lag_seconds INTEGER DEFAULT 0)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB;
BEGIN
    WITH consumer_lag AS (
        SELECT 
            pc.consumer_group,
            q.name as queue_name,
            p.name as partition_name,
            p.id as partition_id,
            pc.worker_id,
            pc.last_consumed_at,
            -- Find oldest unconsumed message
            MIN(m.created_at) as oldest_unconsumed_at,
            COUNT(m.id) as unconsumed_count,
            EXTRACT(EPOCH FROM (NOW() - MIN(m.created_at)))::integer as lag_seconds
        FROM queen.partition_consumers pc
        JOIN queen.partitions p ON p.id = pc.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        LEFT JOIN queen.messages m ON m.partition_id = pc.partition_id
            AND (
                pc.last_consumed_created_at IS NULL
                OR m.created_at > pc.last_consumed_created_at
                OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                    AND m.id > pc.last_consumed_id)
            )
        GROUP BY pc.consumer_group, q.name, p.name, p.id, pc.worker_id, pc.last_consumed_at
    )
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'consumer_group', consumer_group,
            'queue_name', queue_name,
            'partition_name', partition_name,
            'partition_id', partition_id,
            'worker_id', worker_id,
            'offset_lag', unconsumed_count,
            'time_lag_seconds', lag_seconds,
            'lag_hours', ROUND(lag_seconds / 3600.0, 2),
            'oldest_unconsumed_at', CASE WHEN oldest_unconsumed_at IS NOT NULL 
                THEN to_char(oldest_unconsumed_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END,
            'last_consumed_at', CASE WHEN last_consumed_at IS NOT NULL 
                THEN to_char(last_consumed_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END
        ) ORDER BY lag_seconds DESC
    ), '[]'::jsonb) INTO v_result
    FROM consumer_lag
    WHERE lag_seconds > p_min_lag_seconds AND lag_seconds IS NOT NULL;
    
    RETURN v_result;
END;
$$;

-- ============================================================================
-- queen.delete_consumer_group_v1: Delete consumer group and optionally metadata
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.delete_consumer_group_v1(
    p_consumer_group TEXT,
    p_delete_metadata BOOLEAN DEFAULT TRUE
)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_deleted_count INTEGER;
BEGIN
    -- Delete partition consumers
    DELETE FROM queen.partition_consumers
    WHERE consumer_group = p_consumer_group;
    
    GET DIAGNOSTICS v_deleted_count = ROW_COUNT;
    
    -- Delete metadata if requested
    IF p_delete_metadata THEN
        DELETE FROM queen.consumer_groups_metadata
        WHERE consumer_group = p_consumer_group;
    END IF;
    
    RETURN jsonb_build_object(
        'success', true,
        'consumerGroup', p_consumer_group,
        'deletedPartitions', v_deleted_count,
        'metadataDeleted', p_delete_metadata
    );
END;
$$;

-- ============================================================================
-- queen.update_consumer_group_subscription_v1: Update subscription timestamp
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.update_consumer_group_subscription_v1(
    p_consumer_group TEXT,
    p_new_timestamp TEXT
)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_updated_count INTEGER;
BEGIN
    UPDATE queen.consumer_groups_metadata
    SET subscription_timestamp = p_new_timestamp::timestamptz
    WHERE consumer_group = p_consumer_group;
    
    GET DIAGNOSTICS v_updated_count = ROW_COUNT;
    
    RETURN jsonb_build_object(
        'success', true,
        'consumerGroup', p_consumer_group,
        'newTimestamp', p_new_timestamp,
        'rowsUpdated', v_updated_count
    );
END;
$$;

-- Grant execute permissions
GRANT EXECUTE ON FUNCTION queen.get_consumer_groups_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_consumer_group_details_v1(TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_lagging_partitions_v1(INTEGER) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.delete_consumer_group_v1(TEXT, BOOLEAN) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.update_consumer_group_subscription_v1(TEXT, TEXT) TO PUBLIC;

