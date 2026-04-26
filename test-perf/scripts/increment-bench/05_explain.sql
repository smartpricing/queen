-- ============================================================================
-- 05_explain.sql -- EXPLAIN (ANALYZE, BUFFERS, WAL) of the inner statement
--                   in increment_message_counts_v1.
-- ============================================================================
--
-- We can't EXPLAIN the function call itself (it's plpgsql), so we replicate
-- the body verbatim. If you change the procedure body, mirror the change
-- here.
--
-- Run this AFTER an inject step so the active_partitions CTE has rows.
--
-- This file mirrors the CURRENT body of queen.increment_message_counts_v1
-- (post-patch). For the historical pre-patch plan, see
-- out/v1-baseline/explain.txt produced by the v1 baseline run -- it was
-- captured against the OLD body and shows the full Index Only Scan over
-- queen.messages plus the Seq Scan trailing UPDATE that the new body
-- eliminates.
-- ============================================================================

\set ON_ERROR_STOP on
\timing on

\echo === EXPLAIN: inner UPDATE (active_partitions -> per-partition msg count) ===
EXPLAIN (ANALYZE, BUFFERS, WAL, VERBOSE, SETTINGS)
WITH active_partitions AS (
    SELECT s.stat_key, s.partition_id, s.last_scanned_at
    FROM queen.stats s
    JOIN queen.partition_lookup pl ON pl.partition_id = s.partition_id
    WHERE s.stat_type = 'partition'
      AND s.last_scanned_at IS NOT NULL
      AND pl.updated_at > s.last_scanned_at
),
new_message_counts AS (
    SELECT
        ap.stat_key,
        ap.partition_id,
        COUNT(m.id)       AS new_messages,
        MAX(m.created_at) AS newest_at
    FROM active_partitions ap
    JOIN queen.messages m
      ON m.partition_id = ap.partition_id
     AND m.created_at  > ap.last_scanned_at
    GROUP BY ap.stat_key, ap.partition_id
)
UPDATE queen.stats s SET
    total_messages    = s.total_messages + nmc.new_messages,
    pending_messages  = s.pending_messages + nmc.new_messages,
    newest_message_at = GREATEST(s.newest_message_at, nmc.newest_at),
    last_scanned_at   = NOW()
FROM new_message_counts nmc
WHERE s.stat_type = 'partition' AND s.stat_key = nmc.stat_key;

\echo
\echo === Note: trailing blanket UPDATE removed in v2 (deliberate) ===
\echo No EXPLAIN to show -- the statement no longer exists.

\timing off
