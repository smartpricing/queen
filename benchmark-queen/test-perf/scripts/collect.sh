#!/usr/bin/env bash
# Capture post-run artifacts into $OUT_DIR:
#   - server-analytics.json: snapshot of /api/v1/resources/queues/<queue>
#   - server-metrics.json:   snapshot of /metrics if available
#   - docker-stats.txt:      single-shot docker stats for the PG container
#   - log-summary.txt:       grep of interesting patterns in server.log
set -euo pipefail

: "${OUT_DIR:?OUT_DIR required}"
SERVER_PORT="${SERVER_PORT:-6632}"
QUEUE_NAME="${QUEUE_NAME:-perf-test}"
PG_CONTAINER="${PG_CONTAINER:-queen-pg-bench}"
LOG_FILE="${LOG_FILE:-$OUT_DIR/server.log}"

mkdir -p "$OUT_DIR"

curl -sf "http://127.0.0.1:${SERVER_PORT}/api/v1/resources/queues/${QUEUE_NAME}" \
  -o "$OUT_DIR/server-analytics.json" \
  || echo '{"_error":"failed to fetch queue analytics"}' > "$OUT_DIR/server-analytics.json"

curl -sf "http://127.0.0.1:${SERVER_PORT}/metrics" \
  -o "$OUT_DIR/server-metrics.txt" 2>/dev/null || true

docker stats --no-stream --format \
  '{{json .}}' "$PG_CONTAINER" > "$OUT_DIR/docker-stats.json" 2>/dev/null || \
  echo '{"_error":"docker stats failed"}' > "$OUT_DIR/docker-stats.json"

{
  echo "=== event_loop_lag events ==="
  grep -Ei 'Event loop lag' "$LOG_FILE" 2>/dev/null | tail -n 50 || true
  echo
  echo "=== errors / warnings ==="
  grep -Ei '\[error\]|\[warning\]|queue_full|overloaded|disconnect|reconnect' "$LOG_FILE" 2>/dev/null | tail -n 100 || true
  echo
  echo "=== line count ==="
  wc -l "$LOG_FILE" 2>/dev/null || true
} > "$OUT_DIR/log-summary.txt"

echo "[collect] artifacts written to $OUT_DIR"
