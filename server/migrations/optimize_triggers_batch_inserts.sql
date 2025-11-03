-- Migration: Optimize triggers for batch insert performance
-- Date: 2025-11-03
-- Description: Convert row-level triggers to statement-level triggers with transition tables
--              to eliminate lock contention during batch inserts under high load
-- 
-- Impact: Dramatically reduces lock contention on partitions, partition_consumers, and 
--         queue_watermarks tables during batch message inserts

-- ============================================================================
-- 1. Optimize partition last_activity trigger
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.update_partition_last_activity()
RETURNS TRIGGER AS $$
BEGIN
    -- Update last_activity for all affected partitions in the batch
    UPDATE queen.partitions 
    SET last_activity = NOW() 
    WHERE id IN (SELECT DISTINCT partition_id FROM new_messages);
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trigger_update_partition_activity ON queen.messages;
CREATE TRIGGER trigger_update_partition_activity
AFTER INSERT ON queen.messages
REFERENCING NEW TABLE AS new_messages
FOR EACH STATEMENT
EXECUTE FUNCTION queen.update_partition_last_activity();

-- ============================================================================
-- 2. Optimize pending estimate trigger
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.update_pending_on_push()
RETURNS TRIGGER AS $$
BEGIN
    -- Increment pending_estimate by the count of messages per partition
    UPDATE queen.partition_consumers pc
    SET pending_estimate = pc.pending_estimate + msg_counts.count,
        last_stats_update = NOW()
    FROM (
        SELECT partition_id, COUNT(*) as count
        FROM new_messages
        GROUP BY partition_id
    ) msg_counts
    WHERE pc.partition_id = msg_counts.partition_id;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trigger_update_pending_on_push ON queen.messages;
CREATE TRIGGER trigger_update_pending_on_push
AFTER INSERT ON queen.messages
REFERENCING NEW TABLE AS new_messages
FOR EACH STATEMENT
EXECUTE FUNCTION queen.update_pending_on_push();

-- ============================================================================
-- 3. Optimize watermark trigger (critical for streaming)
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.update_queue_watermark()
RETURNS TRIGGER AS $$
BEGIN
    -- Process all inserted rows in the batch at once
    INSERT INTO queen.queue_watermarks (queue_id, queue_name, max_created_at)
    SELECT 
        q.id,
        q.name,
        MAX(nm.created_at) as max_created_at
    FROM new_messages nm
    JOIN queen.partitions p ON p.id = nm.partition_id
    JOIN queen.queues q ON p.queue_id = q.id
    GROUP BY q.id, q.name
    ON CONFLICT (queue_id)
    DO UPDATE SET
        max_created_at = GREATEST(queue_watermarks.max_created_at, EXCLUDED.max_created_at),
        updated_at = NOW();
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trigger_update_watermark ON queen.messages;
CREATE TRIGGER trigger_update_watermark
AFTER INSERT ON queen.messages
REFERENCING NEW TABLE AS new_messages
FOR EACH STATEMENT
EXECUTE FUNCTION queen.update_queue_watermark();

-- ============================================================================
-- Verification
-- ============================================================================
-- You can verify the triggers are now statement-level with:
-- SELECT trigger_name, action_timing, action_orientation 
-- FROM information_schema.triggers 
-- WHERE event_object_table = 'messages' 
--   AND event_object_schema = 'queen';
-- 
-- Expected output:
-- trigger_update_partition_activity | AFTER | STATEMENT
-- trigger_update_pending_on_push     | AFTER | STATEMENT
-- trigger_update_watermark           | AFTER | STATEMENT

