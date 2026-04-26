-- ============================================================================
-- 03_inject_new_messages.sql -- inject a controlled trickle of fresh messages
-- ============================================================================
--
-- Inserts :inject_messages messages into :inject_partitions distinct
-- partitions, with created_at = NOW() so they fall inside the function's
-- 30-second window.
--
-- This is what gives the function under test a non-zero amount of REAL work
-- on each call. Without this, every call only hits the trailing blanket
-- UPDATE (which is the bulk of the v1 overhead -- we want to measure both
-- the legitimate work AND the overhead).
--
-- Inputs (psql -v):
--   :inject_messages   (default 10) -- how many messages to add this round
--   :inject_partitions (default 5)  -- how many distinct partitions to hit
--   :iter              (default 0)  -- iteration number; used to deterministically
--                                     pick a different subset of partitions each call
--
-- Determinism: chosen partitions are a pure function of (iter, partition_count).
-- ============================================================================

\set ON_ERROR_STOP on

\if :{?inject_messages}   \else \set inject_messages   10 \endif
\if :{?inject_partitions} \else \set inject_partitions  5 \endif
\if :{?iter}              \else \set iter               0 \endif

-- ----------------------------------------------------------------------------
-- Pick :inject_partitions DISTINCT partition indices deterministically from
-- iter. Mapping idx -> partition uses partition.name = 'p-N' (a UNIQUE
-- constraint on (queue_id, name) gives us O(1) lookup).
--
-- We use a simple LCG-style walk over partition_idx; across iterations we
-- touch a different small subset each time. Collisions (two j's hashing to
-- the same idx) are tolerated -- DISTINCT below collapses them, so the
-- effective fan-out is <= :inject_partitions, never more.
-- ----------------------------------------------------------------------------

WITH cfg AS (
    SELECT
        :iter::int              AS iter,
        :inject_partitions::int AS k,
        :inject_messages::int   AS n,
        (SELECT COUNT(*)::int FROM queen.partitions) AS pcount
),
picks AS (
    SELECT DISTINCT
        ((cfg.iter * 1000003 + j * 7919) % cfg.pcount) + 1 AS partition_idx
    FROM cfg, LATERAL generate_series(1, cfg.k) j
),
chosen_partitions AS (
    SELECT p.id AS partition_id
    FROM queen.partitions p
    JOIN picks pk ON p.name = 'p-' || pk.partition_idx::text
),
-- Distribute :inject_messages across chosen partitions round-robin.
-- msg_per = ceil(n / k_actual); we may emit slightly more than n, which is
-- fine -- it only matters that the count is reproducible.
plan AS (
    SELECT
        cp.partition_id,
        GREATEST(1, CEIL(cfg.n::numeric /
                         GREATEST(1, (SELECT COUNT(*) FROM chosen_partitions)))::int) AS msg_per
    FROM chosen_partitions cp, cfg
)
-- Deterministic IDs: id and transaction_id are pure functions of
-- (iter, partition_id, row). This is critical for v1-vs-v2 correctness
-- comparison: a v1 run and a v2 run starting from the same seed must
-- produce IDENTICAL injected messages so that `after_md5` differences
-- (if any) come solely from the function under test, not from RNG noise.
--
-- Caveat: re-running the harness against an ALREADY-injected DB will
-- collide on (partition_id, transaction_id) UNIQUE. The driver always
-- re-seeds (SKIP_SEED=0) for a clean comparison run; SKIP_SEED=1 is
-- only for fast iteration during development, in which case the user
-- accepts non-reproducible inject IDs.
INSERT INTO queen.messages (id, transaction_id, partition_id, payload, created_at)
SELECT
    -- 32 hex chars from md5() reformatted to UUID
    (substring(md5('inj-id:'  || :iter || ':' || partition_id::text || ':' || g) from 1  for 8)  || '-' ||
     substring(md5('inj-id:'  || :iter || ':' || partition_id::text || ':' || g) from 9  for 4)  || '-' ||
     substring(md5('inj-id:'  || :iter || ':' || partition_id::text || ':' || g) from 13 for 4)  || '-' ||
     substring(md5('inj-id:'  || :iter || ':' || partition_id::text || ':' || g) from 17 for 4)  || '-' ||
     substring(md5('inj-id:'  || :iter || ':' || partition_id::text || ':' || g) from 21 for 12))::uuid,
    'inject-' || :iter || '-' || partition_id::text || '-' || g,
    partition_id,
    '{"inject":true}'::jsonb,
    NOW()
FROM plan, LATERAL generate_series(1, msg_per) g;

-- ----------------------------------------------------------------------------
-- Update partition_lookup so the v3 reconciler / any future watermark-aware
-- version of the function recognises these partitions as "active" since the
-- last bench cycle.
-- ----------------------------------------------------------------------------
WITH latest_per_part AS (
    SELECT DISTINCT ON (m.partition_id)
        m.partition_id, m.id, m.created_at
    FROM queen.messages m
    WHERE m.transaction_id LIKE ('inject-' || :iter || '-%')
    ORDER BY m.partition_id, m.created_at DESC, m.id DESC
)
INSERT INTO queen.partition_lookup
    (queue_name, partition_id, last_message_id, last_message_created_at, updated_at)
SELECT 'bench', l.partition_id, l.id, l.created_at, NOW()
FROM latest_per_part l
ON CONFLICT (queue_name, partition_id) DO UPDATE SET
    last_message_id         = EXCLUDED.last_message_id,
    last_message_created_at = EXCLUDED.last_message_created_at,
    updated_at              = NOW();
