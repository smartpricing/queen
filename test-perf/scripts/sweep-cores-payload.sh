#!/usr/bin/env bash
# sweep-cores-payload.sh — rapid sweep over (PG_CORES × PAYLOAD_KB) with
# Vegas uncapped so we can empirically fit a linear relationship between
# PG cores and the Vegas-discovered optimal in-flight concurrency.
#
# Each combo: fresh PG, fresh queen-server with uncapped Vegas, 5 s warmup,
# 30 s measure. n=1 per combo. Outputs a CSV summary + per-combo artifacts.
#
# Usage:
#   ./sweep-cores-payload.sh               # all 12 combos (~12 min)
#   PG_CORES_LIST="1 2 4" ./sweep-cores-payload.sh   # subset
#   MEASURE_SECONDS=60 ./sweep-cores-payload.sh      # longer per combo
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

PG_CORES_LIST="${PG_CORES_LIST:-1 2 3 4 5 6}"
PAYLOAD_KB_LIST="${PAYLOAD_KB_LIST:-1 10}"
PUSH_BATCH="${PUSH_BATCH:-1}"
PROD_WORKERS="${PROD_WORKERS:-4}"
PROD_CONN="${PROD_CONN:-200}"
CONS_WORKERS="${CONS_WORKERS:-1}"
CONS_CONN="${CONS_CONN:-25}"
POP_BATCH="${POP_BATCH:-100}"
NUM_WORKERS="${NUM_WORKERS:-2}"
SIDECAR_POOL_SIZE="${SIDECAR_POOL_SIZE:-200}"
DB_POOL_SIZE="${DB_POOL_SIZE:-20}"
MAX_PARTITIONS="${MAX_PARTITIONS:-500}"
WARMUP_SECONDS="${WARMUP_SECONDS:-5}"
MEASURE_SECONDS="${MEASURE_SECONDS:-30}"
QUEUE_NAME="${QUEUE_NAME:-perf-test}"
SERVER_PORT="${SERVER_PORT:-6632}"
PG_PORT="${PG_PORT:-5434}"
PG_MAX_CONN="${PG_MAX_CONN:-300}"

# Vegas configuration - keep it uncapped so we can *observe* the natural
# steady-state limit. BETA=16 is well below MAX_LIMIT=32, so Vegas has
# room to both grow and shrink.
Q_PUSH_MAX_CONCURRENT="${Q_PUSH_MAX_CONCURRENT:-32}"
Q_ACK_MAX_CONCURRENT="${Q_ACK_MAX_CONCURRENT:-32}"
Q_POP_MAX_CONCURRENT="${Q_POP_MAX_CONCURRENT:-32}"
Q_VEGAS_MAX_LIMIT="${Q_VEGAS_MAX_LIMIT:-32}"
Q_VEGAS_ALPHA="${Q_VEGAS_ALPHA:-3}"
Q_VEGAS_BETA="${Q_VEGAS_BETA:-16}"
Q_PUSH_PREFERRED_BATCH_SIZE="${Q_PUSH_PREFERRED_BATCH_SIZE:-50}"
Q_PUSH_MAX_HOLD_MS="${Q_PUSH_MAX_HOLD_MS:-20}"

STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
OUT_ROOT="$HERE/../results/sweep_${STAMP}"
mkdir -p "$OUT_ROOT"

GIT_SHA="$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || echo unknown)"
GIT_BRANCH="$(cd "$ROOT" && git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"

cat > "$OUT_ROOT/meta.json" <<EOF
{
  "stamp": "$STAMP",
  "git": { "sha": "$GIT_SHA", "branch": "$GIT_BRANCH" },
  "sweep": {
    "pg_cores_list": "$PG_CORES_LIST",
    "payload_kb_list": "$PAYLOAD_KB_LIST",
    "push_batch": $PUSH_BATCH,
    "num_workers": $NUM_WORKERS,
    "producer": { "workers": $PROD_WORKERS, "connections": $PROD_CONN },
    "consumer": { "workers": $CONS_WORKERS, "connections": $CONS_CONN },
    "warmup_s": $WARMUP_SECONDS,
    "measure_s": $MEASURE_SECONDS,
    "vegas": {
      "push_max_concurrent": $Q_PUSH_MAX_CONCURRENT,
      "max_limit": $Q_VEGAS_MAX_LIMIT,
      "alpha": $Q_VEGAS_ALPHA,
      "beta": $Q_VEGAS_BETA
    }
  }
}
EOF

