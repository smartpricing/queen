# pg_qpubsub

A PostgreSQL extension providing a high-performance message queue with partitions, consumer groups, dead-letter queues, and real-time analytics.

## Features

- ✅ **Push/Pop/Ack** — Standard queue operations
- ✅ **Batch Operations** — Process hundreds of messages per call
- ✅ **Partitions** — Parallel processing with ordering guarantees
- ✅ **Consumer Groups** — Pub/Sub pattern (multiple groups receive same messages)
- ✅ **At-Least-Once Delivery** — Messages redelivered if not acknowledged
- ✅ **Dead Letter Queue** — Failed messages preserved for inspection
- ✅ **NOTIFY/LISTEN** — Real-time notifications on new messages
- ✅ **Long Polling** — Blocking pop with configurable timeout
- ✅ **Transactional Pipelines** — Ack + Push atomically
- ✅ **Real-time Analytics** — Throughput, lag, queue depth metrics

## Installation

There are two ways to install pg_qpubsub:

### Option 1: Direct SQL Loading (Recommended)

**No extension installation required.** Works on managed databases (AWS RDS, Google Cloud SQL, Azure, etc.).

```bash
# 1. Build the SQL file
./build.sh

# 2. Load pgcrypto (required dependency)
PGPASSWORD=postgres psql -U postgres -h localhost -d mydb -c "CREATE EXTENSION IF NOT EXISTS pgcrypto"

# 3. Load pg_qpubsub directly
PGPASSWORD=postgres psql -U postgres -h localhost -d mydb -f pg_qpubsub--1.0.sql
```

That's it! All functions are now available in the `queen` schema.

### Option 2: PostgreSQL Extension

Requires PostgreSQL dev headers and superuser access.

```bash
# Build
./build.sh

# Install to PostgreSQL
make install

# Enable in database
psql -d mydb -c "CREATE EXTENSION pg_qpubsub"
```

### Which Option Should I Use?

| Scenario | Recommended |
|----------|-------------|
| AWS RDS, Cloud SQL, Azure DB | **Direct SQL** (extensions often blocked) |
| Local development | Either works |
| Self-hosted PostgreSQL | Either works |
| Need `ALTER EXTENSION UPDATE` | PostgreSQL Extension |
| CI/CD pipelines | **Direct SQL** (simpler) |

## Quick Start

### Basic Usage

```sql
-- Configure a queue (optional - queues auto-create on first push)
SELECT queen.configure('orders', 
    p_lease_time := 60,        -- 60 second lease
    p_retry_limit := 3,        -- Retry failed messages 3 times
    p_dead_letter_queue := true -- Enable DLQ for failed messages
);

-- Push a message
SELECT queen.push('orders', '{"orderId": 123}'::jsonb);

-- Pop messages (queue mode)
CREATE TEMP TABLE popped AS SELECT * FROM queen.pop_batch('orders', 10, 60);

-- Process your messages...
SELECT payload FROM popped;

-- Acknowledge all messages
SELECT queen.ack(transaction_id, partition_id, lease_id) FROM popped;

-- Or with consumer groups (pub/sub mode):
SELECT * FROM queen.pop('orders', 'processor-group', 10, 60);
```

### With Notifications

```sql
-- Push with notification (consumers listening will wake up)
SELECT queen.push_notify('orders', '{"orderId": 123}'::jsonb);

-- Or use long polling (blocks until messages arrive or timeout)
SELECT * FROM queen.pop_wait('orders', 'processor', 10, 60, 30);
```

## API Reference

### Push Functions

#### `queen.push(queue, payload, [transaction_id])` → `UUID`
Push a message to a queue.
```sql
queen.push(
    p_queue TEXT,
    p_payload JSONB,
    p_transaction_id TEXT DEFAULT NULL
) RETURNS UUID
```

#### `queen.push(queue, partition, payload, [transaction_id])` → `UUID`
Push to a specific partition.
```sql
queen.push(
    p_queue TEXT,
    p_partition TEXT,
    p_payload JSONB,
    p_transaction_id TEXT DEFAULT NULL
) RETURNS UUID
```

#### `queen.push_notify(queue, payload, [transaction_id], [partition])` → `UUID`
Push message and send NOTIFY to wake consumers.
```sql
queen.push_notify(
    p_queue TEXT,
    p_payload JSONB,
    p_transaction_id TEXT DEFAULT NULL,
    p_partition TEXT DEFAULT 'Default'
) RETURNS UUID
```

