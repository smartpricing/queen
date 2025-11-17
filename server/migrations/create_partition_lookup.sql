-- ============================================================================
-- Migration: Create partition_lookup table
-- Date: 2025-11-14
-- Priority: HIGH - Performance Critical
-- Downtime Required: No
-- ============================================================================

BEGIN;

-- Step 1: Create the lookup table
CREATE TABLE IF NOT EXISTS queen.partition_lookup (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    queue_name VARCHAR(255) NOT NULL REFERENCES queen.queues(name) ON DELETE CASCADE,
    partition_id UUID NOT NULL REFERENCES queen.partitions(id) ON DELETE CASCADE,
    last_message_id UUID NOT NULL,
    last_message_created_at TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(queue_name, partition_id)
);

-- Step 2: Create indexes for optimal query performance
CREATE INDEX IF NOT EXISTS idx_partition_lookup_queue_name 
    ON queen.partition_lookup(queue_name);

CREATE INDEX IF NOT EXISTS idx_partition_lookup_partition_id 
    ON queen.partition_lookup(partition_id);

CREATE INDEX IF NOT EXISTS idx_partition_lookup_timestamp 
    ON queen.partition_lookup(last_message_created_at DESC);

-- Step 3: Populate with existing data (MIGRATION FOR OLD DATA)
-- This finds the last message per partition and inserts into lookup table
INSERT INTO queen.partition_lookup (queue_name, partition_id, last_message_id, last_message_created_at)
SELECT 
    q.name as queue_name,
    p.id as partition_id,
    m.id as last_message_id,
    m.created_at as last_message_created_at
FROM queen.partitions p
JOIN queen.queues q ON p.queue_id = q.id
JOIN LATERAL (
    SELECT id, created_at
    FROM queen.messages
    WHERE partition_id = p.id
    ORDER BY created_at DESC, id DESC
    LIMIT 1
) m ON true
ON CONFLICT (queue_name, partition_id) DO NOTHING;

-- Step 4: Create trigger function to maintain lookup table automatically
CREATE OR REPLACE FUNCTION queen.update_partition_lookup_trigger()
RETURNS TRIGGER AS $$
BEGIN
    -- Aggregate the batch to find the latest message per partition
    WITH batch_max AS (
        SELECT DISTINCT ON (partition_id)
            partition_id, 
            created_at as max_created_at,
            id as max_id
        FROM new_messages  -- Contains all inserted rows in this batch
        ORDER BY partition_id, created_at DESC, id DESC
    )
    -- Update lookup table once per partition involved in this batch
    INSERT INTO queen.partition_lookup (
        queue_name, partition_id, last_message_id, last_message_created_at, updated_at
    )
    SELECT 
        q.name, 
        bm.partition_id, 
        bm.max_id, 
        bm.max_created_at, 
        NOW()
    FROM batch_max bm
    JOIN queen.partitions p ON p.id = bm.partition_id
    JOIN queen.queues q ON q.id = p.queue_id
    ON CONFLICT (queue_name, partition_id)
    DO UPDATE SET
        last_message_id = EXCLUDED.last_message_id,
        last_message_created_at = EXCLUDED.last_message_created_at,
        updated_at = NOW()
    WHERE 
        -- Only update if the new message is actually newer (handles out-of-order commits)
        EXCLUDED.last_message_created_at > queen.partition_lookup.last_message_created_at
        OR (EXCLUDED.last_message_created_at = queen.partition_lookup.last_message_created_at 
            AND EXCLUDED.last_message_id > queen.partition_lookup.last_message_id);
            
    RETURN NULL; -- Result is ignored for statement-level triggers
END;
$$ LANGUAGE plpgsql;

-- Step 5: Create the trigger
DROP TRIGGER IF EXISTS trg_update_partition_lookup ON queen.messages;
CREATE TRIGGER trg_update_partition_lookup
AFTER INSERT ON queen.messages
REFERENCING NEW TABLE AS new_messages  -- Batch-aware: processes all inserted rows at once
FOR EACH STATEMENT                     -- Fires once per INSERT statement (not per row)
EXECUTE FUNCTION queen.update_partition_lookup_trigger();

COMMIT;

-- Verify the migration
SELECT 
    COUNT(*) as total_partitions,
    COUNT(pl.id) as partitions_with_messages
FROM queen.partitions p
LEFT JOIN queen.partition_lookup pl ON pl.partition_id = p.id;

-- Check trigger was created
SELECT 
    'Trigger created: ' || tgname as status,
    CASE 
        WHEN tgtype & 1 = 1 THEN 'statement-level ✓'
        ELSE 'row-level (should be statement-level!)'
    END as trigger_type
FROM pg_trigger 
WHERE tgname = 'trg_update_partition_lookup';

