/**
 * Example 08: Consumer Groups
 * 
 * This example demonstrates:
 * - Multiple consumer groups processing the same messages
 * - Scaling within a group (load balancing)
 * - Fan-out pattern (each group gets all messages)
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'events'

console.log('Creating events queue...')
await queen.queue(queueName).create()

// Push some events
console.log('\nPushing 10 events...')
for (let i = 1; i <= 10; i++) {
  await queen.queue(queueName).push([
    { data: { eventId: i, type: 'user-action', timestamp: Date.now() } }
  ])
}
console.log('Pushed 10 events')

// Consumer Group 1: Analytics
// Multiple workers in the same group share the load
console.log('\n--- Starting Consumer Group: "analytics" ---')
console.log('(2 workers sharing the load)\n')

let analyticsCount1 = 0
let analyticsCount2 = 0

// Worker 1 in analytics group
const analytics1 = queen
  .queue(queueName)
  .group('analytics')
  .limit(10)
  .consume(async (message) => {
    analyticsCount1++
    console.log(`Analytics Worker 1: Processing event ${message.data.eventId}`)
  })

// Worker 2 in analytics group (same group, shares messages)
const analytics2 = queen
  .queue(queueName)
  .group('analytics')
  .limit(10)
  .consume(async (message) => {
    analyticsCount2++
    console.log(`Analytics Worker 2: Processing event ${message.data.eventId}`)
  })

await Promise.all([analytics1, analytics2])

console.log(`\nAnalytics group processed: Worker1=${analyticsCount1}, Worker2=${analyticsCount2}`)
console.log(`Total: ${analyticsCount1 + analyticsCount2} (messages shared between workers)`)

// Push more events for group 2
console.log('\nPushing 10 more events for email group...')
for (let i = 11; i <= 20; i++) {
  await queen.queue(queueName).push([
    { data: { eventId: i, type: 'user-action', timestamp: Date.now() } }
  ])
}

// Consumer Group 2: Email Notifications
// This group gets ALL the same messages (fan-out pattern)
console.log('\n--- Starting Consumer Group: "email-notifications" ---')
console.log('(This group gets the same messages as analytics)\n')

let emailCount = 0

await queen
  .queue(queueName)
  .group('email-notifications')
  .limit(10)
  .consume(async (message) => {
    emailCount++
    console.log(`Email Worker: Sending notification for event ${message.data.eventId}`)
  })

console.log(`\nEmail group processed: ${emailCount} messages`)

// Demonstrate partition-specific consumer groups
console.log('\n--- Consumer Groups with Partitions ---')
await queen.queue(queueName).partition('user-123').push([
  { data: { eventId: 100, userId: 'user-123', action: 'login' } }
])

await queen.queue(queueName).partition('user-456').push([
  { data: { eventId: 101, userId: 'user-456', action: 'logout' } }
])

await queen
  .queue(queueName)
  .partition('user-123')
  .group('user-specific-analytics')
  .limit(1)
  .consume(async (message) => {
    console.log(`Processing user-123 events:`, message.data)
  })

await queen
  .queue(queueName)
  .partition('user-456')
  .group('user-specific-analytics')
  .limit(1)
  .autoAck(false)
  .consume(async (message) => {
    console.log(`Processing user-456 events:`, message.data)
  })
  .onSuccess(async (message) => {
    console.log(`Successfully processed user-456 events:`, message.data)
    await queen.ack(message, true, { group: 'user-specific-analytics' })
  })
  .onError(async (message, error) => {
    console.error(`Error processing user-456 events:`, error)
    await queen.ack(message, false, { group: 'user-specific-analytics', error: 'some error' })
  })

// Cleanup
await queen.close()
console.log('\nDone!')
console.log('\nKey Takeaways:')
console.log('1. Workers in the SAME group share messages (load balancing)')
console.log('2. Different groups get ALL messages (fan-out)')
console.log('3. Groups can be combined with partitions for fine-grained control')
