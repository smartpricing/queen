-- ════════════════════════════════════════════════════════════════
-- Queen Message Queue Schema V3
-- Simplified: Merged cursor+lease, removed messages_status
-- ════════════════════════════════════════════════════════════════

CREATE SCHEMA IF NOT EXISTS queen;

-- ────────────────────────────────────────────────────────────────
-- 1. QUEUES - Top-level configuration
-- ────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS queen.queues (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) UNIQUE NOT NULL,
    namespace VARCHAR(255),
    task VARCHAR(255),
    priority INTEGER DEFAULT 0,
    
    -- Processing configuration
    lease_time INTEGER DEFAULT 300,
    retry_limit INTEGER DEFAULT 3,
    retry_delay INTEGER DEFAULT 1000,
    max_size INTEGER DEFAULT 10000,
    ttl INTEGER DEFAULT 3600,
    dead_letter_queue BOOLEAN DEFAULT FALSE,
    dlq_after_max_retries BOOLEAN DEFAULT FALSE,
    delayed_processing INTEGER DEFAULT 0,
    window_buffer INTEGER DEFAULT 0,
    
    -- Enterprise features
    retention_seconds INTEGER DEFAULT 0,
    completed_retention_seconds INTEGER DEFAULT 0,
    retention_enabled BOOLEAN DEFAULT FALSE,
    encryption_enabled BOOLEAN DEFAULT FALSE,
    max_wait_time_seconds INTEGER DEFAULT 0,
    max_queue_size INTEGER DEFAULT 0,
    
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Indexes for queues
CREATE INDEX IF NOT EXISTS idx_queues_name ON queen.queues(name);
CREATE INDEX IF NOT EXISTS idx_queues_priority ON queen.queues(priority DESC);
CREATE INDEX IF NOT EXISTS idx_queues_namespace ON queen.queues(namespace) WHERE namespace IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_task ON queen.queues(task) WHERE task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_namespace_task ON queen.queues(namespace, task) 
WHERE namespace IS NOT NULL AND task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_retention_enabled ON queen.queues(retention_enabled) WHERE retention_enabled = true;

-- ────────────────────────────────────────────────────────────────
-- 2. PARTITIONS - FIFO subdivisions within queues
-- ────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS queen.partitions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL DEFAULT 'Default',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    last_activity TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(queue_id, name)
);

CREATE INDEX IF NOT EXISTS idx_partitions_queue_name 
ON queen.partitions(queue_id, name);

CREATE INDEX IF NOT EXISTS idx_partitions_last_activity 
ON queen.partitions(last_activity);

-- ────────────────────────────────────────────────────────────────
-- 3. MESSAGES - Immutable message data
-- ────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS queen.messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    transaction_id VARCHAR(255) UNIQUE NOT NULL,
    trace_id UUID DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    payload JSONB NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    is_encrypted BOOLEAN DEFAULT FALSE
);

-- Indexes for messages (critical for cursor-based queries!)
CREATE INDEX IF NOT EXISTS idx_messages_partition_created_id 
ON queen.messages(partition_id, created_at, id);

CREATE INDEX IF NOT EXISTS idx_messages_transaction_id 
ON queen.messages(transaction_id);

CREATE INDEX IF NOT EXISTS idx_messages_trace_id 
ON queen.messages(trace_id);

CREATE INDEX IF NOT EXISTS idx_messages_created_at 
ON queen.messages(created_at);

-- ────────────────────────────────────────────────────────────────
-- 4. PARTITION_CONSUMERS - Unified cursor + lease tracking
-- ────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS queen.partition_consumers (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',
    
    -- ═══════════════════════════════════════════════════════════
    -- CURSOR STATE (Persistent - consumption progress)
    -- ═══════════════════════════════════════════════════════════
    last_consumed_id UUID DEFAULT '00000000-0000-0000-0000-000000000000',
    last_consumed_created_at TIMESTAMPTZ,
    total_messages_consumed BIGINT DEFAULT 0,
    total_batches_consumed BIGINT DEFAULT 0,
    last_consumed_at TIMESTAMPTZ,
    
    -- ═══════════════════════════════════════════════════════════
    -- LEASE STATE (Ephemeral - active processing lock)
    -- ═══════════════════════════════════════════════════════════
    lease_expires_at TIMESTAMPTZ,
    lease_acquired_at TIMESTAMPTZ,
    message_batch JSONB,
    batch_size INTEGER DEFAULT 0,
    acked_count INTEGER DEFAULT 0,
    worker_id VARCHAR(255),
    
    -- ═══════════════════════════════════════════════════════════
    -- STATISTICS (for dashboard - avoids expensive COUNT queries)
    -- ═══════════════════════════════════════════════════════════
    pending_estimate BIGINT DEFAULT 0,
    last_stats_update TIMESTAMPTZ,
    
    -- ═══════════════════════════════════════════════════════════
    -- METADATA
    -- ═══════════════════════════════════════════════════════════
    created_at TIMESTAMPTZ DEFAULT NOW(),
    
    UNIQUE(partition_id, consumer_group),
    
    -- Constraint: cursor consistency
    CHECK (
        (last_consumed_id = '00000000-0000-0000-0000-000000000000' 
         AND last_consumed_created_at IS NULL)
        OR 
        (last_consumed_id != '00000000-0000-0000-0000-000000000000' 
         AND last_consumed_created_at IS NOT NULL)
    )
);

