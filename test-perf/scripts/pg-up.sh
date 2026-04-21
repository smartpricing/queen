#!/usr/bin/env bash
# Bring up a fresh, capped PostgreSQL in Docker.
# Volume is wiped first so the schema is re-initialized by the queen server on boot.
#
# Env inputs:
#   PG_CPUS        (required, integer)  e.g. 2
#   PG_MEM_GB      (required, integer)  e.g. 4
#   PG_PORT        (default 5434)
#   PG_CONTAINER   (default queen-pg-bench)
#   PG_VOLUME      (default queen-pg-bench-data)
#   PG_MAX_CONN    (default 200)
set -euo pipefail

: "${PG_CPUS:?PG_CPUS required}"
: "${PG_MEM_GB:?PG_MEM_GB required}"
PG_PORT="${PG_PORT:-5434}"
PG_CONTAINER="${PG_CONTAINER:-queen-pg-bench}"
PG_VOLUME="${PG_VOLUME:-queen-pg-bench-data}"
PG_MAX_CONN="${PG_MAX_CONN:-200}"
# Pin PG to CPUs 0..(PG_CPUS-1) inside the Docker VM. Prevents PG from
# wandering across every CPU the VM has, reducing intra-VM scheduling noise.
# Note: on Docker Desktop (macOS/Windows) these are VM-virtual CPUs, not host cores.
PG_CPUSET="${PG_CPUSET:-0-$((PG_CPUS - 1))}"

# Derived PG tuning (integers, MB units where applicable)
SHARED_BUFFERS_MB=$((PG_MEM_GB * 1024 / 4))                  # 25% of RAM
EFFECTIVE_CACHE_MB=$((PG_MEM_GB * 1024 / 2))                 # 50% of RAM
MAINTENANCE_WORK_MB=$((PG_MEM_GB * 1024 / 16))               # ~6% of RAM
WORK_MEM_MB=16
MAX_WAL_MB=$((PG_MEM_GB * 1024 / 4))                         # 25% of RAM

echo "[pg-up] tearing down any previous container/volume..."
docker rm -f "$PG_CONTAINER" >/dev/null 2>&1 || true
# docker rm -f returns before the container is fully reaped; wait for it to disappear
# before removing the volume, else volume rm silently fails with 'in use' and the next
# 'docker run -v same-volume:...' reuses the OLD data -> schema collisions on restart.
for i in $(seq 1 30); do
  docker inspect "$PG_CONTAINER" >/dev/null 2>&1 || break
  sleep 0.2
done
# Retry volume rm until it really succeeds or the volume no longer exists.
for i in $(seq 1 30); do
  if ! docker volume inspect "$PG_VOLUME" >/dev/null 2>&1; then
    break  # already gone
  fi
  if docker volume rm "$PG_VOLUME" >/dev/null 2>&1; then
    break  # removed
  fi
  sleep 0.2
done
if docker volume inspect "$PG_VOLUME" >/dev/null 2>&1; then
  echo "[pg-up] ERROR: could not remove previous volume '$PG_VOLUME' after 6s." >&2
  echo "[pg-up]        This would cause schema collisions. Aborting." >&2
  exit 1
fi

echo "[pg-up] starting fresh PG: cpus=$PG_CPUS (cpuset=$PG_CPUSET) mem=${PG_MEM_GB}g port=$PG_PORT shared_buffers=${SHARED_BUFFERS_MB}MB"
docker run -d \
  --name "$PG_CONTAINER" \
  --cpus="$PG_CPUS" \
  --cpuset-cpus="$PG_CPUSET" \
  --memory="${PG_MEM_GB}g" \
  --memory-swap="${PG_MEM_GB}g" \
  -p "${PG_PORT}:5432" \
  -e POSTGRES_PASSWORD=postgres \
  -e POSTGRES_DB=queen \
  -v "${PG_VOLUME}:/var/lib/postgresql/data" \
  postgres:16 \
  postgres \
    -c "shared_buffers=${SHARED_BUFFERS_MB}MB" \
    -c "effective_cache_size=${EFFECTIVE_CACHE_MB}MB" \
    -c "work_mem=${WORK_MEM_MB}MB" \
    -c "maintenance_work_mem=${MAINTENANCE_WORK_MB}MB" \
    -c "max_connections=${PG_MAX_CONN}" \
    -c "synchronous_commit=on" \
    -c "fsync=on" \
    -c "full_page_writes=on" \
    -c "wal_level=replica" \
    -c "max_wal_size=${MAX_WAL_MB}MB" \
    -c "checkpoint_completion_target=0.9" \
    -c "log_min_duration_statement=50" \
  >/dev/null

echo "[pg-up] waiting for PG to accept real connections (SELECT 1)..."
# `pg_isready` can return true while PG is still rejecting connections mid-startup.
# Use an actual SELECT 1 from inside the container — only succeeds when PG is fully up.
for i in $(seq 1 90); do
  if docker exec "$PG_CONTAINER" psql -U postgres -d queen -tAc "SELECT 1" >/dev/null 2>&1; then
    # Give PG one extra second after first successful SELECT. Some versions
    # still churn for a moment after the first accept.
    sleep 1
    # Re-probe to make sure it's really stable.
    if docker exec "$PG_CONTAINER" psql -U postgres -d queen -tAc "SELECT 1" >/dev/null 2>&1; then
      echo "[pg-up] ready after ${i}s (stable)"
      exit 0
    fi
  fi
  sleep 1
done
echo "[pg-up] ERROR: PG did not become ready in 90s" >&2
docker logs --tail 50 "$PG_CONTAINER" >&2 || true
exit 1
