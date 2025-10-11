-- Queen Message Queue Schema V2 with Bus/Consumer Group Support
-- Structure: Queues → Partitions → Messages → Status (per consumer group)

-- Create schema only if it doesn't exist (preserves existing data)
CREATE SCHEMA IF NOT EXISTS queen;

-- Queues table (top level, with all configuration options)
CREATE TABLE IF NOT EXISTS queen.queues (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) UNIQUE NOT NULL,
    namespace VARCHAR(255),  -- optional grouping
    task VARCHAR(255),       -- optional grouping
    priority INTEGER DEFAULT 0,  -- Queue priority (higher = processed first)
    -- Configuration options (all at queue level now)
    lease_time INTEGER DEFAULT 300,
    retry_limit INTEGER DEFAULT 3,
    retry_delay INTEGER DEFAULT 1000,
    max_size INTEGER DEFAULT 10000,
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
    max_queue_size INTEGER DEFAULT 0,  -- 0 = unlimited queue size
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Partitions table (subdivisions of queues, where FIFO happens)
CREATE TABLE IF NOT EXISTS queen.partitions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL DEFAULT 'Default',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    last_activity TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(queue_id, name)
);

-- Messages table (immutable message data only)
CREATE TABLE IF NOT EXISTS queen.messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    transaction_id VARCHAR(255) UNIQUE NOT NULL,  -- Changed from UUID to string for flexibility
    trace_id UUID DEFAULT gen_random_uuid(),  -- For tracking multi-step pipelines
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    payload JSONB NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    is_encrypted BOOLEAN DEFAULT FALSE
    -- Removed all status fields - moved to messages_status table
);

-- Messages status table (tracks consumption per consumer group)
CREATE TABLE IF NOT EXISTS queen.messages_status (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    message_id UUID REFERENCES queen.messages(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255),  -- NULL for queue mode (competing consumers)
    status VARCHAR(20) DEFAULT 'pending',
    worker_id VARCHAR(255),
    locked_at TIMESTAMPTZ,
    completed_at TIMESTAMPTZ,
    failed_at TIMESTAMPTZ,
    error_message TEXT,
    retry_count INTEGER DEFAULT 0,
    lease_expires_at TIMESTAMPTZ,
    processing_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(message_id, consumer_group)  -- One status per message per consumer group
);

-- Consumer groups registry (tracks subscription preferences)
CREATE TABLE IF NOT EXISTS queen.consumer_groups (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    subscription_start_from TIMESTAMPTZ,  -- NULL = consume all, timestamp = consume from this point
    active BOOLEAN DEFAULT true,
    last_seen_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(queue_id, name)
);

-- Partition leases table (tracks which partition is locked for which consumer group)
CREATE TABLE IF NOT EXISTS queen.partition_leases (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',  -- '__QUEUE_MODE__' for queue mode
    lease_expires_at TIMESTAMPTZ NOT NULL,
    message_batch JSONB,  -- Store IDs of messages in this lease (for exclusion)
    batch_size INTEGER DEFAULT 0,  -- Total number of messages in the batch
    acked_count INTEGER DEFAULT 0,  -- Number of messages acknowledged so far
    created_at TIMESTAMPTZ DEFAULT NOW(),
    released_at TIMESTAMPTZ,  -- When lease was released (NULL if active)
    UNIQUE(partition_id, consumer_group)  -- One active lease per partition per consumer group
);

-- Indexes for queues
CREATE INDEX IF NOT EXISTS idx_queues_name ON queen.queues(name);
CREATE INDEX IF NOT EXISTS idx_queues_priority ON queen.queues(priority DESC);
CREATE INDEX IF NOT EXISTS idx_queues_namespace ON queen.queues(namespace) WHERE namespace IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_task ON queen.queues(task) WHERE task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_namespace_task ON queen.queues(namespace, task) WHERE namespace IS NOT NULL AND task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_namespace_priority ON queen.queues(namespace, priority DESC) WHERE namespace IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_task_priority ON queen.queues(task, priority DESC) WHERE task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_retention_enabled ON queen.queues(retention_enabled) WHERE retention_enabled = true;
CREATE INDEX IF NOT EXISTS idx_queues_lease_time ON queen.queues(lease_time);
CREATE INDEX IF NOT EXISTS idx_queues_ttl ON queen.queues(ttl);

