#!/usr/bin/env bash
set -euo pipefail
PID_FILE="${PID_FILE:-./server.pid}"
if [[ ! -f "$PID_FILE" ]]; then
  echo "[server-down] no pid file at $PID_FILE; nothing to do"
  exit 0
fi
PID="$(cat "$PID_FILE")"
if kill -0 "$PID" 2>/dev/null; then
  echo "[server-down] sending SIGTERM to $PID..."
  kill -TERM "$PID" 2>/dev/null || true
  for i in $(seq 1 20); do
    if ! kill -0 "$PID" 2>/dev/null; then
      echo "[server-down] exited cleanly after ${i}s"
      rm -f "$PID_FILE"
      exit 0
    fi
    sleep 1
  done
  echo "[server-down] SIGTERM timed out, sending SIGKILL"
  kill -9 "$PID" 2>/dev/null || true
fi
rm -f "$PID_FILE"
