#!/usr/bin/env bash
# Execute ONE run for ONE scenario.
#
# Usage:
#   SCENARIO_JSON=... RUN_INDEX=1 OUT_DIR=... ./run-once.sh
#
# Required env:
#   SCENARIO_JSON   A JSON blob (one object from scenarios.json#scenarios[*])
#                   with defaults already merged in (caller's responsibility).
#   RUN_INDEX       1, 2, 3, ...
#   OUT_DIR         Absolute path to this run's output directory.
#
# Optional env (with sane defaults):
#   WARMUP_SECONDS  (default 10)
#   MEASURE_SECONDS (default 60)
#   SERVER_PORT     (default 6632)
#   PG_PORT         (default 5434)
#   QUEUE_NAME      (default perf-test)
#   MAX_PARTITIONS  (default 500)
#   SIDECAR_POOL_SIZE     (default 200)
#   DB_POOL_SIZE_PER_WORKER (default 20)
#   PG_MAX_CONN     (default 200)
set -euo pipefail

: "${SCENARIO_JSON:?SCENARIO_JSON required}"
: "${RUN_INDEX:?RUN_INDEX required}"
: "${OUT_DIR:?OUT_DIR required}"

WARMUP_SECONDS="${WARMUP_SECONDS:-10}"
MEASURE_SECONDS="${MEASURE_SECONDS:-60}"
SERVER_PORT="${SERVER_PORT:-6632}"
PG_PORT="${PG_PORT:-5434}"
QUEUE_NAME="${QUEUE_NAME:-perf-test}"
MAX_PARTITIONS="${MAX_PARTITIONS:-500}"
SIDECAR_POOL_SIZE="${SIDECAR_POOL_SIZE:-200}"
DB_POOL_SIZE="${DB_POOL_SIZE:-20}"
PG_MAX_CONN="${PG_MAX_CONN:-300}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

# Parse scenario with jq-free approach: use node (already available)
read_json() {
  node -e "const s=JSON.parse(process.env.SCENARIO_JSON); process.stdout.write(String($1))"
}

SC_ID=$(read_json "s.id")
SC_NAME=$(read_json "s.name")
PG_CPUS=$(read_json "s.pg.cpus")
PG_MEM_GB=$(read_json "s.pg.memory_gb")
NUM_WORKERS=$(read_json "s.queen.num_workers")
PROD_WORKERS=$(read_json "s.producer.workers")
PROD_CONN=$(read_json "s.producer.connections")
CONS_WORKERS=$(read_json "s.consumer.workers")
CONS_CONN=$(read_json "s.consumer.connections")
PUSH_BATCH=$(read_json "s.push_batch ?? 10")
POP_BATCH=$(read_json "s.pop_batch ?? 100")

mkdir -p "$OUT_DIR"

# Save the full resolved config for this run
cat > "$OUT_DIR/config.json" <<EOF
{
  "scenario_id": $SC_ID,
  "scenario_name": "$SC_NAME",
  "run_index": $RUN_INDEX,
  "pg":       { "cpus": $PG_CPUS, "memory_gb": $PG_MEM_GB, "max_connections": $PG_MAX_CONN },
  "queen":    { "num_workers": $NUM_WORKERS, "db_pool_size": $DB_POOL_SIZE, "sidecar_pool_size": $SIDECAR_POOL_SIZE, "sidecar_micro_batch_wait_ms": ${SIDECAR_MICRO_BATCH_WAIT_MS:-5} },
  "producer": { "workers": $PROD_WORKERS, "connections": $PROD_CONN, "push_batch": $PUSH_BATCH },
  "consumer": { "workers": $CONS_WORKERS, "connections": $CONS_CONN, "pop_batch": $POP_BATCH },
  "timing":   { "warmup_seconds": $WARMUP_SECONDS, "measure_seconds": $MEASURE_SECONDS },
  "queue_name": "$QUEUE_NAME",
  "max_partitions": $MAX_PARTITIONS,
  "start_iso": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF

echo "=========================================================="
echo "  Scenario $SC_ID ($SC_NAME), run $RUN_INDEX"
echo "  PG: ${PG_CPUS}c/${PG_MEM_GB}g  Queen: ${NUM_WORKERS}w  Prod: ${PROD_WORKERS}w/${PROD_CONN}c  Cons: ${CONS_WORKERS}w/${CONS_CONN}c"
echo "  Out: $OUT_DIR"
echo "=========================================================="

# Thread-budget warning: autocannon worker threads + queen workers compete for host CPUs.
# docker-contained PG runs in its own VM so we don't count it, but it still competes
# with the host at the VM boundary. Heuristic: warn only when heavy threads >= host_cpus
# (one per core is fine, oversubscription starts at more-than-one-per-core).
HOST_CPUS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 0)"
HEAVY_THREADS=$(( PROD_WORKERS + CONS_WORKERS + NUM_WORKERS ))
if [[ "$HOST_CPUS" -gt 0 && "$HEAVY_THREADS" -ge "$HOST_CPUS" ]]; then
  echo "[run-once] WARNING: ${HEAVY_THREADS} heavy host threads (prod=${PROD_WORKERS} + cons=${CONS_WORKERS} + queen=${NUM_WORKERS}) on ${HOST_CPUS} CPUs."
  echo "[run-once]          Host is over-subscribed; results may reflect OS scheduler contention rather than queen/PG."
  node -e "
