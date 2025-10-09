# Partition-Level Locking Design (Simplified - No Worker ID)

## Core Concept
Each partition can be "leased" to exactly one consumer per consumer group at a time. We don't track WHO has the lease, only THAT the partition is locked for a specific consumer group. This simplifies the design for stateless consumers.

## Database Schema Changes

### 1. Updated Messages Table

```sql
-- Modify messages table with new fields
ALTER TABLE queen.messages 
    ALTER COLUMN transaction_id TYPE VARCHAR(255),  -- Change from UUID to string
    ADD COLUMN IF NOT EXISTS trace_id UUID DEFAULT gen_random_uuid();  -- For tracking multi-step pipelines

-- Add index for trace_id lookups
CREATE INDEX idx_messages_trace_id ON queen.messages(trace_id);

-- Add index for transaction_id (string now)
CREATE INDEX idx_messages_transaction_id ON queen.messages(transaction_id);
```

### 2. Queue Size Tracking and Limits

```sql
-- Add max_queue_size to queues table
ALTER TABLE queen.queues 
    ADD COLUMN IF NOT EXISTS max_queue_size INTEGER DEFAULT 0;  -- 0 = unlimited

-- Index for fast queue depth checks
CREATE INDEX idx_messages_status_pending_processing 
ON queen.messages_status(message_id, status) 
WHERE status IN ('pending', 'processing') OR status IS NULL;
```

### 3. New Table: partition_leases

```sql
CREATE TABLE IF NOT EXISTS queen.partition_leases (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',  -- '__QUEUE_MODE__' for queue mode
    lease_expires_at TIMESTAMPTZ NOT NULL,
    message_batch JSONB,  -- Store IDs of messages in this lease
    created_at TIMESTAMPTZ DEFAULT NOW(),
    released_at TIMESTAMPTZ,  -- When lease was released (NULL if active)
    UNIQUE(partition_id, consumer_group)  -- One active lease per partition per consumer group
);

-- Index for finding expired leases
CREATE INDEX idx_partition_leases_expires ON queen.partition_leases(lease_expires_at) 
WHERE released_at IS NULL;

-- Index for active leases
CREATE INDEX idx_partition_leases_active ON queen.partition_leases(partition_id, consumer_group) 
WHERE released_at IS NULL;
```

## Push Operation with Queue Size Limits

### Check Queue Capacity Before Push

```sql
-- Push with queue size check and new fields
WITH queue_check AS (
    -- Check if queue has capacity
    SELECT 
        q.id as queue_id,
        q.max_queue_size,
        COUNT(m.id) as current_depth,
        CASE 
            WHEN q.max_queue_size = 0 THEN true  -- Unlimited
            WHEN COUNT(m.id) < q.max_queue_size THEN true
            ELSE false
        END as has_capacity
    FROM queen.queues q
    LEFT JOIN queen.partitions p ON q.id = p.queue_id
    LEFT JOIN queen.messages m ON p.id = m.partition_id
    LEFT JOIN queen.messages_status ms ON m.id = ms.message_id 
        AND ms.consumer_group = '__QUEUE_MODE__'
    WHERE q.name = $1
        AND (ms.status IS NULL OR ms.status IN ('pending', 'processing'))
    GROUP BY q.id, q.max_queue_size
),
partition_select AS (
    -- Only proceed if queue has capacity
    SELECT 
        p.id as partition_id,
        qc.queue_id,
        qc.current_depth,
        qc.max_queue_size
    FROM queue_check qc
    JOIN queen.partitions p ON p.queue_id = qc.queue_id
    WHERE qc.has_capacity = true
        AND ($2 IS NULL OR p.name = $2)  -- Specific partition or default
    ORDER BY p.last_activity ASC  -- Round-robin distribution
    LIMIT 1
    FOR UPDATE OF p SKIP LOCKED
),
insert_message AS (
    INSERT INTO queen.messages (
        transaction_id,  -- Now a string
        trace_id,        -- New field for pipeline tracking
        partition_id,
        payload,
        is_encrypted
    )
    SELECT 
        $3,  -- transaction_id (string)
        $4,  -- trace_id (UUID, can be NULL for single messages)
        partition_id,
        $5,  -- payload
        $6   -- is_encrypted
    FROM partition_select
    RETURNING *
)
SELECT 
    im.*,
    ps.current_depth + 1 as new_depth,
    ps.max_queue_size,
    CASE 
        WHEN ps.max_queue_size > 0 THEN 
            ps.max_queue_size - (ps.current_depth + 1)
        ELSE NULL
    END as remaining_capacity
FROM insert_message im
CROSS JOIN partition_select ps;
```

