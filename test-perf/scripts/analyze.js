#!/usr/bin/env node
// Aggregate a campaign directory into aggregate.json + report.md.
//
// Usage: node analyze.js <campaign-dir>
//
// Reads:
//   <campaign-dir>/scenario-*/run-N/config.json
//                              /producer.json     (autocannon raw)
//                              /consumer.json
//                              /server.log
//                              /server-analytics.json
//                              /docker-stats.json
//
// Writes:
//   <campaign-dir>/aggregate.json   machine-readable per-scenario stats
//   <campaign-dir>/report.md        human-readable table with stddev + deltas
//
// Metric names:
//   push_rps  = producer.requests.average
//   push_p50  = producer.latency.p50
//   push_p99  = producer.latency.p99
//   push_msgs_per_s = push_rps * push_batch      (items actually produced)
//   pop_rps   = consumer.requests.average
//   pop_p50   = consumer.latency.p50
//   pop_p99   = consumer.latency.p99
//   pop_msgs_per_s ≈ reported by queen analytics / duration (best effort)
//
// Aggregation across runs: mean, stddev, min, max.

import { readFileSync, writeFileSync, readdirSync, existsSync, statSync } from 'node:fs';
import { join } from 'node:path';

const campaignDir = process.argv[2];
if (!campaignDir) {
  console.error('usage: node analyze.js <campaign-dir>');
  process.exit(2);
}

const readJson = (p, fallback = null) => {
  try { return JSON.parse(readFileSync(p, 'utf8')); } catch { return fallback; }
};

const mean = (xs) => xs.length ? xs.reduce((a,b)=>a+b,0) / xs.length : 0;
const stddev = (xs) => {
  if (xs.length < 2) return 0;
  const m = mean(xs);
  return Math.sqrt(xs.reduce((a,b)=>a + (b-m)*(b-m), 0) / (xs.length - 1));
};
const pct = (a, b) => (b === 0 ? 0 : ((a - b) / b) * 100);
const fmt = (n, d = 2) => (typeof n === 'number' ? n.toFixed(d) : String(n));

// Patterns we treat as cosmetic noise in the server log and do NOT count as warnings.
const COSMETIC_WARNING_PATTERNS = [
  /Webapp directory not found/i,
  /Set WEBAPP_ROOT environment variable/i,
  /Searched:.*webapp/i,
  /Encryption disabled - QUEEN_ENCRYPTION_KEY not set/i,
];

function isCosmeticWarning(line) {
  return COSMETIC_WARNING_PATTERNS.some(p => p.test(line));
}

function parseLogCounts(logPath) {
  if (!existsSync(logPath)) return { lag_events: 0, lag_max_ms: 0, errors: 0, warnings: 0, queue_full: 0 };
  const text = readFileSync(logPath, 'utf8');
  const lines = text.split('\n');
  let lag_events = 0, lag_max_ms = 0, errors = 0, warnings = 0, queue_full = 0;
  for (const line of lines) {
    const lagMatch = line.match(/Event loop lag:\s*(\d+)\s*ms/i);
    if (lagMatch) {
      lag_events++;
      const v = parseInt(lagMatch[1], 10);
      if (v > lag_max_ms) lag_max_ms = v;
    }
    if (/\[error\]/i.test(line)) errors++;
    if (/\[warning\]/i.test(line) && !isCosmeticWarning(line)) warnings++;
    if (/queue_full|overloaded/i.test(line)) queue_full++;
  }
  return { lag_events, lag_max_ms, errors, warnings, queue_full };
}

