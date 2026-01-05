/**
 * Example 31: Direct pg_qpubsub Usage with pg Driver
 * 
 * This example demonstrates using pg_qpubsub procedures directly with the
 * native PostgreSQL driver, without the Queen client library.
 * 
 * TWO-TIER API:
 * 
 * PRIMARY (JSONB batch - best for programmatic use):
 * - produce(items_jsonb)  - Batch produce, one call = guaranteed ordering
 * - consume(requests_jsonb) - Batch consume  
 * - commit(acks_jsonb)    - Batch acknowledge
 * 
 * CONVENIENCE (scalar params - for SQL/simple cases):
 * - produce_one(queue, payload, partition, transaction_id, notify)
 * - consume_one(queue, consumer_group, batch_size, partition, timeout, auto_commit, subscription_mode)
 * - commit_one(transaction_id, partition_id, lease_id, consumer_group, status, error_message)
 * 
 * Features demonstrated:
 * - Creating/configuring a queue
 * - Producing messages
 * - Consuming from multiple consumer groups (pub/sub fan-out)
 * - Manual consume and commit
 * - Subscription modes for consumer groups
 * - Queue lag/depth checking
 * 
 * Prerequisites:
 * - PostgreSQL with pg_qpubsub extension loaded
 * - Run: psql -d your_db -f pg_qpubsub/pg_qpubsub--1.0.sql
 * 
 * Usage:
 *   DATABASE_URL=postgres://user:pass@localhost/dbname node 31-pg-qpubsub-direct.js
 */

import pg from 'pg'
import { v4 as uuidv4 } from 'uuid'

const { Pool } = pg

// Configuration
const DATABASE_URL = process.env.DATABASE_URL || 'postgres://postgres:postgres@localhost/postgres'
const QUEUE_NAME = 'orders-demo'

// Create connection pool
const pool = new Pool({ connectionString: DATABASE_URL })

// Helper to log with timestamp
function log(consumer, ...args) {
  const ts = new Date().toISOString().slice(11, 23)
  console.log(`[${ts}] [${consumer}]`, ...args)
}

// ============================================================================
// STEP 1: Configure the queue
// ============================================================================
async function configureQueue() {
  console.log('\n=== Step 1: Configure Queue ===\n')
  
  // queen.configure(queue, lease_time, retry_limit, dlq_enabled)
  const result = await pool.query(
    `SELECT queen.configure($1::text, $2::int, $3::int, $4::bool)`,
    [QUEUE_NAME, 60, 3, true]
  )
  
  console.log(`Queue "${QUEUE_NAME}" configured:`)
  console.log('  - Lease time: 60 seconds (from queue config, not per-consume)')
  console.log('  - Retry limit: 3')
  console.log('  - DLQ enabled: true')
  
  return result.rows[0].configure
}

// ============================================================================
// STEP 2: Produce messages
// ============================================================================
async function produceMessages() {
  console.log('\n=== Step 2: Produce Messages ===\n')
  
  const messages = [
    { orderId: 1001, product: 'Widget A', quantity: 5, price: 29.99 },
    { orderId: 1002, product: 'Widget B', quantity: 2, price: 49.99 },
    { orderId: 1003, product: 'Gadget X', quantity: 1, price: 199.99 },
    { orderId: 1004, product: 'Widget A', quantity: 10, price: 29.99 },
    { orderId: 1005, product: 'Gadget Y', quantity: 3, price: 79.99 },
  ]
  
  for (const msg of messages) {
    // queen.produce_one(queue, payload, partition, transaction_id, notify)
    const transactionId = uuidv4()
    const result = await pool.query(
      `SELECT queen.produce_one($1::text, $2::jsonb, $3::text, $4::text, $5::bool) AS message_id`,
      [QUEUE_NAME, JSON.stringify(msg), 'Default', transactionId, false]
    )
    
    console.log(`Produced order ${msg.orderId}: ${result.rows[0].message_id}`)
  }
  
  console.log(`\nTotal: ${messages.length} messages produced`)
}

// ============================================================================
// STEP 3: Consumer function (used by both consumer groups)
// ============================================================================
async function consumeMessages(consumerGroup, subscriptionMode = null, processDelay = 100) {
  log(consumerGroup, `Starting consumer...`)
  
  let processed = 0
  let hasMore = true
  let isFirstConsume = true
  
  while (hasMore) {
    // queen.consume_one(queue, consumer_group, batch_size, partition, timeout, auto_commit, subscription_mode)
    // subscription_mode only applies on first consume for new consumer groups
    const subMode = isFirstConsume ? subscriptionMode : null
    isFirstConsume = false
    
    const result = await pool.query(`
      SELECT 
        partition_id,
        id,
        transaction_id,
        payload,
        created_at,
        lease_id
      FROM queen.consume_one($1::text, $2::text, $3::int, $4::text, $5::int, $6::bool, $7::text)
    `, [
      QUEUE_NAME, 
      consumerGroup, 
      2,        // batch_size
      null,     // partition (any)
      0,        // timeout (immediate)
      false,    // auto_commit
      subMode   // subscription_mode
    ])
    
    if (result.rows.length === 0) {
      log(consumerGroup, 'No more messages')
      hasMore = false
      break
    }
    
    // Process each message
    for (const msg of result.rows) {
      const order = msg.payload
      
      log(consumerGroup, `Processing order ${order.orderId}:`)
      log(consumerGroup, `  Product: ${order.product}`)
      log(consumerGroup, `  Quantity: ${order.quantity}`)
      log(consumerGroup, `  Price: $${order.price}`)
      log(consumerGroup, `  Total: $${(order.quantity * order.price).toFixed(2)}`)
      
      // Simulate processing time
      await new Promise(resolve => setTimeout(resolve, processDelay))
      
      // queen.commit_one(transaction_id, partition_id, lease_id, consumer_group, status, error_message)
      const commitResult = await pool.query(
        `SELECT queen.commit_one($1::text, $2::uuid, $3::text, $4::text, $5::text, $6::text) AS success`,
        [msg.transaction_id, msg.partition_id, msg.lease_id, consumerGroup, 'completed', null]
      )
      
      if (commitResult.rows[0].success) {
        log(consumerGroup, `  ✓ Committed`)
      } else {
        log(consumerGroup, `  ✗ Commit failed!`)
      }
      
      processed++
    }
  }
  
  log(consumerGroup, `Finished. Processed ${processed} messages.`)
  return processed
}