### Batch Push with Capacity Check

```sql
-- Batch push with atomic capacity check
WITH queue_check AS (
    SELECT 
        q.id as queue_id,
        q.max_queue_size,
        COUNT(m.id) as current_depth,
        $2::integer as batch_size,  -- Number of messages to push
        CASE 
            WHEN q.max_queue_size = 0 THEN true  -- Unlimited
            WHEN COUNT(m.id) + $2 <= q.max_queue_size THEN true
            ELSE false
        END as has_capacity_for_batch
    FROM queen.queues q
    LEFT JOIN queen.partitions p ON q.id = p.queue_id
    LEFT JOIN queen.messages m ON p.id = m.partition_id
    LEFT JOIN queen.messages_status ms ON m.id = ms.message_id 
        AND ms.consumer_group = '__QUEUE_MODE__'
    WHERE q.name = $1
        AND (ms.status IS NULL OR ms.status IN ('pending', 'processing'))
    GROUP BY q.id, q.max_queue_size
),
check_result AS (
    SELECT * FROM queue_check
    WHERE has_capacity_for_batch = true
)
-- Only insert if capacity check passes
INSERT INTO queen.messages (transaction_id, trace_id, partition_id, payload, is_encrypted)
SELECT 
    unnest($3::text[]) as transaction_id,  -- Array of transaction IDs (strings)
    unnest($4::uuid[]) as trace_id,        -- Array of trace IDs
    p.id as partition_id,
    unnest($5::jsonb[]) as payload,
    unnest($6::boolean[]) as is_encrypted
FROM check_result cr
JOIN queen.partitions p ON p.queue_id = cr.queue_id
WHERE ($7 IS NULL OR p.name = $7);  -- Optional partition name
```

### Error Handling for Queue Full

```javascript
// In the push implementation
async function pushMessage(queue, message, options = {}) {
    const { partition, traceId } = options;
    
    try {
        const result = await db.query(pushQuery, [
            queue,
            partition,
            message.transactionId || generateTransactionId(),  // String ID
            traceId || null,  // Optional trace ID for pipeline tracking
            message.payload,
            message.isEncrypted || false
        ]);
        
        if (result.rows.length === 0) {
            throw new Error(`Queue '${queue}' is full. Max capacity reached.`);
        }
        
        return {
            messageId: result.rows[0].id,
            transactionId: result.rows[0].transaction_id,
            traceId: result.rows[0].trace_id,
            queueDepth: result.rows[0].new_depth,
            remainingCapacity: result.rows[0].remaining_capacity
        };
    } catch (error) {
        if (error.message.includes('is full')) {
            // Queue is at capacity
            throw new QueueFullError(queue, error.message);
        }
        throw error;
    }
}
```

## Pop Operation Flow

### Step 1: Acquire Partition Lease

```sql
WITH available_partitions AS (
    -- Find partitions with messages that don't have active leases
    SELECT DISTINCT p.id, p.name, MIN(m.created_at) as oldest_message
    FROM queen.partitions p
    JOIN queen.messages m ON m.partition_id = p.id
    LEFT JOIN queen.messages_status ms ON m.id = ms.message_id 
        AND ms.consumer_group = $consumer_group
    LEFT JOIN queen.partition_leases pl ON p.id = pl.partition_id 
        AND pl.consumer_group = $consumer_group
        AND pl.released_at IS NULL
        AND pl.lease_expires_at > NOW()
    WHERE p.queue_id = $queue_id
        AND pl.id IS NULL  -- No active lease
        AND (ms.id IS NULL OR ms.status IN ('pending', 'failed'))
    GROUP BY p.id, p.name
    ORDER BY oldest_message  -- Fairness: oldest message first
    LIMIT 1
    FOR UPDATE OF p SKIP LOCKED  -- Prevent race on partition selection
),
acquire_lease AS (
    INSERT INTO queen.partition_leases (
        partition_id, 
        consumer_group, 
        lease_expires_at
    )
    SELECT 
        id,
        $consumer_group,
        NOW() + INTERVAL '1 second' * $lease_time
    FROM available_partitions
    ON CONFLICT (partition_id, consumer_group) 
    WHERE released_at IS NULL
    DO UPDATE SET 
        lease_expires_at = EXCLUDED.lease_expires_at  -- Extend lease if somehow exists
    RETURNING partition_id, id as lease_id
)
SELECT * FROM acquire_lease;
```

