/**
 * Example: Stream Processing Phase 1
 * Demonstrates filter, map, groupBy, and aggregate operations
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

async function example() {
  // Create a queue for events
  await queen.queue('events').create()
  
  // Push some sample events
  const events = [
    { userId: 'alice', action: 'login', amount: 0 },
    { userId: 'bob', action: 'purchase', amount: 150 },
    { userId: 'alice', action: 'purchase', amount: 200 },
    { userId: 'charlie', action: 'view', amount: 0 },
    { userId: 'bob', action: 'purchase', amount: 75 },
    { userId: 'alice', action: 'purchase', amount: 300 },
    { userId: 'charlie', action: 'purchase', amount: 50 }
  ]
  
  await queen.queue('events').push(events.map(data => ({ data })))
  console.log(`Pushed ${events.length} events`)
  
  // Wait a bit for messages to be available
  await new Promise(resolve => setTimeout(resolve, 200))
  
  console.log('\n' + '='.repeat(60))
  console.log('EXAMPLE 1: Filter purchases only')
  console.log('='.repeat(60))
  
  const purchases = await queen
    .stream('events@example1')
    .filter({ 'payload.action': 'purchase' })
    .execute()
  
  console.log(`Found ${purchases.messages?.length || 0} purchases`)
  console.log(JSON.stringify(purchases.messages, null, 2))
  
  console.log('\n' + '='.repeat(60))
  console.log('EXAMPLE 2: Filter high-value purchases (> 100)')
  console.log('='.repeat(60))
  
  const highValue = await queen
    .stream('events@example2')
    .filter({
      'payload.action': 'purchase',
      'payload.amount': { $gt: 100 }
    })
    .execute()
  
  console.log(`Found ${highValue.messages?.length || 0} high-value purchases`)
  console.log(JSON.stringify(highValue.messages, null, 2))
  
  console.log('\n' + '='.repeat(60))
  console.log('EXAMPLE 3: Map to simplified structure')
  console.log('='.repeat(60))
  
  const mapped = await queen
    .stream('events@example3')
    .filter({ 'payload.action': 'purchase' })
    .map({
      user: 'payload.userId',
      amount: 'payload.amount',
      timestamp: 'created_at'
    })
    .execute()
  
  console.log('Mapped purchases:')
  console.log(JSON.stringify(mapped.messages, null, 2))
  
  console.log('\n' + '='.repeat(60))
  console.log('EXAMPLE 4: Count events per user')
  console.log('='.repeat(60))
  
  const userCounts = await queen
    .stream('events@example4')
    .groupBy('payload.userId')
    .count()
    .execute()
  
  console.log('Events per user:')
  console.log(JSON.stringify(userCounts.messages, null, 2))
  
  console.log('\n' + '='.repeat(60))
  console.log('EXAMPLE 5: Sum purchase amounts per user')
  console.log('='.repeat(60))
  
  const userTotals = await queen
    .stream('events@example5')
    .filter({ 'payload.action': 'purchase' })
    .groupBy('payload.userId')
    .sum('payload.amount')
    .execute()
  
  console.log('Total purchases per user:')
  console.log(JSON.stringify(userTotals.messages, null, 2))
  
  console.log('\n' + '='.repeat(60))
  console.log('EXAMPLE 6: Multiple aggregations per user')
  console.log('='.repeat(60))
  
  const userStats = await queen
    .stream('events@example6')
    .filter({ 'payload.action': 'purchase' })
    .groupBy('payload.userId')
    .aggregate({
      count: { $count: '*' },
      total: { $sum: 'payload.amount' },
      avg: { $avg: 'payload.amount' },
      min: { $min: 'payload.amount' },
      max: { $max: 'payload.amount' }
    })
    .execute()
  
  console.log('User purchase statistics:')
  console.log(JSON.stringify(userStats.messages, null, 2))
  
  console.log('\n' + '='.repeat(60))
  console.log('EXAMPLE 7: Chained pipeline')
  console.log('='.repeat(60))
  
  const pipeline = await queen
    .stream('events@example7')
    .filter({ 'payload.action': 'purchase' })
    .map({
      user: 'payload.userId',
      amount: 'payload.amount'
    })
    .groupBy('user')
    .aggregate({
      purchases: { $count: '*' },
      revenue: { $sum: 'amount' }
    })
    .execute()
  
  console.log('Pipeline result:')
  console.log(JSON.stringify(pipeline.messages, null, 2))
  
  console.log('\n' + '='.repeat(60))
  console.log('EXAMPLE 8: Distinct users')
  console.log('='.repeat(60))
  
  const uniqueUsers = await queen
    .stream('events@example8')
    .distinct('payload.userId')
    .execute()
  
  console.log(`Found ${uniqueUsers.messages?.length || 0} unique users`)
  console.log(JSON.stringify(uniqueUsers.messages, null, 2))
  
  console.log('\n✅ All examples completed!\n')
  
  await queen.close()
}

example().catch(error => {
  console.error('Error:', error)
  process.exit(1)
})