// ============================================================================
// STEP 4: Run two consumer groups in parallel
// ============================================================================
async function runConsumerGroups() {
  console.log('\n=== Step 3: Consume with Two Consumer Groups (Pub/Sub) ===\n')
  console.log('Both consumer groups will receive ALL messages (fan-out pattern)\n')
  
  // Run both consumers in parallel
  // Both use subscription_mode='all' to process all messages from beginning
  const [count1, count2] = await Promise.all([
    consumeMessages('inventory-service', 'all', 50),
    consumeMessages('billing-service', 'all', 75),
  ])
  
  console.log(`\n=== Summary ===`)
  console.log(`inventory-service processed: ${count1} messages`)
  console.log(`billing-service processed: ${count2} messages`)
}

// ============================================================================
// BONUS: Check queue lag
// ============================================================================
async function checkQueueLag() {
  console.log('\n=== Bonus: Queue Lag ===\n')
  
  // queen.lag(queue, consumer_group)
  const queueLag = await pool.query(
    `SELECT queen.lag($1::text) AS lag`,
    [QUEUE_NAME]
  )
  console.log(`Queue "${QUEUE_NAME}" lag (queue mode): ${queueLag.rows[0].lag}`)
  
  // Check lag for each consumer group
  const invLag = await pool.query(
    `SELECT queen.lag($1::text, $2::text) AS lag`,
    [QUEUE_NAME, 'inventory-service']
  )
  console.log(`Queue "${QUEUE_NAME}" lag (inventory-service): ${invLag.rows[0].lag}`)
  
  const billLag = await pool.query(
    `SELECT queen.lag($1::text, $2::text) AS lag`,
    [QUEUE_NAME, 'billing-service']
  )
  console.log(`Queue "${QUEUE_NAME}" lag (billing-service): ${billLag.rows[0].lag}`)
}

// ============================================================================
// BONUS: Demonstrate subscription modes
// ============================================================================
async function demonstrateSubscriptionModes() {
  console.log('\n=== Bonus: Subscription Modes ===\n')
  
  // Add some new messages
  for (let i = 1; i <= 3; i++) {
    await pool.query(
      `SELECT queen.produce_one($1::text, $2::jsonb)`,
      [QUEUE_NAME, JSON.stringify({ lateOrder: i })]
    )
  }
  console.log('Added 3 more messages...')
  
  // Consumer with 'new' mode starts from NOW (skips existing)
  console.log('\nConsumer with subscription_mode="new" (skips historical):')
  const newModeCount = await consumeMessages('new-only-consumer', 'new', 10)
  console.log(`  → Processed ${newModeCount} messages (should be 0, as no messages after subscription)`)
}

// ============================================================================
// BONUS: Seek consumer group
// ============================================================================
async function demonstrateSeek() {
  console.log('\n=== Bonus: Seek Consumer Group ===\n')
  
  // First, let the new-only-consumer consume some messages
  await pool.query(
    `SELECT queen.produce_one($1::text, $2::jsonb)`,
    [QUEUE_NAME, JSON.stringify({ seekTest: 1 })]
  )
  
  // Consume one message
  await pool.query(`
    SELECT * FROM queen.consume_one($1::text, $2::text, $3::int)
  `, [QUEUE_NAME, 'seek-demo-consumer', 10])
  
  console.log('Created seek-demo-consumer and consumed messages')
  
  // Now seek to end (skip all pending)
  const seekResult = await pool.query(
    `SELECT queen.seek($1::text, $2::text, $3::bool) AS success`,
    ['seek-demo-consumer', QUEUE_NAME, true]  // to_end = true
  )
  console.log(`Seek to end: ${seekResult.rows[0].success}`)
  
  // Verify no messages available
  const afterSeek = await pool.query(`
    SELECT COUNT(*) as count FROM queen.consume_one($1::text, $2::text, $3::int)
  `, [QUEUE_NAME, 'seek-demo-consumer', 10])
  console.log(`Messages after seek to end: ${afterSeek.rows[0].count}`)
}

// ============================================================================
// Main
// ============================================================================
async function main() {
  console.log('╔══════════════════════════════════════════════════════════════╗')
  console.log('║       pg_qpubsub Direct Usage Example                        ║')
  console.log('║       Simplified API: produce, consume, commit               ║')
  console.log('╚══════════════════════════════════════════════════════════════╝')
  
  try {
    // Step 1: Configure queue
    await configureQueue()
    
    // Step 2: Produce messages
    await produceMessages()
    
    // Step 3: Consume with two consumer groups
    await runConsumerGroups()
    
    // Bonus: Check remaining lag
    await checkQueueLag()
    
    // Bonus: Demonstrate subscription modes
    await demonstrateSubscriptionModes()
    
    // Bonus: Demonstrate seek
    await demonstrateSeek()
    
    console.log('\n✓ Example completed successfully!')
    
  } catch (error) {
    console.error('Error:', error.message)
    throw error
  } finally {
    await pool.end()
  }
}

main()
