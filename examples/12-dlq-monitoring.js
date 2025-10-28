/**
 * Example 12: Dead Letter Queue Monitoring
 * 
 * This example demonstrates:
 * - Handling failed messages
 * - Messages moving to DLQ after max retries
 * - Querying the DLQ
 * - Monitoring and alerting on failures
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'risky-operations'

console.log('Creating queue with DLQ enabled...')
await queen
  .queue(queueName)
  .config({
    retryLimit: 2,              // Retry only 2 times
    dlqAfterMaxRetries: true    // Move to DLQ after retries exhausted
  })
  .create()

// Push some messages (some will fail)
console.log('\nPushing 10 messages...')
for (let i = 1; i <= 10; i++) {
  await queen.queue(queueName).push([{
    data: {
      id: i,
      value: i <= 5 ? i * 10 : -i,  // Negative values will fail
      shouldFail: i > 5
    }
  }])
}
console.log('Pushed 10 messages (5 will succeed, 5 will fail)')

// Process messages (simulating failures)
console.log('\n--- Processing Messages ---')

let successCount = 0
let failureCount = 0
let attemptCounts = new Map()

await queen
  .queue(queueName)
  .limit(50)  // Allow retries (up to 50 total attempts)
  .consume(async (message) => {
    const attemptKey = message.data.id
    const attempts = (attemptCounts.get(attemptKey) || 0) + 1
    attemptCounts.set(attemptKey, attempts)
    
    console.log(`Processing message ${message.data.id} (attempt ${attempts})`)
    
    // Simulate processing failure for negative values
    if (message.data.value < 0) {
      failureCount++
      console.log(`  ❌ Failed: Negative value ${message.data.value}`)
      throw new Error('Negative values not allowed')
    }
    
    successCount++
    console.log(`  ✅ Success: Processed value ${message.data.value}`)
  })
  .onError(async (message, error) => {
    console.log(`  ⚠️  Error handler: ${error.message}`)
  })

console.log('\n--- Processing Complete ---')
console.log(`Successful: ${successCount}`)
console.log(`Failed: ${failureCount}`)

// Wait a moment for DLQ processing
await new Promise(resolve => setTimeout(resolve, 1000))

// Query the DLQ
console.log('\n--- Checking Dead Letter Queue ---')
const dlqResult = await queen
  .queue(queueName)
  .dlq()
  .limit(100)
  .get()

console.log(`Found ${dlqResult.total} messages in DLQ\n`)

if (dlqResult.messages && dlqResult.messages.length > 0) {
  console.log('DLQ Messages:')
  dlqResult.messages.forEach((msg, idx) => {
    console.log(`\n${idx + 1}. Message ID: ${msg.data.id}`)
    console.log(`   Value: ${msg.data.value}`)
    console.log(`   Error: ${msg.errorMessage}`)
    console.log(`   Retry Count: ${msg.retryCount}`)
    console.log(`   Failed At: ${new Date(msg.dlqTimestamp).toISOString()}`)
  })
}

// Demonstrate DLQ monitoring patterns
console.log('\n--- DLQ Monitoring Patterns ---')

// Pattern 1: Alert on DLQ threshold
const dlqThreshold = 3
if (dlqResult.total > dlqThreshold) {
  console.log(`\n🚨 ALERT: DLQ has ${dlqResult.total} messages (threshold: ${dlqThreshold})`)
  console.log('   Action: Investigate failing messages')
}

// Pattern 2: Categorize errors
const errorTypes = {}
dlqResult.messages.forEach(msg => {
  const errorType = msg.errorMessage || 'Unknown'
  errorTypes[errorType] = (errorTypes[errorType] || 0) + 1
})

console.log('\n📊 Error Distribution:')
for (const [error, count] of Object.entries(errorTypes)) {
  console.log(`   ${error}: ${count} occurrences`)
}

// Pattern 3: Time-based DLQ query
console.log('\n--- Time-Based DLQ Query ---')
const oneMinuteAgo = new Date(Date.now() - 60000).toISOString()
const recentDlq = await queen
  .queue(queueName)
  .dlq()
  .from(oneMinuteAgo)
  .limit(100)
  .get()

console.log(`Messages that failed in the last minute: ${recentDlq.total}`)

// Pattern 4: Consumer group specific DLQ
console.log('\n--- Consumer Group Specific DLQ ---')

// Create a message that will fail for a specific consumer group
await queen.queue(queueName).push([{
  data: { id: 999, value: -999, group: 'specific-group' }
}])

// Process with a consumer group
await queen
  .queue(queueName)
  .group('specific-group')
  .limit(10)
  .consume(async (message) => {
    if (message.data.value < 0) {
      throw new Error('Group-specific failure')
    }
  })

// Wait for DLQ
await new Promise(resolve => setTimeout(resolve, 1000))

// Query DLQ for specific group
const groupDlq = await queen
  .queue(queueName)
  .dlq('specific-group')
  .limit(10)
  .get()

console.log(`DLQ messages for group 'specific-group': ${groupDlq.total}`)

// Best practices summary
console.log('\n--- DLQ Best Practices ---')
console.log('1. ✅ Monitor DLQ size regularly')
console.log('2. ✅ Set up alerts for DLQ threshold')
console.log('3. ✅ Categorize and track error types')
console.log('4. ✅ Use time-based queries for trend analysis')
console.log('5. ✅ Investigate and fix root causes')
console.log('6. ✅ Consider reprocessing or manual intervention for DLQ messages')

// Cleanup
await queen.close()
console.log('\n✅ Done!')
