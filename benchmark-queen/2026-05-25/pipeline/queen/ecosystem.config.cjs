// pm2 ecosystem for the Queen pipeline benchmark.
//
// Defaults tuned after the 2026-04-27 profiling exercise that showed Queen's
// pop claims one partition per call (advisory lock) so per-partition depth
// drives effective batch size. With 10 000 partitions and random producer
// spread, batches collapsed to 1–10 messages and aggregate worker
// throughput capped at ~150 msg/s. Reducing to 1 000 partitions and
// processing batches of 100 in one shot (no .each()) restores normal
// throughput.
//
//   2 producers      pushing to q1 (1000 partitions), each ~5 k msg/s
//                    single-message-per-request, 40 in-flight per producer
//   3 workers        consuming q1 group="workers", batch=100, push q2 + ack
//   3 analytics      consuming q2 group="analytics", batch=100
//   3 log            consuming q2 group="log",       batch=100
//
// Total: 11 Node processes. Each writes its per-message JSONL into
// $RESULTS_DIR (default /tmp/queen-pipeline/), which the runner aggregates
// after the run.

const SERVER_URL    = process.env.SERVER_URL    || 'http://localhost:6632'
const RESULTS_DIR   = process.env.RESULTS_DIR   || '/tmp/queen-pipeline'
const DURATION_SEC  = process.env.DURATION_SEC  || '1200' // 20 min

const NUM_PRODUCERS = parseInt(process.env.NUM_PRODUCERS || '2', 10)
const NUM_WORKERS   = parseInt(process.env.NUM_WORKERS   || '3', 10)
const NUM_ANALYTICS = parseInt(process.env.NUM_ANALYTICS || '3', 10)
const NUM_LOG       = parseInt(process.env.NUM_LOG       || '3', 10)

const Q1            = process.env.Q1 || 'pipe-q1'
const Q2            = process.env.Q2 || 'pipe-q2'
const NUM_PARTITIONS = process.env.NUM_PARTITIONS || '1000'
const TARGET_RATE   = process.env.TARGET_RATE || '5000'  // per-producer rate
const IN_FLIGHT     = process.env.IN_FLIGHT   || '40'
const CONCURRENCY   = process.env.CONCURRENCY || '10'    // per-consumer concurrency
const BATCH_SIZE    = process.env.BATCH_SIZE  || '100'

const baseEnv = {
  SERVER_URL,
  RESULTS_DIR,
  DURATION_SEC,
  NUM_PARTITIONS,
}

const producers = Array.from({ length: NUM_PRODUCERS }, (_, i) => ({
  name: `prod-${i + 1}`,
  script: 'producer.js',
  exec_mode: 'fork',
  autorestart: false,
  env: {
    ...baseEnv,
    INSTANCE: String(i + 1),
    QUEUE_NAME: Q1,
    TARGET_RATE,
    IN_FLIGHT,
  },
}))

const workers = Array.from({ length: NUM_WORKERS }, (_, i) => ({
  name: `worker-${i + 1}`,
  script: 'worker.js',
  exec_mode: 'fork',
  autorestart: false,
  env: {
    ...baseEnv,
    INSTANCE: String(i + 1),
    SOURCE_QUEUE: Q1,
    TARGET_QUEUE: Q2,
    GROUP: 'workers',
    CONCURRENCY,
    BATCH_SIZE,
  },
}))

const analytics = Array.from({ length: NUM_ANALYTICS }, (_, i) => ({
  name: `analytics-${i + 1}`,
  script: 'consumer.js',
  exec_mode: 'fork',
  autorestart: false,
  env: {
    ...baseEnv,
    INSTANCE: String(i + 1),
    QUEUE_NAME: Q2,
    GROUP: 'analytics',
    CONCURRENCY,
    BATCH_SIZE,
  },
}))

const log = Array.from({ length: NUM_LOG }, (_, i) => ({
  name: `log-${i + 1}`,
  script: 'consumer.js',
  exec_mode: 'fork',
  autorestart: false,
  env: {
    ...baseEnv,
    INSTANCE: String(i + 1),
    QUEUE_NAME: Q2,
    GROUP: 'log',
    CONCURRENCY,
    BATCH_SIZE,
  },
}))

module.exports = {
  apps: [...producers, ...workers, ...analytics, ...log],
}