-- Indexes for partition_consumers (critical for performance!)
CREATE INDEX IF NOT EXISTS idx_partition_consumers_lookup
ON queen.partition_consumers(partition_id, consumer_group);

CREATE INDEX IF NOT EXISTS idx_partition_consumers_active_leases
ON queen.partition_consumers(partition_id, consumer_group, lease_expires_at)
WHERE lease_expires_at IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_partition_consumers_expired_leases
ON queen.partition_consumers(lease_expires_at)
WHERE lease_expires_at IS NOT NULL;

CREATE INDEX IF NOT EXISTS idx_partition_consumers_progress
ON queen.partition_consumers(last_consumed_at DESC);

CREATE INDEX IF NOT EXISTS idx_partition_consumers_idle
ON queen.partition_consumers(partition_id, consumer_group)
WHERE lease_expires_at IS NULL;

CREATE INDEX IF NOT EXISTS idx_partition_consumers_consumer_group
ON queen.partition_consumers(consumer_group);

-- ────────────────────────────────────────────────────────────────
-- 5. DEAD_LETTER_QUEUE - Failed messages
-- ────────────────────────────────────────────────────────────────
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

CREATE INDEX IF NOT EXISTS idx_dlq_partition
ON queen.dead_letter_queue(partition_id);

CREATE INDEX IF NOT EXISTS idx_dlq_consumer_group
ON queen.dead_letter_queue(consumer_group);

CREATE INDEX IF NOT EXISTS idx_dlq_failed_at
ON queen.dead_letter_queue(failed_at DESC);

CREATE INDEX IF NOT EXISTS idx_dlq_message_consumer
ON queen.dead_letter_queue(message_id, consumer_group);

-- ────────────────────────────────────────────────────────────────
-- 6. RETENTION_HISTORY - Optional audit of cleanup operations
-- ────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS queen.retention_history (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    messages_deleted INTEGER DEFAULT 0,
    retention_type VARCHAR(50), -- 'retention', 'completed', 'evicted'
    executed_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_retention_history_partition 
ON queen.retention_history(partition_id);

CREATE INDEX IF NOT EXISTS idx_retention_history_executed 
ON queen.retention_history(executed_at);

-- ────────────────────────────────────────────────────────────────
-- TRIGGERS
-- ────────────────────────────────────────────────────────────────

-- Update partition last_activity on message insert
CREATE OR REPLACE FUNCTION update_partition_last_activity()
RETURNS TRIGGER AS $$
BEGIN
    UPDATE queen.partitions 
    SET last_activity = NOW() 
    WHERE id = NEW.partition_id;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trigger_update_partition_activity ON queen.messages;
CREATE TRIGGER trigger_update_partition_activity
AFTER INSERT ON queen.messages
FOR EACH ROW
EXECUTE FUNCTION update_partition_last_activity();

-- Update pending_estimate on message insert
CREATE OR REPLACE FUNCTION update_pending_on_push()
RETURNS TRIGGER AS $$
BEGIN
    -- Increment pending estimate for all consumers on this partition
    UPDATE queen.partition_consumers
    SET pending_estimate = pending_estimate + 1,
        last_stats_update = NOW()
    WHERE partition_id = NEW.partition_id;
    
    -- If no consumers exist yet, that's OK - they'll be created on first POP
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trigger_update_pending_on_push ON queen.messages;
CREATE TRIGGER trigger_update_pending_on_push
AFTER INSERT ON queen.messages
FOR EACH ROW
EXECUTE FUNCTION update_pending_on_push();
