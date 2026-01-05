import pg from 'pg'
import { v4 as uuidv4 } from 'uuid'

const { Pool } = pg

// Configuration
const DATABASE_URL = process.env.DATABASE_URL || 'postgres://postgres:postgres@localhost/postgres'
const QUEUE_NAME = 'test_qpubsub'
const PARTITIONS = 10
const ITEMS_PER_PARTITION = 100

// Create connection pool
const pool = new Pool({ connectionString: DATABASE_URL })

await pool.query(
    `DELETE FROM queen.queues WHERE name = $1`,
    [QUEUE_NAME]
)

await pool.query(
    `SELECT queen.configure($1::text, $2::int, $3::int, $4::bool)`,
    [QUEUE_NAME, 60, 3, true]
)

// Use batch produce (JSONB API) for guaranteed ordering within partition
for (let p = 0; p < PARTITIONS; p++) {
    const partition = `p-${p}`
    const items = []
    for (let i = 0; i < ITEMS_PER_PARTITION; i++) {
        items.push({
            queue: QUEUE_NAME,
            partition: partition,
            payload: { message: 'Hello, world!', id: i, partition: partition },
            transactionId: uuidv4()
        })
    }
    // Single call = single session = sequential UUIDs = guaranteed order
    await pool.query(
        `SELECT queen.produce($1::jsonb)`,
        [JSON.stringify(items)]
    )
    console.log(`Produced ${items.length} messages for partition ${partition}`)
}

for (let p = 0; p < PARTITIONS; p++) {
    // Use scalar API (consume_one) for simple single-request consumption
    const consume = await pool.query(
        `SELECT * FROM queen.consume_one($1::text, $2::text, $3::int)`,
        [QUEUE_NAME, '__QUEUE_MODE__', ITEMS_PER_PARTITION]
    )
    console.log(`Consumed ${consume.rows.length} messages for partition ${consume.rows[0].partition_id}`)
    let lastId = -1
    for (const msg of consume.rows) {
        if (msg.payload.id !== lastId + 1) {
            console.log(`Out of order: got ${msg.payload.id} after ${lastId}`)
        }
        lastId = msg.payload.id
    }
    // Use scalar API (commit_one) for single ack
    const commit = await pool.query(
        `SELECT queen.commit_one($1::text, $2::uuid, $3::text)`,
        [consume.rows[ITEMS_PER_PARTITION - 1].transaction_id, consume.rows[ITEMS_PER_PARTITION - 1].partition_id, consume.rows[ITEMS_PER_PARTITION - 1].lease_id]
    )    
    console.log(`Committed for partition ${consume.rows[ITEMS_PER_PARTITION - 1].partition_id}`)
}

for (let p = 0; p < PARTITIONS; p++) {
    const consume = await pool.query(
        `SELECT * FROM queen.consume_one($1::text, $2::text, $3::int)`,
        [QUEUE_NAME, 'another-consumer', ITEMS_PER_PARTITION]
    )
    console.log(`Consumed ${consume.rows.length} messages for partition ${consume.rows[0].partition_id}`)
    const commit = await pool.query(
        `SELECT queen.commit_one($1::text, $2::uuid, $3::text, $4::text)`,
        [consume.rows[ITEMS_PER_PARTITION - 1].transaction_id, consume.rows[ITEMS_PER_PARTITION - 1].partition_id, consume.rows[ITEMS_PER_PARTITION - 1].lease_id, 'another-consumer']
    )    
    console.log(`Committed for partition ${consume.rows[ITEMS_PER_PARTITION - 1].partition_id}`)
}

