/**
 * Example 03: Transactional Pipeline
 * 
 * This example demonstrates:
 * - Processing messages from one queue
 * - Atomically acking input and pushing to output queue
 * - Ensuring exactly-once processing with transactions
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const inputQueue = 'pipeline-input'
const outputQueue = 'pipeline-output'

console.log('Creating queues...')
await queen.queue(inputQueue).create()
await queen.queue(outputQueue).create()

// Push some input messages
console.log('Pushing input messages...')
const inputs = Array.from({ length: 10 }, (_, i) => ({
  data: { id: i + 1, value: i * 2 }
}))

await queen.queue(inputQueue).push(inputs)
console.log('Pushed 10 input messages')

// Process with transactions
console.log('\nProcessing with transactional pipeline...')

await queen
  .queue(inputQueue)
  .batch(1)
  .limit(10)
  .autoAck(false)  // Disable auto-ack for manual transaction control
  .consume(async (message) => {
    // Transform the message
    const processed = {
      originalId: message.data.id,
      transformed: message.data.value * 2,
      timestamp: Date.now()
    }
    
    console.log(`Processing message ${message.data.id}: ${message.data.value} -> ${processed.transformed}`)
    
    // Atomically: ack the input and push the output
    await queen
      .transaction()
      .ack(message)
      .queue(outputQueue)
      .push([{ data: processed }])
      .commit()
    
    console.log(`  Committed transaction for message ${message.data.id}`)
  })

// Verify outputs
console.log('\nVerifying output queue...')
const outputs = await queen
  .queue(outputQueue)
  .batch(10)
  .wait(false)
  .pop()

console.log(`Found ${outputs.length} messages in output queue`)
outputs.forEach(msg => {
  console.log(`  ID: ${msg.data.originalId}, Value: ${msg.data.transformed}`)
})

// Cleanup
await queen.ack(outputs, true)
await queen.close()
console.log('\nDone!')
