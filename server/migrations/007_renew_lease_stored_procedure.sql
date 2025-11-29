-- Migration: Add renew_lease_v2 stored procedure
-- For high-performance async lease renewal via sidecar pattern
--
-- Queen Architecture Notes:
-- - Leases are at partition level (partition_consumers.worker_id, lease_expires_at)
-- - worker_id is the lease ID (UUID stored as text)

-- Drop if exists (for development iteration)
DROP FUNCTION IF EXISTS queen.renew_lease_v2(jsonb);

-- ============================================================================
-- renew_lease_v2: Batch lease renewal
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.renew_lease_v2(
    p_items JSONB
    -- [{index, leaseId, extendSeconds}, ...]
) RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_item JSONB;
    v_results JSONB := '[]'::jsonb;
    v_success BOOLEAN;
    v_new_expires TIMESTAMPTZ;
    v_error TEXT;
    v_lease_id TEXT;
    v_extend_seconds INT;
BEGIN
    -- Sort by leaseId for consistent lock ordering
    -- While lease renewals rarely overlap, consistent ordering prevents deadlocks
    FOR v_item IN 
        SELECT value FROM jsonb_array_elements(p_items)
        ORDER BY (value->>'leaseId')
    LOOP
        v_success := false;
        v_error := NULL;
        v_new_expires := NULL;
        v_lease_id := v_item->>'leaseId';
        v_extend_seconds := COALESCE((v_item->>'extendSeconds')::int, 60);
        
        -- Update the lease if it exists and hasn't expired
        -- Use GREATEST to ensure we never shorten the lease
        UPDATE queen.partition_consumers
        SET lease_expires_at = GREATEST(
            lease_expires_at, 
            NOW() + (v_extend_seconds || ' seconds')::interval
        )
        WHERE worker_id = v_lease_id
          AND lease_expires_at > NOW()  -- Only if not already expired
        RETURNING lease_expires_at INTO v_new_expires;
        
        IF v_new_expires IS NOT NULL THEN
            v_success := true;
        ELSE
            -- Check why it failed
            IF NOT EXISTS (
                SELECT 1 FROM queen.partition_consumers 
                WHERE worker_id = v_lease_id
            ) THEN
                v_error := 'Lease not found';
            ELSIF EXISTS (
                SELECT 1 FROM queen.partition_consumers 
                WHERE worker_id = v_lease_id
                  AND lease_expires_at <= NOW()
            ) THEN
                v_error := 'Lease already expired';
            ELSE
                v_error := 'Lease not active';
            END IF;
        END IF;
        
        v_results := v_results || jsonb_build_object(
            'index', (v_item->>'index')::int,
            'leaseId', v_lease_id,
            'success', v_success,
            'error', v_error,
            'expiresAt', CASE WHEN v_new_expires IS NOT NULL 
                              THEN to_char(v_new_expires, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
                              ELSE NULL END
        );
    END LOOP;
    
    -- Sort results by original index
    SELECT COALESCE(jsonb_agg(item ORDER BY (item->>'index')::int), '[]'::jsonb)
    INTO v_results
    FROM jsonb_array_elements(v_results) item;
    
    RETURN v_results;
END;
$$;

-- Index for lease lookups by worker_id (if not already created)
CREATE INDEX IF NOT EXISTS idx_partition_consumers_worker_id 
ON queen.partition_consumers (worker_id)
WHERE worker_id IS NOT NULL;

-- Grant execute permissions
GRANT EXECUTE ON FUNCTION queen.renew_lease_v2(jsonb) TO PUBLIC;

COMMENT ON FUNCTION queen.renew_lease_v2 IS 
'Batch lease renewal for extending message processing time.
Sorted by leaseId for consistent lock ordering (deadlock prevention).

Architecture:
- Leases are at partition level (partition_consumers)
- worker_id is the lease ID
- Uses GREATEST to never shorten an existing lease

Parameters:
  p_items: Array of renewal requests:
    - index: Original position for result ordering
    - leaseId: Worker ID (UUID as text) to extend
    - extendSeconds: Number of seconds to extend (default 60)

Returns: Array of result objects with keys:
  - index: Original position
  - leaseId: Lease ID
  - success: Whether renewal succeeded
  - error: Error message if failed
  - expiresAt: New expiration timestamp if successful
';
