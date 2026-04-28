#!/usr/bin/env bash
# Start ./server/bin/queen-server in background, configured for perf tests.
# Logs go to $LOG_FILE. PID is written to $PID_FILE so server-down.sh can stop cleanly.
#
# Env inputs:
#   NUM_WORKERS         (required)
#   DB_POOL_SIZE        (required, per-worker)
#   SIDECAR_POOL_SIZE   (required, total)
#   PG_PORT             (default 5434)
#   SERVER_PORT         (default 6632)
#   LOG_FILE            (default ./server.log)
#   PID_FILE            (default ./server.pid)
set -euo pipefail

: "${NUM_WORKERS:?NUM_WORKERS required}"
: "${DB_POOL_SIZE:?DB_POOL_SIZE required}"
: "${SIDECAR_POOL_SIZE:?SIDECAR_POOL_SIZE required}"
PG_PORT="${PG_PORT:-5434}"
SERVER_PORT="${SERVER_PORT:-6632}"
LOG_FILE="${LOG_FILE:-./server.log}"
PID_FILE="${PID_FILE:-./server.pid}"
# Tunables the bench may override via env.
SIDECAR_MICRO_BATCH_WAIT_MS="${SIDECAR_MICRO_BATCH_WAIT_MS:-5}"
POP_WAIT_INITIAL_INTERVAL_MS="${POP_WAIT_INITIAL_INTERVAL_MS:-10}"
POP_WAIT_BACKOFF_THRESHOLD="${POP_WAIT_BACKOFF_THRESHOLD:-5}"
POP_WAIT_BACKOFF_MULTIPLIER="${POP_WAIT_BACKOFF_MULTIPLIER:-2}"
POP_WAIT_MAX_INTERVAL_MS="${POP_WAIT_MAX_INTERVAL_MS:-100}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$ROOT/server/bin/queen-server"
if [[ ! -x "$BIN" ]]; then
  echo "[server-up] ERROR: $BIN not found or not executable. Run 'make build-only' first." >&2
  exit 1
fi

echo "[server-up] launching queen-server: workers=$NUM_WORKERS sidecar=$SIDECAR_POOL_SIZE dbpool=$DB_POOL_SIZE micro_batch_wait=${SIDECAR_MICRO_BATCH_WAIT_MS}ms port=$SERVER_PORT pg=$PG_PORT"

# cd into repo root so the server finds lib/schema/ correctly.
cd "$ROOT"

PORT="$SERVER_PORT" \
HOST="0.0.0.0" \
PG_HOST="127.0.0.1" \
PG_PORT="$PG_PORT" \
PG_USER="postgres" \
PG_PASSWORD="postgres" \
PG_DB="queen" \
NUM_WORKERS="$NUM_WORKERS" \
DB_POOL_SIZE="$DB_POOL_SIZE" \
SIDECAR_POOL_SIZE="$SIDECAR_POOL_SIZE" \
POP_WAIT_INITIAL_INTERVAL_MS="$POP_WAIT_INITIAL_INTERVAL_MS" \
POP_WAIT_BACKOFF_THRESHOLD="$POP_WAIT_BACKOFF_THRESHOLD" \
POP_WAIT_BACKOFF_MULTIPLIER="$POP_WAIT_BACKOFF_MULTIPLIER" \
POP_WAIT_MAX_INTERVAL_MS="$POP_WAIT_MAX_INTERVAL_MS" \
SIDECAR_MICRO_BATCH_WAIT_MS="$SIDECAR_MICRO_BATCH_WAIT_MS" \
  "$BIN" > "$LOG_FILE" 2>&1 &
echo $! > "$PID_FILE"

echo "[server-up] pid=$(cat "$PID_FILE") log=$LOG_FILE"

# Wait for /health
for i in $(seq 1 60); do
  if curl -sf "http://127.0.0.1:${SERVER_PORT}/health" >/dev/null 2>&1; then
    echo "[server-up] healthy after ${i}s"
    exit 0
  fi
  if ! kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "[server-up] ERROR: server died. Tail of log:" >&2
    tail -n 50 "$LOG_FILE" >&2 || true
    exit 1
  fi
  sleep 1
done
echo "[server-up] ERROR: server never became healthy in 60s. Tail of log:" >&2
tail -n 50 "$LOG_FILE" >&2 || true
kill -9 "$(cat "$PID_FILE")" 2>/dev/null || true
exit 1
