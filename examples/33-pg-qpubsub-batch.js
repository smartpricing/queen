/**
 * Example 33: pg_qpubsub Batch API (JSONB)
 * 
 * This example demonstrates using the PRIMARY JSONB batch API directly.
 * This is the recommended approach for Node.js/Python applications.
 * 
 * Benefits of batch API:
 * - Single call = guaranteed ordering (sequential UUIDs from same session)
 * - Better performance (fewer round-trips)
 * - Full control over request structure
 * 
 * API used:
 * - queen.produce(items_jsonb)   - Batch produce
 * - queen.consume(requests_jsonb) - Batch consume  
 * - queen.commit(acks_jsonb)     - Batch acknowledge
 * 
 * Usage:
 *   node 33-pg-qpubsub-batch.js
 */

import pg from 'pg'
import { v4 as uuidv4 } from 'uuid'

const { Pool } = pg

// Configuration
const DATABASE_URL = process.env.DATABASE_URL || 'postgres://postgres:postgres@localhost/postgres'
const QUEUE_NAME = 'test_batch_api'
const PARTITIONS = 10
const ITEMS_PER_PARTITION = 100

// Create connection pool
const pool = new Pool({ connectionString: DATABASE_URL })

console.log('╔══════════════════════════════════════════════════════════════╗')
console.log('║       pg_qpubsub Batch API Example                           ║')
console.log('║       Using PRIMARY JSONB API for all operations             ║')
console.log('╚══════════════════════════════════════════════════════════════╝\n')

// Cleanup previous run
await pool.query(`DELETE FROM queen.queues WHERE name = $1`, [QUEUE_NAME])

// Configure queue
await pool.query(
    `SELECT queen.configure($1::text, $2::int, $3::int, $4::bool)`,
    [QUEUE_NAME, 60, 3, true]
)
console.log(`Queue "${QUEUE_NAME}" configured\n`)

// ============================================================================
// PRODUCE: Batch API
// ============================================================================
console.log('=== Batch Produce ===\n')

for (let p = 0; p < PARTITIONS; p++) {
    const partition = `p-${p}`
    
    // Build batch of items for this partition
    const items = []
    for (let i = 0; i < ITEMS_PER_PARTITION; i++) {
        items.push({
            queue: QUEUE_NAME,
            partition: partition,
            payload: { message: 'Hello, world!', id: i, partition: partition },
            transactionId: uuidv4()
        })
    }
    
    // Single batch produce call
    // Returns: [{index, transaction_id, status, message_id, partition_id}, ...]
    const result = await pool.query(
        `SELECT queen.produce($1::jsonb) AS results`,
        [JSON.stringify(items)]
    )
    
    console.log(`Produced ${items.length} messages for partition ${partition}`)
}

console.log(`\nTotal: ${PARTITIONS * ITEMS_PER_PARTITION} messages produced\n`)

// ============================================================================
// CONSUME: Batch API
// ============================================================================
console.log('=== Batch Consume (Queue Mode) ===\n')

for (let p = 0; p < PARTITIONS; p++) {
    // Build consume request
    const consumeRequest = [{
        queue_name: QUEUE_NAME,
        partition_name: '',           // empty = any partition
        consumer_group: '__QUEUE_MODE__',
        batch_size: ITEMS_PER_PARTITION,
        lease_seconds: 60,
        worker_id: `worker-${uuidv4()}`,
        auto_ack: false,
        sub_mode: '',
        sub_from: ''
    }]
    
    // Single batch consume call
    // Returns: [{idx, result: {success, partitionId, messages: [...]}}]
    const consumeResult = await pool.query(
        `SELECT queen.consume($1::jsonb) AS results`,
        [JSON.stringify(consumeRequest)]
    )
    
    const batch = consumeResult.rows[0].results[0].result
    const messages = batch.messages || []
    const partitionId = batch.partitionId
    const leaseId = consumeRequest[0].worker_id
    
    console.log(`Consumed ${messages.length} messages from partition ${partitionId}`)
    
    // Verify ordering
    let lastId = -1
    for (const msg of messages) {
        const payload = msg.data
        if (payload.id !== lastId + 1) {
            console.log(`  ⚠ Out of order: got ${payload.id} after ${lastId}`)
        }
        lastId = payload.id
    }
    
    // ========================================================================
    // COMMIT: Batch API
    // ========================================================================
    // Build batch ack for all messages in this batch
    const acks = messages.map(msg => ({
        transactionId: msg.transactionId,
        partitionId: partitionId,
        leaseId: leaseId,
        consumerGroup: '__QUEUE_MODE__',
        status: 'completed'
    }))
    
    // Single batch commit call
    // Returns: [{index, transactionId, success, error}, ...]
    const commitResult = await pool.query(
        `SELECT queen.commit($1::jsonb) AS results`,
        [JSON.stringify(acks)]
    )
    
    const commitResults = commitResult.rows[0].results
    const successCount = commitResults.filter(r => r.success).length
    console.log(`Committed ${successCount}/${messages.length} messages\n`)
}

// ============================================================================
// CONSUME: Another Consumer Group (Pub/Sub)
// ============================================================================
console.log('=== Batch Consume (Consumer Group: analytics) ===\n')

for (let p = 0; p < PARTITIONS; p++) {
    const consumeRequest = [{
        queue_name: QUEUE_NAME,
        partition_name: '',
        consumer_group: 'analytics',
        batch_size: ITEMS_PER_PARTITION,
        lease_seconds: 60,
        worker_id: `analytics-worker-${uuidv4()}`,
        auto_ack: false,
        sub_mode: 'all',  // Process from beginning
        sub_from: ''
    }]
    
    const consumeResult = await pool.query(
        `SELECT queen.consume($1::jsonb) AS results`,
        [JSON.stringify(consumeRequest)]
    )
    
    const batch = consumeResult.rows[0].results[0].result
    const messages = batch.messages || []
    const partitionId = batch.partitionId
    const leaseId = consumeRequest[0].worker_id
    
    console.log(`[analytics] Consumed ${messages.length} messages from partition ${partitionId}`)
    
    // Batch commit
    const acks = messages.map(msg => ({
        transactionId: msg.transactionId,
        partitionId: partitionId,
        leaseId: leaseId,
        consumerGroup: 'analytics',
        status: 'completed'
    }))
    
    const commitResult = await pool.query(
        `SELECT queen.commit($1::jsonb) AS results`,
        [JSON.stringify(acks)]
    )
    
    const successCount = commitResult.rows[0].results.filter(r => r.success).length
    console.log(`[analytics] Committed ${successCount}/${messages.length} messages\n`)
}

// ============================================================================
// Summary
// ============================================================================
console.log('=== Summary ===\n')
console.log(`✓ Produced ${PARTITIONS * ITEMS_PER_PARTITION} messages across ${PARTITIONS} partitions`)
console.log(`✓ Consumed and committed by __QUEUE_MODE__`)
console.log(`✓ Consumed and committed by analytics consumer group`)
console.log(`\n✓ All operations used batch JSONB API!`)

await pool.end()

