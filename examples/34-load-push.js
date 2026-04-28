/**
 * Example 08: Consumer Groups
 * 
 * This example demonstrates:
 * - Multiple consumer groups processing the same messages
 * - Scaling within a group (load balancing)
 * - Fan-out pattern (each group gets all messages)
 */

import { Queen } from '../clients/client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'load'

console.log('Creating events queue...')
await queen.queue(queueName).delete()
await queen.queue(queueName).create()

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms))
}
async function pushMessages() {
  while (true) {
    await queen.queue(queueName)
    .partition((Math.floor(Math.random() * 1000)).toString())
    .push([
      { data: { type: 'user-action', timestamp: Date.now() } }
    ])
  }
}

const slots = Array.from({ length: 100 }, () => pushMessages())