-- Indexes for partitions
CREATE INDEX IF NOT EXISTS idx_partitions_queue_name ON queen.partitions(queue_id, name);

-- Indexes for messages (simplified - no status fields)
CREATE INDEX IF NOT EXISTS idx_messages_partition_created ON queen.messages(partition_id, created_at);
CREATE INDEX IF NOT EXISTS idx_messages_transaction_id ON queen.messages(transaction_id);
CREATE INDEX IF NOT EXISTS idx_messages_trace_id ON queen.messages(trace_id);
CREATE INDEX IF NOT EXISTS idx_messages_created_at ON queen.messages(created_at);

-- Indexes for messages_status (critical for performance)
-- Note: Removed the partial unique index idx_status_queue_mode as it conflicts with 
-- the table-level UNIQUE(message_id, consumer_group) constraint

-- Bus mode index (consumer_group IS NOT NULL)
CREATE INDEX IF NOT EXISTS idx_status_bus_mode 
ON queen.messages_status(message_id, consumer_group, status) 
WHERE consumer_group IS NOT NULL;

-- Index for finding pending messages by consumer group
CREATE INDEX IF NOT EXISTS idx_status_pending 
ON queen.messages_status(consumer_group, status, message_id) 
WHERE status = 'pending';

-- Index for lease expiration checks
CREATE INDEX IF NOT EXISTS idx_status_lease_expires 
ON queen.messages_status(lease_expires_at, status, consumer_group) 
WHERE status = 'processing';

-- Index for fast queue depth checks
CREATE INDEX IF NOT EXISTS idx_messages_status_pending_processing 
ON queen.messages_status(message_id, status) 
WHERE status IN ('pending', 'processing') OR status IS NULL;

-- Index for efficient pop operations (find messages without status for a consumer group)
CREATE INDEX IF NOT EXISTS idx_status_message_consumer 
ON queen.messages_status(message_id, consumer_group);

-- Indexes for consumer groups
CREATE INDEX IF NOT EXISTS idx_consumer_groups_queue_name 
ON queen.consumer_groups(queue_id, name);

CREATE INDEX IF NOT EXISTS idx_consumer_groups_active 
ON queen.consumer_groups(queue_id, active) 
WHERE active = true;

-- Indexes for partition leases
CREATE INDEX IF NOT EXISTS idx_partition_leases_expires 
ON queen.partition_leases(lease_expires_at) 
WHERE released_at IS NULL;

CREATE INDEX IF NOT EXISTS idx_partition_leases_active 
ON queen.partition_leases(partition_id, consumer_group) 
WHERE released_at IS NULL;

CREATE INDEX IF NOT EXISTS idx_partition_leases_active_lookup 
ON queen.partition_leases(partition_id, consumer_group, lease_expires_at) 
WHERE released_at IS NULL;

-- Retention history table for tracking deletions
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

-- Trigger to update partition last_activity
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

-- Function to clean up old completed statuses in bus mode
CREATE OR REPLACE FUNCTION cleanup_completed_statuses()
RETURNS void AS $$
BEGIN
    -- Delete completed statuses older than retention period for bus mode
    DELETE FROM queen.messages_status ms
    USING queen.messages m, queen.partitions p, queen.queues q
    WHERE ms.message_id = m.id
      AND m.partition_id = p.id
      AND p.queue_id = q.id
      AND ms.status IN ('completed', 'failed')
      AND ms.consumer_group IS NOT NULL  -- Only for bus mode
      AND q.completed_retention_seconds > 0
      AND ms.completed_at < NOW() - INTERVAL '1 second' * q.completed_retention_seconds;
END;
$$ LANGUAGE plpgsql;