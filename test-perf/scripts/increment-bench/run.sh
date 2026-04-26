#!/usr/bin/env bash
# ============================================================================
# run.sh -- end-to-end driver for the increment-bench harness.
# ============================================================================
#
# Steps:
#   0. Bring up an isolated Postgres in Docker (00_pg_up.sh)
#   1. Install schema + procedures (01_install_schema.sh)
#   2. Seed prod-shape state (02_seed.sql)
#   3. Take a 'before' correctness snapshot (06_snapshot.sql)
#   4. Run :calls iterations of:
#        - inject N messages (03_inject_new_messages.sql)
#        - call + measure (04_run_calls.sql)
#      writing one line per iteration to per_call.csv
#   5. Take an 'after' correctness snapshot (06_snapshot.sql)
#   6. Capture EXPLAIN plans of the two inner statements (05_explain.sql)
#   7. Emit a JSON summary
#
# All artefacts go to:
#   test-perf/scripts/increment-bench/out/<label>/
#
# Env (all optional):
#   LABEL              (default: v1-baseline)
#   CALLS              (default: 30)         -- how many iterations to time
#   PARTITION_COUNT    (default: 200000)
#   MESSAGE_COUNT      (default: 10000000)
#   STATS_AGE_MIN      (default: 5)
#   INJECT_MESSAGES    (default: 10)
#   INJECT_PARTITIONS  (default: 5)
#   PG_PORT            (default: 5440)
#   PG_CONTAINER       (default: queen-pg-incbench)
#   SKIP_PG_UP         (default: 0) -- set to 1 to reuse existing PG container
#   SKIP_SEED          (default: 0) -- set to 1 to skip schema+seed (assumes
#                                       container already has the right state)
# ============================================================================

set -euo pipefail

LABEL="${LABEL:-v1-baseline}"
CALLS="${CALLS:-30}"
PARTITION_COUNT="${PARTITION_COUNT:-200000}"
MESSAGE_COUNT="${MESSAGE_COUNT:-10000000}"
STATS_AGE_MIN="${STATS_AGE_MIN:-5}"
INJECT_MESSAGES="${INJECT_MESSAGES:-10}"
INJECT_PARTITIONS="${INJECT_PARTITIONS:-5}"
PG_PORT="${PG_PORT:-5440}"
PG_CONTAINER="${PG_CONTAINER:-queen-pg-incbench}"
SKIP_PG_UP="${SKIP_PG_UP:-0}"
SKIP_SEED="${SKIP_SEED:-0}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out/$LABEL"

# ----------------------------------------------------------------------------
# psql shim. We exec into the container so the harness works on macOS without
# requiring a host-side psql install. Because the SQL files live on the HOST,
# we pipe every script via stdin (`-f -`) instead of `-f <path>` (which would
# look inside the container).
# ----------------------------------------------------------------------------
PSQL=( docker exec -i "$PG_CONTAINER" psql -U postgres -d queen -v ON_ERROR_STOP=1 )

