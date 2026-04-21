import autocannon from 'autocannon';
import { writeFileSync } from 'node:fs';

// Env-driven config. Defaults match scenarios.json defaults.
const SERVER_URL     = process.env.SERVER_URL     || 'http://localhost:6632';
const QUEUE_NAME     = process.env.QUEUE_NAME     || 'perf-test';
const WORKERS        = parseInt(process.env.WORKERS        || '1', 10);
const CONNECTIONS    = parseInt(process.env.CONNECTIONS    || '25', 10);
const DURATION       = parseInt(process.env.DURATION       || '60', 10);
const POP_BATCH      = parseInt(process.env.POP_BATCH      || '100', 10);
// Server-side long-poll timeout per request. Must be < autocannon's per-request timeout below.
const POP_WAIT_MS    = parseInt(process.env.POP_WAIT_MS    || '5000', 10);
const OUTPUT_FILE    = process.env.OUTPUT_FILE || './consumer.json';

// Wildcard long-poll pop: production consumer path.
// - Wildcard: server picks an available partition via SKIP LOCKED discovery.
// - Long-poll (wait=true, timeout=5s): request parks on server until a message
//   arrives or the timeout fires. Exercises the backoff-tracker / UDP-notify
//   code path that the libqueen hot-path plan actually modifies.
const url = `${SERVER_URL}/api/v1/pop/queue/${encodeURIComponent(QUEUE_NAME)}`
  + `?batch=${POP_BATCH}&wait=true&timeout=${POP_WAIT_MS}&autoAck=true`;

const instance = autocannon({
  url,
  method: 'GET',
  connections: CONNECTIONS,
  duration: DURATION,
  workers: WORKERS,
  pipelining: 1,
  // Autocannon default per-request timeout is 10s; long-polls can legitimately
  // take up to POP_WAIT_MS + network, so give it headroom.
  timeout: Math.ceil((POP_WAIT_MS + 2000) / 1000),
  headers: { 'Content-Type': 'application/json' },
});

instance.on('done', (results) => {
  const summary = {
    phase: 'measure',
    mode: 'wildcard-longpoll',
    config: {
      workers: WORKERS,
      connections: CONNECTIONS,
      duration: DURATION,
      pop_batch: POP_BATCH,
      pop_wait_ms: POP_WAIT_MS,
    },
    url,
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
  console.log(`[consumer] ${results.requests.total} req in ${results.duration}s, p50=${results.latency.p50}ms p99=${results.latency.p99}ms, errors=${results.errors}, non2xx=${results.non2xx}`);
});

autocannon.track(instance, { renderProgressBar: false });
