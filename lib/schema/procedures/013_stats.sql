-- ============================================================================
-- Stats Tables and Optimized Analytics Procedures
-- ============================================================================
-- Pre-computed statistics for O(1) analytics queries
-- Replaces expensive message table scans with cached counters
-- ============================================================================

-- ============================================================================
-- Stats Tables and Optimized Analytics Procedures
-- ============================================================================
-- Pre-computed statistics for O(1) analytics queries
-- Replaces expensive message table scans with cached counters
-- ============================================================================

-- ============================================================================
-- BACKGROUND JOB PROCEDURES
-- ============================================================================

-- Lightweight incremental message count (for fast aggregation)
-- Uses a fixed 30-second window for index efficiency (~37ms vs 8s)
-- Does NOT recalculate pending counts (that's expensive, done in full reconciliation)
-- Uses transaction-level advisory lock to prevent concurrent execution across replicas
CREATE OR REPLACE FUNCTION queen.increment_message_counts_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_updated INTEGER := 0;
    v_now TIMESTAMPTZ := NOW();
    v_lock_key BIGINT := 7868669789; -- hash of 'queen_stats_increment'
BEGIN
    -- Try to acquire transaction-level advisory lock
    -- If another instance is running, skip this execution (stats are eventually consistent)
    IF NOT pg_try_advisory_xact_lock(v_lock_key) THEN
        RETURN jsonb_build_object(
            'skipped', true, 
            'reason', 'another_instance_computing'
        );
    END IF;

    -- Use fixed 30-second window for index efficiency
    -- The index on messages(created_at) can be used because NOW() - INTERVAL is a constant
    -- The per-partition last_scanned_at is still used as secondary filter to avoid double-counting
    WITH recent_messages AS (
        SELECT partition_id, id, created_at
        FROM queen.messages
        WHERE created_at > v_now - INTERVAL '30 seconds'
    ),
    new_message_counts AS (
        SELECT 
            s.stat_key,
            s.partition_id,
            COUNT(rm.id) as new_messages,
            MAX(rm.created_at) as newest_at
        FROM queen.stats s
        JOIN recent_messages rm 
            ON rm.partition_id = s.partition_id 
           AND rm.created_at > s.last_scanned_at
        WHERE s.stat_type = 'partition'
          AND s.last_scanned_at IS NOT NULL
        GROUP BY s.stat_key, s.partition_id
    )
    UPDATE queen.stats s SET
        total_messages = s.total_messages + nmc.new_messages,
        pending_messages = s.pending_messages + nmc.new_messages,
        newest_message_at = GREATEST(s.newest_message_at, nmc.newest_at),
        last_scanned_at = v_now
    FROM new_message_counts nmc
    WHERE s.stat_type = 'partition' AND s.stat_key = nmc.stat_key;
    
    GET DIAGNOSTICS v_updated = ROW_COUNT;
    
    -- Also update last_scanned_at for partitions with no new messages
    -- This prevents the scan window from drifting too far back
    UPDATE queen.stats
    SET last_scanned_at = v_now
    WHERE stat_type = 'partition'
      AND last_scanned_at < v_now - INTERVAL '30 seconds';
    
    RETURN jsonb_build_object('partitionsUpdated', v_updated);
END;
$$;

-- Drop old function signature without parameters (if exists)
DROP FUNCTION IF EXISTS queen.compute_partition_stats_v1();

-- Compute partition-level stats using INCREMENTAL SCAN
-- Only scans NEW messages since last_scanned_at (fast!)
-- Uses partition_consumers metadata for completed/processing (no message scan needed)
-- Uses transaction-level advisory lock to prevent concurrent execution
-- p_force: if true, bypasses the 5-second debounce check (for manual refresh)
CREATE OR REPLACE FUNCTION queen.compute_partition_stats_v1(p_force BOOLEAN DEFAULT FALSE)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_updated INTEGER := 0;
    v_new_partitions INTEGER := 0;
    v_now TIMESTAMPTZ := NOW();
    v_lock_key BIGINT := 7868669788; -- hash of 'queen_stats_partition'
    v_last_computed TIMESTAMPTZ;
BEGIN
    -- Try to acquire transaction-level advisory lock
    -- If another instance is computing, skip this execution
    IF NOT pg_try_advisory_xact_lock(v_lock_key) THEN
        RETURN jsonb_build_object(
            'skipped', true, 
            'reason', 'another_instance_computing'
        );
    END IF;
    
    -- Check if PARTITION stats were computed very recently (within 5 seconds)
    -- This prevents redundant work when multiple instances call in sequence
    -- Skip this check if p_force is true (manual refresh)
    -- NOTE: Must check partition stats, not system stats (aggregations update system.last_computed_at)
    IF NOT p_force THEN
    SELECT MAX(last_computed_at) INTO v_last_computed
    FROM queen.stats
    WHERE stat_type = 'partition';
    
    IF v_last_computed IS NOT NULL AND v_last_computed > v_now - INTERVAL '5 seconds' THEN
        RETURN jsonb_build_object(
            'skipped', true,
            'reason', 'recently_computed',
            'lastComputedAt', v_last_computed
        );
        END IF;
    END IF;
    -- STEP 1: Handle partitions that already have stats (INCREMENTAL - only new messages)
    -- Count new messages since last_scanned_at and ADD to existing totals
    WITH new_message_counts AS (
        SELECT 
            s.stat_key,
            s.partition_id,
            COUNT(m.id) as new_messages,
            MAX(m.created_at) as newest_at
        FROM queen.stats s
        JOIN queen.messages m ON m.partition_id = s.partition_id
        WHERE s.stat_type = 'partition'
          AND s.last_scanned_at IS NOT NULL
          AND m.created_at > s.last_scanned_at
        GROUP BY s.stat_key, s.partition_id
    )
    UPDATE queen.stats s SET
        total_messages = s.total_messages + nmc.new_messages,
        pending_messages = s.pending_messages + nmc.new_messages,
        newest_message_at = GREATEST(s.newest_message_at, nmc.newest_at),
        last_scanned_at = v_now,
        last_computed_at = v_now
    FROM new_message_counts nmc
    WHERE s.stat_type = 'partition' AND s.stat_key = nmc.stat_key;
    
    GET DIAGNOSTICS v_updated = ROW_COUNT;
    
    -- STEP 2: Calculate pending from ACTUAL cursor position (accurate, avoids race conditions)
    -- OPTIMIZED: Uses a single batched join instead of correlated subqueries (7x faster)
    -- This scans the messages table once instead of once per partition×consumer
    WITH cursor_positions AS (
        SELECT 
            s.stat_key,
            s.partition_id,
            pc.consumer_group,
            pc.last_consumed_id,
            pc.last_consumed_created_at,
            pc.total_messages_consumed,
            pc.lease_expires_at,
            pc.batch_size,
            pc.acked_count
        FROM queen.stats s
        JOIN queen.partition_consumers pc 
            ON pc.partition_id = s.partition_id AND pc.consumer_group = s.consumer_group
        WHERE s.stat_type = 'partition'
    ),
    pending_counts AS (
        SELECT 
            cp.stat_key,
            cp.partition_id,
            cp.consumer_group,
            cp.last_consumed_created_at,
            cp.lease_expires_at,
            cp.batch_size,
            cp.acked_count,
            COUNT(m.id) as actual_pending
        FROM cursor_positions cp
        LEFT JOIN queen.messages m 
            ON m.partition_id = cp.partition_id
           AND (cp.last_consumed_created_at IS NULL 
                OR m.created_at > cp.last_consumed_created_at
                OR (m.created_at = cp.last_consumed_created_at AND m.id > cp.last_consumed_id))
        GROUP BY cp.stat_key, cp.partition_id, cp.consumer_group,
                 cp.last_consumed_created_at, cp.lease_expires_at, cp.batch_size, cp.acked_count
    )
    UPDATE queen.stats s SET
        -- Pending: from actual cursor position (most accurate)
        pending_messages = COALESCE(pend.actual_pending, 0),
        -- Processing: messages currently being processed (active lease)
        processing_messages = CASE 
            WHEN pend.lease_expires_at IS NOT NULL AND pend.lease_expires_at > v_now
            THEN LEAST(GREATEST(0, COALESCE(pend.batch_size, 0) - COALESCE(pend.acked_count, 0)), pend.actual_pending)
            ELSE 0
        END,
        -- Completed: total - pending - dead_letter (derived from accurate pending)
        completed_messages = GREATEST(0, s.total_messages - COALESCE(pend.actual_pending, 0) - s.dead_letter_messages),
        -- Oldest pending: use cursor position
        oldest_pending_at = CASE 
            WHEN pend.actual_pending > 0 THEN pend.last_consumed_created_at
            ELSE NULL
        END,
        -- Track when partition stats were actually computed (for debounce check)
        last_computed_at = v_now
    FROM pending_counts pend
    WHERE s.stat_type = 'partition'
      AND s.stat_key = pend.stat_key;
    
    -- STEP 3: Update dead_letter counts (small table, fast query)
    UPDATE queen.stats s SET
        dead_letter_messages = dlq_counts.dlq_count
    FROM (
        SELECT partition_id, consumer_group, COUNT(*) as dlq_count
        FROM queen.dead_letter_queue
        GROUP BY partition_id, consumer_group
    ) dlq_counts
    WHERE s.stat_type = 'partition'
      AND s.partition_id = dlq_counts.partition_id
      AND s.consumer_group = COALESCE(dlq_counts.consumer_group, '__QUEUE_MODE__');
    
    -- STEP 4: Handle NEW partitions (no existing stats - need full count, but only for these)
    -- OPTIMIZED: Uses CTEs to pre-compute counts in single passes instead of correlated subqueries
    -- This changes O(partitions × messages) to O(messages + partitions) for initial bootstrap
    WITH message_counts AS (
        SELECT partition_id, COUNT(*) as msg_count
        FROM queen.messages
        GROUP BY partition_id
    ),
    dlq_counts AS (
        SELECT partition_id, COUNT(*) as dlq_count
        FROM queen.dead_letter_queue
        GROUP BY partition_id
    )
    INSERT INTO queen.stats (
        stat_type, stat_key, queue_id, partition_id, consumer_group,
        total_messages, pending_messages, processing_messages, completed_messages, dead_letter_messages,
        oldest_pending_at, newest_message_at, last_scanned_at, last_computed_at
    )
    SELECT 
        'partition',
        p.id::text || ':' || COALESCE(pc.consumer_group, '__QUEUE_MODE__'),
        q.id,
        p.id,
        COALESCE(pc.consumer_group, '__QUEUE_MODE__'),
        -- Total messages in partition (from CTE)
        COALESCE(mc.msg_count, 0),
        -- Pending = total - completed - dlq
        GREATEST(0, 
            COALESCE(mc.msg_count, 0)
            - COALESCE(pc.total_messages_consumed, 0)
            - COALESCE(dc.dlq_count, 0)
        ),
        -- Processing
        CASE 
            WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > v_now
            THEN GREATEST(0, COALESCE(pc.batch_size, 0) - COALESCE(pc.acked_count, 0))
            ELSE 0
        END,
        -- Completed
        COALESCE(pc.total_messages_consumed, 0),
        -- Dead letter (from CTE)
        COALESCE(dc.dlq_count, 0),
        -- Oldest pending (use partition_lookup via JOIN)
        pl.last_message_created_at,
        -- Newest message (use partition_lookup via JOIN)
        pl.last_message_created_at,
        v_now,
        v_now
    FROM queen.partitions p
    JOIN queen.queues q ON q.id = p.queue_id
    LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
    LEFT JOIN message_counts mc ON mc.partition_id = p.id
    LEFT JOIN dlq_counts dc ON dc.partition_id = p.id
    LEFT JOIN queen.partition_lookup pl ON pl.partition_id = p.id
    WHERE NOT EXISTS (
        SELECT 1 FROM queen.stats s 
        WHERE s.stat_type = 'partition' 
        AND s.partition_id = p.id
        AND s.consumer_group = COALESCE(pc.consumer_group, '__QUEUE_MODE__')
    )
    ON CONFLICT (stat_type, stat_key) DO NOTHING;
    
    GET DIAGNOSTICS v_new_partitions = ROW_COUNT;
    
    -- STEP 5: Update last_scanned_at for partitions with no new messages
    UPDATE queen.stats SET
        last_scanned_at = v_now,
        last_computed_at = v_now
    WHERE stat_type = 'partition'
      AND (last_scanned_at IS NULL OR last_scanned_at < v_now);
    
    RETURN jsonb_build_object(
        'partitionsUpdated', v_updated,
        'newPartitions', v_new_partitions
    );
END;
$$;

-- Aggregate queue stats from partition stats
CREATE OR REPLACE FUNCTION queen.aggregate_queue_stats_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_updated INTEGER := 0;
BEGIN
    INSERT INTO queen.stats (
        stat_type, stat_key, queue_id,
        child_count, total_messages, pending_messages, processing_messages, 
        completed_messages, dead_letter_messages,
        oldest_pending_at, newest_message_at,
        avg_lag_seconds, max_lag_seconds, avg_offset_lag, max_offset_lag,
        ingested_per_second, processed_per_second,
        prev_total_messages, prev_completed_messages, prev_snapshot_at,
        last_computed_at
    )
    SELECT 
        'queue',
        q.id::text,
        q.id,
        COUNT(DISTINCT p.id),
        COALESCE(SUM(s.total_messages), 0),
        COALESCE(SUM(s.pending_messages), 0),
        COALESCE(SUM(s.processing_messages), 0),
        COALESCE(SUM(s.completed_messages), 0),
        COALESCE(SUM(s.dead_letter_messages), 0),
        MIN(s.oldest_pending_at),
        MAX(s.newest_message_at),
        -- Avg lag across partitions
        COALESCE(AVG(EXTRACT(EPOCH FROM (NOW() - s.oldest_pending_at)))::integer, 0),
        -- Max lag
        COALESCE(MAX(EXTRACT(EPOCH FROM (NOW() - s.oldest_pending_at)))::integer, 0),
        -- Avg offset lag
        COALESCE(AVG(s.pending_messages)::integer, 0),
        -- Max offset lag
        COALESCE(MAX(s.pending_messages)::integer, 0),
        -- Throughput (will be computed from deltas)
        0, 0,
        -- Previous values for delta (preserve existing)
        COALESCE((SELECT prev_total_messages FROM queen.stats WHERE stat_type = 'queue' AND stat_key = q.id::text), 0),
        COALESCE((SELECT prev_completed_messages FROM queen.stats WHERE stat_type = 'queue' AND stat_key = q.id::text), 0),
        COALESCE((SELECT prev_snapshot_at FROM queen.stats WHERE stat_type = 'queue' AND stat_key = q.id::text), NOW()),
        NOW()
    FROM queen.queues q
    LEFT JOIN queen.partitions p ON p.queue_id = q.id
    LEFT JOIN queen.stats s ON s.stat_type = 'partition' AND s.partition_id = p.id
    GROUP BY q.id
    ON CONFLICT (stat_type, stat_key) DO UPDATE SET
        child_count = EXCLUDED.child_count,
        total_messages = EXCLUDED.total_messages,
        pending_messages = EXCLUDED.pending_messages,
        processing_messages = EXCLUDED.processing_messages,
        completed_messages = EXCLUDED.completed_messages,
        dead_letter_messages = EXCLUDED.dead_letter_messages,
        oldest_pending_at = EXCLUDED.oldest_pending_at,
        newest_message_at = EXCLUDED.newest_message_at,
        avg_lag_seconds = EXCLUDED.avg_lag_seconds,
        max_lag_seconds = EXCLUDED.max_lag_seconds,
        avg_offset_lag = EXCLUDED.avg_offset_lag,
        max_offset_lag = EXCLUDED.max_offset_lag,
        -- Compute throughput from delta
        ingested_per_second = CASE 
            WHEN queen.stats.prev_snapshot_at IS NOT NULL 
                AND NOW() > queen.stats.prev_snapshot_at
                AND EXTRACT(EPOCH FROM (NOW() - queen.stats.prev_snapshot_at)) > 0
            THEN ((EXCLUDED.total_messages - queen.stats.prev_total_messages)::numeric / 
                  EXTRACT(EPOCH FROM (NOW() - queen.stats.prev_snapshot_at)))
            ELSE 0 
        END,
        processed_per_second = CASE 
            WHEN queen.stats.prev_snapshot_at IS NOT NULL 
                AND NOW() > queen.stats.prev_snapshot_at
                AND EXTRACT(EPOCH FROM (NOW() - queen.stats.prev_snapshot_at)) > 0
            THEN ((EXCLUDED.completed_messages - queen.stats.prev_completed_messages)::numeric / 
                  EXTRACT(EPOCH FROM (NOW() - queen.stats.prev_snapshot_at)))
            ELSE 0 
        END,
        prev_total_messages = queen.stats.total_messages,
        prev_completed_messages = queen.stats.completed_messages,
        prev_snapshot_at = queen.stats.last_computed_at,
        last_computed_at = NOW();
    
    GET DIAGNOSTICS v_updated = ROW_COUNT;
    
    RETURN jsonb_build_object('queuesUpdated', v_updated);
END;
$$;

-- Aggregate namespace stats from queue stats
CREATE OR REPLACE FUNCTION queen.aggregate_namespace_stats_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_updated INTEGER := 0;
BEGIN
    INSERT INTO queen.stats (
        stat_type, stat_key,
        child_count, total_messages, pending_messages, processing_messages,
        completed_messages, dead_letter_messages,
        avg_lag_seconds, max_lag_seconds,
        ingested_per_second, processed_per_second,
        last_computed_at
    )
    SELECT 
        'namespace',
        q.namespace,
        COUNT(DISTINCT q.id),
        COALESCE(SUM(s.total_messages), 0),
        COALESCE(SUM(s.pending_messages), 0),
        COALESCE(SUM(s.processing_messages), 0),
        COALESCE(SUM(s.completed_messages), 0),
        COALESCE(SUM(s.dead_letter_messages), 0),
        COALESCE(AVG(s.avg_lag_seconds)::integer, 0),
        COALESCE(MAX(s.max_lag_seconds), 0),
        COALESCE(SUM(s.ingested_per_second), 0),
        COALESCE(SUM(s.processed_per_second), 0),
        NOW()
    FROM queen.queues q
    LEFT JOIN queen.stats s ON s.stat_type = 'queue' AND s.queue_id = q.id
    WHERE q.namespace IS NOT NULL AND q.namespace != ''
    GROUP BY q.namespace
    ON CONFLICT (stat_type, stat_key) DO UPDATE SET
        child_count = EXCLUDED.child_count,
        total_messages = EXCLUDED.total_messages,
        pending_messages = EXCLUDED.pending_messages,
        processing_messages = EXCLUDED.processing_messages,
        completed_messages = EXCLUDED.completed_messages,
        dead_letter_messages = EXCLUDED.dead_letter_messages,
        avg_lag_seconds = EXCLUDED.avg_lag_seconds,
        max_lag_seconds = EXCLUDED.max_lag_seconds,
        ingested_per_second = EXCLUDED.ingested_per_second,
        processed_per_second = EXCLUDED.processed_per_second,
        last_computed_at = NOW();
    
    GET DIAGNOSTICS v_updated = ROW_COUNT;
    
    RETURN jsonb_build_object('namespacesUpdated', v_updated);
END;
$$;

-- Aggregate task stats from queue stats
CREATE OR REPLACE FUNCTION queen.aggregate_task_stats_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_updated INTEGER := 0;
BEGIN
    INSERT INTO queen.stats (
        stat_type, stat_key,
        child_count, total_messages, pending_messages, processing_messages,
        completed_messages, dead_letter_messages,
        avg_lag_seconds, max_lag_seconds,
        ingested_per_second, processed_per_second,
        last_computed_at
    )
    SELECT 
        'task',
        q.task,
        COUNT(DISTINCT q.id),
        COALESCE(SUM(s.total_messages), 0),
        COALESCE(SUM(s.pending_messages), 0),
        COALESCE(SUM(s.processing_messages), 0),
        COALESCE(SUM(s.completed_messages), 0),
        COALESCE(SUM(s.dead_letter_messages), 0),
        COALESCE(AVG(s.avg_lag_seconds)::integer, 0),
        COALESCE(MAX(s.max_lag_seconds), 0),
        COALESCE(SUM(s.ingested_per_second), 0),
        COALESCE(SUM(s.processed_per_second), 0),
        NOW()
    FROM queen.queues q
    LEFT JOIN queen.stats s ON s.stat_type = 'queue' AND s.queue_id = q.id
    WHERE q.task IS NOT NULL AND q.task != ''
    GROUP BY q.task
    ON CONFLICT (stat_type, stat_key) DO UPDATE SET
        child_count = EXCLUDED.child_count,
        total_messages = EXCLUDED.total_messages,
        pending_messages = EXCLUDED.pending_messages,
        processing_messages = EXCLUDED.processing_messages,
        completed_messages = EXCLUDED.completed_messages,
        dead_letter_messages = EXCLUDED.dead_letter_messages,
        avg_lag_seconds = EXCLUDED.avg_lag_seconds,
        max_lag_seconds = EXCLUDED.max_lag_seconds,
        ingested_per_second = EXCLUDED.ingested_per_second,
        processed_per_second = EXCLUDED.processed_per_second,
        last_computed_at = NOW();
    
    GET DIAGNOSTICS v_updated = ROW_COUNT;
    
    RETURN jsonb_build_object('tasksUpdated', v_updated);
END;
$$;

-- Aggregate system-wide stats from queue stats
CREATE OR REPLACE FUNCTION queen.aggregate_system_stats_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_updated INTEGER := 0;
BEGIN
    INSERT INTO queen.stats (
        stat_type, stat_key,
        child_count, total_messages, pending_messages, processing_messages,
        completed_messages, dead_letter_messages,
        avg_lag_seconds, max_lag_seconds, median_lag_seconds,
        avg_offset_lag, max_offset_lag,
        ingested_per_second, processed_per_second,
        prev_total_messages, prev_completed_messages, prev_snapshot_at,
        last_computed_at
    )
    SELECT 
        'system',
        'global',
        (SELECT COUNT(*) FROM queen.partitions),
        COALESCE(SUM(s.total_messages), 0),
        COALESCE(SUM(s.pending_messages), 0),
        COALESCE(SUM(s.processing_messages), 0),
        COALESCE(SUM(s.completed_messages), 0),
        COALESCE(SUM(s.dead_letter_messages), 0),
        COALESCE(AVG(s.avg_lag_seconds)::integer, 0),
        COALESCE(MAX(s.max_lag_seconds), 0),
        COALESCE(PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY s.max_lag_seconds)::integer, 0),
        COALESCE(AVG(s.pending_messages)::integer, 0),
        COALESCE(MAX(s.pending_messages)::integer, 0),
        COALESCE(SUM(s.ingested_per_second), 0),
        COALESCE(SUM(s.processed_per_second), 0),
        COALESCE((SELECT prev_total_messages FROM queen.stats WHERE stat_type = 'system' AND stat_key = 'global'), 0),
        COALESCE((SELECT prev_completed_messages FROM queen.stats WHERE stat_type = 'system' AND stat_key = 'global'), 0),
        COALESCE((SELECT prev_snapshot_at FROM queen.stats WHERE stat_type = 'system' AND stat_key = 'global'), NOW()),
        NOW()
    FROM queen.stats s
    WHERE s.stat_type = 'queue'
    ON CONFLICT (stat_type, stat_key) DO UPDATE SET
        child_count = EXCLUDED.child_count,
        total_messages = EXCLUDED.total_messages,
        pending_messages = EXCLUDED.pending_messages,
        processing_messages = EXCLUDED.processing_messages,
        completed_messages = EXCLUDED.completed_messages,
        dead_letter_messages = EXCLUDED.dead_letter_messages,
        avg_lag_seconds = EXCLUDED.avg_lag_seconds,
        max_lag_seconds = EXCLUDED.max_lag_seconds,
        median_lag_seconds = EXCLUDED.median_lag_seconds,
        avg_offset_lag = EXCLUDED.avg_offset_lag,
        max_offset_lag = EXCLUDED.max_offset_lag,
        -- Compute throughput from delta
        ingested_per_second = CASE 
            WHEN queen.stats.prev_snapshot_at IS NOT NULL 
                AND NOW() > queen.stats.prev_snapshot_at
                AND EXTRACT(EPOCH FROM (NOW() - queen.stats.prev_snapshot_at)) > 0
            THEN ((EXCLUDED.total_messages - queen.stats.prev_total_messages)::numeric / 
                  EXTRACT(EPOCH FROM (NOW() - queen.stats.prev_snapshot_at)))
            ELSE queen.stats.ingested_per_second 
        END,
        processed_per_second = CASE 
            WHEN queen.stats.prev_snapshot_at IS NOT NULL 
                AND NOW() > queen.stats.prev_snapshot_at
                AND EXTRACT(EPOCH FROM (NOW() - queen.stats.prev_snapshot_at)) > 0
            THEN ((EXCLUDED.completed_messages - queen.stats.prev_completed_messages)::numeric / 
                  EXTRACT(EPOCH FROM (NOW() - queen.stats.prev_snapshot_at)))
            ELSE queen.stats.processed_per_second 
        END,
        prev_total_messages = queen.stats.total_messages,
        prev_completed_messages = queen.stats.completed_messages,
        prev_snapshot_at = queen.stats.last_computed_at,
        last_computed_at = NOW();
    
    GET DIAGNOSTICS v_updated = ROW_COUNT;
    
    RETURN jsonb_build_object('systemUpdated', v_updated);
