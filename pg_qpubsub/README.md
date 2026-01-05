# pg_qpubsub

PostgreSQL extension wrapper for Queen MQ - a simplified Kafka-style API for pub/sub message queuing directly in PostgreSQL.

## Features

- **Two-Layer API**: JSONB batch API for applications, scalar API for SQL
- **Produce/Consume/Commit**: Kafka-style queue operations
- **Consumer Groups**: Fan-out - multiple groups receive all messages
- **Subscription Modes**: `new` (skip history), `all` (from beginning), or timestamp
- **Lease Management**: Configured per-queue, with renewal support
- **Dead Letter Queue**: Failed messages preserved for inspection
- **UUID v7**: Time-ordered IDs with sub-millisecond precision

## Two-Layer API

### Primary API (JSONB)

For programmatic access from Node.js, Python, Go, etc.

| Function | Description |
|----------|-------------|
| `queen.produce(items JSONB)` | Batch produce messages |
| `queen.consume(requests JSONB)` | Batch consume messages |
| `queen.commit(acks JSONB)` | Batch acknowledge messages |
| `queen.renew(leases JSONB)` | Batch renew leases |
| `queen.transaction(ops JSONB)` | Atomic multi-operation |

### Convenience API (Scalar)

For SQL usage, psql, triggers, and simple cases.

| Function | Description |
|----------|-------------|
| `queen.produce_one(queue, payload, ...)` | Produce single message |
| `queen.consume_one(queue, consumer_group, ...)` | Consume messages |
| `queen.commit_one(txn_id, partition_id, lease_id, ...)` | Acknowledge message |
| `queen.renew_one(lease_id, extend_seconds)` | Renew single lease |
| `queen.nack(...)` | Mark for retry |
| `queen.reject(...)` | Send to DLQ |

## Quick Start

### Node.js (Primary API)

```javascript
import pg from 'pg'
const pool = new pg.Pool({ connectionString: DATABASE_URL })

// Batch produce
const items = [
  { queue: 'orders', payload: { orderId: 1 } },
  { queue: 'orders', payload: { orderId: 2 } }
]
await pool.query(`SELECT queen.produce($1::jsonb)`, [JSON.stringify(items)])

// Batch consume
const req = [{
  queue_name: 'orders',
  consumer_group: '__QUEUE_MODE__',
  batch_size: 10,
  lease_seconds: 60,
  worker_id: 'worker-1'
}]
const result = await pool.query(`SELECT queen.consume($1::jsonb)`, [JSON.stringify(req)])
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

### SQL (Convenience API)

```sql
-- Configure queue
SELECT queen.configure('orders', 60, 3, true);

-- Produce
SELECT queen.produce_one('orders', '{"orderId": 123}'::jsonb);

-- Consume
SELECT * FROM queen.consume_one('orders', '__QUEUE_MODE__', 10);

-- Commit (use values from consumed row)
SELECT queen.commit_one('txn-id', 'partition-uuid'::uuid, 'lease-id');
```

## API Reference

### Primary API (JSONB)

#### `queen.produce(p_items JSONB)` -> `JSONB`

```javascript
// Input
[{ "queue": "orders", "partition": "Default", "payload": {...}, "transactionId": "optional" }]
// Output
[{ "index": 0, "message_id": "uuid", "status": "queued" }]
```

#### `queen.consume(p_requests JSONB)` -> `JSONB`

```javascript
// Input
[{
  "queue_name": "orders",
  "partition_name": "",
  "consumer_group": "__QUEUE_MODE__",
  "batch_size": 10,
  "lease_seconds": 60,
  "worker_id": "worker-1",
  "auto_ack": false,
  "sub_mode": "",
  "sub_from": ""
}]
// Output
[{ "idx": 0, "result": { "success": true, "partitionId": "uuid", "messages": [...] }}]
```

#### `queen.commit(p_acks JSONB)` -> `JSONB`

```javascript
// Input
[{
  "transactionId": "...",
  "partitionId": "uuid",
  "leaseId": "worker-1",
  "consumerGroup": "__QUEUE_MODE__",
  "status": "completed",
  "errorMessage": null
}]
// Output
[{ "index": 0, "success": true }]
```

#### `queen.renew(p_leases JSONB)` -> `JSONB`

```javascript
// Input
[{ "leaseId": "worker-1", "extendSeconds": 60 }]
// Output
[{ "index": 0, "success": true, "expiresAt": "timestamp" }]
```

#### `queen.transaction(p_operations JSONB)` -> `JSONB`

Execute multiple operations atomically.

### Convenience API (Scalar)

#### `queen.produce_one(queue, payload, partition, transaction_id, notify)` -> `UUID`

#### `queen.consume_one(queue, consumer_group, batch_size, partition, timeout, auto_commit, subscription_mode)` -> `TABLE`

Returns: `partition_id`, `id`, `transaction_id`, `payload`, `created_at`, `lease_id`

#### `queen.commit_one(txn_id, partition_id, lease_id, consumer_group, status, error_message)` -> `BOOLEAN`

#### `queen.nack(txn_id, partition_id, lease_id, error_message, consumer_group)` -> `BOOLEAN`

Mark message for retry.

#### `queen.reject(txn_id, partition_id, lease_id, error_message, consumer_group)` -> `BOOLEAN`

Send message to DLQ.

#### `queen.renew_one(lease_id, extend_seconds)` -> `TIMESTAMPTZ`

Returns new expiration or NULL if not found.

### Utility Functions

```sql
queen.configure(queue, lease_time, retry_limit, dlq) -> BOOLEAN
queen.lag(queue, consumer_group) -> BIGINT
queen.has_messages(queue, consumer_group) -> BOOLEAN
queen.seek(consumer_group, queue, to_end, to_timestamp) -> BOOLEAN
queen.delete_consumer_group(consumer_group, queue, delete_metadata) -> BOOLEAN
queen.forward(source_txn, source_partition, source_lease, source_group, dest_queue, dest_payload, dest_partition, dest_txn) -> UUID
queen.channel_name(queue) -> TEXT
queen.notify(queue, payload) -> VOID
```

### UUID v7 Functions

```sql
queen.uuid_generate_v7() -> UUID
queen.uuid_v7_to_timestamptz(uuid) -> TIMESTAMPTZ
queen.uuid_v7_boundary(time) -> UUID
queen.uuid_generate_v7_at(time) -> UUID
```

## Installation

### Direct SQL Loading (Recommended)

```bash
cd pg_qpubsub
./build.sh
psql -d mydb -c "CREATE EXTENSION IF NOT EXISTS pgcrypto"
psql -d mydb -f pg_qpubsub--1.0.sql
```

### PostgreSQL Extension

```bash
./build.sh
make install
psql -d mydb -c "CREATE EXTENSION pg_qpubsub"
```

## Requirements

- PostgreSQL 14+
- `pgcrypto` extension