SUMMARY="$OUT_ROOT/summary.csv"
echo "pg_cores,payload_kb,push_batch,pg_ins_per_s,pg_msgs_bytes_per_s,xact_per_s,msgs_per_commit,push_p99_ms,vegas_f_mean,vegas_f_max,vegas_limit_mean,vegas_limit_max,pg_cpu_p95,srv_cpu_p95,push_rps,pop_rps,errors,non2xx" > "$SUMMARY"

echo "=========================================================="
echo "  Sweep campaign: $OUT_ROOT"
echo "  PG cores: $PG_CORES_LIST"
echo "  Payload KB: $PAYLOAD_KB_LIST"
echo "  push_batch=$PUSH_BATCH, warmup=${WARMUP_SECONDS}s, measure=${MEASURE_SECONDS}s, n=1"
echo "=========================================================="

# Ensure server binary exists
if [[ ! -x "$ROOT/server/bin/queen-server" ]]; then
  echo "[sweep] server binary missing, running 'make -C server build-only'..."
  make -C "$ROOT/server" build-only
fi

# Ensure workload node_modules exist
if [[ ! -d "$HERE/../workload/node_modules" ]]; then
  (cd "$HERE/../workload" && npm install --no-audit --no-fund --loglevel=error)
fi

combo_idx=0
for PG_CORES in $PG_CORES_LIST; do
  # Memory: 4 GB for small PG, 8 GB for ≥ 5 cores.
  if [[ "$PG_CORES" -le 4 ]]; then
    PG_MEM_GB=4
  else
    PG_MEM_GB=8
  fi

  for PAYLOAD_KB in $PAYLOAD_KB_LIST; do
    combo_idx=$((combo_idx + 1))
    PAYLOAD_BYTES=$((PAYLOAD_KB * 1024))

    # Batch cap: keep per-batch payload ≤ 500 KB regardless of item size.
    MAX_BATCH=$((500 / PAYLOAD_KB))
    [[ "$MAX_BATCH" -lt 1   ]] && MAX_BATCH=1
    [[ "$MAX_BATCH" -gt 500 ]] && MAX_BATCH=500

    COMBO_NAME="pg${PG_CORES}c_payload${PAYLOAD_KB}kb"
    OUT_DIR="$OUT_ROOT/$COMBO_NAME"
    mkdir -p "$OUT_DIR"

    echo
    echo "=========================================================="
    echo "  [${combo_idx}/$(echo $PG_CORES_LIST | wc -w | tr -d ' ')x$(echo $PAYLOAD_KB_LIST | wc -w | tr -d ' ')] $COMBO_NAME"
    echo "  PG: ${PG_CORES}c/${PG_MEM_GB}g  queen workers=$NUM_WORKERS"
    echo "  Producer: ${PROD_WORKERS}w/${PROD_CONN}c  push_batch=$PUSH_BATCH  payload=${PAYLOAD_KB}KB  max_batch=$MAX_BATCH"
    echo "=========================================================="

    # 1. Fresh PG
    PG_CPUS="$PG_CORES" PG_MEM_GB="$PG_MEM_GB" PG_PORT="$PG_PORT" PG_MAX_CONN="$PG_MAX_CONN" \
      bash "$HERE/pg-up.sh"

    STREAM_PID=""
    CURRENT_PID_FILE="$OUT_DIR/server.pid"
    cleanup_combo() {
      local ec=$?
      if [[ -n "$STREAM_PID" ]]; then
        kill "$STREAM_PID" 2>/dev/null || true
        wait "$STREAM_PID" 2>/dev/null || true
      fi
      PID_FILE="$CURRENT_PID_FILE" bash "$HERE/server-down.sh" || true
      bash "$HERE/pg-down.sh" || true
      return $ec
    }
    trap cleanup_combo ERR

    # 2. Launch queen with uncapped Vegas + per-type override envs
    NUM_WORKERS="$NUM_WORKERS" \
    DB_POOL_SIZE="$DB_POOL_SIZE" \
    SIDECAR_POOL_SIZE="$SIDECAR_POOL_SIZE" \
    PG_PORT="$PG_PORT" \
    SERVER_PORT="$SERVER_PORT" \
    LOG_FILE="$OUT_DIR/server.log" \
    PID_FILE="$CURRENT_PID_FILE" \
    QUEEN_CONCURRENCY_MODE="vegas" \
    QUEEN_PUSH_MAX_CONCURRENT="$Q_PUSH_MAX_CONCURRENT" \
    QUEEN_ACK_MAX_CONCURRENT="$Q_ACK_MAX_CONCURRENT" \
    QUEEN_POP_MAX_CONCURRENT="$Q_POP_MAX_CONCURRENT" \
    QUEEN_PUSH_PREFERRED_BATCH_SIZE="$Q_PUSH_PREFERRED_BATCH_SIZE" \
    QUEEN_PUSH_MAX_BATCH_SIZE="$MAX_BATCH" \
    QUEEN_PUSH_MAX_HOLD_MS="$Q_PUSH_MAX_HOLD_MS" \
    QUEEN_VEGAS_MAX_LIMIT="$Q_VEGAS_MAX_LIMIT" \
    QUEEN_VEGAS_ALPHA="$Q_VEGAS_ALPHA" \
    QUEEN_VEGAS_BETA="$Q_VEGAS_BETA" \
      bash "$HERE/server-up.sh"

    # 3. Warmup (discarded)
    (
      cd "$HERE/../workload"
      SERVER_URL="http://127.0.0.1:${SERVER_PORT}" \
      QUEUE_NAME="$QUEUE_NAME" \
      WORKERS="$PROD_WORKERS" \
      CONNECTIONS="$PROD_CONN" \
      DURATION="$WARMUP_SECONDS" \
      PUSH_BATCH="$PUSH_BATCH" \
      PAYLOAD_SIZE_BYTES="$PAYLOAD_BYTES" \
      MAX_PARTITIONS="$MAX_PARTITIONS" \
      WARMUP="1" \
      OUTPUT_FILE="$OUT_DIR/producer-warmup.json" \
        node producer.js
    ) || echo "[sweep] warmup producer exited non-zero"

    # 4. Pre-measurement PG snapshot
    OUT_DIR="$OUT_DIR" LABEL="pre" bash "$HERE/collect-pg-stats.sh" || true

    # 5. Start stats streamer
    SERVER_PID_VAL="$(cat "$CURRENT_PID_FILE" 2>/dev/null || echo 0)"
    if [[ "$SERVER_PID_VAL" -gt 0 ]]; then
      OUT_DIR="$OUT_DIR" SERVER_PID="$SERVER_PID_VAL" bash "$HERE/stats-stream.sh" \
        > "$OUT_DIR/stats-stream.log" 2>&1 &
      STREAM_PID=$!
    fi

    # 6. Measurement: producer + consumer in parallel
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
        PAYLOAD_SIZE_BYTES="$PAYLOAD_BYTES" \
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
      wait "$PROD_PID" || echo "[sweep] producer exited non-zero"
      wait "$CONS_PID" || echo "[sweep] consumer exited non-zero"
    )
    MEASURE_END=$(date +%s)

    # 7. Stop streamer
    if [[ -n "$STREAM_PID" ]]; then
      kill "$STREAM_PID" 2>/dev/null || true
      wait "$STREAM_PID" 2>/dev/null || true
      STREAM_PID=""
    fi

    # 8. Post-measurement PG snapshot
    OUT_DIR="$OUT_DIR" LABEL="post" bash "$HERE/collect-pg-stats.sh" || true

    # 9. Parse & append CSV row via node
    node --input-type=module -e "
