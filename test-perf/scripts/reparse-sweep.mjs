// Re-parse sweep-cores-payload artifacts into a clean summary.csv.
// Usage:  node reparse-sweep.mjs <sweep-dir>
import fs from 'node:fs';
import path from 'node:path';

const dir = process.argv[2];
if (!dir) {
  console.error('usage: node reparse-sweep.mjs <sweep-dir>');
  process.exit(2);
}
const out = path.join(dir, 'summary.csv');
const header = [
  'pg_cores', 'payload_kb', 'push_batch',
  'pg_ins_per_s', 'pg_bytes_per_s',
  'xact_per_s', 'msgs_per_commit',
  'push_p99_ms', 'pop_p99_ms',
  'vegas_f_mean', 'vegas_f_max',
  'vegas_limit_mean', 'vegas_limit_max',
  'pg_cpu_p95', 'srv_cpu_p95',
  'push_rps', 'pop_rps',
  'errors', 'non2xx'
];
fs.writeFileSync(out, header.join(',') + '\n');

const loadJson = (p) => {
  try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch { return null; }
};

const statS = (xs) => {
  if (!xs.length) return { mean: 0, p95: 0, max: 0 };
  const s = [...xs].sort((a, b) => a - b);
  return {
    mean: xs.reduce((a, b) => a + b, 0) / xs.length,
    p95: s[Math.floor(s.length * 0.95)],
    max: s[s.length - 1],
  };
};

const measureSeconds = (pre, post) => {
  if (pre?.database?.ts && post?.database?.ts) {
    return Math.max(1, post.database.ts - pre.database.ts);
  }
  return 30;
};

const rows = [];
for (const sub of fs.readdirSync(dir)) {
  if (!sub.startsWith('pg')) continue;
  const m = sub.match(/pg(\d+)c_payload(\d+)kb/);
  if (!m) continue;
  const pgCores = +m[1];
  const payloadKb = +m[2];
  const cdir = path.join(dir, sub);

  const pre = loadJson(path.join(cdir, 'pg-stats-pre.json'));
  const post = loadJson(path.join(cdir, 'pg-stats-post.json'));
  const prod = loadJson(path.join(cdir, 'producer.json')) || {};
  const cons = loadJson(path.join(cdir, 'consumer.json')) || {};

  const ms = measureSeconds(pre, post);
  const insDelta =
    (post?.messages_table?.n_tup_ins ?? 0) -
    (pre?.messages_table?.n_tup_ins ?? 0);
  const xactDelta =
    (post?.database?.xact_commit ?? 0) - (pre?.database?.xact_commit ?? 0);
  const pgIns = insDelta / ms;
  const xact = xactDelta / ms;
  const msgsPerCommit = xact > 0 ? pgIns / xact : 0;

  const pushRps = prod?.requests?.average ?? 0;
  const popRps = cons?.requests?.average ?? 0;
  const pushP99 = prod?.latency?.p99 ?? 0;
  const popP99 = cons?.latency?.p99 ?? 0;
  const errors = (prod?.errors ?? 0) + (cons?.errors ?? 0);
  const non2xx = (prod?.non2xx ?? 0) + (cons?.non2xx ?? 0);

  const streamPath = path.join(cdir, 'stats-stream.jsonl');
  const pgCpuArr = [];
  const srvCpuArr = [];
  if (fs.existsSync(streamPath)) {
    for (const line of fs.readFileSync(streamPath, 'utf8').split('\n')) {
      if (!line.trim()) continue;
      try {
        const j = JSON.parse(line);
        if (j.pg?.CPUPerc) {
          const v = parseFloat(j.pg.CPUPerc);
          if (!isNaN(v)) pgCpuArr.push(v);
        }
        if (typeof j.server?.cpu_pct === 'number') srvCpuArr.push(j.server.cpu_pct);
      } catch {}
    }
  }
  const pgCpu = statS(pgCpuArr);
  const srvCpu = statS(srvCpuArr);

  const logPath = path.join(cdir, 'server.log');
  const fArr = [];
  const limArr = [];
  if (fs.existsSync(logPath)) {
    const text = fs.readFileSync(logPath, 'utf8');
    for (const line of text.split('\n')) {
      if (!line.includes('[libqueen]') || !line.includes('push(q=')) continue;
      const mm = line.match(/push\(q=\d+ f=(\d+)\/(\d+) /);
      if (mm) {
        fArr.push(+mm[1]);
        limArr.push(+mm[2]);
      }
    }
  }
  const fStats = statS(fArr);
  const limStats = statS(limArr);

  rows.push({
    pgCores,
    payloadKb,
    row: [
      pgCores,
      payloadKb,
      1,
      pgIns.toFixed(1),
      (pgIns * payloadKb * 1024).toFixed(0),
      xact.toFixed(1),
      msgsPerCommit.toFixed(1),
      pushP99,
      popP99,
      fStats.mean.toFixed(2),
      fStats.max,
      limStats.mean.toFixed(2),
      limStats.max,
      pgCpu.p95.toFixed(1),
      srvCpu.p95.toFixed(1),
      pushRps.toFixed(1),
      popRps.toFixed(1),
      errors,
      non2xx,
    ],
  });
}
rows.sort((a, b) => a.pgCores - b.pgCores || a.payloadKb - b.payloadKb);
for (const r of rows) fs.appendFileSync(out, r.row.join(',') + '\n');
console.log(fs.readFileSync(out, 'utf8'));