### Step 2: Get Messages from Leased Partition

```sql
-- Now that we have a lease on partition_id, get messages
WITH messages_to_process AS (
    SELECT m.*
    FROM queen.messages m
    LEFT JOIN queen.messages_status ms ON m.id = ms.message_id 
        AND ms.consumer_group = $consumer_group
    WHERE m.partition_id = $partition_id  -- The partition we just leased
        AND (ms.id IS NULL OR ms.status IN ('pending', 'failed'))
    ORDER BY m.created_at ASC
    LIMIT $batch_size
    FOR UPDATE OF m  -- Lock these specific messages
),
update_lease AS (
    -- Store which messages are part of this lease
    UPDATE queen.partition_leases
    SET message_batch = (SELECT jsonb_agg(id) FROM messages_to_process)
    WHERE partition_id = $partition_id 
        AND consumer_group = $consumer_group
        AND released_at IS NULL
)
-- Insert status records and return messages
INSERT INTO queen.messages_status (message_id, consumer_group, status, lease_expires_at)
SELECT id, $consumer_group, 'processing', NOW() + INTERVAL '1 second' * $lease_time
FROM messages_to_process
ON CONFLICT (message_id, consumer_group) DO UPDATE
SET status = 'processing', lease_expires_at = EXCLUDED.lease_expires_at
RETURNING (SELECT json_agg(row_to_json(m)) FROM messages_to_process m);
```

### Step 3: ACK Releases the Partition Lease

```sql
-- When messages are ACKed, release the partition lease
WITH ack_messages AS (
    UPDATE queen.messages_status
    SET status = 'completed',
        completed_at = NOW()
    WHERE message_id = ANY($message_ids)
        AND consumer_group = $consumer_group
    RETURNING message_id
)
-- Release the partition lease
UPDATE queen.partition_leases
SET released_at = NOW()
WHERE partition_id = $partition_id
    AND consumer_group = $consumer_group
    AND released_at IS NULL;
```

## Lease Expiration Handling

### Background Process: Reclaim Expired Leases

```sql
-- Run periodically (every few seconds)
WITH expired_leases AS (
    UPDATE queen.partition_leases
    SET released_at = NOW()
    WHERE lease_expires_at < NOW()
        AND released_at IS NULL
    RETURNING partition_id, consumer_group, message_batch
)
-- Reset message status for messages in expired leases
UPDATE queen.messages_status ms
SET status = 'pending',
    retry_count = retry_count + 1,
    failed_at = NOW(),
    error_message = 'Partition lease expired'
FROM expired_leases el
WHERE ms.message_id = ANY(
    SELECT jsonb_array_elements_text(el.message_batch)::uuid
)
AND ms.consumer_group = el.consumer_group;
```

## Benefits of This Approach

### 1. **True Sequential Processing**
- Only one worker processes a partition at a time
- Messages K1, K2, K3 MUST complete before K4, K5, K6 can start
- Absolute FIFO guarantee

### 2. **Per Consumer Group Isolation**
- Queue mode: One worker per partition
- Bus mode: One worker per partition per consumer group
- Different consumer groups don't block each other

### 3. **Automatic Failover**
- If a worker dies, lease expires automatically
- Another worker can claim the partition
- Messages are retried in order

### 4. **Clear Ownership**
- partition_leases table shows exactly who owns what
- Easy to debug and monitor
- Can implement partition stealing for load balancing

