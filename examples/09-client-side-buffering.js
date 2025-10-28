/**
 * Example 09: Client-Side Buffering
 * 
 * This example demonstrates:
 * - Client-side message buffering for high throughput
 * - Automatic flushing based on count or time
 * - Manual flush control
 * - Buffer statistics
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'high-throughput'

console.log('Creating queue...')
await queen.queue(queueName).create()

// Example 1: Basic buffering
console.log('\n--- Example 1: Basic Buffering ---')
console.log('Pushing 500 messages with buffering (count=100, time=1000ms)...')

const startTime = Date.now()

for (let i = 1; i <= 500; i++) {
  await queen
    .queue(queueName)
    .buffer({ messageCount: 100, timeMillis: 1000 })
    .push([
      { data: { id: i, timestamp: Date.now() } }
    ])
  
  if (i % 100 === 0) {
    console.log(`  Buffered ${i} messages...`)
  }
}

// Flush remaining messages
await queen.flushAllBuffers()

const elapsed = Date.now() - startTime
console.log(`\nCompleted in ${elapsed}ms`)
console.log('(Without buffering, this would be much slower!)')

// Example 2: Time-based flushing
console.log('\n--- Example 2: Time-Based Flushing ---')
console.log('Pushing 50 messages slowly (will flush after 2 seconds)...')

for (let i = 1; i <= 50; i++) {
  await queen
    .queue(queueName)
    .buffer({ messageCount: 1000, timeMillis: 2000 })
    .push([
      { data: { id: i, batch: 2 } }
    ])
  
  // Slow push rate - will trigger time-based flush
  await new Promise(resolve => setTimeout(resolve, 50))
}

console.log('Waiting for time-based flush...')
await new Promise(resolve => setTimeout(resolve, 2500))
console.log('Flushed by timer!')

// Example 3: Buffer statistics
console.log('\n--- Example 3: Buffer Statistics ---')

// Add some buffered messages without flushing
for (let i = 1; i <= 25; i++) {
  await queen
    .queue(queueName)
    .buffer({ messageCount: 100, timeMillis: 10000 })
    .push([
      { data: { id: i, batch: 3 } }
    ])
}

// Check buffer stats
const stats = queen.getBufferStats()
console.log('Current buffer stats:')
for (const [queueAddress, stat] of Object.entries(stats)) {
  console.log(`  ${queueAddress}: ${stat.count} messages, ${stat.size} bytes`)
}

// Manual flush specific queue
console.log('\nManually flushing buffer...')
await queen.queue(queueName).flushBuffer()
console.log('Buffer flushed!')

// Verify stats are cleared
const statsAfter = queen.getBufferStats()
console.log('Buffer stats after flush:', Object.keys(statsAfter).length === 0 ? 'Empty' : statsAfter)

// Example 4: Multiple queues with different buffer settings
console.log('\n--- Example 4: Multiple Queues with Different Buffers ---')

await queen.queue('logs').create()
await queen.queue('metrics').create()
await queen.queue('events').create()

console.log('Pushing to multiple queues with different buffer settings...')

// Logs: Large buffer (1000 messages or 5 seconds)
for (let i = 1; i <= 100; i++) {
  await queen
    .queue('logs')
    .buffer({ messageCount: 1000, timeMillis: 5000 })
    .push([{ data: { level: 'info', message: `Log ${i}` } }])
}

// Metrics: Medium buffer (100 messages or 1 second)
for (let i = 1; i <= 100; i++) {
  await queen
    .queue('metrics')
    .buffer({ messageCount: 100, timeMillis: 1000 })
    .push([{ data: { cpu: Math.random() * 100 } }])
}

// Events: Small buffer (10 messages or 500ms)
for (let i = 1; i <= 100; i++) {
  await queen
    .queue('events')
    .buffer({ messageCount: 10, timeMillis: 500 })
    .push([{ data: { type: 'click', eventId: i } }])
}

console.log('Checking buffer stats across all queues...')
const multiStats = queen.getBufferStats()
for (const [queueAddress, stat] of Object.entries(multiStats)) {
  console.log(`  ${queueAddress}: ${stat.count} messages`)
}

// Flush all buffers
console.log('\nFlushing all buffers...')
await queen.flushAllBuffers()
console.log('All buffers flushed!')

// Cleanup
await queen.close()
console.log('\nDone!')
console.log('\nKey Takeaways:')
console.log('1. Buffering dramatically improves throughput for high-volume pushes')
console.log('2. Messages flush when count OR time threshold is reached')
console.log('3. Always flush buffers before shutdown to prevent message loss')
console.log('4. Use buffer stats to monitor buffering behavior')
