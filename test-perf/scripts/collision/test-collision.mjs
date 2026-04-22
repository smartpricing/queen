#!/usr/bin/env node
/*
 * Push/Pop collision benchmark.
 *
 * What it measures
 * ----------------
 * Exercises concurrent `push_messages_v3` + pop-path calls directly against
 * PostgreSQL (no HTTP, no libqueen) and samples `pg_stat_activity` for lock
 * waits attributable to the `queen.partition_lookup` row-lock contention
 * described in `docs/` (push's trigger blocks on pop's `FOR UPDATE`).
 *
 * Usage
 * -----
 *   PG_URL=postgres://postgres:postgres@localhost:5434/queen \
 *     node test-collision.mjs \
 *       --procedure pop_unified_batch          # or pop_unified_batch_v2
 *       --queue collision-bench
 *       --partitions 200
 *       --push-workers 16
 *       --pop-workers 16
 *       --push-batch 40
 *       --pop-batch 100
 *       --warmup 5
 *       --duration 30
 *       --output result.json
 *
 * All of --push-batch / --pop-batch mirror the shapes libqueen would send.
 *
 * Output
 * ------
 * JSON to --output (or stdout if omitted) with:
 *   - throughput: push ops/s, pop ops/s, push msgs/s, pop msgs/s
 *   - latency:    p50/p95/p99/max for push and pop PL/pgSQL calls
 *   - locks:      wait-event histogram sampled every 100ms
 *   - errors:     by type
 *   - config:     the arguments so runs are self-describing
 */

import pg from 'pg';
import { randomUUID } from 'node:crypto';
import fs from 'node:fs';
import { setTimeout as sleep } from 'node:timers/promises';

// ---------- argv ----------

function parseArgs() {
  const out = {
    procedure: 'pop_unified_batch',
    queue: 'collision-bench',
    partitions: 200,
    pushWorkers: 16,
    popWorkers: 16,
    pushBatch: 40,
    popBatch: 100,
    warmup: 5,
    duration: 30,
    output: null,
    prePushMessages: 50000, // seed data so pops have work from t=0
    verbose: false,
  };
  const argv = process.argv.slice(2);
  for (let i = 0; i < argv.length; i++) {
    const k = argv[i];
    const next = () => argv[++i];
    switch (k) {
      case '--procedure':      out.procedure       = next(); break;
      case '--queue':          out.queue           = next(); break;
      case '--partitions':     out.partitions      = +next(); break;
      case '--push-workers':   out.pushWorkers     = +next(); break;
      case '--pop-workers':    out.popWorkers      = +next(); break;
      case '--push-batch':     out.pushBatch       = +next(); break;
      case '--pop-batch':      out.popBatch        = +next(); break;
      case '--warmup':         out.warmup          = +next(); break;
      case '--duration':       out.duration        = +next(); break;
      case '--output':         out.output          = next(); break;
      case '--pre-push':       out.prePushMessages = +next(); break;
      case '--verbose':        out.verbose         = true; break;
      default: throw new Error(`unknown arg: ${k}`);
    }
  }
  if (!['pop_unified_batch', 'pop_unified_batch_v2'].includes(out.procedure)) {
    throw new Error(`--procedure must be pop_unified_batch or pop_unified_batch_v2, got: ${out.procedure}`);
  }
  return out;
}

const args = parseArgs();
const PG_URL = process.env.PG_URL;
if (!PG_URL) {
  console.error('PG_URL env var is required (e.g. postgres://postgres:postgres@localhost:5434/queen)');
  process.exit(2);
}

// ---------- helpers ----------

function pctl(arr, p) {
  if (arr.length === 0) return 0;
  const sorted = arr.slice().sort((a, b) => a - b);
  const i = Math.min(sorted.length - 1, Math.floor(sorted.length * p));
  return sorted[i];
}

// Reservoir-style sampling of latencies: cap the array so long runs don't
// blow up memory. 50k samples is plenty for stable percentiles.
const LATENCY_CAP = 50000;
function pushLatency(arr, value) {
  if (arr.length < LATENCY_CAP) { arr.push(value); return; }
  const idx = Math.floor(Math.random() * LATENCY_CAP);
  arr[idx] = value;
}

function dot() { if (!args.verbose) return; process.stdout.write('.'); }
function log(msg) { if (args.verbose) console.error(`[${new Date().toISOString()}] ${msg}`); }

// ---------- setup ----------