import fs from 'node:fs';
import path from 'node:path';
const dir = '$OUT_DIR';
const pgCores = $PG_CORES;
const payloadKb = $PAYLOAD_KB;
const pushBatch = $PUSH_BATCH;
const measureS = Math.max(1, $MEASURE_END - $MEASURE_START);

function loadJson(p) { try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch { return null; } }
function stats(xs) {
  if (!xs.length) return { mean: null, p95: null, max: null };
  const s = [...xs].sort((a,b)=>a-b);
  return {
    mean: xs.reduce((a,b)=>a+b,0) / xs.length,
    p95: s[Math.floor(s.length*0.95)],
    max: s[s.length-1]
  };
}

// PG truth from pg-stats diff
const pre = loadJson(path.join(dir, 'pg-stats-pre.json')) || {};
const post = loadJson(path.join(dir, 'pg-stats-post.json')) || {};
const qt = (obj, key) => (obj?.queen_messages?.[key] ?? obj?.[key] ?? null);
const insDelta = (post?.queen_messages?.n_tup_ins ?? 0) - (pre?.queen_messages?.n_tup_ins ?? 0);
const xactDelta = (post?.pg_stat_database?.xact_commit ?? 0) - (pre?.pg_stat_database?.xact_commit ?? 0);
const pgIns = insDelta / measureS;
const xact = xactDelta / measureS;
const msgsPerCommit = xact > 0 ? pgIns / xact : 0;