// Diff the pre/post PG-stats snapshots produced by collect-pg-stats.sh.
// Returns null if either file is missing/unparseable.
function diffPgStats(preJson, postJson, measureSeconds) {
  if (!preJson || !postJson) return null;
  const dur = measureSeconds > 0 ? measureSeconds : 1;
  const dbDelta = (key) => {
    const a = preJson?.database?.[key];
    const b = postJson?.database?.[key];
    if (typeof a !== 'number' || typeof b !== 'number') return null;
    return b - a;
  };
  const tableDelta = (table, key) => {
    const a = preJson?.[table]?.[key];
    const b = postJson?.[table]?.[key];
    if (typeof a !== 'number' || typeof b !== 'number') return null;
    return b - a;
  };
  const safeDiv = (num) => (num == null ? null : num / dur);

  return {
    measure_seconds: dur,
    // database-level counters
    xact_commit_delta:   dbDelta('xact_commit'),
    xact_commit_per_s:   safeDiv(dbDelta('xact_commit')),
    xact_rollback_delta: dbDelta('xact_rollback'),
    tup_inserted_delta:  dbDelta('tup_inserted'),
    tup_inserted_per_s:  safeDiv(dbDelta('tup_inserted')),
    tup_updated_delta:   dbDelta('tup_updated'),
    tup_updated_per_s:   safeDiv(dbDelta('tup_updated')),
    tup_deleted_delta:   dbDelta('tup_deleted'),
    tup_fetched_delta:   dbDelta('tup_fetched'),
    tup_fetched_per_s:   safeDiv(dbDelta('tup_fetched')),
    blks_read_delta:     dbDelta('blks_read'),
    blks_hit_delta:      dbDelta('blks_hit'),
    buffer_hit_ratio: (() => {
      const r = dbDelta('blks_read'); const h = dbDelta('blks_hit');
      if (r == null || h == null || (r + h) === 0) return null;
      return h / (r + h);
    })(),
    deadlocks_delta: dbDelta('deadlocks'),
    // queen.messages = append-only push target. n_tup_ins = actual pushes.
    messages_n_tup_ins_delta: tableDelta('messages_table', 'n_tup_ins'),
    messages_n_tup_ins_per_s: safeDiv(tableDelta('messages_table', 'n_tup_ins')),
    messages_n_tup_del_delta: tableDelta('messages_table', 'n_tup_del'),
    // queen.partition_consumers = cursor advanced by pop/ack. n_tup_upd ≈ pop operations.
    // Note: pops of multiple messages batch-update one row, so this is "pop CALLS" not "messages popped".
    consumers_n_tup_upd_delta: tableDelta('partition_consumers_table', 'n_tup_upd'),
    consumers_n_tup_upd_per_s: safeDiv(tableDelta('partition_consumers_table', 'n_tup_upd')),
    consumers_n_tup_hot_upd_delta: tableDelta('partition_consumers_table', 'n_tup_hot_upd'),
    // final state
    messages_final_total: postJson?.row_counts?.messages_total ?? null,
    partitions_final_total: postJson?.row_counts?.partitions_total ?? null,
    partition_consumers_final_total: postJson?.row_counts?.partition_consumers_total ?? null,
  };
}

// Parse stats-stream.jsonl into a tiny summary (we don't include full samples
// in aggregate.json — too bulky). Returns p50/p95/max of PG CPU% and server CPU%.
function summarizeStatsStream(path) {
  if (!existsSync(path)) return null;
  const lines = readFileSync(path, 'utf8').split('\n').filter(Boolean);
  const pgCpu = [], srvCpu = [], pgMemPct = [];
  for (const ln of lines) {
    try {
      const o = JSON.parse(ln);
      // docker stats' CPUPerc is a string like "42.5%"
      const pgC = o?.pg?.CPUPerc;
      if (typeof pgC === 'string') {
        const v = parseFloat(pgC.replace('%',''));
        if (!Number.isNaN(v)) pgCpu.push(v);
      }
      const pgM = o?.pg?.MemPerc;
      if (typeof pgM === 'string') {
        const v = parseFloat(pgM.replace('%',''));
        if (!Number.isNaN(v)) pgMemPct.push(v);
      }
      const sC = o?.server?.cpu_pct;
      if (typeof sC === 'number') srvCpu.push(sC);
    } catch { /* skip malformed line */ }
  }
  const pct = (arr, p) => {
    if (arr.length === 0) return null;
    const sorted = [...arr].sort((a,b)=>a-b);
    const idx = Math.min(sorted.length - 1, Math.floor(p / 100 * sorted.length));
    return sorted[idx];
  };
  return {
    samples: lines.length,
    pg_cpu_pct:  { p50: pct(pgCpu, 50),  p95: pct(pgCpu, 95),  max: pgCpu.length ? Math.max(...pgCpu) : null },
    pg_mem_pct:  { p50: pct(pgMemPct,50),p95: pct(pgMemPct,95),max: pgMemPct.length ? Math.max(...pgMemPct) : null },
    server_cpu_pct: { p50: pct(srvCpu, 50), p95: pct(srvCpu, 95), max: srvCpu.length ? Math.max(...srvCpu) : null },
  };
}

