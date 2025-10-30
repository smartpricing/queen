# Queen MQ - PostgreSQL-backed C++ Message Queue

<div align="center">

**A modern, high-performance message queue system built on PostgreSQL**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE.md)
[![Node](https://img.shields.io/badge/node-%3E%3D22.0.0-brightgreen.svg)](https://nodejs.org/)

[Quick Start](#one-single-example) • [Complete V2 Guide](client-js/client-v2/README.md) • [Webapp](#webapp) • [Server Setup](#install-the-server-and-configure-it) • [HTTP API](#raw-http-api)

<p align="center">
  <img src="assets/queen-logo.svg" alt="Queen Logo" width="120" />
</p>

</div>

---

Why "Queen"? Because years ago, when I first read the word "queue" in my mind, I read it as "queen".

---

## Introduction

QueenMQ is a queue system written in C++ and backed by PostgreSQL, born from the need to manage many FIFO partitions for Smartchat with solid guarantees around delivery and failure handling. During the initial development, I realized that with a few simple additions to the original design, I could build a very powerful and flexible queue system. This project is almost entirely written by AI, with my supervision—only the test files (or a good part of them) are manually written.

Here are the main features:
- Unlimited FIFO partitions within queues
- Queue semantics like RabbitMQ
- Consumer groups over queues like Kafka
- Allows for all the patterns you need, from simple queues to complex workflows and request/response patterns
- QoS levels: Exactly-once delivery (with transactionId), at-least-once delivery, and at-most-once delivery
- Subscription modes for replay (new messages only or from a specific timestamp) and message history control
- Transactions between operations (push and ack mainly) for atomicity
- Dead letter queue for failure handling
- Lease renewal for long-running tasks
- Message tracing for debugging workflows
- Encryption of messages at DB level
- A nice webapp for monitoring and managing the system

The system consists of a PostgreSQL database, a replicated server that can be scaled horizontally (though it won't be the bottleneck), and a client library for interacting with the server. All client-server communication happens over HTTP, and the client library is written in JavaScript (support for other languages is planned). There's also a modern Vue 3 web app for monitoring and managing the system.

With proper batching, the system can handle +200k **messages** per second (not req/s) on modest hardware.

Main documentation:
- [Client Guide](client-js/client-v2/README.md)
- [Server Guide](server/README.md)
- [API Reference](API.md)
- [Webapp](webapp/README.md)

## Concepts

Although the system is designed to be simple to use, there are some concepts that are important to understand to use the system effectively.

### Queues

Queues are a way to organize your messages into logical groups. Each queue is like a container for messages. You can have as many queues as you want, and you can configure each queue with different settings, like the lease time, retry limit, encryption, retention, priority, delayed processing, window buffer and retention.

### Partitions

Partitions are a way to organize your messages into logical ordered groups. Each partition is like a separate queue inside a queue, and messages in the same partition are guaranteed to be processed in order. Only one consumer/client can acquire the lock for a partition at a time. You can have as many partitions as you want, and you can have as many consumers/clients as you want. If the lease in not acknowledged in time, the message is released and can be acquired by another consumer/client. Use renewLease() to renew the lease during long-running tasks. Each different consumer group can acquire the lock for a partition independently, so you can have as many consumer groups as you want.

### Default partition and consumer group

The default partition (if not specified) is `Default`. If you don't specify a consumer group, the messages are processed in queue mode, that is creating a default consumer group named `__QUEUE_MODE__`. Each queue can be used at the same time by multiple consumer groups, and each consumer group can process messages from multiple partitions.

### Consumer groups

Consumer groups are a way to process messages for different purposes. Each consumer group is like a separate queue, and messages in the same consumer group partition are guaranteed to be processed in order. Each consumer group tracks its own position in the queue. You can start a consumer group from the beginning of the queue, from a specific timestamp, or only new messages.

### Subscription modes

Subscription modes are a way to control the message history that is processed. You can choose to process all messages (including historical ones), only new messages, or messages from a specific timestamp. Subscription modes are only available when using consumer groups.

### Long polling (waiting for messages)

The client works with the pull model for pop operations, meaning that you need to explicitly request messages from the queue. Pop and consume mehtods can "wait" server side for messages to be available. When the method is called with wait=true, the method will block until messages are available or the timeout is reached. When the timeout is reached, the method returns an empty array. Long polling is a very efficient way to wait for messages, and it is the recommended way to consume messages.

### Lease renewal

Lease renewal is a way to keep the lock for a partition or consumer group alive. You can use lease renewal to prevent the lock from expiring and being acquired by another consumer/client.

### Ack and Nack

Ack and Nack are the way to acknowledge or not a message. Ack means that the message has been processed successfully, and Nack means that the message has not been processed successfully. Ack/Nack requires the partitionId, the transactionId and the leaseId to be specified. If the ack is coming from a consumer group, the consumer group name is also required. This logic is handled automatically by the client library, you don't need to worry about it. If the message has already been acknowledged, the ack will be ignored. Based on the configuration, the message can be automatically acknowledged or manually acknowledged.

### Transactions

Transactions are a way to ensure that a group of operations are atomic. You can use transactions to ensure that a group of operations are executed together, and if one of the operations fails, the entire transaction is rolled back. Transactions are useful to achieve the exactly-once guarantee. You can process a message on a queue, and forward to the next queue only if the consumer lease is still valid, concatenating ack and push into a single transaction.

### Dead letter queue

The dead letter queue is a way to handle messages that are not processed successfully. When a message is not processed successfully, it is moved to the dead letter queue. Messages goes in the DLQ when they are NACK after the retry limit is reached. You configure the retry limit for each queue at queue config.

## Comparison with RabbitMQ, Kafka, and NATS

For users familiar with existing message queue systems, here's how Queen's semantics and usage patterns compare:

### Conceptual Mapping

| Queen Concept | RabbitMQ Equivalent | Kafka Equivalent | NATS Equivalent |
|---------------|---------------------|------------------|-----------------|
| Queue | Queue | Topic | Stream (JetStream) |
| Partition | N/A (queues are single-consumer by default) | Partition | N/A |
| Consumer Group | Competing Consumers pattern | Consumer Group | Queue Group |
| Queue Mode (no group) | Exclusive consumer | N/A (always uses groups) | Single subscriber |
| Lease | Message TTL / Visibility timeout | N/A (commit-based) | Ack wait / nak delay |
| Ack/Nack | Ack/Nack | Commit offset | Ack/Nak |
| Transaction | Publisher confirms + consumer acks | Transactional producer/consumer | N/A |
| Dead Letter Queue | Dead Letter Exchange | N/A (manual) | N/A (manual) |

### Semantic Differences

**Message Consumption Model:**

**RabbitMQ**: Messages are **deleted** after acknowledgment. Each message is delivered to one consumer (competing consumers). No message replay - once acked, it's gone.

**Kafka**: Messages are **never deleted** by consumption. Consumer groups track their offset in the log. Multiple groups can independently consume the same messages. Replay is core to the model.

**NATS**: Core NATS has **no persistence** (fire-and-forget). JetStream adds persistence with consumer-tracked positions similar to Kafka, but simpler.

**Queen**: **Hybrid approach** - supports BOTH semantics on the same queue:
- **Queue mode** (default, no consumer group): RabbitMQ-style - messages are locked per partition, one consumer at a time, no replay
- **Consumer group mode**: Kafka-style - multiple groups can independently consume with replay from timestamp/beginning

```javascript
// Queue mode - RabbitMQ semantics (exclusive lock, no replay)
await queen.queue('tasks').partition('user-123').pop()

// Consumer group mode - Kafka semantics (shared log, replay, multiple groups)
await queen.queue('tasks').group('analytics').from('beginning').consume(handler)
await queen.queue('tasks').group('billing').from('beginning').consume(handler)
// Both groups process the same messages independently
```

**Partition Ordering:**

**RabbitMQ**: **No ordering guarantees** across the queue. Single-consumer queues maintain order, but scaling requires splitting queues manually.

**Kafka**: **Strict ordering per partition**. Partitions are the unit of parallelism. You explicitly assign partition keys.

**Queen**: **Strict ordering per partition**, like Kafka. Partitions are user-defined strings (not numeric):
```javascript
// Each customer gets their own ordered stream
.partition(`customer-${customerId}`)

// Kafka-style numeric partitions also work
.partition('0'), .partition('1'), etc.
```

**Consumer Concurrency:**

**RabbitMQ**: Concurrency via **multiple consumers** on the same queue (competing consumers). Each consumer is independent.

**Kafka**: Concurrency via **partition assignment**. One consumer per partition in a group. Rebalancing required to add consumers.

**Queen**: Concurrency via **partitions** (Kafka-style) OR **client-side workers**:
```javascript
// Kafka-style: spread across partitions, one consumer per partition
await queen.queue('tasks').group('workers').concurrency(10).consume(handler)

// RabbitMQ-style: one partition, parallel workers on client
await queen.queue('tasks').partition('default').concurrency(10).each().consume(handler)
```

**Message Acknowledgment:**

**RabbitMQ**: **Per-message ack/nack**. Nack returns message to queue immediately or to DLQ.

**Kafka**: **Offset commits**. Committing offset N means "all messages up to N are done." No per-message granularity.

**NATS JetStream**: **Per-message ack/nak**. Nak can delay redelivery.

**Queen**: **Per-message ack/nack with lease**, similar to RabbitMQ but with additional guarantees:
- Messages are locked with a lease when popped
- Must ack/nack within lease time or message auto-releases
- Nack increments retry count, moves to DLQ after limit
- Lease can be renewed during processing

```javascript
// Long-running task with lease renewal
await queen
  .queue('tasks')
  .renewLease(true, 2000) // Auto-renew every 2s
  .consume(async (msg) => {
    // Process for 30 seconds - lease kept alive automatically
  })
```

**Transactions:**

**RabbitMQ**: **Publisher confirms + consumer acks**. Not atomic across operations. No native support for "consume from A, produce to B" atomically.

**Kafka**: **Transactional producer** for atomic multi-partition writes. **Transactional consumer** for exactly-once via offset commits + writes. Complex to implement correctly.

**Queen**: **Atomic ack + push** - PostgreSQL ACID transactions enable true atomicity:
```javascript
// Atomic: ack from queue A, push to queue B
// If either fails, both rollback
await queen
  .transaction()
  .ack(messageFromA)
  .queue('queueB').push([newMessage])
  .commit()
```

This is simpler than Kafka's transactional API and more powerful than RabbitMQ's confirms.

**Message Replay:**

**RabbitMQ**: **No replay**. Messages are removed after ack. Plugins like message history exist but aren't core.

**Kafka**: **Full replay**. Consumers can reset offsets to any point. Core use case for event sourcing.

**NATS JetStream**: **Replay supported**. Can reset consumer position.

**Queen**: **Replay via consumer groups**:
```javascript
// Start from beginning (Kafka-style)
.group('new-processor').from('beginning')

// Start from timestamp
.group('backfill').from(new Date('2024-01-01'))

// Only new messages (default)
.group('realtime').from('new')
```

## One single example

> 📖 **[Complete Guide: client-js/client-v2/README.md](client-js/client-v2/README.md)** - Full tutorial with all features!

```javascript
import { Queen } from 'queen-mq'

// Connect to Queen
const queen = new Queen('http://localhost:6632')

// Create a queue
await queen
.queue('critical-task')
.config({
  leaseTime: 10, // 10 seconds to process the messages (seconds)
})
.create()

// Push messages
await queen
.queue('critical-task')
.partition('tenant-123')
.push([
  { 
    transactionId: 'my-id', // This is autogenerated, but you can set your own. Unique per queue partition.
    data: { id: 123, description: 'Critical task' }  // The message payload
  },
])
.onSuccess(async (messages) => { // Not mandatory
  console.log('Messages pushed successfully:', messages)
})
.onDuplicate(async (messages) => { // Not mandatory, triggered when a message with the same transactionId is pushed
  console.warn('Duplicate transaction IDs detected')
})
.onError(async (messages, error) => {  // Without callbacks, push throws an error if some messages are not pushed
  console.error('Error pushing messages:', error)
})

// Consume messages
await queen
.queue('critical-task')
// The consumer group name, without this, the messages are processed in queue mode
.group('processor-consumer-group') 
// 10 parallel workers
.concurrency(10)
// I want to manually ack/nack messages 
.autoAck(false) 
// 10 messages per batch, prefetch them
.batch(10) 
// Auto-renew the lease for the messages every 2 seconds
.renewLease(true, 2000) 
// Process each message individually
.each() 
// Do your work here
.consume(async (message) => {
  console.log('Processing:', message.data) 
})
// Ack the messages if you processed them successfully
.onSuccess(async (message) => {
  await queen
  .transaction()
  .queue('critical-task-next')
  .partition('XXX')
  .push([{ data: { message: 'Final', count: 3 } }])
  .ack(message)
  .commit()  
})
// Nack the messages if you failed to process them
.onError(async (message, error) => {
  console.error('Error processing messages:', error)
  await queen.ack(message, false)
})
```

## Webapp

A modern Vue 3 web interface for managing and monitoring Queen MQ.

![Queen MQ Dashboard](./assets/dashboard.png)


**Features:**
- 📊 Real-time dashboard with system metrics
- 📈 Message throughput visualization
- 🔍 Queue management and monitoring
- 👥 Consumer group tracking
- 💬 Message browser with trace timeline
- 🔎 **Trace explorer for debugging distributed workflows**
- 📉 Analytics and insights
- 🌓 Dark/light theme support

**Quick Start:**
```bash
cd webapp
npm install
npm run dev
```

The dashboard will be available at `http://localhost:4000` or at `http://localhost:6632` directly from the server.

See [webapp/README.md](webapp/README.md) for more details.

## Architecture

Queen uses a high-performance **acceptor/worker pattern** with uWebSockets, combining non-blocking I/O for HTTP/WebSocket with a dedicated thread pool for database operations.

**View the interactive architecture diagram:** [architecture.svg](./assets/architecture.svg)

**Key Components:**
- **UWS Acceptor**: Single thread listening on port 6632, round-robin distributes to workers
- **UWS Workers**: N event loop threads (default: 10) handling HTTP routes and WebSocket
- **Response Timers**: Per-worker timers (25ms tick) drain response queue back to clients
- **DB ThreadPool**: Separate pool for blocking PostgreSQL operations
- **Poll Workers**: 2 reserved threads for long-polling with adaptive backoff (100ms→2000ms)
- **Poll Intention Registry**: Thread-safe store for long-poll requests
- **Database Pool**: 150 shared PostgreSQL connections (libpq) with mutex/condition variable
- **Response Queue**: Thread-safe queue decoupling DB results from event loop responses

**Request Flow:**
1. Client → Acceptor → Worker (event loop)
2. Worker registers response, submits job to DB ThreadPool
3. DB thread executes query, pushes result to Response Queue
4. Worker's response timer drains queue, sends HTTP response

**Long-Polling Flow:**
1. No immediate messages? Register intention in Registry
2. Poll Workers wake every 50ms, group intentions by queue/partition/consumer
3. Rate-limited DB queries (100ms initial, exponential backoff to 2s)
4. Messages distributed to waiting clients via Response Queue
5. Timeouts detected by Poll Workers, send 204 No Content

This architecture provides high concurrency, efficient connection pooling, and minimal latency for both immediate and long-polling requests.

### PostgreSQL Failover

Queen automatically buffers messages to disk when PostgreSQL is unavailable - **zero message loss**:

- Normal pushes go directly to PostgreSQL (FIFO preserved)
- If PostgreSQL is down, messages buffered to file (macOS: `/tmp/queen`, Linux: `/var/lib/queen/buffers`)
- Automatic replay when PostgreSQL recovers
- Survives server crashes and restarts
- Directory auto-created on first run

**No configuration needed** - failover is automatic!

**Custom directory:**
```bash
FILE_BUFFER_DIR=/custom/path ./bin/queen-server
```

## Install the server and configure it

### Quick Start

```sh
cd server
make clean
make deps
make build-only
DB_POOL_SIZE=50 ./bin/queen-server
```

**📖 Complete Build & Tuning Guide:** [server/README.md](server/README.md)

Includes:
- Build instructions and optimization
- Performance tuning (worker threads, database pool)
- Production deployment (systemd, Docker, load balancing)
- Troubleshooting common issues
- Benchmarking guides

### Environment Variables

[The full list of environment variables is here](server/ENV_VARIABLES.md)

### With Docker
```sh
./build.sh
```

### Running on k8s

[Running in k8s](server/k8s-example.yaml)

## 🔌 Raw HTTP API

You can use Queen directly from HTTP without the JS client.

[Here the complete list of API endpoints](server/API.md)

## ⚠️ Known Issues & Roadmap

### Server Startup Timing (Critical)
**Issue:** Worker initialization timeout (30s → 3600s) now matches file buffer recovery timeout. This is a temporary fix.

**Better Solution Needed:**
- Make recovery non-blocking while preserving FIFO ordering guarantees
- Implement progressive readiness with memory-buffered queue during recovery
- Add configurable recovery timeout with graceful degradation
- See: `server/src/services/file_buffer.cpp:212` (MAX_STARTUP_RECOVERY_SECONDS)
- See: `server/src/acceptor_server.cpp:1876` (worker initialization timeout)


### Other TODO Items
- retention jobs
- auth
- streaming engine