import pg from 'pg'

const DATABASE_URL = process.env.DATABASE_URL || 'postgres://postgres:postgres@localhost/postgres'
const pool = new pg.Pool({ connectionString: DATABASE_URL })

// Batch produce
const items = [
  { queue: 'orders', payload: { orderId: 1 } },
  { queue: 'orders', payload: { orderId: 2 } }
]
const produceResult = await pool.query(`SELECT queen.produce($1::jsonb)`, [JSON.stringify(items)])
console.log(produceResult.rows[0])

// Batch consume
const req = [{
  queueName: 'orders',
  consumerGroup: '__QUEUE_MODE__',
  batchSize: 10,
  leaseSeconds: 60,
  workerId: 'worker-1'
}]
const result = await pool.query(`SELECT queen.consume($1::jsonb)`, [JSON.stringify(req)])
const messages = result.rows[0].consume[0].result.messages
console.log(JSON.stringify(result.rows, null, 2))

// Batch commit
const acks = messages.map(m => ({
  transactionId: m.transactionId,
  partitionId: result.rows[0].consume[0].result.partitionId,
  leaseId: 'worker-1',
  consumerGroup: '__QUEUE_MODE__',
  status: 'completed'
}))
const commitResult = await pool.query(`SELECT queen.commit($1::jsonb)`, [JSON.stringify(acks)])
console.log(commitResult.rows[0])