## Trade-offs

### Pros:
- ✅ Absolute ordering guarantees
- ✅ No out-of-order processing possible
- ✅ Simple mental model
- ✅ Predictable behavior
- ✅ Works well for ordered event streams

### Cons:
- ❌ Lower throughput (one worker per partition)
- ❌ Potential for partition imbalance
- ❌ Slower processing if one partition has many messages
- ❌ Worker underutilization if fewer partitions than workers

## Implementation Priority

1. **Phase 1: Basic Partition Leasing**
   - Create partition_leases table
   - Implement lease acquisition in pop
   - Implement lease release in ACK
   - Add lease expiration handling

2. **Phase 2: Monitoring & Observability**
   - Add metrics for lease utilization
   - Track partition processing times
   - Monitor worker efficiency

## Simplified Flow (No Worker ID Needed!)

### The Key Insight
We don't need to track WHO has the partition, just THAT it's locked for a consumer group.

### Producer (Unchanged)
```javascript
// Messages go to partitions as before
await client.push('orders', {
    orderId: '123',
    action: 'create'
}, { partition: 'customer_456' });
```

### Consumer Flow

#### Step 1: Consumer A calls pop()
```javascript
const messages = await client.pop('orders', { batch: 10 });
```
**Behind the scenes:**
1. Find an available partition (no active lease for this consumer group)
2. Create lease: `(partition_K, consumer_group_X) -> locked until T+300s`
3. Return messages from partition K only

#### Step 2: Consumer B calls pop() (while A is processing)
```javascript
const messages = await client.pop('orders', { batch: 10 });
```
**Behind the scenes:**
1. Partition K is locked for consumer_group_X
2. Find a different available partition (e.g., Partition L)
3. Create lease: `(partition_L, consumer_group_X) -> locked until T+300s`
4. Return messages from partition L only

#### Step 3: Consumer A finishes and ACKs
```javascript
await client.ack(messages);
```
**Behind the scenes:**
1. Mark messages as completed
2. Release lease: `(partition_K, consumer_group_X) -> released`
3. Partition K is now available for any consumer

### The Beauty: No Worker ID Required!
- We only track: `(partition_id, consumer_group) -> lease_expires_at`
- Any consumer can acquire an available partition
- Any consumer can release a partition by ACKing its messages
- If consumer crashes, lease expires automatically

## Monitoring Queries

### Active Leases (Simplified - No Worker ID)
```sql
SELECT 
    q.name as queue_name,
    p.name as partition_name,
    pl.consumer_group,
    pl.lease_expires_at,
    jsonb_array_length(pl.message_batch) as message_count,
    EXTRACT(EPOCH FROM (pl.lease_expires_at - NOW())) as seconds_remaining
FROM queen.partition_leases pl
JOIN queen.partitions p ON pl.partition_id = p.id
JOIN queen.queues q ON p.queue_id = q.id
WHERE pl.released_at IS NULL
ORDER BY q.name, p.name;
```

### Partition Utilization
```sql
SELECT 
    q.name as queue_name,
    COUNT(DISTINCT p.id) as total_partitions,
    COUNT(DISTINCT pl.partition_id) as leased_partitions,
    ROUND(100.0 * COUNT(DISTINCT pl.partition_id) / NULLIF(COUNT(DISTINCT p.id), 0), 2) as utilization_pct
FROM queen.queues q
LEFT JOIN queen.partitions p ON q.id = p.queue_id
LEFT JOIN queen.partition_leases pl ON p.id = pl.partition_id 
    AND pl.released_at IS NULL
    AND pl.lease_expires_at > NOW()
GROUP BY q.name;
```

### Stuck Partitions (Leases About to Expire)
```sql
SELECT 
    q.name as queue_name,
    p.name as partition_name,
    pl.consumer_group,
    pl.lease_expires_at,
    pl.created_at as lease_started_at,
    EXTRACT(EPOCH FROM (NOW() - pl.created_at)) as lease_held_seconds
FROM queen.partition_leases pl
JOIN queen.partitions p ON pl.partition_id = p.id
JOIN queen.queues q ON p.queue_id = q.id
WHERE pl.released_at IS NULL
    AND pl.lease_expires_at < NOW() + INTERVAL '30 seconds'
ORDER BY pl.lease_expires_at;
```

