#!/usr/bin/env bash
# Monitor harness for a timed workload run against queen + docker PG (port 5433).
#
# Usage: monitor.sh <label> <duration_sec>
#   monitor.sh push-only   120
#   monitor.sh pop-only    120
#   monitor.sh combined    120
#
# Writes to out/<label>/ the following JSONL streams (2s period):
#   wait_events.jsonl   - pg_stat_activity histogram {ts, wait_event_type, wait_event, n}
#   tables.jsonl        - pg_stat_user_tables cumulative {ts, relname, n_tup_ins, n_tup_upd, n_tup_hot_upd, n_live_tup, n_dead_tup}
#   wal.jsonl           - pg_stat_wal cumulative {ts, wal_records, wal_fpi, wal_bytes, wal_write, wal_sync, wal_write_time, wal_sync_time}
#   docker.jsonl        - docker stats {ts, cpu_pct, mem_mb, mem_pct, net_io_mb}
#   activity_top.jsonl  - top 5 active queries (for ad-hoc debugging)
#
# Plus summary.txt: deltas + averages printed at end.

set -euo pipefail

LABEL="${1:?label required}"
DURATION="${2:?duration required}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/out/$LABEL"
rm -rf "$OUT"
mkdir -p "$OUT"

PG="docker exec queen-pg-5433 psql -U postgres -d queen -tAF, -v ON_ERROR_STOP=1"
NOW() { date +%s; }

# Snapshot cumulative counters at start (for delta).
$PG -c "SELECT relname, n_tup_ins, n_tup_upd, n_tup_hot_upd, n_tup_del, n_live_tup, n_dead_tup, seq_scan, idx_scan
        FROM pg_stat_user_tables
        WHERE schemaname='queen'
          AND relname IN ('messages','partition_lookup','partition_consumers','messages_consumed','consumer_watermarks','queues','partitions')
        ORDER BY relname" > "$OUT/tables_t0.csv"

$PG -c "SELECT wal_records, wal_fpi, wal_bytes, wal_buffers_full, wal_write, wal_sync, wal_write_time, wal_sync_time
        FROM pg_stat_wal" > "$OUT/wal_t0.csv"

$PG -c "SELECT pg_stat_reset_shared('io'); SELECT pg_stat_reset_shared('bgwriter');" >/dev/null
# We leave pg_stat_wal and pg_stat_user_tables alone (we subtract t0 instead).

END=$(( $(NOW) + DURATION ))
echo "Monitoring '$LABEL' for ${DURATION}s → $OUT"

# ---- background samplers (2s period) ----

# Wait events by type/event (non-backend_pid of this script's connection is ignored).
(
  while [ $(NOW) -lt $END ]; do
    TS=$(NOW)
    $PG -c "
      SELECT '$TS,'||COALESCE(wait_event_type,'NULL')||','||COALESCE(wait_event,'NULL')||','||cnt
      FROM (
        SELECT wait_event_type, wait_event, count(*)::int AS cnt
        FROM pg_stat_activity
        WHERE state='active'
          AND pid <> pg_backend_pid()
          AND datname='queen'
        GROUP BY 1,2
      ) x" 2>/dev/null >> "$OUT/wait_events.jsonl"
    sleep 2
  done
) &
WAIT_PID=$!

# pg_stat_user_tables snapshot (delta computed at end from stream).
(
  while [ $(NOW) -lt $END ]; do
    TS=$(NOW)
    $PG -c "
      SELECT '$TS,'||relname||','||n_tup_ins||','||n_tup_upd||','||n_tup_hot_upd||','||n_live_tup||','||n_dead_tup
      FROM pg_stat_user_tables
      WHERE schemaname='queen'
        AND relname IN ('messages','partition_lookup','partition_consumers','messages_consumed','consumer_watermarks')
      " 2>/dev/null >> "$OUT/tables.jsonl"
    sleep 2
  done
) &
TAB_PID=$!

# WAL + IO summary every 2s.
(
  while [ $(NOW) -lt $END ]; do
    TS=$(NOW)
    $PG -c "
      SELECT '$TS,'||wal_records||','||wal_fpi||','||wal_bytes||','||wal_buffers_full||','||wal_write||','||wal_sync||','||wal_write_time::bigint||','||wal_sync_time::bigint
      FROM pg_stat_wal" 2>/dev/null >> "$OUT/wal.jsonl"
    $PG -c "
      SELECT '$TS,'||backend_type||','||object||','||context||','||reads||','||writes||','||extends||','||read_time::bigint||','||write_time::bigint
      FROM pg_stat_io
      WHERE backend_type IN ('client backend','background writer','checkpointer','walwriter','autovacuum worker')
      " 2>/dev/null >> "$OUT/io.jsonl"
    sleep 2
  done
) &
WAL_PID=$!

