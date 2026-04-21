#!/usr/bin/env bash
set -euo pipefail
PG_CONTAINER="${PG_CONTAINER:-queen-pg-bench}"
PG_VOLUME="${PG_VOLUME:-queen-pg-bench-data}"
docker rm -f "$PG_CONTAINER" >/dev/null 2>&1 || true
docker volume rm "$PG_VOLUME" >/dev/null 2>&1 || true
echo "[pg-down] container and volume removed"
