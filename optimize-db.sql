-- Performance optimization indexes for high-throughput message consumption
-- Run this script to improve pop operation performance

-- Composite index for efficient message retrieval
-- This index covers the WHERE clause and ORDER BY for queue-specific pops
CREATE INDEX IF NOT EXISTS idx_messages_pop_queue 
ON queen.messages(queue_id, status, created_at) 
WHERE status = 'pending';

-- Index for namespace-level and task-level pops with priority ordering
CREATE INDEX IF NOT EXISTS idx_messages_pop_priority 
ON queen.messages(status, created_at) 
INCLUDE (queue_id)
WHERE status = 'pending';

-- Partial index for faster pending message counts
CREATE INDEX IF NOT EXISTS idx_messages_pending_count 
ON queen.messages(queue_id) 
WHERE status = 'pending';

-- Index to speed up lease expiration checks
CREATE INDEX IF NOT EXISTS idx_messages_lease_check 
ON queen.messages(lease_expires_at, status) 
WHERE status = 'processing';

-- Update table statistics for better query planning
ANALYZE queen.messages;
ANALYZE queen.queues;
ANALYZE queen.tasks;
ANALYZE queen.namespaces;

-- Show current index usage stats (optional - for monitoring)
SELECT 
    schemaname,
    tablename,
    indexname,
    idx_scan as index_scans,
    idx_tup_read as tuples_read,
    idx_tup_fetch as tuples_fetched,
    pg_size_pretty(pg_relation_size(indexrelid)) as index_size
FROM pg_stat_user_indexes 
WHERE schemaname = 'queen'
ORDER BY idx_scan DESC;
