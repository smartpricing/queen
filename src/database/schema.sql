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
