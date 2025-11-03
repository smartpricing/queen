-- Verification script for optimized triggers
-- This script checks if the triggers are properly configured as statement-level

\echo '==================================================================='
\echo 'Checking trigger configuration for queen.messages table'
\echo '==================================================================='
\echo ''

SELECT 
    trigger_name,
    action_timing,
    action_orientation,
    event_manipulation,
    CASE 
        WHEN action_orientation = 'STATEMENT' THEN '✓ OPTIMIZED'
        WHEN action_orientation = 'ROW' THEN '✗ NEEDS MIGRATION'
        ELSE '? UNKNOWN'
    END as status
FROM information_schema.triggers 
WHERE event_object_table = 'messages' 
  AND event_object_schema = 'queen'
  AND trigger_name IN (
      'trigger_update_partition_activity',
      'trigger_update_pending_on_push',
      'trigger_update_watermark'
  )
ORDER BY trigger_name;

\echo ''
\echo '==================================================================='
\echo 'Expected: All triggers should show "STATEMENT" orientation'
\echo '==================================================================='
\echo ''

-- Check if any triggers are still row-level
SELECT 
    CASE 
        WHEN COUNT(*) = 0 THEN '✓ SUCCESS: All triggers are optimized (STATEMENT-level)'
        ELSE '✗ FAILED: ' || COUNT(*) || ' trigger(s) still using ROW-level (needs migration)'
    END as overall_status
FROM information_schema.triggers 
WHERE event_object_table = 'messages' 
  AND event_object_schema = 'queen'
  AND trigger_name IN (
      'trigger_update_partition_activity',
      'trigger_update_pending_on_push',
      'trigger_update_watermark'
  )
  AND action_orientation = 'ROW';

\echo ''
\echo '==================================================================='
\echo 'Trigger function details'
\echo '==================================================================='
\echo ''

-- Show trigger function definitions to confirm they use transition tables
SELECT 
    p.proname as function_name,
    pg_get_functiondef(p.oid) as function_definition
FROM pg_proc p
JOIN pg_namespace n ON p.pronamespace = n.oid
WHERE n.nspname = 'queen'
  AND p.proname IN (
      'update_partition_last_activity',
      'update_pending_on_push',
      'update_queue_watermark'
  )
ORDER BY p.proname;