# Top active queries every 4s (snapshot what's actually running).
(
  while [ $(NOW) -lt $END ]; do
    TS=$(NOW)
    $PG -c "
      SELECT '$TS,'||pid||','||state||','||COALESCE(wait_event_type,'NULL')||','||COALESCE(wait_event,'NULL')||','||COALESCE(substr(regexp_replace(query,'\s+',' ','g'),1,120),'')
      FROM pg_stat_activity
      WHERE state='active'
        AND pid <> pg_backend_pid()
        AND datname='queen'
      ORDER BY query_start
      LIMIT 10" 2>/dev/null >> "$OUT/activity_top.jsonl"
    sleep 4
  done
) &
ACT_PID=$!

# Docker stats (CPU / mem) every 2s.
(
  while [ $(NOW) -lt $END ]; do
    TS=$(NOW)
    docker stats --no-stream --format "{{.Container}},{{.CPUPerc}},{{.MemUsage}},{{.MemPerc}},{{.NetIO}}" queen-pg-5433 2>/dev/null \
      | while IFS= read -r line; do echo "$TS,$line"; done \
      >> "$OUT/docker.jsonl"
    sleep 2
  done
) &
DOC_PID=$!

# Lock attribution sampler via debug_lock_attribution — every 250ms.
# Writes one jsonb per line to lock_attr.jsonl with ts prefix.
(
  while [ $(NOW) -lt $END ]; do
    TS=$(NOW)
    docker exec queen-pg-5433 psql -U postgres -d queen -tAc "SELECT '$TS,' || queen.debug_lock_attribution('queen-long-running')::text" 2>/dev/null \
      >> "$OUT/lock_attr.jsonl"
    sleep 0.25
  done
) &
ATTR_PID=$!

# ---- wait for duration ----
wait $WAIT_PID $TAB_PID $WAL_PID $ACT_PID $DOC_PID $ATTR_PID 2>/dev/null || true

# ---- snapshots at end ----
$PG -c "SELECT relname, n_tup_ins, n_tup_upd, n_tup_hot_upd, n_tup_del, n_live_tup, n_dead_tup, seq_scan, idx_scan
        FROM pg_stat_user_tables
        WHERE schemaname='queen'
          AND relname IN ('messages','partition_lookup','partition_consumers','messages_consumed','consumer_watermarks','queues','partitions')
        ORDER BY relname" > "$OUT/tables_t1.csv"

$PG -c "SELECT wal_records, wal_fpi, wal_bytes, wal_buffers_full, wal_write, wal_sync, wal_write_time, wal_sync_time
        FROM pg_stat_wal" > "$OUT/wal_t1.csv"

# ---- aggregate summary ----
python3 - "$OUT" "$DURATION" <<'PY' > "$OUT/summary.txt"
import csv, os, sys
from collections import defaultdict, Counter

out, dur = sys.argv[1], int(sys.argv[2])

def parse_csv(p):
    with open(p) as f:
        return [row.strip().split(',') for row in f if row.strip()]

# --- Table deltas ---
def tbl(p):
    d = {}
    for row in parse_csv(p):
        d[row[0]] = [int(x) if x.strip() else 0 for x in row[1:]]
    return d

try:
    t0 = tbl(f"{out}/tables_t0.csv")
    t1 = tbl(f"{out}/tables_t1.csv")
except Exception as e:
    t0 = t1 = {}
    print(f"table csv parse failure: {e}")

print("=" * 80)
print(f"  Summary for {out} (duration={dur}s)")
print("=" * 80)
print()
print("  TABLE DELTAS  (ins / upd / hot_upd / del  |  HOT% = hot_upd / upd)")
print(f"  {'relname':<22} {'ins':>10} {'upd':>10} {'hot_upd':>10} {'del':>10}   {'HOT%':>6}")
print("  " + "-"*76)
for k in sorted(t0):
    if k not in t1: continue
    d = [t1[k][i] - t0[k][i] for i in range(len(t0[k]))]
    # cols: n_tup_ins, n_tup_upd, n_tup_hot_upd, n_tup_del, n_live_tup, n_dead_tup, seq_scan, idx_scan
    ins, upd, hot, dele = d[0], d[1], d[2], d[3]
    hot_pct = (100.0 * hot / upd) if upd else 0.0
    print(f"  {k:<22} {ins:>10} {upd:>10} {hot:>10} {dele:>10}   {hot_pct:>5.1f}%")

# --- WAL delta ---
def wal(p):
    for row in parse_csv(p):
        return [int(x) if x.strip() else 0 for x in row]
    return []

