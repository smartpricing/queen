# Queen - High-Performance Message Queue System

A modern, high-performance message queue system built with PostgreSQL and uWebSockets.js, featuring priority-based processing, advanced scheduling, and real-time monitoring.

![Queen Dashboard](assets/dashboard.png)

## 🚀 Features

- **🏗️ Flexible Architecture**: Queues → Partitions → Messages with optional namespace/task grouping
- **⚡ Priority Processing**: Queue and partition-level priorities with FIFO within partitions
- **🕒 Advanced Scheduling**: Delayed processing and window buffering
- **🔄 Reliable Processing**: Lease-based processing with automatic retry and dead letter queues
- **📊 Real-time Monitoring**: WebSocket dashboard with live metrics and analytics
- **🔒 Message Guarantees**: ACID transactions, idempotency, and no message loss
- **🚄 High Performance**: 10,000+ messages/second with sub-10ms latency
- **🌐 Long Polling**: Event-driven optimization for real-time message consumption
- **📦 Batch Operations**: Efficient bulk message processing with individual and batch consumer modes
- **🛠️ Client SDK**: Full-featured JavaScript client with retry logic and helpers

## 📋 Table of Contents

- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Core Concepts](#core-concepts)
- [API Reference](#api-reference)
- [Client SDK](#client-sdk)
- [Dashboard](#dashboard)
- [Examples](#examples)
- [Performance](#performance)
- [Configuration](#configuration)

## 🏃 Quick Start

### Prerequisites

- Node.js 22+
- PostgreSQL 12+

### Installation

```bash
# Clone the repository
git clone https://github.com/smartpricing/queen
cd queen

# Install dependencies
nvm use 22
npm install

# Set up environment (optional)
export PG_USER=postgres
export PG_HOST=localhost
export PG_DB=postgres
export PG_PASSWORD=postgres
export PG_PORT=5432
```

### Database Setup

```bash
# Initialize the database schema
node init-db.js
```

### Start the Server

```bash
# Optional: Enable encryption
export QUEEN_ENCRYPTION_KEY=$(openssl rand -hex 32)

# Start the Queen server
npm start
# Or use the startup script
./start.sh

# Server starts on http://localhost:6632
```

### Basic Usage

```javascript
import { createQueenClient } from '@dev.smartpricing/queen'

const client = createQueenClient({
  baseUrl: 'http://localhost:6632'
});

// Push a message
await client.push({
  items: [{
    queue: 'email-queue',
    partition: 'urgent',
    payload: { to: 'user@example.com', subject: 'Hello!' }
  }]
});

// Pop and process messages
const result = await client.pop({
  queue: 'email-queue',
  batch: 10,
  wait: true
});

for (const message of result.messages) {
  console.log('Processing:', message.data);
  await client.ack(message.transactionId, 'completed');
}
```

## 🏗️ Architecture

### System Overview

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Client SDK    │───▶│   Queen Server   │───▶│   PostgreSQL    │
└─────────────────┘    └──────────────────┘    └─────────────────┘
                              │
                              ▼
                       ┌──────────────────┐
                       │   Dashboard UI   │
                       └──────────────────┘
```

### Data Model

```
Queues (with optional namespace/task grouping)
  └── Partitions (FIFO ordering, priority-based selection)
       └── Messages (lease-based processing)
```

**Database Schema:**
- `queen.queues` - Top-level message containers with optional grouping
- `queen.partitions` - Subdivisions within queues where FIFO is maintained
- `queen.messages` - Individual messages with processing state

### Key Components

- **uWebSockets.js Server**: High-performance HTTP/WebSocket server
- **Queue Manager**: Core message processing logic with optimizations
- **Resource Cache**: In-memory caching for queue/partition lookups
- **Event Manager**: Real-time notifications for long polling
- **WebSocket Server**: Live dashboard updates and monitoring

## 💡 Core Concepts

### Queues and Partitions

**Queues** are the top-level organizational units. Each queue automatically gets a "Default" partition, and you can create additional partitions for different processing priorities or logical separation.

```javascript
// Messages go to "Default" partition if not specified
await client.push({
  items: [{ queue: 'orders', payload: { orderId: 123 } }]
});

// Explicit partition specification
await client.push({
  items: [{ 
    queue: 'orders', 
    partition: 'high-priority',
    payload: { orderId: 456, urgent: true } 
  }]
});
```

### Priority Processing

The system supports two levels of priority:

1. **Queue Priority**: Higher priority queues are processed first
2. **FIFO Within Partitions**: Messages within the same partition are always processed in order
3. **Partitions**: Partitions are now simple FIFO containers - all configuration is at the queue level

```javascript
// Configure queue with priority
await client.configure({
  queue: 'orders',
  options: { priority: 10 } // Higher number = higher priority
});
```

### Message Lifecycle

```
pending → processing → completed/failed → (retry) → dead_letter
```

1. **Pending**: Message is queued and waiting to be processed
2. **Processing**: Message is leased to a worker (with timeout)
3. **Completed**: Message was successfully processed
4. **Failed**: Message processing failed (may retry based on configuration)
5. **Dead Letter**: Message exceeded retry limits

### Lease-Based Processing

Messages are "leased" to workers for a specific duration. If not acknowledged within the lease time, they automatically return to pending status for retry.

```javascript
// Configure lease time (default: 300 seconds)
await client.configure({
  queue: 'long-tasks',
  options: { leaseTime: 600 } // 10 minutes
});
```

## 🔐 Advanced Concepts

### Partition Locking

Partition locking is a critical mechanism that ensures message processing isolation and prevents duplicate processing. When a consumer retrieves messages from a partition, that partition becomes "locked" to that consumer for the duration of the lease.

#### How Partition Locking Works

1. **Lock Acquisition**: When a consumer calls `pop()`, the system:
   - Checks for available messages in unlocked partitions
   - Acquires a lease on the partition(s) containing those messages
   - Records the lease with an expiration time based on the queue's `leaseTime`

2. **Lock Duration**: The partition remains locked until:
   - The consumer acknowledges all messages (releases the lock)
   - The lease expires (automatic release after `leaseTime` seconds)
   - The consumer explicitly releases the partition

3. **Lock Scope**: 
   - In **Queue Mode**: Each consumer gets a unique session, preventing any other consumer from accessing the same partition
   - In **Bus Mode**: Locks are per consumer group, allowing different groups to process the same messages independently

```javascript
// Example: Two consumers in queue mode
const consumer1 = await client.pop({ queue: 'orders' }); 
// Consumer 1 gets messages from partition A and locks it

const consumer2 = await client.pop({ queue: 'orders' }); 
// Consumer 2 gets messages from partition B (A is locked)

// After Consumer 1 acknowledges:
await client.ack(consumer1.messages[0].transactionId, 'completed');
// Partition A is now unlocked and available
```

### FIFO Ordering Guarantees

Queen provides strong FIFO (First-In-First-Out) ordering guarantees **within each partition**. This means:

#### Partition-Level FIFO

Messages within the same partition are always processed in the exact order they were received:

```javascript
// These messages will be processed in order 1, 2, 3
await client.push({
  items: [
    { queue: 'tasks', partition: 'user-123', payload: { step: 1 } },
    { queue: 'tasks', partition: 'user-123', payload: { step: 2 } },
    { queue: 'tasks', partition: 'user-123', payload: { step: 3 } }
  ]
});

// Consumer will always receive them in order 1, 2, 3
const result = await client.pop({ queue: 'tasks', partition: 'user-123' });
```

#### Cross-Partition Ordering

Messages in different partitions can be processed in parallel and have no ordering guarantees relative to each other:

```javascript
// These can be processed in any order relative to each other
await client.push({
  items: [
    { queue: 'tasks', partition: 'user-123', payload: { data: 'A' } },
    { queue: 'tasks', partition: 'user-456', payload: { data: 'B' } }
  ]
});
```

#### Use Cases for Partitioning

- **Per-User Processing**: Use user ID as partition to ensure all user operations are processed in order
- **Per-Resource Processing**: Use resource ID to maintain operation order for specific resources
- **Priority Lanes**: Use different partitions for different priority levels

### Consumer Groups (Bus Mode)

Consumer groups enable pub-sub messaging patterns where multiple independent consumers can process the same messages. This is ideal for scenarios like event streaming, audit logging, and analytics.

#### How Consumer Groups Work

1. **Independent Processing**: Each consumer group maintains its own:
   - Message status tracking
   - Partition leases
   - Retry counters
   - Processing state

2. **Message Visibility**: All consumer groups see all messages, but each group tracks which messages it has processed independently

3. **Partition Locking per Group**: Within a consumer group, partition locking still applies to prevent duplicate processing

```javascript
// Analytics service (Group A)
const analyticsResult = await client.pop({
  queue: 'events',
  consumerGroup: 'analytics-service'
});

// Audit service (Group B) - gets the same messages
const auditResult = await client.pop({
  queue: 'events',
  consumerGroup: 'audit-service'
});

// Billing service (Group C) - also gets the same messages
const billingResult = await client.pop({
  queue: 'events',
  consumerGroup: 'billing-service'
});
```

#### Consumer Group Subscription Modes

When a consumer group is created, it can specify when to start consuming messages:

```javascript
// Start from all existing messages
await client.pop({
  queue: 'events',
  consumerGroup: 'replay-service',
  subscriptionMode: 'all'
});

// Start from messages created after joining
await client.pop({
  queue: 'events',
  consumerGroup: 'realtime-service',
  subscriptionMode: 'new'
});

// Start from a specific timestamp
await client.pop({
  queue: 'events',
  consumerGroup: 'batch-processor',
  subscriptionFrom: '2024-01-01T00:00:00Z'
});
```

### Namespace and Task Filtering

Queen supports cross-queue message consumption through namespace and task filtering, with full partition locking support:

#### Namespace-Based Routing

Group related queues under a namespace and consume from all of them:

```javascript
// Configure multiple queues with the same namespace
await client.configure({
  queue: 'orders-processing',
  namespace: 'ecommerce',
  task: 'process',
  options: { leaseTime: 30 }
});

await client.configure({
  queue: 'inventory-updates',
  namespace: 'ecommerce',
  task: 'update',
  options: { leaseTime: 30 }
});

// Consume from all queues in the namespace
const messages = await client.pop({
  namespace: 'ecommerce'
}, { batch: 10 });
// Gets messages from both queues, with partition locking across all
```

#### Task-Based Routing

Filter messages by specific tasks across namespaces:

```javascript
// Consume only 'process' tasks from the ecommerce namespace
const messages = await client.pop({
  namespace: 'ecommerce',
  task: 'process'
}, { batch: 5 });
```

#### Partition Locking with Filters

When using namespace/task filtering:
- The system locks all partitions from which messages are retrieved
- Different consumers cannot access the same partitions until locks are released
- Consumer groups maintain independent locks

```javascript
// Consumer 1: Gets messages and locks partitions A, B, C
const result1 = await client.pop({ namespace: 'ecommerce' });

// Consumer 2: Gets messages from different partitions D, E (A, B, C are locked)
const result2 = await client.pop({ namespace: 'ecommerce' });

// No partition overlap between consumers
```

### Concurrency Control

Queen provides several mechanisms for controlling concurrent message processing:

#### 1. Partition-Based Concurrency

Control parallelism by the number of partitions:

```javascript
// Create multiple partitions for parallel processing
const partitions = ['worker-1', 'worker-2', 'worker-3', 'worker-4'];

// Distribute messages across partitions
await client.push({
  items: messages.map((msg, i) => ({
    queue: 'tasks',
    partition: partitions[i % partitions.length],
    payload: msg
  }))
});

// Each worker processes one partition
const worker1 = await client.pop({ queue: 'tasks', partition: 'worker-1' });
const worker2 = await client.pop({ queue: 'tasks', partition: 'worker-2' });
// Workers process in parallel without interference
```

#### 2. Lease-Based Concurrency

Automatic concurrency control through lease timeouts:

```javascript
// Configure short leases for quick tasks
await client.configure({
  queue: 'quick-tasks',
  options: { 
    leaseTime: 30,  // 30 seconds per message
    retryLimit: 3   // Retry up to 3 times
  }
});

// Long-running tasks need longer leases
await client.configure({
  queue: 'heavy-processing',
  options: { 
    leaseTime: 600,  // 10 minutes per message
    retryLimit: 1    // Retry only once
  }
});
```

#### 3. Batch Size Control

Limit concurrent processing per consumer:

```javascript
// Each consumer processes max 5 messages at a time
const batch = await client.pop({ 
  queue: 'tasks' 
}, { 
  batch: 5  // Limit to 5 concurrent messages
});

// Process batch
for (const message of batch.messages) {
  await processMessage(message);
  await client.ack(message.transactionId, 'completed');
}
```

### Message Visibility and Isolation

#### Queue Mode (Default)

In queue mode, messages are consumed competitively - once a consumer gets a message, no other consumer can see it:

```javascript
// Without consumer group - competitive consumption
const consumer1 = await client.pop({ queue: 'tasks' });
const consumer2 = await client.pop({ queue: 'tasks' });
// Each consumer gets different messages
```

#### Bus Mode (Consumer Groups)

In bus mode, all consumer groups see all messages:

```javascript
// With consumer groups - broadcast consumption
const service1 = await client.pop({ 
  queue: 'events',
  consumerGroup: 'service-1'
});

const service2 = await client.pop({ 
  queue: 'events',
  consumerGroup: 'service-2'
});
// Both services get the same messages
```

#### Mixed Mode

You can combine both patterns in the same system:

```javascript
// Competitive workers for processing
const worker = await client.pop({ queue: 'jobs' });

// Broadcast to monitoring services
const monitor = await client.pop({ 
  queue: 'jobs',
  consumerGroup: 'monitoring'
});

const analytics = await client.pop({ 
  queue: 'jobs',
  consumerGroup: 'analytics'
});
```

### Best Practices

#### 1. Partition Strategy

- **User-based**: Use user IDs as partitions for per-user ordering
- **Resource-based**: Use resource IDs for ordered operations on resources
- **Round-robin**: Use rotating partition names for load distribution
- **Priority-based**: Use separate partitions for different priority levels

#### 2. Consumer Group Design

- **Single Responsibility**: Each consumer group should have one clear purpose
- **Independent Processing**: Design groups to be independent of each other
- **Idempotent Operations**: Ensure operations can be safely retried

#### 3. Lease Management

- **Right-size Leases**: Set lease times slightly longer than expected processing time
- **Handle Timeouts**: Implement proper timeout handling and retries
- **Release Early**: Acknowledge messages as soon as processing completes

#### 4. Error Handling

```javascript
try {
  const messages = await client.pop({ queue: 'tasks' });
  
  for (const message of messages.messages) {
    try {
      await processMessage(message);
      await client.ack(message.transactionId, 'completed');
    } catch (error) {
      // Log error but don't ack - message will retry
      console.error('Processing failed:', error);
      await client.ack(message.transactionId, 'failed', error.message);
    }
  }
} catch (error) {
  console.error('Pop failed:', error);
}
```

## 🔌 API Reference

### Base URL
```
http://localhost:6632/api/v1
```

### Push Messages

**Endpoint:** `POST /api/v1/push`

```javascript
{
  "items": [
    {
      "queue": "email-queue",           // Required
      "partition": "urgent",            // Optional (defaults to "Default")
      "payload": {                      // Required: message data
        "to": "user@example.com",
        "subject": "Hello"
      },
      "transactionId": "uuid-here"      // Optional: for idempotency
    }
  ]
}
```

**Response:**
```javascript
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

**From Specific Partition:**
```
GET /api/v1/pop/queue/{queue}/partition/{partition}?wait=true&timeout=30000&batch=10
```

**From Any Partition in Queue:**
```
GET /api/v1/pop/queue/{queue}?wait=true&timeout=30000&batch=10
```

**With Namespace/Task Filter:**
```
GET /api/v1/pop?namespace=my-app&task=emails&wait=true&timeout=30000&batch=10
```

**Response:**
```javascript
{
  "messages": [
    {
      "id": "018e63b7-6165-453f-88ae-56effa177605",
      "transactionId": "4dfb0478-655b-4c91-bcd9-b7acacf0400f",
      "queue": "email-queue",
      "partition": "urgent",
      "data": { "to": "user@example.com", "subject": "Hello" },
      "retryCount": 0,
      "priority": 10,
      "createdAt": "2023-10-08T12:00:00.000Z",
      "options": { "leaseTime": 300 }
    }
  ]
}
```

### Acknowledge Messages

**Single Acknowledgment:**
```javascript
POST /api/v1/ack
{
  "transactionId": "uuid",
  "status": "completed",        // "completed" or "failed"
  "error": "optional error"     // Required if status is "failed"
}
```

**Batch Acknowledgment:**
```javascript
POST /api/v1/ack/batch
{
  "acknowledgments": [
    { "transactionId": "uuid1", "status": "completed" },
    { "transactionId": "uuid2", "status": "failed", "error": "Processing error" }
  ]
}
```

### Queue Configuration

```javascript
POST /api/v1/configure
{
  "queue": "email-queue",
  "partition": "urgent",        // Optional (defaults to "Default")
  "options": {
    "leaseTime": 600,           // Seconds before lease expires
    "retryLimit": 5,            // Max retry attempts
    "priority": 10,             // Partition priority (higher = first)
    "delayedProcessing": 60,    // Delay before message becomes available
    "windowBuffer": 30          // Buffer messages for batch processing
  }
}
```

### Analytics

```javascript
// Get all queues overview
GET /api/v1/analytics/queues

// Get queue statistics
GET /api/v1/analytics/queue/{queueName}

// Get namespace statistics
GET /api/v1/analytics?namespace={namespace}

// Get throughput metrics
GET /api/v1/analytics/throughput

// Get queue depths
GET /api/v1/analytics/queue-depths
```

## 📱 Client SDK

### Installation

```javascript
import { createQueenClient } from './src/client/queenClient.js';

const client = createQueenClient({
  baseUrl: 'http://localhost:6632',
  timeout: 30000,
  retryAttempts: 3,
  retryDelay: 1000
});
```

### Basic Operations

```javascript
// Configure a queue
await client.configure({
  queue: 'orders',
  options: {
    priority: 10,
    leaseTime: 600,
    retryLimit: 3
  }
});

// Push single message
await client.push({
  items: [{
    queue: 'orders',
    partition: 'high-priority',
    payload: { orderId: 123, amount: 99.99 }
  }]
});

// Push batch of messages
await client.push({
  items: [
    { queue: 'orders', payload: { orderId: 124 } },
    { queue: 'orders', payload: { orderId: 125 } },
    { queue: 'orders', payload: { orderId: 126 } }
  ]
});

// Pop messages with long polling
const result = await client.pop({
  queue: 'orders',
  wait: true,
  timeout: 30000,
  batch: 10
});

// Process messages
for (const message of result.messages) {
  try {
    await processOrder(message.data);
    await client.ack(message.transactionId, 'completed');
  } catch (error) {
    await client.ack(message.transactionId, 'failed', error.message);
  }
}
```

### Consumer Pattern

The SDK provides a convenient consumer helper for continuous message processing with two modes:

#### Individual Message Processing

Process messages one by one (default behavior):

```javascript
const stopConsumer = client.consume({
  queue: 'orders',
  partition: 'high-priority',
  handler: async (message) => {
    console.log('Processing order:', message.data.orderId);
    await processOrder(message.data);
    // Message is automatically acknowledged on success
  },
  options: {
    batch: 5,
    wait: true,
    timeout: 30000,
    stopOnError: false
  }
});

// Stop the consumer when needed
// stopConsumer();
```

#### Batch Message Processing

Process entire batches of messages at once for better performance:

```javascript
const stopConsumer = client.consume({
  queue: 'orders',
  partition: 'high-priority',
  handlerBatch: async (messages) => {
    console.log(`Processing batch of ${messages.length} orders`);
    
    // Process all messages in parallel
    await Promise.all(messages.map(async (message) => {
      console.log('Processing order:', message.data.orderId);
      await processOrder(message.data);
    }));
    
    // Or process sequentially if needed
    // for (const message of messages) {
    //   await processOrder(message.data);
    // }
    
    // All messages are automatically batch-acknowledged on success
  },
  options: {
    batch: 10,  // Larger batches for better throughput
    wait: true,
    timeout: 30000,
    stopOnError: false
  }
});
```

**Key Benefits of Batch Processing:**
- **Higher Throughput**: Process multiple messages simultaneously
- **Efficient Acknowledgments**: Single batch ACK instead of individual ACKs
- **Atomic Processing**: Either the entire batch succeeds or fails together
- **Reduced Network Overhead**: Fewer round trips to the server

**Important Notes:**
- Use either `handler` OR `handlerBatch`, not both
- In batch mode, if processing fails, all messages in the batch are marked as failed
- Batch size is controlled by the `batch` option (default: 1)

### Advanced Features

```javascript
// Pop with namespace filter (cross-queue priority)
const result = await client.pop({
  namespace: 'ecommerce',
  batch: 10,
  wait: true
});

// Batch acknowledgment
await client.ackBatch([
  { transactionId: 'uuid1', status: 'completed' },
  { transactionId: 'uuid2', status: 'failed', error: 'Invalid data' }
]);

// Message management
const messages = await client.messages.list({
  queue: 'orders',
  status: 'failed',
  limit: 100
});

await client.messages.retry('transaction-id');
await client.messages.moveToDLQ('transaction-id');
```

## 📊 Dashboard

The Queen system includes a comprehensive web dashboard for monitoring and management.

### Accessing the Dashboard

1. **Start the server**: `npm start`
2. **Open dashboard**: Navigate to `http://localhost:6632` in your browser
3. **WebSocket connection**: The dashboard connects via WebSocket for real-time updates

### Dashboard Features

#### 1. **System Overview**
- **Real-time Metrics**: Total messages, processing rate, system health
- **Queue Summary**: Active queues, pending messages, processing status
- **Performance Indicators**: Throughput, latency, error rates

#### 2. **Queue Management**
- **Queue List**: All queues with current status and message counts
- **Partition View**: Partitions within each queue with priority indicators
- **Message Counts**: Pending, processing, completed, failed, and dead letter counts
- **Priority Visualization**: Color-coded priority levels

#### 3. **Real-time Monitoring**
- **Live Updates**: WebSocket-powered real-time data updates
- **Throughput Charts**: Messages per second over time
- **Queue Depth Graphs**: Pending message counts with trend analysis
- **Lag Monitoring**: Processing time and queue lag metrics

#### 4. **Message Browser**
- **Message Search**: Filter by queue, partition, status, or time range
- **Message Details**: Full payload, metadata, and processing history
- **Retry Management**: Manually retry failed messages
- **Dead Letter Queue**: View and manage messages that exceeded retry limits

#### 5. **Analytics Dashboard**
- **Performance Metrics**: Detailed throughput and latency statistics
- **Queue Analytics**: Per-queue performance and usage patterns
- **Historical Data**: Trends and patterns over time
- **System Health**: Database connections, memory usage, error rates

#### 6. **Configuration Management**
- **Queue Configuration**: View and modify queue settings
- **Partition Settings**: Priority, lease time, retry limits
- **System Settings**: Global configuration options

### Dashboard Components

The dashboard is built with Vue.js and includes:

```
dashboard/
├── src/
│   ├── components/
│   │   ├── charts/           # Chart components
│   │   │   ├── QueueDepthChart.vue
│   │   │   ├── QueueLagChart.vue
│   │   │   └── ThroughputChart.vue
│   │   ├── cards/            # Metric cards
│   │   │   └── MetricCard.vue
│   │   ├── common/           # Shared components
│   │   │   └── ActivityFeed.vue
│   │   └── layout/           # Layout components
│   │       ├── AppHeader.vue
│   │       ├── AppLayout.vue
│   │       └── AppSidebar.vue
│   ├── views/                # Main pages
│   │   ├── Dashboard.vue     # System overview
│   │   ├── Queues.vue        # Queue management
│   │   ├── QueueDetail.vue   # Individual queue details
│   │   ├── Messages.vue      # Message browser
│   │   └── Analytics.vue     # Analytics dashboard
│   └── services/
│       ├── api.js            # API client
│       └── websocket.js      # WebSocket connection
```

### WebSocket API

The dashboard connects via WebSocket for real-time updates:

```javascript
// Connect to dashboard WebSocket
const ws = new WebSocket('ws://localhost:6632/ws/dashboard');

// Receive real-time updates
ws.onmessage = (event) => {
  const { event: eventType, data } = JSON.parse(event.data);
  
  switch (eventType) {
    case 'queue.depth.updated':
      updateQueueDepth(data);
      break;
    case 'message.processed':
      updateThroughput(data);
      break;
    case 'system.stats':
      updateSystemStats(data);
      break;
  }
};
```

## 📚 Examples

### Basic Email Queue

```javascript
// Configure email queue with priority
await client.configure({
  queue: 'emails-urgent',
  options: { priority: 10, leaseTime: 300 }
});

await client.configure({
  queue: 'emails-normal',
  options: { priority: 5, leaseTime: 300 }
});

// Send urgent email
await client.push({
  items: [{
    queue: 'emails',
    partition: 'urgent',
    payload: {
      to: 'admin@company.com',
      subject: 'System Alert',
      body: 'Critical system issue detected'
    }
  }]
});

// Process emails (urgent emails processed first)
const result = await client.pop({
  queue: 'emails',
  batch: 10,
  wait: true
});
```

### Delayed Job Processing

```javascript
// Configure queue with delayed processing
await client.configure({
  queue: 'scheduled-jobs',
  options: {
    delayedProcessing: 3600, // 1 hour delay
    priority: 5
  }
});

// Schedule a job for later processing
await client.push({
  items: [{
    queue: 'scheduled-jobs',
    partition: 'daily-reports',
    payload: {
      reportType: 'daily-sales',
      date: '2023-10-08',
      recipients: ['manager@company.com']
    }
  }]
});

// Job will not be available for processing until 1 hour later
```

### Batch Processing with Window Buffer

```javascript
// Configure for batch processing
await client.configure({
  queue: 'analytics',
  options: {
    windowBuffer: 60,  // Wait 60 seconds to batch messages
    priority: 3
  }
});

// Send multiple events
for (let i = 0; i < 100; i++) {
  await client.push({
    items: [{
      queue: 'analytics',
      partition: 'events',
      payload: { userId: i, action: 'page_view', timestamp: Date.now() }
    }]
  });
}

// Messages will be held for 60 seconds to allow batching
// Then all messages become available at once for efficient processing
```

### Multi-Queue Processing with Priorities

```javascript
// Set up multiple queues with different priorities
const queues = [
  { name: 'critical-alerts', priority: 100 },
  { name: 'user-notifications', priority: 50 },
  { name: 'background-tasks', priority: 10 }
];

for (const queue of queues) {
  await client.configure({
    queue: queue.name,
    options: { priority: queue.priority }
  });
}

// Consumer that processes all queues by priority
const stopConsumer = client.consume({
  namespace: 'my-app', // Process all queues in namespace by priority
  handler: async (message) => {
    console.log(`Processing ${message.queue}: ${message.data.type}`);
    await processMessage(message);
  },
  options: { batch: 5, wait: true }
});
```

### High-Throughput Batch Processing

```javascript
// Configure queue for high-throughput batch processing
await client.configure({
  queue: 'data-processing',
  options: {
    priority: 5,
    leaseTime: 600, // 10 minutes for batch processing
    windowBuffer: 30 // Buffer messages for 30 seconds
  }
});

// High-performance batch consumer
const stopConsumer = client.consume({
  queue: 'data-processing',
  partition: 'analytics',
  handlerBatch: async (messages) => {
    const startTime = Date.now();
    console.log(`Processing batch of ${messages.length} analytics events`);
    
    try {
      // Extract all payloads for batch processing
      const events = messages.map(msg => ({
        id: msg.transactionId,
        ...msg.data
      }));
      
      // Process entire batch efficiently
      await processAnalyticsBatch(events);
      
      const processingTime = Date.now() - startTime;
      console.log(`✅ Batch processed in ${processingTime}ms`);
      
    } catch (error) {
      console.error('Batch processing failed:', error);
      throw error; // Will mark all messages as failed
    }
  },
  options: {
    batch: 50,     // Process up to 50 messages at once
    wait: true,    // Use long polling
    timeout: 30000,
    stopOnError: false
  }
});

async function processAnalyticsBatch(events) {
  // Example: Bulk insert to database
  await database.analytics.insertMany(events);
  
  // Example: Send to external analytics service
  await analyticsService.sendBatch(events);
  
  // Example: Update aggregated metrics
  await updateMetrics(events);
}
```

## ⚡ Performance

### Benchmarks

- **Throughput**: 10,000+ messages/second
- **Latency**: < 10ms for immediate pop operations
- **Concurrent Connections**: 1,000+ long polling connections
- **Database**: Optimized for PostgreSQL with proper indexing

### Optimization Features

- **Connection Pooling**: Efficient database connection management
- **Resource Caching**: In-memory cache for queue/partition lookups
- **Batch Operations**: Bulk insert/update for high throughput
- **Optimized Queries**: Carefully crafted SQL with proper indexes
- **Event-Driven Architecture**: Minimal polling overhead

### Performance Tuning

```javascript
// Environment variables for performance tuning
export DB_POOL_SIZE=20              // Database connection pool size
export DB_IDLE_TIMEOUT=30000        // Connection idle timeout
export DB_CONNECTION_TIMEOUT=2000   // Connection establishment timeout
```

## 🔒 Enterprise Features

Queen includes three powerful enterprise features for production deployments:

### 1. Encryption
Protect sensitive data with AES-256-GCM encryption at the queue level.

**Setup:**
```bash
# Set encryption key (64 hex characters = 32 bytes)
export QUEEN_ENCRYPTION_KEY=$(openssl rand -hex 32)
```

**Configuration:**
```javascript
await client.configure({
  queue: 'sensitive-data',
  options: {
    encryptionEnabled: true
  }
});
```

### 2. Message Retention
Automatically clean up old messages to prevent storage bloat.

**Configuration:**
```javascript
await client.configure({
  queue: 'temp-queue',
  options: {
    retentionSeconds: 3600,          // Delete pending after 1 hour
    completedRetentionSeconds: 300,  // Delete completed after 5 minutes
    retentionEnabled: true
  }
});
```

**Environment:**
```bash
export RETENTION_INTERVAL=300000  # Cleanup interval in milliseconds
```

### 3. Message Eviction
Enforce SLAs by automatically evicting messages that wait too long.

**Configuration:**
```javascript
await client.configure({
  queue: 'time-sensitive',
  options: {
    maxWaitTimeSeconds: 60  // Evict messages older than 1 minute
  }
});
```

**Environment:**
```bash
export EVICTION_INTERVAL=60000  # Check interval in milliseconds
```

### Combined Example
```javascript
await client.configure({
  queue: 'production-queue',
  options: {
    // Encryption
    encryptionEnabled: true,
    
    // Retention
    retentionSeconds: 86400,
    completedRetentionSeconds: 3600,
    retentionEnabled: true,
    
    // Eviction
    maxWaitTimeSeconds: 600,
    
    // Standard options
    priority: 10,
    leaseTime: 300
  }
});
```

## ⚙️ Configuration

### Environment Variables

All configuration values have sensible defaults and can be overridden using environment variables. Configuration is centralized in `src/config.js`.

#### Server Configuration

```bash
# Server basics
PORT=6632                      # Server port (default: 6632)
HOST=0.0.0.0                   # Server host (default: 0.0.0.0)
WORKER_ID=worker-1             # Worker identifier (default: worker-${process.pid})
APP_NAME=queen-uws             # Application name for database connections

# CORS settings
CORS_MAX_AGE=86400             # CORS max age in seconds (default: 86400 = 24 hours)
CORS_ALLOWED_ORIGINS=*         # Allowed origins (default: *)
CORS_ALLOWED_METHODS=GET,POST,PUT,DELETE,OPTIONS  # Allowed methods
CORS_ALLOWED_HEADERS=Content-Type,Authorization   # Allowed headers
```

#### Database Configuration

```bash
# Connection settings
PG_USER=postgres               # PostgreSQL user (default: postgres)
PG_HOST=localhost              # PostgreSQL host (default: localhost)
PG_DB=postgres                 # PostgreSQL database (default: postgres)
PG_PASSWORD=postgres           # PostgreSQL password (default: postgres)
PG_PORT=5432                   # PostgreSQL port (default: 5432)

# Connection pool settings
DB_POOL_SIZE=20                # Max pool size (default: 20)
DB_IDLE_TIMEOUT=30000          # Idle connection timeout in ms (default: 30000)
DB_CONNECTION_TIMEOUT=2000     # Connection timeout in ms (default: 2000)
DB_STATEMENT_TIMEOUT=30000     # Statement timeout in ms (default: 30000)
DB_QUERY_TIMEOUT=30000         # Query timeout in ms (default: 30000)
DB_MAX_RETRIES=3               # Max retry attempts for queries (default: 3)
```

#### Queue Processing Configuration

```bash
# Pop operation defaults
DEFAULT_TIMEOUT=30000          # Default pop timeout in ms (default: 30000)
MAX_TIMEOUT=60000              # Maximum pop timeout in ms (default: 60000)
DEFAULT_BATCH_SIZE=1           # Default batch size for pop (default: 1)
BATCH_INSERT_SIZE=1000         # Batch size for bulk inserts (default: 1000)

# Long polling
QUEUE_POLL_INTERVAL=100        # Poll interval in ms (default: 100)
QUEUE_POLL_INTERVAL_FILTERED=1000  # Poll interval for filtered pops (default: 1000)

# Queue defaults
DEFAULT_LEASE_TIME=300         # Default lease time in seconds (default: 300 = 5 minutes)
DEFAULT_RETRY_LIMIT=3          # Default retry limit (default: 3)
DEFAULT_RETRY_DELAY=1000       # Default retry delay in ms (default: 1000)
DEFAULT_MAX_SIZE=10000         # Default max queue size (default: 10000)
DEFAULT_TTL=3600               # Default TTL in seconds (default: 3600 = 1 hour)
DEFAULT_PRIORITY=0             # Default queue priority (default: 0)
DEFAULT_DELAYED_PROCESSING=0   # Default delayed processing in seconds (default: 0)
DEFAULT_WINDOW_BUFFER=0        # Default window buffer in seconds (default: 0)

# Dead Letter Queue
DEFAULT_DLQ_ENABLED=false      # Enable DLQ by default (default: false)
DEFAULT_DLQ_AFTER_MAX_RETRIES=false  # Move to DLQ after max retries (default: false)

# Retention
DEFAULT_RETENTION_SECONDS=0    # Default retention for all messages (default: 0 = disabled)
DEFAULT_COMPLETED_RETENTION_SECONDS=0  # Retention for completed messages (default: 0)
DEFAULT_RETENTION_ENABLED=false  # Enable retention by default (default: false)

# Eviction
DEFAULT_MAX_WAIT_TIME_SECONDS=0  # Max wait time before eviction (default: 0 = disabled)
```

#### Background Jobs Configuration

```bash
# Job intervals
LEASE_RECLAIM_INTERVAL=5000    # Lease reclamation interval in ms (default: 5000)
RETENTION_INTERVAL=300000      # Retention check interval in ms (default: 300000 = 5 minutes)
RETENTION_BATCH_SIZE=1000      # Retention batch size (default: 1000)
PARTITION_CLEANUP_DAYS=7       # Days before cleaning empty partitions (default: 7)
EVICTION_INTERVAL=60000        # Eviction check interval in ms (default: 60000 = 1 minute)
EVICTION_BATCH_SIZE=1000       # Eviction batch size (default: 1000)

# WebSocket updates
QUEUE_DEPTH_UPDATE_INTERVAL=5000    # Queue depth update interval (default: 5000)
SYSTEM_STATS_UPDATE_INTERVAL=10000  # System stats update interval (default: 10000)
```

#### WebSocket Configuration

```bash
# WebSocket settings
WS_COMPRESSION=0               # Compression level (default: 0 = disabled)
WS_MAX_PAYLOAD_LENGTH=16384    # Max payload length in bytes (default: 16384 = 16KB)
WS_IDLE_TIMEOUT=60             # Idle timeout in seconds (default: 60)
WS_MAX_CONNECTIONS=1000        # Max concurrent connections (default: 1000)
WS_HEARTBEAT_INTERVAL=30000    # Heartbeat interval in ms (default: 30000)
```

#### Encryption Configuration

```bash
# Encryption settings
QUEEN_ENCRYPTION_KEY=<64-hex>  # 32-byte key as 64 hex characters
                                # Generate with: openssl rand -hex 32
                                # Required for encryption features
```

#### Client SDK Configuration

```bash
# Client defaults
QUEEN_BASE_URL=http://localhost:6632  # Default server URL
CLIENT_RETRY_ATTEMPTS=3        # Default retry attempts (default: 3)
CLIENT_RETRY_DELAY=1000        # Default retry delay in ms (default: 1000)
CLIENT_RETRY_BACKOFF=2         # Retry backoff multiplier (default: 2)
CLIENT_POOL_SIZE=10            # Client connection pool size (default: 10)
CLIENT_REQUEST_TIMEOUT=30000   # Request timeout in ms (default: 30000)
```

#### API Configuration

```bash
# Pagination
API_DEFAULT_LIMIT=100          # Default page size (default: 100)
API_MAX_LIMIT=1000             # Maximum page size (default: 1000)
API_DEFAULT_OFFSET=0           # Default offset (default: 0)
```

#### Analytics Configuration

```bash
# Analytics settings
ANALYTICS_RECENT_HOURS=24      # Hours to consider for recent stats (default: 24)
ANALYTICS_MIN_COMPLETED=5      # Min completed messages for stats (default: 5)
RECENT_MESSAGE_WINDOW=60       # Recent message window in seconds (default: 60)
RELATED_MESSAGE_WINDOW=3600    # Related message window in seconds (default: 3600)
MAX_RELATED_MESSAGES=10        # Max related messages to return (default: 10)
```

#### Monitoring Configuration

```bash
# Performance monitoring
ENABLE_REQUEST_COUNTING=true   # Enable request counting (default: true)
ENABLE_MESSAGE_COUNTING=true   # Enable message counting (default: true)
METRICS_ENDPOINT_ENABLED=true  # Enable /metrics endpoint (default: true)
HEALTH_CHECK_ENABLED=true      # Enable /health endpoint (default: true)
```

#### Logging Configuration

```bash
# Logging settings
ENABLE_LOGGING=true            # Enable logging (default: true)
LOG_LEVEL=info                 # Log level (default: info)
LOG_FORMAT=json                # Log format (default: json)
LOG_TIMESTAMP=true             # Include timestamps (default: true)
```

### Queue Options

```javascript
{
  // Standard Options
  "leaseTime": 300,           // Seconds before message lease expires
  "retryLimit": 3,            // Maximum retry attempts
  "priority": 0,              // Queue/partition priority (higher = first)
  "delayedProcessing": 0,     // Delay in seconds before message is available
  "windowBuffer": 0,          // Buffer time in seconds for batching
  "dlqAfterMaxRetries": true, // Move to dead letter queue after max retries
  
  // Encryption (Queue-level)
  "encryptionEnabled": false,  // Enable AES-256-GCM encryption for this queue
  
  // Retention (Partition-level)
  "retentionSeconds": 0,              // Delete pending messages after X seconds (0 = disabled)
  "completedRetentionSeconds": 0,     // Delete completed/failed messages after X seconds
  "partitionRetentionSeconds": 0,     // Delete empty partitions after X seconds
  "retentionEnabled": false,          // Enable retention for this partition
  
  // Eviction (Queue-level)
  "maxWaitTimeSeconds": 0     // Evict messages older than X seconds (0 = disabled)
}
```

## 🧪 Testing

### Run Core Feature Tests

```bash
# Start the server
npm start

# Run comprehensive test suite
node src/test/core-features-test.js

# Run full test suite (more detailed)
node src/test/comprehensive-test.js
```

### Test Results

The test suite verifies:
- ✅ Single and batch message push
- ✅ Queue configuration and options
- ✅ Pop operations (specific partition and queue-level)
- ✅ Delayed processing (2+ second delays)
- ✅ Partition priority ordering
- ✅ Consumer pattern with automatic acknowledgment
- ✅ Message acknowledgment and retry logic
- ✅ FIFO ordering within partitions

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run the test suite
5. Submit a pull request

## 📄 License

MIT License - see LICENSE file for details.

---

**Queen Message Queue System** - Built for performance, reliability, and scalability. 🚀