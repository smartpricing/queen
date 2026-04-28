#!/usr/bin/env bash
# Orchestrator: bring up a clean local PG, apply schema + BOTH procedures,
# run the collision benchmark N times per variant in an alternating order
# (v1, v2, v1, v2, ...) with a full data reset between every single run,
# then print mean ± std-dev comparison.
#
# This design controls for:
#   - run-order bias (warming buffers, autovacuum cycles, WAL pre-allocation)
#     by alternating variants and running each >= 3 times.
#   - state-pollution bias by TRUNCATEing all queue-state tables between
#     every run so each bench starts from an empty schema.
#   - single-sample noise by reporting mean and std dev across repeats.
#
# Env overrides:
#   PG_PORT            (default 5434)
#   PG_CPUS            (default 4)
#   PG_MEM_GB          (default 4)
#   PARTITIONS         (default 150)
#   PUSH_WORKERS       (default 12)
#   POP_WORKERS        (default 12)
#   PUSH_BATCH         (default 40)
#   POP_BATCH          (default 100)
#   WARMUP             (default 3)
#   DURATION           (default 20)
#   PRE_PUSH           (default 30000)
#   RUNS               (default 3)  # runs per variant
#   VERBOSE            (default 0)

set -euo pipefail

PG_PORT="${PG_PORT:-5434}"
PG_CPUS="${PG_CPUS:-4}"
PG_MEM_GB="${PG_MEM_GB:-4}"
PARTITIONS="${PARTITIONS:-150}"
PUSH_WORKERS="${PUSH_WORKERS:-12}"
POP_WORKERS="${POP_WORKERS:-12}"
PUSH_BATCH="${PUSH_BATCH:-40}"
POP_BATCH="${POP_BATCH:-100}"
WARMUP="${WARMUP:-3}"
DURATION="${DURATION:-20}"
PRE_PUSH="${PRE_PUSH:-30000}"
RUNS="${RUNS:-3}"
VERBOSE="${VERBOSE:-0}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT_DIR="$HERE/out"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

PG_CONTAINER="queen-collision-pg"
PG_VOLUME="queen-collision-pg-data"
PG_URL="postgres://postgres:postgres@localhost:${PG_PORT}/queen"

log() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }

# ---------------------------------------------------------------------------
# 1. Bring up a fresh PG
# ---------------------------------------------------------------------------
log "Bringing up fresh PostgreSQL container"
docker rm -f "$PG_CONTAINER" >/dev/null 2>&1 || true
docker volume rm "$PG_VOLUME" >/dev/null 2>&1 || true

SHARED_BUFFERS_MB=$((PG_MEM_GB * 256))
EFFECTIVE_CACHE_MB=$((PG_MEM_GB * 512))
MAX_WAL_MB=$((PG_MEM_GB * 256))

docker run -d \
  --name "$PG_CONTAINER" \
  --cpus="$PG_CPUS" \
  --memory="${PG_MEM_GB}g" \
  -p "${PG_PORT}:5432" \
  -e POSTGRES_PASSWORD=postgres \
  -e POSTGRES_DB=queen \
  -v "${PG_VOLUME}:/var/lib/postgresql/data" \
  postgres:16 \
  postgres \
    -c "shared_buffers=${SHARED_BUFFERS_MB}MB" \
    -c "effective_cache_size=${EFFECTIVE_CACHE_MB}MB" \
    -c "work_mem=16MB" \
    -c "max_connections=200" \
    -c "synchronous_commit=on" \
    -c "max_wal_size=${MAX_WAL_MB}MB" \
    -c "checkpoint_completion_target=0.9" \
    -c "log_min_duration_statement=5000" \
  >/dev/null

echo "waiting for PG"
for _ in $(seq 1 60); do
  docker exec "$PG_CONTAINER" psql -U postgres -d queen -tAc "SELECT 1" >/dev/null 2>&1 && break
  sleep 1
done

# ---------------------------------------------------------------------------
# 2. Apply schema + ALL procedures (including 002b v2) up front
# ---------------------------------------------------------------------------
log "Applying base schema"
docker exec -i "$PG_CONTAINER" psql -U postgres -d queen -v ON_ERROR_STOP=1 < "$ROOT/lib/schema/schema.sql" > "$OUT_DIR/schema-apply.log" 2>&1

log "Applying all procedures (both v1 pop_unified_batch and v2 pop_unified_batch_v2)"
for f in "$ROOT/lib/schema/procedures/"*.sql; do
  echo "  apply $(basename "$f")"
  docker exec -i "$PG_CONTAINER" psql -U postgres -d queen -v ON_ERROR_STOP=1 < "$f" >> "$OUT_DIR/schema-apply.log" 2>&1
