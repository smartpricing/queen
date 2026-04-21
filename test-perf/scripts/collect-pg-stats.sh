#!/usr/bin/env bash
# Snapshot PG ground-truth counters for the queen database.
# Called twice per run: once before measurement (label=pre), once after (label=post).
# analyze.js diffs pre/post to compute actual tx/s, inserts/s, updates/s.
#
# Usage:
#   OUT_DIR=... LABEL=pre ./collect-pg-stats.sh
#   OUT_DIR=... LABEL=post ./collect-pg-stats.sh
#
# Env:
#   OUT_DIR       (required)
#   LABEL         (required) "pre" or "post"
#   PG_CONTAINER  (default queen-pg-bench)
#   PG_DB         (default queen)
#   PG_USER       (default postgres)
set -euo pipefail

: "${OUT_DIR:?OUT_DIR required}"
: "${LABEL:?LABEL required}"
PG_CONTAINER="${PG_CONTAINER:-queen-pg-bench}"
PG_DB="${PG_DB:-queen}"
PG_USER="${PG_USER:-postgres}"

mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/pg-stats-${LABEL}.json"

# Build a single SQL that produces one JSON object covering everything we care about.
# We use pg_stat_database for session-wide counters, pg_stat_user_tables for
# per-table counters (focus on queen.messages which is the hot table), and a
# per-status breakdown of queen.messages rows so we know what's actually there.
SQL=$(cat <<'SQL'
-- We capture counters for two tables:
--   * queen.messages           -- append-only. INSERTs = pushes. n_tup_ins is push ground truth.
--   * queen.partition_consumers-- cursor rows. UPDATEs = pops (autoAck/ack advances last_consumed_id).
-- We also grab total row counts for sanity.
WITH
db AS (
  SELECT jsonb_build_object(
    'ts', extract(epoch from now()),
    'numbackends', numbackends,
    'xact_commit', xact_commit,
    'xact_rollback', xact_rollback,
    'blks_read', blks_read,
    'blks_hit', blks_hit,
    'tup_returned', tup_returned,
    'tup_fetched', tup_fetched,
    'tup_inserted', tup_inserted,
    'tup_updated', tup_updated,
    'tup_deleted', tup_deleted,
    'conflicts', conflicts,
    'temp_files', temp_files,
    'temp_bytes', temp_bytes,
    'deadlocks', deadlocks,
    'blk_read_time', blk_read_time,
    'blk_write_time', blk_write_time
  ) AS j
  FROM pg_stat_database
  WHERE datname = current_database()
),
tbl_stats AS (
  SELECT relname,
    jsonb_build_object(
      'n_live_tup', COALESCE(n_live_tup, 0),
      'n_dead_tup', COALESCE(n_dead_tup, 0),
      'n_tup_ins',  COALESCE(n_tup_ins, 0),
      'n_tup_upd',  COALESCE(n_tup_upd, 0),
      'n_tup_del',  COALESCE(n_tup_del, 0),
      'n_tup_hot_upd', COALESCE(n_tup_hot_upd, 0),
      'seq_scan',   COALESCE(seq_scan, 0),
      'seq_tup_read', COALESCE(seq_tup_read, 0),
      'idx_scan',   COALESCE(idx_scan, 0),
      'idx_tup_fetch', COALESCE(idx_tup_fetch, 0)
    ) AS j
  FROM pg_stat_user_tables
  WHERE schemaname = 'queen' AND relname IN ('messages', 'partition_consumers', 'partitions', 'queues')
),
messages_count AS (
  SELECT jsonb_build_object(
    'messages_total', (SELECT count(*)::bigint FROM queen.messages),
    'partitions_total', (SELECT count(*)::bigint FROM queen.partitions),
    'partition_consumers_total', (SELECT count(*)::bigint FROM queen.partition_consumers)
  ) AS j
)
SELECT jsonb_build_object(
  'database', (SELECT j FROM db),
  'messages_table',            COALESCE((SELECT j FROM tbl_stats WHERE relname = 'messages'), '{}'::jsonb),
  'partition_consumers_table', COALESCE((SELECT j FROM tbl_stats WHERE relname = 'partition_consumers'), '{}'::jsonb),
  'partitions_table',          COALESCE((SELECT j FROM tbl_stats WHERE relname = 'partitions'), '{}'::jsonb),
  'row_counts', (SELECT j FROM messages_count)
)::text;
SQL
)

RAW_OUT="$OUT_DIR/pg-stats-${LABEL}.raw.json"
if ! docker exec -i "$PG_CONTAINER" psql -v ON_ERROR_STOP=1 -U "$PG_USER" -d "$PG_DB" \
      -tAX -c "$SQL" > "$RAW_OUT" 2>"$OUT_DIR/pg-stats-${LABEL}.err"; then
  echo "[collect-pg-stats] ERROR: psql failed for label=$LABEL. Stderr:"
  cat "$OUT_DIR/pg-stats-${LABEL}.err" >&2
  echo '{"_error":"psql query failed","label":"'"$LABEL"'"}' > "$OUT"
  rm -f "$RAW_OUT"
  exit 1
fi

# Wrap the JSON from psql with our own label. Using node to keep JSON well-formed
# even if psql's output has trailing whitespace.
node -e "
const fs = require('fs');
const raw = fs.readFileSync('$RAW_OUT','utf8').trim();
let inner;
try { inner = JSON.parse(raw); } catch (e) {
  console.error('[collect-pg-stats] ERROR: psql output is not valid JSON:', e.message);
  console.error('raw:', raw.slice(0, 500));
  process.exit(1);
}
const wrapped = Object.assign({ label: '$LABEL' }, inner);
fs.writeFileSync('$OUT', JSON.stringify(wrapped, null, 2));
"
rm -f "$RAW_OUT" "$OUT_DIR/pg-stats-${LABEL}.err"

echo "[collect-pg-stats] label=$LABEL wrote $OUT"