## New Features: Transaction ID, Trace ID, and Queue Limits

### 1. Transaction ID as String
**Change:** `transaction_id` is now `VARCHAR(255)` instead of `UUID`

**Benefits:**
- More flexibility for client-generated IDs
- Support for external system IDs (order IDs, request IDs, etc.)
- Easier integration with existing systems
- Human-readable identifiers possible

**Example Usage:**
```javascript
await push('orders', {
    transactionId: 'ORDER-2024-001234',  // Meaningful string ID
    payload: { ... }
});
```

### 2. Trace ID for Pipeline Tracking
**New Field:** `trace_id UUID` for tracking messages across multi-step pipelines

**Use Cases:**
- Track related messages through multiple queues
- Correlate events in distributed systems
- Debug message flows in complex pipelines
- Audit trails for multi-step processes

**Example Pipeline:**
```javascript
const traceId = generateUUID();

// Step 1: Order received
await push('order-intake', {
    transactionId: 'ORDER-001',
    traceId: traceId,  // Same trace ID
    payload: { action: 'validate' }
});

// Step 2: Payment processing
await push('payment-queue', {
    transactionId: 'PAYMENT-001',
    traceId: traceId,  // Same trace ID links them
    payload: { action: 'charge' }
});

// Step 3: Fulfillment
await push('shipping-queue', {
    transactionId: 'SHIP-001',
    traceId: traceId,  // Same trace ID
    payload: { action: 'ship' }
});

// Query all messages in pipeline
SELECT * FROM queen.messages WHERE trace_id = $traceId;
```

### 3. Max Queue Size Limits
**New Field:** `max_queue_size INTEGER` on queues table

**Features:**
- Prevent queue overflow
- Backpressure mechanism
- Resource protection
- Configurable per queue (0 = unlimited)

**Configuration:**
```sql
-- Set max size for a queue
UPDATE queen.queues 
SET max_queue_size = 10000 
WHERE name = 'high-volume-queue';

-- Check current capacity
SELECT 
    queue_name,
    current_depth,
    max_queue_size,
    remaining_capacity
FROM queen.queue_depths
WHERE queue_name = 'high-volume-queue';
```

**Error Handling:**
```javascript
try {
    await push('limited-queue', message);
} catch (error) {
    if (error.code === 'QUEUE_FULL') {
        // Implement backpressure strategy
        await delay(1000);
        await retryWithExponentialBackoff();
    }
}
```

## Summary: Why This Design is Right

### The Problem We're Solving
- Current system allows multiple workers to get messages from the same partition
- Messages can complete out of order (Worker B finishes K4-K6 before Worker A finishes K1-K3)
- This breaks FIFO guarantees within partitions

### The Solution: Partition Leasing (Without Worker ID)
1. **One consumer at a time per partition per consumer group**
2. **Track only (partition, consumer_group) -> lease_expires_at**
3. **No need to track which specific worker/process has the lease**

### Why No Worker ID?
- **Stateless consumers**: Any consumer instance can handle any message
- **Simpler design**: Just track if partition is locked, not who has it
- **Easier scaling**: Add/remove consumer instances without registration
- **Natural failover**: Lease expires, any consumer can claim it

### The Flow
1. Consumer calls `pop()` → System finds unlocked partition → Creates lease
2. Consumer gets messages from ONLY that partition (true FIFO)
3. Consumer calls `ack()` → Lease released → Partition available again
4. If consumer crashes → Lease expires automatically → Another consumer takes over

### Key Benefits
- ✅ **Absolute FIFO within partitions**: Messages K1-K3 MUST complete before K4-K6 can start
- ✅ **Simple state management**: Just (partition, consumer_group, lease_expires_at)
- ✅ **Automatic recovery**: Lease expiration handles failures
- ✅ **Per-consumer-group isolation**: Different groups don't interfere

### Trade-offs Accepted
- ❌ Lower throughput (one consumer per partition)
- ❌ But this is REQUIRED for true FIFO ordering
- ❌ Mitigated by having multiple partitions

