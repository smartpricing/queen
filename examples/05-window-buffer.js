/**
 * Example 05: Window Buffer
 * 
 * This example demonstrates:
 * - Server-side window buffering
 * - Messages are held for a duration to create natural batches
 * - Improves batch processing efficiency
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'window-buffer-example'

console.log('Creating queue with 3 second window buffer...')
await queen
  .queue(queueName)
  .config({
    windowBuffer: 3  // Hold messages for 3 seconds to batch them
  })
  .create()

// Push messages rapidly
console.log('Pushing 15 messages rapidly...')
for (let i = 1; i <= 15; i++) {
  await queen.queue(queueName).push([
    { data: { id: i, timestamp: Date.now() } }
  ])
  
  if (i % 5 === 0) {
    console.log(`  Pushed ${i} messages...`)
  }
}

console.log('All messages pushed at', new Date().toISOString())

// Try to consume immediately (should get nothing due to window buffer)
console.log('\nTrying to consume immediately...')
const immediate = await queen.queue(queueName).batch(20).wait(false).pop()
console.log(`Received ${immediate.length} messages (should be 0 due to window buffer)`)

// Wait for window buffer to release
console.log('\nWaiting for 3 second window buffer...')
for (let i = 3; i > 0; i--) {
  process.stdout.write(`${i}... `)
  await new Promise(resolve => setTimeout(resolve, 1000))
}
console.log('\n')

// Now consume - should get all messages in one batch
console.log('Consuming at', new Date().toISOString())
const batched = await queen.queue(queueName).batch(20).wait(true).pop()
console.log(`Received ${batched.length} messages in one batch!`)

// Show that messages were buffered together
if (batched.length > 0) {
  const timestamps = batched.map(m => m.data.timestamp)
  const span = Math.max(...timestamps) - Math.min(...timestamps)
  console.log(`  Messages were pushed over ${span}ms but delivered together`)
}

// Cleanup
await queen.ack(batched, true)
await queen.close()
console.log('\nDone!')
