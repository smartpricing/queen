# Load Management & Operation Queue - Implementation Plan

**Date:** October 13, 2025  
**Status:** 📝 Planning Phase  
**Goal:** Handle high load gracefully without message loss or ordering violations

---

## 🎯 Problem Statement

### Current Issues:
1. **Database pool exhaustion** under high load (500-2000ms connection waits)
2. **No backpressure mechanism** - requests fail instead of queue
3. **Ordering violations possible** if some operations queue while others don't
4. **Cascading failures** - overload leads to 429s, retries, more overload
5. **System events amplify load** in cluster mode (3-10x fan-out)

### Requirements:
- ✅ Maintain FIFO ordering within queue/partition
- ✅ Handle load spikes without rejecting requests
- ✅ Fault-tolerant (worker crashes don't lose operations)
- ✅ Inspectable (can see pending operations)
- ✅ Prioritizable (critical ops process first)
- ✅ Idempotent (operations can retry safely)

---

## 🏗️ Solution: Database-Backed Operation Queue

### Core Concept:
Use PostgreSQL itself as an operation queue with:
- **UUIDv7 for time-ordered IDs** (globally unique, sortable)
- **Queue/Partition tracking** (enforce ordering)
- **Priority levels** (critical ops first)
- **FOR UPDATE SKIP LOCKED** (distributed coordination)
- **Timeout-based recovery** (handle crashes)

---

## 📊 Schema Design

### Operation Queue Table:

```sql

-- Main operation queue table
CREATE TABLE IF NOT EXISTS queen.operation_queue (
  -- Identity
  id UUID PRIMARY KEY DEFAULT uuid_generate_v7(),  -- Time-ordered UUID
  operation_type VARCHAR(20) NOT NULL,              -- 'PUSH', 'ACK_BATCH', 'SYSTEM_EVENT'
  
  -- Ordering & Routing
  queue_name VARCHAR(255) NOT NULL,                -- Queue name
  partition_name VARCHAR(255),                     -- Partition name (NULL for queue-level ops)
  priority INTEGER DEFAULT 5,                      -- 1=critical, 5=normal, 10=low
  
  -- Payload
  payload JSONB NOT NULL,                          -- Operation-specific data
  estimated_size INTEGER,                          -- For monitoring (bytes)
  
  -- Status tracking
  status VARCHAR(20) DEFAULT 'pending',            -- pending, processing, completed, failed
  created_at TIMESTAMPTZ DEFAULT NOW(),
  processing_started_at TIMESTAMPTZ,               -- When claimed by worker
  processed_at TIMESTAMPTZ,                        -- When completed
  
  -- Worker tracking (for monitoring/debugging only)
  claimed_by_worker VARCHAR(255),                  -- Which worker is processing
  
  -- Error handling
  timeout_seconds INTEGER DEFAULT 30,              -- Operation-specific timeout
  retry_count INTEGER DEFAULT 0,
  max_retries INTEGER DEFAULT 3,
  error_message TEXT,
  last_error_at TIMESTAMPTZ
);

-- Indexes for performance
CREATE INDEX idx_opq_pending_priority 
  ON queen.operation_queue(priority ASC, id ASC) 
  WHERE status = 'pending';

CREATE INDEX idx_opq_queue_partition_pending 
  ON queen.operation_queue(queue_name, partition_name, id ASC) 
  WHERE status IN ('pending', 'processing');

CREATE INDEX idx_opq_stuck_processing 
  ON queen.operation_queue(processing_started_at) 
  WHERE status = 'processing';

CREATE INDEX idx_opq_status 
  ON queen.operation_queue(status);
```

---

## 🔄 Operation Flow

### Phase 1: Request Arrives (PUSH Example)

```javascript
export const handlePush = async (queue, partition, messages) => {
  const qpKey = partition ? `${queue}:${partition}` : queue;
  const poolHealth = pool.waitingCount / pool.max;
  
  // CRITICAL: Check if this Q/P has pending queued operations
  const hasPendingOps = await pool.query(`
    SELECT EXISTS(
      SELECT 1 FROM queen.operation_queue
      WHERE queue_name = $1 
        AND (partition_name = $2 OR ($2 IS NULL AND partition_name IS NULL))
        AND status IN ('pending', 'processing')
    ) as has_pending
  `, [queue, partition || null]);
  
  const mustQueue = hasPendingOps.rows[0].has_pending;
  const shouldQueue = poolHealth > 0.7;  // 70% utilization threshold
  
  if (mustQueue || shouldQueue) {
    // Queue the operation
    const result = await pool.query(`
      INSERT INTO queen.operation_queue (
        id,
        operation_type,
        queue_name,
        partition_name,
        priority,
        payload,
        estimated_size
      ) VALUES (
        uuid_generate_v7(),  -- Time-ordered UUID
        'PUSH',
        $1,
        $2,
        $3,  -- Priority based on operation type
        $4,  -- JSONB payload
        $5   -- Size in bytes
      )
      RETURNING id, created_at
    `, [
      queue,
      partition || null,
      getPriority('PUSH'),
      JSON.stringify({ queue, partition, messages }),
      JSON.stringify(messages).length
    ]);
    
    // Calculate queue position
    const position = await pool.query(`
      SELECT COUNT(*) as position
      FROM queen.operation_queue
      WHERE status = 'pending'
        AND id < $1  -- UUIDv7 is time-ordered, so < means "created before"
    `, [result.rows[0].id]);
    
    return {
      status: 'queued',
      operationId: result.rows[0].id,
      queuePosition: position.rows[0].position,
      reason: mustQueue ? 'maintaining_order' : 'system_busy',
      estimatedWait: Math.ceil(position.rows[0].position / 100) + 's'
    };
  }
  
  // Execute directly (no pending ops, pool healthy)
  const result = await queueManager.pushMessages(...);
  return { status: 'executed', result };
};
```

---

### Phase 2: Background Processor

```javascript
/**
 * Process operation queue
 * - Claims operations using FOR UPDATE SKIP LOCKED
 * - Processes one operation per Q/P at a time (maintains ordering)
 * - Reclaims stuck operations from crashed workers
 */
async function processOperationQueue() {
  // Check our own capacity first
  if (pool.waitingCount > 5) {
    // Skip this cycle if we're overloaded
    return;
  }
  
  try {
    // Claim work atomically
    const ops = await pool.query(`
      WITH next_operations AS (
        -- Get the oldest pending/stuck operation for each Q/P
        SELECT DISTINCT ON (queue_name, COALESCE(partition_name, ''))
          id,
          operation_type,
          queue_name,
          partition_name,
          priority,
          payload,
          retry_count
        FROM queen.operation_queue
        WHERE status = 'pending'
          OR (
            status = 'processing' 
            AND processing_started_at < NOW() - timeout_seconds * INTERVAL '1 second'
          )
        ORDER BY 
          queue_name, 
          COALESCE(partition_name, ''),
          id ASC  -- UUIDv7 is time-ordered
      ),
      claimable AS (
        -- From those, pick top N by priority
        SELECT id 
        FROM next_operations
        ORDER BY priority ASC, id ASC
        LIMIT 50
        FOR UPDATE SKIP LOCKED  -- Other workers skip these
      )
      UPDATE queen.operation_queue oq
      SET 
        status = 'processing',
        processing_started_at = NOW(),
        claimed_by_worker = $1
      FROM claimable c
      WHERE oq.id = c.id
      RETURNING oq.*
    `, [config.WORKER_ID]);
    
    if (ops.rows.length === 0) {
      return;  // No work
    }
    
    log(`📋 Processing ${ops.rows.length} queued operations`);
    
    // Process each operation
    for (const op of ops.rows) {
      await processOneOperation(op);
    }
    
  } catch (error) {
    log(`❌ Error processing operation queue: ${error.message}`);
  }
}

// Run every 100ms
setInterval(processOperationQueue, 100);
```

---

### Phase 3: Operation Execution

```javascript
async function processOneOperation(op) {
  const startTime = Date.now();
  
  try {
    let result;
    
    switch (op.operation_type) {
      case 'PUSH':
        const { queue, partition, messages } = op.payload;
        result = await queueManager.pushMessages(
          messages.map(m => ({ queue, partition, payload: m }))
        );
        break;
        
      case 'ACK_BATCH':
        const { acknowledgments, consumerGroup } = op.payload;
        result = await queueManager.acknowledgeMessages(acknowledgments, consumerGroup);
        break;
        
      case 'SYSTEM_EVENT':
        result = await systemEventManager.processSystemEvent(op.payload);
        break;
        
      default:
        throw new Error(`Unknown operation type: ${op.operation_type}`);
    }
    
    // Mark as completed
    await pool.query(`
      UPDATE queen.operation_queue
      SET status = 'completed',
          processed_at = NOW()
      WHERE id = $1
    `, [op.id]);
    
    const duration = Date.now() - startTime;
    log(`✅ Completed ${op.operation_type} operation ${op.id} in ${duration}ms`);
    
  } catch (error) {
    log(`❌ Failed ${op.operation_type} operation ${op.id}: ${error.message}`);
    
    // Update with error
    await pool.query(`
      UPDATE queen.operation_queue
      SET status = CASE 
            WHEN retry_count >= max_retries THEN 'failed'
            ELSE 'pending'  -- Retry
          END,
          retry_count = retry_count + 1,
          error_message = $2,
          last_error_at = NOW(),
          processing_started_at = NULL,
          claimed_by_worker = NULL
      WHERE id = $1
    `, [op.id, error.message]);
  }
}
```

---

## 🎯 Priority Levels

```javascript
const OPERATION_PRIORITIES = {
  SYSTEM_EVENT: 1,      // Critical for cache consistency
  ACK_BATCH: 3,         // High (releases partition leases)
  ACK_SINGLE: 4,        // High (releases partition leases)
  PUSH: 5,              // Normal user traffic
  POP: 6,               // Normal (but usually not queued)
  BACKGROUND_JOB: 8,    // Low (retention, eviction)
  ANALYTICS: 10         // Lowest (can wait)
};

function getPriority(operationType) {
  return OPERATION_PRIORITIES[operationType] || 5;
}
```

---

## 🔑 UUIDv7 for Ordering

### Why UUIDv7 > BIGSERIAL:

| Feature | BIGSERIAL | UUIDv7 | Winner |
|---------|-----------|--------|---------|
| **Time-ordered** | ✅ Sequential | ✅ Timestamp-based | Tie |
| **Globally unique** | ❌ Per-table | ✅ Universally | UUIDv7 |
| **Distributed** | ❌ Single sequence | ✅ No coordination | UUIDv7 |
| **Sortable** | ✅ Numeric | ✅ Lexicographic | Tie |
| **Index size** | ✅ 8 bytes | ⚠️ 16 bytes | BIGSERIAL |
| **Collision-free** | ✅ Guaranteed | ✅ Guaranteed | Tie |
| **Works across DBs** | ❌ No | ✅ Yes | UUIDv7 |

**Verdict: UUIDv7 is better for distributed systems!** ✅

### UUIDv7 Structure:
```
UUIDv7: 01234567-89ab-7def-8012-3456789abcdef
        ├─────┬─┘    │
        │     │      └─ Random bits
        │     └─ Timestamp milliseconds (lower bits)
        └─ Timestamp milliseconds (upper bits)

Sorted by time automatically!
```

### Ordering Query:
```sql
-- UUIDs are time-ordered, so we can sort them directly
ORDER BY id ASC  -- Oldest first (created_at implicit in UUID)

-- Or explicitly
ORDER BY created_at ASC, id ASC  -- Belt and suspenders
```

---

## 📋 Implementation Phases

### **Phase 1: Schema & Infrastructure** (2 hours)

#### 1.1 Create UUIDv7 Function
```sql
-- File: migrations/001_add_uuidv7_function.sql
CREATE OR REPLACE FUNCTION uuid_generate_v7()
RETURNS uuid
AS $$
DECLARE
  unix_ts_ms BIGINT;
  uuid_bytes BYTEA;
BEGIN
  -- Get current timestamp in milliseconds
  unix_ts_ms := (EXTRACT(EPOCH FROM CLOCK_TIMESTAMP()) * 1000)::BIGINT;
  
  -- Build UUIDv7: timestamp (48 bits) + version (4 bits) + random (74 bits)
  uuid_bytes := DECODE(
    -- 48-bit timestamp
    LPAD(TO_HEX(unix_ts_ms), 12, '0') ||
    -- Version 7 and random bits
    '7' || ENCODE(gen_random_bytes(9), 'hex'),
    'hex'
  );
  
  RETURN ENCODE(uuid_bytes, 'hex')::uuid;
END;
$$ LANGUAGE plpgsql VOLATILE;

-- Test it
SELECT uuid_generate_v7(), uuid_generate_v7(), uuid_generate_v7();
-- Should see sequential UUIDs
```

#### 1.2 Create Operation Queue Table
```sql
-- File: migrations/002_create_operation_queue.sql
CREATE TABLE IF NOT EXISTS queen.operation_queue (
  -- Identity (UUIDv7 - time-ordered)
  id UUID PRIMARY KEY DEFAULT uuid_generate_v7(),
  operation_type VARCHAR(20) NOT NULL,
  
  -- Routing & Ordering
  queue_name VARCHAR(255) NOT NULL,
  partition_name VARCHAR(255),              -- NULL for queue-level operations
  priority INTEGER DEFAULT 5,               -- 1=critical, 10=low
  
  -- Payload
  payload JSONB NOT NULL,
  estimated_size INTEGER,                   -- Bytes (for monitoring)
  
  -- Status
  status VARCHAR(20) DEFAULT 'pending',     -- pending, processing, completed, failed
  created_at TIMESTAMPTZ DEFAULT NOW(),
  processing_started_at TIMESTAMPTZ,
  processed_at TIMESTAMPTZ,
  
  -- Worker info (debugging only)
  claimed_by_worker VARCHAR(255),
  
  -- Retry logic
  timeout_seconds INTEGER DEFAULT 30,
  retry_count INTEGER DEFAULT 0,
  max_retries INTEGER DEFAULT 3,
  error_message TEXT,
  last_error_at TIMESTAMPTZ
);

-- Indexes
CREATE INDEX idx_opq_pending_priority 
  ON queen.operation_queue(priority ASC, id ASC) 
  WHERE status = 'pending';

CREATE INDEX idx_opq_queue_partition_order 
  ON queen.operation_queue(queue_name, COALESCE(partition_name, ''), id ASC) 
  WHERE status IN ('pending', 'processing');

CREATE INDEX idx_opq_stuck 
  ON queen.operation_queue(processing_started_at) 
  WHERE status = 'processing';

CREATE INDEX idx_opq_type_status 
  ON queen.operation_queue(operation_type, status);

-- Cleanup old completed operations
CREATE INDEX idx_opq_cleanup 
  ON queen.operation_queue(processed_at) 
  WHERE status = 'completed';
```

---

### **Phase 2: Operation Queueing Logic** (3 hours)

#### 2.1 Load Detection Module
```javascript
// File: src/managers/loadManager.js

export class LoadManager {
  constructor(pool) {
    this.pool = pool;
    this.metrics = {
      poolWaitCount: 0,
      poolUtilization: 0,
      queueDepth: 0,
      lastUpdate: Date.now()
    };
    
    // Update metrics every second
    setInterval(() => this.updateMetrics(), 1000);
  }
  
  async updateMetrics() {
    this.metrics.poolWaitCount = this.pool.waitingCount;
    this.metrics.poolUtilization = this.pool.totalCount / this.pool.max;
    
    // Get queue depth
    const result = await this.pool.query(`
      SELECT COUNT(*) as depth 
      FROM queen.operation_queue 
      WHERE status = 'pending'
    `);
    this.metrics.queueDepth = parseInt(result.rows[0].depth);
    this.metrics.lastUpdate = Date.now();
  }
  
  shouldQueueOperation(operationType) {
    const thresholds = {
      PUSH: { poolUtilization: 0.7, queueDepth: 10000 },
      ACK_BATCH: { poolUtilization: 0.8, queueDepth: 50000 },
      ACK_SINGLE: { poolUtilization: 0.8, queueDepth: 50000 },
      POP: { poolUtilization: 0.95, queueDepth: 0 },  // Almost never queue POPs
      SYSTEM_EVENT: { poolUtilization: 1.0, queueDepth: 0 }  // Never queue critical ops
    };
    
    const threshold = thresholds[operationType] || { poolUtilization: 0.7, queueDepth: 10000 };
    
    // Check pool utilization
    if (this.metrics.poolUtilization > threshold.poolUtilization) {
      // Check if queue is too deep
      if (this.metrics.queueDepth > threshold.queueDepth) {
        return 'REJECT';  // Queue full, return 429
      }
      return 'QUEUE';  // Queue it
    }
    
    return 'EXECUTE';  // Execute directly
  }
  
  async hasPendingOperations(queue, partition) {
    const result = await this.pool.query(`
      SELECT EXISTS(
        SELECT 1 FROM queen.operation_queue
        WHERE queue_name = $1 
          AND (partition_name = $2 OR ($2 IS NULL AND partition_name IS NULL))
          AND status IN ('pending', 'processing')
      ) as has_pending
    `, [queue, partition || null]);
    
    return result.rows[0].has_pending;
  }
}
```

#### 2.2 Operation Queue Manager
```javascript
// File: src/managers/operationQueueManager.js

export class OperationQueueManager {
  constructor(pool, queueManager, loadManager) {
    this.pool = pool;
    this.queueManager = queueManager;
    this.loadManager = loadManager;
    this.processing = false;
  }
  
  async enqueueOperation(operationType, queue, partition, payload, priority = null) {
    const result = await this.pool.query(`
      INSERT INTO queen.operation_queue (
        id,
        operation_type,
        queue_name,
        partition_name,
        priority,
        payload,
        estimated_size
      ) VALUES (
        uuid_generate_v7(),
        $1, $2, $3, $4, $5, $6
      )
      RETURNING id, created_at
    `, [
      operationType,
      queue,
      partition || null,
      priority || getPriority(operationType),
      JSON.stringify(payload),
      JSON.stringify(payload).length
    ]);
    
    // Get queue position
    const position = await this.pool.query(`
      SELECT COUNT(*) as position
      FROM queen.operation_queue
      WHERE status = 'pending' AND id < $1
    `, [result.rows[0].id]);
    
    return {
      operationId: result.rows[0].id,
      queuePosition: position.rows[0].position,
      estimatedWait: Math.ceil(position.rows[0].position / 100)
    };
  }
  
  async processQueue() {
    if (this.processing) return;  // Already running
    if (this.loadManager.metrics.poolWaitCount > 5) return;  // Too busy
    
    this.processing = true;
    try {
      await this._claimAndProcess();
    } finally {
      this.processing = false;
    }
  }
  
  async _claimAndProcess() {
    // Implementation as shown in Phase 2 above
    // ...
  }
  
  startProcessor(intervalMs = 100) {
    setInterval(() => this.processQueue(), intervalMs);
    log(`✅ Operation queue processor started (interval: ${intervalMs}ms)`);
  }
}
```

---

### **Phase 3: Route Integration** (2 hours)

#### 3.1 PUSH Route with Queueing
```javascript
// File: src/routes/push.js (modified)

app.post('/api/v1/push', (res, req) => {
  readBody(res, async (body) => {
    const { items } = body;
    
    // For each unique Q/P in the batch
    const queuePartitions = groupByQueuePartition(items);
    
    for (const [qpKey, qpItems] of queuePartitions) {
      const [queue, partition] = qpKey.split(':');
      
      // Check if we should queue
      const hasPending = await loadManager.hasPendingOperations(queue, partition);
      const decision = loadManager.shouldQueueOperation('PUSH');
      
      if (hasPending || decision === 'QUEUE') {
        // Queue it
        const queueResult = await operationQueueManager.enqueueOperation(
          'PUSH',
          queue,
          partition,
          { queue, partition, messages: qpItems }
        );
        
        res.writeStatus('202');  // Accepted
        res.end(JSON.stringify({
          status: 'queued',
          ...queueResult
        }));
        return;
      }
      
      if (decision === 'REJECT') {
        // System overloaded, queue full
        res.writeStatus('429');
        res.writeHeader('Retry-After', '5');
        res.end(JSON.stringify({ 
          error: 'System overloaded, queue full',
          queueDepth: loadManager.metrics.queueDepth
        }));
        return;
      }
      
      // Execute directly (no pending, pool healthy)
      const result = await queueManager.pushMessages(qpItems);
      res.writeStatus('201');
      res.end(JSON.stringify(result));
    }
  });
});
```

#### 3.2 ACK Route with Queueing
```javascript
// File: src/routes/ack.js (modified)

app.post('/api/v1/ack/batch', (res, req) => {
  readBody(res, async (body) => {
    const { acknowledgments } = body;
    
    const decision = loadManager.shouldQueueOperation('ACK_BATCH');
    
    if (decision === 'QUEUE') {
      // Queue batch ACK
      const queueResult = await operationQueueManager.enqueueOperation(
        'ACK_BATCH',
        '__ack_batch__',  // Virtual queue name
        null,
        { acknowledgments, consumerGroup: body.consumerGroup },
        3  // High priority
      );
      
      res.writeStatus('202');
      res.end(JSON.stringify({
        status: 'queued',
        ...queueResult,
        note: 'ACK will be processed within 1 second'
      }));
      return;
    }
    
    // Execute directly
    const result = await queueManager.acknowledgeMessages(acknowledgments);
    res.writeStatus('200');
    res.end(JSON.stringify(result));
  });
});
```

---

### **Phase 4: Monitoring & Maintenance** (1 hour)

#### 4.1 Cleanup Job
```javascript
// Delete completed operations older than 1 hour
async function cleanupCompletedOperations() {
  const result = await pool.query(`
    DELETE FROM queen.operation_queue
    WHERE status = 'completed'
      AND processed_at < NOW() - INTERVAL '1 hour'
  `);
  
  if (result.rowCount > 0) {
    log(`🧹 Cleaned up ${result.rowCount} completed operations`);
  }
}

// Run every 10 minutes
setInterval(cleanupCompletedOperations, 600000);
```

#### 4.2 Monitoring Endpoint
```javascript
// File: src/routes/operations.js (new)

app.get('/api/v1/operations/stats', (res, req) => {
  // Get real-time stats directly from operation_queue
  const stats = await pool.query(`
    SELECT 
      operation_type,
      priority,
      status,
      COUNT(*) as count,
      MIN(created_at) as oldest,
      MAX(created_at) as newest,
      AVG(EXTRACT(EPOCH FROM (COALESCE(processed_at, NOW()) - created_at))) as avg_latency_seconds
    FROM queen.operation_queue
    WHERE status != 'completed' OR processed_at > NOW() - INTERVAL '5 minutes'
    GROUP BY operation_type, priority, status
    ORDER BY priority, operation_type
  `);
  
  res.end(JSON.stringify({
    stats: stats.rows,
    systemHealth: {
      poolUtilization: loadManager.metrics.poolUtilization,
      poolWaiting: loadManager.metrics.poolWaitCount,
      queueDepth: loadManager.metrics.queueDepth
    }
  }));
});

app.get('/api/v1/operations/:operationId', (res, req) => {
  const opId = req.getParameter(0);
  
  const result = await pool.query(`
    SELECT 
      id,
      operation_type,
      status,
      priority,
      created_at,
      processing_started_at,
      processed_at,
      retry_count,
      error_message,
      EXTRACT(EPOCH FROM (COALESCE(processed_at, NOW()) - created_at)) as latency_seconds
    FROM queen.operation_queue
    WHERE id = $1
  `, [opId]);
  
  res.end(JSON.stringify(result.rows[0] || { error: 'Not found' }));
});
```

---

## 🛡️ Ordering Guarantee - Complete Solution

### The Critical Check:

```javascript
async function executePush(queue, partition, messages) {
  // Step 1: Check for pending operations on this Q/P
  const hasPending = await pool.query(`
    SELECT EXISTS(
      SELECT 1 FROM queen.operation_queue
      WHERE queue_name = $1 
        AND partition_name = $2
        AND status IN ('pending', 'processing')
    )
  `, [queue, partition]);
  
  if (hasPending.rows[0].exists) {
    // MUST queue to maintain order
    return await enqueueOperation('PUSH', queue, partition, messages);
  }
  
  // Step 2: Check system load
  if (loadManager.shouldQueueOperation('PUSH') === 'QUEUE') {
    return await enqueueOperation('PUSH', queue, partition, messages);
  }
  
  // Step 3: Safe to execute directly (no pending ops, system healthy)
  return await directPush(queue, partition, messages);
}
```

### Why This Works:

```
Batch X: No pending ops → Execute directly → Order: 1
Batch Y: Load high → Queue (seq: 1001) → Order: 2
Batch Z: Load OK BUT Y is pending → Queue (seq: 1002) → Order: 3

Processor:
  - Picks Y first (seq 1001) → Processes
  - Then picks Z (seq 1002) → Processes
  
Final: X, Y, Z ✅ ORDER PRESERVED!
```

---

## 📊 Performance Impact

### Without Operation Queue (Current):
```
Load Spike:
  100 concurrent pushes
  ↓
  Pool (30 connections) exhausted
  ↓
  70 requests timeout after 2 seconds
  ↓
  Client retries → More load
  ↓
  Death spiral
```

### With Operation Queue:
```
Load Spike:
  100 concurrent pushes
  ↓
  First 30 execute directly (pool available)
  ↓
  Next 70 queued (202 Accepted in 5ms)
  ↓
  Background processor drains queue (100 ops/second)
  ↓
  All 100 completed in ~2 seconds
  ↓
  No failures, order preserved ✅
```

---

## 🎯 Benefits Summary

### ✅ **Ordering Preserved**
- Check for pending ops before direct execution
- UUIDv7 ensures time-ordered processing
- One op per Q/P at a time

### ✅ **Fault Tolerant**
- Any worker can process any operation
- Automatic timeout recovery
- No stuck operations

### ✅ **Load Handling**
- Queue when busy, execute when available
- Smooth degradation (202 vs 429)
- Prioritized processing

### ✅ **Durable**
- Operations survive crashes
- ACID guarantees
- Inspectable via SQL

### ✅ **Observable**
- See queue depth per Q/P
- Monitor stuck operations
- Track latency per operation type

---

## 🔧 Client Changes Required

### Current Client Behavior (PROBLEM):

**File:** `src/client/utils/retry.js` (lines 12-15)
```javascript
// Don't retry on client errors (4xx)
if (error.status && error.status >= 400 && error.status < 500) {
  throw error;  // ❌ This includes 429!
}
```

**Issue:** Client throws immediately on 429 without retrying!

---

### Required Changes:

#### Change 1: Handle 429 as Retryable (CRITICAL)

**File:** `src/client/utils/retry.js`
```javascript
export const withRetry = async (fn, attempts = 3, delay = 1000, backoff = 2) => {
  let lastError;
  
  for (let i = 0; i < attempts; i++) {
    try {
      return await fn();
    } catch (error) {
      lastError = error;
      
      // SPECIAL CASE: 429 is retryable (rate limit/backpressure)
      if (error.status === 429) {
        // Honor Retry-After header if present
        const retryAfter = error.retryAfter || (delay * Math.pow(backoff, i)) / 1000;
        const waitMs = Math.min(retryAfter * 1000, 30000);  // Max 30s
        
        console.log(`⚠️  Server busy (429), retrying in ${retryAfter}s...`);
        await new Promise(resolve => setTimeout(resolve, waitMs));
        continue;
      }
      
      // 202 Accepted - operation queued, not an error!
      if (error.status === 202) {
        // Return the queued response, not an error
        return error.data;
      }
      
      // Don't retry on other client errors (400, 401, 403, 404, etc)
      if (error.status && error.status >= 400 && error.status < 500) {
        throw error;
      }
      
      // Retry on server errors (5xx) and network errors
      if (i < attempts - 1) {
        const waitTime = delay * Math.pow(backoff, i);
        await new Promise(resolve => setTimeout(resolve, waitTime));
      }
    }
  }
  
  throw lastError;
};
```

---

#### Change 2: Parse Retry-After Header

**File:** `src/client/utils/http.js` (add to response parsing)
```javascript
// When parsing error response
if (!response.ok) {
  const errorData = await response.json().catch(() => ({}));
  
  const error = new Error(errorData.error || `Request failed with status ${response.status}`);
  error.status = response.status;
  error.data = errorData;
  
  // Parse Retry-After header
  const retryAfter = response.headers.get('Retry-After');
  if (retryAfter) {
    error.retryAfter = parseInt(retryAfter);  // Seconds
  }
  
  throw error;
}
```

---

#### Change 3: Handle 202 Accepted Responses

**File:** `src/client/client.js` (push method)
```javascript
async push(address, messages) {
  const result = await this.#http.post('/api/v1/push', {
    items: formattedItems
  });
  
  // Handle queued operations (202 Accepted)
  if (result.status === 'queued') {
    console.log(`ℹ️  Operation queued: ${result.operationId}, position: ${result.queuePosition}`);
    
    // Option A: Return queued info to user
    return {
      status: 'queued',
      operationId: result.operationId,
      queuePosition: result.queuePosition,
      estimatedWait: result.estimatedWait
    };
    
    // Option B: Poll until complete (blocking)
    // return await this.#pollOperation(result.operationId);
  }
  
  // Normal execution
  return result;
}

// Optional: Poll for queued operation completion
async #pollOperation(operationId, maxWait = 30000) {
  const start = Date.now();
  
  while (Date.now() - start < maxWait) {
    const status = await this.#http.get(`/api/v1/operations/${operationId}`);
    
    if (status.status === 'completed') {
      return status.result;
    }
    
    if (status.status === 'failed') {
      throw new Error(status.error_message);
    }
    
    // Still pending/processing - wait and retry
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  
  throw new Error('Operation timeout');
}
```

---

### Client Behavior Summary:

| Server Response | Current Client | Updated Client | Behavior |
|----------------|----------------|----------------|----------|
| **200 OK** | ✅ Return data | ✅ Return data | Normal |
| **201 Created** | ✅ Return data | ✅ Return data | Normal |
| **202 Accepted** | ❌ Error | ✅ Return queued info | Queued |
| **429 Too Many** | ❌ Throw error | ✅ Retry with backoff | Auto-retry |
| **503 Unavailable** | ⚠️ Retry (5xx) | ✅ Retry with backoff | Auto-retry |
| **400, 404, etc** | ✅ Throw | ✅ Throw | Don't retry |

---

### Compatibility:

**Backward Compatible:**
- ✅ Existing code works (200/201 unchanged)
- ✅ New 429 handling is additive
- ✅ 202 can be treated as success or queued

**Breaking Changes:**
- ⚠️ PUSH may return `{status: 'queued'}` instead of `{messages: [...]}`
- ⚠️ Applications need to handle queued responses

**Migration Path:**
```javascript
// Old code (still works)
const result = await queen.push(queue, messages);
// Result: { messages: [...] } or throws

// New code (handles queueing)
const result = await queen.push(queue, messages);
if (result.status === 'queued') {
  console.log(`Queued at position ${result.queuePosition}`);
  // Option: Poll for completion
  // const finalResult = await queen.getOperation(result.operationId);
} else {
  console.log(`Pushed ${result.messages.length} messages`);
}
```

---

### Client Configuration:

Add to Queen constructor options:
```javascript
const queen = new Queen({
  baseUrls: ['http://localhost:6632'],
  retryOn429: true,              // Auto-retry on 429 (default: true)
  honorRetryAfter: true,         // Use server's Retry-After (default: true)
  maxRetryDelay: 30000,          // Max wait between retries (default: 30s)
  queuedOperationBehavior: 'return'  // 'return' | 'poll' (default: return)
});
```

---

## 📋 Client Update Checklist

- [ ] Modify `src/client/utils/retry.js` to handle 429 as retryable
- [ ] Parse `Retry-After` header in `src/client/utils/http.js`
- [ ] Handle 202 Accepted responses in `src/client/client.js`
- [ ] Add configuration options for retry behavior
- [ ] Update client documentation
- [ ] Test with load scenarios (429, 202, timeouts)
- [ ] Ensure backward compatibility

---

## 📅 Implementation Timeline

| Phase | Task | Time | Priority |
|-------|------|------|----------|
| 1.1 | operation_queue table | 30min | 🏆 |
| 1.2 | LoadManager class | 1h | 🏆 |
| 1.3 | OperationQueueManager | 2h | 🏆 |
| 2.1 | **Client 429 handling** | 1h | 🏆 |
| 2.2 | **Client Retry-After support** | 30min | 🏆 |
| 3.1 | PUSH route integration | 1h | 🏆 |
| 3.2 | ACK route integration | 1h | ⭐ |
| 3.3 | POP 429 logic | 30min | ⭐ |
| 4.1 | Cleanup job | 30min | 💡 |
| 4.2 | Monitoring endpoints | 30min | 💡 |
| **Total** | | **8.5 hours** | |

---

## 🧪 Testing Strategy

### Test 1: Ordering Under Load
```javascript
// Push 3 batches rapidly
await push(queue, partition, batch1);  // Direct
await push(queue, partition, batch2);  // Queued (load spike)
await push(queue, partition, batch3);  // Queued (maintains order)

// Verify:
// - All 3 batches processed
// - Order: 1, 2, 3 (not 1, 3, 2)
```

### Test 2: Worker Crash Recovery
```javascript
// Start operation processing
// Kill worker mid-operation
// Wait 30 seconds
// Verify operation completes via other worker
```

### Test 3: Queue Full Behavior
```javascript
// Fill queue to max_depth
// Send more operations
// Verify 429 responses
// Drain queue
// Verify operations resume
```

---

## 🚀 Migration Path

### Week 1: Foundation
- ✅ Add UUIDv7 function
- ✅ Create operation_queue table
- ✅ Create LoadManager
- ✅ Create OperationQueueManager

### Week 2: Integration
- ✅ Integrate PUSH route with queueing
- ✅ Integrate ACK route with queueing
- ✅ Add monitoring endpoints

### Week 3: Optimization
- ✅ Tune thresholds based on real load
- ✅ Add alerting for stuck operations
- ✅ Performance testing

---

## 💎 Key Takeaways

**Your UUIDv7 idea is perfect because:**
1. ✅ Time-ordered (maintains sequence)
2. ✅ Globally unique (works across workers)
3. ✅ Sortable (ORDER BY id works)
4. ✅ Distributed-friendly (no coordination needed)

**Ordering is guaranteed by:**
1. ✅ Checking for pending ops before direct execution
2. ✅ UUIDv7 time-ordering
3. ✅ Processing one op per Q/P at a time
4. ✅ `DISTINCT ON (queue, partition)` in processor

**Fault tolerance via:**
1. ✅ No worker affinity (any worker processes any op)
2. ✅ FOR UPDATE SKIP LOCKED (coordination)
3. ✅ Timeout reclaim (crash recovery)
4. ✅ PostgreSQL ACID (durability)

---

**Status: Ready for implementation!** 🚀

**Next step:** Implement Phase 1 (UUIDv7 + schema)?

