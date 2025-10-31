/**
 * Phase 1 Tests - Basic Stream Operations
 * Tests: filter, map, groupBy, aggregate
 */

import { Queen } from '../../client-v2/index.js'

const queen = new Queen('http://localhost:6632')

// Test utilities
function startTest(name, category = 'streaming') {
  console.log(`\n${'='.repeat(60)}`)
  console.log(`TEST: ${name}`)
  console.log(`Category: ${category}`)
  console.log(`${'='.repeat(60)}`)
}

function passTest(message) {
  console.log(`✅ PASS: ${message}`)
}

function failTest(message) {
  console.error(`❌ FAIL: ${message}`)
  throw new Error(message)
}

function assertEqual(actual, expected, message) {
  if (actual !== expected) {
    failTest(`${message}: expected ${expected}, got ${actual}`)
  }
  passTest(message)
}

function assertGreaterThan(actual, threshold, message) {
  if (actual <= threshold) {
    failTest(`${message}: expected > ${threshold}, got ${actual}`)
  }
  passTest(message)
}

// ========== TEST 1: Filter with Object Syntax ==========
export async function testFilterObjectSyntax() {
  startTest('Filter with Object Syntax')
  
  const queueName = `test-filter-obj-${Date.now()}`
  
  try {
    // Create queue
    await queen.queue(queueName).create()
    console.log(`Created queue: ${queueName}`)
    
    // Push test data
    const messages = [
      { amount: 500, status: 'pending' },
      { amount: 1500, status: 'completed' },
      { amount: 2000, status: 'completed' },
      { amount: 800, status: 'pending' },
      { amount: 3000, status: 'completed' }
    ]
    
    await queen.queue(queueName).push(messages.map(data => ({ data })))
    console.log(`Pushed ${messages.length} messages`)
    
    // Wait a bit for messages to be available
    await new Promise(resolve => setTimeout(resolve, 100))
    
    // Filter: amount > 1000
    const result = await queen
      .stream(`${queueName}@test-filter`)
      .filter({ 'payload.amount': { $gt: 1000 } })
      .execute()
    
    console.log('Filter result:', JSON.stringify(result, null, 2))
    
    const filteredCount = result.messages?.length || 0
    assertEqual(filteredCount, 3, 'Should filter to 3 messages with amount > 1000')
    
    passTest('Filter with object syntax works')
    
  } catch (error) {
    console.error('Error:', error)
    failTest(error.message)
  }
}

// ========== TEST 2: Filter with Multiple Conditions ==========
export async function testFilterMultipleConditions() {
  startTest('Filter with Multiple Conditions')
  
  const queueName = `test-filter-multi-${Date.now()}`
  
  try {
    await queen.queue(queueName).create()
    
    const messages = [
      { amount: 500, status: 'pending' },
      { amount: 1500, status: 'completed' },
      { amount: 2000, status: 'completed' },
      { amount: 800, status: 'pending' },
      { amount: 3000, status: 'pending' }
    ]
    
    await queen.queue(queueName).push(messages.map(data => ({ data })))
    await new Promise(resolve => setTimeout(resolve, 100))
    
    // Filter: amount > 1000 AND status = 'completed'
    const result = await queen
      .stream(`${queueName}@test-multi`)
      .filter({
        'payload.amount': { $gt: 1000 },
        'payload.status': 'completed'
      })
      .execute()
    
    console.log('Multi-filter result:', JSON.stringify(result, null, 2))
    
    const filteredCount = result.messages?.length || 0
    assertEqual(filteredCount, 2, 'Should filter to 2 messages')
    
    passTest('Multiple filter conditions work')
    
  } catch (error) {
    console.error('Error:', error)
    failTest(error.message)
  }
}

// ========== TEST 3: Map to New Fields ==========
export async function testMapFields() {
  startTest('Map to New Fields')
  
  const queueName = `test-map-${Date.now()}`
  
  try {
    await queen.queue(queueName).create()
    
    const messages = [
      { userId: 'user-1', amount: 1500 },
      { userId: 'user-2', amount: 2000 },
      { userId: 'user-3', amount: 2500 }
    ]
    
    await queen.queue(queueName).push(messages.map(data => ({ data })))
    await new Promise(resolve => setTimeout(resolve, 100))
    
    // Map to new structure
    const result = await queen
      .stream(`${queueName}@test-map`)
      .map({
        userId: 'payload.userId',
        amount: 'payload.amount',
        timestamp: 'created_at'
      })
      .execute()
    
    console.log('Map result:', JSON.stringify(result, null, 2))
    
    const mapped = result.messages || []
    assertEqual(mapped.length, 3, 'Should have 3 mapped messages')
    
  // Check first message has expected fields
  // Note: PostgreSQL lowercases identifiers, so userId becomes userid
  if (mapped.length > 0) {
    const first = mapped[0]
    const hasUserId = first.userId || first.userid
    const hasAmount = first.amount
    const hasTimestamp = first.timestamp
    
    if (!hasUserId || !hasAmount || !hasTimestamp) {
      console.log('First message:', first)
      console.log('Fields:', Object.keys(first))
      failTest('Mapped message missing expected fields')
    }
    passTest('Mapped messages have expected fields')
  }
    
    passTest('Map operation works')
    
  } catch (error) {
    console.error('Error:', error)
    failTest(error.message)
  }
}

