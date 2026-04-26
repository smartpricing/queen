#!/usr/bin/env bash
# Apply lib/schema/schema.sql + lib/schema/procedures/*.sql against the
# bench Postgres. Idempotent.
#
# Env:
#   PG_CONTAINER  (default queen-pg-incbench)
#   REPO_ROOT     (default: derived from this script's path)

set -euo pipefail

PG_CONTAINER="${PG_CONTAINER:-queen-pg-incbench}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "$HERE/../../.." && pwd)}"
SCHEMA_DIR="$REPO_ROOT/lib/schema"

if [ ! -f "$SCHEMA_DIR/schema.sql" ]; then
  echo "[schema] ERROR: $SCHEMA_DIR/schema.sql not found" >&2
  exit 1
fi

echo "[schema] applying schema.sql (REPO_ROOT=$REPO_ROOT)..."
docker exec -i "$PG_CONTAINER" psql -U postgres -d queen -v ON_ERROR_STOP=1 -q < "$SCHEMA_DIR/schema.sql"

echo "[schema] applying procedures/ (in lexicographic order)..."
for f in "$SCHEMA_DIR"/procedures/*.sql; do
  bn="$(basename "$f")"
  echo "[schema]   - $bn"
  docker exec -i "$PG_CONTAINER" psql -U postgres -d queen -v ON_ERROR_STOP=1 -q < "$f"
done

echo "[schema] verifying queen.increment_message_counts_v1 exists..."
docker exec "$PG_CONTAINER" psql -U postgres -d queen -tAc \
  "SELECT proname FROM pg_proc WHERE proname='increment_message_counts_v1'" \
  | grep -q increment_message_counts_v1 \
  || { echo "[schema] ERROR: function not installed" >&2; exit 1; }

echo "[schema] done"