async function setup() {
  const admin = new pg.Client({ connectionString: PG_URL });
  await admin.connect();

  log(`creating queue "${args.queue}"`);
  await admin.query(`
    INSERT INTO queen.queues (name, namespace, task)
    VALUES ($1, '', '')
    ON CONFLICT (name) DO NOTHING
  `, [args.queue]);

  log(`ensuring ${args.partitions} partitions`);
  await admin.query(`
    INSERT INTO queen.partitions (queue_id, name)
    SELECT q.id, g::text
    FROM queen.queues q, generate_series(0, $1 - 1) g
    WHERE q.name = $2
    ON CONFLICT (queue_id, name) DO NOTHING
  `, [args.partitions, args.queue]);

  // Seed some messages so pops have work from t=0 and we really exercise
  // the fetch path, not just the empty-scan watermark path.
  if (args.prePushMessages > 0) {
    log(`seeding ${args.prePushMessages} messages (split evenly across partitions)`);
    const perBatch = 500;
    let seeded = 0;
    while (seeded < args.prePushMessages) {
      const batch = Math.min(perBatch, args.prePushMessages - seeded);
      const items = new Array(batch);
      for (let i = 0; i < batch; i++) {
        const p = (seeded + i) % args.partitions;
        items[i] = {
          queue: args.queue,
          partition: String(p),
          transactionId: randomUUID(),
          payload: { seed: true, n: seeded + i },
        };
      }
      await admin.query('SELECT queen.push_messages_v3($1::jsonb)', [JSON.stringify(items)]);
      seeded += batch;
    }
  }

  // Ensure a consumer_watermarks row exists so the wildcard branch doesn't
  // start each pop with an EXISTS probe storm during the first second.
  await admin.query(`
    INSERT INTO queen.consumer_watermarks (queue_name, consumer_group, last_empty_scan_at, updated_at)
    VALUES ($1, '__QUEUE_MODE__', NOW() - interval '1 hour', NOW() - interval '1 hour')
    ON CONFLICT (queue_name, consumer_group) DO NOTHING
  `, [args.queue]);

  await admin.end();
}

// ---------- workers ----------

function makePushItems() {
  const items = new Array(args.pushBatch);
  for (let i = 0; i < args.pushBatch; i++) {
    items[i] = {
      queue: args.queue,
      // random partition so push touches many partition_lookup rows,
      // matching the real-world producer pattern.
      partition: String(Math.floor(Math.random() * args.partitions)),
      transactionId: randomUUID(),
      payload: { b: 1 },
    };
  }
  return JSON.stringify(items);
}

function makePopReq(workerId) {
  return JSON.stringify([{
    idx: 0,
    queue_name: args.queue,
    // no partition_name -> wildcard discovery path (what we're fixing)
    consumer_group: '__QUEUE_MODE__',
    batch_size: args.popBatch,
    worker_id: workerId,
    auto_ack: true,
  }]);
}

async function pushWorker(stop, stats) {
  const client = new pg.Client({ connectionString: PG_URL });
  await client.connect();
  try {
    while (!stop.stopped) {
      const body = makePushItems();
      const t0 = performance.now();
      try {
        await client.query('SELECT queen.push_messages_v3($1::jsonb)', [body]);
        const dt = performance.now() - t0;
        if (stats.measuring) {
          stats.push.ops++;
          stats.push.msgs += args.pushBatch;
          pushLatency(stats.push.lat, dt);
        }
      } catch (e) {
        if (stats.measuring) stats.push.errors[e.code || e.message] = (stats.push.errors[e.code || e.message] || 0) + 1;
      }
    }
  } finally {
    await client.end().catch(() => {});
  }
}

async function popWorker(stop, stats) {
  const client = new pg.Client({ connectionString: PG_URL });
  await client.connect();
  const workerId = randomUUID();
  try {
    while (!stop.stopped) {
      const body = makePopReq(workerId);
      const t0 = performance.now();
      try {
        const res = await client.query(
          `SELECT queen.${args.procedure}($1::jsonb) AS r`,
          [body]
        );
        const dt = performance.now() - t0;
        if (stats.measuring) {
          stats.pop.ops++;
          pushLatency(stats.pop.lat, dt);
          const r = res.rows[0].r;
          const arr = Array.isArray(r) ? r : [];
          const first = arr[0];
          const success = first?.result?.success === true;
          const nMsgs = first?.result?.messages?.length ?? 0;
          if (success) {
            stats.pop.msgs += nMsgs;
            if (nMsgs === 0) stats.pop.emptyOk++;
          } else {
            const err = first?.result?.error ?? 'unknown';
            stats.pop.popErrors[err] = (stats.pop.popErrors[err] || 0) + 1;
          }
        }
      } catch (e) {
        if (stats.measuring) stats.pop.errors[e.code || e.message] = (stats.pop.errors[e.code || e.message] || 0) + 1;
      }
    }
  } finally {
    await client.end().catch(() => {});
  }
}

