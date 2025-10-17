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
- **Pipeline & Transaction APIs**: High-level fluent interfaces for complex workflows
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
- **Parallel processing** across partitions with `withConcurrency()`

**🏗️ Flexible Architecture**
- **Queue Mode**: Competitive consumption (traditional work queue)
- **Bus Mode**: Pub/sub with consumer groups (event streaming)
- **Mixed Mode**: Combine both patterns in the same system
- **Partitions**: FIFO ordering with parallel processing
- **Exactly-Once Processing**: Lease-based locking with automatic validation

**🔒 Enterprise Features**
- **AES-256-GCM Encryption**: Protect sensitive data at rest
- **Message Retention**: Automatic cleanup policies
- **Message Eviction**: SLA enforcement for time-sensitive tasks
- **Dead Letter Queue**: Handle failed messages gracefully
- **Automatic Lease Renewal**: For long-running tasks
- **Atomic Transactions**: Multi-operation consistency

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

- [First Queue](#first-queue)
- [Quick Start](#-quick-start)
- [Advanced Client APIs](#advanced-client-apis)
- [Standard Client Examples](#-client-examples)
- [Server Setup](#-server-setup)
- [Core Concepts](#-core-concepts)
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
  baseUrl: 'http://localhost:6632'  // Single server
  // OR for multiple servers with load balancing:
  // baseUrls: ['http://server1:6632', 'http://server2:6632'],
  // loadBalancingStrategy: 'round-robin', // or 'random', 'least-connections'
  // enableFailover: true
});

// Configure queue
await client.queue('tasks', {
  leaseTime: 300,     // 5 minutes to process each message
  retryLimit: 3       // Retry up to 3 times
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

## Advanced Client APIs

### Pipeline API - Fluent Message Processing

The Pipeline API provides a chainable interface for complex message processing workflows:

```javascript
// Simple message processing (one at a time)
await client.pipeline('my-queue')
  .take(100)                          // Take up to 100 messages
  .process(async (message) => {       // Process each message individually
    console.log('Processing:', message.data);
    return { processed: true };
  })
  .execute();

// Batch processing
await client.pipeline('my-queue')
  .take(100)
  .processBatch(async (messages) => { // Process messages as a batch
    console.log(`Processing ${messages.length} messages`);
    return messages.map(m => ({ processed: m.id }));
  })
  .execute();

// With automatic lease renewal for long-running tasks
await client.pipeline('my-queue')
  .take(50)
  .withAutoRenewal({ interval: 5000 })  // Renew lease every 5 seconds
  .process(async (message) => {
    // Long-running task - lease automatically renewed
    // Without this, if task takes > leaseTime, message may be redelivered
    await heavyComputation(message);
  })
  .execute();

// Parallel processing across partitions
await client.pipeline('my-queue')
  .take(100)
  .withConcurrency(4)  // 4 parallel workers
  .process(async (message) => {
    await processMessage(message);
  })
  .repeat({ continuous: true })  // Keep running continuously (default)
  .execute();

// With error handling
await client.pipeline('my-queue')
  .take(100)
  .process(async (message) => {
    if (message.data.invalid) {
      throw new Error('Invalid message format');
    }
    return await riskyOperation(message);
  })
  .onError(async (error, messages) => {
    console.error('Processing failed:', error.message);
    
    // Move failed messages to error queue
    await client.push('error-queue', { 
      error: error.message, 
      messages: messages.map(m => m.data),
      timestamp: Date.now()
    });
    
    // ACK as failed (will retry based on retryLimit)
    for (const msg of messages) {
      await client.ack(msg, false, { error: error.message });
    }
  })
  .execute();
```

#### Automatic Lease Renewal

By default, messages have a lease time (e.g., 5 minutes). If processing takes longer, the message may be redelivered to another consumer. Use `.withAutoRenewal()` to prevent this:

```javascript
// WITHOUT auto-renewal (default) - Risk of redelivery
await client.pipeline('video-processing')
  .take(10)
  .process(async (message) => {
    // If this takes > leaseTime, message may be processed twice!
    await longRunningTask(message);  
  })
  .execute();

// WITH auto-renewal - Safe for long tasks
await client.pipeline('video-processing')
  .take(10)
  .withAutoRenewal({ 
    interval: 30000  // Renew every 30 seconds (default)
  })
  .process(async (message) => {
    // Lease automatically renewed while processing
    await longRunningTask(message);  // Safe even if takes hours
  })
  .execute();
```

**When to use:**
- Video/audio processing
- Large file operations
- Machine learning inference
- Any task that might exceed the lease time

**Important:**
- Auto-renewal stops when message is ACKed or process crashes
- Set interval < lease time (e.g., renew at 1/3 of lease time)
- Only works within pipeline API

#### Error Handling in Pipeline

The `onError` handler provides robust error recovery:

**Key Behaviors:**
- Catches errors from `process()` or `processBatch()`
- Receives the error and affected messages
- Pipeline continues after error handler (doesn't stop)
- Without `onError`, errors stop the pipeline

**Error Recovery Strategies:**
```javascript
// Strategy 1: Retry with backoff
.onError(async (error, messages) => {
  // ACK as failed - will retry based on retryLimit
  await client.ack(messages, false);
})

// Strategy 2: Move to Dead Letter Queue
.onError(async (error, messages) => {
  for (const msg of messages) {
    if (msg.retryCount >= 3) {
      await client.push('dlq', { original: msg, error: error.message });
      await client.ack(msg, true); // Remove from queue
    } else {
      await client.ack(msg, false); // Retry
    }
  }
})

// Strategy 3: Skip and continue
.onError(async (error, messages) => {
  console.error(`Skipping ${messages.length} messages: ${error.message}`);
  await client.ack(messages, true); // ACK as success to remove
})
```

**Important Notes:**
- With `atomically()`: atomic operations only run on success
- In `repeat()` mode: errors don't stop continuous processing
- Batch processing: error affects all messages in the batch
- Parallel workers: each worker has independent error handling

### Transaction API - Atomic Operations

Execute multiple operations atomically with lease validation:

```javascript
// Atomic ACK + PUSH
const messages = await client.takeSingleBatch('input-queue');

await client.transaction()
  .ack(messages)                           // ACK input messages
  .push('output-queue', processedResults)  // Push to output
  .extend(leaseId)                        // Extend another lease
  .commit();                               // Execute atomically

// Pipeline with custom atomic operations
await client.pipeline('my-queue')
  .take(50)
  .process(async (message) => {
    return await transform(message);
  })
  .atomically((tx, originalMessages, processedMessages) => {
    tx.ack(originalMessages)
      .push('output-queue', processedMessages)
      .push('audit-queue', { 
        timestamp: Date.now(), 
        count: processedMessages.length 
      });
  })
  .execute();
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
  baseUrl: 'http://localhost:6632',
  timeout: 30000,
  retryAttempts: 3
});

// Configure with options
await client.queue('orders', {
  priority: 10,                // Higher priority queues processed first
  leaseTime: 300,              // 5 minutes to process each message
  retryLimit: 3,               // Retry up to 3 times
  windowBuffer: 0,             // No delay (immediate processing)
  retentionSeconds: 86400,     // Keep messages for 24 hours
  completedRetentionSeconds: 3600, // Keep completed messages for 1 hour
  partitions: 10,              // Create 10 partitions for parallel processing
  maxWaitTimeSeconds: 600      // Evict messages waiting > 10 minutes
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

// Take single batch (convenience method)
const messages = await client.takeSingleBatch('orders', { batch: 100 });
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

// Batch acknowledgment (with lease validation)
await client.ack(messages);  // Pass array for batch ack
```

#### 5. Renew Leases (for long-running tasks)

```javascript
// Renew lease for a single message
const result = await client.renewLease(message);
console.log(`Lease renewed until: ${result.newExpiresAt}`);

// Renew by lease ID directly
await client.renewLease('lease-uuid-123');

// Renew multiple messages at once
const results = await client.renewLease(messages);
results.forEach(r => {
  if (r.success) {
    console.log(`Renewed ${r.leaseId} until ${r.newExpiresAt}`);
  } else {
    console.error(`Failed to renew ${r.leaseId}: ${r.error}`);
  }
});

// Example: Manual renewal during long processing
for await (const message of client.take('my-queue')) {
  // Set up periodic renewal
  const renewalInterval = setInterval(async () => {
    await client.renewLease(message);
  }, 30000); // Renew every 30 seconds
  
  try {
    await longRunningTask(message);
    await client.ack(message);
  } finally {
    clearInterval(renewalInterval);
  }
}
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

console.log('Shutdown complete');
```

---

## 🖥️ Server Setup

### Environment Variables

```bash
# Server configuration
PORT=6632                                    # Server port
HOST=0.0.0.0                                # Server host
WORKER_ID=srv-1                             # Unique server instance ID
APP_NAME=queen-mq                           # Application name

# Database connection
PG_USER=postgres                            # PostgreSQL user
PG_HOST=localhost                           # PostgreSQL host
PG_DB=postgres                              # PostgreSQL database
PG_PASSWORD=postgres                        # PostgreSQL password
PG_PORT=5432                                # PostgreSQL port
PG_USE_SSL=false                            # Enable SSL for PostgreSQL
DB_POOL_SIZE=150                            # Connection pool size
DB_IDLE_TIMEOUT=30000                       # Idle connection timeout (ms)
DB_CONNECTION_TIMEOUT=2000                  # Connection timeout (ms)
DB_STATEMENT_TIMEOUT=30000                  # Statement timeout (ms)

# Queue configuration
DEFAULT_TIMEOUT=30000                       # Default pop timeout (ms)
MAX_TIMEOUT=60000                           # Maximum pop timeout (ms)
DEFAULT_BATCH_SIZE=1                        # Default batch size
BATCH_INSERT_SIZE=1000                      # Batch insert size for push operations
DEFAULT_LEASE_TIME=300                      # Default lease time (seconds)
DEFAULT_RETRY_LIMIT=3                       # Default retry limit
DEFAULT_RETRY_DELAY=1000                    # Default retry delay (ms)
DEFAULT_PRIORITY=0                          # Default message priority
DEFAULT_WINDOW_BUFFER=0                     # Default window buffer (seconds)

# Long polling configuration
QUEUE_POLL_INTERVAL=100                     # Initial poll interval (ms)
QUEUE_MAX_POLL_INTERVAL=2000               # Max poll interval after backoff (ms)
QUEUE_BACKOFF_THRESHOLD=5                   # Empty polls before backoff
QUEUE_BACKOFF_MULTIPLIER=2                  # Exponential backoff multiplier

# Encryption
QUEEN_ENCRYPTION_KEY=                       # 32-byte hex key for AES-256-GCM encryption

# Retention & Eviction
DEFAULT_RETENTION_SECONDS=0                 # Message retention (0 = disabled)
DEFAULT_COMPLETED_RETENTION_SECONDS=0       # Completed message retention
RETENTION_INTERVAL=300000                   # Retention service interval (ms)
RETENTION_BATCH_SIZE=1000                   # Retention batch size
EVICTION_INTERVAL=60000                     # Eviction service interval (ms)
EVICTION_BATCH_SIZE=1000                    # Eviction batch size
METRICS_RETENTION_DAYS=90                   # Metrics retention period

# System Events
QUEEN_SYSTEM_EVENTS_ENABLED=false          # Enable system event propagation
QUEEN_SYSTEM_EVENTS_BATCH_MS=10            # Event batching window (ms)
QUEEN_SYSTEM_EVENTS_SYNC_TIMEOUT=30000     # Startup sync timeout (ms)

# WebSocket configuration
WS_COMPRESSION=0                            # WebSocket compression level
WS_MAX_PAYLOAD_LENGTH=16384                # Max WebSocket payload (bytes)
WS_IDLE_TIMEOUT=60                         # WebSocket idle timeout (seconds)
WS_MAX_CONNECTIONS=1000                    # Max WebSocket connections
WS_HEARTBEAT_INTERVAL=30000                # WebSocket heartbeat interval (ms)

# API configuration
MAX_BODY_SIZE=104857600                    # Max request body size (100MB)
API_DEFAULT_LIMIT=100                      # Default pagination limit
API_MAX_LIMIT=1000                         # Max pagination limit
CORS_MAX_AGE=86400                         # CORS max age (seconds)
CORS_ALLOWED_ORIGINS=*                     # CORS allowed origins
CORS_ALLOWED_METHODS=GET,POST,PUT,DELETE,OPTIONS
CORS_ALLOWED_HEADERS=Content-Type,Authorization

# Monitoring & Logging
ENABLE_REQUEST_COUNTING=true               # Enable request metrics
ENABLE_MESSAGE_COUNTING=true               # Enable message metrics
METRICS_ENDPOINT_ENABLED=true              # Enable metrics endpoint
HEALTH_CHECK_ENABLED=true                  # Enable health check endpoint
ENABLE_LOGGING=true                        # Enable logging
LOG_LEVEL=info                             # Log level (debug|info|warn|error)
LOG_FORMAT=json                            # Log format (json|text)
```

### Programmatic Server

```javascript
import { createQueenServer } from 'queen-mq/server';

const server = await createQueenServer({
  port: 6632,
  database: {
    connectionString: 'postgresql://...',
    poolSize: 20
  },
  encryption: {
    key: 'your-32-byte-hex-key'
  },
  features: {
    retention: true,
    eviction: true
  }
});

await server.start();
```

---

## 🔑 Core Concepts

### Message Flow

1. **Push**: Messages are encrypted and stored in PostgreSQL with metadata
2. **Pop/Take**: Messages are leased to consumers with automatic lock management
3. **Process**: Consumers process messages with automatic lease renewal
4. **ACK**: Messages are marked complete and leases are released
5. **Retry**: Failed messages are retried with exponential backoff

### Partition & Lease Management

- Each queue can have multiple partitions for parallel processing
- Consumers acquire exclusive leases on partitions
- Leases include unique IDs for validation and fencing
- Automatic lease renewal for long-running tasks
- Dead letter queue for exhausted retries

### Scalability

- Horizontal scaling with multiple server instances
- Partition-based parallelism
- Connection pooling and query optimization
- WebSocket support for real-time updates
- Efficient batch operations

---

## 📚 HTTP API Reference

### Queue Management

```bash
# Create/configure queue
curl -X POST http://localhost:6632/api/v1/configure \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "my-queue",
    "options": {
      "maxRetries": 3,
      "visibilityTimeout": 30000
    }
  }'

# Delete queue
curl -X DELETE http://localhost:6632/api/v1/configure/my-queue
```

### Message Operations

```bash
# Push messages
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "my-queue",
  "messages": [
      { "data": "message 1" },
      { "data": "message 2", "priority": 100 }
    ]
  }'

# Pop messages (take)
curl -X POST http://localhost:6632/api/v1/pop \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "my-queue",
    "batch": 10,
    "visibilityTimeout": 30000
  }'

# Acknowledge messages
curl -X POST http://localhost:6632/api/v1/ack \
  -H "Content-Type: application/json" \
  -d '{
    "queue": "my-queue",
    "transactionId": "msg-transaction-id",
  "status": "completed",
    "leaseId": "lease-uuid"
  }'
```

### Advanced Operations

```bash
# Atomic transaction
curl -X POST http://localhost:6632/api/v1/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "operations": [
      {
        "type": "ack",
        "queue": "input-queue",
        "transactionId": "msg-id",
        "status": "completed"
      },
      {
        "type": "push",
        "queue": "output-queue",
        "messages": [{"data": "processed"}]
      }
    ],
    "requiredLeases": ["lease-uuid-1", "lease-uuid-2"]
  }'

# Extend lease
curl -X POST http://localhost:6632/api/v1/lease/lease-uuid/extend \
  -H "Content-Type: application/json" \
  -d '{}'

# Get queue status
curl http://localhost:6632/api/v1/status/my-queue
```

---

## 📊 Dashboard

Access the real-time dashboard at `http://localhost:6632/`

### Features
- Real-time queue metrics
- Message browser with search
- Consumer group monitoring
- System health indicators
- Performance graphs

### WebSocket Events
```javascript
const ws = new WebSocket('ws://localhost:6632/ws/dashboard');
ws.on('message', (data) => {
  const event = JSON.parse(data);
  console.log('Queue event:', event);
});
```

---

## 🧪 Testing

```bash
# Run test suite
npm test

# Run specific test
npm test -- --grep "Pipeline"

# Benchmark
npm run benchmark
```

---

## 🤝 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup and guidelines.

---

## 📄 License

Apache 2.0 - see [LICENSE.md](LICENSE.md)