#### `queen.push_notify(queue, partition, payloads[])` → `UUID[]`
Batch push with single NOTIFY.
```sql
queen.push_notify(
    p_queue TEXT,
    p_partition TEXT,
    p_payloads JSONB[]
) RETURNS UUID[]
```

#### `queen.push_full(...)` → `TABLE(message_id, transaction_id)`
Push with all options (namespace, task, delay).
```sql
queen.push_full(
    p_queue TEXT,
    p_payload JSONB,
    p_partition TEXT DEFAULT 'Default',
    p_transaction_id TEXT DEFAULT NULL,
    p_namespace TEXT DEFAULT NULL,
    p_task TEXT DEFAULT NULL,
    p_delay_until TIMESTAMPTZ DEFAULT NULL
) RETURNS TABLE(message_id UUID, transaction_id TEXT)
```

---

### Pop Functions

#### `queen.pop(queue, [consumer_group], [batch_size], [lease_seconds])` → `TABLE`
Pop messages from a queue.
```sql
queen.pop(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INTEGER DEFAULT 1,
    p_lease_seconds INTEGER DEFAULT 60
) RETURNS TABLE(partition_id UUID, id UUID, transaction_id TEXT, payload JSONB, created_at TIMESTAMPTZ, lease_id TEXT)
```

#### `queen.pop(queue, partition, consumer_group, batch_size, [lease_seconds])` → `TABLE`
Pop from a specific partition.
```sql
queen.pop(
    p_queue TEXT,
    p_partition TEXT,
    p_consumer_group TEXT,
    p_batch_size INTEGER,
    p_lease_seconds INTEGER DEFAULT 60
) RETURNS TABLE(partition_id UUID, id UUID, transaction_id TEXT, payload JSONB, created_at TIMESTAMPTZ, lease_id TEXT)
```

#### `queen.pop_one(queue, [consumer_group], [lease_seconds])` → `TABLE`
Pop a single message.
```sql
queen.pop_one(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_lease_seconds INTEGER DEFAULT 60
) RETURNS TABLE(partition_id UUID, id UUID, transaction_id TEXT, payload JSONB, created_at TIMESTAMPTZ, lease_id TEXT)
```

#### `queen.pop_batch(queue, batch_size, [lease_seconds])` → `TABLE`
Pop a batch of messages (queue mode, no consumer group required).
```sql
queen.pop_batch(
    p_queue TEXT,
    p_batch_size INTEGER,
    p_lease_seconds INTEGER DEFAULT 60
) RETURNS TABLE(partition_id UUID, id UUID, transaction_id TEXT, payload JSONB, created_at TIMESTAMPTZ, lease_id TEXT)
```

#### `queen.pop_auto_ack(queue, [consumer_group], [batch_size])` → `TABLE`
Pop with automatic acknowledgment (fire-and-forget).
```sql
queen.pop_auto_ack(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INTEGER DEFAULT 1
) RETURNS TABLE(partition_id UUID, id UUID, transaction_id TEXT, payload JSONB, created_at TIMESTAMPTZ, lease_id TEXT)
```

#### `queen.pop_wait(queue, [consumer_group], [batch_size], [lease_seconds], [timeout_seconds])` → `TABLE`
Long polling - blocks until messages arrive or timeout.
```sql
queen.pop_wait(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_batch_size INTEGER DEFAULT 1,
    p_lease_seconds INTEGER DEFAULT 60,
    p_timeout_seconds INTEGER DEFAULT 30
) RETURNS TABLE(partition_id UUID, id UUID, transaction_id TEXT, payload JSONB, created_at TIMESTAMPTZ, lease_id TEXT)
```

#### `queen.pop_wait_one(queue, [consumer_group], [lease_seconds], [timeout_seconds])` → `TABLE`
Long poll for a single message.
```sql
queen.pop_wait_one(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_lease_seconds INTEGER DEFAULT 60,
    p_timeout_seconds INTEGER DEFAULT 30
) RETURNS TABLE(partition_id UUID, id UUID, transaction_id TEXT, payload JSONB, created_at TIMESTAMPTZ, lease_id TEXT)
```

---

### Ack Functions

#### `queen.ack(transaction_id, partition_id, lease_id)` → `BOOLEAN`
Acknowledge successful processing (queue mode).
```sql
queen.ack(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT
) RETURNS BOOLEAN
```

#### `queen.ack_group(transaction_id, partition_id, lease_id, consumer_group)` → `BOOLEAN`
Acknowledge successful processing (pub/sub mode with explicit consumer group).
```sql
queen.ack_group(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT,
    p_consumer_group TEXT
) RETURNS BOOLEAN
```

