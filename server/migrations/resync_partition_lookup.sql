-- ============================================================================
-- Post-Deploy: Resync partition_lookup table
-- Purpose: Ensure lookup table is in sync after code deployment
-- Can be run multiple times safely (idempotent)
-- ============================================================================

BEGIN;

-- Update existing entries with latest message data
WITH latest_messages AS (
    SELECT DISTINCT ON (m.partition_id)
        q.name as queue_name,
        p.id as partition_id,
        m.id as last_message_id,
        m.created_at as last_message_created_at
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    ORDER BY m.partition_id, m.created_at DESC, m.id DESC
)
INSERT INTO queen.partition_lookup (queue_name, partition_id, last_message_id, last_message_created_at)
SELECT queue_name, partition_id, last_message_id, last_message_created_at
FROM latest_messages
ON CONFLICT (queue_name, partition_id) 
DO UPDATE SET
    last_message_id = EXCLUDED.last_message_id,
    last_message_created_at = EXCLUDED.last_message_created_at,
    updated_at = NOW()
WHERE 
    -- Only update if the incoming message is actually newer
    EXCLUDED.last_message_created_at > partition_lookup.last_message_created_at
    OR (EXCLUDED.last_message_created_at = partition_lookup.last_message_created_at 
        AND EXCLUDED.last_message_id > partition_lookup.last_message_id);

COMMIT;

-- Verification query
SELECT 
    pl.queue_name,
    pl.partition_id,
    pl.last_message_id,
    pl.last_message_created_at,
    pl.updated_at,
    -- Verify against actual latest message
    (SELECT COUNT(*) FROM queen.messages m 
     WHERE m.partition_id = pl.partition_id 
       AND (m.created_at > pl.last_message_created_at 
            OR (m.created_at = pl.last_message_created_at AND m.id > pl.last_message_id))
    ) as messages_newer_than_lookup
FROM queen.partition_lookup pl
ORDER BY pl.queue_name, pl.last_message_created_at DESC
LIMIT 20;

-- Should return 0 for all partitions
SELECT 
    COUNT(*) as partitions_with_stale_lookup
FROM queen.partition_lookup pl
WHERE EXISTS (
    SELECT 1 FROM queen.messages m 
    WHERE m.partition_id = pl.partition_id 
      AND (m.created_at > pl.last_message_created_at 
           OR (m.created_at = pl.last_message_created_at AND m.id > pl.last_message_id))
);

