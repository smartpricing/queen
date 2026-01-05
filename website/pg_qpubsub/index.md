# pg_qpubsub

A PostgreSQL extension providing a high-performance message queue with partitions, consumer groups, dead-letter queues, and real-time analytics.

## Features

- **Produce/Consume/Commit** - Kafka-style queue operations
- **Two-Layer API** - JSONB batch API for applications, scalar API for SQL
- **Partitions** - Parallel processing with ordering guarantees
- **Consumer Groups** - Pub/Sub pattern (multiple groups receive same messages)
- **At-Least-Once Delivery** - Messages redelivered if not committed
- **Dead Letter Queue** - Failed messages preserved for inspection
- **NOTIFY/LISTEN** - Real-time notifications on new messages
- **Transactional Pipelines** - Commit + Produce atomically

## Two-Layer API

pg_qpubsub provides two API layers for different use cases:

### Primary API (JSONB)

For programmatic access from Node.js, Python, Go, etc. Accepts JSONB arrays for batch operations.

| Function | Description |
|----------|-------------|
| `queen.produce(items JSONB)` | Batch produce messages |
| `queen.consume(requests JSONB)` | Batch consume messages |
| `queen.commit(acks JSONB)` | Batch acknowledge messages |
| `queen.renew(leases JSONB)` | Batch renew leases |
| `queen.transaction(ops JSONB)` | Atomic multi-operation |

### Convenience API (Scalar)

For SQL usage, psql, triggers, and simple cases. Uses scalar parameters.

| Function | Description |
|----------|-------------|
| `queen.produce_one(queue, payload, ...)` | Produce single message |
| `queen.consume_one(queue, consumer_group, ...)` | Consume messages |
| `queen.commit_one(txn_id, partition_id, lease_id, ...)` | Acknowledge single message |
| `queen.renew_one(lease_id, extend_seconds)` | Renew single lease |
| `queen.nack(...)` | Mark for retry |
| `queen.reject(...)` | Send to DLQ |

### Note on JSONB API

The JSONB functions (`produce`, `consume`, `commit`, `renew`) are thin wrappers around the core libqueen procedures (`push_messages_v2`, `pop_unified_batch`, `ack_messages_v2`, `renew_lease_v2`). They return immediately after executing the operation.

Features like **LISTEN/NOTIFY** for real-time notifications must be implemented at the application level:

```javascript
// LISTEN/NOTIFY with JSONB API
const client = await pool.connect()

client.on('notification', async (msg) => {
  // Notified - now consume using batch API
  const result = await client.query(
    `SELECT queen.consume($1::jsonb)`,
    [JSON.stringify([{ queueName: 'orders', batchSize: 10, workerId: 'w1' }])]
  )
  // process messages...
})

await client.query(`LISTEN queen_orders`)
```

The scalar `consume_one` function supports a `timeout` parameter for long-polling within SQL.

## Installation

### Direct SQL Loading (Recommended)

Works on managed databases (AWS RDS, Google Cloud SQL, Azure, etc.).

```bash
# Build the SQL file
cd pg_qpubsub && ./build.sh

# Load pgcrypto (required)
psql -d mydb -c "CREATE EXTENSION IF NOT EXISTS pgcrypto"

# Load pg_qpubsub
psql -d mydb -f pg_qpubsub--1.0.sql
```

### PostgreSQL Extension

Requires superuser access.

```bash
./build.sh
make install
psql -d mydb -c "CREATE EXTENSION pg_qpubsub"
```

## Quick Start

### Using Primary API (Node.js)

```javascript
import pg from 'pg'
const pool = new pg.Pool({ connectionString: DATABASE_URL })

// Batch produce (guaranteed ordering)
const items = [
  { queue: 'orders', payload: { orderId: 1 } },
  { queue: 'orders', payload: { orderId: 2 } },
  { queue: 'orders', payload: { orderId: 3 } }
]
await pool.query(`SELECT queen.produce($1::jsonb)`, [JSON.stringify(items)])

// Batch consume
const consumeReq = [{
  queueName: 'orders',
  consumerGroup: '__QUEUE_MODE__',
  batchSize: 10,
  leaseSeconds: 60,
  workerId: 'worker-1'
}]
const result = await pool.query(`SELECT queen.consume($1::jsonb)`, [JSON.stringify(consumeReq)])
const messages = result.rows[0].consume[0].result.messages

// Batch commit
const acks = messages.map(m => ({
  transactionId: m.transactionId,
  partitionId: result.rows[0].consume[0].result.partitionId,
  leaseId: 'worker-1',
  consumerGroup: '__QUEUE_MODE__',
  status: 'completed'
}))
await pool.query(`SELECT queen.commit($1::jsonb)`, [JSON.stringify(acks)])
```

### Using Convenience API (SQL)

```sql
-- Configure queue
SELECT queen.configure('orders', 60, 3, true);

-- Produce a message
SELECT queen.produce_one('orders', '{"orderId": 123}'::jsonb);

-- Consume messages
SELECT * FROM queen.consume_one('orders', '__QUEUE_MODE__', 10);

-- Commit (use values from consumed row)
SELECT queen.commit_one('txn-id', 'partition-uuid', 'lease-id');
```

## API Reference

### Primary API

#### `queen.produce(p_items JSONB)` -> `JSONB`

Batch produce messages.

