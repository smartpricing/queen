-- ============================================================================
-- Stats Tables and Optimized Analytics Procedures
-- ============================================================================
-- Pre-computed statistics for O(1) analytics queries
-- Replaces expensive message table scans with cached counters
-- ============================================================================

-- ============================================================================
-- TABLES
-- ============================================================================

-- Unified stats table for all hierarchy levels
CREATE TABLE IF NOT EXISTS queen.stats (
    -- Primary key: type + flexible key
    stat_type VARCHAR(20) NOT NULL,      -- 'system', 'namespace', 'task', 'queue', 'partition'
    stat_key VARCHAR(512) NOT NULL,      -- Identifier varies by type
    
    -- Foreign key references (nullable based on type, for cascades)
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',
    
    -- Universal counters
    child_count INTEGER NOT NULL DEFAULT 0,       -- partitions for queue, queues for namespace, etc.
    total_messages BIGINT NOT NULL DEFAULT 0,
    pending_messages BIGINT NOT NULL DEFAULT 0,
    processing_messages BIGINT NOT NULL DEFAULT 0,
    completed_messages BIGINT NOT NULL DEFAULT 0,
    dead_letter_messages BIGINT NOT NULL DEFAULT 0,
    
    -- Time bounds (for lag calculations)
    oldest_pending_at TIMESTAMPTZ,
    newest_message_at TIMESTAMPTZ,
    
    -- Lag metrics (pre-computed during roll-up)
    avg_lag_seconds INTEGER DEFAULT 0,
    max_lag_seconds INTEGER DEFAULT 0,
    median_lag_seconds INTEGER DEFAULT 0,
    avg_offset_lag INTEGER DEFAULT 0,
    max_offset_lag INTEGER DEFAULT 0,
    
    -- Throughput metrics (rate per second, computed from deltas)
    ingested_per_second NUMERIC(10,2) DEFAULT 0,
    processed_per_second NUMERIC(10,2) DEFAULT 0,
    
    -- Delta tracking for rate calculation
    prev_total_messages BIGINT DEFAULT 0,
    prev_completed_messages BIGINT DEFAULT 0,
    prev_snapshot_at TIMESTAMPTZ,
    
    -- Incremental scan tracking (for partition stats)
    last_scanned_at TIMESTAMPTZ,              -- Last time we scanned messages for this partition
    last_scanned_message_id UUID,             -- Last message ID we counted
    
    -- Metadata
    last_computed_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    
    PRIMARY KEY (stat_type, stat_key)
);

-- Add columns if table already exists (migration)
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns 
                   WHERE table_schema = 'queen' AND table_name = 'stats' 
                   AND column_name = 'last_scanned_at') THEN
        ALTER TABLE queen.stats ADD COLUMN last_scanned_at TIMESTAMPTZ;
        ALTER TABLE queen.stats ADD COLUMN last_scanned_message_id UUID;
    END IF;
END $$;

-- Indexes for efficient lookups
CREATE INDEX IF NOT EXISTS idx_stats_type ON queen.stats(stat_type);
CREATE INDEX IF NOT EXISTS idx_stats_queue_id ON queen.stats(queue_id) WHERE queue_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_stats_partition_id ON queen.stats(partition_id) WHERE partition_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_stats_computed ON queen.stats(last_computed_at);

-- Time-series history for throughput charts
CREATE TABLE IF NOT EXISTS queen.stats_history (
    stat_type VARCHAR(20) NOT NULL,
    stat_key VARCHAR(512) NOT NULL,
    bucket_time TIMESTAMPTZ NOT NULL,
    
    -- Snapshot of counters at this point
    total_messages BIGINT DEFAULT 0,
    pending_messages BIGINT DEFAULT 0,
    processing_messages BIGINT DEFAULT 0,
    completed_messages BIGINT DEFAULT 0,
    dead_letter_messages BIGINT DEFAULT 0,
    
    -- Delta since last bucket (for throughput)
    messages_ingested INTEGER DEFAULT 0,
    messages_processed INTEGER DEFAULT 0,
    
    -- Lag at this point
    max_lag_seconds INTEGER DEFAULT 0,
    
    PRIMARY KEY (stat_type, stat_key, bucket_time)
);

