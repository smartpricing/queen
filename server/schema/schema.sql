-- ============================================================================
-- Queen Message Queue - Consolidated Database Schema
-- Version: 0 (base schema)
-- ============================================================================
--
-- This file contains the complete current schema for Queen MQ.
-- It is designed to be idempotent (safe to run multiple times).
--
-- Usage:
-- - Fresh install: Run this file once to create all objects
-- - Upgrades: Apply migrations from server/migrations/ with version > 0
--
-- ============================================================================

-- Create schema
DROP SCHEMA IF EXISTS queen CASCADE; 
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
CREATE INDEX IF NOT EXISTS idx_messages_partition_created_id ON queen.messages(partition_id, created_at, id);
CREATE INDEX IF NOT EXISTS idx_messages_transaction_id ON queen.messages(transaction_id);


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

-- Stored procedure indexes
CREATE INDEX IF NOT EXISTS idx_messages_txn_partition ON queen.messages(transaction_id, partition_id);
CREATE INDEX IF NOT EXISTS idx_messages_partition_created ON queen.messages (partition_id, created_at, id);
CREATE INDEX IF NOT EXISTS idx_partition_lookup_queue_message_ts ON queen.partition_lookup(queue_name, last_message_created_at DESC);

-- ============================================================================
-- Trigger Functions
-- ============================================================================

-- Partition lookup trigger (statement-level, batch-efficient)
-- NOTE: ORDER BY partition_id ensures consistent lock ordering to prevent deadlocks
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
    ORDER BY bm.partition_id  -- Consistent lock ordering prevents deadlocks
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




-- ============================================================================
-- System State Initialization
-- ============================================================================

INSERT INTO queen.system_state (key, value, updated_at)
VALUES ('maintenance_mode', '{"enabled": false}'::jsonb, NOW())
ON CONFLICT (key) DO NOTHING;

-- ============================================================================
-- Mark Schema Version
-- ============================================================================
-- This is the base schema (version 0).
-- Future migrations go in server/migrations/ with version > 0.
-- The application will check this table and apply any missing migrations.

INSERT INTO queen.schema_version (version, description)
VALUES (0, 'Base schema')
ON CONFLICT (version) DO NOTHING;
