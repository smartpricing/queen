#!/bin/bash
# Pipeline benchmark runner — Queen edition.
#
# Orchestrates a full run:
#   1. Cleanup any previous pm2/results state.
#   2. Recreate Queen + Postgres containers (fresh DB).
#   3. Pre-create the two pipeline queues (configure.js).
#   4. Start all 31 producer/worker/consumer processes via pm2.
#   5. Wait for the configured duration.
#   6. Stop processes, capture analytics endpoints, run analyze.js.
#
# Inputs (env vars or defaults):
#   DURATION_SEC      — total run duration (default 1200 = 20 min)
#   NUM_PRODUCERS     — default 10
#   NUM_WORKERS       — default 7
#   NUM_ANALYTICS     — default 7
#   NUM_LOG           — default 7
#   NUM_PARTITIONS    — default 10000
#   TARGET_RATE       — per-producer rate, default 1000 msg/s
#   IN_FLIGHT         — per-producer in-flight pushes, default 30
#   CONCURRENCY       — per-consumer concurrency, default 20
#   BATCH_SIZE        — per-consumer pop batch, default 10
#   QUEEN_IMAGE_TAG   — default 0.14.0.alpha.3
#   RESULTS_DIR       — default /tmp/queen-pipeline (per-process JSONL files)
#   OUTDIR            — default /root/bench-runs/pipeline-results/queen-<timestamp>

set -u

DURATION_SEC="${DURATION_SEC:-1200}"
NUM_PRODUCERS="${NUM_PRODUCERS:-2}"
NUM_WORKERS="${NUM_WORKERS:-7}"
NUM_ANALYTICS="${NUM_ANALYTICS:-7}"
NUM_LOG="${NUM_LOG:-7}"
NUM_PARTITIONS="${NUM_PARTITIONS:-1000}"
TARGET_RATE="${TARGET_RATE:-5000}"
IN_FLIGHT="${IN_FLIGHT:-40}"
CONCURRENCY="${CONCURRENCY:-10}"
BATCH_SIZE="${BATCH_SIZE:-100}"
QUEEN_IMAGE_TAG="${QUEEN_IMAGE_TAG:-0.14.0.alpha.3}"
RESULTS_DIR="${RESULTS_DIR:-/tmp/queen-pipeline}"
TS=$(date -u +%Y%m%dT%H%M%SZ)
OUTDIR="${OUTDIR:-/root/bench-runs/pipeline-results/queen-${TS}}"
WARMUP_SEC="${WARMUP_SEC:-180}"

ts() { date -u +%FT%TZ; }
log() { echo "[$(ts)] [pipeline-queen] $*"; }

