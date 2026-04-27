// One-shot pre-run script: reset and configure the two pipeline queues.
// Runs before the producers/workers/consumers start.

import { Queen } from 'queen-mq'

const SERVER_URL = process.env.SERVER_URL || 'http://localhost:6632'
const QUEUES = (process.env.QUEUES || 'pipe-q1,pipe-q2').split(',')

const queen = new Queen(SERVER_URL)

async function reset(name) {
  try {
    await queen.queue(name).delete()
    process.stderr.write(`[configure] deleted queue=${name}\n`)
  } catch (e) {
    // delete may fail if queue doesn't exist — ignore.
  }
  await queen.queue(name).config({
    leaseTime: 60,
    retryLimit: 3,
    retentionEnabled: true,
    retentionSeconds: 7200,
    completedRetentionSeconds: 1800,
  }).create()
  process.stderr.write(`[configure] created queue=${name}\n`)
}

async function main() {
  for (const name of QUEUES) {
    await reset(name)
  }
  process.stderr.write('[configure] done\n')
}

main().catch((e) => {
  process.stderr.write(`[configure] fatal: ${e.stack || e.message}\n`)
  process.exit(1)
})