# psql_file [psql-args...] <local-sql-path>
# Pipes a HOST-side SQL file through `psql -f -` running INSIDE the bench
# Postgres container. We use stdin instead of `-f <path>` because the file
# lives on the host, not inside the container.
psql_file() {
    local sqlfile="${!#}"          # last arg is the file
    local n=$(($# - 1))
    if [ "$n" -gt 0 ]; then
        "${PSQL[@]}" "${@:1:$n}" -f - < "$sqlfile"
    else
        "${PSQL[@]}" -f - < "$sqlfile"
    fi
}

ts() { date -u +%FT%T.%3NZ; }
log() { echo "[$(ts)] [bench:$LABEL] $*"; }

mkdir -p "$OUT"

# ----------------------------------------------------------------------------
# Step 0 -- bring up Postgres
# ----------------------------------------------------------------------------
if [ "$SKIP_PG_UP" != "1" ]; then
  log "step 0: bringing up Postgres ($PG_CONTAINER on port $PG_PORT)"
  PG_PORT="$PG_PORT" PG_CONTAINER="$PG_CONTAINER" \
    bash "$HERE/00_pg_up.sh" 2>&1 | sed 's/^/  | /'
else
  log "step 0: SKIP_PG_UP=1 -- reusing existing $PG_CONTAINER"
fi

# ----------------------------------------------------------------------------
# Step 1+2 -- install schema and seed
# ----------------------------------------------------------------------------
if [ "$SKIP_SEED" != "1" ]; then
  log "step 1: installing schema..."
  PG_CONTAINER="$PG_CONTAINER" \
    bash "$HERE/01_install_schema.sh" 2>&1 | sed 's/^/  | /'

  log "step 2: seeding prod-shape state (partitions=$PARTITION_COUNT messages=$MESSAGE_COUNT)..."
  psql_file \
    -v partition_count="$PARTITION_COUNT" \
    -v message_count="$MESSAGE_COUNT" \
    -v stats_age_min="$STATS_AGE_MIN" \
    "$HERE/02_seed.sql" 2>&1 | tee "$OUT/seed.log" | sed 's/^/  | /'
else
  log "step 1+2: SKIP_SEED=1 -- assuming DB already seeded"
fi

# ----------------------------------------------------------------------------
# Reset cumulative counters so per-call deltas are clean
# ----------------------------------------------------------------------------
log "resetting pg_stat_statements + pg_stat_user_tables counters"
"${PSQL[@]}" -tAc "SELECT pg_stat_statements_reset();" >/dev/null
"${PSQL[@]}" -tAc "SELECT pg_stat_reset();"           >/dev/null

# ----------------------------------------------------------------------------
# Step 3 -- correctness 'before' snapshot
# ----------------------------------------------------------------------------
log "step 3: capturing 'before' snapshot"
psql_file -v label=before "$HERE/06_snapshot.sql" \
    > "$OUT/snapshot_before.txt" 2> "$OUT/snapshot_before.err" || true

# ----------------------------------------------------------------------------
# Step 4 -- the measurement loop
# ----------------------------------------------------------------------------
log "step 4: running $CALLS measured iterations (inject=$INJECT_MESSAGES into $INJECT_PARTITIONS partitions per call)"

# Write CSV header
cat > "$OUT/per_call.csv" <<'EOF'
iter,duration_ms,wal_bytes,stats_n_tup_upd_delta,stats_n_tup_hot_upd_delta,stats_dead_tup_delta,messages_seq_scan_delta,messages_idx_scan_delta,fn_pgsuf_total_ms_delta,fn_pgsuf_calls_delta,returned_partitions_updated,returned_skipped
EOF

PSQL_CSV=( docker exec -i "$PG_CONTAINER" psql -U postgres -d queen \
            -v ON_ERROR_STOP=1 -A -t -F',' --quiet )

for i in $(seq 1 "$CALLS"); do
  # 4a. inject fresh messages (so the function has real work to do)
  psql_file -q \
      -v iter="$i" \
      -v inject_messages="$INJECT_MESSAGES" \
      -v inject_partitions="$INJECT_PARTITIONS" \
      "$HERE/03_inject_new_messages.sql" >/dev/null 2>>"$OUT/run_calls.err"

  # 4b. measure call. We use a dedicated CSV-mode psql shim so we get a
  # single tuples-only line on stdout. grep -v '^$' as a defence in depth
  # in case any future statement leaves an empty void-result row.
  "${PSQL_CSV[@]}" -v iter="$i" -f - < "$HERE/04_run_calls.sql" 2>>"$OUT/run_calls.err" \
      | grep -v '^$' >> "$OUT/per_call.csv"

  if [ $((i % 5)) -eq 0 ] || [ "$i" = "$CALLS" ]; then
    last=$(tail -n 1 "$OUT/per_call.csv")
    log "  iter $i/$CALLS  -> $last"
  fi
done

# ----------------------------------------------------------------------------
# Step 5 -- correctness 'after' snapshot
# ----------------------------------------------------------------------------
log "step 5: capturing 'after' snapshot"
psql_file -v label=after "$HERE/06_snapshot.sql" \
    > "$OUT/snapshot_after.txt" 2> "$OUT/snapshot_after.err" || true

# ----------------------------------------------------------------------------
# Step 6 -- EXPLAIN of the two inner statements (one extra inject so the first
# UPDATE has rows to operate on).
# ----------------------------------------------------------------------------
log "step 6: capturing EXPLAIN plans of inner statements"
psql_file -q \
    -v iter=$((CALLS + 1)) \
    -v inject_messages="$INJECT_MESSAGES" \
    -v inject_partitions="$INJECT_PARTITIONS" \
    "$HERE/03_inject_new_messages.sql" >/dev/null

psql_file "$HERE/05_explain.sql" > "$OUT/explain.txt" 2>&1 || true

# ----------------------------------------------------------------------------
# Step 7 -- JSON summary (computed from per_call.csv via a small embedded
# python). We deliberately avoid jq / python deps here -- just stdlib.
# ----------------------------------------------------------------------------
log "step 7: emitting JSON summary"
python3 - "$OUT/per_call.csv" "$OUT/snapshot_before.txt" "$OUT/snapshot_after.txt" "$LABEL" "$CALLS" "$PARTITION_COUNT" "$MESSAGE_COUNT" <<'PY' > "$OUT/summary.json"
import csv, json, statistics, sys
from pathlib import Path

per_call_path, before_path, after_path, label, calls, pcount, mcount = sys.argv[1:8]

def percentile(xs, p):
    if not xs: return None
    xs = sorted(xs)
    if len(xs) == 1: return xs[0]
    k = (len(xs) - 1) * p
    f = int(k); c = min(f + 1, len(xs) - 1)
    return xs[f] + (xs[c] - xs[f]) * (k - f)

def stats(xs):
    if not xs: return {"n": 0}
    xs = [float(x) for x in xs]
    return {
        "n": len(xs),
        "min": min(xs),
        "p50": percentile(xs, 0.50),
        "p90": percentile(xs, 0.90),
        "p99": percentile(xs, 0.99),
        "max": max(xs),
        "mean": statistics.mean(xs),
        "sum": sum(xs),
    }

rows = []
with open(per_call_path) as f:
    rdr = csv.DictReader(f)
    for r in rdr:
        rows.append(r)

def col(name): return [r[name] for r in rows if r.get(name) not in (None, "")]

# Pull md5 from the summary line of the snapshot file. The summary section
# looks like:
#   --- BEGIN summary label=after ---
#   <label>,<rows_n>,<sum_total>,<sum_pending>,<md5>
#   --- END summary ---
# We split on the END marker so the BEGIN line itself (which has the label
# inline) is excluded, then take the last comma-separated field of the first
# real row.
def parse_md5(path):
    text = Path(path).read_text()
    section = text.split("--- BEGIN summary")[-1].split("--- END summary")[0]
    for line in section.splitlines():
        s = line.strip()
        # Skip the residue of the BEGIN marker (" label=after ---") and blanks
        if not s or s.startswith("---") or s.startswith("label="):
            continue
        return s.split(",")[-1]
    return None

summary = {
    "label": label,
    "calls": int(calls),
    "seed": {
        "partition_count": int(pcount),
        "message_count": int(mcount),
    },
    "timing_ms":             stats(col("duration_ms")),
    "fn_pgsuf_total_ms":     stats(col("fn_pgsuf_total_ms_delta")),
    "wal_bytes":             stats(col("wal_bytes")),
    "stats_n_tup_upd":       stats(col("stats_n_tup_upd_delta")),
    "stats_n_tup_hot_upd":   stats(col("stats_n_tup_hot_upd_delta")),
    "stats_dead_tup":        stats(col("stats_dead_tup_delta")),
    "messages_seq_scan":     stats(col("messages_seq_scan_delta")),
    "messages_idx_scan":     stats(col("messages_idx_scan_delta")),
    "returned_partitions":   stats(col("returned_partitions_updated")),
    "correctness": {
        "before_md5": parse_md5(before_path),
        "after_md5":  parse_md5(after_path),
    },
}
print(json.dumps(summary, indent=2, default=str))
PY

log "DONE -- artefacts at $OUT"
log "  - $OUT/summary.json     (top-level metrics)"
log "  - $OUT/per_call.csv     (raw per-iteration data)"
log "  - $OUT/snapshot_before.txt / snapshot_after.txt  (correctness contract)"
log "  - $OUT/explain.txt      (planner output)"
log "  - $OUT/seed.log         (seed phase log)"
echo
echo "Top-level results:"
echo "----------------------------------------"
cat "$OUT/summary.json"