const fs = require('fs');
const p = '$OUT_DIR/config.json';
const c = JSON.parse(fs.readFileSync(p,'utf8'));
c.host_cpus = $HOST_CPUS;
c.heavy_host_threads = $HEAVY_THREADS;
c.host_oversubscribed = true;
fs.writeFileSync(p, JSON.stringify(c, null, 2));
"
else
  node -e "
const fs = require('fs');
const p = '$OUT_DIR/config.json';
const c = JSON.parse(fs.readFileSync(p,'utf8'));
c.host_cpus = $HOST_CPUS;
c.heavy_host_threads = $HEAVY_THREADS;
c.host_oversubscribed = false;
fs.writeFileSync(p, JSON.stringify(c, null, 2));
"
fi

# 1. Fresh PG
PG_CPUS="$PG_CPUS" PG_MEM_GB="$PG_MEM_GB" PG_PORT="$PG_PORT" PG_MAX_CONN="$PG_MAX_CONN" \
  bash "$HERE/pg-up.sh"

STREAM_PID=""
cleanup() {
  local ec=$?
  echo "[run-once] cleanup (exit=$ec)"
  if [[ -n "$STREAM_PID" ]]; then
    kill "$STREAM_PID" 2>/dev/null || true
    wait "$STREAM_PID" 2>/dev/null || true
  fi
  PID_FILE="$OUT_DIR/server.pid" bash "$HERE/server-down.sh" || true
  bash "$HERE/pg-down.sh" || true
  return $ec
}
trap cleanup EXIT

# 2. Server
NUM_WORKERS="$NUM_WORKERS" \
DB_POOL_SIZE="$DB_POOL_SIZE" \
SIDECAR_POOL_SIZE="$SIDECAR_POOL_SIZE" \
PG_PORT="$PG_PORT" \
SERVER_PORT="$SERVER_PORT" \
LOG_FILE="$OUT_DIR/server.log" \
PID_FILE="$OUT_DIR/server.pid" \
  bash "$HERE/server-up.sh"

# 3. Producer warm-up (discarded)
echo "[run-once] producer warm-up: ${WARMUP_SECONDS}s (discarded)"
# We intentionally kick the warm-up BEFORE taking the `pre` PG-stats snapshot
# so JIT/page-cache effects don't pollute the measurement window. The `pre`
# snapshot is taken AFTER warm-up and BEFORE measurement starts.
(
  cd "$HERE/../workload"
  SERVER_URL="http://127.0.0.1:${SERVER_PORT}" \
  QUEUE_NAME="$QUEUE_NAME" \
  WORKERS="$PROD_WORKERS" \
  CONNECTIONS="$PROD_CONN" \
  DURATION="$WARMUP_SECONDS" \
  PUSH_BATCH="$PUSH_BATCH" \
  MAX_PARTITIONS="$MAX_PARTITIONS" \
  WARMUP="1" \
  OUTPUT_FILE="$OUT_DIR/producer-warmup.json" \
    node producer.js
) || echo "[run-once] WARNING: warmup producer exited non-zero"