// ========== TEST 4: GroupBy + Count ==========
export async function testGroupByCount() {
  startTest('GroupBy + Count Aggregation')
  
  const queueName = `test-groupby-count-${Date.now()}`
  
  try {
    await queen.queue(queueName).create()
    
    const messages = [
      { userId: 'user-1', event: 'click' },
      { userId: 'user-1', event: 'view' },
      { userId: 'user-2', event: 'click' },
      { userId: 'user-1', event: 'purchase' },
      { userId: 'user-2', event: 'view' },
      { userId: 'user-3', event: 'click' }
    ]
    
    await queen.queue(queueName).push(messages.map(data => ({ data })))
    await new Promise(resolve => setTimeout(resolve, 100))
    
    // GroupBy userId and count
    const result = await queen
      .stream(`${queueName}@test-groupby`)
      .groupBy('payload.userId')
      .count()
      .execute()
    
    console.log('GroupBy count result:', JSON.stringify(result, null, 2))
    
    const grouped = result.messages || []
    assertEqual(grouped.length, 3, 'Should have 3 groups')
    
    // Find user-1 count
    const user1 = grouped.find(g => g.userId === 'user-1' || g['payload.userId'] === 'user-1')
    if (user1) {
      assertEqual(user1.count, 3, 'user-1 should have count of 3')
    }
    
    passTest('GroupBy with count works')
    
  } catch (error) {
    console.error('Error:', error)
    failTest(error.message)
  }
}

// ========== TEST 5: GroupBy + Sum ==========
export async function testGroupBySum() {
  startTest('GroupBy + Sum Aggregation')
  
  const queueName = `test-groupby-sum-${Date.now()}`
  
  try {
    await queen.queue(queueName).create()
    
    const messages = [
      { userId: 'user-1', amount: 100 },
      { userId: 'user-1', amount: 200 },
      { userId: 'user-2', amount: 300 },
      { userId: 'user-1', amount: 150 },
      { userId: 'user-2', amount: 250 }
    ]
    
    await queen.queue(queueName).push(messages.map(data => ({ data })))
    await new Promise(resolve => setTimeout(resolve, 100))
    
    // GroupBy userId and sum amounts
    const result = await queen
      .stream(`${queueName}@test-sum`)
      .groupBy('payload.userId')
      .sum('payload.amount')
      .execute()
    
    console.log('GroupBy sum result:', JSON.stringify(result, null, 2))
    
    const grouped = result.messages || []
    assertEqual(grouped.length, 2, 'Should have 2 groups')
    
    passTest('GroupBy with sum works')
    
  } catch (error) {
    console.error('Error:', error)
    failTest(error.message)
  }
}

// ========== TEST 6: GroupBy + Multiple Aggregations ==========
export async function testGroupByMultipleAgg() {
  startTest('GroupBy + Multiple Aggregations')
  
  const queueName = `test-groupby-multi-${Date.now()}`
  
  try {
    await queen.queue(queueName).create()
    
    const messages = [
      { userId: 'user-1', amount: 100 },
      { userId: 'user-1', amount: 200 },
      { userId: 'user-1', amount: 300 },
      { userId: 'user-2', amount: 150 },
      { userId: 'user-2', amount: 250 }
    ]
    
    await queen.queue(queueName).push(messages.map(data => ({ data })))
    await new Promise(resolve => setTimeout(resolve, 100))
    
    // GroupBy with multiple aggregations
    const result = await queen
      .stream(`${queueName}@test-multi-agg`)
      .groupBy('payload.userId')
      .aggregate({
        count: { $count: '*' },
        total: { $sum: 'payload.amount' },
        avg: { $avg: 'payload.amount' },
        min: { $min: 'payload.amount' },
        max: { $max: 'payload.amount' }
      })
      .execute()
    
    console.log('Multiple aggregations result:', JSON.stringify(result, null, 2))
    
    const grouped = result.messages || []
    assertEqual(grouped.length, 2, 'Should have 2 groups')
    
    // Check user-1 has all aggregation fields
    const user1 = grouped.find(g => g.userId === 'user-1' || g['payload.userId'] === 'user-1')
    if (user1) {
      if (!user1.count || !user1.total || !user1.avg || !user1.min || !user1.max) {
        failTest('Missing aggregation fields')
      }
      passTest('All aggregation fields present')
    }
    
    passTest('Multiple aggregations work')
    
  } catch (error) {
    console.error('Error:', error)
    failTest(error.message)
  }
}

