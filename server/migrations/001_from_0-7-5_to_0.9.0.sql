-- ============================================================================
-- Queen Schema Migration: Master → Tasync (Cleanup)
-- Run this AFTER deploying tasync to production
-- 
-- This migration removes objects that exist in master but are NOT used by tasync.
-- It's safe to run after tasync is deployed because:
--   1. Tasync's schema.sql uses CREATE OR REPLACE for objects it needs
--   2. This script only drops objects tasync doesn't use
--   3. All DROP statements use IF EXISTS for idempotency
-- ============================================================================

BEGIN;

-- 1. DROP STREAMING TABLES (CASCADE handles FKs)
DROP TABLE IF EXISTS queen.stream_leases CASCADE;
DROP TABLE IF EXISTS queen.stream_consumer_offsets CASCADE;
DROP TABLE IF EXISTS queen.stream_sources CASCADE;
DROP TABLE IF EXISTS queen.streams CASCADE;
DROP TABLE IF EXISTS queen.queue_watermarks CASCADE;

-- 2. DROP MASTER-ONLY INDEXES
DROP INDEX IF EXISTS queen.idx_queue_watermarks_name;
DROP INDEX IF EXISTS queen.idx_stream_leases_lookup;
DROP INDEX IF EXISTS queen.idx_stream_leases_expires;
DROP INDEX IF EXISTS queen.idx_stream_consumer_offsets_lookup;
DROP INDEX IF EXISTS queen.idx_queues_priority;
DROP INDEX IF EXISTS queen.idx_queues_namespace;
DROP INDEX IF EXISTS queen.idx_queues_task;
DROP INDEX IF EXISTS queen.idx_queues_namespace_task;
DROP INDEX IF EXISTS queen.idx_queues_retention_enabled;
DROP INDEX IF EXISTS queen.idx_partitions_queue_name;
DROP INDEX IF EXISTS queen.idx_partitions_last_activity;
DROP INDEX IF EXISTS queen.idx_messages_trace_id;
DROP INDEX IF EXISTS queen.idx_messages_created_at;
DROP INDEX IF EXISTS queen.idx_partition_consumers_lookup;
DROP INDEX IF EXISTS queen.idx_partition_consumers_active_leases;
DROP INDEX IF EXISTS queen.idx_partition_consumers_expired_leases;
DROP INDEX IF EXISTS queen.idx_partition_consumers_progress;
DROP INDEX IF EXISTS queen.idx_partition_consumers_idle;
DROP INDEX IF EXISTS queen.idx_partition_consumers_consumer_group;
DROP INDEX IF EXISTS queen.idx_partition_lookup_queue_name;
DROP INDEX IF EXISTS queen.idx_partition_lookup_partition_id;
DROP INDEX IF EXISTS queen.idx_partition_lookup_timestamp;

-- 3. DROP MASTER-ONLY TRIGGERS (triggers that exist in master but NOT in tasync)
-- NOTE: trg_update_partition_lookup is NOT dropped here - it's preserved and updated by schema.sql
DROP TRIGGER IF EXISTS trigger_update_partition_activity ON queen.messages;
DROP TRIGGER IF EXISTS trigger_update_pending_on_push ON queen.messages;
DROP TRIGGER IF EXISTS trigger_update_watermark ON queen.messages;

-- 4. DROP MASTER-ONLY TRIGGER FUNCTIONS
DROP FUNCTION IF EXISTS update_partition_last_activity();
DROP FUNCTION IF EXISTS update_pending_on_push();
DROP FUNCTION IF EXISTS update_queue_watermark();

COMMIT;

-- Verify cleanup
SELECT 'Tables remaining:' AS info;
SELECT tablename FROM pg_tables WHERE schemaname = 'queen' ORDER BY tablename;

SELECT 'Indexes remaining:' AS info;
SELECT indexname FROM pg_indexes WHERE schemaname = 'queen' ORDER BY indexname;

SELECT 'Triggers remaining:' AS info;
SELECT tgname FROM pg_trigger t
JOIN pg_class c ON t.tgrelid = c.oid
JOIN pg_namespace n ON c.relnamespace = n.oid
WHERE n.nspname = 'queen' AND NOT tgisinternal;