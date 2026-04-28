-- ============================================================================
-- 02_seed.sql -- create a deterministic, prod-shape state for the bench.
-- ============================================================================
--
-- Inputs (psql variables, set with -v from the driver):
--   :partition_count  (default 200000) -- how many queen.partitions rows
--   :message_count    (default 10000000) -- how many queen.messages rows
--   :stats_age_min    (default 5)       -- minutes to age last_scanned_at
--                                         (so the trailing UPDATE in
--                                          increment_message_counts_v1 has
--                                          something to do)
--
-- Determinism strategy:
--   - setseed(0.42) at the top makes random() reproducible
--   - All UUIDs are derived from md5() of a deterministic seed string
--   - All timestamps are derived from a fixed reference time
--
-- After this script:
--   - 1 queue 'bench'
--   - :partition_count partitions named 'p-1'..'p-N' (deterministic UUIDs)
--   - :message_count messages distributed Zipf-like across partitions
--     (a few "hot" partitions get many messages, most get few or zero)
--   - queen.partition_lookup populated correctly
--   - queen.stats populated for every partition (one row each, default
--     consumer_group '__QUEUE_MODE__'), with last_scanned_at aged
--     :stats_age_min minutes in the past so the function under test will
--     actually do work on the very first call.
-- ============================================================================

\timing on
\set ON_ERROR_STOP on

\if :{?partition_count} \else \set partition_count 200000 \endif
\if :{?message_count}   \else \set message_count   10000000 \endif
\if :{?stats_age_min}   \else \set stats_age_min   5 \endif

\echo === seed parameters ===
\echo   partition_count = :partition_count
\echo   message_count   = :message_count
\echo   stats_age_min   = :stats_age_min

-- ----------------------------------------------------------------------------
-- Reproducible RNG. We pin a fixed seed so two runs with the same parameters
-- produce byte-identical state in queen.partitions / queen.messages.
-- ----------------------------------------------------------------------------
SELECT setseed(0.42);

-- Reference instant for all timestamps in the seed. Pinned so re-seeding the
-- same parameters produces the same created_at distribution.
\set ref_ts '2026-04-01 00:00:00+00'

-- ----------------------------------------------------------------------------
-- Helper: deterministic UUID from a seed string. md5() returns 32 hex chars,
-- which we slice into the canonical 8-4-4-4-12 form. Wrapped in pg_temp so it
-- vanishes when the session closes.
-- ----------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION pg_temp.det_uuid(seed text) RETURNS uuid
LANGUAGE sql IMMUTABLE PARALLEL SAFE AS $$
  SELECT (
    substring(md5($1) from 1  for 8)  || '-' ||
    substring(md5($1) from 9  for 4)  || '-' ||
    substring(md5($1) from 13 for 4)  || '-' ||
    substring(md5($1) from 17 for 4)  || '-' ||
    substring(md5($1) from 21 for 12)
  )::uuid
$$;

-- ----------------------------------------------------------------------------
-- 0. Wipe relevant tables (idempotent)
-- ----------------------------------------------------------------------------
\echo
\echo [seed] wiping previous state...
TRUNCATE queen.stats,
         queen.partition_lookup,
         queen.partition_consumers,
         queen.messages_consumed,
         queen.dead_letter_queue,
         queen.messages,
         queen.partitions,
         queen.queues
RESTART IDENTITY CASCADE;

-- ----------------------------------------------------------------------------
-- 1. One queue
-- ----------------------------------------------------------------------------
\echo [seed] creating queue 'bench'...
INSERT INTO queen.queues (id, name, namespace, task, created_at)
VALUES (
    pg_temp.det_uuid('queue:bench'),
    'bench',
    'bench-ns',
    'bench-task',
    :'ref_ts'::timestamptz - INTERVAL '30 days'
);

-- ----------------------------------------------------------------------------
-- 2. Partitions (deterministic UUIDs)
-- ----------------------------------------------------------------------------
\echo [seed] creating :partition_count partitions...
INSERT INTO queen.partitions (id, queue_id, name, created_at, last_activity)
SELECT
    pg_temp.det_uuid('partition:'||g),
    (SELECT id FROM queen.queues WHERE name='bench'),
    'p-' || g::text,
    :'ref_ts'::timestamptz - INTERVAL '7 days',
    :'ref_ts'::timestamptz - INTERVAL '7 days'
FROM generate_series(1, :partition_count) g;

