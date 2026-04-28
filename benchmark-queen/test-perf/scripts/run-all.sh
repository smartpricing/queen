#!/usr/bin/env bash
# Run all (or a subset of) scenarios. Creates a timestamped campaign dir
# under test-perf/results/ and produces an aggregate report at the end.
#
# Usage:
#   ./run-all.sh                     # all scenarios
#   ./run-all.sh 1 2 3               # just these IDs
#   SCENARIOS="1 4 6" ./run-all.sh   # same, via env
#   RUNS_PER_SCENARIO=1 ./run-all.sh # shorter (for smoke-testing)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SCENARIOS_FILE="$HERE/../scenarios.json"
RESULTS_ROOT="$HERE/../results"

# Pick scenarios: CLI args > env SCENARIOS > all.
if [[ $# -gt 0 ]]; then
  IDS="$*"
elif [[ -n "${SCENARIOS:-}" ]]; then
  IDS="$SCENARIOS"
else
  IDS="$(node -e "
const s = JSON.parse(require('fs').readFileSync('$SCENARIOS_FILE','utf8'));
console.log(s.scenarios.map(x=>x.id).join(' '));
")"
fi

# Read defaults from scenarios.json; fall back to built-in defaults only if the
# file doesn't declare them. Env vars still take precedence for ad-hoc overrides.
SCEN_RUNS=$(node -e "
const f = JSON.parse(require('fs').readFileSync('$SCENARIOS_FILE','utf8'));
process.stdout.write(String(f.defaults?.runs_per_scenario ?? 3));
")
SCEN_WARMUP=$(node -e "
const f = JSON.parse(require('fs').readFileSync('$SCENARIOS_FILE','utf8'));
process.stdout.write(String(f.defaults?.warmup_seconds ?? 10));
")
SCEN_MEASURE=$(node -e "
const f = JSON.parse(require('fs').readFileSync('$SCENARIOS_FILE','utf8'));
process.stdout.write(String(f.defaults?.measure_seconds ?? 60));
")
RUNS_PER_SCENARIO="${RUNS_PER_SCENARIO:-$SCEN_RUNS}"
WARMUP_SECONDS="${WARMUP_SECONDS:-$SCEN_WARMUP}"
MEASURE_SECONDS="${MEASURE_SECONDS:-$SCEN_MEASURE}"

STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
CAMPAIGN_DIR="$RESULTS_ROOT/$STAMP"
mkdir -p "$CAMPAIGN_DIR"

# Optional: label campaign with a git sha + short message.
GIT_SHA="$(cd "$ROOT" && git rev-parse --short HEAD 2>/dev/null || echo unknown)"
GIT_BRANCH="$(cd "$ROOT" && git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
GIT_DIRTY="$(cd "$ROOT" && git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"

cat > "$CAMPAIGN_DIR/meta.json" <<EOF
{
  "stamp": "$STAMP",
  "scenarios": [$(echo "$IDS" | xargs -n1 | paste -sd, -)],
  "runs_per_scenario": $RUNS_PER_SCENARIO,
  "warmup_seconds": $WARMUP_SECONDS,
  "measure_seconds": $MEASURE_SECONDS,
  "git": { "sha": "$GIT_SHA", "branch": "$GIT_BRANCH", "dirty_files": $GIT_DIRTY },
  "host": {
    "kernel": "$(uname -sr | sed 's/"/\\"/g')",
    "arch":   "$(uname -m)",
    "cpus":   "$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo unknown)"
  },
  "start_iso": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF

echo "=========================================================="
echo "  Campaign: $CAMPAIGN_DIR"
echo "  Scenarios: $IDS"
echo "  Runs per scenario: $RUNS_PER_SCENARIO"
echo "  Warmup ${WARMUP_SECONDS}s + Measure ${MEASURE_SECONDS}s per run"
echo "  Git: $GIT_BRANCH @ $GIT_SHA (dirty files: $GIT_DIRTY)"
echo "=========================================================="

# Ensure workload deps installed
if [[ ! -d "$HERE/../workload/node_modules" ]]; then
  echo "[run-all] installing workload node_modules..."
  (cd "$HERE/../workload" && npm install --no-audit --no-fund --loglevel=error)
fi

# Ensure the server binary exists. Rebuild once at campaign start.
if [[ ! -x "$ROOT/server/bin/queen-server" ]]; then
  echo "[run-all] server binary missing, running 'make -C server build-only'..."
  make -C "$ROOT/server" build-only
else
  echo "[run-all] server binary found at $ROOT/server/bin/queen-server ($(stat -f %Sm "$ROOT/server/bin/queen-server" 2>/dev/null || stat -c %y "$ROOT/server/bin/queen-server"))"
fi

for id in $IDS; do
  RUNS_PER_SCENARIO="$RUNS_PER_SCENARIO" \
  WARMUP_SECONDS="$WARMUP_SECONDS" \
  MEASURE_SECONDS="$MEASURE_SECONDS" \
    bash "$HERE/run-scenario.sh" "$id" "$CAMPAIGN_DIR"
done

# Stamp finish
node -e "
const fs = require('fs');
const p = '$CAMPAIGN_DIR/meta.json';
const m = JSON.parse(fs.readFileSync(p, 'utf8'));
m.finish_iso = new Date().toISOString();
fs.writeFileSync(p, JSON.stringify(m, null, 2));
"

# Aggregate + report
echo
echo "[run-all] aggregating results..."
node "$HERE/analyze.js" "$CAMPAIGN_DIR"

echo
echo "[run-all] campaign complete: $CAMPAIGN_DIR"
echo "           report: $CAMPAIGN_DIR/report.md"
