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

**Problem:** During startup, if the file buffer has many events to recover (e.g., after a long PostgreSQL outage), Worker 0 performs blocking recovery that can take up to 1 hour. The acceptor waits for all workers to initialize before starting to accept connections.

**Current Fix:** Worker initialization timeout increased to 3600s to prevent premature timeout.

**Better Solution Needed:**
- Make recovery non-blocking while preserving FIFO ordering guarantees
- Implement progressive readiness with memory-buffered queue during recovery
- Add configurable recovery timeout with graceful degradation
- See: `server/src/services/file_buffer.cpp:212` (MAX_STARTUP_RECOVERY_SECONDS)
- See: `server/src/acceptor_server.cpp:1876` (worker initialization timeout)

### Long-Polling Timeout Cleanup Bug (Fixed)
**Issue:** Long-polling requests with `timeout >= 30s` would hang and never receive a response, even when the server correctly completed the operation.

**Root Cause:** Response registry cleanup timer was set to 30 seconds, causing registered responses to be deleted before long-polling operations could complete and send them back to clients.

**Timeline of Bug:**
1. Client sends request with `wait=true&timeout=30000`
2. Server registers response in registry
3. After 30s, cleanup timer **deletes** the response (thinking it's orphaned)
4. Thread pool completes wait operation, pushes result to response queue
5. Response timer tries to send but can't find the response (already deleted)
6. Client times out after 35s (30s + 5s buffer) with no response

**Fix Applied (v0.2.12):**
- Response registry cleanup timeout increased from 30s → 120s
- Allows long-polling requests up to 60s with safe buffer
- See: `server/src/acceptor_server.cpp:1721` (cleanup timeout)
- See: `server/include/queen/response_queue.hpp:113` (default max_age)

**Client-Side Recommendation:**
- **Set maximum client timeout to 60 seconds** to stay within server cleanup window
- For longer waits, use shorter timeouts with continuous retry loops
- Example: Use `timeout: 30000` (30s) instead of 60s+ for reliable operation

### Long-Polling Architecture Limitation (Planned Redesign)

**Current Issue:** Each long-polling request blocks a ThreadPool thread for the entire timeout duration (up to 30s), causing resource exhaustion when many clients poll empty queues.

**Impact:**
- With `DB_POOL_SIZE=20` → only 19 concurrent long-polling requests supported
- Additional requests queue up, adding latency (can exceed client timeout)
- Poor resource utilization (threads sleeping while waiting)

**Workarounds:**
1. Increase `DB_POOL_SIZE` to accommodate concurrent long-polling clients
2. Use shorter timeouts (5-10s) with continuous retry
3. Reduce number of concurrent consumers

**Planned Solution:** "Registry of Intentions" pattern
- Clients register polling intentions (queue, partition, timeout)
- 1-2 dedicated worker threads service ALL registered intentions
- No thread blocking per request
- Can handle 100+ concurrent long-polling clients efficiently
- See: [LONG_POLLING_REDESIGN.md](LONG_POLLING_REDESIGN.md) for full design

**Status:** Design complete, implementation tracked in GitHub issues

### Other TODO Items
- retention jobs
- reconsume
- fix frontend
- pg async
- pg reconnect
- memory leak?
- Long-polling redesign (registry of intentions pattern)