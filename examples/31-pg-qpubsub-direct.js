/**
 * Example 31: Direct pg_qpubsub Usage with pg Driver
 * 
 * This example demonstrates using pg_qpubsub procedures directly with the
 * native PostgreSQL driver, without the Queen client library.
 * 
 * API follows Kafka-style naming:
 * - produce  (send messages to a queue)
 * - consume  (receive messages from a queue)
 * - commit   (acknowledge message processing)
 * - poll     (consume with wait/long-polling)
 * 
 * Features demonstrated:
 * - Creating/configuring a queue
 * - Producing messages
 * - Consuming from multiple consumer groups (pub/sub fan-out)
 * - Manual consume and commit
 * 
 * Prerequisites:
 * - PostgreSQL with pg_qpubsub extension loaded
 * - Run: psql -d your_db -f pg_qpubsub/pg_qpubsub--1.0.sql
 * 
 * Usage:
 *   DATABASE_URL=postgres://user:pass@localhost/dbname node 31-pg-qpubsub-direct.js
 * 
 * NOTE: When using pg driver with PostgreSQL functions, explicit type casts
 * (e.g., $1::text) are required for proper function overload resolution.
 */

import pg from 'pg'
import { v4 as uuidv4 } from 'uuid'

const { Pool } = pg

// Configuration
const DATABASE_URL = process.env.DATABASE_URL || 'postgres://postgres:postgres@localhost/postgres'
const QUEUE_NAME = 'orders-example'  // Fresh queue name

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
  
  // NOTE: Explicit type casts required for pg driver function calls
  const result = await pool.query(
    `SELECT queen.configure($1::text, $2::int, $3::int, $4::bool)`,
    [QUEUE_NAME, 60, 3, true]  // queue, lease_time, retry_limit, dlq_enabled
  )
  
  console.log(`Queue "${QUEUE_NAME}" configured:`)
  console.log('  - Lease time: 60 seconds')
  console.log('  - Retry limit: 3')
  console.log('  - DLQ enabled: true')
  
  return result.rows[0].configure
}

// ============================================================================
// STEP 2: Produce messages (formerly "push")
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
    // queen.produce(queue, payload, transaction_id)
    const transactionId = uuidv4()
    const result = await pool.query(
      `SELECT queen.produce($1::text, $2::jsonb, $3::text) AS message_id`,
      [QUEUE_NAME, JSON.stringify(msg), transactionId]
    )
    
    console.log(`Produced order ${msg.orderId}: ${result.rows[0].message_id}`)
  }
  
  console.log(`\nTotal: ${messages.length} messages produced`)
}

// ============================================================================
// STEP 3: Consumer function (used by both consumer groups)
// ============================================================================
async function consumeMessages(consumerGroup, processDelay = 100) {
  log(consumerGroup, `Starting consumer...`)
  
  let processed = 0
  let hasMore = true
  
  while (hasMore) {
    // Consume a batch of messages (formerly "pop")
    // queen.consume(queue, consumer_group, batch_size, lease_seconds)
    // NOTE: Explicit type casts required!
    const result = await pool.query(`
      SELECT 
        partition_id,
        id,
        transaction_id,
        payload,
        created_at,
        lease_id
      FROM queen.consume($1::text, $2::text, $3::int, $4::int)
    `, [QUEUE_NAME, consumerGroup, 2, 60])
    
    if (result.rows.length === 0) {
      log(consumerGroup, 'No more messages')
      hasMore = false
      break
    }
    
    // Process each message
    for (const msg of result.rows) {
      // payload is already parsed by pg driver (JSONB -> object)
      const order = msg.payload
      
      log(consumerGroup, `Processing order ${order.orderId}:`)
      log(consumerGroup, `  Product: ${order.product}`)
      log(consumerGroup, `  Quantity: ${order.quantity}`)
      log(consumerGroup, `  Price: $${order.price}`)
      log(consumerGroup, `  Total: $${(order.quantity * order.price).toFixed(2)}`)
      
      // Simulate processing time
      await new Promise(resolve => setTimeout(resolve, processDelay))
      
      // Commit the message (formerly "ack")
      // queen.commit(transaction_id, partition_id, lease_id, status, consumer_group, error_message)
      // Using 6-param version to avoid ambiguity with 4-param version
      const commitResult = await pool.query(
        `SELECT queen.commit($1::text, $2::uuid, $3::text, $4::text, $5::text, $6::text) AS success`,
        [msg.transaction_id, msg.partition_id, msg.lease_id, 'completed', consumerGroup, null]
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
  const [count1, count2] = await Promise.all([
    consumeMessages('inventory-service', 50),
    consumeMessages('billing-service', 75),
  ])
  
  console.log(`\n=== Summary ===`)
  console.log(`inventory-service processed: ${count1} messages`)
  console.log(`billing-service processed: ${count2} messages`)
}

// ============================================================================
// BONUS: Check queue depth (lag)
// ============================================================================
async function checkQueueDepth() {
  console.log('\n=== Bonus: Queue Depth (Lag) ===\n')
  
  const result = await pool.query(
    `SELECT queen.depth($1::text) AS depth`,
    [QUEUE_NAME]
  )
  
  console.log(`Queue "${QUEUE_NAME}" depth: ${result.rows[0].depth}`)
  
  // Can also use queen.lag() which is an alias for depth()
  const lagResult = await pool.query(
    `SELECT queen.lag($1::text) AS lag`,
    [QUEUE_NAME]
  )
  console.log(`Queue "${QUEUE_NAME}" lag: ${lagResult.rows[0].lag}`)
}

// ============================================================================
// Main
// ============================================================================
async function main() {
  console.log('╔══════════════════════════════════════════════════════════════╗')
  console.log('║       pg_qpubsub Direct Usage Example                        ║')
  console.log('║       Using native pg driver without Queen client            ║')
  console.log('║       Kafka-style API: produce, consume, commit              ║')
  console.log('╚══════════════════════════════════════════════════════════════╝')
  
  try {
    // Step 1: Configure queue
    await configureQueue()
    
    // Step 2: Produce messages
    await produceMessages()
    
    // Step 3: Consume with two consumer groups
    await runConsumerGroups()
    
    // Bonus: Check remaining depth
    await checkQueueDepth()
    
    console.log('\n✓ Example completed successfully!')
    
  } catch (error) {
    console.error('Error:', error.message)
    throw error
  } finally {
    await pool.end()
  }
}

main()
