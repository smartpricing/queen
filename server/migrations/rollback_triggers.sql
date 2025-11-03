-- ROLLBACK SCRIPT (NOT RECOMMENDED)
-- This script reverts the optimized statement-level triggers back to row-level triggers
-- 
-- WARNING: This will restore the lock contention issue under high load
-- Only use this for debugging or if you encounter unexpected issues

-- ============================================================================
-- 1. Rollback partition last_activity trigger to row-level
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.update_partition_last_activity()
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
EXECUTE FUNCTION queen.update_partition_last_activity();

-- ============================================================================
-- 2. Rollback pending estimate trigger to row-level
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.update_pending_on_push()
RETURNS TRIGGER AS $$
BEGIN
    UPDATE queen.partition_consumers
    SET pending_estimate = pending_estimate + 1,
        last_stats_update = NOW()
    WHERE partition_id = NEW.partition_id;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trigger_update_pending_on_push ON queen.messages;
CREATE TRIGGER trigger_update_pending_on_push
AFTER INSERT ON queen.messages
FOR EACH ROW
EXECUTE FUNCTION queen.update_pending_on_push();

-- ============================================================================
-- 3. Rollback watermark trigger to row-level
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.update_queue_watermark()
RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO queen.queue_watermarks (queue_id, queue_name, max_created_at)
    SELECT 
        q.id,
        q.name,
        NEW.created_at
    FROM queen.partitions p
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE p.id = NEW.partition_id
    ON CONFLICT (queue_id)
    DO UPDATE SET
        max_created_at = GREATEST(queue_watermarks.max_created_at, EXCLUDED.max_created_at),
        updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

DROP TRIGGER IF EXISTS trigger_update_watermark ON queen.messages;
CREATE TRIGGER trigger_update_watermark
AFTER INSERT ON queen.messages
FOR EACH ROW
EXECUTE FUNCTION queen.update_queue_watermark();

-- ============================================================================
-- Verification
-- ============================================================================
\echo 'Rollback complete. Triggers are now row-level.'
\echo 'WARNING: This configuration will cause lock contention under high load!'
\echo ''
\echo 'To verify the rollback, run: psql $DATABASE_URL -f verify_triggers.sql'

