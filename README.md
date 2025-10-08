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
- **📦 Batch Operations**: Efficient bulk message processing
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
# Start the Queen server
npm start
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

1. **Queue Priority**: When using namespace/task filters, higher priority queues are processed first
2. **Partition Priority**: Within a queue, higher priority partitions are processed first
3. **FIFO Within Partitions**: Messages within the same partition are always processed in order

```javascript
// Configure partition with priority
await client.configure({
  queue: 'orders',
  partition: 'urgent',
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
  partition: 'high-priority',
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

The SDK provides a convenient consumer helper for continuous message processing:

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
    stopOnError: false,
    retryDelay: 5000
  }
});

// Stop the consumer when needed
// stopConsumer();
```

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
// Configure email queue with priority partitions
await client.configure({
  queue: 'emails',
  partition: 'urgent',
  options: { priority: 10, leaseTime: 300 }
});

await client.configure({
  queue: 'emails',
  partition: 'normal',
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
  partition: 'daily-reports',
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
  partition: 'events',
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

## ⚙️ Configuration

### Environment Variables

```bash
# Database Configuration
PG_USER=postgres
PG_HOST=localhost
PG_DB=postgres
PG_PASSWORD=postgres
PG_PORT=5432

# Performance Tuning
DB_POOL_SIZE=20
DB_IDLE_TIMEOUT=30000
DB_CONNECTION_TIMEOUT=2000

# Server Configuration
PORT=6632
HOST=0.0.0.0
```

### Queue Options

```javascript
{
  "leaseTime": 300,           // Seconds before message lease expires
  "retryLimit": 3,            // Maximum retry attempts
  "priority": 0,              // Queue/partition priority (higher = first)
  "delayedProcessing": 0,     // Delay in seconds before message is available
  "windowBuffer": 0,          // Buffer time in seconds for batching
  "dlqAfterMaxRetries": true  // Move to dead letter queue after max retries
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