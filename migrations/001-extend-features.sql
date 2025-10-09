-- Queen Message Queue Extension Features Migration
-- Adds support for: Encryption, Retention, and Eviction

-- 1. ENCRYPTION SUPPORT
-- Add encryption flag to queues table
ALTER TABLE queen.queues 
ADD COLUMN IF NOT EXISTS encryption_enabled BOOLEAN DEFAULT FALSE;

-- Add encrypted flag to messages for tracking
ALTER TABLE queen.messages 
ADD COLUMN IF NOT EXISTS is_encrypted BOOLEAN DEFAULT FALSE;

-- 2. RETENTION SUPPORT
-- Add last_activity timestamp to partitions for retention
ALTER TABLE queen.partitions 
ADD COLUMN IF NOT EXISTS last_activity TIMESTAMP DEFAULT NOW();

-- Create retention tracking table
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

-- Add index for efficient retention queries
CREATE INDEX IF NOT EXISTS idx_messages_retention 
ON queen.messages(partition_id, created_at, status)
WHERE status IN ('completed', 'failed');

-- 3. EVICTION SUPPORT
-- Add maxWaitTimeSeconds to queue configuration
ALTER TABLE queen.queues 
ADD COLUMN IF NOT EXISTS max_wait_time_seconds INTEGER DEFAULT 0;

-- Note: Message status is already VARCHAR(20), can handle 'evicted' status

-- Add index for eviction queries
CREATE INDEX IF NOT EXISTS idx_messages_eviction 
ON queen.messages(partition_id, status, created_at)
WHERE status = 'pending';

-- Update partition last_activity trigger
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