-- ----------------------------------------------------------------------------
-- 3. Messages, distributed Zipf-like across partitions.
--
-- We sample a partition for each message via:
--     idx = floor( pow(random(), 5) * partition_count ) + 1
-- where pow(random(), 5) is heavily skewed toward 0, so partition #1 gets a
-- LOT of messages, partition #partition_count gets very few. This mimics the
-- production heavy-tail (smartchat.router.outgoing has 229k partitions but
-- only ~4M total consumed -> ~17 msg / partition lifetime, with most in a
-- small subset).
--
-- We pre-materialise the partition UUIDs into an ARRAY so we can index into
-- it from the INSERT without a per-row subquery (would be O(n*m) otherwise).
--
-- Why not COPY: INSERT ... SELECT on 10M rows takes ~30-60s on a laptop
-- which is acceptable; using COPY would require client-side generation of
-- the deterministic data which loses the inline determinism.
-- ----------------------------------------------------------------------------
\echo [seed] dropping messages indexes for fast bulk load (will recreate after)...
-- Per-row unique-index maintenance dominates a random-order INSERT of 10M
-- rows because every insert touches a near-random leaf of the (partition_id,
-- transaction_id) B-tree -- terrible cache locality. Building the index
-- post-insert from already-loaded data is 5-10x faster.
DROP INDEX IF EXISTS queen.messages_partition_transaction_unique;
DROP INDEX IF EXISTS queen.idx_messages_partition_created;

\echo [seed] inserting :message_count messages in 500k-row chunks...
-- Stash counts in custom GUCs so the procedure body (server-side) can read
-- them. (psql does NOT interpolate :vars inside dollar-quoted strings.)
SELECT set_config('bench.message_count', :'message_count', false);
SELECT set_config('bench.ref_ts',        :'ref_ts',         false);

-- We use a PROCEDURE (not a DO block) so we can COMMIT between chunks. A
-- single 10M-row INSERT pins all of its WAL and dead-tuple work in one
-- monster transaction, which on a 4 CPU / 4 GiB Postgres takes 8+ minutes
-- and frequently hits the WAL-buffers-full ceiling. Chunked commits keep
-- WAL turnover healthy and let us stream progress into the seed.log.
CREATE OR REPLACE PROCEDURE pg_temp.bench_insert_messages_chunked()
LANGUAGE plpgsql
AS $$
DECLARE
    v_partition_ids uuid[];
    v_count int;
    v_msg_count int := current_setting('bench.message_count')::int;
    v_ref_ts timestamptz := current_setting('bench.ref_ts')::timestamptz;
    v_chunk_size int := 500000;
    v_offset int := 0;
    v_done int;
    v_t0 timestamptz := clock_timestamp();
BEGIN
    SELECT array_agg(id ORDER BY name) INTO v_partition_ids
      FROM queen.partitions;
    v_count := array_length(v_partition_ids, 1);
    RAISE NOTICE '  loaded % partition ids, will insert % messages in chunks of %',
                 v_count, v_msg_count, v_chunk_size;

    -- Pin RNG so the message->partition assignment is reproducible across
    -- runs (deterministic given a fixed partition_count + message_count).
    PERFORM setseed(0.13);

    WHILE v_offset < v_msg_count LOOP
        v_done := LEAST(v_chunk_size, v_msg_count - v_offset);

        INSERT INTO queen.messages (id, transaction_id, partition_id, payload, created_at)
        SELECT
            pg_temp.det_uuid('msg:'||(v_offset + g)),
            'tx-'||(v_offset + g)::text,
            v_partition_ids[
                1 + LEAST(v_count - 1, FLOOR(POWER(random(), 5) * v_count)::int)
            ],
            '{"k":"v"}'::jsonb,
            -- Spread created_at across the last 7 days so age distribution
            -- is realistic. The 30s window is empty because v_ref_ts is in
            -- the past; the inject step adds fresh messages right before
            -- each measurement call.
            v_ref_ts - (random() * INTERVAL '7 days')
        FROM generate_series(1, v_done) g;

        v_offset := v_offset + v_done;
        COMMIT;
        RAISE NOTICE '  inserted % / %  (%.1f%%) elapsed=%s',
                     v_offset, v_msg_count,
                     100.0 * v_offset / v_msg_count,
                     ROUND(EXTRACT(EPOCH FROM clock_timestamp() - v_t0)::numeric, 1);
    END LOOP;
END
$$;

CALL pg_temp.bench_insert_messages_chunked();

-- ----------------------------------------------------------------------------
-- 3b. Recreate the indexes we dropped, now that data is loaded. Build from
--     scratch is dramatically faster than incremental maintenance.
-- ----------------------------------------------------------------------------
\echo [seed] rebuilding indexes on queen.messages...
CREATE UNIQUE INDEX IF NOT EXISTS messages_partition_transaction_unique
    ON queen.messages (partition_id, transaction_id);