function collectScenario(scenarioDir) {
  const runs = readdirSync(scenarioDir)
    .filter(n => n.startsWith('run-'))
    .map(n => join(scenarioDir, n))
    .filter(p => statSync(p).isDirectory())
    .sort();

  const perRun = runs.map(runDir => {
    const cfg = readJson(join(runDir, 'config.json'));
    const prod = readJson(join(runDir, 'producer.json'));
    const cons = readJson(join(runDir, 'consumer.json'));
    const analytics = readJson(join(runDir, 'server-analytics.json'));
    const logCounts = parseLogCounts(join(runDir, 'server.log'));
    const pgPre = readJson(join(runDir, 'pg-stats-pre.json'));
    const pgPost = readJson(join(runDir, 'pg-stats-post.json'));
    const measureSeconds = cfg?.measure_actual_seconds ?? cfg?.timing?.measure_seconds ?? 60;
    const pgStats = diffPgStats(pgPre, pgPost, measureSeconds);
    const streamSummary = summarizeStatsStream(join(runDir, 'stats-stream.jsonl'));

    const push_rps  = prod?.requests?.average ?? 0;
    const push_total = prod?.requests?.total ?? 0;
    const push_duration = prod?.duration ?? 0;
    const push_p50  = prod?.latency?.p50 ?? 0;
    const push_p95  = prod?.latency?.p97_5 ?? prod?.latency?.p95 ?? 0;
    const push_p99  = prod?.latency?.p99 ?? 0;
    const push_errors  = prod?.errors ?? 0;
    const push_non2xx  = prod?.non2xx ?? 0;

    const pop_rps   = cons?.requests?.average ?? 0;
    const pop_total  = cons?.requests?.total ?? 0;
    const pop_duration = cons?.duration ?? 0;
    const pop_p50   = cons?.latency?.p50 ?? 0;
    const pop_p95   = cons?.latency?.p97_5 ?? cons?.latency?.p95 ?? 0;
    const pop_p99   = cons?.latency?.p99 ?? 0;
    const pop_errors   = cons?.errors ?? 0;
    const pop_non2xx   = cons?.non2xx ?? 0;

    const pushBatch = cfg?.producer?.push_batch ?? 1;
    const push_msgs_per_s = push_rps * pushBatch;

    return {
      run: cfg?.run_index ?? null,
      push_rps, push_total, push_duration,
      push_p50, push_p95, push_p99,
      push_errors, push_non2xx,
      push_msgs_per_s,
      pop_rps, pop_total, pop_duration,
      pop_p50, pop_p95, pop_p99,
      pop_errors, pop_non2xx,
      queue_final_total: analytics?.totals?.total ?? null,
      log: logCounts,
      pg_stats: pgStats,
      stats_stream: streamSummary,
      host: {
        oversubscribed: cfg?.host_oversubscribed ?? null,
        cpus: cfg?.host_cpus ?? null,
        heavy_threads: cfg?.heavy_host_threads ?? null,
      },
    };
  });

  const agg = {};
  const numeric = [
    'push_rps','push_msgs_per_s','push_p50','push_p95','push_p99','push_errors','push_non2xx',
    'pop_rps','pop_p50','pop_p95','pop_p99','pop_errors','pop_non2xx',
  ];
  for (const key of numeric) {
    const vs = perRun.map(r => r[key]).filter(v => typeof v === 'number');
    agg[key] = { mean: mean(vs), stddev: stddev(vs), min: Math.min(...vs, Infinity) === Infinity ? 0 : Math.min(...vs), max: Math.max(...vs, -Infinity) === -Infinity ? 0 : Math.max(...vs) };
  }

  // Aggregate PG-ground-truth metrics too.
  const pgNumeric = [
    'xact_commit_per_s','tup_inserted_per_s','tup_updated_per_s','tup_fetched_per_s',
    'messages_n_tup_ins_per_s','consumers_n_tup_upd_per_s',
    'buffer_hit_ratio','deadlocks_delta','xact_rollback_delta',
  ];
  agg.pg = {};
  for (const key of pgNumeric) {
    const vs = perRun.map(r => r.pg_stats?.[key]).filter(v => typeof v === 'number');
    agg.pg[key] = vs.length === 0
      ? { mean: null, stddev: null, min: null, max: null, n: 0 }
      : { mean: mean(vs), stddev: stddev(vs), min: Math.min(...vs), max: Math.max(...vs), n: vs.length };
  }
  // Final message count (should be ~= inserted - completed-evicted; gives a
  // rough sanity check against autocannon-reported push counts).
  const finals = perRun.map(r => r.pg_stats?.messages_final_total).filter(v => typeof v === 'number');
  agg.pg.messages_final_total = finals.length === 0
    ? { mean: null, max: null }
    : { mean: mean(finals), max: Math.max(...finals) };

  // docker/host stats stream summary (medians across runs for stability).
  const pg_cpu_p95s = perRun.map(r => r.stats_stream?.pg_cpu_pct?.p95).filter(v => typeof v === 'number');
  const pg_cpu_maxs = perRun.map(r => r.stats_stream?.pg_cpu_pct?.max).filter(v => typeof v === 'number');
  const srv_cpu_p95s = perRun.map(r => r.stats_stream?.server_cpu_pct?.p95).filter(v => typeof v === 'number');
  const srv_cpu_maxs = perRun.map(r => r.stats_stream?.server_cpu_pct?.max).filter(v => typeof v === 'number');
  agg.resources = {
    pg_cpu_pct_p95: pg_cpu_p95s.length ? mean(pg_cpu_p95s) : null,
    pg_cpu_pct_max: pg_cpu_maxs.length ? Math.max(...pg_cpu_maxs) : null,
    server_cpu_pct_p95: srv_cpu_p95s.length ? mean(srv_cpu_p95s) : null,
    server_cpu_pct_max: srv_cpu_maxs.length ? Math.max(...srv_cpu_maxs) : null,
  };
  agg.host_oversubscribed = perRun.some(r => r.host?.oversubscribed === true);
  const lag_events = perRun.map(r => r.log.lag_events);
  const lag_max    = perRun.map(r => r.log.lag_max_ms);
  const errors     = perRun.map(r => r.log.errors);
  const warnings   = perRun.map(r => r.log.warnings);
  const queue_full = perRun.map(r => r.log.queue_full);
  agg.log = {
    lag_events: { mean: mean(lag_events), max: Math.max(...lag_events, 0) },
    lag_max_ms: { mean: mean(lag_max),    max: Math.max(...lag_max, 0) },
    errors_lines:    { mean: mean(errors),     max: Math.max(...errors, 0) },
    warning_lines:   { mean: mean(warnings),   max: Math.max(...warnings, 0) },
    queue_full_hits: { mean: mean(queue_full), max: Math.max(...queue_full, 0) },
  };

  const cfg0 = readJson(join(runs[0], 'config.json')) || {};
  return { scenario_id: cfg0.scenario_id, scenario_name: cfg0.scenario_name, config: cfg0, per_run: perRun, aggregate: agg };
}