CREATE INDEX IF NOT EXISTS idx_stats_history_time ON queen.stats_history(bucket_time DESC);
CREATE INDEX IF NOT EXISTS idx_stats_history_lookup ON queen.stats_history(stat_type, stat_key, bucket_time DESC);

-- ============================================================================
-- BACKGROUND JOB PROCEDURES
-- ============================================================================

-- Compute partition-level stats using INCREMENTAL SCAN
-- Only scans NEW messages since last_scanned_at (fast!)
-- Uses partition_consumers metadata for completed/processing (no message scan needed)
CREATE OR REPLACE FUNCTION queen.compute_partition_stats_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_updated INTEGER := 0;
    v_new_partitions INTEGER := 0;
    v_now TIMESTAMPTZ := NOW();
BEGIN
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
    -- This counts messages after the cursor - small, indexed query per partition
    WITH pending_counts AS (
        SELECT 
            s.stat_key,
            s.partition_id,
            pc.consumer_group,
            pc.last_consumed_id,
            pc.last_consumed_created_at,
            pc.total_messages_consumed,
            pc.lease_expires_at,
            pc.batch_size,
            pc.acked_count,
            -- Count actual pending messages (after cursor position)
            (SELECT COUNT(*) FROM queen.messages m 
             WHERE m.partition_id = s.partition_id
               AND (pc.last_consumed_created_at IS NULL 
                    OR m.created_at > pc.last_consumed_created_at
                    OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
            ) as actual_pending
        FROM queen.stats s
        JOIN queen.partition_consumers pc 
            ON pc.partition_id = s.partition_id AND pc.consumer_group = s.consumer_group
        WHERE s.stat_type = 'partition'
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
        END
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
        -- Total messages in partition
        (SELECT COUNT(*) FROM queen.messages m WHERE m.partition_id = p.id),
        -- Pending = total - completed - dlq
        GREATEST(0, 
            (SELECT COUNT(*) FROM queen.messages m WHERE m.partition_id = p.id)
            - COALESCE(pc.total_messages_consumed, 0)
            - COALESCE((SELECT COUNT(*) FROM queen.dead_letter_queue dlq WHERE dlq.partition_id = p.id), 0)
        ),
        -- Processing
        CASE 
            WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > v_now
            THEN GREATEST(0, COALESCE(pc.batch_size, 0) - COALESCE(pc.acked_count, 0))
            ELSE 0
        END,
        -- Completed
        COALESCE(pc.total_messages_consumed, 0),
        -- Dead letter
        COALESCE((SELECT COUNT(*) FROM queen.dead_letter_queue dlq WHERE dlq.partition_id = p.id), 0),
        -- Oldest pending (use partition_lookup)
        (SELECT pl.last_message_created_at FROM queen.partition_lookup pl WHERE pl.partition_id = p.id),
        -- Newest message (use partition_lookup)
        (SELECT pl.last_message_created_at FROM queen.partition_lookup pl WHERE pl.partition_id = p.id),
        v_now,
        v_now
    FROM queen.partitions p
    JOIN queen.queues q ON q.id = p.queue_id
    LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
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
CREATE OR REPLACE FUNCTION queen.write_stats_history_v1(p_bucket_minutes INTEGER DEFAULT 1)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_bucket_time TIMESTAMPTZ;
    v_prev_bucket_time TIMESTAMPTZ;
    v_inserted INTEGER := 0;
BEGIN
    -- Round to bucket
    v_bucket_time := date_trunc('minute', NOW()) - 
        (EXTRACT(minute FROM NOW())::integer % p_bucket_minutes) * INTERVAL '1 minute';
    v_prev_bucket_time := v_bucket_time - (p_bucket_minutes || ' minutes')::INTERVAL;
    
    -- Insert system and queue stats to history
    -- Calculate delta from previous history bucket (not stats.prev_* which updates every 10s)
    -- If no previous bucket exists, set ingested/processed to 0 (avoid initial load spike)
    INSERT INTO queen.stats_history (
        stat_type, stat_key, bucket_time,
        total_messages, pending_messages, processing_messages,
        completed_messages, dead_letter_messages,
        messages_ingested, messages_processed, max_lag_seconds
    )
    SELECT 
        s.stat_type,
        s.stat_key,
        v_bucket_time,
        s.total_messages,
        s.pending_messages,
        s.processing_messages,
        s.completed_messages,
        s.dead_letter_messages,
        -- Calculate ingested as delta from previous bucket's total_messages
        -- If no previous bucket exists, use 0 to avoid counting existing messages as "ingested"
        CASE 
            WHEN EXISTS (SELECT 1 FROM queen.stats_history sh 
                        WHERE sh.stat_type = s.stat_type AND sh.stat_key = s.stat_key 
                        AND sh.bucket_time = v_prev_bucket_time)
            THEN GREATEST(0, (s.total_messages - (
                SELECT sh.total_messages FROM queen.stats_history sh 
                WHERE sh.stat_type = s.stat_type AND sh.stat_key = s.stat_key 
                AND sh.bucket_time = v_prev_bucket_time))::integer)
            ELSE 0
        END,
        -- Calculate processed as delta from previous bucket's completed_messages
        CASE 
            WHEN EXISTS (SELECT 1 FROM queen.stats_history sh 
                        WHERE sh.stat_type = s.stat_type AND sh.stat_key = s.stat_key 
                        AND sh.bucket_time = v_prev_bucket_time)
            THEN GREATEST(0, (s.completed_messages - (
                SELECT sh.completed_messages FROM queen.stats_history sh 
                WHERE sh.stat_type = s.stat_type AND sh.stat_key = s.stat_key 
                AND sh.bucket_time = v_prev_bucket_time))::integer)
            ELSE 0
        END,
        s.max_lag_seconds
    FROM queen.stats s
    WHERE s.stat_type IN ('system', 'queue')
    ON CONFLICT (stat_type, stat_key, bucket_time) DO UPDATE SET
        total_messages = EXCLUDED.total_messages,
        pending_messages = EXCLUDED.pending_messages,
        processing_messages = EXCLUDED.processing_messages,
        completed_messages = EXCLUDED.completed_messages,
        dead_letter_messages = EXCLUDED.dead_letter_messages,
        messages_ingested = EXCLUDED.messages_ingested,
        messages_processed = EXCLUDED.messages_processed,
        max_lag_seconds = EXCLUDED.max_lag_seconds;
    
    GET DIAGNOSTICS v_inserted = ROW_COUNT;
    
    RETURN jsonb_build_object('historyWritten', v_inserted, 'bucketTime', v_bucket_time);
END;
$$;

-- Cleanup old stats history
CREATE OR REPLACE FUNCTION queen.cleanup_stats_history_v1(p_retention_days INTEGER DEFAULT 7)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_deleted INTEGER := 0;
BEGIN
    DELETE FROM queen.stats_history
    WHERE bucket_time < NOW() - (p_retention_days || ' days')::INTERVAL;
    
    GET DIAGNOSTICS v_deleted = ROW_COUNT;
    
    RETURN jsonb_build_object('historyDeleted', v_deleted);
END;
$$;

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

-- queen.get_system_overview_v2: O(1) system overview
CREATE OR REPLACE FUNCTION queen.get_system_overview_v2()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_stats queen.stats;
    v_queue_count INTEGER;
    v_namespace_count INTEGER;
    v_task_count INTEGER;
BEGIN
    -- Get system stats
    SELECT * INTO v_stats 
    FROM queen.stats 
    WHERE stat_type = 'system' AND stat_key = 'global';
    
    -- Get counts
    SELECT COUNT(*) INTO v_queue_count FROM queen.queues;
    SELECT COUNT(*) INTO v_namespace_count FROM queen.stats WHERE stat_type = 'namespace';
    SELECT COUNT(*) INTO v_task_count FROM queen.stats WHERE stat_type = 'task';
    
    -- Return same format as get_system_overview_v1
    RETURN jsonb_build_object(
        'queues', v_queue_count,
        'partitions', COALESCE(v_stats.child_count, 0),
        'namespaces', v_namespace_count,
        'tasks', v_task_count,
        'messages', jsonb_build_object(
            'total', COALESCE(v_stats.total_messages, 0),
            'pending', GREATEST(0, COALESCE(v_stats.pending_messages, 0) - COALESCE(v_stats.processing_messages, 0)),
            'processing', COALESCE(v_stats.processing_messages, 0),
            'completed', COALESCE(v_stats.completed_messages, 0),
            'failed', 0,
            'deadLetter', COALESCE(v_stats.dead_letter_messages, 0)
        ),
        'lag', jsonb_build_object(
            'time', jsonb_build_object(
                'avg', COALESCE(v_stats.avg_lag_seconds, 0),
                'median', COALESCE(v_stats.median_lag_seconds, 0),
                'min', 0,
                'max', COALESCE(v_stats.max_lag_seconds, 0)
            ),
            'offset', jsonb_build_object(
                'avg', COALESCE(v_stats.avg_offset_lag, 0),
                'median', 0,
                'min', 0,
                'max', COALESCE(v_stats.max_offset_lag, 0)
            )
        ),
        'throughput', jsonb_build_object(
            'ingestedPerSecond', COALESCE(v_stats.ingested_per_second, 0),
            'processedPerSecond', COALESCE(v_stats.processed_per_second, 0)
        ),
        'timestamp', to_char(NOW(), 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
        'statsAge', CASE 
            WHEN v_stats.last_computed_at IS NOT NULL 
            THEN EXTRACT(EPOCH FROM (NOW() - v_stats.last_computed_at))::integer 
            ELSE -1 
        END
    );
END;
$$;

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
    WITH partition_stats AS (
        SELECT 
            p.id,
            p.name,
            p.created_at,
            COALESCE(s.total_messages, 0) as total,
            COALESCE(s.pending_messages, 0) as pending,
            COALESCE(s.processing_messages, 0) as processing,
            COALESCE(s.completed_messages, 0) as completed,
            0 as failed,
            COALESCE(s.dead_letter_messages, 0) as dead_letter,
            s.oldest_pending_at as oldest_message,
            s.newest_message_at as newest_message
        FROM queen.partitions p
        LEFT JOIN queen.stats s ON s.stat_type = 'partition' 
            AND s.partition_id = p.id 
            AND s.consumer_group = '__QUEUE_MODE__'
        WHERE p.queue_id = v_queue_id
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

-- queen.get_status_v2: Dashboard with throughput from stats_history
CREATE OR REPLACE FUNCTION queen.get_status_v2(p_filters JSONB DEFAULT '{}'::jsonb)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_from_ts TIMESTAMPTZ;
    v_to_ts TIMESTAMPTZ;
    v_queue TEXT;
    v_namespace TEXT;
    v_task TEXT;
    v_duration_minutes INTEGER;
    v_bucket_minutes INTEGER;
    v_throughput JSONB;
    v_queues JSONB;
    v_messages JSONB;
    v_leases JSONB;
    v_dlq JSONB;
    v_point_count INTEGER;
    v_system_stats queen.stats;
BEGIN
    -- Parse filters
    v_from_ts := COALESCE((p_filters->>'from')::timestamptz, NOW() - INTERVAL '1 hour');
    v_to_ts := COALESCE((p_filters->>'to')::timestamptz, NOW());
    v_queue := p_filters->>'queue';
    v_namespace := p_filters->>'namespace';
    v_task := p_filters->>'task';
    
    -- Calculate bucket size
    v_duration_minutes := EXTRACT(EPOCH FROM (v_to_ts - v_from_ts)) / 60;
    v_bucket_minutes := CASE
        WHEN v_duration_minutes <= 60 THEN 1
        WHEN v_duration_minutes <= 360 THEN 5
        WHEN v_duration_minutes <= 1440 THEN 15
        WHEN v_duration_minutes <= 10080 THEN 60
        ELSE 360
    END;
    
    -- Get throughput from stats_history (if filtering by queue, use queue stats)
    IF v_queue IS NOT NULL THEN
        SELECT 
            COALESCE(jsonb_agg(
                jsonb_build_object(
                    'timestamp', to_char(sh.bucket_time, 'YYYY-MM-DD"T"HH24:MI:SS"Z"'),
                    'ingested', COALESCE(sh.messages_ingested, 0),
                    'processed', COALESCE(sh.messages_processed, 0),
                    'ingestedPerSecond', ROUND(COALESCE(sh.messages_ingested, 0)::numeric / (v_bucket_minutes * 60), 2),
                    'processedPerSecond', ROUND(COALESCE(sh.messages_processed, 0)::numeric / (v_bucket_minutes * 60), 2)
                ) ORDER BY sh.bucket_time DESC
            ), '[]'::jsonb),
            COUNT(*)
        INTO v_throughput, v_point_count
        FROM queen.stats_history sh
        JOIN queen.queues q ON sh.stat_key = q.id::text
        WHERE sh.stat_type = 'queue'
          AND q.name = v_queue
          AND sh.bucket_time >= v_from_ts
          AND sh.bucket_time <= v_to_ts;
    ELSE
        -- System-wide throughput
        SELECT 
            COALESCE(jsonb_agg(
                jsonb_build_object(
                    'timestamp', to_char(sh.bucket_time, 'YYYY-MM-DD"T"HH24:MI:SS"Z"'),
                    'ingested', COALESCE(sh.messages_ingested, 0),
                    'processed', COALESCE(sh.messages_processed, 0),
                    'ingestedPerSecond', ROUND(COALESCE(sh.messages_ingested, 0)::numeric / (v_bucket_minutes * 60), 2),
                    'processedPerSecond', ROUND(COALESCE(sh.messages_processed, 0)::numeric / (v_bucket_minutes * 60), 2)
                ) ORDER BY sh.bucket_time DESC
            ), '[]'::jsonb),
            COUNT(*)
        INTO v_throughput, v_point_count
        FROM queen.stats_history sh
        WHERE sh.stat_type = 'system' AND sh.stat_key = 'global'
          AND sh.bucket_time >= v_from_ts
          AND sh.bucket_time <= v_to_ts;
    END IF;
    
    -- Get active queues from stats
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'id', q.id,
            'name', q.name,
            'namespace', q.namespace,
            'task', q.task,
            'partitions', COALESCE(s.child_count, 0),
            'totalConsumed', COALESCE(s.completed_messages, 0)
        )
    ), '[]'::jsonb) INTO v_queues
    FROM queen.queues q
    LEFT JOIN queen.stats s ON s.stat_type = 'queue' AND s.queue_id = q.id
    WHERE (v_queue IS NULL OR q.name = v_queue)
      AND (v_namespace IS NULL OR q.namespace = v_namespace)
      AND (v_task IS NULL OR q.task = v_task)
      AND COALESCE(s.total_messages, 0) > 0;
    
    -- Get message counts from system stats
    SELECT * INTO v_system_stats 
    FROM queen.stats 
    WHERE stat_type = 'system' AND stat_key = 'global';
    
    v_messages := jsonb_build_object(
        'total', COALESCE(v_system_stats.total_messages, 0),
        'pending', GREATEST(0, COALESCE(v_system_stats.pending_messages, 0) - COALESCE(v_system_stats.processing_messages, 0)),
        'processing', COALESCE(v_system_stats.processing_messages, 0),
        'completed', COALESCE(v_system_stats.completed_messages, 0),
        'failed', 0,
        'deadLetter', COALESCE(v_system_stats.dead_letter_messages, 0)
    );
    
    -- Get active leases (still need to query partition_consumers for real-time data)
    SELECT jsonb_build_object(
        'active', COUNT(*),
        'partitionsWithLeases', COUNT(DISTINCT partition_id),
        'totalBatchSize', COALESCE(SUM(batch_size), 0),
        'totalAcked', COALESCE(SUM(acked_count), 0)
    ) INTO v_leases
    FROM queen.partition_consumers
    WHERE lease_expires_at IS NOT NULL AND lease_expires_at > NOW();
    
    -- Get DLQ stats (simplified - count from system stats)
    v_dlq := jsonb_build_object(
        'totalMessages', COALESCE(v_system_stats.dead_letter_messages, 0),
        'affectedPartitions', 0,
        'topErrors', '[]'::jsonb
    );
    
    RETURN jsonb_build_object(
        'timeRange', jsonb_build_object(
            'from', to_char(v_from_ts, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'),
            'to', to_char(v_to_ts, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
        ),
        'bucketMinutes', v_bucket_minutes,
        'pointCount', COALESCE(v_point_count, 0),
        'throughput', COALESCE(v_throughput, '[]'::jsonb),
        'queues', v_queues,
        'messages', v_messages,
        'leases', v_leases,
        'deadLetterQueue', v_dlq,
        'statsAge', CASE 
            WHEN v_system_stats.last_computed_at IS NOT NULL 
            THEN EXTRACT(EPOCH FROM (NOW() - v_system_stats.last_computed_at))::integer 
            ELSE -1 
        END
    );
END;
$$;

-- queen.get_queue_detail_v2: Queue detail using pre-computed stats
-- Returns format matching frontend expectations: partition.messages.*, totals.messages.*, partition.cursor.*
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
    WITH partition_stats AS (
        SELECT 
            p.id,
            p.name,
            p.created_at,
            COALESCE(s.total_messages, 0) as total,
            COALESCE(s.pending_messages, 0) as pending,
            COALESCE(s.processing_messages, 0) as processing,
            COALESCE(s.completed_messages, 0) as completed,
            0 as failed,
            COALESCE(s.dead_letter_messages, 0) as dead_letter,
            s.oldest_pending_at as oldest_message,
            s.newest_message_at as newest_message,
            -- Cursor info from partition_consumers
            COALESCE(pc.total_messages_consumed, 0) as total_consumed,
            COALESCE(pc.total_batches_consumed, 0) as batches_consumed,
            pc.last_consumed_at as last_activity
        FROM queen.partitions p
        LEFT JOIN queen.stats s ON s.stat_type = 'partition' 
            AND s.partition_id = p.id 
            AND s.consumer_group = '__QUEUE_MODE__'
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
            AND pc.consumer_group = '__QUEUE_MODE__'
        WHERE p.queue_id = v_queue_id
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

CREATE OR REPLACE FUNCTION queen.refresh_all_stats_v1()
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_result JSONB := '{}'::jsonb;
    v_step JSONB;
BEGIN
    -- Step 1: Compute partition stats (from messages)
    SELECT queen.compute_partition_stats_v1() INTO v_step;
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
    
    -- Step 6: Write history
    SELECT queen.write_stats_history_v1() INTO v_step;
    v_result := v_result || jsonb_build_object('history', v_step);
    
    -- Step 7: Cleanup orphaned
    SELECT queen.cleanup_orphaned_stats_v1() INTO v_step;
    v_result := v_result || jsonb_build_object('cleanup', v_step);
    
    RETURN v_result;
END;
$$;

-- ============================================================================
-- GRANT PERMISSIONS
-- ============================================================================

GRANT EXECUTE ON FUNCTION queen.compute_partition_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.aggregate_queue_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.aggregate_namespace_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.aggregate_task_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.aggregate_system_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.write_stats_history_v1(INTEGER) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.cleanup_stats_history_v1(INTEGER) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.cleanup_orphaned_stats_v1() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.refresh_all_stats_v1() TO PUBLIC;

GRANT EXECUTE ON FUNCTION queen.get_system_overview_v2() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_queues_v2() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_queue_v2(TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_namespaces_v2() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_tasks_v2() TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_status_v2(JSONB) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_queue_detail_v2(TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.get_status_queues_v2(JSONB, INTEGER, INTEGER) TO PUBLIC;