done

# Sanity: both functions must exist before we benchmark anything.
docker exec "$PG_CONTAINER" psql -U postgres -d queen -tAc "
  SELECT string_agg(proname, ',' ORDER BY proname)
  FROM pg_proc
  WHERE proname IN ('pop_unified_batch','pop_unified_batch_v2')
    AND pronamespace = (SELECT oid FROM pg_namespace WHERE nspname='queen')
" | tee "$OUT_DIR/installed-procs.txt"

# ---------------------------------------------------------------------------
# 3. JS deps
# ---------------------------------------------------------------------------
log "Preparing JS deps (pg driver)"
cd "$HERE"
if [ ! -d node_modules/pg ]; then
  cat > package.json <<'EOF'
{
  "name": "collision-test",
  "version": "1.0.0",
  "private": true,
  "type": "module",
  "dependencies": { "pg": "^8.11.0" }
}
EOF
  npm install --silent --no-audit --no-fund
fi

V_FLAG=""
[ "$VERBOSE" = "1" ] && V_FLAG="--verbose"

# ---------------------------------------------------------------------------
# 4. Reset function: truncate all queue-state tables between runs.
#    Each test call creates its own queue + partitions; truncating resets to
#    a pristine baseline so v1 and v2 benches start from identical state.
# ---------------------------------------------------------------------------
reset_state() {
  docker exec -i "$PG_CONTAINER" psql -U postgres -d queen -v ON_ERROR_STOP=1 <<'SQL' >/dev/null 2>&1
    TRUNCATE
      queen.messages,
      queen.partition_lookup,
      queen.partition_consumers,
      queen.consumer_watermarks,
      queen.consumer_groups_metadata,
      queen.messages_consumed,
      queen.dead_letter_queue,
      queen.message_traces,
      queen.partitions,
      queen.queues
    CASCADE;
    -- Ensure the planner sees empty tables (otherwise it may still use
    -- stale relpages / reltuples estimates from prior runs).
    ANALYZE
      queen.messages,
      queen.partition_lookup,
      queen.partition_consumers,
      queen.partitions,
      queen.queues;
    SELECT pg_stat_reset();
SQL
}

run_bench() {
  local PROC="$1"
  local RUN_IDX="$2"
  local OUT="$OUT_DIR/${PROC}-run${RUN_IDX}.json"
  log "Run #${RUN_IDX} --procedure ${PROC}"
  reset_state
  # Queue name includes procedure + run index so the bench writes into a
  # fresh queue each time and produces deterministic, traceable artifacts.
  local QUEUE_NAME="bench-${PROC}-${RUN_IDX}"
  PG_URL="$PG_URL" node "$HERE/test-collision.mjs" \
    --procedure "$PROC" \
    --queue "$QUEUE_NAME" \
    --partitions "$PARTITIONS" \
    --push-workers "$PUSH_WORKERS" \
    --pop-workers  "$POP_WORKERS" \
    --push-batch   "$PUSH_BATCH" \
    --pop-batch    "$POP_BATCH" \
    --warmup       "$WARMUP" \
    --duration     "$DURATION" \
    --pre-push     "$PRE_PUSH" \
    --output       "$OUT" \
    $V_FLAG
}

# ---------------------------------------------------------------------------
# 5. Alternating-order benchmark: v1, v2, v1, v2, ..., RUNS times each.
# ---------------------------------------------------------------------------
log "Running $RUNS repeats of each variant in alternating order"
for i in $(seq 1 "$RUNS"); do
  run_bench "pop_unified_batch"    "$i"
  run_bench "pop_unified_batch_v2" "$i"
done

# ---------------------------------------------------------------------------
# 6. Aggregate and compare
# ---------------------------------------------------------------------------
log "Aggregating"
node - "$OUT_DIR" "$RUNS" <<'NODE'
const fs = require('fs');
const path = require('path');

const [, , outDir, runsStr] = process.argv;
const runs = Number(runsStr);

function load(proc) {
  return Array.from({length: runs}, (_, i) =>
    JSON.parse(fs.readFileSync(path.join(outDir, `${proc}-run${i + 1}.json`)))
  );
}

const v1 = load('pop_unified_batch');
const v2 = load('pop_unified_batch_v2');

