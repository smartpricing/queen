# Queen - PostgreSQL-backed Message Queue System

<div align="center">

**A modern, high-performance message queue system built on PostgreSQL**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE.md)
[![Node](https://img.shields.io/badge/node-%3E%3D22.0.0-brightgreen.svg)](https://nodejs.org/)

[Quick Start](#-quick-start) • [Client Examples](#-client-examples) • [Server Setup](#-server-setup) • [Core Concepts](#-core-concepts) • [API Reference](#-http-api-reference) • [Dashboard](#-dashboard)

<p align="center">
  <img src="assets/queen-logo.svg" alt="Queen Logo" width="120" />
</p>

</div>

---

## 🎯 Introduction

**Queen** is a production-ready message queue system that combines the reliability of PostgreSQL with the performance of modern async architectures. Built with uWebSockets.js for blazing-fast HTTP handling and designed for real-world workloads.

### Why Queen?

**🚀 Developer-First API**
- **4 core methods, infinite patterns**: `queue()`, `push()`, `take()`, `ack()`
- **Async iteration**: Process messages with familiar `for await` syntax
- **Batch processing**: Use `takeBatch()` for 250k+ msg/sec throughput on millions of messages
- **Smart addressing**: `orders/urgent@workers` - queue, partition, and consumer group in one

**⚡ Production-Ready Performance**
- **100,000+ msg/sec** throughput with cursor-based consumption
- **Constant-time batch operations** - O(batch_size) regardless of queue depth
- **Long polling** for event-driven, real-time message delivery
- **Partition locking** prevents duplicate processing across consumers
- **Connection pooling** and optimized batch operations

**🏗️ Flexible Architecture**
- **Queue Mode**: Competitive consumption (traditional work queue)
- **Bus Mode**: Pub/sub with consumer groups (event streaming)
- **Mixed Mode**: Combine both patterns in the same system
- **Partitions**: FIFO ordering with parallel processing

**🔒 Enterprise Features**
- **AES-256-GCM Encryption**: Protect sensitive data at rest
- **Message Retention**: Automatic cleanup policies
- **Message Eviction**: SLA enforcement for time-sensitive tasks
- **Dead Letter Queue**: Handle failed messages gracefully

**📊 Built-in Observability**
- **Real-time Dashboard**: WebSocket-powered monitoring
- **Rich Analytics**: Throughput, lag, queue depth metrics
- **Message Browser**: Search, inspect, and retry messages
- **System Health**: Database, memory, and performance metrics
- **Cursor Tracking**: Monitor consumption progress per consumer group

### Use Cases

- **Task Queues**: Background jobs, email sending, data processing
- **Event Streaming**: Audit logs, analytics, multi-service event handling
- **Workflow Orchestration**: Multi-stage pipelines, saga patterns
- **Rate Limiting**: Throttle and batch time-sensitive operations
- **Priority Processing**: Handle urgent tasks before routine ones

---

## 📋 Table of Contents

- [First Queue](#-first-queue)
- [Quick Start](#-quick-start)
- [Client Examples](#-client-examples)
- [Server Setup](#-server-setup)
- [Core Concepts](#-core-concepts)
- [Cursor-Based Consumption Strategy](#-cursor-based-consumption-strategy)
- [HTTP API Reference](#-http-api-reference)
- [Dashboard](#-dashboard)
- [Configuration](#-configuration)
- [Full Examples](#-full-examples)
- [Testing](#-testing)
- [Contributing](#-contributing)

---

## First Queue

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({
  baseUrls: ['http://localhost:6632']
});

// Configure queue
await client.queue('tasks', {
  leaseTime: 300,
  retryLimit: 3
});

// Push a message
await client.push('tasks', { 
  action: 'send-email',
  to: 'user@example.com' 
});

// Process messages
for await (const message of client.take('tasks')) {
  console.log('Processing:', message.data);
  await client.ack(message);
}
```

That's it! You now have a working message queue system.

## 🏃 Quick Start

### Prerequisites

- **Node.js 22+**
- **PostgreSQL 12+**

### Installation

```bash
# Clone the repository
git clone https://github.com/smartpricing/queen
cd queen

# Install dependencies
nvm use 22
npm install

# Initialize database schema
node init-db.js
```

### Set Environment (Optional)

```bash
# Database connection
export PG_USER=postgres
export PG_HOST=localhost
export PG_DB=postgres
export PG_PASSWORD=postgres
export PG_PORT=5432

# Enable encryption (optional)
export QUEEN_ENCRYPTION_KEY=$(openssl rand -hex 32)
```

### Start the Server

```bash
npm start
# Server starts on http://localhost:6632
```

---

## 💻 Client Examples

The Queen client provides a minimalist API with just 4 methods that compose into any messaging pattern you need.

### Installation

```bash
npm install queen-mq
```

### Basic Usage

#### 1. Configure a Queue

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({
  baseUrls: ['http://localhost:6632'],
  timeout: 30000,
  retryAttempts: 3
});

// Configure with options
await client.queue('orders', {
  priority: 10,          // Higher priority queues processed first
  leaseTime: 600,        // 10 minutes to process each message
  retryLimit: 3,         // Retry up to 3 times
  delayedProcessing: 0,  // No delay (immediate processing)
});

// Configure with namespace and task for grouping
await client.queue('order-processing', {
  priority: 10
}, {
  namespace: 'ecommerce',
  task: 'checkout'
});
```

#### 2. Push Messages

```javascript
// Single message
await client.push('orders', { 
  orderId: 12345, 
  amount: 99.99 
});

// To a specific partition
await client.push('orders/urgent', { 
  orderId: 12346, 
  amount: 999.99,
  priority: 'high'
});

// Batch messages
await client.push('orders', [
  { orderId: 12347, amount: 49.99 },
  { orderId: 12348, amount: 79.99 },
  { orderId: 12349, amount: 29.99 }
]);

// With message properties
await client.push('orders', {
  orderId: 12350,
  amount: 199.99
}, {
  transactionId: 'txn-12350',  // For idempotency
  traceId: '550e8400-e29b-41d4-a716-446655440000'  // Valid UUID for tracing
});
```

#### 3. Take Messages (Async Iterator)

```javascript
// Process continuously with long polling
for await (const message of client.take('orders', { 
  wait: true,      // Enable long polling
  timeout: 30000,  // 30 second timeout
  batch: 10        // Fetch up to 10 at once
})) {
  try {
    await processOrder(message.data);
    await client.ack(message);  // Success
  } catch (error) {
    await client.ack(message, false, { error: error.message });  // Failure
  }
}

// Process limited messages
for await (const message of client.take('orders', { limit: 100 })) {
  await processOrder(message.data);
  await client.ack(message);
}

// Take from specific partition
for await (const message of client.take('orders/urgent')) {
  await processUrgentOrder(message.data);
  await client.ack(message);
}

// Use takeBatch to get arrays of messages (higher throughput)
for await (const messages of client.takeBatch('orders', { 
  batch: 1000,     // Fetch 1000 at a time
  wait: true 
})) {
  // messages is an array of up to 1000 messages
  console.log(`Processing batch of ${messages.length} messages`);
  
  try {
    // Process entire batch
    await processBatch(messages.map(m => m.data));
    
    // Acknowledge entire batch at once (efficient!)
    await client.ack(messages);  // Pass array for batch ack
  } catch (error) {
    // Mark entire batch as failed
    await client.ack(messages, false, { error: error.message });
  }
}
```

#### 4. Acknowledge Messages

```javascript
// Acknowledge success
await client.ack(message);
// or
await client.ack(message, true);

// Acknowledge failure (will retry based on retryLimit)
await client.ack(message, false);

// Acknowledge with error context
await client.ack(message, false, { 
  error: 'Payment gateway timeout' 
});

// Acknowledge using transaction ID
await client.ack('4dfb0478-655b-4c91-bcd9-b7acacf0400f', true);

// Request explicit retry
await client.ack(message, 'retry');
```

### Address Notation

Queen uses a simple addressing scheme that encodes queue, partition, and consumer group:

```javascript
// Basic addresses
'orders'                    // Queue (Default partition)
'orders/urgent'             // Queue with specific partition
'orders@workers'            // Queue with consumer group (bus mode)
'orders/urgent@workers'     // Full address: queue + partition + group

// Namespace/task filtering (cross-queue consumption)
'namespace:ecommerce'                      // All queues in namespace
'task:checkout'                            // All queues with task
'namespace:ecommerce/task:checkout'        // Combined filter
'namespace:ecommerce/task:checkout@audit'  // With consumer group
```

### Consumer Patterns

#### Continuous Processing (Long Polling)

```javascript
// Efficient real-time processing
for await (const message of client.take('tasks', {
  wait: true,      // Long polling - waits for messages
  timeout: 30000   // Server timeout
})) {
  await processTask(message.data);
  await client.ack(message);
}
```

#### Batch Processing

```javascript
// Method 1: Manual batching with take()
const batch = [];
for await (const message of client.take('analytics', { batch: 100 })) {
  batch.push(message);
  
  if (batch.length >= 100) {
    await processBatch(batch.map(m => m.data));
    
    // Acknowledge all
    for (const msg of batch) {
      await client.ack(msg);
    }
    batch.length = 0;
  }
}

// Method 2: Direct batch processing with takeBatch() (RECOMMENDED)
for await (const messages of client.takeBatch('analytics', { batch: 1000 })) {
  // messages is already an array!
  await processBatch(messages.map(m => m.data));
  
  // Single batch acknowledgment (much faster!)
  await client.ack(messages);
}
```

#### Parallel Processing with Partitions

```javascript
// Create workers for parallel processing
const partitions = ['worker-1', 'worker-2', 'worker-3', 'worker-4'];

// Distribute messages across partitions
for (let i = 0; i < messages.length; i++) {
  const partition = partitions[i % partitions.length];
  await client.push(`tasks/${partition}`, messages[i]);
}

// Each worker processes its own partition (in parallel)
async function worker(partition) {
  for await (const msg of client.take(`tasks/${partition}`)) {
    await processTask(msg.data);
    await client.ack(msg);
  }
}

// Start all workers
await Promise.all(partitions.map(p => worker(p)));
```

#### Consumer Groups (Bus Mode)

```javascript
// Multiple services process the same messages independently

// Analytics service
for await (const event of client.take('events@analytics')) {
  await updateAnalytics(event.data);
  await client.ack(event, true, { group: 'analytics' });
}

// Monitoring service (gets same messages)
for await (const event of client.take('events@monitoring')) {
  await checkThresholds(event.data);
  await client.ack(event, true, { group: 'monitoring' });
}

// Audit service (also gets same messages)
for await (const event of client.take('events@audit')) {
  await logToAudit(event.data);
  await client.ack(event, true, { group: 'audit' });
}
```

#### Subscription Modes

```javascript
// Start from all existing messages (replay)
for await (const event of client.take('events@replay-service', {
  subscriptionMode: 'all'
})) {
  await replayEvent(event.data);
  await client.ack(event);
}

// Start from new messages only (real-time)
for await (const event of client.take('events@realtime', {
  subscriptionMode: 'new'
})) {
  await processEvent(event.data);
  await client.ack(event);
}

// Start from specific timestamp
for await (const event of client.take('events@historical', {
  subscriptionMode: 'from',
  subscriptionFrom: '2024-01-01T00:00:00Z'
})) {
  await processHistoricalEvent(event.data);
  await client.ack(event);
}
```

#### Error Handling

```javascript
// Robust error handling with retries
for await (const message of client.take('critical-tasks')) {
  let retries = 3;
  
  while (retries > 0) {
    try {
      await processTask(message.data);
      await client.ack(message);
      break;
    } catch (error) {
      retries--;
      if (retries === 0) {
        console.error('Task failed after retries:', error);
        await client.ack(message, false, { error: error.message });
      } else {
        await new Promise(r => setTimeout(r, 1000 * (4 - retries)));
      }
    }
  }
}
```

#### Graceful Shutdown

```javascript
let running = true;

process.on('SIGTERM', () => {
  console.log('Shutting down gracefully...');
  running = false;
});

for await (const message of client.take('orders')) {
  if (!running) break;
  
  await processOrder(message.data);
  await client.ack(message);
}

await client.close();
console.log('Shutdown complete');
```

---

## 🖥️ Server Setup

### Single Server

```bash
# Start the server
npm start

# Or with custom configuration
PORT=6632 \
DB_POOL_SIZE=20 \
QUEEN_ENCRYPTION_KEY=$(openssl rand -hex 32) \
npm start
```

### Multi-Server (Load Balanced)

Queen supports running multiple servers for high availability and load distribution:

```bash
# Server 1
PORT=6632 WORKER_ID=server-1 npm start

# Server 2
PORT=6633 WORKER_ID=server-2 npm start

# Server 3
PORT=6634 WORKER_ID=server-3 npm start
```

Client configuration:

```javascript
const client = new Queen({
  baseUrls: [
    'http://localhost:6632',
    'http://localhost:6633',
    'http://localhost:6634'
  ],
  loadBalancingStrategy: 'ROUND_ROBIN',  // or 'RANDOM', 'LEAST_CONNECTIONS'
  enableFailover: true
});
```

### Docker Deployment

```dockerfile
FROM node:22-alpine

WORKDIR /app
COPY package*.json ./
RUN npm ci --production

COPY . .

EXPOSE 6632
CMD ["node", "src/server.js"]
```

```yaml
# docker-compose.yml
version: '3.8'

services:
  postgres:
    image: postgres:16
    environment:
      POSTGRES_DB: queen
      POSTGRES_USER: queen
      POSTGRES_PASSWORD: queen
    volumes:
      - postgres_data:/var/lib/postgresql/data
    ports:
      - "5432:5432"

  queen:
    build: .
    ports:
      - "6632:6632"
    environment:
      PG_HOST: postgres
      PG_DB: queen
      PG_USER: queen
      PG_PASSWORD: queen
      DB_POOL_SIZE: 20
      QUEEN_ENCRYPTION_KEY: ${QUEEN_ENCRYPTION_KEY}
    depends_on:
      - postgres

volumes:
  postgres_data:
```

### Environment Variables

See the [Configuration](#-configuration) section for a complete list of environment variables.

### Database Schema

The database schema is automatically created when you run:

```bash
node init-db.js
```

This creates:
- `queen.queues` - Top-level message containers
- `queen.partitions` - Subdivisions within queues (FIFO ordering)
- `queen.messages` - Individual messages with processing state

---

## 💡 Core Concepts

### Architecture

Queen uses a two-tier architecture:

```
Queues (optional namespace/task grouping)
  └── Partitions (FIFO ordering, parallel processing)
       └── Messages (lease-based processing)
```

**Key Principles:**
- **Configuration at queue level**: All settings (priority, lease time, retries) apply to the entire queue
- **FIFO within partitions**: Messages in the same partition are always processed in order
- **Partition locking**: Prevents duplicate processing across consumers
- **Lease-based processing**: Messages automatically return to pending if not acknowledged

### Queues and Partitions

**Queues** are top-level organizational units. Each queue automatically gets a "Default" partition, and you can create additional partitions for logical separation or parallel processing.

```javascript
// Messages go to "Default" partition
await client.push('orders', { orderId: 123 });

// Push to specific partition
await client.push('orders/high-priority', { orderId: 456 });

// Take from specific partition
for await (const order of client.take('orders/high-priority')) {
  await processUrgentOrder(order.data);
  await client.ack(order);
}
```

**Partitions enable:**
- **Parallel processing**: Different consumers can process different partitions simultaneously
- **Ordered processing**: FIFO guarantees within each partition
- **Logical separation**: Different priorities, teams, or workflow stages
- **Resource isolation**: Lock contention is per-partition

### Message Lifecycle

```
pending → processing → completed/failed → (retry) → dead_letter
```

1. **Pending**: Message queued, waiting to be processed
2. **Processing**: Leased to a worker (with timeout)
3. **Completed**: Successfully processed
4. **Failed**: Processing failed (may retry based on `retryLimit`)
5. **Dead Letter**: Exceeded retry limits

### Partition Locking

**Partition locking ensures message processing isolation** - when a consumer retrieves messages from a partition, that partition is locked to prevent other consumers from accessing it until:

- The consumer acknowledges all messages (releases lock)
- The lease expires (automatic release)
- The consumer explicitly releases the partition

**Lock Scope:**
- **Queue Mode**: Each consumer session is unique - locks prevent any other consumer from accessing the partition
- **Bus Mode**: Locks are per consumer group - different groups can process the same partition independently

```javascript
// Example: Partition locking in action

// Consumer 1 takes from partition A (locks it)
for await (const msg of client.take('orders', { limit: 5 })) {
  // Processing partition A - no other consumer can access it
  await client.ack(msg);
  // Partition A unlocked after all 5 messages acknowledged
}

// Consumer 2 gets messages from partition B (A was locked)
for await (const msg of client.take('orders', { limit: 5 })) {
  // Processing partition B instead
  await client.ack(msg);
}
```

### FIFO Ordering

Queen provides **strong FIFO guarantees within each partition**:

```javascript
// These messages will be processed in order 1, 2, 3
await client.push('tasks/user-123', [
  { step: 1, action: 'create' },
  { step: 2, action: 'update' },
  { step: 3, action: 'complete' }
]);

// Consumer will always receive them in order
for await (const task of client.take('tasks/user-123')) {
  console.log(task.data.step);  // Prints: 1, then 2, then 3
  await client.ack(task);
}
```

**Use cases:**
- **Per-user operations**: Use user ID as partition for ordered processing
- **Per-resource operations**: Use resource ID to maintain operation order
- **Workflow stages**: Use partition to represent different stages

### Consumer Groups (Bus Mode)

Consumer groups enable **pub-sub messaging** where multiple independent consumers process the same messages:

```javascript
// Push once
await client.push('events', { type: 'order.created', orderId: 123 });

// Multiple services consume independently
// Service 1: Analytics
for await (const event of client.take('events@analytics')) {
  await updateAnalytics(event.data);
  await client.ack(event);
}

// Service 2: Notification (gets same message)
for await (const event of client.take('events@notification')) {
  await sendNotification(event.data);
  await client.ack(event);
}

// Service 3: Audit (also gets same message)
for await (const event of client.take('events@audit')) {
  await logEvent(event.data);
  await client.ack(event);
}
```

**Each consumer group maintains:**
- Independent message status tracking
- Separate partition leases
- Individual retry counters
- Isolated processing state

### Queue Mode vs Bus Mode

**Queue Mode** (default - competitive consumption):
```javascript
// Without consumer group - messages distributed
await client.push('tasks', { id: 1 });
await client.push('tasks', { id: 2 });

// Worker 1 gets message 1
for await (const msg of client.take('tasks')) { }

// Worker 2 gets message 2 (different message)
for await (const msg of client.take('tasks')) { }
```

**Bus Mode** (pub-sub with consumer groups):
```javascript
// With consumer groups - all groups see all messages
await client.push('events', { id: 1 });

// Group 1 gets message 1
for await (const msg of client.take('events@group1')) { }

// Group 2 also gets message 1 (same message)
for await (const msg of client.take('events@group2')) { }
```

**Mixed Mode** (combine both):
```javascript
// Competitive workers process jobs
for await (const job of client.take('jobs')) {
  await processJob(job.data);
  await client.ack(job);
}

// Monitoring sees all jobs (bus mode)
for await (const job of client.take('jobs@monitoring')) {
  await monitorJob(job.data);
  await client.ack(job);
}
```

### Priority Processing

Configure priority at the **queue level**:

```javascript
await client.queue('urgent-orders', { priority: 100 });
await client.queue('normal-orders', { priority: 50 });
await client.queue('batch-jobs', { priority: 10 });

// Urgent orders processed first, then normal, then batch
```

### Lease-Based Processing

Messages are "leased" to workers for a specific duration. If not acknowledged within the lease time, they automatically return to pending status:

```javascript
// Configure lease time
await client.queue('long-tasks', { 
  leaseTime: 600  // 10 minutes to process
});

// If worker crashes or takes too long:
// - After 10 minutes, lease expires
// - Message returns to pending
// - Another worker can pick it up
```

### Delayed Processing

Schedule messages for future processing:

```javascript
await client.queue('scheduled-jobs', {
  delayedProcessing: 3600  // 1 hour delay
});

await client.push('scheduled-jobs', { 
  reportType: 'daily-sales' 
});
// Message won't be available for processing until 1 hour later
```

### Window Buffering

Batch messages within a time window:

```javascript
await client.queue('analytics', {
  windowBuffer: 60  // Wait 60 seconds to accumulate messages
});

// Messages held for 60 seconds to allow efficient batching
```

### Retry and Dead Letter Queue

```javascript
await client.queue('payments', {
  retryLimit: 3,              // Retry up to 3 times
  dlqAfterMaxRetries: true    // Move to DLQ after max retries
});

// Failed messages automatically retry
await client.ack(message, false);  // Will retry if retries < 3

// After 3 failures, message moves to dead_letter status
```

### Enterprise Features

#### 1. Encryption (AES-256-GCM)

```bash
# Generate encryption key
export QUEEN_ENCRYPTION_KEY=$(openssl rand -hex 32)
```

```javascript
await client.queue('sensitive-data', {
  encryptionEnabled: true
});

// Messages encrypted at rest in database
await client.push('sensitive-data', { ssn: '123-45-6789' });
```

#### 2. Message Retention

```javascript
await client.queue('temp-queue', {
  retentionSeconds: 3600,           // Delete pending after 1 hour
  completedRetentionSeconds: 300,   // Delete completed after 5 minutes
  retentionEnabled: true
});
```

#### 3. Message Eviction (SLA Enforcement)

```javascript
await client.queue('time-sensitive', {
  maxWaitTimeSeconds: 60  // Evict messages older than 1 minute
});
```

### Best Practices

**1. Partition Strategy**
- Use user IDs for per-user ordering
- Use resource IDs for per-resource ordering
- Use round-robin for load distribution
- Keep partition counts manageable (10-100s, not 1000s)

**2. Lease Management**
- Set lease time slightly longer than expected processing time
- Handle timeouts gracefully
- Acknowledge messages as soon as processing completes

**3. Consumer Group Design**
- One clear purpose per consumer group
- Design groups to be independent
- Ensure operations are idempotent

**4. Error Handling**
- Always wrap processing in try-catch
- Provide meaningful error messages in ack
- Use retry limits appropriately
- Monitor dead letter queue

---

## 🚀 Cursor-Based Consumption Strategy

Queen uses a **cursor-based consumption model** for optimal performance at scale, providing O(batch_size) constant-time operations regardless of queue depth.

### How It Works

Traditional message queues scan through all messages to find pending ones, leading to performance degradation as messages accumulate. Queen's cursor-based approach maintains a position marker (cursor) for each consumer, allowing direct access to the next batch of messages.

**Partition Cursors:**

Each partition maintains a cursor position per consumer group:
- `last_consumed_created_at`: Timestamp of last consumed message
- `last_consumed_id`: UUID of last consumed message (tie-breaker for same timestamp)
- `total_messages_consumed`: Running count of consumed messages

The cursor always moves **forward** in time, ensuring strict FIFO ordering.

### The takeBatch Method

Queen provides two consumption methods:

**1. `take()` - Individual message iterator:**
```javascript
// Processes messages one at a time
for await (const message of client.take('orders', { batch: 1000 })) {
  await processOrder(message.data);
  await client.ack(message);
}
```

**2. `takeBatch()` - Array iterator (HIGH PERFORMANCE):**
```javascript
// Yields arrays of messages - achieves 100k+ msg/s throughput
for await (const messages of client.takeBatch('orders', { batch: 1000 })) {
  // messages is an array of up to 1000 message objects
  await processBatch(messages.map(m => m.data));
  
  // Batch acknowledge - single DB transaction for all messages
  await client.ack(messages);
}
```

**Under the hood**, both methods use cursor-based batch retrieval:

```sql
-- Cursor-based query (simplified)
SELECT * FROM messages 
WHERE partition_id = $1 
  AND id > $2::uuid  -- Start after last cursor position
ORDER BY created_at ASC, id ASC
LIMIT $3  -- Batch size
FOR UPDATE SKIP LOCKED
```

**Key characteristics:**
1. **Constant-time**: Performance stays consistent whether you've consumed 0% or 99% of messages
2. **FIFO guarantee**: Messages always returned in creation order
3. **Lock-free scanning**: `SKIP LOCKED` prevents contention between consumers
4. **Efficient**: No table scans - direct cursor-based access using UUIDv7 (time-ordered)

**Performance tip:** Use `takeBatch()` with large batch sizes (1,000-10,000) for maximum throughput. The server fetches messages in batches regardless, but `takeBatch()` gives you the array directly, allowing bulk processing and batch acknowledgment in a single operation.

### Batch Acknowledgment Semantics

Queen handles batch acknowledgments intelligently:

**Partial Success** (some messages succeed, some fail):
```javascript
// Batch: 10,000 messages
// Success: 9,999 messages
// Failed: 1 message

// Behavior:
// ✅ Cursor advances past all 10,000 messages
// ✅ Failed message moved to Dead Letter Queue
// ✅ Next take() starts from message 10,001
// ✅ FIFO maintained, no redelivery of successful messages
```

**Total Batch Failure** (all messages fail):
```javascript
// Batch: 10,000 messages
// Success: 0 messages
// Failed: 10,000 messages

// Behavior:
// ❌ Cursor DOES NOT advance
// ❌ Messages NOT moved to DLQ
// ✅ Lease released
// ✅ Next take() gets SAME batch (retry)
// ✅ Allows recovery from transient failures
```

This design handles transient failures (network issues, service outages) gracefully while preventing poison messages from blocking the queue.

### Performance Comparison

| Operation | Traditional Approach | Cursor Approach | Improvement |
|-----------|---------------------|-----------------|-------------|
| Pop @ 0% consumed | O(partition_size) | O(batch_size) | Same |
| Pop @ 50% consumed | O(partition_size) | O(batch_size) | **10-100x faster** |
| Pop @ 99% consumed | O(partition_size) | O(batch_size) | **100-1000x faster** |

**Real-world benchmark** (1M messages):
```
Traditional:
  Early batches: 300ms per pop
  Late batches:  3500ms per pop (10x degradation)
  
Cursor-based:
  Early batches: 150ms per pop
  Late batches:  200ms per pop (constant!)
```

### Dead Letter Queue

Individual message failures are moved to the Dead Letter Queue for inspection and manual intervention:

```javascript
// Monitor DLQ
const response = await fetch('http://localhost:6632/api/v1/analytics/dlq');
const dlqMessages = await response.json();

// Inspect failed messages
for (const msg of dlqMessages) {
  console.log(`Failed: ${msg.error_message}`);
  
  // After fixing issue, can re-push if needed
  await client.push(msg.queue, fixedPayload);
}
```

**DLQ Query:**
```sql
SELECT * FROM queen.dead_letter_queue
WHERE consumer_group = 'my-group'
ORDER BY failed_at DESC
LIMIT 100;
```

### Batch Size Guidelines

Choose batch sizes based on your workload:

**Smaller batches (100-1,000):**
- ✅ Faster individual batch processing
- ✅ Less impact if entire batch fails
- ✅ Lower memory footprint
- ❌ More network round-trips

**Larger batches (5,000-10,000):**
- ✅ Higher throughput (100,000+ msg/sec achievable)
- ✅ Fewer network round-trips
- ✅ Better database efficiency
- ❌ More messages retry if entire batch fails
- ❌ Higher memory usage

**Recommendation:** Start with 1,000-2,000 for balanced performance. Increase to 5,000-10,000 for maximum throughput with reliable processing.

### Monitoring Cursor Progress

Track consumption progress via SQL:

```sql
-- View cursor positions
SELECT 
  p.name as partition,
  pc.consumer_group,
  pc.total_messages_consumed,
  pc.total_batches_consumed,
  pc.last_consumed_at,
  EXTRACT(EPOCH FROM (NOW() - pc.last_consumed_at)) as seconds_since_last_consume
FROM queen.partition_cursors pc
JOIN queen.partitions p ON p.id = pc.partition_id
ORDER BY pc.last_consumed_at DESC;

-- Monitor DLQ
SELECT COUNT(*) as failed_count, consumer_group
FROM queen.dead_letter_queue
GROUP BY consumer_group;
```

---

## 🔌 HTTP API Reference

Base URL: `http://localhost:6632/api/v1`

### Push Messages

**Endpoint:** `POST /api/v1/push`

**Request:**
```json
{
  "items": [
    {
      "queue": "orders",
      "partition": "urgent",
      "payload": { "orderId": 123, "amount": 99.99 },
      "transactionId": "optional-idempotency-key",
      "traceId": "550e8400-e29b-41d4-a716-446655440000"
    }
  ]
}
```

**Response:**
```json
{
  "messages": [
    {
      "id": "018e63b7-6165-453f-88ae-56effa177605",
      "transactionId": "4dfb0478-655b-4c91-bcd9-b7acacf0400f",
      "status": "queued"
    }
  ]
}
```

### Pop Messages

**From specific partition:**
```
GET /api/v1/pop/queue/{queue}/partition/{partition}?wait=true&timeout=30000&batch=10
```

**From any partition in queue:**
```
GET /api/v1/pop/queue/{queue}?wait=true&timeout=30000&batch=10
```

**With namespace/task filter:**
```
GET /api/v1/pop?namespace=ecommerce&task=checkout&wait=true&timeout=30000&batch=10
```

**With consumer group (bus mode):**
```
GET /api/v1/pop/queue/{queue}?consumerGroup=analytics&subscriptionMode=all&wait=true&timeout=30000
```

**Response:**
```json
{
  "messages": [
    {
      "id": "018e63b7-6165-453f-88ae-56effa177605",
      "transactionId": "4dfb0478-655b-4c91-bcd9-b7acacf0400f",
      "queue": "orders",
      "partition": "urgent",
      "data": { "orderId": 123, "amount": 99.99 },
      "retryCount": 0,
      "priority": 10,
      "createdAt": "2024-10-08T12:00:00.000Z",
      "options": { "leaseTime": 300, "retryLimit": 3 }
    }
  ]
}
```

### Acknowledge Messages

**Single:**
```json
POST /api/v1/ack
{
  "transactionId": "4dfb0478-655b-4c91-bcd9-b7acacf0400f",
  "status": "completed",
  "consumerGroup": "analytics",
  "error": null
}
```

**Batch:**
```json
POST /api/v1/ack/batch
{
  "acknowledgments": [
    { "transactionId": "uuid-1", "status": "completed" },
    { "transactionId": "uuid-2", "status": "failed", "error": "Processing error" }
  ]
}
```

### Configure Queue

```json
POST /api/v1/configure
{
  "queue": "orders",
  "namespace": "ecommerce",
  "task": "checkout",
  "options": {
    "leaseTime": 600,
    "retryLimit": 5,
    "priority": 10,
    "maxSize": 10000,
    "ttl": 3600,
    "dlqAfterMaxRetries": true,
    "delayedProcessing": 0,
    "windowBuffer": 0,
    "retentionSeconds": 0,
    "completedRetentionSeconds": 0,
    "retentionEnabled": false,
    "encryptionEnabled": false,
    "maxWaitTimeSeconds": 0
  }
}
```

### Analytics

**Queue statistics:**
```
GET /api/v1/analytics/queue/{queue}
```

**All queues overview:**
```
GET /api/v1/analytics/queues
```

**Queue depths:**
```
GET /api/v1/analytics/queue-depths
```

**Throughput metrics:**
```
GET /api/v1/analytics/throughput
```

**Queue lag analysis:**
```
GET /api/v1/analytics/queue-lag?queue=orders
```

### Message Management

**List messages:**
```
GET /api/v1/messages?queue=orders&status=pending&limit=100
```

**Get single message:**
```
GET /api/v1/messages/{transactionId}
```

**Delete message:**
```
DELETE /api/v1/messages/{transactionId}
```

**Retry failed message:**
```
POST /api/v1/messages/{transactionId}/retry
```

**Move to dead letter queue:**
```
POST /api/v1/messages/{transactionId}/dlq
```

**Clear queue:**
```
DELETE /api/v1/queues/{queue}/clear
```

**Delete queue:**
```
DELETE /api/v1/resources/queues/{queue}
```
_Note: Deletes the queue and all its partitions, messages, and related data._

### System Health

**Health check:**
```
GET /health
```

**Detailed metrics:**
```
GET /metrics
```

### WebSocket (Real-time Updates)

**Connect:**
```javascript
const ws = new WebSocket('ws://localhost:6632/ws/dashboard');

ws.onmessage = (event) => {
  const { event: eventType, data } = JSON.parse(event.data);
  // Handle events: message.pushed, message.completed, queue.depth, etc.
};
```

**Events:**
- `message.pushed` - New message added
- `message.processing` - Message being processed
- `message.completed` - Message completed
- `message.failed` - Message failed
- `queue.created` - New queue created
- `queue.depth` - Queue depth update (every 5s)
- `system.stats` - System statistics (every 10s)

See [API.md](API.md) for complete API documentation.

---

## 📊 Dashboard

Queen includes a comprehensive web dashboard for monitoring and management.

[Dashboard](/assets/dashboard-01.png)

### Access

1. Start the server: `npm start`
2. Open browser: `http://localhost:6632`
3. WebSocket connection provides real-time updates

### Features

**System Overview**
- Real-time metrics: total messages, processing rate, system health
- Queue summary with pending/processing/completed counts
- Performance indicators: throughput, latency, error rates

**Queue Management**
- Queue list with status and message counts
- Partition view with priority indicators
- Message browser with search and filter
- Retry and DLQ management

**Real-time Monitoring**
- Live updates via WebSocket
- Throughput charts (messages per second over time)
- Queue depth graphs with trend analysis
- Lag monitoring (processing time and backlog)

**Analytics Dashboard**
- Performance metrics per queue
- Historical trends and patterns
- System health monitoring
- Database connections and memory usage

**Message Browser**
- Search by queue, partition, status, time range
- View full payload and metadata
- Manually retry failed messages
- Dead letter queue management

### Dashboard Development

The dashboard is built with Vue.js and located in the `dashboard/` directory:

```bash
cd dashboard
npm install
npm run dev    # Development mode
npm run build  # Production build
```

---

## ⚙️ Configuration

All configuration uses environment variables with sensible defaults. Configuration is centralized in `src/config.js`.

### Server Configuration

```bash
PORT=6632                      # Server port (default: 6632)
HOST=0.0.0.0                   # Server host (default: 0.0.0.0)
WORKER_ID=worker-1             # Worker identifier
APP_NAME=queen-mq              # Application name

# CORS
CORS_MAX_AGE=86400
CORS_ALLOWED_ORIGINS=*
CORS_ALLOWED_METHODS=GET,POST,PUT,DELETE,OPTIONS
CORS_ALLOWED_HEADERS=Content-Type,Authorization
```

### Database Configuration

```bash
# Connection
PG_USER=postgres
PG_HOST=localhost
PG_DB=postgres
PG_PASSWORD=postgres
PG_PORT=5432

# Connection pool
DB_POOL_SIZE=20                # Max connections
DB_IDLE_TIMEOUT=30000          # Idle timeout (ms)
DB_CONNECTION_TIMEOUT=2000     # Connection timeout (ms)
DB_STATEMENT_TIMEOUT=30000     # Statement timeout (ms)
DB_QUERY_TIMEOUT=30000         # Query timeout (ms)
DB_MAX_RETRIES=3               # Max retry attempts
```

### Queue Processing

```bash
# Pop defaults
DEFAULT_TIMEOUT=30000          # Default pop timeout (ms)
MAX_TIMEOUT=60000              # Maximum pop timeout (ms)
DEFAULT_BATCH_SIZE=1           # Default batch size
BATCH_INSERT_SIZE=1000         # Batch size for bulk inserts

# Long polling
QUEUE_POLL_INTERVAL=100        # Poll interval (ms)
QUEUE_POLL_INTERVAL_FILTERED=1000  # Poll interval for filtered pops (ms)

# Queue defaults
DEFAULT_LEASE_TIME=300         # Lease time (seconds)
DEFAULT_RETRY_LIMIT=3          # Retry limit
DEFAULT_RETRY_DELAY=1000       # Retry delay (ms)
DEFAULT_MAX_SIZE=10000         # Max queue size
DEFAULT_TTL=3600               # TTL (seconds)
DEFAULT_PRIORITY=0             # Priority
DEFAULT_DELAYED_PROCESSING=0   # Delayed processing (seconds)
DEFAULT_WINDOW_BUFFER=0        # Window buffer (seconds)
```

### Background Jobs

```bash
LEASE_RECLAIM_INTERVAL=5000    # Lease reclamation (ms)
RETENTION_INTERVAL=300000      # Retention checks (ms)
RETENTION_BATCH_SIZE=1000      # Retention batch size
PARTITION_CLEANUP_DAYS=7       # Days before cleaning empty partitions
EVICTION_INTERVAL=60000        # Eviction checks (ms)
EVICTION_BATCH_SIZE=1000       # Eviction batch size
```

### WebSocket

```bash
WS_COMPRESSION=0               # Compression level
WS_MAX_PAYLOAD_LENGTH=16384    # Max payload (bytes)
WS_IDLE_TIMEOUT=60             # Idle timeout (seconds)
WS_MAX_CONNECTIONS=1000        # Max connections
WS_HEARTBEAT_INTERVAL=30000    # Heartbeat (ms)
```

### Encryption

```bash
# Generate key: openssl rand -hex 32
QUEEN_ENCRYPTION_KEY=<64-hex-chars>  # AES-256-GCM encryption key
```

### Client SDK

```bash
QUEEN_BASE_URL=http://localhost:6632
CLIENT_RETRY_ATTEMPTS=3
CLIENT_RETRY_DELAY=1000
CLIENT_RETRY_BACKOFF=2
CLIENT_POOL_SIZE=10
CLIENT_REQUEST_TIMEOUT=30000
```

### Queue Options

```javascript
{
  // Processing
  leaseTime: 300,              // Seconds before lease expires
  retryLimit: 3,               // Max retry attempts
  priority: 0,                 // Queue priority (higher = first)
  delayedProcessing: 0,        // Delay in seconds
  windowBuffer: 0,             // Buffer time for batching
  dlqAfterMaxRetries: true,    // Move to DLQ after max retries
  
  // Encryption (Queue-level)
  encryptionEnabled: false,    // Enable AES-256-GCM encryption
  
  // Retention (Partition-level)
  retentionSeconds: 0,                // Delete pending messages after X seconds
  completedRetentionSeconds: 0,       // Delete completed/failed after X seconds
  partitionRetentionSeconds: 0,       // Delete empty partitions after X seconds
  retentionEnabled: false,            // Enable retention
  
  // Eviction (Queue-level)
  maxWaitTimeSeconds: 0        // Evict messages older than X seconds
}
```

---

## 📚 Full Examples

### Example 1: Email Queue with Priority

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({
  baseUrls: ['http://localhost:6632']
});

// Configure queues with different priorities
await client.queue('emails-urgent', { 
  priority: 10, 
  leaseTime: 300,
  retryLimit: 5
});

await client.queue('emails-normal', { 
  priority: 5, 
  leaseTime: 300,
  retryLimit: 3
});

// Producer: Send emails
async function sendEmails() {
  // Urgent email
  await client.push('emails-urgent', {
    to: 'admin@company.com',
    subject: 'Critical Alert',
    body: 'System issue detected',
    timestamp: Date.now()
  });

  // Normal email
  await client.push('emails-normal', {
    to: 'user@example.com',
    subject: 'Welcome',
    body: 'Thanks for signing up',
    timestamp: Date.now()
  });
}

// Consumer: Process emails
async function processEmails() {
  // Urgent emails processed first (higher priority)
  for await (const email of client.take('emails-urgent', { 
    wait: true,
    timeout: 30000
  })) {
    try {
      console.log('Sending urgent email:', email.data.to);
      await sendEmail(email.data);
      await client.ack(email);
    } catch (error) {
      console.error('Failed to send email:', error);
      await client.ack(email, false, { error: error.message });
    }
  }
}

// Send batch of emails
await sendEmails();

// Start processing
processEmails().catch(console.error);
```

### Example 2: Task Pipeline

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({
  baseUrls: ['http://localhost:6632']
});

// Configure pipeline stages
await client.queue('stage-1-validate', { priority: 10 });
await client.queue('stage-2-process', { priority: 9 });
await client.queue('stage-3-finalize', { priority: 8 });

// Stage 1: Validate
async function validateStage() {
  for await (const msg of client.take('stage-1-validate', { wait: true })) {
    try {
      const validated = await validate(msg.data);
      await client.ack(msg);
      
      // Pass to next stage
      await client.push('stage-2-process', validated);
    } catch (error) {
      await client.ack(msg, false, { error: error.message });
    }
  }
}

// Stage 2: Process
async function processStage() {
  for await (const msg of client.take('stage-2-process', { wait: true })) {
    try {
      const processed = await process(msg.data);
      await client.ack(msg);
      
      // Pass to next stage
      await client.push('stage-3-finalize', processed);
    } catch (error) {
      await client.ack(msg, false, { error: error.message });
    }
  }
}

// Stage 3: Finalize
async function finalizeStage() {
  for await (const msg of client.take('stage-3-finalize', { wait: true })) {
    try {
      await finalize(msg.data);
      await client.ack(msg);
      console.log('Pipeline complete:', msg.data.id);
    } catch (error) {
      await client.ack(msg, false, { error: error.message });
    }
  }
}

// Start pipeline
Promise.all([
  validateStage(),
  processStage(),
  finalizeStage()
]);

// Add work to pipeline
await client.push('stage-1-validate', { id: 1, data: 'raw data' });
```

### Example 3: Event Streaming (Bus Mode)

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({
  baseUrls: ['http://localhost:6632']
});

// Configure event queue
await client.queue('events', { 
  priority: 10,
  leaseTime: 60
});

// Producer: Emit events
async function emitEvents() {
  await client.push('events', {
    type: 'order.created',
    orderId: 12345,
    userId: 789,
    amount: 99.99,
    timestamp: Date.now()
  });
}

// Consumer 1: Analytics Service
async function analyticsService() {
  for await (const event of client.take('events@analytics', {
    subscriptionMode: 'all',  // Replay all messages
    wait: true
  })) {
    console.log('[Analytics] Processing event:', event.data.type);
    await updateAnalytics(event.data);
    await client.ack(event, true, { group: 'analytics' });
  }
}

// Consumer 2: Notification Service
async function notificationService() {
  for await (const event of client.take('events@notifications', {
    subscriptionMode: 'new',  // Only new messages
    wait: true
  })) {
    console.log('[Notifications] Processing event:', event.data.type);
    await sendNotification(event.data);
    await client.ack(event, true, { group: 'notifications' });
  }
}

// Consumer 3: Audit Service
async function auditService() {
  for await (const event of client.take('events@audit', {
    subscriptionMode: 'all',  // Log everything
    wait: true
  })) {
    console.log('[Audit] Logging event:', event.data.type);
    await logToAudit(event.data);
    await client.ack(event, true, { group: 'audit' });
  }
}

// Start all services (they all see the same events)
Promise.all([
  analyticsService(),
  notificationService(),
  auditService()
]);

// Emit events
await emitEvents();
```

### Example 4: Batch Processing (High Throughput)

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({
  baseUrls: ['http://localhost:6632']
});

// Configure for batch processing
await client.queue('data-processing', {
  priority: 5,
  leaseTime: 600,        // 10 minutes for batch
  windowBuffer: 30       // Buffer for 30 seconds
});

// Producer: Send data
async function sendData() {
  const records = [];
  for (let i = 0; i < 100000; i++) {
    records.push({ id: i, value: Math.random() });
  }
  
  // Push in batches
  await client.push('data-processing/analytics', records);
}

// Consumer: HIGH PERFORMANCE batch processor using takeBatch()
async function batchProcessor() {
  const BATCH_SIZE = 5000;  // Large batches for 100k+ msg/s throughput
  
  // takeBatch() yields arrays directly - no manual batching needed!
  for await (const messages of client.takeBatch('data-processing/analytics', {
    batch: BATCH_SIZE,
    wait: true,
    timeout: 30000
  })) {
    try {
      console.log(`Processing batch of ${messages.length} records`);
      
      // Extract data
      const records = messages.map(m => m.data);
      
      // Bulk process (single DB operation)
      await bulkInsertToDatabase(records);
      
      // Batch acknowledge (single DB transaction!)
      await client.ack(messages);
      
      console.log(`✓ Batch complete in single transaction`);
    } catch (error) {
      console.error('Batch processing failed:', error);
      
      // Mark entire batch as failed (single transaction)
      await client.ack(messages, false, { error: error.message });
    }
  }
}

// Run
await sendData();
await batchProcessor();

// Performance characteristics:
// - Batch size 5000: ~100,000 messages/second
// - Single DB transaction per batch (fetch + ack)
// - Constant memory usage
// - No performance degradation as queue grows
```

### Example 5: Scheduled Jobs

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({
  baseUrls: ['http://localhost:6632']
});

// Configure with delayed processing
await client.queue('scheduled-jobs', {
  delayedProcessing: 3600,  // 1 hour delay
  priority: 5
});

// Schedule a job
async function scheduleReport() {
  await client.push('scheduled-jobs/daily-reports', {
    reportType: 'daily-sales',
    date: new Date().toISOString().split('T')[0],
    recipients: ['manager@company.com'],
    scheduledAt: Date.now()
  });
  
  console.log('Report scheduled for processing in 1 hour');
}

// Process scheduled jobs
async function processScheduledJobs() {
  for await (const job of client.take('scheduled-jobs/daily-reports', {
    wait: true
  })) {
    try {
      console.log('Generating report:', job.data.reportType);
      await generateReport(job.data);
      await client.ack(job);
    } catch (error) {
      await client.ack(job, false, { error: error.message });
    }
  }
}

await scheduleReport();
processScheduledJobs().catch(console.error);
```

### Example 6: Rate Limiting

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({
  baseUrls: ['http://localhost:6632']
});

await client.queue('api-calls', { 
  priority: 5,
  leaseTime: 60
});

// Producer: Queue API calls
async function queueApiCalls(calls) {
  await client.push('api-calls', calls);
}

// Consumer: Rate-limited processor (10 calls per second max)
async function rateLimitedProcessor() {
  const RATE_LIMIT = 10;  // calls per second
  const INTERVAL = 1000;  // 1 second
  
  let callsThisInterval = 0;
  let intervalStart = Date.now();
  
  for await (const call of client.take('api-calls', { wait: true })) {
    // Check if we need to wait
    if (callsThisInterval >= RATE_LIMIT) {
      const elapsed = Date.now() - intervalStart;
      if (elapsed < INTERVAL) {
        await new Promise(r => setTimeout(r, INTERVAL - elapsed));
      }
      callsThisInterval = 0;
      intervalStart = Date.now();
    }
    
    try {
      await makeApiCall(call.data);
      await client.ack(call);
      callsThisInterval++;
    } catch (error) {
      await client.ack(call, false, { error: error.message });
    }
  }
}

// Generate calls
const calls = Array.from({ length: 100 }, (_, i) => ({
  id: i,
  endpoint: '/api/data',
  method: 'GET'
}));

await queueApiCalls(calls);
rateLimitedProcessor().catch(console.error);
```

### Example 7: Enterprise Features

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({
  baseUrls: ['http://localhost:6632']
});

// Configure with all enterprise features
await client.queue('production-queue', {
  // Encryption
  encryptionEnabled: true,
  
  // Retention
  retentionSeconds: 86400,              // Delete pending after 24 hours
  completedRetentionSeconds: 3600,      // Delete completed after 1 hour
  retentionEnabled: true,
  
  // Eviction (SLA enforcement)
  maxWaitTimeSeconds: 600,              // Evict messages older than 10 minutes
  
  // Standard options
  priority: 10,
  leaseTime: 300,
  retryLimit: 3,
  dlqAfterMaxRetries: true
});

// Push sensitive data (will be encrypted)
await client.push('production-queue', {
  userId: 123,
  creditCard: '4111-1111-1111-1111',
  amount: 99.99,
  timestamp: Date.now()
});

// Process (data decrypted automatically)
for await (const message of client.take('production-queue', { wait: true })) {
  console.log('Processing encrypted data:', message.data.userId);
  await processPayment(message.data);
  await client.ack(message);
}
```

---

## 🧪 Testing

Queen includes a comprehensive test suite covering all features.

### Run Tests

```bash
# Start the server first
npm start

# Run all tests
node src/test/test-new.js

# Run specific test categories
node src/test/test-new.js core        # Core features
node src/test/test-new.js partition   # Partition locking
node src/test/test-new.js enterprise  # Enterprise features
node src/test/test-new.js bus         # Bus mode
node src/test/test-new.js edge        # Edge cases
node src/test/test-new.js advanced    # Advanced patterns

# Show help
node src/test/test-new.js help
```

### Test Coverage

The test suite verifies:

**Core Features:**
- Queue creation and configuration
- Single and batch message push
- Message take and acknowledgment
- Delayed processing
- Partition FIFO ordering

**Partition Locking:**
- Lock acquisition and release
- Bus mode partition locking
- Specific partition locking
- Namespace/task filtering with locking

**Enterprise Features:**
- AES-256-GCM encryption
- Message retention policies
- Message eviction
- Combined enterprise features

**Bus Mode:**
- Consumer groups
- Subscription modes (all, new, from)
- Consumer group isolation
- Mixed mode (queue + bus)

**Edge Cases:**
- Empty and null payloads
- Very large payloads
- Concurrent operations
- Lease expiration
- SQL injection prevention
- XSS prevention

**Advanced Patterns:**
- Multi-stage pipelines
- Fan-out/fan-in
- Priority scenarios
- Dead letter queue
- Circuit breaker
- Message deduplication
- Event sourcing

### Test Results

Example output:
```
🚀 Starting Queen Message Queue Test Suite
   Using the new minimalist Queen client interface
================================================================================

📦 CORE FEATURES
----------------------------------------
✅ Queue Creation Policy
✅ Single Message Push
✅ Batch Message Push
✅ Queue Configuration
✅ Take and Acknowledgment
✅ Delayed Processing
✅ Partition FIFO Ordering

🔒 PARTITION LOCKING
----------------------------------------
✅ Partition Locking
✅ Bus Partition Locking
✅ Specific Partition Locking
✅ Namespace Task Filtering
✅ Namespace Task Bus Mode

📈 Test Summary
================================================================================
Total: 42 | Passed: 42 | Failed: 0 | Duration: 45.2s
```

---

## 🤝 Contributing

We welcome contributions! Here's how to get started:

1. **Fork the repository**
2. **Create a feature branch**: `git checkout -b feature/amazing-feature`
3. **Make your changes**
4. **Run the test suite**: `node src/test/test-new.js`
5. **Commit your changes**: `git commit -m 'Add amazing feature'`
6. **Push to the branch**: `git push origin feature/amazing-feature`
7. **Open a Pull Request**

### Development Setup

```bash
# Clone your fork
git clone https://github.com/your-username/queen
cd queen

# Install dependencies
nvm use 22
npm install

# Initialize database
node init-db.js

# Start server
npm start

# Run tests
node src/test/test-new.js
```

### Code Style

- Use ES6+ features
- Follow existing code style
- Add comments for complex logic
- Write tests for new features

---

## 📄 License

Apache License 2.0 - see [LICENSE.md](LICENSE.md) for details.

---

## 🔗 Links

- **Repository**: [github.com/smartpricing/queen](https://github.com/smartpricing/queen)
- **Documentation**: See `docs/` directory
- **Issues**: [GitHub Issues](https://github.com/smartpricing/queen/issues)
- **API Reference**: [API.md](API.md)

---

## 📈 Performance

**Benchmarks** (PostgreSQL 16, Node.js 22, cursor-based consumption):
- **Throughput**: 100,000+ messages/second with batch operations
- **Latency**: < 10ms for immediate pop operations
- **Constant-time consumption**: O(batch_size) regardless of queue depth
- **Concurrent Connections**: 1,000+ long polling connections
- **Database**: Optimized with proper indexing and connection pooling

**Cursor-Based Architecture Benefits:**
- **No performance degradation**: Consistent speed whether queue has 1K or 1B messages
- **Predictable latency**: 150-200ms per batch throughout entire queue lifecycle
- **Efficient batch processing**: Direct cursor access eliminates table scans
- **Scalable to billions**: UUIDv7-based cursor positioning

**Additional Optimization Features:**
- Connection pooling with configurable size
- Resource caching for queue/partition lookups
- Batch operations for bulk inserts/updates (up to 10,000 messages per batch)
- Optimized SQL queries with proper indexes
- Event-driven architecture for minimal polling overhead
- Long polling for real-time message delivery
- SKIP LOCKED for lock-free concurrent consumption

---

## 🎯 Roadmap

- [ ] **Horizontal Scaling**: Better support for multiple server instances
- [ ] **Message Scheduling**: Cron-like scheduling for recurring jobs
- [ ] **Priority Lanes**: Dynamic priority adjustment based on load
- [ ] **Metrics Export**: Prometheus/Grafana integration
- [ ] **Admin API**: REST API for queue management
- [ ] **Client Libraries**: Python, Go, Java clients
- [ ] **Message Tracing**: Distributed tracing integration
- [ ] **Queue Templates**: Pre-configured queue patterns
- [ ] **GraphQL API**: Alternative to REST API
- [ ] **Kubernetes Operator**: Native K8s support

---

<div align="center">

**Queen Message Queue System** - Built for performance, reliability, and developer happiness 🚀

Made with ❤️ by [Smartpricing](https://github.com/smartpricing)

</div>