// ---- main ----

const scenarioDirs = readdirSync(campaignDir)
  .filter(n => n.startsWith('scenario-'))
  .map(n => join(campaignDir, n))
  .filter(p => statSync(p).isDirectory())
  .sort();

const results = scenarioDirs.map(collectScenario);
results.sort((a,b) => (a.scenario_id ?? 0) - (b.scenario_id ?? 0));

const aggregatePath = join(campaignDir, 'aggregate.json');
writeFileSync(aggregatePath, JSON.stringify(results, null, 2));
console.log(`[analyze] wrote ${aggregatePath}`);

// ---- report.md ----
const meta = readJson(join(campaignDir, 'meta.json')) || {};
const baseline = results[0]?.aggregate;

const header = [
  '# Perf campaign report',
  '',
  `Campaign: \`${meta.stamp ?? ''}\`  `,
  `Git: \`${meta.git?.branch ?? ''} @ ${meta.git?.sha ?? ''}\` (dirty files: ${meta.git?.dirty_files ?? 0})  `,
  `Started: ${meta.start_iso ?? ''}  Finished: ${meta.finish_iso ?? ''}  `,
  `Scenarios: ${(meta.scenarios ?? []).join(', ')}  `,
  `Runs per scenario: ${meta.runs_per_scenario ?? '?'}  Warmup: ${meta.warmup_seconds ?? '?'}s  Measure: ${meta.measure_seconds ?? '?'}s  `,
  `Host: ${meta.host?.cpus ?? '?'} logical CPUs, ${meta.host?.kernel ?? ''}  `,
  '',
  '## Headline: throughput & latency (mean ± stddev over runs)',
  '',
  '- `push_msgs/s` = push_rps × push_batch (client-side estimate of items produced).',
  '- `pg_ins/s` = `queen.messages` rows inserted/sec measured from `pg_stat_user_tables` (**PUSH ground truth**).',
  '- `pop_upd/s` = `queen.partition_consumers` UPDATEs/sec. Not "pops that got data" directly — the pop procedure does multiple UPDATEs per call (lease, cursor, counters). A proxy for pop activity level.',
  '- `pg_tx/s` = `xact_commit` rate across all PG sessions.',
  '- ⚠️ = host was over-subscribed during this run (more heavy threads than CPUs-2). Interpret with caution.',
  '',
  '| # | Scenario | push req/s | push msgs/s | pg_ins/s | pop req/s | pop_upd/s | pg_tx/s | push p99 (ms) | errors p/c | host | Δ pg_ins/s vs #1 |',
  '|---|---|---:|---:|---:|---:|---:|---:|---:|---:|:---:|---:|',
];

