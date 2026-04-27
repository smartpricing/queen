// Tail-stage consumer for the Queen pipeline benchmark (analytics OR log).
//
// Reads from q2 with a configurable consumer group ('analytics' or 'log'),
// simulates per-message work with the same long-tail distribution as the
// worker, then batch-acks. Each processed message produces a JSONL record
// with the three timestamps needed to compute end-to-end latency:
//
//   producerPushedAt → workerProcessedAt → consumerProcessedAt (end_t)
//
// Batch mode (no .each()): the handler runs once per popped batch. The
// per-message sleep simulations run in parallel via Promise.all so the
// per-batch wallclock is approximately max(work) instead of the sum.

import { Queen } from 'queen-mq'
import { JsonlLogger, CounterLogger, longTailDelayMs, sleep } from './lib/common.js'

const SERVER_URL    = process.env.SERVER_URL    || 'http://localhost:6632'
const QUEUE_NAME    = process.env.QUEUE_NAME    || 'pipe-q2'
const GROUP         = process.env.GROUP         || 'analytics'   // 'analytics' or 'log'
const CONCURRENCY   = parseInt(process.env.CONCURRENCY  || '10', 10)
const BATCH_SIZE    = parseInt(process.env.BATCH_SIZE   || '100', 10)
const DURATION_SEC  = parseInt(process.env.DURATION_SEC || '900', 10)
const INSTANCE      = process.env.INSTANCE      || '0'
const RESULTS_DIR   = process.env.RESULTS_DIR   || '/tmp/queen-pipeline'

const queen = new Queen(SERVER_URL)
const logger = new JsonlLogger(`${RESULTS_DIR}/${GROUP}-${INSTANCE}.jsonl`)
const counter = new CounterLogger(GROUP, INSTANCE, 5000)

const abortController = new AbortController()
let stopping = false

async function processBatch(messages) {
  // Per-message work simulation in parallel.
  const enriched = await Promise.all(messages.map(async (m) => {
    const work = longTailDelayMs()
    await sleep(work)
    const data = m?.data ?? m?.payload ?? {}
    return {
      txid: m.transactionId,
      partition: String(data.partition ?? ''),
      pushT: data.producerPushedAt,
      workerT: data.workerProcessedAt,
      workMs: Math.round(work),
    }
  }))
  return { endT: Date.now(), enriched }
}

async function ackSuccess(messages, result) {
  try {
    await queen.ack(messages, true, { group: GROUP })
  } catch (ackErr) {
    process.stderr.write(`[${GROUP}-${INSTANCE}] batch ack(true) failed: ${ackErr.message}\n`)
    return
  }
  counter.inc(messages.length)
  for (let i = 0; i < messages.length; i++) {
    const e = result.enriched[i]
    logger.write({
      role: GROUP,
      instance: INSTANCE,
      txid: e.txid,
      partition: e.partition,
      push_t: e.pushT,
      worker_t: e.workerT,
      end_t: result.endT,
      work_ms: e.workMs,
    })
  }
}

async function ackFailure(messages, err) {
  const size = Array.isArray(messages) ? messages.length : 1
  process.stderr.write(`[${GROUP}-${INSTANCE}] batch error (size=${size}): ${err.message}\n`)
  try {
    await queen.ack(messages, false, { group: GROUP })
  } catch (ackErr) {
    process.stderr.write(`[${GROUP}-${INSTANCE}] batch nack failed: ${ackErr.message}\n`)
  }
}

async function main() {
  process.stderr.write(
    `[${GROUP}-${INSTANCE}] starting · queue=${QUEUE_NAME} group=${GROUP} ` +
    `concurrency=${CONCURRENCY} batch=${BATCH_SIZE} dur=${DURATION_SEC}s\n`,
  )

  process.on('SIGTERM', () => { stopping = true; abortController.abort() })
  process.on('SIGINT',  () => { stopping = true; abortController.abort() })
  setTimeout(() => { stopping = true; abortController.abort() }, DURATION_SEC * 1000)

  try {
    await queen
      .queue(QUEUE_NAME)
      .group(GROUP)
      .concurrency(CONCURRENCY)
      .batch(BATCH_SIZE)
      .renewLease(true, 5000)
      .autoAck(false)
      .consume(processBatch, { signal: abortController.signal })
      .onSuccess(ackSuccess)
      .onError(ackFailure)
  } catch (err) {
    if (!stopping) {
      process.stderr.write(`[${GROUP}-${INSTANCE}] consume error: ${err.stack || err.message}\n`)
    }
  } finally {
    counter.stop()
    await logger.close()
    process.stderr.write(`[${GROUP}-${INSTANCE}] done · processed=${counter.count}\n`)
  }
}

main().catch((e) => {
  process.stderr.write(`[${GROUP}-${INSTANCE}] fatal: ${e.stack || e.message}\n`)
  process.exit(1)
})
