// One-shot partition warmer.
//
// Queen creates partitions lazily on first push. When 100s of producer slots
// race to push to fresh partitions in the same minute bucket, the
// `trg_partitions_created_counter` trigger contends on the
// `queue_lag_metrics` ON CONFLICT row (one row per (minute, queue)) and
// produces a deadlock burst that's absorbed by the file-buffer failover
// but adds noise to the first ~30 s of every benchmark run.
//
// Workaround for the benchmark: serially push one tiny "_warmup" message
// to each (queue, partition) pair BEFORE the load starts. After this all
// partitions already exist, the trigger fires with COUNT(*)=0 (since no
// new partitions are inserted), and the real benchmark starts deadlock-free.
//
// The warmup messages are flagged with `data._warmup = true` so analyze.js
// can filter them out of the latency / completeness analysis.

import { Queen } from 'queen-mq'

const SERVER_URL     = process.env.SERVER_URL     || 'http://localhost:6632'
const QUEUES         = (process.env.QUEUES        || 'pipe-q1,pipe-q2').split(',')
const NUM_PARTITIONS = parseInt(process.env.NUM_PARTITIONS || '1000', 10)

const queen = new Queen(SERVER_URL)

async function warmQueue(name) {
  process.stderr.write(`[warmup] ${name}: pushing 1 msg to each of ${NUM_PARTITIONS} partitions\n`)
  const t0 = Date.now()
  // Sequential pushes — slow on purpose so the trigger fires under
  // single-row-update contention rather than ON CONFLICT race.
  for (let p = 0; p < NUM_PARTITIONS; p++) {
    await queen
      .queue(name)
      .partition(String(p))
      .push([{ data: { _warmup: true, partition: p } }])
    if ((p + 1) % 200 === 0) {
      process.stderr.write(`[warmup] ${name}: ${p + 1}/${NUM_PARTITIONS}\n`)
    }
  }
  process.stderr.write(`[warmup] ${name}: done in ${Date.now() - t0} ms\n`)
}

async function main() {
  for (const q of QUEUES) {
    await warmQueue(q)
  }
  process.stderr.write('[warmup] all queues warmed\n')
}

main()
  .then(() => process.exit(0))
  .catch((e) => {
    process.stderr.write(`[warmup] fatal: ${e.stack || e.message}\n`)
    process.exit(1)
  })