function stat(arr, path) {
  const vals = arr.map(r => {
    let v = r; for (const k of path.split('.')) v = v?.[k];
    return Number(v) || 0;
  });
  const mean = vals.reduce((a, b) => a + b, 0) / vals.length;
  const variance = vals.reduce((a, b) => a + (b - mean) ** 2, 0) / vals.length;
  const std = Math.sqrt(variance);
  return { mean, std, samples: vals };
}

function pct(a, b) {
  if (a === 0) return b === 0 ? '   0%' : '+INF%';
  const d = ((b - a) / a) * 100;
  const sign = d >= 0 ? '+' : '';
  return `${sign}${d.toFixed(1)}%`;
}

function overlaps(a, b) {
  // Returns true if the [mean±std] intervals overlap.
  // Useful as a very loose "is the delta bigger than the noise?" check.
  const aLo = a.mean - a.std, aHi = a.mean + a.std;
  const bLo = b.mean - b.std, bHi = b.mean + b.std;
  return !(aHi < bLo || bHi < aLo);
}

function row(label, path, higherIsBetter) {
  const a = stat(v1, path);
  const b = stat(v2, path);
  const delta = pct(a.mean, b.mean);
  const color = a.mean === b.mean ? ''
    : higherIsBetter
      ? (b.mean > a.mean ? '\x1b[32m' : '\x1b[31m')
      : (b.mean < a.mean ? '\x1b[32m' : '\x1b[31m');
  const noisy = overlaps(a, b) ? ' (noisy)' : '';
  const fmt = (x) => x < 10 ? x.toFixed(2) : x < 100 ? x.toFixed(1) : x.toFixed(0);
  const aStr = `${fmt(a.mean)} ± ${fmt(a.std)}`;
  const bStr = `${fmt(b.mean)} ± ${fmt(b.std)}`;
  console.log(`  ${label.padEnd(34)} ${aStr.padStart(18)}  →  ${bStr.padStart(18)}   ${color}${delta}\x1b[0m${noisy}`);
}

console.log('');
console.log(`  N = ${runs} runs per variant, alternating order, state reset between every run.`);
console.log('');
console.log(`                                           v1 (FOR UPDATE)       v2 (advisory)       delta`);
console.log('  ' + '-'.repeat(100));
console.log('  THROUGHPUT (higher is better)');
row('push ops/s',            'push.opsPerSec',  true);
row('push msgs/s',           'push.msgsPerSec', true);
row('pop ops/s',             'pop.opsPerSec',   true);
row('pop msgs/s',            'pop.msgsPerSec',  true);
console.log('');
console.log('  LATENCY (lower is better)');
row('push p50 ms',           'push.latencyMs.p50', false);
row('push p95 ms',           'push.latencyMs.p95', false);
row('push p99 ms',           'push.latencyMs.p99', false);
row('push max ms',           'push.latencyMs.max', false);
row('pop  p50 ms',           'pop.latencyMs.p50',  false);
row('pop  p95 ms',           'pop.latencyMs.p95',  false);
row('pop  p99 ms',           'pop.latencyMs.p99',  false);
console.log('');
console.log('  COLLISIONS (lock waits sampled every 100ms, lower is better)');
row('total Lock/tuple samples',         'locks.tupleWaits',               false);
row('total Lock/transactionid samples', 'locks.transactionidWaits',       false);
row('avg tuple-blocked per sample',     'locks.avgTupleBlockedPerSample', false);
row('avg xid-blocked per sample',       'locks.avgXidBlockedPerSample',   false);
console.log('');

// Per-run sanity dump so the mean/std aren't hiding weirdness.
console.log('  PER-RUN push ops/s (lists samples):');
console.log(`    v1: ${stat(v1, 'push.opsPerSec').samples.map(x => x.toFixed(1)).join('  ')}`);
console.log(`    v2: ${stat(v2, 'push.opsPerSec').samples.map(x => x.toFixed(1)).join('  ')}`);
console.log('  PER-RUN pop ops/s:');
console.log(`    v1: ${stat(v1, 'pop.opsPerSec').samples.map(x => x.toFixed(1)).join('  ')}`);
console.log(`    v2: ${stat(v2, 'pop.opsPerSec').samples.map(x => x.toFixed(1)).join('  ')}`);
console.log('  PER-RUN Lock/tuple samples:');
console.log(`    v1: ${stat(v1, 'locks.tupleWaits').samples.join('  ')}`);
console.log(`    v2: ${stat(v2, 'locks.tupleWaits').samples.join('  ')}`);
console.log('');
NODE

log "Raw JSON kept at $OUT_DIR/"
log "Done."
