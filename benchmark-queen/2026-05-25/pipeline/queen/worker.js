// Worker process for the Queen pipeline benchmark — batch mode.
//
// Pattern (at-least-once):
//   1. handler runs ONCE per batch of up to BATCH_SIZE messages.
//      All messages in the batch share the same q1 partition because
//      Queen's pop claims one partition per call via advisory lock.
//   2. Inside the handler we Promise.all() the per-message work simulation
//      (sleep is non-blocking) and then forward the whole batch to q2 with
//      ONE push call (same partition).
//   3. onSuccess: queen.ack(messages, true, { group }) — the queen-mq
//      client routes arrays to /api/v1/ack/batch automatically.
//   4. onError: queen.ack(messages, false, { group }) — release the lease
//      so the partition's batch is re-delivered without waiting for the
//      60 s lease timeout.
//
// Failure modes (at-least-once, batch):
//   - If the q2 push throws, the entire batch is nacked and re-delivered.
//     Because the push is a single transactional call on the server, no
//     partial duplicates land in q2.
//   - If the q2 push succeeds but ack(q1) fails, the batch is re-delivered
//     after the lease expires; downstream q2 sees duplicates for the entire
//     batch. Acceptable under at-least-once.
//   - Per-message work runs in parallel with Promise.all so per-batch
//     wallclock ≈ max(longTailDelayMs across batch) instead of the sum.

import { Queen } from 'queen-mq'
import { JsonlLogger, CounterLogger, longTailDelayMs, sleep, makePayload } from './lib/common.js'

const SERVER_URL          = process.env.SERVER_URL    || 'http://localhost:6632'
const SOURCE_QUEUE        = process.env.SOURCE_QUEUE  || 'pipe-q1'
const TARGET_QUEUE        = process.env.TARGET_QUEUE  || 'pipe-q2'
const GROUP               = process.env.GROUP         || 'workers'
const CONCURRENCY         = parseInt(process.env.CONCURRENCY  || '10', 10)
const BATCH_SIZE          = parseInt(process.env.BATCH_SIZE   || '100', 10)
// v4 multi-partition pop: each pop call drains up to MAX_PARTITIONS_PER_POP
// partitions in a single round-trip, sharing one global BATCH_SIZE budget.
// 1 = legacy v3 single-partition behaviour.
const MAX_PARTITIONS_PER_POP = parseInt(process.env.MAX_PARTITIONS_PER_POP || '1', 10)
const DURATION_SEC        = parseInt(process.env.DURATION_SEC || '900', 10)
const INSTANCE            = process.env.INSTANCE      || '0'
const RESULTS_DIR         = process.env.RESULTS_DIR   || '/tmp/queen-pipeline'

const queen = new Queen(SERVER_URL)
const logger = new JsonlLogger(`${RESULTS_DIR}/worker-${INSTANCE}.jsonl`)
const counter = new CounterLogger('worker', INSTANCE, 5000)

const PROFILE = process.env.QMQ_PROFILE === '1'
const stats = { workMs: 0, pushMs: 0, ackMs: 0, msgs: 0, batches: 0, batchSizeSum: 0 }
let statsEpoch = process.hrtime.bigint()
if (PROFILE) {
  setInterval(() => {
    if (stats.batches === 0) return
    const elapsedMs = Number(process.hrtime.bigint() - statsEpoch) / 1e6
    process.stdout.write(
      `PROF wk worker=${INSTANCE} batches=${stats.batches} ` +
      `avg_batch=${(stats.batchSizeSum/stats.batches).toFixed(1)} ` +
      `msgs=${stats.msgs} ` +
      `work=${(stats.workMs/stats.batches).toFixed(1)}ms/batch ` +
      `push=${(stats.pushMs/stats.batches).toFixed(1)}ms/batch ` +
      `ack=${(stats.ackMs/stats.batches).toFixed(1)}ms/batch ` +
      `wallclock=${elapsedMs.toFixed(0)}ms ` +
      `rate=${(stats.msgs/(elapsedMs/1000)).toFixed(0)}/s\n`)
    stats.workMs = stats.pushMs = stats.ackMs = 0
    stats.msgs = stats.batches = stats.batchSizeSum = 0
    statsEpoch = process.hrtime.bigint()
  }, 5000).unref()
}

const abortController = new AbortController()
let stopping = false

