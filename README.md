# Queen MQ - PostgreSQL-backed C++ Message Queue

<div align="center">

**A modern, high-performance message queue system built on PostgreSQL**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE.md)
[![Node](https://img.shields.io/badge/node-%3E%3D22.0.0-brightgreen.svg)](https://nodejs.org/)

[Quick Start](#js-client-usage) • [Examples](#-examples) • [Webapp](#webapp) • [Server Setup](#install-server-and-configure-it) • [HTTP API](#raw-http-api)

<p align="center">
  <img src="assets/queen-logo-rose.svg" alt="Queen Logo" width="120" />
</p>

</div>

---

## Introduction

QueenMQ is a queue system written in C++ and backed by Postgres. Supports queues and consumer groups.

## JS Client usage

```js
import { Queen } from 'queen-mq'

const client = new Queen({ 
    baseUrls: ['http://localhost:6632'],
    timeout: 30000,
    retryAttempts: 3
});

const queue = 'html-processing'

// Create a queue
await client.queue(queue, { leaseTime: 30 });

// Push some data, specifyng the partition
await client.push(`${queue}/customer-1828`, [ { id: 1 } ]); 

// Consume data with iterators
for await (const msg of client.take(queue, { limit: 1 })) {
    console.log(msg.data.id)
    await client.ack(msg) //  OR await client.ack(msg, false) for nack
}

// Consume data with iterators, getting the entire batch
for await (const messages of client.takeBatch(queue, { limit: 1, batch: 10, wait: true, timeout: 2000 })) {
    const newMex = messages.map(x => x.data.id * 2)
    await client.ack(messages) //  OR await client.ack(messages, false) for nack
}

// Consume data with a consumer group
for await (const msg of client.take(`${queue}@analytics-data`, { limit: 2, batch: 2 })) {
    // Do your computation and than ack with consumer group
    await client.ack(msg, true, { group: 'analytics-data' });
}

// Consume data from any partition of the queue, continusly
// This "pipeline" is useful for doing exactly one processing
await client 
.pipeline(queue)
.withAutoRenewal({ 
  interval: 5000  // Renew lease every 5 seconds
})    
.withConcurrency(5) // Five parallel promises
.take(10, {
  wait: true, // Use long polling
  timeout: 30000 // Long polling length in millisconds
})
.processBatch(async (messages) => {
    return messages.map(x => x.data.id * 2);
})
.atomically((tx, originalMessages, processedMessages) => { // ack and push are transactional inside atomically
    tx.ack(originalMessages);
    tx.push('another-queue', processedMessages); 
})
.repeat({ continuous: true })
.execute();
```

## 📚 Examples

### Basic Usage
- **[Basic Queue Operations](examples/01-basic-usage.js)** - Create queue, push, take, and ack messages
- **[Batch Operations](examples/02-batch-operations.js)** - Push and consume messages in batches

### Advanced Features
- **[Queue Configuration](examples/06-queue-configuration.js)** - Configure maxSize, windowBuffer, delay, retryLimit, and priority
- **[Delayed Processing](examples/04-delayed-processing.js)** - Process messages after a delay
- **[Window Buffer](examples/05-window-buffer.js)** - Delay message availability after push

### Filtering & Routing
- **[Namespace & Task Filtering](examples/07-namespace-task-filtering.js)** - Route and filter messages by namespace and task
- **[Consumer Groups](examples/08-consumer-groups.js)** - Multiple consumer groups processing same messages

### Pipelines
- **[Transactional Pipelines](examples/03-transactional-pipeline.js)** - Atomic processing with ack and push in a transaction

### Event Streaming (QoS 0)
- **[Event Streaming](examples/09-event-streaming.js)** - At-most-once delivery with buffering and auto-ack

## QoS 0: At-Most-Once Event Streaming

For high-throughput event streams, Queen supports **at-most-once delivery** with server-side buffering and auto-acknowledgment.

### Server-Side Buffering

Batch events on the server for 10-100x reduction in database writes:

```javascript
// Push with buffering (QoS 0)
await client.push('metrics', { cpu: 45, memory: 67 }, {
  bufferMs: 100,      // Server batches for 100ms
  bufferMax: 100      // Or until 100 events
});

// Result: 1000 events = ~10 DB writes (instead of 1000)
```

### Auto-Acknowledgment

Skip manual ack for fire-and-forget consumption:

```javascript
// Consume with auto-ack
for await (const msg of client.take('metrics', { autoAck: true })) {
  updateDashboard(msg.data);
  // No ack() needed - automatically acknowledged!
}
```

### Fan-Out Pattern (Consumer Groups)

Combine buffering + auto-ack + consumer groups for pub/sub:

```javascript
// Publisher (buffered)
await client.push('events', { action: 'login' }, { bufferMs: 100 });

// Multiple subscribers (each group gets all messages)
for await (const e of client.take('events@dashboard', { autoAck: true })) {
  updateUI(e.data);
}

for await (const e of client.take('events@analytics', { autoAck: true })) {
  trackEvent(e.data);
}
```

### Architecture

┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                              QUEEN MESSAGE QUEUE SERVER ARCHITECTURE                        │
│                             (Acceptor/Worker Pattern with uWebSockets)                      │
└─────────────────────────────────────────────────────────────────────────────────────────────┘

                                    ┌──────────────────┐
                                    │  Client Request  │
                                    └────────┬─────────┘
                                             │
                                             ▼
┌────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   UWS ACCEPTOR THREAD                                      │
│  ┌──────────────────────────────────────────────────────────────────────────────────────┐  │
│  │  • Main event loop (acceptor->run())                                                 │  │
│  │  • Listens on port 6632                                                              │  │
│  │  • Round-robin distributes connections to workers                                    │  │
│  └──────────────────────────────────────────────────────────────────────────────────────┘  │
└─────────────────┬──────────────────┬──────────────────┬────────────────────────────────────┘
                  │                  │                  │
          ┌───────┴───────┐  ┌───────┴───────┐   ┌──────┴────────┐
          ▼               ▼                  ▼                   ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐     ┌─────────────────┐
│ UWS WORKER 0    │ │ UWS WORKER 1    │ │ UWS WORKER 2    │ ... │ UWS WORKER N    │
│ (std::thread)   │ │ (std::thread)   │ │ (std::thread)   │     │ (std::thread)   │
├─────────────────┤ ├─────────────────┤ ├─────────────────┤     ├─────────────────┤
│ • Event Loop    │ │ • Event Loop    │ │ • Event Loop    │     │ • Event Loop    │
│ • HTTP Routes   │ │ • HTTP Routes   │ │ • HTTP Routes   │     │ • HTTP Routes   │
│ • WS Handlers   │ │ • WS Handlers   │ │ • WS Handlers   │     │ • WS Handlers   │
│                 │ │                 │ │                 │     │                 │
│ ┌─────────────┐ │ │ ┌─────────────┐ │ │ ┌─────────────┐ │     │ ┌─────────────┐ │
│ │RESPONSE     │ │ │ │RESPONSE     │ │ │ │RESPONSE     │ │     │ │RESPONSE     │ │
│ │TIMER        │ │ │ │TIMER        │ │ │ │TIMER        │ │     │ │TIMER        │ │
│ │(us_timer_t) │ │ │ │(us_timer_t) │ │ │ │(us_timer_t) │ │     │ │(us_timer_t) │ │
│ │25ms tick    │ │ │ │25ms tick    │ │ │ │25ms tick    │ │     │ │25ms tick    │ │
│ └──────┬──────┘ │ │ └──────┬──────┘ │ │ └──────┬──────┘ │     │ └──────┬──────┘ │
└────────┼────────┘ └────────┼────────┘ └────────┼────────┘     └────────┼────────┘
         │                   │                   │                       │
         └───────────────────┴───────────────────┴───────────────────────┘
                                      │
                                      ▼ (Drain responses)
                        ┌──────────────────────────┐
                        │  GLOBAL RESPONSE QUEUE   │◄──────────────────┐
                        │  (Thread-safe queue)     │                   │
                        │  std::queue<ResponseItem>│                   │
                        └──────────────────────────┘                   │
                                      ▲                                │
                                      │ Push results                   │
                                      │                                │
                                                                       │
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                          GLOBAL DB THREADPOOL (astp::ThreadPool)                         │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐            ┌────────────┐                  │
│  │  Thread 1  │ │  Thread 2  │ │  Thread 3  │   ...      │  Thread N  │                  │
│  └─────┬──────┘ └─────┬──────┘ └─────┬──────┘            └─────┬──────┘                  │
│        │              │              │                         │                         │
│        └──────────────┴──────────────┴─────────────────────────┘                         │
│                                      │                                                   │
│  Jobs: • PUSH operations             │                                                   │
│        • POP operations (immediate)  │                                                   │
│        • ACK operations              │                                                   │
│        • Transaction operations      │                                                   │
│        • POLL WORKERS (reserved)     │                                                   │
└──────────────────────────────────────┼───────────────────────────────────────────────────┘
                                       │
              ┌────────────────────────┴──────────────────────────┐
              │                                                   │
              ▼                                                   ▼
┌──────────────────────────────┐                   ┌──────────────────────────────┐
│   POLL WORKER THREADS        │                   │   DB OPERATION THREADS       │
│   (Reserved from ThreadPool) │                   │   (Dynamic task execution)   │
├──────────────────────────────┤                   ├──────────────────────────────┤
│                              │                   │                              │
│ ┌──────────────────────────┐ │                   │ • Execute DB queries         │
│ │ POLL WORKER 0            │ │                   │ • Get connection from pool   │
│ │ • Loop every 50ms        │ │                   │ • Return connection          │
│ │ • Check registry         │ │                   │ • Push to response queue     │
│ │ • Group intentions       │ │                   │                              │
│ │ • Submit DB jobs         │ │                   └──────────┬───────────────────┘
│ │ • Adaptive backoff       │ │                              │
│ └────────┬─────────────────┘ │                              │
│          │                   │                              │
│ ┌────────┴─────────────────┐ │                              │
│ │ POLL WORKER 1            │ │                              │
│ │ • Loop every 50ms        │ │                              │
│ │ • Load balanced (hash)   │ │                              │
│ │ • Rate limit: 100ms→2s   │ │                              │
│ │ • Timeout detection      │ │                              │
│ └──────────────────────────┘ │                              │
└────────┬─────────────────────┘                              │
         │                                                    │
         ├────────► Get intentions                            │
         │                                                    │
         ▼                                                    │
┌─────────────────────────────────┐                           │
│  POLL INTENTION REGISTRY        │                           │
│  (Shared, thread-safe)          │                           │
├─────────────────────────────────┤                           │
│ • Store long-poll requests      │                           │
│ • Group by queue/partition/CG   │                           │
│ • Track timeouts                │                           │
│ • In-flight group tracking      │                           │
│                                 │                           │
│ std::unordered_map<             │                           │
│   request_id → PollIntention    │                           │
│ >                               │                           │
└─────────────────────────────────┘                           │
                                                              │
                                                              │
        All threads use connection pool ──────────────────────┘
                         │
                         ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│                        GLOBAL DATABASE POOL (DatabasePool)                       │
│  ┌──────────────────────────────────────────────────────────────────────────┐    │
│  │  Connection Queue (std::queue<DatabaseConnection>)                       │    │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐       ┌──────────┐               │    │
│  │  │  Conn 1  │ │  Conn 2  │ │  Conn 3  │  ...  │  Conn N  │  (N=150)      │    │
│  │  │ PGconn*  │ │ PGconn*  │ │ PGconn*  │       │ PGconn*  │               │    │
│  │  └──────────┘ └──────────┘ └──────────┘       └──────────┘               │    │
│  │                                                                          │    │
│  │  Mutex + Condition Variable for thread-safe get/return                   │    │
│  └──────────────────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────┬───────────────────────────────────────────┘
                                       │
                                       ▼
                            ┌──────────────────┐
                            │   PostgreSQL     │
                            │   Database       │
                            │                  │
                            │  • messages      │
                            │  • queues        │
                            │  • partitions    │
                            │  • leases        │
                            └──────────────────┘

┌──────────────────────────────────────────────────────────────────────────────────┐
│                    ADDITIONAL SYSTEM COMPONENTS                                  │
├──────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│  SYSTEM THREADPOOL (separate from DB pool):                                      │
│    • Metrics collection                                                          │
│    • Lease reclamation (every 5s)                                                │
│    • Retention cleanup (every 5min)                                              │
│    • File buffer recovery                                                        │
│                                                                                  │
│  RESPONSE REGISTRY:                                                              │
│    • Track HTTP response objects                                                 │
│    • Thread-safe access                                                          │
│    • Cleanup expired entries                                                     │
│                                                                                  │
│  FILE BUFFER MANAGER:                                                            │
│    • Durability layer for PUSH operations                                        │
│    • Write-ahead buffer before DB                                                │
│    • Background flush to DB                                                      │
│    • Crash recovery on startup                                                   │
│                                                                                  │
│  METRICS COLLECTOR:                                                              │
│    • Track request counts                                                        │
│    • Track message counts                                                        │
│    • Per-worker statistics                                                       │
│                                                                                  │
└──────────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────────┐
│                              REQUEST FLOW EXAMPLES                               │
├──────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│  PUSH (async):                                                                   │
│    Client → Acceptor → Worker → Register response → Submit to DB ThreadPool      │
│           → DB Thread executes → Push to Response Queue → Timer drains queue     │
│           → Send HTTP response                                                   │
│                                                                                  │
│  POP (immediate, no wait):                                                       │
│    Client → Acceptor → Worker → Register response → Submit to DB ThreadPool      │
│           → DB Thread queries → Push to Response Queue → Timer drains → Response │
│                                                                                  │
│  POP (long-poll, wait=true):                                                     │
│    Client → Acceptor → Worker → Register response → Check for messages           │
│           → No messages? Register intention in Registry                          │
│           → Poll Worker wakes (50ms) → Groups intentions → Rate limits (100ms+)  │
│           → Submits DB query job → Gets messages → Push to Response Queue        │
│           → Timer drains → Send response                                         │
│           → Adaptive backoff if empty (100→200→400→...→2000ms)                   │
│                                                                                  │
│  TIMEOUT:                                                                        │
│    Poll Worker detects deadline exceeded → Push 204 to Response Queue            │
│           → Remove from registry → Timer drains → Send 204 No Content            │
│                                                                                  │
└──────────────────────────────────────────────────────────────────────────────────┘

KEY CHARACTERISTICS:
• Acceptor Pattern: One acceptor thread + N worker threads (default: 10)
• Non-blocking I/O: All HTTP/WebSocket handled in event loops
• DB ThreadPool: Separate pool for blocking DB operations (size: based on pool size)
• Poll Workers: Reserved threads (2) that continuously check for long-poll intentions
• Response Decoupling: DB threads push results to queue, timers drain in event loop
• Rate Limiting: Dual-interval (50ms wake + 100ms+ DB query) with exponential backoff
• Connection Pool: 150 connections shared across all threads
• Timers: uSockets timers (us_timer_t) integrated with event loop for low overhead


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

### When to Use

| Feature | Use For | Don't Use For |
|---------|---------|---------------|
| **Buffering** | Metrics, logs, analytics, UI updates | Critical tasks, payments |
| **Auto-Ack** | Fire-and-forget events, notifications | Tasks requiring retry logic |
| **Failover** | Everything (automatic) | N/A - always beneficial |

## Webapp

A modern Vue 3 web interface for managing and monitoring Queen MQ.

**Features:**
- 📊 Real-time dashboard with system metrics
- 📈 Message throughput visualization
- 🔍 Queue management and monitoring
- 👥 Consumer group tracking
- 💬 Message browser
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
- reconsume
- new client
- auth
- streaming engine