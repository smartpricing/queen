-- ============================================================================
-- Queen Message Queue - Consolidated Database Schema
-- Version: 9 (includes all stored procedures up to pop_messages_batch_v2)
-- ============================================================================
--
-- This file contains the complete current schema for Queen MQ.
-- It is designed to be idempotent (safe to run multiple times).
--
-- Usage:
-- - Fresh install: Run this file once to create all objects
-- - Upgrades: Apply migrations from server/migrations/ with version > 9
--
-- ============================================================================

-- Create schema
CREATE SCHEMA IF NOT EXISTS queen;

-- ============================================================================
-- Schema Version Tracking
-- ============================================================================
CREATE TABLE IF NOT EXISTS queen.schema_version (
    version INT PRIMARY KEY,
    description TEXT NOT NULL,
    applied_at TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================================================
-- Core Tables
-- ============================================================================

CREATE TABLE IF NOT EXISTS queen.queues (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) UNIQUE NOT NULL,
    namespace VARCHAR(255),
    task VARCHAR(255),
    priority INTEGER DEFAULT 0,
    lease_time INTEGER DEFAULT 300,
    retry_limit INTEGER DEFAULT 3,
    retry_delay INTEGER DEFAULT 1000,
    ttl INTEGER DEFAULT 3600,
    dead_letter_queue BOOLEAN DEFAULT FALSE,
    dlq_after_max_retries BOOLEAN DEFAULT FALSE,
    delayed_processing INTEGER DEFAULT 0,
    window_buffer INTEGER DEFAULT 0,
    retention_seconds INTEGER DEFAULT 0,
    completed_retention_seconds INTEGER DEFAULT 0,
    retention_enabled BOOLEAN DEFAULT FALSE,
    encryption_enabled BOOLEAN DEFAULT FALSE,
    max_wait_time_seconds INTEGER DEFAULT 0,
    max_queue_size INTEGER DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS queen.partitions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL DEFAULT 'Default',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    last_activity TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(queue_id, name)
);

CREATE TABLE IF NOT EXISTS queen.messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    transaction_id VARCHAR(255) NOT NULL,
    trace_id UUID DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    payload JSONB NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    is_encrypted BOOLEAN DEFAULT FALSE
);

-- Unique constraint scoped to partition (not global)
CREATE UNIQUE INDEX IF NOT EXISTS messages_partition_transaction_unique 
    ON queen.messages(partition_id, transaction_id);

CREATE TABLE IF NOT EXISTS queen.partition_consumers (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',
    last_consumed_id UUID DEFAULT '00000000-0000-0000-0000-000000000000',
    last_consumed_created_at TIMESTAMPTZ,
    total_messages_consumed BIGINT DEFAULT 0,
    total_batches_consumed BIGINT DEFAULT 0,
    last_consumed_at TIMESTAMPTZ,
    lease_expires_at TIMESTAMPTZ,
    lease_acquired_at TIMESTAMPTZ,
    message_batch JSONB,
    batch_size INTEGER DEFAULT 0,
    acked_count INTEGER DEFAULT 0,
    worker_id VARCHAR(255),
    pending_estimate BIGINT DEFAULT 0,
    last_stats_update TIMESTAMPTZ,
    batch_retry_count INTEGER DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(partition_id, consumer_group),
    CHECK (
        (last_consumed_id = '00000000-0000-0000-0000-000000000000' 
         AND last_consumed_created_at IS NULL)
        OR 
        (last_consumed_id != '00000000-0000-0000-0000-000000000000' 
         AND last_consumed_created_at IS NOT NULL)
    )
);

CREATE TABLE IF NOT EXISTS queen.consumer_groups_metadata (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    consumer_group TEXT NOT NULL,
    queue_name TEXT NOT NULL DEFAULT '',
    partition_name TEXT NOT NULL DEFAULT '',
    namespace TEXT NOT NULL DEFAULT '',
    task TEXT NOT NULL DEFAULT '',
    subscription_mode TEXT NOT NULL,
    subscription_timestamp TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (consumer_group, queue_name, partition_name, namespace, task)
);

