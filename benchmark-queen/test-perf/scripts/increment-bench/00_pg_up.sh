#!/usr/bin/env bash
# Spin up an isolated Postgres just for the increment-bench harness.
#
# We deliberately keep this separate from test-perf/scripts/pg-up.sh so the
# two harnesses can coexist (different container name, different volume,
# different port) and so the production bench scripts can be running while
# we iterate on this one.
#
# Env (all optional, sane defaults):
#   PG_PORT       (default 5440)         -- avoid clashing with 5432/5433/5434
#   PG_CONTAINER  (default queen-pg-incbench)
#   PG_VOLUME     (default queen-pg-incbench-data)
#   PG_CPUS       (default 4)
#   PG_MEM_GB     (default 4)
#
# Note: we intentionally use a config close to the long-running benchmark
# (see benchmark-queen/2026-04-25/long-running.md §1.1) so timings here are
# in the same regime as production. Shared buffers/effective cache are
# scaled to the chosen mem.

set -euo pipefail

PG_PORT="${PG_PORT:-5440}"
PG_CONTAINER="${PG_CONTAINER:-queen-pg-incbench}"
PG_VOLUME="${PG_VOLUME:-queen-pg-incbench-data}"
PG_CPUS="${PG_CPUS:-4}"
PG_MEM_GB="${PG_MEM_GB:-4}"

SHARED_BUFFERS_MB=$((PG_MEM_GB * 1024 / 4))      # 25%
EFFECTIVE_CACHE_MB=$((PG_MEM_GB * 1024 / 2))     # 50%
MAINTENANCE_WORK_MB=$((PG_MEM_GB * 1024 / 16))   # ~6%
MAX_WAL_MB=$((PG_MEM_GB * 1024 / 4))             # 25%

echo "[pg-up] tearing down any previous '$PG_CONTAINER' / '$PG_VOLUME'..."
docker rm -f "$PG_CONTAINER" >/dev/null 2>&1 || true
for i in $(seq 1 30); do
  docker inspect "$PG_CONTAINER" >/dev/null 2>&1 || break
  sleep 0.2
done
for i in $(seq 1 30); do
  if ! docker volume inspect "$PG_VOLUME" >/dev/null 2>&1; then break; fi
  if docker volume rm "$PG_VOLUME" >/dev/null 2>&1; then break; fi
  sleep 0.2
done
if docker volume inspect "$PG_VOLUME" >/dev/null 2>&1; then
  echo "[pg-up] ERROR: could not remove previous volume '$PG_VOLUME'." >&2
  exit 1
fi

echo "[pg-up] starting fresh PG: cpus=$PG_CPUS mem=${PG_MEM_GB}g port=$PG_PORT shared_buffers=${SHARED_BUFFERS_MB}MB"
docker run -d \
  --name "$PG_CONTAINER" \
  --cpus="$PG_CPUS" \
  --memory="${PG_MEM_GB}g" \
  --memory-swap="${PG_MEM_GB}g" \
  --shm-size=512m \
  -p "${PG_PORT}:5432" \
  -e POSTGRES_PASSWORD=postgres \
  -e POSTGRES_DB=queen \
  -v "${PG_VOLUME}:/var/lib/postgresql/data" \
  postgres:16 \
  postgres \
    -c "shared_preload_libraries=pg_stat_statements" \
    -c "pg_stat_statements.track=all" \
    -c "pg_stat_statements.max=10000" \
    -c "track_functions=all" \
    -c "shared_buffers=${SHARED_BUFFERS_MB}MB" \
    -c "effective_cache_size=${EFFECTIVE_CACHE_MB}MB" \
    -c "work_mem=64MB" \
    -c "maintenance_work_mem=${MAINTENANCE_WORK_MB}MB" \
    -c "max_connections=100" \
    -c "synchronous_commit=on" \
    -c "fsync=on" \
    -c "full_page_writes=on" \
    -c "wal_level=replica" \
    -c "max_wal_size=${MAX_WAL_MB}MB" \
    -c "min_wal_size=512MB" \
    -c "checkpoint_completion_target=0.9" \
    -c "random_page_cost=1.1" \
    -c "effective_io_concurrency=100" \
    -c "default_statistics_target=200" \
    -c "log_min_duration_statement=200" \
    -c "log_lock_waits=on" \
    -c "log_autovacuum_min_duration=0" \
  >/dev/null

echo "[pg-up] waiting for PG to accept connections..."
for i in $(seq 1 90); do
  if docker exec "$PG_CONTAINER" psql -U postgres -d queen -tAc "SELECT 1" >/dev/null 2>&1; then
    sleep 1
    if docker exec "$PG_CONTAINER" psql -U postgres -d queen -tAc "SELECT 1" >/dev/null 2>&1; then
      echo "[pg-up] ready after ${i}s"
      # Install pg_stat_statements extension
      docker exec "$PG_CONTAINER" psql -U postgres -d queen -tAc "CREATE EXTENSION IF NOT EXISTS pg_stat_statements" >/dev/null
      echo "[pg-up] pg_stat_statements installed"
      exit 0
    fi
  fi
  sleep 1
done
echo "[pg-up] ERROR: PG did not become ready in 90s" >&2
docker logs --tail 50 "$PG_CONTAINER" >&2 || true
exit 1
