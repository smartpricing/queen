-- ============================================================================
-- 06_snapshot.sql -- dump queen.stats partition rows as deterministic CSV
--                    suitable for diffing two implementations.
-- ============================================================================
--
-- The point of this snapshot is to define the CORRECTNESS CONTRACT of the
-- function under test. Any v2 candidate must produce a snapshot whose
-- "must-match" columns are byte-identical to v1's.
--
-- Per the user's decision ("results are correct"), the contract is:
--
--   MUST MATCH (across v1 and v2, given the same input)
--   -- these are what the function is supposed to COMPUTE:
--     - stat_key
--     - total_messages
--     - pending_messages
--
--   PRINTED for visual inspection but NOT in the must-match hash
--   (they are wall-clock-dependent: even two clean runs of the SAME
--    implementation, separated by >1 second, would otherwise hash
--    differently because inject and the function both call NOW(); we'd
--    lose our reproducibility guarantee):
--     - newest_message_at  (driven by inject's NOW())
--     - last_scanned_at    (driven by the function's NOW(), and v2 may
--                           legitimately stop bumping it for idle
--                           partitions per the v3-style optimisation)
--
--   IGNORED entirely (the function under test does not touch them):
--     - processing_messages, completed_messages, dead_letter_messages
--     - last_computed_at, oldest_pending_at, etc.
--
-- The script writes two artefacts to STDOUT (capture them in the driver):
--   1. The full per-partition CSV (sorted by stat_key) for visual diffing
--   2. A single MD5 hash over the must-match columns -- this is the
--      one the driver compares between v1 and v2 runs.
--      (md5 is a built-in; no pgcrypto extension required.)
--
-- Inputs (psql -v):
--   :label  -- label printed in the header (e.g. 'before', 'after', 'v1', 'v2')
-- ============================================================================

\set ON_ERROR_STOP on
\if :{?label} \else \set label snapshot \endif

\pset pager off
\pset format unaligned
\pset tuples_only on
\pset fieldsep ','

-- ----------------------------------------------------------------------------
-- 1. Full per-partition projection for visual diffing. Includes timestamps
--    truncated to seconds (informational only -- not in the hash). NULL is
--    rendered as the literal 'NULL' so the CSV is unambiguous.
-- ----------------------------------------------------------------------------
\echo --- BEGIN snapshot label=:label ---
SELECT
    stat_key,
    total_messages,
    pending_messages,
    COALESCE(date_trunc('second', newest_message_at)::text, 'NULL') AS newest_message_at_s,
    COALESCE(date_trunc('second', last_scanned_at)::text,    'NULL') AS last_scanned_at_s
FROM queen.stats
WHERE stat_type = 'partition'
ORDER BY stat_key;

\echo --- END snapshot ---

-- ----------------------------------------------------------------------------
-- 2. Single-line summary: counts + must-match SHA-256. The hash is computed
--    server-side over the EXACT same projection (column order, rounding,
--    sort) used above, so it doesn't depend on psql output formatting.
--
--    We deliberately use string_agg with a separator that cannot appear in
--    any column (we forbid pipes by construction -- stat_key is a UUID:tag).
-- ----------------------------------------------------------------------------
\echo
\echo --- BEGIN summary label=:label ---
-- The hash is over (stat_key, total_messages, pending_messages) only --
-- the wall-clock-dependent timestamp columns are deliberately excluded so
-- two clean runs of v1 produce the SAME must_match_md5 (which is what
-- makes a v1-vs-v2 comparison meaningful).
WITH proj AS (
    SELECT
        stat_key,
        total_messages,
        pending_messages
    FROM queen.stats
    WHERE stat_type = 'partition'
    ORDER BY stat_key
)
SELECT
    :'label'                                                  AS label,
    COUNT(*)                                                  AS rows_n,
    COALESCE(SUM(total_messages),  0)                         AS sum_total,
    COALESCE(SUM(pending_messages),0)                         AS sum_pending,
    md5(
        COALESCE(string_agg(
            stat_key || '|' || total_messages::text
            || '|' || pending_messages::text,
            E'\n'
            ORDER BY stat_key
        ), '')
    ) AS must_match_md5
FROM proj;
\echo --- END summary ---
