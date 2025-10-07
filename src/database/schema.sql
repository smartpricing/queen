-- Queen Message Queue Schema
CREATE SCHEMA IF NOT EXISTS queen;

-- Namespaces table
CREATE TABLE IF NOT EXISTS queen.namespaces (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) UNIQUE NOT NULL,
    created_at TIMESTAMP DEFAULT NOW()
);

-- Tasks table
CREATE TABLE IF NOT EXISTS queen.tasks (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    namespace_id UUID REFERENCES queen.namespaces(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT NOW(),
    UNIQUE(namespace_id, name)
);

-- Queues table
CREATE TABLE IF NOT EXISTS queen.queues (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    task_id UUID REFERENCES queen.tasks(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL,
    priority INTEGER DEFAULT 0,
    options JSONB DEFAULT '{"leaseTime": 300, "retryLimit": 3}',
    created_at TIMESTAMP DEFAULT NOW(),
    UNIQUE(task_id, name)
);

-- Messages table
CREATE TABLE IF NOT EXISTS queen.messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    transaction_id UUID UNIQUE NOT NULL,
    queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
    payload JSONB NOT NULL,
    status VARCHAR(20) DEFAULT 'pending',
    worker_id VARCHAR(255),
    created_at TIMESTAMP DEFAULT NOW(),
    locked_at TIMESTAMP,
    completed_at TIMESTAMP,
    failed_at TIMESTAMP,
    error_message TEXT,
    retry_count INTEGER DEFAULT 0,
    lease_expires_at TIMESTAMP
);

-- Indexes for performance
CREATE INDEX IF NOT EXISTS idx_messages_queue_status_created ON queen.messages(queue_id, status, created_at);
CREATE INDEX IF NOT EXISTS idx_messages_status_created ON queen.messages(status, created_at);
CREATE INDEX IF NOT EXISTS idx_messages_transaction_id ON queen.messages(transaction_id);
CREATE INDEX IF NOT EXISTS idx_messages_lease_expires ON queen.messages(lease_expires_at) WHERE status = 'processing';
CREATE INDEX IF NOT EXISTS idx_namespaces_name ON queen.namespaces(name);
CREATE INDEX IF NOT EXISTS idx_tasks_namespace_name ON queen.tasks(namespace_id, name);
CREATE INDEX IF NOT EXISTS idx_queues_task_name ON queen.queues(task_id, name);
CREATE INDEX IF NOT EXISTS idx_queues_priority ON queen.queues(priority DESC);


-- Additional indexes for enhanced throughput analytics
-- These indexes optimize the new throughput queries that calculate incoming, completed, processing, failed, and lag metrics

-- Index for incoming messages (created_at queries)
CREATE INDEX IF NOT EXISTS idx_messages_created_at ON queen.messages(created_at);

-- Index for completed messages with status
CREATE INDEX IF NOT EXISTS idx_messages_completed_at_status ON queen.messages(completed_at, status) 
WHERE completed_at IS NOT NULL;

-- Index for processing messages (processing_at column needs to be added first)
-- Check if processing_at column exists, if not add it
DO $$ 
BEGIN
    IF NOT EXISTS (SELECT 1 
                   FROM information_schema.columns 
                   WHERE table_schema = 'queen' 
                   AND table_name = 'messages' 
                   AND column_name = 'processing_at') THEN
        ALTER TABLE queen.messages ADD COLUMN processing_at TIMESTAMP;
        -- Set processing_at to locked_at for existing processing messages
        UPDATE queen.messages SET processing_at = locked_at WHERE status = 'processing' AND locked_at IS NOT NULL;
    END IF;
END $$;

-- Now create the index for processing_at
CREATE INDEX IF NOT EXISTS idx_messages_processing_at ON queen.messages(processing_at) 
WHERE processing_at IS NOT NULL;

-- Composite index for lag calculation (completed_at and created_at together)
CREATE INDEX IF NOT EXISTS idx_messages_lag_calculation ON queen.messages(completed_at, created_at, status) 
WHERE completed_at IS NOT NULL AND created_at IS NOT NULL;

-- Index for failed messages
CREATE INDEX IF NOT EXISTS idx_messages_failed_status ON queen.messages(completed_at, status) 
WHERE status = 'failed';

-- Note: We cannot create partial indexes with NOW() as it's not immutable
-- Instead, the regular indexes on created_at and completed_at will be used
-- The query planner will efficiently use these indexes for time-range queries

-- Analyze the table to update statistics for query planner
ANALYZE queen.messages;

-- Note: These indexes will improve query performance but will slightly increase write overhead.
-- Monitor your database size and performance after adding these indexes.
-- Consider dropping indexes that are not being used by checking pg_stat_user_indexes.
