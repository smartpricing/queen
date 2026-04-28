import autocannon from 'autocannon';
import axios from 'axios';
import { writeFileSync } from 'node:fs';

// Env-driven config. Defaults match scenarios.json defaults.
const SERVER_URL     = process.env.SERVER_URL     || 'http://localhost:6632';
const QUEUE_NAME     = process.env.QUEUE_NAME     || 'perf-test';
const WORKERS        = parseInt(process.env.WORKERS        || '2', 10);
const CONNECTIONS    = parseInt(process.env.CONNECTIONS    || '100', 10);
const DURATION       = parseInt(process.env.DURATION       || '60', 10);
const PUSH_BATCH     = parseInt(process.env.PUSH_BATCH     || '10', 10);
const MAX_PARTITIONS = parseInt(process.env.MAX_PARTITIONS || '500', 10);
const WARMUP         = process.env.WARMUP === '1';
const OUTPUT_FILE    = process.env.OUTPUT_FILE || (WARMUP ? '/dev/null' : './producer.json');
// Target per-message payload size (in bytes). 0 = no padding (tiny default payload).
// The producer pads `payload.pad` with 'x' characters to reach approximately this size,
// so each item is ~PAYLOAD_SIZE_BYTES bytes of JSON.
const PAYLOAD_SIZE_BYTES = parseInt(process.env.PAYLOAD_SIZE_BYTES || '0', 10);

async function ensureQueue() {
  try {
    await axios.post(`${SERVER_URL}/api/v1/configure`, {
      queue: QUEUE_NAME,
      options: {
        leaseTime: 60,
        retryLimit: 3,
        retentionEnabled: true,
        retentionSeconds: 1800,
        completedRetentionSeconds: 1800
      }
    }, { timeout: 5000 });
  } catch (err) {
    const msg = err?.response?.data || err.message;
    console.error(`[producer] queue configure warning: ${JSON.stringify(msg)}`);
  }
}

await ensureQueue();

// Pre-generate one request per partition with PUSH_BATCH items each.
// Optionally pad each item's payload to PAYLOAD_SIZE_BYTES (approximate).
function makePayload(p, j) {
  const base = { message: 'Hello World', partition_id: p, seq: j };
  if (PAYLOAD_SIZE_BYTES > 0) {
    const baseStr = JSON.stringify(base);
    // Account for overhead of the ',"pad":"..."' key/value wrapper. 15 is a safe
    // upper bound on the structural JSON overhead. Clamp to non-negative.
    const padLen = Math.max(0, PAYLOAD_SIZE_BYTES - baseStr.length - 15);
    base.pad = 'x'.repeat(padLen);
  }
  return base;
}

const requests = [];
for (let p = 0; p <= MAX_PARTITIONS; p++) {
  const items = [];
  for (let j = 0; j < PUSH_BATCH; j++) {
    items.push({
      queue: QUEUE_NAME,
      partition: `${p}`,
      payload: makePayload(p, j)
    });
  }
  requests.push({
    method: 'POST',
    path: '/api/v1/push',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ items })
  });
}

const instance = autocannon({
  url: SERVER_URL,
  connections: CONNECTIONS,
  duration: DURATION,
  workers: WORKERS,
  requests
});

instance.on('done', (results) => {
  const summary = {
    phase: WARMUP ? 'warmup' : 'measure',
    config: {
      workers: WORKERS,
      connections: CONNECTIONS,
      duration: DURATION,
      push_batch: PUSH_BATCH,
      max_partitions: MAX_PARTITIONS
    },
    requests: results.requests,
    latency: results.latency,
    throughput: results.throughput,
    errors: results.errors,
    timeouts: results.timeouts,
    non2xx: results.non2xx,
    start: results.start,
    finish: results.finish,
    duration: results.duration
  };
  writeFileSync(OUTPUT_FILE, JSON.stringify(summary, null, 2));
  if (!WARMUP) {
    console.log(`[producer] ${results.requests.total} req in ${results.duration}s, p50=${results.latency.p50}ms p99=${results.latency.p99}ms, errors=${results.errors}, non2xx=${results.non2xx}`);
  }
});

if (!WARMUP) autocannon.track(instance, { renderProgressBar: false });
