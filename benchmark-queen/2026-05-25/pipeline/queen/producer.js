// Producer process for the Queen pipeline benchmark.
// Pushes ONE message per HTTP request (no batching — realistic microservice
// shape). Uses N concurrent in-flight requests to hit a target rate.
//
// Each message:
//   - has a unique transactionId (UUIDv7-like)
//   - has a producerPushedAt timestamp baked into the payload (epoch ms)
//   - is sent to a randomly-picked partition in [0, NUM_PARTITIONS)

import { Queen } from 'queen-mq'
import { v7 as uuidv7 } from 'uuid'
import { TokenBucket, makePayload, randomPartition, JsonlLogger, CounterLogger } from './lib/common.js'

const SERVER_URL    = process.env.SERVER_URL    || 'http://localhost:6632'
const QUEUE_NAME    = process.env.QUEUE_NAME    || 'pipe-q1'
const NUM_PARTITIONS = parseInt(process.env.NUM_PARTITIONS || '10000', 10)
const TARGET_RATE   = parseInt(process.env.TARGET_RATE   || '1000', 10) // msg/s per process
const IN_FLIGHT     = parseInt(process.env.IN_FLIGHT     || '30', 10)
const DURATION_SEC  = parseInt(process.env.DURATION_SEC  || '900', 10)
const INSTANCE      = process.env.INSTANCE      || '0'
const RESULTS_DIR   = process.env.RESULTS_DIR   || '/tmp/queen-pipeline'

const queen = new Queen(SERVER_URL)
const logger = new JsonlLogger(`${RESULTS_DIR}/producer-${INSTANCE}.jsonl`)
const counter = new CounterLogger('producer', INSTANCE, 5000)

const bucket = new TokenBucket(TARGET_RATE)
const endTime = Date.now() + DURATION_SEC * 1000
let stopping = false

async function pushOne() {
  await bucket.consume()
  if (stopping || Date.now() >= endTime) return false
  const txid = uuidv7()
  const partition = randomPartition(NUM_PARTITIONS)
  const pushedAt = Date.now()
  try {
    await queen
      .queue(QUEUE_NAME)
      .partition(partition)
      .push([
        {
          transactionId: txid,
          data: makePayload({ producerPushedAt: pushedAt, partition }),
        },
      ])
    counter.inc(1)
    logger.write({ role: 'producer', instance: INSTANCE, txid, partition, push_t: pushedAt })
    return true
  } catch (err) {
    process.stderr.write(`[producer-${INSTANCE}] push error: ${err.message}\n`)
    logger.write({ role: 'producer', instance: INSTANCE, txid, partition, error: err.message })
    return false
  }
}

async function loop() {
  while (!stopping && Date.now() < endTime) {
    await pushOne()
  }
}

async function main() {
  process.stderr.write(
    `[producer-${INSTANCE}] starting · queue=${QUEUE_NAME} parts=${NUM_PARTITIONS} target=${TARGET_RATE}/s in_flight=${IN_FLIGHT} dur=${DURATION_SEC}s\n`,
  )
  const slots = Array.from({ length: IN_FLIGHT }, () => loop())
  process.on('SIGTERM', () => {
    stopping = true
  })
  process.on('SIGINT', () => {
    stopping = true
  })
  await Promise.all(slots)
  counter.stop()
  await logger.close()
  process.stderr.write(`[producer-${INSTANCE}] done · total=${counter.count}\n`)
}

main().catch((e) => {
  process.stderr.write(`[producer-${INSTANCE}] fatal: ${e.stack || e.message}\n`)
  process.exit(1)
})
