#!/usr/bin/env bash
# Three-way bench at 30k partitions to isolate:
#   v1            - FOR UPDATE SKIP LOCKED, no ORDER BY (current main)
#   v2            - advisory xact lock + ORDER BY pc.last_consumed_at
#   v2_noorder    - advisory xact lock, NO ORDER BY (diagnostic variant)
#
# The v1 vs v2 delta = advisory-lock win + ORDER BY cost
# The v2 vs v2_noorder delta = pure ORDER BY cost at this partition count
# The v1 vs v2_noorder delta = pure advisory-lock win (ordering neutral)
#
# Run-order alternating: v1, v2, v2_noorder, v1, v2, v2_noorder, ...

set -euo pipefail

PG_PORT="${PG_PORT:-5434}"
PG_CPUS="${PG_CPUS:-6}"
PG_MEM_GB="${PG_MEM_GB:-8}"
PARTITIONS="${PARTITIONS:-30000}"
PUSH_WORKERS="${PUSH_WORKERS:-12}"
POP_WORKERS="${POP_WORKERS:-12}"
PUSH_BATCH="${PUSH_BATCH:-40}"
POP_BATCH="${POP_BATCH:-100}"
WARMUP="${WARMUP:-5}"
DURATION="${DURATION:-15}"
PRE_PUSH="${PRE_PUSH:-150000}"
RUNS="${RUNS:-2}"
VERBOSE="${VERBOSE:-0}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT_DIR="${OUT_DIR:-$HERE/out-${PARTITIONS}}"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

PG_CONTAINER="queen-collision-pg"
PG_VOLUME="queen-collision-pg-data"
PG_URL="postgres://postgres:postgres@localhost:${PG_PORT}/queen"

log() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }

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

log "Applying base schema"
docker exec -i "$PG_CONTAINER" psql -U postgres -d queen -v ON_ERROR_STOP=1 < "$ROOT/lib/schema/schema.sql" > "$OUT_DIR/schema-apply.log" 2>&1

log "Applying all procedures (v1, v2, v2_noorder)"
for f in "$ROOT/lib/schema/procedures/"*.sql; do
  echo "  apply $(basename "$f")"
  docker exec -i "$PG_CONTAINER" psql -U postgres -d queen -v ON_ERROR_STOP=1 < "$f" >> "$OUT_DIR/schema-apply.log" 2>&1
done

docker exec "$PG_CONTAINER" psql -U postgres -d queen -tAc "
  SELECT string_agg(proname, ',' ORDER BY proname)
  FROM pg_proc
  WHERE proname IN ('pop_unified_batch','pop_unified_batch_v2','pop_unified_batch_v3')
    AND pronamespace = (SELECT oid FROM pg_namespace WHERE nspname='queen')
" | tee "$OUT_DIR/installed-procs.txt"

log "Preparing JS deps"
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
  log "Run #${RUN_IDX} --procedure ${PROC} (partitions=${PARTITIONS})"
  reset_state
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

log "Running $RUNS repeats of each variant in alternating order (v1 -> v2 -> v3)"
for i in $(seq 1 "$RUNS"); do
  run_bench "pop_unified_batch"              "$i"
  run_bench "pop_unified_batch_v2"           "$i"
  run_bench "pop_unified_batch_v3"           "$i"
done

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

const v1  = load('pop_unified_batch');
const v2  = load('pop_unified_batch_v2');
const v2n = load('pop_unified_batch_v3');

function stat(arr, p) {
  const vals = arr.map(r => {
    let v = r; for (const k of p.split('.')) v = v?.[k];
    return Number(v) || 0;
  });
  const mean = vals.reduce((a, b) => a + b, 0) / vals.length;
  const variance = vals.reduce((a, b) => a + (b - mean) ** 2, 0) / vals.length;
  const std = Math.sqrt(variance);
  return { mean, std, samples: vals };
}

function pct(base, cmp) {
  if (base === 0) return cmp === 0 ? '   0%' : '+INF%';
  const d = ((cmp - base) / base) * 100;
  const sign = d >= 0 ? '+' : '';
  return `${sign}${d.toFixed(1)}%`;
}

function fmt(x) {
  if (!isFinite(x)) return String(x);
  return x < 10 ? x.toFixed(2) : x < 100 ? x.toFixed(1) : x.toFixed(0);
}

function row(label, p, higherIsBetter) {
  const a = stat(v1, p);
  const b = stat(v2, p);
  const c = stat(v2n, p);
  const aStr = `${fmt(a.mean)}±${fmt(a.std)}`;
  const bStr = `${fmt(b.mean)}±${fmt(b.std)}`;
  const cStr = `${fmt(c.mean)}±${fmt(c.std)}`;
  console.log(`  ${label.padEnd(30)} ${aStr.padStart(15)} | ${bStr.padStart(15)} (${pct(a.mean, b.mean).padStart(7)}) | ${cStr.padStart(15)} (${pct(a.mean, c.mean).padStart(7)})   [v2 vs v2n: ${pct(c.mean, b.mean)}]`);
}

console.log('');
console.log(`  N = ${runs} runs per variant, alternating order.`);
console.log(`  Baseline for % deltas = v1.`);
console.log(`  v2 vs v2n delta = pure ORDER BY cost.`);
console.log('');
console.log(`  ${' '.repeat(30)} ${'v1'.padStart(15)} | ${'v2 (adv+ORDER)'.padStart(15)} ${'vs v1'.padStart(9)}   | ${'v2_noorder'.padStart(15)} ${'vs v1'.padStart(9)}`);
console.log('  ' + '-'.repeat(130));
console.log('  THROUGHPUT (higher is better)');
row('push ops/s',             'push.opsPerSec',     true);
row('push msgs/s',            'push.msgsPerSec',    true);
row('pop  ops/s',             'pop.opsPerSec',      true);
row('pop  msgs/s',            'pop.msgsPerSec',     true);
console.log('');
console.log('  LATENCY (lower is better)');
row('push p50 ms',            'push.latencyMs.p50', false);
row('push p95 ms',            'push.latencyMs.p95', false);
row('push p99 ms',            'push.latencyMs.p99', false);
row('pop  p50 ms',            'pop.latencyMs.p50',  false);
row('pop  p95 ms',            'pop.latencyMs.p95',  false);
row('pop  p99 ms',            'pop.latencyMs.p99',  false);
row('pop  max ms',            'pop.latencyMs.max',  false);
console.log('');
console.log('  COLLISIONS');
row('Lock/tuple samples',     'locks.tupleWaits',                false);
row('Lock/transactionid',     'locks.transactionidWaits',        false);

console.log('');
console.log('  PER-RUN pop ops/s:');
console.log(`    v1:     ${stat(v1,  'pop.opsPerSec').samples.map(fmt).join('  ')}`);
console.log(`    v2:     ${stat(v2,  'pop.opsPerSec').samples.map(fmt).join('  ')}`);
console.log(`    v2_no:  ${stat(v2n, 'pop.opsPerSec').samples.map(fmt).join('  ')}`);
console.log('  PER-RUN pop p99 ms:');
console.log(`    v1:     ${stat(v1,  'pop.latencyMs.p99').samples.map(fmt).join('  ')}`);
console.log(`    v2:     ${stat(v2,  'pop.latencyMs.p99').samples.map(fmt).join('  ')}`);
console.log(`    v2_no:  ${stat(v2n, 'pop.latencyMs.p99').samples.map(fmt).join('  ')}`);
console.log('');
NODE

log "Raw JSON kept at $OUT_DIR/"
log "Done."