This design ensures **correctness over performance** - exactly what's needed for ordered message processing.

## Additional Query Improvements to Implement

### 1. Security and Transaction Hardening

#### Whitelist Transaction Isolation Levels
```javascript
// In database/connection.js
const VALID_ISOLATION_LEVELS = [
  'READ COMMITTED',
  'REPEATABLE READ', 
  'SERIALIZABLE'
];

export const withTransaction = async (pool, callback, isolationLevel = 'READ COMMITTED') => {
  if (!VALID_ISOLATION_LEVELS.includes(isolationLevel)) {
    throw new Error(`Invalid isolation level: ${isolationLevel}`);
  }
  
  const client = await pool.connect();
  try {
    await client.query('BEGIN');
    await client.query(`SET TRANSACTION ISOLATION LEVEL ${isolationLevel}`);
    
    // Set timeouts to prevent long blocks
    await client.query('SET LOCAL statement_timeout = 30000'); // 30 seconds
    await client.query('SET LOCAL lock_timeout = 5000'); // 5 seconds
    
    const result = await callback(client);
    await client.query('COMMIT');
    return result;
  } catch (error) {
    await client.query('ROLLBACK');
    
    // Retry on serialization failures and deadlocks
    if (error.code === '40001' || error.code === '40P01') {
      // Implement exponential backoff retry
      return retryTransaction(pool, callback, isolationLevel);
    }
    throw error;
  } finally {
    client.release();
  }
};
```

#### Retry Logic for Contention
```javascript
const retryTransaction = async (pool, callback, isolationLevel, attempt = 1, maxAttempts = 3) => {
  if (attempt > maxAttempts) {
    throw new Error('Max transaction retry attempts exceeded');
  }
  
  // Exponential backoff: 100ms, 200ms, 400ms
  const delay = Math.min(100 * Math.pow(2, attempt - 1), 1000);
  await new Promise(resolve => setTimeout(resolve, delay));
  
  try {
    return await withTransaction(pool, callback, isolationLevel);
  } catch (error) {
    if ((error.code === '40001' || error.code === '40P01') && attempt < maxAttempts) {
      return retryTransaction(pool, callback, isolationLevel, attempt + 1, maxAttempts);
    }
    throw error;
  }
};
```

### 2. Optimized Pop Query with Partition Leasing

