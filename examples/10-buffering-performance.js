/**
 * Example 10: Buffering Performance Comparison
 * 
 * This example demonstrates:
 * - Performance difference between buffered and unbuffered pushes
 * - Throughput measurements
 * - Best practices for high-volume scenarios
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'performance-test'
const messageCount = 1000

console.log('Creating queue...')
await queen.queue(queueName).create()
console.log(`\nPerformance Test: Pushing ${messageCount} messages\n`)

// Test 1: Without buffering (slower)
console.log('--- Test 1: WITHOUT Buffering ---')
const start1 = Date.now()

for (let i = 1; i <= messageCount; i++) {
  await queen.queue(queueName).push([
    { data: { id: i, value: Math.random() * 1000 } }
  ])
  
  if (i % 100 === 0) {
    process.stdout.write(`${i}... `)
  }
}

const elapsed1 = Date.now() - start1
const throughput1 = Math.round((messageCount / elapsed1) * 1000)

console.log(`\nCompleted in ${elapsed1}ms`)
console.log(`Throughput: ${throughput1} messages/second`)

// Clear the queue
console.log('\nClearing queue for next test...')
let cleared = 0
while (true) {
  const messages = await queen.queue(queueName).batch(100).wait(false).pop()
  if (messages.length === 0) break
  await queen.ack(messages, true)
  cleared += messages.length
}
console.log(`Cleared ${cleared} messages`)

// Test 2: With buffering (faster)
console.log('\n--- Test 2: WITH Buffering ---')
const start2 = Date.now()

for (let i = 1; i <= messageCount; i++) {
  await queen
    .queue(queueName)
    .buffer({ messageCount: 100, timeMillis: 1000 })
    .push([
      { data: { id: i, value: Math.random() * 1000 } }
    ])
  
  if (i % 100 === 0) {
    process.stdout.write(`${i}... `)
  }
}

await queen.flushAllBuffers()

const elapsed2 = Date.now() - start2
const throughput2 = Math.round((messageCount / elapsed2) * 1000)

console.log(`\nCompleted in ${elapsed2}ms`)
console.log(`Throughput: ${throughput2} messages/second`)

// Compare results
console.log('\n--- Performance Comparison ---')
console.log(`Without buffering: ${elapsed1}ms (${throughput1} msg/s)`)
console.log(`With buffering:    ${elapsed2}ms (${throughput2} msg/s)`)

const speedup = (elapsed1 / elapsed2).toFixed(2)
const improvement = Math.round(((elapsed1 - elapsed2) / elapsed1) * 100)

console.log(`\nSpeedup: ${speedup}x faster`)
console.log(`Improvement: ${improvement}% faster`)

// Best practices demonstration
console.log('\n--- Best Practices ---')

// 1. Adjust buffer size based on message rate
console.log('\n1. Adaptive buffer size based on message volume:')
console.log('   High volume (>100/sec): buffer 500-1000 messages')
console.log('   Medium volume (10-100/sec): buffer 50-100 messages')
console.log('   Low volume (<10/sec): buffer 10-20 messages')

// 2. Time-based flushing prevents delays
console.log('\n2. Always set a time limit for flushing:')
console.log('   High priority: 100-500ms')
console.log('   Medium priority: 1-2 seconds')
console.log('   Low priority: 5-10 seconds')

// 3. Batch pushes when possible
console.log('\n3. Batch multiple messages in a single push():')
const start3 = Date.now()

// Batch approach
const batch = []
for (let i = 1; i <= 1000; i++) {
  batch.push({ data: { id: i } })
  
  if (batch.length === 100) {
    await queen
      .queue(queueName)
      .buffer({ messageCount: 500, timeMillis: 1000 })
      .push(batch)
    batch.length = 0  // Clear array
  }
}
if (batch.length > 0) {
  await queen.queue(queueName).push(batch)
}
await queen.flushAllBuffers()

const elapsed3 = Date.now() - start3
const throughput3 = Math.round((1000 / elapsed3) * 1000)

console.log(`   Batched approach: ${elapsed3}ms (${throughput3} msg/s)`)
console.log(`   This is even faster than individual pushes with buffering!`)

// Cleanup
await queen.close()
console.log('\n✅ Done!')
