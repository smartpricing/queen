-- Queen Message Queue Schema V2
-- Structure: Queues → Partitions → Messages

CREATE SCHEMA IF NOT EXISTS queen;

-- Queues table (top level, with optional grouping)
CREATE TABLE IF NOT EXISTS queen.queues (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) UNIQUE NOT NULL,
    namespace VARCHAR(255),  -- optional grouping
    task VARCHAR(255),       -- optional grouping
    priority INTEGER DEFAULT 0,  -- Queue priority (higher = processed first)
    created_at TIMESTAMP DEFAULT NOW()
);

-- Partitions table (subdivisions of queues, where FIFO happens)
CREATE TABLE IF NOT EXISTS queen.partitions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL DEFAULT 'Default',
    priority INTEGER DEFAULT 0,
    options JSONB DEFAULT '{"leaseTime": 300, "retryLimit": 3}',
    created_at TIMESTAMP DEFAULT NOW(),
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
    processing_at TIMESTAMP
);

-- Indexes for queues
CREATE INDEX IF NOT EXISTS idx_queues_name ON queen.queues(name);
CREATE INDEX IF NOT EXISTS idx_queues_priority ON queen.queues(priority DESC);
CREATE INDEX IF NOT EXISTS idx_queues_namespace ON queen.queues(namespace) WHERE namespace IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_task ON queen.queues(task) WHERE task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_namespace_task ON queen.queues(namespace, task) WHERE namespace IS NOT NULL AND task IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_namespace_priority ON queen.queues(namespace, priority DESC) WHERE namespace IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_queues_task_priority ON queen.queues(task, priority DESC) WHERE task IS NOT NULL;

-- Indexes for partitions
CREATE INDEX IF NOT EXISTS idx_partitions_queue_name ON queen.partitions(queue_id, name);
CREATE INDEX IF NOT EXISTS idx_partitions_priority ON queen.partitions(priority DESC);

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