// Process an entire batch of messages popped from q1.
// Returns metadata that onSuccess uses to write per-message log rows.
//
// With v4 multi-partition pop (`.partitions(N>1)`), a single batch may span
// up to N q1 partitions. We group results by partition before forwarding to
// q2 so that:
//   1. Per-partition ordering is preserved end-to-end (same partition → same
//      q2 partition).
//   2. Each push call is single-partition (queen-mq's QueueBuilder stamps
//      one partition onto every item it sends).
//   3. The push calls run in parallel via Promise.all so the wallclock cost
//      is one push round-trip, not N.
async function processBatch(messages) {
  const tWork0 = process.hrtime.bigint()
  // Run all per-message work simulations in parallel (sleep is non-blocking)
  const enriched = await Promise.all(messages.map(async (m) => {
    const work = longTailDelayMs()
    await sleep(work)
    const data = m?.data ?? m?.payload ?? {}
    return {
      transactionId: m.transactionId,
      partition: String(data.partition ?? '0'),
      pushT: data.producerPushedAt,
      workMs: Math.round(work),
      data: makePayload({
        producerPushedAt: data.producerPushedAt,
        workerProcessedAt: Date.now(),
        partition: data.partition,
      }),
    }
  }))
  const tWork1 = process.hrtime.bigint()

  // Group by q1 partition so we can issue one push call per partition.
  const byPartition = new Map()
  for (const e of enriched) {
    let bucket = byPartition.get(e.partition)
    if (!bucket) { bucket = []; byPartition.set(e.partition, bucket) }
    bucket.push({ transactionId: e.transactionId, data: e.data })
  }

  const tPush0 = process.hrtime.bigint()
  await Promise.all(
    [...byPartition.entries()].map(([partition, items]) =>
      queen.queue(TARGET_QUEUE).partition(partition).push(items),
    ),
  )
  const tPush1 = process.hrtime.bigint()

  if (PROFILE) {
    stats.workMs += Number(tWork1 - tWork0) / 1e6
    stats.pushMs += Number(tPush1 - tPush0) / 1e6
  }

  return { workerT: Date.now(), enriched }
}

async function ackSuccess(messages, result) {
  const tAck0 = process.hrtime.bigint()
  try {
    await queen.ack(messages, true, { group: GROUP })
  } catch (ackErr) {
    process.stderr.write(`[worker-${INSTANCE}] batch ack(true) failed: ${ackErr.message}\n`)
    return
  }
  const tAck1 = process.hrtime.bigint()

  if (PROFILE) {
    stats.ackMs += Number(tAck1 - tAck0) / 1e6
    stats.msgs += messages.length
    stats.batches++
    stats.batchSizeSum += messages.length
  }

  counter.inc(messages.length)
  // Per-message JSONL log lines for traceability + later analysis.
  for (let i = 0; i < messages.length; i++) {
    const m = messages[i]
    const e = result.enriched[i]
    logger.write({
      role: 'worker', instance: INSTANCE, txid: m.transactionId,
      partition: e.partition, push_t: e.pushT,
      worker_t: result.workerT, work_ms: e.workMs,
    })
  }
}

async function ackFailure(messages, err) {
  const size = Array.isArray(messages) ? messages.length : 1
  process.stderr.write(`[worker-${INSTANCE}] batch error (size=${size}): ${err.message}\n`)
  try {
    await queen.ack(messages, false, { group: GROUP })
  } catch (ackErr) {
    process.stderr.write(`[worker-${INSTANCE}] batch nack failed: ${ackErr.message}\n`)
  }
}

async function main() {
  process.stderr.write(
    `[worker-${INSTANCE}] starting · ${SOURCE_QUEUE} → ${TARGET_QUEUE} · ` +
    `group=${GROUP} concurrency=${CONCURRENCY} batch=${BATCH_SIZE} ` +
    `partitions/pop=${MAX_PARTITIONS_PER_POP} dur=${DURATION_SEC}s\n`,
  )

  process.on('SIGTERM', () => { stopping = true; abortController.abort() })
  process.on('SIGINT',  () => { stopping = true; abortController.abort() })
  setTimeout(() => { stopping = true; abortController.abort() }, DURATION_SEC * 1000)

  try {
    await queen
      .queue(SOURCE_QUEUE)
      .group(GROUP)
      .concurrency(CONCURRENCY)
      .batch(BATCH_SIZE)
      .partitions(MAX_PARTITIONS_PER_POP)
      .renewLease(true, 5000)
      .autoAck(false)
      .consume(processBatch, { signal: abortController.signal })
      .onSuccess(ackSuccess)
      .onError(ackFailure)
  } catch (err) {
    if (!stopping) {
      process.stderr.write(`[worker-${INSTANCE}] consume error: ${err.stack || err.message}\n`)
    }
  } finally {
    counter.stop()
    await logger.close()
    process.stderr.write(`[worker-${INSTANCE}] done · processed=${counter.count}\n`)
  }
}

main().catch((e) => {
  process.stderr.write(`[worker-${INSTANCE}] fatal: ${e.stack || e.message}\n`)
  process.exit(1)
})
