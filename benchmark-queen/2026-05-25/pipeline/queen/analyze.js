// Post-run analysis for the pipeline benchmark.
//
// Reads all per-process JSONL files in $RESULTS_DIR/, joins them by txid,
// and produces:
//   - end-to-end latency histogram (p50/p90/p99/max), separately for
//     producer→analytics and producer→log
//   - per-stage latency: q1-to-worker, q2-to-consumer
//   - delivery completeness: was every produced txid processed by the
//     worker AND by both consumer groups?
//   - duplicate count: did any txid appear more than once at the same stage?
//   - per-process throughput (msg/s) sustained over the run
//   - warm-up exclusion: ignore the first WARMUP_SEC seconds of producer
//     timestamps when computing percentile statistics
//
// Output: a JSON summary on stdout and a human-readable report on stderr.

import fs from 'node:fs'
import path from 'node:path'
import readline from 'node:readline'

const RESULTS_DIR = process.env.RESULTS_DIR || '/tmp/queen-pipeline'
const WARMUP_SEC  = parseInt(process.env.WARMUP_SEC || '180', 10) // first 3 min discarded

function pickStats(arr) {
  if (arr.length === 0) return null
  const sorted = [...arr].sort((a, b) => a - b)
  const n = sorted.length
  return {
    count: n,
    min: sorted[0],
    p50: sorted[Math.floor(n * 0.5)],
    p90: sorted[Math.floor(n * 0.9)],
    p99: sorted[Math.floor(n * 0.99)],
    p999: sorted[Math.floor(n * 0.999)],
    max: sorted[n - 1],
    avg: sorted.reduce((a, b) => a + b, 0) / n,
  }
}

async function* iterJsonl(file) {
  if (!fs.existsSync(file)) return
  const rl = readline.createInterface({ input: fs.createReadStream(file), crlfDelay: Infinity })
  for await (const line of rl) {
    if (!line.trim()) continue
    try { yield JSON.parse(line) } catch { /* skip bad line */ }
  }
}