const rows = results.map(r => {
  const a = r.aggregate;
  // Use PG ground truth for the delta when available; fall back to client-side estimate.
  const baselineIns = baseline?.pg?.messages_n_tup_ins_per_s?.mean;
  const curIns = a.pg?.messages_n_tup_ins_per_s?.mean;
  const haveTruth = typeof curIns === 'number' && typeof baselineIns === 'number' && baselineIns > 0;
  const delta = haveTruth ? pct(curIns, baselineIns)
              : (baseline?.push_msgs_per_s?.mean > 0 ? pct(a.push_msgs_per_s.mean, baseline.push_msgs_per_s.mean) : 0);
  const oversubIcon = a.host_oversubscribed ? '⚠️' : '';
  const ins = a.pg?.messages_n_tup_ins_per_s?.mean;
  const upd = a.pg?.consumers_n_tup_upd_per_s?.mean;
  const tx  = a.pg?.xact_commit_per_s?.mean;
  return `| ${r.scenario_id} | \`${r.scenario_name}\` | ${fmt(a.push_rps.mean,0)} ± ${fmt(a.push_rps.stddev,0)} | ${fmt(a.push_msgs_per_s.mean,0)} | ${ins==null?'—':fmt(ins,0)} | ${fmt(a.pop_rps.mean,0)} | ${upd==null?'—':fmt(upd,0)} | ${tx==null?'—':fmt(tx,0)} | ${fmt(a.push_p99.mean,0)} ± ${fmt(a.push_p99.stddev,0)} | ${fmt(a.push_errors.mean+a.push_non2xx.mean,0)} / ${fmt(a.pop_errors.mean+a.pop_non2xx.mean,0)} | ${oversubIcon} | ${delta >= 0 ? '+' : ''}${fmt(delta,1)}% |`;
});

