-- Queen Message Queue Schema V2
-- Structure: Queues → Partitions → Messages

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
    created_at TIMESTAMP DEFAULT NOW()
);

-- Partitions table (subdivisions of queues, where FIFO happens)
CREATE TABLE IF NOT EXISTS queen.partitions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL DEFAULT 'Default',
    created_at TIMESTAMP DEFAULT NOW(),
    last_activity TIMESTAMP DEFAULT NOW(),
    UNIQUE(queue_id, name)
);

-- Messages table (stored in partitions)
CREATE TABLE IF NOT EXISTS queen.messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    transaction_id UUID UNIQUE NOT NULL,
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    payload JSONB NOT NULL,
    status VARCHAR(20) DEFAULT 'pending',
    worker_id VARCHAR(255),
    created_at TIMESTAMP DEFAULT NOW(),
    locked_at TIMESTAMP,
    completed_at TIMESTAMP,
    failed_at TIMESTAMP,
    error_message TEXT,
    retry_count INTEGER DEFAULT 0,
    lease_expires_at TIMESTAMP,
    processing_at TIMESTAMP,
    is_encrypted BOOLEAN DEFAULT FALSE
);

-- Indexes for queues
CREATE INDEX IF NOT EXISTS idx_queues_name ON queen.queues(name);
CREATE INDEX IF NOT EXISTS idx_queues_priority ON queen.queues(priority DESC);
CREATE INDEX IF NOT EXISTS idx_queues_namespace ON queen.queues(namespace) WHERE namespace IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_task ON queen.queues(task) WHERE task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_namespace_task ON queen.queues(namespace, task) WHERE namespace IS NOT NULL AND task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_namespace_priority ON queen.queues(namespace, priority DESC) WHERE namespace IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_task_priority ON queen.queues(task, priority DESC) WHERE task IS NOT NULL;
-- New indexes for configuration columns
CREATE INDEX IF NOT EXISTS idx_queues_retention_enabled ON queen.queues(retention_enabled) WHERE retention_enabled = true;
CREATE INDEX IF NOT EXISTS idx_queues_lease_time ON queen.queues(lease_time);
CREATE INDEX IF NOT EXISTS idx_queues_ttl ON queen.queues(ttl);

-- Indexes for partitions
CREATE INDEX IF NOT EXISTS idx_partitions_queue_name ON queen.partitions(queue_id, name);
-- Priority index removed - partitions no longer have priority (queue-level only)

-- Indexes for messages (performance-critical)
CREATE INDEX IF NOT EXISTS idx_messages_partition_status_created ON queen.messages(partition_id, status, created_at);
CREATE INDEX IF NOT EXISTS idx_messages_status_created ON queen.messages(status, created_at);
CREATE INDEX IF NOT EXISTS idx_messages_transaction_id ON queen.messages(transaction_id);
CREATE INDEX IF NOT EXISTS idx_messages_lease_expires ON queen.messages(lease_expires_at) WHERE status = 'processing';

-- Optimized indexes for high-throughput message consumption
CREATE INDEX IF NOT EXISTS idx_messages_pop_partition 
ON queen.messages(partition_id, status, created_at) 
WHERE status = 'pending';

-- Index for queue-level pops (across partitions)
CREATE INDEX IF NOT EXISTS idx_messages_pop_queue 
ON queen.messages(status, created_at) 
INCLUDE (partition_id)
WHERE status = 'pending';

-- Partial index for faster pending message counts
CREATE INDEX IF NOT EXISTS idx_messages_pending_count 
ON queen.messages(partition_id) 
WHERE status = 'pending';

-- Index to speed up lease expiration checks
CREATE INDEX IF NOT EXISTS idx_messages_lease_check 
ON queen.messages(lease_expires_at, status) 
WHERE status = 'processing';

-- Indexes for analytics
CREATE INDEX IF NOT EXISTS idx_messages_created_at ON queen.messages(created_at);
CREATE INDEX IF NOT EXISTS idx_messages_completed_at_status ON queen.messages(completed_at, status) 
WHERE completed_at IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_messages_processing_at ON queen.messages(processing_at) 
WHERE processing_at IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_messages_lag_calculation ON queen.messages(completed_at, created_at, status) 
WHERE completed_at IS NOT NULL AND created_at IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_messages_failed_status ON queen.messages(completed_at, status) 
WHERE status = 'failed';

-- Retention support indexes
CREATE INDEX IF NOT EXISTS idx_messages_retention 
ON queen.messages(partition_id, created_at, status)
WHERE status IN ('completed', 'failed');

-- Eviction support indexes
CREATE INDEX IF NOT EXISTS idx_messages_eviction 
ON queen.messages(partition_id, status, created_at)
WHERE status = 'pending';

-- Retention history table for tracking deletions
CREATE TABLE IF NOT EXISTS queen.retention_history (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    messages_deleted INTEGER DEFAULT 0,
    retention_type VARCHAR(50), -- 'retention', 'completed', 'evicted'
    executed_at TIMESTAMP DEFAULT NOW()
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