mkdir -p "$OUTDIR"
mkdir -p "$RESULTS_DIR"
rm -f "$RESULTS_DIR"/*.jsonl 2>/dev/null || true

log "=== START · dur=${DURATION_SEC}s · prod=${NUM_PRODUCERS}@${TARGET_RATE}/s · worker=${NUM_WORKERS}xc=${CONCURRENCY} · analytics=${NUM_ANALYTICS} · log=${NUM_LOG} · parts=${NUM_PARTITIONS} ==="

# ---------- container cleanup ----------
log "cleaning up containers"
docker stop queen postgres 2>/dev/null || true
docker rm -v queen postgres 2>/dev/null || true
docker volume prune -f >/dev/null 2>&1 || true
docker network create queen >/dev/null 2>&1 || true

# ---------- start postgres ----------
log "starting postgres"
docker run -d --name postgres --network queen \
  --ulimit nofile=65535:65535 --shm-size=1g \
  -e POSTGRES_PASSWORD=postgres -p 5432:5432 \
  postgres \
  -c max_connections=300 -c shared_buffers=24GB -c effective_cache_size=48GB \
  -c maintenance_work_mem=2GB -c work_mem=32MB -c temp_buffers=64MB -c huge_pages=try \
  -c max_worker_processes=20 -c max_parallel_workers=20 \
  -c max_parallel_workers_per_gather=4 -c max_parallel_maintenance_workers=4 \
  -c wal_buffers=16MB -c min_wal_size=2GB -c max_wal_size=16GB \
  -c checkpoint_timeout=15min -c checkpoint_completion_target=0.9 \
  -c synchronous_commit=on -c wal_compression=on \
  -c random_page_cost=1.1 -c effective_io_concurrency=200 -c default_statistics_target=200 \
  -c autovacuum_max_workers=4 -c autovacuum_naptime=10s \
  -c autovacuum_vacuum_scale_factor=0.05 -c autovacuum_analyze_scale_factor=0.02 \
  -c autovacuum_vacuum_cost_limit=2000 -c autovacuum_vacuum_cost_delay=2ms \
  -c log_min_duration_statement=1000 -c log_checkpoints=on -c log_lock_waits=on \
  -c log_temp_files=0 -c log_autovacuum_min_duration=0 \
  >/dev/null

for i in $(seq 1 60); do
  docker exec postgres pg_isready -U postgres >/dev/null 2>&1 && { log "postgres ready (after ${i}s)"; break; }
  sleep 1
done

# ---------- start queen ----------
log "starting queen ${QUEEN_IMAGE_TAG}"
docker run -d --ulimit nofile=65535:65535 \
  --name queen -p 6632:6632 --network queen \
  -e PG_HOST=postgres -e PG_PASSWORD=postgres \
  -e NUM_WORKERS=10 -e DB_POOL_SIZE=50 -e SIDECAR_POOL_SIZE=250 \
  "smartnessai/queen-mq:${QUEEN_IMAGE_TAG}" >/dev/null

for i in $(seq 1 120); do
  curl -sf http://localhost:6632/api/v1/status >/dev/null 2>&1 && { log "queen ready (after ${i}s)"; break; }
  sleep 1
done

sleep 5

# ---------- belt-and-suspenders: wipe the file-buffer dir ----------
# `docker rm -v queen` should already have removed the writable layer along
# with /var/lib/queen/buffers, but kill it explicitly so a leftover .buf file
# from any prior run can't make the new test "drain" stale messages on start.
log "wiping queen file-buffer directory"
docker exec queen sh -c 'rm -rf /var/lib/queen/buffers/* 2>/dev/null; ls /var/lib/queen/buffers 2>/dev/null | wc -l'

# ---------- pre-configure queues ----------
log "configuring queues"
node configure.js || { log "FATAL configure failed"; exit 1; }

# ---------- pre-create partitions to avoid creation-burst deadlocks ----------
# Sequentially push 1 message to every (queue, partition) pair so that when
# the real load hits, all partitions already exist. Without this we see a
# burst of ~200-1800 deadlocks on the `queue_lag_metrics` ON CONFLICT row
# in the first 30 seconds (absorbed by file-buffer failover but noisy).
log "pre-creating partitions (warmup)"
NUM_PARTITIONS="$NUM_PARTITIONS" QUEUES="pipe-q1,pipe-q2" \
  node warmup-partitions.js || { log "FATAL warmup failed"; exit 1; }

# ---------- start the pipeline ----------
log "starting pm2 ecosystem (${NUM_PRODUCERS} prod + ${NUM_WORKERS} worker + ${NUM_ANALYTICS} analytics + ${NUM_LOG} log)"

DURATION_SEC="$DURATION_SEC" \
NUM_PRODUCERS="$NUM_PRODUCERS" NUM_WORKERS="$NUM_WORKERS" \
NUM_ANALYTICS="$NUM_ANALYTICS" NUM_LOG="$NUM_LOG" \
NUM_PARTITIONS="$NUM_PARTITIONS" TARGET_RATE="$TARGET_RATE" \
IN_FLIGHT="$IN_FLIGHT" CONCURRENCY="$CONCURRENCY" BATCH_SIZE="$BATCH_SIZE" \
RESULTS_DIR="$RESULTS_DIR" \
pm2 start ecosystem.config.cjs >/dev/null

START_TIME=$(date -u +%FT%T.000Z)
echo "$START_TIME" > "$OUTDIR/start_time.txt"

log "pipeline started · waiting ${DURATION_SEC}s"

# ---------- monitor every 60s ----------
for i in $(seq 1 $((DURATION_SEC / 60))); do
  sleep 60
  PG_S=$(docker stats --no-stream --format "{{.CPUPerc}}/{{.MemUsage}}" postgres 2>/dev/null | head -1)
  QN_S=$(docker stats --no-stream --format "{{.CPUPerc}}/{{.MemUsage}}" queen 2>/dev/null | head -1)
  PROC_COUNT=$(pm2 jlist 2>/dev/null | jq 'map(select(.pm2_env.status=="online")) | length' 2>/dev/null || echo 0)
  log "  +${i}min · postgres=${PG_S} · queen=${QN_S} · live procs=${PROC_COUNT}"
  echo "[$(ts)] postgres=${PG_S} queen=${QN_S} procs=${PROC_COUNT}" >> "$OUTDIR/docker-stats.log"
done

log "duration elapsed · gracefully stopping pipeline (30s drain)"
sleep 30

END_TIME=$(date -u +%FT%T.000Z)
echo "$END_TIME" > "$OUTDIR/end_time.txt"

# ---------- stop everything ----------
pm2 stop all >/dev/null 2>&1 || true
sleep 5
pm2 delete all >/dev/null 2>&1 || true

# ---------- collect server-side metrics ----------
log "collecting analytics endpoints"
curl -s "http://localhost:6632/api/v1/status?from=${START_TIME}&to=${END_TIME}" > "$OUTDIR/status.json"
curl -s "http://localhost:6632/api/v1/analytics/queue-ops?from=${START_TIME}&to=${END_TIME}" > "$OUTDIR/queue-ops.json"
curl -s "http://localhost:6632/api/v1/analytics/system-metrics?from=${START_TIME}&to=${END_TIME}" > "$OUTDIR/system-metrics.json"
curl -s "http://localhost:6632/api/v1/analytics/postgres-stats" > "$OUTDIR/postgres-stats.json"
curl -s "http://localhost:6632/api/v1/resources/queues/pipe-q1" > "$OUTDIR/q1-resource.json"
curl -s "http://localhost:6632/api/v1/resources/queues/pipe-q2" > "$OUTDIR/q2-resource.json"
docker logs queen > "$OUTDIR/queen.log" 2>&1
docker stats --no-stream queen postgres > "$OUTDIR/docker-stats-final.txt" 2>&1

# ---------- copy per-process JSONL files ----------
log "copying per-process JSONL files"
mkdir -p "$OUTDIR/jsonl"
cp -a "$RESULTS_DIR"/*.jsonl "$OUTDIR/jsonl/" 2>/dev/null || true
ls -la "$OUTDIR/jsonl" | head -5

# ---------- run analysis ----------
log "running analyze.js"
RESULTS_DIR="$RESULTS_DIR" WARMUP_SEC="$WARMUP_SEC" \
  node analyze.js > "$OUTDIR/summary.json" 2> "$OUTDIR/summary.txt"

# ---------- save metadata ----------
cat > "$OUTDIR/metadata.json" <<JSON
{
  "system": "queen",
  "queenImageTag": "$QUEEN_IMAGE_TAG",
  "durationSec": $DURATION_SEC,
  "warmupSec": $WARMUP_SEC,
  "numPartitions": $NUM_PARTITIONS,
  "queues": ["pipe-q1", "pipe-q2"],
  "producers": $NUM_PRODUCERS,
  "perProducerTargetRate": $TARGET_RATE,
  "perProducerInFlight": $IN_FLIGHT,
  "workers": $NUM_WORKERS,
  "analytics": $NUM_ANALYTICS,
  "log": $NUM_LOG,
  "perConsumerConcurrency": $CONCURRENCY,
  "perConsumerBatch": $BATCH_SIZE,
  "startTime": "$START_TIME",
  "endTime": "$END_TIME"
}
JSON

log "=== DONE · output dir: $OUTDIR ==="
cat "$OUTDIR/summary.txt"