#### Complete Pop Implementation with All Improvements
```sql
-- Pop with partition leasing, optimized predicates, and clean RETURNING
WITH queue_config AS (
    -- Precompute time thresholds once
    SELECT 
        id as queue_id,
        delayed_processing,
        max_wait_time_seconds,
        window_buffer,
        lease_time,
        NOW() - INTERVAL '1 second' * delayed_processing as earliest_available,
        NOW() - INTERVAL '1 second' * max_wait_time_seconds as latest_created,
        NOW() - INTERVAL '1 second' * window_buffer as window_cutoff
    FROM queen.queues
    WHERE name = $1
),
available_partitions AS (
    -- Find partitions with available messages and no active lease
    SELECT DISTINCT 
        p.id, 
        p.name,
        MIN(m.created_at) as oldest_message,
        COUNT(m.id) as pending_count
    FROM queen.partitions p
    JOIN queue_config qc ON p.queue_id = qc.queue_id
    JOIN queen.messages m ON m.partition_id = p.id
    WHERE NOT EXISTS (
        -- No active lease for this partition/consumer group
        SELECT 1 FROM queen.partition_leases pl
        WHERE pl.partition_id = p.id 
            AND pl.consumer_group = $2
            AND pl.released_at IS NULL
            AND pl.lease_expires_at > NOW()
    )
    AND m.created_at <= qc.earliest_available
    AND (qc.max_wait_time_seconds = 0 OR m.created_at > qc.latest_created)
    AND NOT EXISTS (
        -- No messages in window buffer
        SELECT 1 FROM queen.messages m2
        WHERE m2.partition_id = p.id
            AND qc.window_buffer > 0
            AND m2.created_at > qc.window_cutoff
    )
    AND NOT EXISTS (
        -- Message not already processed/processing
        SELECT 1 FROM queen.messages_status ms
        WHERE ms.message_id = m.id
            AND ms.consumer_group = $2
            AND ms.status NOT IN ('pending', 'failed')
    )
    GROUP BY p.id, p.name
    ORDER BY 
        oldest_message ASC,  -- Fairness: oldest first
        pending_count DESC   -- Then by most messages
    LIMIT 1
    FOR UPDATE OF p SKIP LOCKED
),
acquire_lease AS (
    INSERT INTO queen.partition_leases (
        partition_id, 
        consumer_group, 
        lease_expires_at
    )
    SELECT 
        id,
        $2,
        NOW() + INTERVAL '1 second' * (SELECT lease_time FROM queue_config)
    FROM available_partitions
    ON CONFLICT (partition_id, consumer_group) DO NOTHING
    RETURNING partition_id, id as lease_id, lease_expires_at
),
messages_to_process AS (
    SELECT 
        m.id,
        m.transaction_id,  -- Now a string
        m.trace_id,        -- New field for pipeline tracking
        m.payload,
        m.is_encrypted,
        m.created_at,
        p.name as partition_name,
        q.name as queue_name,
        al.lease_id,
        al.lease_expires_at
    FROM acquire_lease al
    JOIN queen.messages m ON m.partition_id = al.partition_id
    JOIN queen.partitions p ON p.id = al.partition_id
    JOIN queen.queues q ON q.id = p.queue_id
    JOIN queue_config qc ON q.id = qc.queue_id
    WHERE NOT EXISTS (
        SELECT 1 FROM queen.messages_status ms
        WHERE ms.message_id = m.id
            AND ms.consumer_group = $2
            AND ms.status NOT IN ('pending', 'failed')
    )
    AND m.created_at <= qc.earliest_available
    AND (qc.max_wait_time_seconds = 0 OR m.created_at > qc.latest_created)
    ORDER BY m.created_at ASC
    LIMIT $3
    FOR UPDATE OF m SKIP LOCKED
),
update_lease AS (
    UPDATE queen.partition_leases
    SET message_batch = (SELECT jsonb_agg(id) FROM messages_to_process)
    WHERE id = (SELECT lease_id FROM messages_to_process LIMIT 1)
),
insert_status AS (
    INSERT INTO queen.messages_status (
        message_id, 
        consumer_group, 
        status, 
        lease_expires_at,
        processing_at
    )
    SELECT 
        id,
        $2,
        'processing',
        lease_expires_at,
        NOW()
    FROM messages_to_process
    ON CONFLICT (message_id, consumer_group) 
    DO UPDATE SET 
        status = 'processing',
        lease_expires_at = EXCLUDED.lease_expires_at,
        processing_at = EXCLUDED.processing_at,
        retry_count = queen.messages_status.retry_count
)
-- Clean RETURNING without correlated subqueries
SELECT 
    id as message_id,
    transaction_id,  -- String now
    trace_id,        -- For pipeline tracking
    payload,
    is_encrypted,
    created_at,
    partition_name,
    queue_name
FROM messages_to_process;
```

### 3. Optimized ACK with Partition Release

```sql
-- ACK messages and release partition lease
WITH ack_messages AS (
    UPDATE queen.messages_status
    SET 
        status = 'completed',
        completed_at = NOW()
    WHERE message_id = ANY($1::uuid[])
        AND consumer_group = $2
        AND status = 'processing'
    RETURNING message_id
),
get_partition AS (
    -- Find which partition these messages belong to
    SELECT DISTINCT m.partition_id
    FROM queen.messages m
    WHERE m.id = ANY(SELECT message_id FROM ack_messages)
    LIMIT 1
)
-- Release the partition lease
UPDATE queen.partition_leases pl
SET released_at = NOW()
FROM get_partition gp
WHERE pl.partition_id = gp.partition_id
    AND pl.consumer_group = $2
    AND pl.released_at IS NULL
RETURNING pl.partition_id;
```

### 4. Keyset Pagination for listMessages API

