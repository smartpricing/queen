/**
 * Example 11: Event Streaming Pattern
 * 
 * This example demonstrates:
 * - High-throughput event streaming
 * - Multiple consumers processing the same events
 * - Real-time event processing with auto-ack
 * - Combining buffering with consumer groups
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const eventQueue = 'event-stream'

console.log('Creating event stream queue...')
await queen.queue(eventQueue).create()

// Simulate event producers
console.log('\n--- Simulating Event Producers ---')

async function produceUserEvents() {
  console.log('User events producer started...')
  for (let i = 1; i <= 20; i++) {
    await queen
      .queue(eventQueue)
      .buffer({ messageCount: 10, timeMillis: 500 })
      .push([{
        data: {
          type: 'user-event',
          action: ['login', 'logout', 'click', 'view'][i % 4],
          userId: `user-${i % 5}`,
          timestamp: Date.now()
        }
      }])
  }
  await queen.flushAllBuffers()
  console.log('User events producer finished')
}

async function produceSystemEvents() {
  console.log('System events producer started...')
  for (let i = 1; i <= 15; i++) {
    await queen
      .queue(eventQueue)
      .buffer({ messageCount: 10, timeMillis: 500 })
      .push([{
        data: {
          type: 'system-event',
          metric: 'cpu-usage',
          value: Math.random() * 100,
          timestamp: Date.now()
        }
      }])
  }
  await queen.flushAllBuffers()
  console.log('System events producer finished')
}

// Produce events concurrently
await Promise.all([
  produceUserEvents(),
  produceSystemEvents()
])

console.log('\n--- Starting Event Consumers ---')

// Consumer 1: Real-time dashboard updates
let dashboardCount = 0
const dashboardConsumer = queen
  .queue(eventQueue)
  .group('dashboard')
  .batch(5)
  .consume(async (messages) => {
    dashboardCount += messages.length
    console.log(`📊 Dashboard: Received ${messages.length} events (total: ${dashboardCount})`)
    
    // Process events for dashboard
    const userEvents = messages.filter(m => m.data.type === 'user-event')
    const systemEvents = messages.filter(m => m.data.type === 'system-event')
    
    if (userEvents.length > 0) {
      console.log(`   User events: ${userEvents.length}`)
    }
    if (systemEvents.length > 0) {
      console.log(`   System events: ${systemEvents.length}`)
    }
  })

// Consumer 2: Analytics processing
let analyticsCount = 0
const analyticsConsumer = queen
  .queue(eventQueue)
  .group('analytics')
  .batch(5)
  .consume(async (messages) => {
    analyticsCount += messages.length
    console.log(`📈 Analytics: Received ${messages.length} events (total: ${analyticsCount})`)
    
    // Process for analytics
    const actions = messages
      .filter(m => m.data.type === 'user-event')
      .map(m => m.data.action)
    
    if (actions.length > 0) {
      const actionCounts = actions.reduce((acc, action) => {
        acc[action] = (acc[action] || 0) + 1
        return acc
      }, {})
      console.log('   Action counts:', actionCounts)
    }
  })

// Consumer 3: Alerting system
let alertCount = 0
const alertingConsumer = queen
  .queue(eventQueue)
  .group('alerting')
  .batch(5)
  .consume(async (messages) => {
    alertCount += messages.length
    
    // Check for high CPU
    const highCpu = messages.filter(
      m => m.data.type === 'system-event' && m.data.value > 80
    )
    
    if (highCpu.length > 0) {
      console.log(`🚨 Alert: High CPU detected in ${highCpu.length} events!`)
      highCpu.forEach(m => {
        console.log(`   CPU: ${m.data.value.toFixed(2)}%`)
      })
    }
  })

// Wait for all consumers to finish
await Promise.all([
  dashboardConsumer,
  analyticsConsumer,
  alertingConsumer
])

console.log('\n--- Event Processing Summary ---')
console.log(`Dashboard processed: ${dashboardCount} events`)
console.log(`Analytics processed: ${analyticsCount} events`)
console.log(`Alerting processed: ${alertCount} events`)
console.log('\n✅ All consumer groups received all events (fan-out pattern)')

// Demonstrate subscription mode (streaming from a point in time)
console.log('\n--- Subscription Mode Demo ---')

// Push some more events
console.log('Pushing 10 new events...')
for (let i = 1; i <= 10; i++) {
  await queen.queue(eventQueue).push([{
    data: {
      type: 'subscription-test',
      id: i,
      timestamp: Date.now()
    }
  }])
}

// Subscribe to new events only
let subscriptionCount = 0
await queen
  .queue(eventQueue)
  .group('late-subscriber')
  .limit(10)
  .consume(async (message) => {
    subscriptionCount++
    console.log(`📡 Late subscriber received event ${message.data.id}`)
  })

console.log(`Late subscriber processed: ${subscriptionCount} events`)

// Cleanup
await queen.close()
console.log('\n✅ Done!')
console.log('\nKey Takeaways:')
console.log('1. Multiple consumer groups can process the same events')
console.log('2. Each group gets ALL events (fan-out pattern)')
console.log('3. Buffering improves producer throughput')
console.log('4. Batch consumption improves consumer efficiency')