END;
$$;

-- Write stats snapshot to history table
-- Calculates deltas by comparing to previous bucket in history (not stats.prev_* fields)
-- Skips if this bucket was already written recently (prevents duplicate work across instances)
-- Write stats history with 1-minute granularity
-- If reconciliation interval > 1 minute, writes multiple rows dividing delta evenly
-- p_bucket_minutes parameter is kept for backwards compatibility but ignored (always 1 min)
-- NOTE: write_stats_history_v1 and cleanup_stats_history_v1 REMOVED
-- Throughput history is now handled by worker_metrics (see 014_worker_metrics.sql)

-- Cleanup orphaned stats (for deleted queues/partitions)
CREATE OR REPLACE FUNCTION queen.cleanup_orphaned_stats_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_deleted INTEGER := 0;
    v_count INTEGER := 0;
BEGIN
    -- Delete partition stats for non-existent partitions
    DELETE FROM queen.stats 
    WHERE stat_type = 'partition' 
    AND partition_id NOT IN (SELECT id FROM queen.partitions);
    
    GET DIAGNOSTICS v_deleted = ROW_COUNT;
    
    -- Delete queue stats for non-existent queues
    DELETE FROM queen.stats 
    WHERE stat_type = 'queue' 
    AND queue_id NOT IN (SELECT id FROM queen.queues);
    
    GET DIAGNOSTICS v_count = ROW_COUNT;
    v_deleted := v_deleted + v_count;
    
    RETURN jsonb_build_object('orphanedDeleted', v_deleted);
