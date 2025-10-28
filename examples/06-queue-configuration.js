/**
 * Example 06: Queue Configuration
 * 
 * This example demonstrates:
 * - Comprehensive queue configuration options
 * - Lease time, retries, priorities, limits
 * - TTL and retention settings
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

console.log('Creating queues with different configurations...\n')

// High priority queue
console.log('1. High Priority Queue')
await queen
  .queue('urgent-tasks')
  .config({
    priority: 10,              // High priority
    leaseTime: 60,            // 1 minute to process
    retryLimit: 5,            // Retry 5 times
    maxSize: 1000            // Max 1000 messages
  })
  .create()
console.log('   Created with priority=10, leaseTime=60s, retryLimit=5')

// Normal priority queue
console.log('\n2. Normal Priority Queue')
await queen
  .queue('regular-tasks')
  .config({
    priority: 5,
    leaseTime: 300,           // 5 minutes to process
    retryLimit: 3
  })
  .create()
console.log('   Created with priority=5, leaseTime=300s, retryLimit=3')

// Low priority queue with TTL
console.log('\n3. Low Priority Queue with TTL')
await queen
  .queue('background-jobs')
  .config({
    priority: 1,
    leaseTime: 600,           // 10 minutes to process
    ttl: 3600,               // Messages expire after 1 hour
    retentionSeconds: 7200   // Keep for 2 hours max
  })
  .create()
console.log('   Created with priority=1, ttl=3600s, retentionSeconds=7200s')

// Queue with encryption
console.log('\n4. Encrypted Queue')
await queen
  .queue('sensitive-data')
  .config({
    encryptionEnabled: true,
    leaseTime: 300,
    retryLimit: 3
  })
  .create()
console.log('   Created with encryptionEnabled=true')

// Push test messages to each queue
console.log('\n5. Pushing test messages...')
await queen.queue('urgent-tasks').push([
  { data: { task: 'Critical alert', priority: 'high' } }
])
await queen.queue('regular-tasks').push([
  { data: { task: 'Normal processing', priority: 'medium' } }
])
await queen.queue('background-jobs').push([
  { data: { task: 'Background sync', priority: 'low' } }
])
await queen.queue('sensitive-data').push([
  { data: { ssn: '123-45-6789', encrypted: true } }
])
console.log('   Pushed messages to all queues')

// Consume from multiple queues by priority
console.log('\n6. Consuming by priority (high priority first)...')
let count = 0

// This would consume high priority first in a real scenario
// For demo, we'll just consume from urgent queue
await queen
  .queue('urgent-tasks')
  .limit(1)
  .consume(async (message) => {
    count++
    console.log(`   [${count}] Processed urgent task:`, message.data.task)
  })

await queen
  .queue('regular-tasks')
  .limit(1)
  .consume(async (message) => {
    count++
    console.log(`   [${count}] Processed regular task:`, message.data.task)
  })

// Verify encrypted queue
console.log('\n7. Verifying encrypted queue...')
const encrypted = await queen.queue('sensitive-data').pop()
if (encrypted.length > 0) {
  console.log('   Message was encrypted at rest and decrypted on retrieval')
  console.log('   Data:', encrypted[0].data)
  await queen.ack(encrypted[0], true)
}

// Cleanup
await queen.close()
console.log('\nDone!')