// Autocannon
const prod = loadJson(path.join(dir, 'producer.json')) || {};
const cons = loadJson(path.join(dir, 'consumer.json')) || {};
const pushRps = prod?.requests?.average ?? 0;
const popRps  = cons?.requests?.average ?? 0;
const pushP99 = prod?.latency?.p99 ?? 0;
const errors  = (prod?.errors ?? 0) + (cons?.errors ?? 0);
const non2xx  = (prod?.non2xx ?? 0) + (cons?.non2xx ?? 0);

// Stats stream for CPU%
const streamPath = path.join(dir, 'stats-stream.jsonl');
const pgCpuArr = [], srvCpuArr = [];
if (fs.existsSync(streamPath)) {
  for (const line of fs.readFileSync(streamPath, 'utf8').split('\n')) {
    if (!line.trim()) continue;
    try {
      const j = JSON.parse(line);
      if (j.pg?.CPUPerc) { const v = parseFloat(j.pg.CPUPerc); if (!isNaN(v)) pgCpuArr.push(v); }
      if (typeof j.server?.cpu_pct === 'number') srvCpuArr.push(j.server.cpu_pct);
    } catch {}
  }
}
const pgCpu = stats(pgCpuArr);
const srvCpu = stats(srvCpuArr);

// Vegas observed f=X/Y from server.log libqueen stats lines
const logPath = path.join(dir, 'server.log');
const fArr = [], limArr = [];
if (fs.existsSync(logPath)) {
  const text = fs.readFileSync(logPath, 'utf8');
  for (const line of text.split('\n')) {
    if (!line.includes('[libqueen]') || !line.includes('push(q=')) continue;
    const m = line.match(/push\(q=\d+ f=(\d+)\/(\d+) /);
    if (m) { fArr.push(+m[1]); limArr.push(+m[2]); }
  }
}
const fStats = stats(fArr);
const limStats = stats(limArr);

const row = [
  pgCores, payloadKb, pushBatch,
  pgIns.toFixed(1), (pgIns * payloadKb * 1024).toFixed(0),
  xact.toFixed(1), msgsPerCommit.toFixed(1),
  pushP99,
  fStats.mean?.toFixed(2) ?? '', fStats.max ?? '',
  limStats.mean?.toFixed(2) ?? '', limStats.max ?? '',
  pgCpu.p95?.toFixed(1) ?? '', srvCpu.p95?.toFixed(1) ?? '',
  pushRps.toFixed(1), popRps.toFixed(1),
  errors, non2xx
].join(',');
fs.appendFileSync('$SUMMARY', row + '\n');
console.log('[sweep] result:', row);
"

    # 10. Teardown
    PID_FILE="$CURRENT_PID_FILE" bash "$HERE/server-down.sh" || true
    bash "$HERE/pg-down.sh" || true
    trap - ERR
  done
done

echo
echo "=========================================================="
echo "  Sweep complete: $OUT_ROOT"
echo "  Summary CSV: $SUMMARY"
echo "=========================================================="
cat "$SUMMARY"
