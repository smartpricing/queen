-- Migration: Add batch_retry_count to partition_consumers
-- This tracks how many times a batch has been retried for total batch failures
-- to respect the queue's retryLimit configuration

-- Add batch_retry_count column to partition_consumers
ALTER TABLE queen.partition_consumers 
ADD COLUMN IF NOT EXISTS batch_retry_count INTEGER DEFAULT 0;

-- Add comment for documentation
COMMENT ON COLUMN queen.partition_consumers.batch_retry_count IS 
'Tracks retry count for total batch failures to respect queue retryLimit';

-- Create index for performance (optional, but good for monitoring queries)
CREATE INDEX IF NOT EXISTS idx_partition_consumers_batch_retry_count 
ON queen.partition_consumers(batch_retry_count) 
WHERE batch_retry_count > 0;