// ========== TEST 7: Chain Operations ==========
export async function testChainOperations() {
  startTest('Chain Filter + Map + GroupBy + Aggregate')
  
  const queueName = `test-chain-${Date.now()}`
  
  try {
    await queen.queue(queueName).create()
    
    const messages = [
      { userId: 'user-1', amount: 500, status: 'completed' },
      { userId: 'user-1', amount: 1500, status: 'completed' },
      { userId: 'user-2', amount: 2000, status: 'pending' },
      { userId: 'user-1', amount: 800, status: 'pending' },
      { userId: 'user-2', amount: 2500, status: 'completed' },
      { userId: 'user-3', amount: 1200, status: 'completed' }
    ]
    
    await queen.queue(queueName).push(messages.map(data => ({ data })))
    await new Promise(resolve => setTimeout(resolve, 100))
    
    // Chain: filter completed, map fields, groupBy user, sum amounts
    const result = await queen
      .stream(`${queueName}@test-chain`)
      .filter({ 'payload.status': 'completed' })
      .map({
        userId: 'payload.userId',
        amount: 'payload.amount'
      })
      .groupBy('userId')
      .aggregate({
        count: { $count: '*' },
        total: { $sum: 'amount' }
      })
      .execute()
    
    console.log('Chained operations result:', JSON.stringify(result, null, 2))
    
    const grouped = result.messages || []
    assertGreaterThan(grouped.length, 0, 'Should have grouped results')
    
    passTest('Chained operations work')
    
  } catch (error) {
    console.error('Error:', error)
    failTest(error.message)
  }
}

// ========== TEST 8: Distinct ==========
export async function testDistinct() {
  startTest('Distinct Operation')
  
  const queueName = `test-distinct-${Date.now()}`
  
  try {
    await queen.queue(queueName).create()
    
    const messages = [
      { userId: 'user-1', event: 'click' },
      { userId: 'user-2', event: 'view' },
      { userId: 'user-1', event: 'purchase' },
      { userId: 'user-3', event: 'click' },
      { userId: 'user-2', event: 'view' }
    ]
    
    await queen.queue(queueName).push(messages.map(data => ({ data })))
    await new Promise(resolve => setTimeout(resolve, 100))
    
    // Get distinct userIds
    const result = await queen
      .stream(`${queueName}@test-distinct`)
      .distinct('payload.userId')
      .execute()
    
    console.log('Distinct result:', JSON.stringify(result, null, 2))
    
    const distinct = result.messages || []
    assertEqual(distinct.length, 3, 'Should have 3 distinct users')
    
    passTest('Distinct operation works')
    
  } catch (error) {
    console.error('Error:', error)
    failTest(error.message)
  }
}

// ========== TEST 9: Limit ==========
export async function testLimit() {
  startTest('Limit Operation')
  
  const queueName = `test-limit-${Date.now()}`
  
  try {
    await queen.queue(queueName).create()
    
    const messages = Array.from({ length: 20 }, (_, i) => ({
      index: i,
      value: i * 10
    }))
    
    await queen.queue(queueName).push(messages.map(data => ({ data })))
    await new Promise(resolve => setTimeout(resolve, 100))
    
    // Limit to 5 messages
    const result = await queen
      .stream(`${queueName}@test-limit`)
      .limit(5)
      .execute()
    
    console.log('Limit result:', JSON.stringify(result, null, 2))
    
    const limited = result.messages || []
    assertEqual(limited.length, 5, 'Should limit to 5 messages')
    
    passTest('Limit operation works')
    
  } catch (error) {
    console.error('Error:', error)
    failTest(error.message)
  }
}

// ========== RUN ALL TESTS ==========
async function runAllTests() {
  console.log('\n' + '='.repeat(60))
  console.log('PHASE 1 STREAMING TESTS')
  console.log('='.repeat(60))
  
  const tests = [
    testFilterObjectSyntax,
    testFilterMultipleConditions,
    testMapFields,
    testGroupByCount,
    testGroupBySum,
    testGroupByMultipleAgg,
    testChainOperations,
    testDistinct,
    testLimit
  ]
  
  let passed = 0
  let failed = 0
  
  for (const test of tests) {
    try {
      await test()
      passed++
    } catch (error) {
      failed++
      console.error(`\n❌ Test failed: ${error.message}`)
    }
  }
  
  console.log('\n' + '='.repeat(60))
  console.log(`RESULTS: ${passed} passed, ${failed} failed`)
  console.log('='.repeat(60) + '\n')
  
  await queen.close()
  
  if (failed > 0) {
    process.exit(1)
  }
}

// Run if executed directly
if (import.meta.url === `file://${process.argv[1]}`) {
  runAllTests().catch(error => {
    console.error('Fatal error:', error)
    process.exit(1)
  })
}

export { runAllTests }