END;
$$;

-- ============================================================================
-- OPTIMIZED ANALYTICS PROCEDURES (V2)
-- These return the SAME FORMAT as V1 but use pre-computed stats
-- ============================================================================

-- NOTE: get_system_overview_v2 REMOVED - replaced by get_system_overview_v3 in 014_worker_metrics.sql

-- queen.get_queues_v2: O(queues) queue list with stats
CREATE OR REPLACE FUNCTION queen.get_queues_v2()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
BEGIN
    -- Return same format as get_queues_v1
    RETURN (
        SELECT jsonb_build_object(
            'queues', COALESCE(jsonb_agg(
                jsonb_build_object(
                    'id', q.id,
                    'name', q.name,
                    'namespace', q.namespace,
                    'task', q.task,
                    'createdAt', to_char(q.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                    'partitions', COALESCE(s.child_count, 0),
                    'messages', jsonb_build_object(
                        'total', COALESCE(s.total_messages, 0),
                        'pending', GREATEST(0, COALESCE(s.pending_messages, 0) - COALESCE(s.processing_messages, 0)),
                        'processing', COALESCE(s.processing_messages, 0)
                    )
                ) ORDER BY q.created_at DESC
            ), '[]'::jsonb)
        )
        FROM queen.queues q
        LEFT JOIN queen.stats s ON s.stat_type = 'queue' AND s.queue_id = q.id
    );
END;
$$;

-- queen.get_queue_v2: O(partitions) single queue detail
-- Aggregates stats across ALL consumer groups (not just __QUEUE_MODE__)
CREATE OR REPLACE FUNCTION queen.get_queue_v2(p_queue_name TEXT)
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
    
    -- Get partitions with pre-computed stats
    -- Aggregate across ALL consumer groups per partition (MAX for totals since they're the same)
    WITH partition_stats AS (
        SELECT 
            p.id,
            p.name,
            p.created_at,
            -- Use MAX to get the message counts (same for all consumer groups)
            COALESCE(MAX(s.total_messages), 0) as total,
            -- Pending/processing may differ per consumer group - use MAX for queue view
            COALESCE(MAX(s.pending_messages), 0) as pending,
            COALESCE(MAX(s.processing_messages), 0) as processing,
            COALESCE(MAX(s.completed_messages), 0) as completed,
            0 as failed,
            COALESCE(MAX(s.dead_letter_messages), 0) as dead_letter,
            MAX(s.oldest_pending_at) as oldest_message,
            MAX(s.newest_message_at) as newest_message
        FROM queen.partitions p
        LEFT JOIN queen.stats s ON s.stat_type = 'partition' 
            AND s.partition_id = p.id
        WHERE p.queue_id = v_queue_id
        GROUP BY p.id, p.name, p.created_at
    )
    SELECT 
        COALESCE(jsonb_agg(
            jsonb_build_object(
                'id', id,
                'name', name,
                'createdAt', to_char(created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                'stats', jsonb_build_object(
                    'total', total,
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
            'total', COALESCE(SUM(total), 0),
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

-- queen.get_namespaces_v2: O(namespaces) namespace list
CREATE OR REPLACE FUNCTION queen.get_namespaces_v2()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
BEGIN
    -- Return same format as get_namespaces_v1
    RETURN (
        SELECT jsonb_build_object(
            'namespaces', COALESCE(jsonb_agg(
                jsonb_build_object(
                    'namespace', s.stat_key,
                    'queues', s.child_count,
                    'partitions', (
                        SELECT COUNT(*) FROM queen.partitions p 
                        JOIN queen.queues q ON q.id = p.queue_id 
                        WHERE q.namespace = s.stat_key
                    ),
                    'messages', jsonb_build_object(
                        'total', s.total_messages,
                        'pending', GREATEST(0, s.pending_messages - s.processing_messages)
                    )
                ) ORDER BY s.stat_key
            ), '[]'::jsonb)
        )
        FROM queen.stats s
        WHERE s.stat_type = 'namespace'
    );
END;
$$;

-- queen.get_tasks_v2: O(tasks) task list
CREATE OR REPLACE FUNCTION queen.get_tasks_v2()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
BEGIN
    -- Return same format as get_tasks_v1
    RETURN (
        SELECT jsonb_build_object(
            'tasks', COALESCE(jsonb_agg(
                jsonb_build_object(
                    'task', s.stat_key,
                    'queues', s.child_count,
                    'partitions', (
                        SELECT COUNT(*) FROM queen.partitions p 
                        JOIN queen.queues q ON q.id = p.queue_id 
                        WHERE q.task = s.stat_key
                    ),
                    'messages', jsonb_build_object(
                        'total', s.total_messages,
                        'pending', GREATEST(0, s.pending_messages - s.processing_messages)
                    )
                ) ORDER BY s.stat_key
            ), '[]'::jsonb)
        )
        FROM queen.stats s
        WHERE s.stat_type = 'task'
    );
END;
$$;

-- NOTE: get_status_v2 REMOVED - replaced by get_status_v3 in 014_worker_metrics.sql

-- queen.get_queue_detail_v2: Queue detail using pre-computed stats
-- Returns format matching frontend expectations: partition.messages.*, totals.messages.*, partition.cursor.*
-- Aggregates stats across ALL consumer groups (not just __QUEUE_MODE__)
CREATE OR REPLACE FUNCTION queen.get_queue_detail_v2(p_queue_name TEXT)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_queue_id UUID;
    v_queue_info JSONB;
    v_partitions JSONB;
    v_totals JSONB;
BEGIN
    -- Get queue info with config (wrapped for frontend)
    SELECT q.id, jsonb_build_object(
        'queue', jsonb_build_object(
            'id', q.id,
            'name', q.name,
            'namespace', q.namespace,
            'task', q.task,
            'priority', q.priority,
            'createdAt', to_char(q.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
            'config', jsonb_build_object(
                'leaseTime', q.lease_time,
                'retryLimit', q.retry_limit,
                'retryDelay', q.retry_delay,
                'ttl', q.ttl,
                'maxQueueSize', q.max_queue_size,
                'deadLetterQueue', q.dead_letter_queue
            )
        )
    )
    INTO v_queue_id, v_queue_info
    FROM queen.queues q
    WHERE q.name = p_queue_name;
    
    IF v_queue_id IS NULL THEN
        RETURN jsonb_build_object('error', 'Queue not found');
    END IF;
    
    -- Get partitions with pre-computed stats
    -- Format matches frontend: partition.messages.*, partition.cursor.*
    -- Aggregate across ALL consumer groups per partition
    WITH partition_stats AS (
        SELECT 
            p.id,
            p.name,
            p.created_at,
            -- Use MAX for message counts (same for all consumer groups)
            COALESCE(MAX(s.total_messages), 0) as total,
            COALESCE(MAX(s.pending_messages), 0) as pending,
            COALESCE(MAX(s.processing_messages), 0) as processing,
            COALESCE(MAX(s.completed_messages), 0) as completed,
            0 as failed,
            COALESCE(MAX(s.dead_letter_messages), 0) as dead_letter,
            MAX(s.oldest_pending_at) as oldest_message,
            MAX(s.newest_message_at) as newest_message,
            -- Cursor info: SUM across all consumer groups
            COALESCE(SUM(pc.total_messages_consumed), 0) as total_consumed,
            COALESCE(SUM(pc.total_batches_consumed), 0) as batches_consumed,
            MAX(pc.last_consumed_at) as last_activity
        FROM queen.partitions p
        LEFT JOIN queen.stats s ON s.stat_type = 'partition' 
            AND s.partition_id = p.id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
        WHERE p.queue_id = v_queue_id
        GROUP BY p.id, p.name, p.created_at
    )
    SELECT 
        COALESCE(jsonb_agg(
            jsonb_build_object(
                'id', id,
                'name', name,
                'createdAt', to_char(created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                -- Frontend expects 'messages' not 'stats'
                'messages', jsonb_build_object(
                    'total', total,
                    'pending', pending,
                    'processing', processing,
                    'completed', completed,
                    'failed', failed,
                    'deadLetter', dead_letter
                ),
                -- Also keep 'stats' for backward compatibility
                'stats', jsonb_build_object(
                    'total', total,
                    'pending', pending,
                    'processing', processing,
                    'completed', completed,
                    'failed', failed,
                    'deadLetter', dead_letter
                ),
                -- Cursor info for "Consumed" column
                'cursor', jsonb_build_object(
                    'totalConsumed', total_consumed,
                    'batchesConsumed', batches_consumed
                ),
                'lastActivity', CASE WHEN last_activity IS NOT NULL 
                    THEN to_char(last_activity, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END,
                'oldestMessage', CASE WHEN oldest_message IS NOT NULL 
                    THEN to_char(oldest_message, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END,
                'newestMessage', CASE WHEN newest_message IS NOT NULL 
                    THEN to_char(newest_message, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"') ELSE NULL END
            ) ORDER BY name
        ), '[]'::jsonb),
        -- Frontend expects totals.messages.* not totals.*
        jsonb_build_object(
            'messages', jsonb_build_object(
                'total', COALESCE(SUM(total), 0),
                'pending', COALESCE(SUM(pending), 0),
                'processing', COALESCE(SUM(processing), 0),
                'completed', COALESCE(SUM(completed), 0),
                'failed', COALESCE(SUM(failed), 0),
                'deadLetter', COALESCE(SUM(dead_letter), 0)
            ),
            -- Also keep flat values for backward compatibility
            'total', COALESCE(SUM(total), 0),
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

-- queen.get_status_queues_v2: Queues list using pre-computed stats
CREATE OR REPLACE FUNCTION queen.get_status_queues_v2(
    p_filters JSONB DEFAULT '{}'::jsonb,
    p_limit INTEGER DEFAULT 100,
    p_offset INTEGER DEFAULT 0
)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_namespace TEXT;
    v_task TEXT;
BEGIN
    v_namespace := p_filters->>'namespace';
    v_task := p_filters->>'task';
    
    RETURN (
        SELECT jsonb_build_object(
            'queues', COALESCE(jsonb_agg(queue_data), '[]'::jsonb),
            'pagination', jsonb_build_object(
                'limit', p_limit,
                'offset', p_offset
            )
        )
        FROM (
            SELECT jsonb_build_object(
                'id', q.id,
                'name', q.name,
                'namespace', q.namespace,
                'task', q.task,
                'createdAt', to_char(q.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
                'partitions', COALESCE(s.child_count, 0),
                'messages', jsonb_build_object(
                    'total', COALESCE(s.total_messages, 0),
                    'pending', COALESCE(s.pending_messages, 0),
                    'processing', COALESCE(s.processing_messages, 0)
                )
            ) as queue_data
            FROM queen.queues q
            LEFT JOIN queen.stats s ON s.stat_type = 'queue' AND s.queue_id = q.id
            WHERE (v_namespace IS NULL OR q.namespace = v_namespace)
              AND (v_task IS NULL OR q.task = v_task)
            ORDER BY q.created_at DESC
            LIMIT p_limit OFFSET p_offset
        ) subq
    );
END;
$$;

-- ============================================================================
-- FULL STATS REFRESH (for initial population or reconciliation)
-- ============================================================================
-- Uses transaction-level advisory lock to prevent concurrent execution
-- across multiple server instances. Lock auto-releases when function returns.

-- p_force: if true, bypasses debounce checks (for manual "Hard Refresh")
CREATE OR REPLACE FUNCTION queen.refresh_all_stats_v1(p_force BOOLEAN DEFAULT FALSE)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB := '{}'::jsonb;
    v_step JSONB;
    v_lock_key BIGINT := 7868669787; -- hash of 'queen_stats_refresh'
BEGIN
    -- Try to acquire transaction-level advisory lock
    -- If another instance is running, skip this execution
    IF NOT pg_try_advisory_xact_lock(v_lock_key) THEN
        RETURN jsonb_build_object(
            'skipped', true, 
            'reason', 'another_instance_running'
        );
    END IF;
    
    -- Step 1: Compute partition stats (from messages)
    SELECT queen.compute_partition_stats_v1(p_force) INTO v_step;
    v_result := v_result || jsonb_build_object('partition', v_step);
    
    -- Step 2: Aggregate queue stats
    SELECT queen.aggregate_queue_stats_v1() INTO v_step;
    v_result := v_result || jsonb_build_object('queue', v_step);
    
    -- Step 3: Aggregate namespace stats
    SELECT queen.aggregate_namespace_stats_v1() INTO v_step;
    v_result := v_result || jsonb_build_object('namespace', v_step);
    
    -- Step 4: Aggregate task stats
    SELECT queen.aggregate_task_stats_v1() INTO v_step;
    v_result := v_result || jsonb_build_object('task', v_step);
    
    -- Step 5: Aggregate system stats
    SELECT queen.aggregate_system_stats_v1() INTO v_step;
    v_result := v_result || jsonb_build_object('system', v_step);
    
    -- NOTE: Step 6 (write_stats_history_v1) REMOVED - handled by worker_metrics
    
    -- Step 6: Cleanup orphaned
    SELECT queen.cleanup_orphaned_stats_v1() INTO v_step;
    v_result := v_result || jsonb_build_object('cleanup', v_step);
    
    -- Lock automatically released when transaction commits
    RETURN v_result;
END;
$$;

-- ============================================================================
-- GRANT PERMISSIONS
-- ============================================================================

GRANT EXECUTE ON FUNCTION queen.increment_message_counts_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.compute_partition_stats_v1(BOOLEAN) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.aggregate_queue_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.aggregate_namespace_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.aggregate_task_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.aggregate_system_stats_v1() TO PUBLIC;
-- NOTE: write_stats_history_v1 and cleanup_stats_history_v1 REMOVED (handled by worker_metrics)
GRANT EXECUTE ON FUNCTION queen.cleanup_orphaned_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.refresh_all_stats_v1(BOOLEAN) TO PUBLIC;

-- NOTE: get_system_overview_v2 and get_status_v2 REMOVED (replaced by v3 in 014_worker_metrics.sql)
GRANT EXECUTE ON FUNCTION queen.get_queues_v2() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_queue_v2(TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_namespaces_v2() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_tasks_v2() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_queue_detail_v2(TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_status_queues_v2(JSONB, INTEGER, INTEGER) TO PUBLIC;
