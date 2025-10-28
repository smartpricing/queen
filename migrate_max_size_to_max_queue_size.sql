-- Migration: Consolidate max_size and max_queue_size columns
-- The server was using both max_size and max_queue_size inconsistently
-- This migration removes max_size and uses max_queue_size exclusively

-- Step 1: Copy any max_size values to max_queue_size (if max_queue_size is still 0)
UPDATE queen.queues
SET max_queue_size = max_size
WHERE max_queue_size = 0 AND max_size > 0;

-- Step 2: Drop the old max_size column (if it exists)
ALTER TABLE queen.queues DROP COLUMN IF EXISTS max_size;

-- Verify the migration
SELECT name, max_queue_size FROM queen.queues LIMIT 10;

