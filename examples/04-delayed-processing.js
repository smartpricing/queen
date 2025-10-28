/**
 * Example 04: Delayed Processing
 * 
 * This example demonstrates:
 * - Configuring a queue with delayed processing
 * - Messages become available after a delay
 * - Useful for scheduled tasks, rate limiting, etc.
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'delayed-example'

console.log('Creating queue with 5 second delay...')
await queen
  .queue(queueName)
  .config({
    delayedProcessing: 5  // Messages invisible for 5 seconds
  })
  .create()

// Push messages
console.log('Pushing messages at', new Date().toISOString())
await queen.queue(queueName).push([
  { data: { task: 'Send reminder email', delay: 5 } },
  { data: { task: 'Update cache', delay: 5 } },
  { data: { task: 'Sync data', delay: 5 } }
])

// Try to pop immediately (should get nothing)
console.log('\nTrying to pop immediately...')
const immediate = await queen.queue(queueName).batch(10).wait(false).pop()
console.log(`Received ${immediate.length} messages (should be 0)`)

// Wait for the delay
console.log('\nWaiting 5 seconds for messages to become available...')
for (let i = 5; i > 0; i--) {
  process.stdout.write(`${i}... `)
  await new Promise(resolve => setTimeout(resolve, 1000))
}
console.log('\n')

// Now pop again
console.log('Popping at', new Date().toISOString())
const delayed = await queen.queue(queueName).batch(10).wait(true).pop()
console.log(`Received ${delayed.length} messages`)

delayed.forEach(msg => {
  console.log(`  Task: ${msg.data.task}`)
})

// Cleanup
await queen.ack(delayed, true)
await queen.close()
console.log('\nDone!')