#### `queen.ack_status(transaction_id, partition_id, lease_id, status, [consumer_group], [error_message])` → `BOOLEAN`
Acknowledge with explicit status ('completed', 'failed', 'retry', 'dlq').
```sql
queen.ack_status(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT,
    p_status TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__',
    p_error_message TEXT DEFAULT NULL
) RETURNS BOOLEAN
```

#### `queen.nack(transaction_id, partition_id, lease_id, [error_message], [consumer_group])` → `BOOLEAN`
Mark as failed - message will be retried.
```sql
queen.nack(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT,
    p_error_message TEXT DEFAULT 'Processing failed',
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
) RETURNS BOOLEAN
```

#### `queen.reject(transaction_id, partition_id, lease_id, [error_message], [consumer_group])` → `BOOLEAN`
Send directly to Dead Letter Queue (skip retries).
```sql
queen.reject(
    p_transaction_id TEXT,
    p_partition_id UUID,
    p_lease_id TEXT,
    p_error_message TEXT DEFAULT 'Rejected',
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
) RETURNS BOOLEAN
```

---

### Utility Functions

#### `queen.configure(queue, [lease_time], [retry_limit], [dead_letter_queue])` → `BOOLEAN`
Configure queue settings.
```sql
queen.configure(
    p_queue TEXT,
    p_lease_time INTEGER DEFAULT 300,
    p_retry_limit INTEGER DEFAULT 3,
    p_dead_letter_queue BOOLEAN DEFAULT FALSE
) RETURNS BOOLEAN
```

#### `queen.has_messages(queue, [partition], [consumer_group])` → `BOOLEAN`
Check if queue has pending messages.
```sql
queen.has_messages(
    p_queue TEXT,
    p_partition TEXT DEFAULT NULL,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
) RETURNS BOOLEAN
```

#### `queen.depth(queue, [consumer_group])` → `BIGINT`
Get approximate queue depth.
```sql
queen.depth(
    p_queue TEXT,
    p_consumer_group TEXT DEFAULT '__QUEUE_MODE__'
) RETURNS BIGINT
```

#### `queen.renew(lease_id, [extend_seconds])` → `TIMESTAMPTZ`
Extend an active lease. Returns new expiration time or NULL if lease not found.
```sql
queen.renew(
    p_lease_id TEXT,
    p_extend_seconds INTEGER DEFAULT 60
) RETURNS TIMESTAMPTZ
```

#### `queen.forward(...)` → `UUID`
Atomically ack source message and push to destination queue.
```sql
queen.forward(
    p_source_transaction_id TEXT,
    p_source_partition_id UUID,
    p_source_lease_id TEXT,
    p_source_consumer_group TEXT,
    p_dest_queue TEXT,
    p_dest_payload JSONB,
    p_dest_partition TEXT DEFAULT 'Default',
    p_dest_transaction_id TEXT DEFAULT NULL
) RETURNS UUID
```

---

### Notification Functions

#### `queen.notify(queue, [payload])` → `VOID`
Send NOTIFY on queue channel.
```sql
queen.notify(
    p_queue TEXT,
    p_payload TEXT DEFAULT ''
) RETURNS VOID
```

#### `queen.channel_name(queue)` → `TEXT`
Get the NOTIFY channel name for a queue.
```sql
queen.channel_name(p_queue TEXT) RETURNS TEXT
-- Example: queen.channel_name('orders') → 'queen_orders'
```

## Testing

```bash
# Set database credentials
export PGPASSWORD=postgres
export PGUSER=postgres
export PGHOST=localhost

# Run tests
./test_extension.sh
```

## Directory Structure

```
pg_qpubsub/
├── pg_qpubsub.control     # Extension metadata
├── pg_qpubsub--1.0.sql    # Built extension (generated)
├── wrappers.sql           # Simplified API wrappers
├── build.sh               # Build script
├── test_extension.sh      # Test runner
├── Makefile               # PGXS makefile
├── README.md              # This file
└── test/
    └── sql/               # Test files
        ├── 01_setup.sql
        ├── 02_push.sql
        ├── 03_pop.sql
        ├── 04_ack.sql
        ├── 05_notify.sql
        └── 06_long_poll.sql
```

## Dependencies

- **pgcrypto** — PostgreSQL extension for `gen_random_bytes()` (UUID v7 generation)
- **Queen schema** — Core tables and procedures from `../lib/schema/`

> **Note:** `pgcrypto` is included by default in PostgreSQL and available on most managed database platforms (RDS, Cloud SQL, etc.). Just run `CREATE EXTENSION pgcrypto` before loading pg_qpubsub.

## License

MIT