CREATE INDEX IF NOT EXISTS idx_messages_partition_created
    ON queen.messages (partition_id, created_at, id);

-- ----------------------------------------------------------------------------
-- 4. partition_lookup (populate from the actual newest message per partition)
-- ----------------------------------------------------------------------------
\echo [seed] populating partition_lookup...
INSERT INTO queen.partition_lookup
    (queue_name, partition_id, last_message_id, last_message_created_at, updated_at)
SELECT DISTINCT ON (m.partition_id)
    'bench',
    m.partition_id,
    m.id,
    m.created_at,
    m.created_at
FROM queen.messages m
ORDER BY m.partition_id, m.created_at DESC, m.id DESC
ON CONFLICT (queue_name, partition_id) DO UPDATE SET
    last_message_id         = EXCLUDED.last_message_id,
    last_message_created_at = EXCLUDED.last_message_created_at,
    updated_at              = EXCLUDED.updated_at;

-- ----------------------------------------------------------------------------
-- 5. Materialise queen.stats for every partition (default consumer group).
--    We let the production code do this so row contents match what the real
--    system would produce. We force=true to bypass debounce.
-- ----------------------------------------------------------------------------
\echo [seed] materialising queen.stats via compute_partition_stats_v3(true)...
SELECT queen.compute_partition_stats_v3(true);

-- ----------------------------------------------------------------------------
-- 6. Age last_scanned_at so the very first call to
--    increment_message_counts_v1() has something to do with its trailing
--    blanket UPDATE. We back-date by :stats_age_min minutes (default 5).
-- ----------------------------------------------------------------------------
\echo [seed] aging last_scanned_at by :stats_age_min minute(s)...
UPDATE queen.stats
   SET last_scanned_at = NOW() - (:stats_age_min || ' minutes')::interval
 WHERE stat_type = 'partition';

-- ----------------------------------------------------------------------------
-- 7. ANALYZE so the planner has fresh statistics
-- ----------------------------------------------------------------------------
\echo [seed] ANALYZE...
ANALYZE queen.messages;
ANALYZE queen.partitions;
ANALYZE queen.partition_lookup;
ANALYZE queen.stats;

-- ----------------------------------------------------------------------------
-- 8. Disable autovacuum on the two tables we measure, so it doesn't kick
--    in mid-run and pollute the timing distribution. We re-enable explicitly
--    in a tear-down step (or just throw away the volume).
-- ----------------------------------------------------------------------------
ALTER TABLE queen.messages SET (autovacuum_enabled = false);
ALTER TABLE queen.stats    SET (autovacuum_enabled = false);

-- ----------------------------------------------------------------------------
-- 9. Print a seed summary
-- ----------------------------------------------------------------------------
\echo
\echo === seed summary ===
SELECT
    (SELECT COUNT(*) FROM queen.queues)               AS queues,
    (SELECT COUNT(*) FROM queen.partitions)           AS partitions,
    (SELECT COUNT(*) FROM queen.messages)             AS messages,
    (SELECT COUNT(*) FROM queen.partition_lookup)     AS partition_lookup_rows,
    (SELECT COUNT(*) FROM queen.stats
       WHERE stat_type='partition')                   AS partition_stats_rows,
    pg_size_pretty(pg_total_relation_size('queen.messages')) AS messages_size,
    pg_size_pretty(pg_total_relation_size('queen.stats'))    AS stats_size;

-- Also print the heavy-tail shape (top-10 partitions by message count) so we
-- can sanity-check that the Zipf distribution actually produced one.
\echo
\echo === message distribution (top 10 partitions, then percentiles) ===
WITH pc AS (
    SELECT partition_id, COUNT(*)::int AS n
      FROM queen.messages
     GROUP BY partition_id
)
SELECT 'top10' AS bucket, n FROM pc ORDER BY n DESC LIMIT 10;

WITH pc AS (
    SELECT partition_id, COUNT(*)::int AS n
      FROM queen.messages
     GROUP BY partition_id
)
SELECT
    PERCENTILE_CONT(0.50) WITHIN GROUP (ORDER BY n)::int AS p50,
    PERCENTILE_CONT(0.90) WITHIN GROUP (ORDER BY n)::int AS p90,
    PERCENTILE_CONT(0.99) WITHIN GROUP (ORDER BY n)::int AS p99,
    MAX(n)                                                AS max,
    COUNT(*) FILTER (WHERE n = 1)                         AS singletons,
    COUNT(*)                                              AS partitions_with_msgs
FROM pc;

\timing off
