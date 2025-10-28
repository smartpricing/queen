/**
 * Example 02: Batch Operations
 * 
 * This example demonstrates:
 * - Pushing multiple messages at once
 * - Consuming messages in batches
 * - Processing batches efficiently
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'batch-example'

console.log('Creating queue...')
await queen.queue(queueName).create()

// Push a batch of messages
console.log('Pushing 20 messages...')
const messages = Array.from({ length: 20 }, (_, i) => ({
  data: {
    id: i + 1,
    value: Math.random() * 100
  }
}))

await queen.queue(queueName).push(messages)
console.log('Pushed 20 messages')

// Consume in batches of 5
console.log('\nConsuming in batches of 5...')
let batchCount = 0

await queen
  .queue(queueName)
  .batch(5)          // Get up to 5 messages at a time
  .limit(20)         // Process up to 20 messages total
  .consume(async (batch) => {
    batchCount++
    console.log(`Batch ${batchCount}: Received ${batch.length} messages`)
    
    // Process the batch
    const sum = batch.reduce((acc, msg) => acc + msg.data.value, 0)
    const avg = sum / batch.length
    
    console.log(`  Average value: ${avg.toFixed(2)}`)
    
    // Auto-acked on success!
  })

console.log(`\nProcessed ${batchCount} batches`)

// Cleanup
await queen.close()
console.log('Done!')
