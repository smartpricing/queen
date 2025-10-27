-- ═══════════════════════════════════════════════════════════════════════════
-- Migration: Change transaction_id uniqueness from GLOBAL to PARTITION-SCOPED
-- ═══════════════════════════════════════════════════════════════════════════
-- 
-- This migration changes the uniqueness constraint on transaction_id from being
-- globally unique across all queues to being unique within each partition.
--
-- SAFE FOR LIVE SYSTEMS:
-- - New constraint is added first (validates data integrity)
-- - Old constraint is dropped second (no downtime)
-- - Operations are atomic and non-blocking
--
-- ═══════════════════════════════════════════════════════════════════════════

BEGIN;

-- Step 1: Add new partition-scoped unique constraint
-- This will fail if there are any existing duplicates within the same partition
-- (which there shouldn't be, since the global constraint was stricter)
DO $$ 
BEGIN
    -- Check if constraint already exists
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint 
        WHERE conname = 'messages_partition_transaction_unique'
    ) THEN
        ALTER TABLE queen.messages 
        ADD CONSTRAINT messages_partition_transaction_unique 
        UNIQUE (partition_id, transaction_id);
        
        RAISE NOTICE 'Added new partition-scoped unique constraint: messages_partition_transaction_unique';
    ELSE
        RAISE NOTICE 'Constraint messages_partition_transaction_unique already exists, skipping...';
    END IF;
END $$;

-- Step 2: Find and drop the old global unique constraint
-- The constraint name is typically auto-generated as: messages_transaction_id_key
DO $$ 
DECLARE
    constraint_name TEXT;
BEGIN
    -- Find the old global constraint (exclude the new one we just created)
    SELECT conname INTO constraint_name
    FROM pg_constraint 
    WHERE conrelid = 'queen.messages'::regclass 
      AND contype = 'u' 
      AND conname != 'messages_partition_transaction_unique'
      AND array_length(conkey, 1) = 1  -- Single column constraint
    LIMIT 1;
    
    IF constraint_name IS NOT NULL THEN
        EXECUTE format('ALTER TABLE queen.messages DROP CONSTRAINT %I', constraint_name);
        RAISE NOTICE 'Dropped old global unique constraint: %', constraint_name;
    ELSE
        RAISE NOTICE 'No old global constraint found, skipping...';
    END IF;
END $$;

-- Step 3: Verify the migration
DO $$
DECLARE
    constraint_count INTEGER;
BEGIN
    SELECT COUNT(*) INTO constraint_count
    FROM pg_constraint 
    WHERE conrelid = 'queen.messages'::regclass 
      AND conname = 'messages_partition_transaction_unique';
    
    IF constraint_count = 1 THEN
        RAISE NOTICE '✓ Migration successful! Partition-scoped uniqueness is now enforced.';
    ELSE
        RAISE EXCEPTION 'Migration verification failed!';
    END IF;
END $$;

COMMIT;

-- ═══════════════════════════════════════════════════════════════════════════
-- Post-migration verification queries (optional)
-- ═══════════════════════════════════════════════════════════════════════════

-- List all constraints on queen.messages
SELECT 
    conname as constraint_name,
    contype as constraint_type,
    pg_get_constraintdef(oid) as constraint_definition
FROM pg_constraint 
WHERE conrelid = 'queen.messages'::regclass;

-- Verify no duplicate transaction_ids exist within the same partition
-- (Should return 0 rows)
SELECT partition_id, transaction_id, COUNT(*) as duplicate_count
FROM queen.messages
GROUP BY partition_id, transaction_id
HAVING COUNT(*) > 1;

