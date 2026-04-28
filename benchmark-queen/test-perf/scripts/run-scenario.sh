#!/usr/bin/env bash
# Run one scenario RUNS_PER_SCENARIO times. Each run gets its own subfolder.
#
# Usage: ./run-scenario.sh <scenario-id> <campaign-dir>
set -euo pipefail

SCENARIO_ID="${1:?usage: run-scenario.sh <scenario-id> <campaign-dir>}"
CAMPAIGN_DIR="${2:?usage: run-scenario.sh <scenario-id> <campaign-dir>}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCENARIOS_FILE="$HERE/../scenarios.json"

# Merge defaults into the target scenario so run-once always sees a complete object.
SCENARIO_JSON="$(node -e "
const fs = require('fs');
const f = JSON.parse(fs.readFileSync('$SCENARIOS_FILE','utf8'));
const d = f.defaults || {};
const s = (f.scenarios || []).find(x => x.id === $SCENARIO_ID);
if (!s) { console.error('scenario $SCENARIO_ID not found'); process.exit(2); }
const merged = Object.assign({}, s, {
  push_batch: s.push_batch ?? d.push_batch,
  pop_batch:  s.pop_batch  ?? d.pop_batch,
  max_partitions: s.max_partitions ?? d.max_partitions,
});
process.stdout.write(JSON.stringify(merged));
")"

SC_NAME="$(SCENARIO_JSON="$SCENARIO_JSON" node -e "process.stdout.write(JSON.parse(process.env.SCENARIO_JSON).name)")"

# Compute per-scenario max_partitions. Rule: max(defaults.max_partitions, 2 x producer.connections).
# Reasons:
#   * producer needs parts >= 2 x conns to avoid per-partition write contention;
#   * but parts should NOT be much bigger than what the workload actually covers in duration,
#     because autocannon iterates requests[] sequentially per-connection from index 0, so an
#     oversized array just means consumer polls partitions producer never touched (= empty pops
#     for reasons unrelated to queen's behaviour).
# A scenario may override by setting its own `max_partitions` field.
SCENARIO_MAX_PARTITIONS="$(SCENARIO_JSON="$SCENARIO_JSON" node -e "
const j = JSON.parse(process.env.SCENARIO_JSON);
if (typeof j.max_partitions === 'number' && j.max_partitions > 0 && j.max_partitions !== 500) {
  // scenario explicitly overrode it (we consider 500 the default floor, not an override)
  process.stdout.write(String(j.max_partitions));
} else {
  const floor = typeof j.max_partitions === 'number' ? j.max_partitions : 500;
  const prodConns = j.producer?.connections ?? 100;
  process.stdout.write(String(Math.max(floor, 2 * prodConns)));
}
")"

RUNS_PER_SCENARIO="${RUNS_PER_SCENARIO:-3}"
WARMUP_SECONDS="${WARMUP_SECONDS:-10}"
MEASURE_SECONDS="${MEASURE_SECONDS:-60}"
MAX_PARTITIONS="${MAX_PARTITIONS:-$SCENARIO_MAX_PARTITIONS}"

for r in $(seq 1 "$RUNS_PER_SCENARIO"); do
  OUT_DIR="$CAMPAIGN_DIR/scenario-${SCENARIO_ID}_${SC_NAME}/run-${r}"
  mkdir -p "$OUT_DIR"
  SCENARIO_JSON="$SCENARIO_JSON" \
  RUN_INDEX="$r" \
  OUT_DIR="$OUT_DIR" \
  WARMUP_SECONDS="$WARMUP_SECONDS" \
  MEASURE_SECONDS="$MEASURE_SECONDS" \
  MAX_PARTITIONS="$MAX_PARTITIONS" \
    bash "$HERE/run-once.sh" 2>&1 | tee "$OUT_DIR/run.log"
  echo
  echo "--- pausing 5s before next run ---"
  sleep 5
done
