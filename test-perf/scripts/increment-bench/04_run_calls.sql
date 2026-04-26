-- ============================================================================
-- 04_run_calls.sql -- run the function once and emit a single CSV row of
--                     measurement deltas to stdout.
-- ============================================================================
--
-- The driver invokes psql with -A -t -F',' so we get unaligned tuples-only
-- CSV output. We intentionally do NOT use \pset here because those commands
-- print "Field separator is ','." style messages even with -q.
--
-- Stats sources used:
--   * clock_timestamp()        for wall-clock duration (most reliable)
--   * pg_current_wal_lsn()     for WAL bytes written by the call
--   * pg_stat_user_functions   for server-side total_time on the function
--                              itself (keyed by funcname, not by query text,
--                              so it works regardless of how the call is
--                              wrapped)
--   * pg_stat_user_tables      for n_tup_upd / n_tup_hot_upd / n_dead_tup
--                              on queen.stats and seq_scan / idx_scan on
--                              queen.messages. Note: these counters are
--                              flushed at end of statement, so we read them
--                              AFTER the call. We call
--                              pg_stat_clear_snapshot() to force the
--                              backend to drop its cached view between
--                              before/after reads.
--
-- Inputs (psql -v):
--   :iter -- iteration number, only used as the first CSV column
-- ============================================================================

\set ON_ERROR_STOP on
\if :{?iter} \else \set iter 0 \endif

-- ----------------------------------------------------------------------------
-- BEFORE snapshot. Force-flush any pending per-backend stats updates and
-- drop the cached snapshot, then read fresh values. Wrapped in DO so it
-- doesn't emit a row (pg_stat_* returns void; under -A -t that becomes a
-- blank line which would corrupt the CSV output).
-- ----------------------------------------------------------------------------
DO $$ BEGIN
    PERFORM pg_stat_force_next_flush();
    PERFORM pg_stat_clear_snapshot();
END $$;

SELECT
    pg_current_wal_lsn()::text                                 AS lsn_before,
    COALESCE((SELECT n_tup_upd     FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='stats'),  0)::bigint AS stats_upd_before,
    COALESCE((SELECT n_tup_hot_upd FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='stats'),  0)::bigint AS stats_hot_before,
    COALESCE((SELECT n_dead_tup    FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='stats'),  0)::bigint AS stats_dead_before,
    COALESCE((SELECT seq_scan      FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='messages'), 0)::bigint AS msg_seqscan_before,
    COALESCE((SELECT idx_scan      FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='messages'), 0)::bigint AS msg_idxscan_before,
    COALESCE((SELECT total_time    FROM pg_stat_user_functions
                WHERE schemaname='queen' AND funcname='increment_message_counts_v1'),
              0)::numeric                                      AS fn_total_before,
    COALESCE((SELECT calls         FROM pg_stat_user_functions
                WHERE schemaname='queen' AND funcname='increment_message_counts_v1'),
              0)::bigint                                       AS fn_calls_before
\gset

-- ----------------------------------------------------------------------------
-- The call itself, wrapped in clock_timestamp() so we get a wall-clock
-- duration independent of the server-side stats counters.
-- ----------------------------------------------------------------------------
SELECT clock_timestamp()::text AS t0 \gset
SELECT (queen.increment_message_counts_v1())::text AS fn_result \gset
SELECT clock_timestamp()::text AS t1 \gset

-- ----------------------------------------------------------------------------
-- AFTER snapshot. Force-flush pending updates and drop snapshot so the
-- next read sees the post-call counters.
-- ----------------------------------------------------------------------------
DO $$ BEGIN
    PERFORM pg_stat_force_next_flush();
    PERFORM pg_stat_clear_snapshot();
END $$;

SELECT
    :'iter'::int AS iter,
    (EXTRACT(EPOCH FROM (:'t1'::timestamptz - :'t0'::timestamptz)) * 1000)::numeric(12,3)
        AS duration_ms,
    pg_wal_lsn_diff(pg_current_wal_lsn(), :'lsn_before'::pg_lsn)::bigint
        AS wal_bytes,
    COALESCE((SELECT n_tup_upd FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='stats'), 0)
        - :'stats_upd_before'::bigint AS stats_n_tup_upd_delta,
    COALESCE((SELECT n_tup_hot_upd FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='stats'), 0)
        - :'stats_hot_before'::bigint AS stats_n_tup_hot_upd_delta,
    COALESCE((SELECT n_dead_tup FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='stats'), 0)
        - :'stats_dead_before'::bigint AS stats_dead_tup_delta,
    COALESCE((SELECT seq_scan FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='messages'), 0)
        - :'msg_seqscan_before'::bigint AS messages_seq_scan_delta,
    COALESCE((SELECT idx_scan FROM pg_stat_user_tables
                WHERE schemaname='queen' AND relname='messages'), 0)
        - :'msg_idxscan_before'::bigint AS messages_idx_scan_delta,
    (COALESCE((SELECT total_time FROM pg_stat_user_functions
                WHERE schemaname='queen' AND funcname='increment_message_counts_v1'),
              0)
     - :'fn_total_before'::numeric)::numeric(12,3) AS fn_pgsuf_total_ms_delta,
    (COALESCE((SELECT calls FROM pg_stat_user_functions
                WHERE schemaname='queen' AND funcname='increment_message_counts_v1'),
              0)
     - :'fn_calls_before'::bigint) AS fn_pgsuf_calls_delta,
    COALESCE((:'fn_result'::jsonb ->> 'partitionsUpdated')::int, 0)
        AS returned_partitions_updated,
    COALESCE((:'fn_result'::jsonb ->> 'skipped')::boolean, false)
        AS returned_skipped;