async function main() {
  const files = fs.readdirSync(RESULTS_DIR).filter((f) => f.endsWith('.jsonl'))
  process.stderr.write(`[analyze] found ${files.length} JSONL files in ${RESULTS_DIR}\n`)

  // Stream-append to per-role arrays one record at a time so we don't
  // blow the stack on multi-million-record spreads.
  const records = { producer: [], worker: [], analytics: [], log: [] }
  for (const file of files) {
    const role = file.split('-')[0]
    if (!records[role]) records[role] = []
    let n = 0
    for await (const rec of iterJsonl(path.join(RESULTS_DIR, file))) {
      records[role].push(rec)
      n++
    }
    process.stderr.write(`[analyze]   ${file} → ${n} records (role=${role})\n`)
  }

  // Producer push timestamps — define the run window from the producers.
  // NB: with millions of records `Math.min(...arr)` blows the stack;
  //     use a loop to scan once for both extrema.
  let nProducer = 0
  let t0 = Infinity
  let tEnd = -Infinity
  for (const r of records.producer) {
    if (typeof r.push_t !== 'number') continue
    nProducer++
    if (r.push_t < t0)   t0 = r.push_t
    if (r.push_t > tEnd) tEnd = r.push_t
  }
  if (nProducer === 0) {
    process.stderr.write('[analyze] FATAL: no producer records\n')
    process.exit(1)
  }
  const tWarmupEnd = t0 + WARMUP_SEC * 1000
  process.stderr.write(
    `[analyze] producer window: ${new Date(t0).toISOString()} .. ${new Date(tEnd).toISOString()}\n`,
  )
  process.stderr.write(`[analyze] warm-up cutoff: ${new Date(tWarmupEnd).toISOString()}\n`)

  // Index by txid (steady-state only).
  const byTxid = new Map()
  for (const r of records.producer) {
    if (r.push_t < tWarmupEnd) continue
    if (!byTxid.has(r.txid)) byTxid.set(r.txid, { producer: r })
  }
  let workerSteady = 0,
    analyticsSteady = 0,
    logSteady = 0
  for (const r of records.worker) {
    const e = byTxid.get(r.txid)
    if (e) {
      e.worker = r
      workerSteady++
    }
  }
  for (const r of records.analytics) {
    const e = byTxid.get(r.txid)
    if (e) {
      e.analytics = r
      analyticsSteady++
    }
  }
  for (const r of records.log) {
    const e = byTxid.get(r.txid)
    if (e) {
      e.log = r
      logSteady++
    }
  }

  // Throughput (steady state) per stage.
  const steadySec = (tEnd - tWarmupEnd) / 1000
  const steadyProducer = byTxid.size
  const stageThroughput = {
    producer_msgPerSec: steadyProducer / steadySec,
    worker_msgPerSec: workerSteady / steadySec,
    analytics_msgPerSec: analyticsSteady / steadySec,
    log_msgPerSec: logSteady / steadySec,
    steadyDurationSec: Math.round(steadySec),
  }

  // Latency arrays (steady state).
  const e2eAnalytics = []
  const e2eLog = []
  const q1ToWorker = []
  const q2ToAnalytics = []
  const q2ToLog = []
  let withWorker = 0
  let withAnalytics = 0
  let withLog = 0
  for (const e of byTxid.values()) {
    if (e.worker) {
      withWorker++
      q1ToWorker.push(e.worker.worker_t - e.producer.push_t)
    }
    if (e.analytics) {
      withAnalytics++
      e2eAnalytics.push(e.analytics.end_t - e.producer.push_t)
      if (e.worker) q2ToAnalytics.push(e.analytics.end_t - e.worker.worker_t)
    }
    if (e.log) {
      withLog++
      e2eLog.push(e.log.end_t - e.producer.push_t)
      if (e.worker) q2ToLog.push(e.log.end_t - e.worker.worker_t)
    }
  }

  // Delivery completeness (steady state).
  const completeness = {
    producedTxids: byTxid.size,
    reachedWorker: withWorker,
    reachedWorkerPct: (withWorker / byTxid.size) * 100,
    reachedAnalytics: withAnalytics,
    reachedAnalyticsPct: (withAnalytics / byTxid.size) * 100,
    reachedLog: withLog,
    reachedLogPct: (withLog / byTxid.size) * 100,
  }

  // Duplicate detection at each stage.
  const dupAt = (recs) => {
    const seen = new Map()
    let dup = 0
    for (const r of recs) {
      seen.set(r.txid, (seen.get(r.txid) || 0) + 1)
    }
    for (const c of seen.values()) if (c > 1) dup += c - 1
    return dup
  }
  const duplicates = {
    worker: dupAt(records.worker.filter((r) => r.push_t >= tWarmupEnd)),
    analytics: dupAt(records.analytics.filter((r) => r.push_t >= tWarmupEnd)),
    log: dupAt(records.log.filter((r) => r.push_t >= tWarmupEnd)),
  }

  const result = {
    config: {
      results_dir: RESULTS_DIR,
      warmup_sec: WARMUP_SEC,
      t0: new Date(t0).toISOString(),
      t_end: new Date(tEnd).toISOString(),
      steady_duration_sec: stageThroughput.steadyDurationSec,
    },
    throughput: stageThroughput,
    completeness,
    duplicates,
    latency_ms: {
      end_to_end_analytics: pickStats(e2eAnalytics),
      end_to_end_log: pickStats(e2eLog),
      q1_to_worker: pickStats(q1ToWorker),
      q2_to_analytics: pickStats(q2ToAnalytics),
      q2_to_log: pickStats(q2ToLog),
    },
  }

  process.stdout.write(JSON.stringify(result, null, 2) + '\n')

  // Human-readable to stderr.
  process.stderr.write('\n========================================\n')
  process.stderr.write(' Pipeline benchmark — analysis\n')
  process.stderr.write('========================================\n\n')
  process.stderr.write(
    `Window (steady): ${result.config.t0}  →  ${result.config.t_end}  (${result.config.steady_duration_sec}s after ${WARMUP_SEC}s warm-up)\n\n`,
  )
  process.stderr.write('Throughput (steady state, msg/s):\n')
  for (const [k, v] of Object.entries(stageThroughput)) {
    if (k === 'steadyDurationSec') continue
    process.stderr.write(`  ${k.padEnd(28)} ${(typeof v === 'number' ? v.toFixed(0) : v).padStart(8)}\n`)
  }
  process.stderr.write('\nDelivery completeness (steady state, of producer txids):\n')
  process.stderr.write(`  reached worker:    ${withWorker}/${byTxid.size} (${completeness.reachedWorkerPct.toFixed(2)}%)\n`)
  process.stderr.write(`  reached analytics: ${withAnalytics}/${byTxid.size} (${completeness.reachedAnalyticsPct.toFixed(2)}%)\n`)
  process.stderr.write(`  reached log:       ${withLog}/${byTxid.size} (${completeness.reachedLogPct.toFixed(2)}%)\n`)
  process.stderr.write(`\nDuplicate processing count (should be 0 with txn ack+push):\n`)
  process.stderr.write(`  worker:    ${duplicates.worker}\n`)
  process.stderr.write(`  analytics: ${duplicates.analytics}\n`)
  process.stderr.write(`  log:       ${duplicates.log}\n`)
  process.stderr.write('\nLatency p50 / p90 / p99 / max (ms):\n')
  for (const [k, v] of Object.entries(result.latency_ms)) {
    if (!v) continue
    process.stderr.write(
      `  ${k.padEnd(22)} ${String(v.p50).padStart(6)} / ${String(v.p90).padStart(6)} / ${String(v.p99).padStart(6)} / ${String(v.max).padStart(6)}  (n=${v.count})\n`,
    )
  }
  process.stderr.write('\n')
}

main().catch((e) => {
  process.stderr.write(`[analyze] fatal: ${e.stack || e.message}\n`)
  process.exit(1)
})
