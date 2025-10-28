# Queen MQ - PostgreSQL-backed C++ Message Queue

<div align="center">

**A modern, high-performance message queue system built on PostgreSQL**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE.md)
[![Node](https://img.shields.io/badge/node-%3E%3D22.0.0-brightgreen.svg)](https://nodejs.org/)

[Quick Start](#js-client-usage-v2) • [Complete V2 Guide](client-js/client-v2/README.md) • [Examples](#-examples) • [Webapp](#webapp) • [Server Setup](#install-server-and-configure-it) • [HTTP API](#raw-http-api)

<p align="center">
  <img src="assets/queen-logo-rose.svg" alt="Queen Logo" width="120" />
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
- QoS levels: Exactly-once delivery (with transactionId), at-least-once delivery, and at-most-once delivery
- Subscription modes for replay (new messages only or from a specific timestamp) and message history control
- Transactions between operations (push and ack mainly) for atomicity
- Dead letter queue for failure handling
- Lease renewal for long-running tasks
- Message tracing for debugging workflows

The system consists of a PostgreSQL database, a replicated server that can be scaled horizontally (though it won't be the bottleneck), and a client library for interacting with the server. All client-server communication happens over HTTP, and the client library is written in JavaScript (support for other languages is planned). There's also a modern Vue 3 web app for monitoring and managing the system.

With proper batching, the system can handle 100k **messages** (not req/s) per second on modest hardware.

Main documentation:
- [Client Guide](client-js/client-v2/README.md)
- [Server Guide](server/README.md)
- [API Reference](API.md)
- [Webapp](webapp/README.md)


## JS Client Usage

> 📖 **[Complete Guide: client-js/client-v2/README.md](client-js/client-v2/README.md)** - Full tutorial with all features!

```javascript
import { Queen } from 'queen-mq'

// Connect to Queen
const queen = new Queen('http://localhost:6632')

// Create a queue
await queen.queue('tasks').create()

// Push messages
await queen.queue('tasks').push([
  { data: { job: 'send-email', to: 'alice@example.com' } },
  { data: { job: 'process-image', id: 123 } }
])

// Consume messages (auto-ack on success, auto-nack on error)
await queen.queue('tasks').consume(async (message) => {
  console.log('Processing:', message.data)
  // If this succeeds → message completed ✅
  // If this throws → message retried 🔄
})

// Pop messages manually
const messages = await queen.queue('tasks').batch(10).pop()
for (const msg of messages) {
  try {
    await processMessage(msg.data)
    await queen.ack(msg, true)  // Success
  } catch (error) {
    await queen.ack(msg, false)  // Retry
  }
}

// Partitions (for ordering)
await queen
  .queue('user-events')
  .partition('user-123')
  .push([{ data: { event: 'login' } }])

// Consumer groups (for scaling)
await queen
  .queue('emails')
  .group('processors')
  .concurrency(5)
  .consume(async (message) => {
    await sendEmail(message.data)
  })

// Subscription modes (control message history)
// Skip historical messages, only process new ones
await queen
  .queue('events')
  .group('realtime-monitor')
  .subscriptionMode('new')
  .consume(async (message) => {
    console.log('New event only:', message.data)
  })

// Start from a specific timestamp
const timestamp = '2025-10-28T10:00:00.000Z'
await queen
  .queue('events')
  .group('replay-from-timestamp')
  .subscriptionFrom(timestamp)
  .consume(async (message) => {
    console.log('Replaying from 10am:', message.data)
  })

// Transactions (atomic ack + push)
const [msg] = await queen.queue('input').pop()
await queen
  .transaction()
  .ack(msg)
  .queue('output')
  .push([{ data: { processed: true } }])
  .commit()

// Client-side buffering (for high throughput)
await queen
  .queue('logs')
  .buffer({ messageCount: 100, timeMillis: 1000 })
  .push([{ data: { level: 'info', message: 'Server started' } }])

// Message tracing (for debugging and monitoring)
await queen.queue('orders').consume(async (msg) => {
  await msg.trace({
    traceName: ['tenant-acme', 'order-flow-123'],
    data: { text: 'Processing started', orderId: msg.data.id }
  })
  
  // Process order...
  
  await msg.trace({
    traceName: ['tenant-acme', 'order-flow-123'],
    data: { text: 'Order completed' }
  })
})

// Graceful shutdown
await queen.close()
```

**Key Features:**
- ✅ Fluent, chainable API
- ✅ Auto-acknowledgment (or manual control)
- ✅ Partitions for ordered processing
- ✅ Consumer groups for scaling
- ✅ **Subscription modes (new messages only or from timestamp)**
- ✅ Transactions for atomicity
- ✅ Client-side buffering for speed
- ✅ Dead letter queue for failures
- ✅ Lease renewal for long tasks
- ✅ **Message tracing for debugging workflows**
- ✅ Graceful shutdown with buffer flush

## 📚 Examples

### Client

See the **[Complete V2 Guide](client-js/client-v2/README.md)** with 14 parts covering everything from basics to advanced features:
- Queue creation, push, and consume
- Partitions and consumer groups
- **Subscription modes (new messages, timestamps)**
- Transactions and buffering
- Dead letter queues
- Lease renewal
- Message tracing
- Complete real-world pipeline example
- And much more!

**Test Files** (94 working examples): [client-js/test-v2/](client-js/test-v2/)


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

## Webapp

A modern Vue 3 web interface for managing and monitoring Queen MQ.

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

The dashboard will be available at `http://localhost:4000`

See [webapp/README.md](webapp/README.md) for more details.

## Install server and configure it

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

[Here the complete list of API endpoints](API.md)

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