// Side-channel sampler: every 100ms, count active sessions by wait_event.
// We focus on the collision signal:
//   - Lock/tuple, Lock/transactionid: row-level lock waits (this is the hotspot)
//   - LWLock/BufferContent, LWLock/WALWrite: secondary
//   - IO/WALSync: fsync stalls
async function lockSampler(stop, stats, samplingPeriodMs = 100) {
  const client = new pg.Client({ connectionString: PG_URL });
  await client.connect();
  try {
    while (!stop.stopped) {
      try {
        const r = await client.query(`
          SELECT
            wait_event_type,
            wait_event,
            count(*)::int AS n
          FROM pg_stat_activity
          WHERE state = 'active'
            AND pid != pg_backend_pid()
            AND query LIKE '%queen.%'
          GROUP BY wait_event_type, wait_event
        `);
        if (stats.measuring) {
          stats.samples.total++;
          for (const row of r.rows) {
            const key = `${row.wait_event_type || 'null'}/${row.wait_event || 'null'}`;
            stats.samples.histogram[key] = (stats.samples.histogram[key] || 0) + row.n;
          }
        }
      } catch (_) { /* ignore: sampler is advisory */ }
      await sleep(samplingPeriodMs);
    }
  } finally {
    await client.end().catch(() => {});
  }
}

// ---------- main ----------

async function main() {
  log(`connecting to PG: ${PG_URL.replace(/\/\/[^@]*@/, '//***@')}`);
  log(`config: ${JSON.stringify(args)}`);

  await setup();

  const stats = {
    measuring: false,
    push: { ops: 0, msgs: 0, lat: [], errors: {} },
    pop:  { ops: 0, msgs: 0, lat: [], emptyOk: 0, errors: {}, popErrors: {} },
    samples: { total: 0, histogram: {} },
  };
  const stop = { stopped: false };

  // Spawn workers.
  const tasks = [];
  for (let i = 0; i < args.pushWorkers; i++) tasks.push(pushWorker(stop, stats));
  for (let i = 0; i < args.popWorkers;  i++) tasks.push(popWorker(stop, stats));
  tasks.push(lockSampler(stop, stats));

  log(`warmup: ${args.warmup}s with ${args.pushWorkers} push + ${args.popWorkers} pop workers`);
  await sleep(args.warmup * 1000);

  log(`measuring for ${args.duration}s`);
  stats.measuring = true;
  const measureStartMs = Date.now();
  await sleep(args.duration * 1000);
  const measureMs = Date.now() - measureStartMs;
  stats.measuring = false;

  log('draining');
  stop.stopped = true;
  await Promise.all(tasks);

  // Compile report.
  const secs = measureMs / 1000;
  const report = {
    config: args,
    durationSec: secs,
    push: {
      ops: stats.push.ops,
      msgs: stats.push.msgs,
      opsPerSec: +(stats.push.ops / secs).toFixed(1),
      msgsPerSec: +(stats.push.msgs / secs).toFixed(1),
      latencyMs: {
        p50: +pctl(stats.push.lat, 0.50).toFixed(2),
        p95: +pctl(stats.push.lat, 0.95).toFixed(2),
        p99: +pctl(stats.push.lat, 0.99).toFixed(2),
        max: +Math.max(0, ...stats.push.lat).toFixed(2),
        n: stats.push.lat.length,
      },
      errors: stats.push.errors,
    },
    pop: {
      ops: stats.pop.ops,
      msgs: stats.pop.msgs,
      opsPerSec: +(stats.pop.ops / secs).toFixed(1),
      msgsPerSec: +(stats.pop.msgs / secs).toFixed(1),
      emptyResponses: stats.pop.emptyOk,
      latencyMs: {
        p50: +pctl(stats.pop.lat, 0.50).toFixed(2),
        p95: +pctl(stats.pop.lat, 0.95).toFixed(2),
        p99: +pctl(stats.pop.lat, 0.99).toFixed(2),
        max: +Math.max(0, ...stats.pop.lat).toFixed(2),
        n: stats.pop.lat.length,
      },
      errors: stats.pop.errors,
      popErrors: stats.pop.popErrors,
    },
    locks: {
      samples: stats.samples.total,
      samplingPeriodMs: 100,
      histogram: stats.samples.histogram,
      // Dedicated call-outs for the collision signal:
      tupleWaits: stats.samples.histogram['Lock/tuple'] || 0,
      transactionidWaits: stats.samples.histogram['Lock/transactionid'] || 0,
      // Mean blocked sessions per sample (shows sustained contention, not just bursts)
      avgTupleBlockedPerSample: stats.samples.total
        ? +((stats.samples.histogram['Lock/tuple'] || 0) / stats.samples.total).toFixed(2)
        : 0,
      avgXidBlockedPerSample: stats.samples.total
        ? +((stats.samples.histogram['Lock/transactionid'] || 0) / stats.samples.total).toFixed(2)
        : 0,
    },
  };

  const json = JSON.stringify(report, null, 2);
  if (args.output) {
    fs.writeFileSync(args.output, json + '\n');
    log(`wrote ${args.output}`);
  } else {
    console.log(json);
  }
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