CREATE INDEX IF NOT EXISTS idx_consumer_groups_metadata_lookup 
ON queen.consumer_groups_metadata(consumer_group, queue_name, namespace, task);

CREATE TABLE IF NOT EXISTS queen.messages_consumed (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) NOT NULL,
    messages_completed INTEGER DEFAULT 0,
    messages_failed INTEGER DEFAULT 0,
    acked_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS queen.dead_letter_queue (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    message_id UUID REFERENCES queen.messages(id) ON DELETE CASCADE,
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255),
    error_message TEXT,
    retry_count INTEGER DEFAULT 0,
    original_created_at TIMESTAMPTZ,
    failed_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS queen.retention_history (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    messages_deleted INTEGER DEFAULT 0,
    retention_type VARCHAR(50),
    executed_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS queen.system_metrics (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    timestamp TIMESTAMPTZ NOT NULL,
    hostname TEXT NOT NULL,
    port INTEGER NOT NULL,
    worker_id TEXT NOT NULL,
    sample_count INTEGER NOT NULL DEFAULT 60,
    metrics JSONB NOT NULL,
    CONSTRAINT unique_metric_per_replica 
        UNIQUE (timestamp, hostname, port, worker_id)
);

CREATE TABLE IF NOT EXISTS queen.message_traces (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    message_id UUID REFERENCES queen.messages(id) ON DELETE CASCADE,
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    transaction_id VARCHAR(255) NOT NULL,
    consumer_group VARCHAR(255),
    event_type VARCHAR(100),
    data JSONB NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    worker_id VARCHAR(255)
);

CREATE TABLE IF NOT EXISTS queen.message_trace_names (
    trace_id UUID REFERENCES queen.message_traces(id) ON DELETE CASCADE,
    trace_name TEXT NOT NULL,
    PRIMARY KEY (trace_id, trace_name)
);

-- System state table for shared configuration across instances
CREATE TABLE IF NOT EXISTS queen.system_state (
    key TEXT PRIMARY KEY,
    value JSONB NOT NULL,
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================================================
-- Streaming Tables
-- ============================================================================

CREATE TABLE IF NOT EXISTS queen.streams (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) UNIQUE NOT NULL,
    namespace VARCHAR(255) NOT NULL,
    partitioned BOOLEAN NOT NULL DEFAULT FALSE,
    window_type VARCHAR(50) NOT NULL,
    window_duration_ms BIGINT, 
    window_size_count INT,
    window_slide_ms BIGINT,
    window_slide_count INT,
    window_grace_period_ms BIGINT NOT NULL DEFAULT 30000,
    window_lease_timeout_ms BIGINT NOT NULL DEFAULT 30000,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS queen.stream_sources (
    stream_id UUID NOT NULL REFERENCES queen.streams(id) ON DELETE CASCADE,
    queue_id UUID NOT NULL REFERENCES queen.queues(id) ON DELETE CASCADE,
    PRIMARY KEY (stream_id, queue_id)
);

CREATE TABLE IF NOT EXISTS queen.stream_consumer_offsets (
    stream_id UUID REFERENCES queen.streams(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) NOT NULL,
    stream_key TEXT NOT NULL,
    last_acked_window_end TIMESTAMPTZ,
    last_acked_message_id UUID,
    total_windows_consumed BIGINT DEFAULT 0,
    last_consumed_at TIMESTAMPTZ,
    PRIMARY KEY (stream_id, consumer_group, stream_key)
);

CREATE TABLE IF NOT EXISTS queen.stream_leases (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    stream_id UUID REFERENCES queen.streams(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) NOT NULL,
    stream_key TEXT NOT NULL, 
    window_start TIMESTAMPTZ NOT NULL,
    window_end TIMESTAMPTZ NOT NULL,
    lease_id UUID NOT NULL UNIQUE,
    lease_consumer_id VARCHAR(255),
    lease_expires_at TIMESTAMPTZ NOT NULL,
    UNIQUE(stream_id, consumer_group, stream_key, window_start, window_end)
);

CREATE TABLE IF NOT EXISTS queen.queue_watermarks (
    queue_id UUID PRIMARY KEY REFERENCES queen.queues(id) ON DELETE CASCADE,
    queue_name VARCHAR(255) NOT NULL,
    max_created_at TIMESTAMPTZ NOT NULL DEFAULT '-infinity'::timestamptz,
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS queen.partition_lookup (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_name VARCHAR(255) NOT NULL REFERENCES queen.queues(name) ON DELETE CASCADE,
    partition_id UUID NOT NULL REFERENCES queen.partitions(id) ON DELETE CASCADE,
    last_message_id UUID NOT NULL,
    last_message_created_at TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(queue_name, partition_id)
);

-- ============================================================================
-- Indexes
-- ============================================================================

CREATE INDEX IF NOT EXISTS idx_queues_name ON queen.queues(name);
CREATE INDEX IF NOT EXISTS idx_queues_priority ON queen.queues(priority DESC);
CREATE INDEX IF NOT EXISTS idx_queues_namespace ON queen.queues(namespace) WHERE namespace IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_task ON queen.queues(task) WHERE task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_namespace_task ON queen.queues(namespace, task) WHERE namespace IS NOT NULL AND task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_retention_enabled ON queen.queues(retention_enabled) WHERE retention_enabled = true;
CREATE INDEX IF NOT EXISTS idx_partitions_queue_name ON queen.partitions(queue_id, name);
CREATE INDEX IF NOT EXISTS idx_partitions_last_activity ON queen.partitions(last_activity);
CREATE INDEX IF NOT EXISTS idx_messages_partition_created_id ON queen.messages(partition_id, created_at, id);
CREATE INDEX IF NOT EXISTS idx_messages_transaction_id ON queen.messages(transaction_id);
CREATE INDEX IF NOT EXISTS idx_messages_trace_id ON queen.messages(trace_id);
CREATE INDEX IF NOT EXISTS idx_messages_created_at ON queen.messages(created_at);
CREATE INDEX IF NOT EXISTS idx_partition_consumers_lookup ON queen.partition_consumers(partition_id, consumer_group);
CREATE INDEX IF NOT EXISTS idx_partition_consumers_active_leases ON queen.partition_consumers(partition_id, consumer_group, lease_expires_at) WHERE lease_expires_at IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_partition_consumers_expired_leases ON queen.partition_consumers(lease_expires_at) WHERE lease_expires_at IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_partition_consumers_progress ON queen.partition_consumers(last_consumed_at DESC);
CREATE INDEX IF NOT EXISTS idx_partition_consumers_idle ON queen.partition_consumers(partition_id, consumer_group) WHERE lease_expires_at IS NULL;
CREATE INDEX IF NOT EXISTS idx_partition_consumers_consumer_group ON queen.partition_consumers(consumer_group);
CREATE INDEX IF NOT EXISTS idx_messages_consumed_acked_at ON queen.messages_consumed(acked_at DESC);
CREATE INDEX IF NOT EXISTS idx_messages_consumed_partition_acked ON queen.messages_consumed(partition_id, acked_at DESC);
CREATE INDEX IF NOT EXISTS idx_messages_consumed_consumer_acked ON queen.messages_consumed(consumer_group, acked_at DESC);
CREATE INDEX IF NOT EXISTS idx_messages_consumed_partition_id ON queen.messages_consumed(partition_id);
CREATE INDEX IF NOT EXISTS idx_dlq_partition ON queen.dead_letter_queue(partition_id);
CREATE INDEX IF NOT EXISTS idx_dlq_consumer_group ON queen.dead_letter_queue(consumer_group);
CREATE INDEX IF NOT EXISTS idx_dlq_failed_at ON queen.dead_letter_queue(failed_at DESC);
CREATE INDEX IF NOT EXISTS idx_dlq_message_consumer ON queen.dead_letter_queue(message_id, consumer_group);
CREATE INDEX IF NOT EXISTS idx_retention_history_partition ON queen.retention_history(partition_id);
CREATE INDEX IF NOT EXISTS idx_retention_history_executed ON queen.retention_history(executed_at);
CREATE INDEX IF NOT EXISTS idx_system_metrics_timestamp ON queen.system_metrics(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_system_metrics_replica ON queen.system_metrics(hostname, port);
CREATE INDEX IF NOT EXISTS idx_system_metrics_worker ON queen.system_metrics(worker_id);
CREATE INDEX IF NOT EXISTS idx_system_metrics_metrics ON queen.system_metrics USING GIN (metrics);
CREATE INDEX IF NOT EXISTS idx_message_traces_message_id ON queen.message_traces(message_id);
CREATE INDEX IF NOT EXISTS idx_message_traces_transaction_partition ON queen.message_traces(transaction_id, partition_id);
CREATE INDEX IF NOT EXISTS idx_message_traces_created_at ON queen.message_traces(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_message_trace_names_name ON queen.message_trace_names(trace_name);
CREATE INDEX IF NOT EXISTS idx_message_trace_names_trace_id ON queen.message_trace_names(trace_id);
CREATE INDEX IF NOT EXISTS idx_system_state_key ON queen.system_state(key);
CREATE INDEX IF NOT EXISTS idx_partition_lookup_queue_name ON queen.partition_lookup(queue_name);
CREATE INDEX IF NOT EXISTS idx_partition_lookup_partition_id ON queen.partition_lookup(partition_id);
CREATE INDEX IF NOT EXISTS idx_partition_lookup_timestamp ON queen.partition_lookup(last_message_created_at DESC);
CREATE INDEX IF NOT EXISTS idx_queue_watermarks_name ON queen.queue_watermarks(queue_name);
CREATE INDEX IF NOT EXISTS idx_stream_leases_lookup ON queen.stream_leases(stream_id, consumer_group, stream_key, window_start);
CREATE INDEX IF NOT EXISTS idx_stream_leases_expires ON queen.stream_leases(lease_expires_at);
CREATE INDEX IF NOT EXISTS idx_stream_consumer_offsets_lookup ON queen.stream_consumer_offsets(stream_id, consumer_group, stream_key);

-- Stored procedure indexes
CREATE INDEX IF NOT EXISTS idx_messages_txn_partition ON queen.messages(transaction_id, partition_id);
CREATE INDEX IF NOT EXISTS idx_messages_partition_created ON queen.messages (partition_id, created_at, id);
CREATE INDEX IF NOT EXISTS idx_partition_consumers_worker ON queen.partition_consumers (worker_id) WHERE worker_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_partition_consumers_worker_id ON queen.partition_consumers (worker_id) WHERE worker_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_messages_transaction_partition ON queen.messages (transaction_id, partition_id);
CREATE INDEX IF NOT EXISTS idx_partition_lookup_queue_message_ts ON queen.partition_lookup(queue_name, last_message_created_at DESC);

-- ============================================================================
-- Trigger Functions
-- ============================================================================

-- Statement-level trigger for partition last_activity (batch-efficient)
CREATE OR REPLACE FUNCTION update_partition_last_activity()
RETURNS TRIGGER AS $$
BEGIN
    UPDATE queen.partitions 
    SET last_activity = NOW() 
    WHERE id IN (SELECT DISTINCT partition_id FROM new_messages);
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- Statement-level trigger for pending estimate (batch-efficient)
CREATE OR REPLACE FUNCTION update_pending_on_push()
RETURNS TRIGGER AS $$
BEGIN
    UPDATE queen.partition_consumers pc
    SET pending_estimate = pc.pending_estimate + msg_counts.count,
        last_stats_update = NOW()
    FROM (
        SELECT partition_id, COUNT(*) as count
        FROM new_messages
        GROUP BY partition_id
    ) msg_counts
    WHERE pc.partition_id = msg_counts.partition_id;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- Streaming watermark trigger function (statement-level for batch efficiency)
CREATE OR REPLACE FUNCTION update_queue_watermark()
RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO queen.queue_watermarks (queue_id, queue_name, max_created_at)
    SELECT 
        q.id,
        q.name,
        MAX(nm.created_at) as max_created_at
    FROM new_messages nm
    JOIN queen.partitions p ON p.id = nm.partition_id
    JOIN queen.queues q ON p.queue_id = q.id
    GROUP BY q.id, q.name
    ON CONFLICT (queue_id)
    DO UPDATE SET
        max_created_at = GREATEST(queen.queue_watermarks.max_created_at, EXCLUDED.max_created_at),
        updated_at = NOW();
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- Partition lookup trigger (statement-level, batch-efficient)
CREATE OR REPLACE FUNCTION queen.update_partition_lookup_trigger()
RETURNS TRIGGER AS $$
BEGIN
    WITH batch_max AS (
        SELECT DISTINCT ON (partition_id)
            partition_id, 
            created_at as max_created_at,
            id as max_id
        FROM new_messages
        ORDER BY partition_id, created_at DESC, id DESC
    )
    INSERT INTO queen.partition_lookup (
        queue_name, partition_id, last_message_id, last_message_created_at, updated_at
    )
    SELECT 
        q.name, 
        bm.partition_id, 
        bm.max_id, 
        bm.max_created_at, 
        NOW()
    FROM batch_max bm
    JOIN queen.partitions p ON p.id = bm.partition_id
    JOIN queen.queues q ON q.id = p.queue_id
    ON CONFLICT (queue_name, partition_id)
    DO UPDATE SET
        last_message_id = EXCLUDED.last_message_id,
        last_message_created_at = EXCLUDED.last_message_created_at,
        updated_at = NOW()
    WHERE 
        EXCLUDED.last_message_created_at > queen.partition_lookup.last_message_created_at
        OR (EXCLUDED.last_message_created_at = queen.partition_lookup.last_message_created_at 
            AND EXCLUDED.last_message_id > queen.partition_lookup.last_message_id);
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- ============================================================================
-- Triggers
-- ============================================================================

DROP TRIGGER IF EXISTS trigger_update_partition_activity ON queen.messages;
CREATE TRIGGER trigger_update_partition_activity
AFTER INSERT ON queen.messages
REFERENCING NEW TABLE AS new_messages
FOR EACH STATEMENT
EXECUTE FUNCTION update_partition_last_activity();

DROP TRIGGER IF EXISTS trigger_update_pending_on_push ON queen.messages;
CREATE TRIGGER trigger_update_pending_on_push
AFTER INSERT ON queen.messages
REFERENCING NEW TABLE AS new_messages
FOR EACH STATEMENT
EXECUTE FUNCTION update_pending_on_push();

DROP TRIGGER IF EXISTS trigger_update_watermark ON queen.messages;
CREATE TRIGGER trigger_update_watermark
AFTER INSERT ON queen.messages
REFERENCING NEW TABLE AS new_messages
FOR EACH STATEMENT
EXECUTE FUNCTION update_queue_watermark();

DROP TRIGGER IF EXISTS trg_update_partition_lookup ON queen.messages;
CREATE TRIGGER trg_update_partition_lookup
AFTER INSERT ON queen.messages
REFERENCING NEW TABLE AS new_messages
FOR EACH STATEMENT
EXECUTE FUNCTION queen.update_partition_lookup_trigger();

-- ============================================================================
-- System State Initialization
-- ============================================================================

INSERT INTO queen.system_state (key, value, updated_at)
VALUES ('maintenance_mode', '{"enabled": false}'::jsonb, NOW())
ON CONFLICT (key) DO NOTHING;

-- ============================================================================
-- Mark Schema Version
-- ============================================================================
-- This is the base schema (version 9).
-- Future migrations go in server/migrations/ with version > 9.
-- The application will check this table and apply any missing migrations.

INSERT INTO queen.schema_version (version, description)
VALUES (9, 'Consolidated base schema with all stored procedures')
ON CONFLICT (version) DO NOTHING;
