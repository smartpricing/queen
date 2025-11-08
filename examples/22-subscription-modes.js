import { Queen } from '../client-js/client-v2/index.js' 

const queen = new Queen('http://localhost:6632')

const queueName = 'test-subscription-modes'

// Create queue
await queen.queue(queueName).create()

// Push some historical messages
console.log('Pushing 5 historical messages...')
for (let i = 1; i <= 5; i++) {
  await queen.queue(queueName).push([{ data: { id: i, type: 'historical' } }])
}

console.log('Historical messages pushed\n')

// Wait a bit to ensure messages are stored
await new Promise(resolve => setTimeout(resolve, 500))

// =============================================================================
// Example 1: Default behavior (process ALL messages)
// =============================================================================
console.log('=== Example 1: Default behavior (all messages) ===')
let allCount = 0
await queen.queue(queueName)
  .group('example1-all-messages')
  .limit(5)
  .each()
  .consume(async (message) => {
    allCount++
    console.log(`  Received: ${message.data.type} message ${message.data.id}`)
  })

console.log(`Total received: ${allCount} (expected: 5)\n`)

// =============================================================================
// Example 2: subscriptionMode('new') - skip historical messages
// =============================================================================
console.log('=== Example 2: subscriptionMode("new") ===')

// This consumer starts AFTER historical messages exist
let newOnlyCount = 0
const consumePromise = queen.queue(queueName)
  .group('example2-new-only')
  .subscriptionMode('new')  // Skip historical messages
  .limit(2)
  .each()
  .consume(async (message) => {
    newOnlyCount++
    console.log(`  Received: ${message.data.type} message ${message.data.id}`)
  })

// Wait a bit to ensure subscription is registered
await new Promise(resolve => setTimeout(resolve, 500))

// Now push NEW messages AFTER subscription
console.log('  Pushing 2 new messages...')
await queen.queue(queueName).push([
  { data: { id: 6, type: 'new' } },
  { data: { id: 7, type: 'new' } }
])

await consumePromise
console.log(`Total received: ${newOnlyCount} (expected: 2, skipped 5 historical)\n`)

// =============================================================================
// Example 3: subscriptionFrom(timestamp)
// =============================================================================
console.log('=== Example 3: subscriptionFrom(timestamp) ===')

// Get current time for reference point
const now = new Date().toISOString()

// Push messages before the timestamp
await queen.queue(queueName).push([
  { data: { id: 8, type: 'before-timestamp' } }
])

await new Promise(resolve => setTimeout(resolve, 100))

// Save timestamp
const startTime = new Date().toISOString()

await new Promise(resolve => setTimeout(resolve, 100))

// Push messages after the timestamp
await queen.queue(queueName).push([
  { data: { id: 9, type: 'after-timestamp' } },
  { data: { id: 10, type: 'after-timestamp' } }
])

// Consumer starts from specific timestamp
let timestampCount = 0
await queen.queue(queueName)
  .group('example3-from-timestamp')
  .subscriptionFrom(startTime)  // Start from this timestamp
  .limit(2)
  .each()
  .consume(async (message) => {
    timestampCount++
    console.log(`  Received: ${message.data.type} message ${message.data.id}`)
  })

console.log(`Total received: ${timestampCount} (expected: 2 after timestamp)\n`)

// =============================================================================
// Example 4: Override server default
// =============================================================================
console.log('=== Example 4: Explicit override ===')
console.log('  Note: If server has DEFAULT_SUBSCRIPTION_MODE="new",')
console.log('  you can still force processing all messages:\n')

console.log('  .group("force-all").subscriptionMode("all")')
console.log('  // Will process ALL messages even if server default is "new"\n')

// =============================================================================
// Server Configuration
// =============================================================================
console.log('=== Server Configuration ===')
console.log('To change the default behavior for ALL new consumer groups:')
console.log('')
console.log('  export DEFAULT_SUBSCRIPTION_MODE="new"')
console.log('  ./bin/queen-server')
console.log('')
console.log('Valid values:')
console.log('  ""          - Process all messages (default, backward compatible)')
console.log('  "new"       - Skip historical messages')
console.log('  "new-only"  - Same as "new"')
console.log('')
console.log('Client explicit settings always override server default.')

await queen.close()
console.log('\n✅ Examples complete!')

