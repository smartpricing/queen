#!/usr/bin/env bash
# Background sampler. Polls docker stats for the PG container AND ps for the
# server PID every ~2s. Writes one JSON object per sample to stats-stream.jsonl.
# Runs until it receives SIGTERM/SIGINT.
#
# Usage (typically invoked by run-once.sh as a background job):
#   OUT_DIR=... SERVER_PID=... PG_CONTAINER=... ./stats-stream.sh &
#   STREAM_PID=$!
#   ...
#   kill $STREAM_PID
set -euo pipefail

: "${OUT_DIR:?OUT_DIR required}"
: "${SERVER_PID:?SERVER_PID required}"
PG_CONTAINER="${PG_CONTAINER:-queen-pg-bench}"
INTERVAL="${STATS_INTERVAL:-2}"
OUT_FILE="$OUT_DIR/stats-stream.jsonl"

: > "$OUT_FILE"

cleanup() {
  echo "[stats-stream] stopping"
  exit 0
}
trap cleanup TERM INT

while :; do
  ts=$(date +%s)

  # docker stats for PG (no-stream returns one snapshot)
  dstats="$(docker stats --no-stream --format '{{json .}}' "$PG_CONTAINER" 2>/dev/null || echo '{}')"

  # Server process: CPU%, RSS in KB (mac/bsd ps). Tolerates the process being gone.
  if ps -p "$SERVER_PID" >/dev/null 2>&1; then
    # %cpu and rss. `rss` is in KB. Format: pid pcpu rss
    srv_raw="$(ps -o pid=,pcpu=,rss= -p "$SERVER_PID" 2>/dev/null | awk '{print $1" "$2" "$3}')"
    srv_pid="$(echo "$srv_raw" | awk '{print $1}')"
    srv_cpu="$(echo "$srv_raw" | awk '{print $2}')"
    srv_rss_kb="$(echo "$srv_raw" | awk '{print $3}')"
  else
    srv_pid="null"; srv_cpu="null"; srv_rss_kb="null"
  fi

  # Assemble one JSON line. Use printf to avoid any shell-quote drama with the
  # docker-stats JSON we got already.
  printf '{"ts":%s,"pg":%s,"server":{"pid":%s,"cpu_pct":%s,"rss_kb":%s}}\n' \
    "$ts" "$dstats" "$srv_pid" "$srv_cpu" "$srv_rss_kb" >> "$OUT_FILE"

  sleep "$INTERVAL"
done