```sql
-- Keyset pagination using (created_at, id) for stable ordering
CREATE INDEX idx_messages_pagination ON queen.messages(created_at DESC, id DESC);

-- First page
SELECT * FROM queen.messages
WHERE partition_id = $partition_id
ORDER BY created_at DESC, id DESC
LIMIT $limit;

-- Next pages (pass last_created_at and last_id from previous page)
SELECT * FROM queen.messages
WHERE partition_id = $partition_id
  AND (created_at, id) < ($last_created_at, $last_id)
ORDER BY created_at DESC, id DESC
LIMIT $limit;
```

### 5. Improved Retention Service

```sql
-- Retention that respects schema v2 and doesn't delete unprocessed messages
WITH retention_candidates AS (
    SELECT 
        m.id,
        m.partition_id,
        q.retention_seconds,
        q.completed_retention_seconds
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.retention_enabled = true
        AND (
            -- Completed messages past completed retention
            EXISTS (
                SELECT 1 FROM queen.messages_status ms
                WHERE ms.message_id = m.id
                    AND ms.status = 'completed'
                    AND ms.completed_at < NOW() - INTERVAL '1 second' * q.completed_retention_seconds
            )
            OR
            -- Any messages past general retention (if not processing)
            (
                m.created_at < NOW() - INTERVAL '1 second' * q.retention_seconds
                AND NOT EXISTS (
                    SELECT 1 FROM queen.messages_status ms
                    WHERE ms.message_id = m.id
                        AND ms.status IN ('processing', 'pending')
                )
            )
        )
),
deleted AS (
    DELETE FROM queen.messages
    WHERE id IN (SELECT id FROM retention_candidates)
    RETURNING id, partition_id
)
-- Log retention activity
INSERT INTO queen.retention_history (queue_id, partition_id, messages_deleted, deleted_at)
SELECT 
    p.queue_id,
    d.partition_id,
    COUNT(*),
    NOW()
FROM deleted d
JOIN queen.partitions p ON d.partition_id = p.id
GROUP BY p.queue_id, d.partition_id;
```

### 6. Performance Indexes

```sql
-- Critical indexes for the new design
CREATE INDEX idx_partition_leases_active_lookup 
ON queen.partition_leases(partition_id, consumer_group, lease_expires_at) 
WHERE released_at IS NULL;

CREATE INDEX idx_messages_partition_created 
ON queen.messages(partition_id, created_at ASC);

CREATE INDEX idx_messages_status_lookup 
ON queen.messages_status(message_id, consumer_group, status);

CREATE INDEX idx_messages_status_processing 
ON queen.messages_status(consumer_group, status) 
WHERE status = 'processing';

-- Partial index for finding available messages
CREATE INDEX idx_messages_available 
ON queen.messages(partition_id, created_at) 
WHERE id NOT IN (
    SELECT message_id FROM queen.messages_status 
    WHERE status NOT IN ('pending', 'failed')
);
```

## Implementation Checklist

### Phase 1: Schema Changes
- [ ] Alter messages table: change transaction_id to VARCHAR(255)
- [ ] Add trace_id UUID column to messages table
- [ ] Add max_queue_size column to queues table
- [ ] Create partition_leases table
- [ ] Add all required indexes

### Phase 2: Core Functionality
- [ ] Implement push with queue size checking
- [ ] Add transaction hardening with timeouts
- [ ] Implement retry logic for deadlocks
- [ ] Rewrite pop logic with partition leasing
- [ ] Update ACK to release partition leases
- [ ] Implement lease expiration background job

### Phase 3: Query Optimization
- [ ] Optimize predicates with NOT EXISTS
- [ ] Precompute time thresholds in CTEs
- [ ] Clean up RETURNING clauses
- [ ] Implement keyset pagination
- [ ] Add performance indexes

### Phase 4: Supporting Features
- [ ] Update retention service for new schema
- [ ] Add trace_id tracking utilities
- [ ] Implement queue capacity monitoring
- [ ] Add backpressure handling in client SDK

### Phase 5: Testing & Documentation
- [ ] Test partition leasing under load
- [ ] Test queue capacity limits
- [ ] Test trace_id pipeline tracking
- [ ] Test string transaction_ids
- [ ] Update API documentation
- [ ] Write migration guide