try:
    w0 = wal(f"{out}/wal_t0.csv")
    w1 = wal(f"{out}/wal_t1.csv")
    wd = [w1[i] - w0[i] for i in range(len(w0))]
    # wal_records, wal_fpi, wal_bytes, wal_buffers_full, wal_write, wal_sync, wal_write_time, wal_sync_time
    print()
    print("  WAL")
    print(f"  wal_records:       {wd[0]:>12} ({wd[0]/dur:>10.0f}/s)")
    print(f"  wal_fpi:           {wd[1]:>12} ({wd[1]/dur:>10.0f}/s)     <-- full-page images")
    print(f"  wal_bytes:         {wd[2]:>12} ({wd[2]/dur/1024/1024:>10.2f} MB/s)")
    print(f"  wal_buffers_full:  {wd[3]:>12}")
    print(f"  wal_write ops:     {wd[4]:>12}")
    print(f"  wal_sync ops:      {wd[5]:>12} ({wd[5]/dur:>10.0f}/s)")
    print(f"  wal_write_time ms: {wd[6]:>12}")
    print(f"  wal_sync_time ms:  {wd[7]:>12}")
except Exception as e:
    print(f"wal parse failure: {e}")

# --- Wait event histogram ---
we = Counter()
samples = 0
try:
    prev_ts = None
    with open(f"{out}/wait_events.jsonl") as f:
        for line in f:
            parts = line.strip().split(',')
            if len(parts) < 4: continue
            ts, et, ev, cnt = parts[0], parts[1], parts[2], int(parts[3])
            if ts != prev_ts:
                samples += 1
                prev_ts = ts
            we[(et, ev)] += cnt
except FileNotFoundError:
    pass

print()
print(f"  WAIT EVENTS  ({samples} samples over {dur}s @ 2s period)")
print(f"  {'wait_event_type':<22} {'wait_event':<30} {'total':>10} {'avg/sample':>12}")
print("  " + "-"*76)
for (et, ev), tot in we.most_common(20):
    avg = tot / samples if samples else 0
    print(f"  {et:<22} {ev:<30} {tot:>10} {avg:>12.2f}")

# --- Docker stats ---
cpu = []
mem = []
try:
    with open(f"{out}/docker.jsonl") as f:
        for line in f:
            parts = line.strip().split(',')
            if len(parts) < 4: continue
            # TS, Container, CPU%, MemUsage, MemPerc, NetIO
            try:
                cpu.append(float(parts[2].replace('%','').strip()))
                memp = parts[4].replace('%','').strip()
                mem.append(float(memp))
            except: pass
except FileNotFoundError: pass

if cpu:
    cpu.sort()
    print()
    print(f"  DOCKER stats ({len(cpu)} samples)")
    print(f"  CPU%  avg={sum(cpu)/len(cpu):6.1f}  p50={cpu[len(cpu)//2]:6.1f}  p95={cpu[int(len(cpu)*0.95)]:6.1f}  max={cpu[-1]:6.1f}")
    print(f"  Mem%  avg={sum(mem)/len(mem):6.1f}  max={max(mem):6.1f}")

# --- Lock attribution ---
import json
keys = ["eligible","unlocked","locked_by_push","locked_by_pop","locked_by_other",
        "push_waits_on_push","push_waits_on_pop","push_waits_on_other",
        "pop_waits_on_push","pop_waits_on_pop","active_pushes","active_pops"]
sums = {k: 0 for k in keys}
maxs = {k: 0 for k in keys}
n = 0
try:
    with open(f"{out}/lock_attr.jsonl") as f:
        for line in f:
            line = line.strip()
            if not line: continue
            ts_comma, _, payload = line.partition(",")
            try:
                d = json.loads(payload)
            except: continue
            n += 1
            for k in keys:
                v = d.get(k, 0) or 0
                sums[k] += v
                if v > maxs[k]: maxs[k] = v
except FileNotFoundError:
    pass

if n > 0:
    print()
    print(f"  LOCK ATTRIBUTION on queen.partition_lookup  ({n} samples, 250ms period)")
    print(f"  {'metric':<26} {'avg':>10} {'max':>8}")
    print("  " + "-"*50)
    for k in keys:
        avg = sums[k] / n
        print(f"  {k:<26} {avg:>10.2f} {maxs[k]:>8}")
    # Ratios
    elig = sums["eligible"] / n if n else 0
    if elig > 0:
        print()
        print(f"  POP SKIP ATTRIBUTION (fraction of eligible rows that pop would skip):")
        print(f"    skipped because push holds:  {100*sums['locked_by_push']/sums['eligible']:5.1f}%")
        print(f"    skipped because pop holds:   {100*sums['locked_by_pop']/sums['eligible']:5.1f}%")
        print(f"    skipped because other holds: {100*sums['locked_by_other']/sums['eligible']:5.1f}%")
        print(f"    freely claimable:            {100*sums['unlocked']/sums['eligible']:5.1f}%")
PY

echo "Summary written to $OUT/summary.txt"
cat "$OUT/summary.txt"
