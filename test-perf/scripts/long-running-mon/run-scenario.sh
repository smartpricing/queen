#!/usr/bin/env bash
# Run a single scenario end-to-end:
#   run-scenario.sh push-only
#   run-scenario.sh pop-only
#   run-scenario.sh combined
#
# Resets pg_stat_statements + pg_stat_user_functions BEFORE the run,
# runs the workload + monitor for 120s, and dumps per-query / per-function
# timing at the end.

set -euo pipefail

SCEN="${1:?scenario (push-only|pop-only|combined)}"
DUR="${2:-120}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out/$SCEN"
mkdir -p "$OUT"

# Workload logs live in a SEPARATE directory so monitor.sh's rm -rf of $OUT
# doesn't wipe in-progress node stdout redirects.
WLOG="$HERE/workload-logs/$SCEN"
mkdir -p "$WLOG"
rm -f "$WLOG"/*.log

PG="docker exec queen-pg-5433 psql -U postgres -d queen -v ON_ERROR_STOP=1"

echo "=== Resetting pg_stat_statements + pg_stat_user_functions ==="
$PG -c "SELECT pg_stat_statements_reset(); SELECT pg_stat_reset();" >/dev/null

echo "=== Starting workload: $SCEN ==="
export NODE_PATH="/Users/alice/Work/queen/examples/node_modules"

case "$SCEN" in
  push-only)
    ( cd /Users/alice/Work/queen/examples && \
      NUM_WORKERS=2 CONNECTIONS_PER_WORKER=100 DURATION=$DUR \
      node long-running/producer-cluster.js > "$WLOG/producer.log" 2>&1 ) &
    WORK_PID=$!
    ;;
  pop-only)
    ( cd /Users/alice/Work/queen/examples && \
      NUM_WORKERS=1 CONNECTIONS_PER_WORKER=50 DURATION=$DUR BATCH_SIZE=1000 \
      node long-running/consumer-clustered.js > "$WLOG/consumer.log" 2>&1 ) &
    WORK_PID=$!
    ;;
  combined)
    ( cd /Users/alice/Work/queen/examples && \
      NUM_WORKERS=2 CONNECTIONS_PER_WORKER=100 DURATION=$DUR \
      node long-running/producer-cluster.js > "$WLOG/producer.log" 2>&1 ) &
    PUSH_PID=$!
    sleep 2
    ( cd /Users/alice/Work/queen/examples && \
      NUM_WORKERS=1 CONNECTIONS_PER_WORKER=50 DURATION=$DUR BATCH_SIZE=1000 \
      node long-running/consumer-clustered.js > "$WLOG/consumer.log" 2>&1 ) &
    POP_PID=$!
    WORK_PID="$PUSH_PID $POP_PID"
    ;;
  *)
    echo "unknown scenario: $SCEN"; exit 1
    ;;
esac

sleep 3
bash "$HERE/monitor.sh" "$SCEN" "$DUR"

wait $WORK_PID 2>/dev/null || true

# Copy workload logs back into $OUT for bundling.
cp "$WLOG"/*.log "$OUT/" 2>/dev/null || true

echo ""
echo "=== pg_stat_statements (top by total_exec_time) ==="
$PG -c "
  SELECT
    calls,
    ROUND(total_exec_time::numeric,0)             AS total_ms,
    ROUND((total_exec_time/GREATEST(calls,1))::numeric,2) AS mean_ms,
    ROUND((100*total_exec_time/SUM(total_exec_time) OVER ())::numeric,1) AS pct,
    ROUND(rows::numeric/GREATEST(calls,1),1)       AS rows_per_call,
    LEFT(REGEXP_REPLACE(query,'\s+',' ','g'), 100) AS query
  FROM pg_stat_statements
  WHERE query NOT LIKE '%pg_stat_statements%'
    AND query NOT LIKE '%pg_stat_reset%'
  ORDER BY total_exec_time DESC
  LIMIT 15" | tee "$OUT/pg_stat_statements.txt"

echo ""
echo "=== pg_stat_user_functions (top by total_time, only queen.*) ==="
$PG -c "
  SELECT
    s.funcname,
    s.calls,
    ROUND(s.total_time::numeric,0)                AS total_ms,
    ROUND(s.self_time::numeric,0)                 AS self_ms,
    ROUND((s.total_time/GREATEST(s.calls,1))::numeric,2) AS mean_ms,
    ROUND((100*s.total_time/NULLIF(SUM(s.total_time) OVER (),0))::numeric,1) AS pct
  FROM pg_stat_user_functions s
  WHERE s.schemaname = 'queen'
  ORDER BY s.total_time DESC
  LIMIT 15" | tee "$OUT/pg_stat_user_functions.txt"

echo ""
echo "=== Workload output ==="
for f in "$OUT/producer.log" "$OUT/consumer.log"; do
  if [ -s "$f" ]; then
    echo "--- $(basename $f) (last 35 lines) ---"
    tail -35 "$f"
  fi
done
echo "=== done: $SCEN (results in $OUT) ==="