const deltaSection = [
  '',
  '## Per-scenario detail',
  '',
];
for (const r of results) {
  const a = r.aggregate;
  deltaSection.push(`### Scenario ${r.scenario_id} — \`${r.scenario_name}\``);
  deltaSection.push('');
  const cfg = r.config;
  if (cfg) {
    deltaSection.push(`- PG: ${cfg.pg?.cpus}c / ${cfg.pg?.memory_gb}g (max_connections=${cfg.pg?.max_connections})`);
    deltaSection.push(`- Queen: NUM_WORKERS=${cfg.queen?.num_workers}, SIDECAR_POOL_SIZE=${cfg.queen?.sidecar_pool_size} (total), DB_POOL_SIZE=${cfg.queen?.db_pool_size} (total)`);
    deltaSection.push(`- Producer: workers=${cfg.producer?.workers}, connections=${cfg.producer?.connections}, push_batch=${cfg.producer?.push_batch}`);
    deltaSection.push(`- Consumer: workers=${cfg.consumer?.workers}, connections=${cfg.consumer?.connections}, pop_batch=${cfg.consumer?.pop_batch}`);
    deltaSection.push('');
  }
  deltaSection.push('| metric | mean | stddev | min | max |');
  deltaSection.push('|---|---:|---:|---:|---:|');
  const keys = ['push_rps','push_msgs_per_s','push_p50','push_p95','push_p99','push_errors','push_non2xx',
                'pop_rps','pop_p50','pop_p95','pop_p99','pop_errors','pop_non2xx'];
  for (const k of keys) {
    const m = a[k];
    deltaSection.push(`| ${k} | ${fmt(m.mean,2)} | ${fmt(m.stddev,2)} | ${fmt(m.min,2)} | ${fmt(m.max,2)} |`);
  }
  deltaSection.push('');
  deltaSection.push(`Server log: lag events mean=${fmt(a.log.lag_events.mean,1)} (max ${a.log.lag_max_ms.max}ms), error lines mean=${fmt(a.log.errors_lines.mean,1)}, warnings mean=${fmt(a.log.warning_lines.mean,1)} (cosmetic filtered), queue_full hits mean=${fmt(a.log.queue_full_hits.mean,1)}.`);

  if (a.pg && a.pg.messages_n_tup_ins_per_s?.mean != null) {
    deltaSection.push('');
    deltaSection.push('**PG ground truth (diff of `pg_stat_*` over measurement window):**');
    deltaSection.push('');
    deltaSection.push('| metric | mean | stddev | min | max |');
    deltaSection.push('|---|---:|---:|---:|---:|');
    const rowPg = (label, m) => deltaSection.push(`| ${label} | ${m?.mean==null?'—':fmt(m.mean,0)} | ${m?.stddev==null?'—':fmt(m.stddev,0)} | ${m?.min==null?'—':fmt(m.min,0)} | ${m?.max==null?'—':fmt(m.max,0)} |`);
    rowPg('queen.messages inserts/s (PUSH truth)', a.pg.messages_n_tup_ins_per_s);
    rowPg('partition_consumers UPDATEs/s (pop activity)', a.pg.consumers_n_tup_upd_per_s);
    rowPg('xact_commit/s',            a.pg.xact_commit_per_s);
    rowPg('tup_fetched/s',            a.pg.tup_fetched_per_s);
    rowPg('buffer_hit_ratio',         a.pg.buffer_hit_ratio);
    rowPg('deadlocks (delta)',        a.pg.deadlocks_delta);
    rowPg('xact_rollback (delta)',    a.pg.xact_rollback_delta);
    deltaSection.push('');
    deltaSection.push(`Final \`queen.messages\` rows (mean): ${fmt(a.pg.messages_final_total?.mean ?? 0, 0)} (max ${fmt(a.pg.messages_final_total?.max ?? 0, 0)}).`);
  }

  if (a.resources && (a.resources.pg_cpu_pct_p95 != null || a.resources.server_cpu_pct_p95 != null)) {
    deltaSection.push('');
    deltaSection.push('**Resource usage during measurement** (sampled every ~2s):');
    deltaSection.push('');
    deltaSection.push(`- PG container CPU%: p95 ${fmt(a.resources.pg_cpu_pct_p95 ?? 0,1)}, max ${fmt(a.resources.pg_cpu_pct_max ?? 0,1)}`);
    deltaSection.push(`- queen-server CPU%: p95 ${fmt(a.resources.server_cpu_pct_p95 ?? 0,1)}, max ${fmt(a.resources.server_cpu_pct_max ?? 0,1)}`);
    if (a.host_oversubscribed) {
      deltaSection.push(`- ⚠️ Host over-subscribed: ${r.config.heavy_host_threads} heavy threads on ${r.config.host_cpus} CPUs. Results reflect scheduler contention.`);
    }
  }
  deltaSection.push('');
}

const notes = [
  '## How to read this',
  '',
  '1. **Primary signal is `pg_ins/s`** (messages actually inserted, measured from `pg_stat_user_tables`). `push msgs/s` is a client-side estimate and can over-count if retries happen or under-count if the server rejects after acknowledge.',
  '2. **`pg_upd/s` ≈ pops + acks** because autoAck updates message rows to completed. If `pop req/s` is high but `pg_upd/s` is low, the consumer is mostly getting empty pops (queue was drained faster than produced).',
  '3. **Treat deltas as noise** unless `|Δ|` exceeds ~2× the reported stddev. The stddev column tells you how reproducible each number is across the 3 runs.',
  '4. **⚠️ host-oversubscribed rows**: the laptop is running more heavy threads than it has CPUs (minus 2). Those numbers reflect OS scheduler contention more than queen/PG behavior — the workload was bottlenecked on the client-side bench, not the server. Ignore for server-side comparisons.',
  '5. **`push_p99` blow-ups** without matching `pg_ins/s` drop mean the server is queuing but not rejecting — look at `queue_full hits` and the `PG ground truth` section.',
  '6. **High `errors`/`non2xx`** invalidate the run. Re-check server log for reconnects/disconnects.',
  '7. **PG CPU at ~`PG_CPUS × 100%`** means PG is the bottleneck. PG CPU far below that + high push_p99 means queen is the bottleneck.',
  '',
];

const md = [...header, ...rows, ...deltaSection, ...notes].join('\n');
const reportPath = join(campaignDir, 'report.md');
writeFileSync(reportPath, md);
console.log(`[analyze] wrote ${reportPath}`);