# 4. Pre-measurement PG snapshot (ground truth t=0)
OUT_DIR="$OUT_DIR" LABEL="pre" bash "$HERE/collect-pg-stats.sh" || \
  echo "[run-once] WARNING: pre-measurement pg-stats snapshot failed"

# 5. Start background stats streamer (samples docker stats + server CPU every 2s)
SERVER_PID_VAL="$(cat "$OUT_DIR/server.pid" 2>/dev/null || echo 0)"
if [[ "$SERVER_PID_VAL" -gt 0 ]]; then
  OUT_DIR="$OUT_DIR" SERVER_PID="$SERVER_PID_VAL" bash "$HERE/stats-stream.sh" \
    > "$OUT_DIR/stats-stream.log" 2>&1 &
  STREAM_PID=$!
  echo "[run-once] stats-stream pid=$STREAM_PID"
else
  STREAM_PID=""
fi

# 6. Measurement: producer + consumer in parallel, both for MEASURE_SECONDS
echo "[run-once] measurement: ${MEASURE_SECONDS}s"
MEASURE_START=$(date +%s)
(
  cd "$HERE/../workload"

  (
    SERVER_URL="http://127.0.0.1:${SERVER_PORT}" \
    QUEUE_NAME="$QUEUE_NAME" \
    WORKERS="$PROD_WORKERS" \
    CONNECTIONS="$PROD_CONN" \
    DURATION="$MEASURE_SECONDS" \
    PUSH_BATCH="$PUSH_BATCH" \
    MAX_PARTITIONS="$MAX_PARTITIONS" \
    OUTPUT_FILE="$OUT_DIR/producer.json" \
      node producer.js
  ) &
  PROD_PID=$!

  (
    SERVER_URL="http://127.0.0.1:${SERVER_PORT}" \
    QUEUE_NAME="$QUEUE_NAME" \
    WORKERS="$CONS_WORKERS" \
    CONNECTIONS="$CONS_CONN" \
    DURATION="$MEASURE_SECONDS" \
    POP_BATCH="$POP_BATCH" \
    MAX_PARTITIONS="$MAX_PARTITIONS" \
    OUTPUT_FILE="$OUT_DIR/consumer.json" \
      node consumer.js
  ) &
  CONS_PID=$!

  wait "$PROD_PID" || echo "[run-once] producer exited non-zero"
  wait "$CONS_PID" || echo "[run-once] consumer exited non-zero"
)
MEASURE_END=$(date +%s)

# 7. Stop stats streamer
if [[ -n "$STREAM_PID" ]]; then
  kill "$STREAM_PID" 2>/dev/null || true
  wait "$STREAM_PID" 2>/dev/null || true
fi

# 8. Post-measurement PG snapshot (ground truth t=end)
OUT_DIR="$OUT_DIR" LABEL="post" bash "$HERE/collect-pg-stats.sh" || \
  echo "[run-once] WARNING: post-measurement pg-stats snapshot failed"

# 9. Record measurement window timestamps for downstream analysis
node -e "
const fs = require('fs');
const p = '$OUT_DIR/config.json';
const c = JSON.parse(fs.readFileSync(p,'utf8'));
c.measure_start_epoch = $MEASURE_START;
c.measure_end_epoch = $MEASURE_END;
c.measure_actual_seconds = $MEASURE_END - $MEASURE_START;
fs.writeFileSync(p, JSON.stringify(c, null, 2));
"

# 10. Collect artifacts
OUT_DIR="$OUT_DIR" \
SERVER_PORT="$SERVER_PORT" \
QUEUE_NAME="$QUEUE_NAME" \
LOG_FILE="$OUT_DIR/server.log" \
  bash "$HERE/collect.sh"

# 6. Append finish time
node -e "
const fs = require('fs');
const p = '$OUT_DIR/config.json';
const c = JSON.parse(fs.readFileSync(p, 'utf8'));
c.finish_iso = new Date().toISOString();
fs.writeFileSync(p, JSON.stringify(c, null, 2));
"

echo "[run-once] scenario $SC_ID run $RUN_INDEX done."