```javascript
// Input format
[
  {
    "queue": "orders",
    "partition": "Default",
    "payload": { "orderId": 123 },
    "transactionId": "optional-idempotency-key"
  }
]

// Returns array of results
[{ "index": 0, "message_id": "uuid", "status": "queued" }]
```

#### `queen.consume(p_requests JSONB)` -> `JSONB`

Batch consume messages.

```javascript
// Input format (camelCase)
[
  {
    "queueName": "orders",
    "partitionName": "",           // empty = any partition
    "consumerGroup": "__QUEUE_MODE__",
    "batchSize": 10,
    "leaseSeconds": 60,
    "workerId": "worker-1",
    "autoAck": false,
    "subMode": "",                 // 'new' for new messages only
    "subFrom": ""
  }
]

// Returns array of results
[{
  "idx": 0,
  "result": {
    "success": true,
    "partitionId": "uuid",
    "messages": [
      { "id": "uuid", "transactionId": "...", "data": {...}, "createdAt": "..." }
    ]
  }
}]
```

#### `queen.commit(p_acks JSONB)` -> `JSONB`

Batch acknowledge messages.

```javascript
// Input format
[
  {
    "transactionId": "...",
    "partitionId": "uuid",
    "leaseId": "worker-1",
    "consumerGroup": "__QUEUE_MODE__",
    "status": "completed",         // 'completed', 'retry', 'dlq'
    "errorMessage": null
  }
]

// Returns array of results
[{ "index": 0, "success": true }]
```

#### `queen.renew(p_leases JSONB)` -> `JSONB`

Batch renew leases.

```javascript
// Input format
[{ "leaseId": "worker-1", "extendSeconds": 60 }]

// Returns array of results
[{ "index": 0, "success": true, "expiresAt": "timestamp" }]
```

#### `queen.transaction(p_operations JSONB)` -> `JSONB`

Execute multiple operations atomically.

```javascript
// Input format
[
  { "type": "push", "queue": "orders", "payload": {...} },
  { "type": "ack", "transactionId": "...", "partitionId": "...", "leaseId": "..." }
]
```

### Convenience API

#### `queen.produce_one(queue, payload, partition, transaction_id, notify)` -> `UUID`

```sql
SELECT queen.produce_one(
    'orders',                    -- queue name
    '{"orderId": 123}'::jsonb,   -- payload
    'Default',                   -- partition (default: 'Default')
    NULL,                        -- transaction_id (default: auto-generated)
    FALSE                        -- notify (default: false)
);
```

#### `queen.consume_one(queue, consumer_group, batch_size, partition, timeout, auto_commit, subscription_mode)` -> `TABLE`

```sql
SELECT * FROM queen.consume_one(
    'orders',                    -- queue name
    '__QUEUE_MODE__',            -- consumer group
    10,                          -- batch size
    NULL,                        -- partition (NULL = any)
    0,                           -- timeout seconds (0 = immediate)
    FALSE,                       -- auto commit
    NULL                         -- subscription mode ('new', 'all', or timestamp)
);
-- Returns: partition_id, id, transaction_id, payload, created_at, lease_id
```

#### `queen.commit_one(txn_id, partition_id, lease_id, consumer_group, status, error_message)` -> `BOOLEAN`

```sql
SELECT queen.commit_one(
    'txn-123',                   -- transaction_id from consume
    'partition-uuid'::uuid,      -- partition_id from consume
    'lease-id',                  -- lease_id from consume
    '__QUEUE_MODE__',            -- consumer group (default)
    'completed',                 -- status: 'completed', 'failed', 'dlq'
    NULL                         -- error message (optional)
);
```

#### `queen.nack(txn_id, partition_id, lease_id, error_message, consumer_group)` -> `BOOLEAN`

Mark message for retry.

#### `queen.reject(txn_id, partition_id, lease_id, error_message, consumer_group)` -> `BOOLEAN`

Send message to Dead Letter Queue.

#### `queen.renew_one(lease_id, extend_seconds)` -> `TIMESTAMPTZ`

Extend lease. Returns new expiration or NULL if not found.

### Utility Functions

#### `queen.configure(queue, lease_time, retry_limit, dead_letter_queue)` -> `BOOLEAN`

```sql
SELECT queen.configure(
    'orders',    -- queue name
    60,          -- lease time seconds (default: 300)
    3,           -- retry limit (default: 3)
    TRUE         -- enable DLQ (default: false)
);
```

#### `queen.lag(queue, consumer_group)` -> `BIGINT`

Get pending message count.

#### `queen.has_messages(queue, consumer_group)` -> `BOOLEAN`

Check if queue has pending messages.

#### `queen.seek(consumer_group, queue, to_end, to_timestamp)` -> `BOOLEAN`

Reposition consumer group cursor.

#### `queen.delete_consumer_group(consumer_group, queue, delete_metadata)` -> `BOOLEAN`

Delete consumer group state.

#### `queen.forward(source_txn_id, source_partition_id, source_lease_id, source_consumer_group, dest_queue, dest_payload, dest_partition, dest_txn_id)` -> `UUID`

Atomically commit source and produce to destination.

#### `queen.channel_name(queue)` -> `TEXT`

Get NOTIFY channel name for a queue.

#### `queen.notify(queue, payload)` -> `VOID`

Send NOTIFY on queue channel.

## Testing

```bash
cd pg_qpubsub
./test_extension.sh
```

## Dependencies

- **pgcrypto** - Required for UUID v7 generation
- **Queen schema** - Core tables and procedures from `../lib/schema/`
