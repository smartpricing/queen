# Queen MQ - PostgreSQL-backed Message Queue

<div align="center">

**A modern, performant message queue system built on PostgreSQL**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE.md)
[![Node](https://img.shields.io/badge/node-%3E%3D22.0.0-brightgreen.svg)](https://nodejs.org/)
[![Python](https://img.shields.io/badge/python-3.8%2B-blue.svg)](https://www.python.org/)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![libuv](https://img.shields.io/badge/libuv-1.48.0-blue.svg)](https://libuv.org/)
[![libpq](https://img.shields.io/badge/libpq-15.5-blue.svg)](https://www.postgresql.org/)
[![uWebSockets](https://img.shields.io/badge/uWebSockets-22.0.0-blue.svg)](https://github.com/uNetworking/uWebSockets)

📚 **[Complete Documentation](https://smartpricing.github.io/queen/)** • 🚀 **[Quick Start](https://smartpricing.github.io/queen/guide/quickstart)** • ⚖️ **[Comparison](https://smartpricing.github.io/queen/guide/comparison)**

<p align="center">
  <img src="assets/queen_logo.png" alt="Queen Logo" width="120" />
</p>

</div>

---

Queen MQ is a modern PostgreSQL-backed Message Queue, built with uWebSockets, libuv and libpq async API, feature-rich system with unlimited FIFO partitions, consumer groups, transactions, tracing and streaming capabilities with exact-once delivery guarantees.

## Why Queen?

Born at [Smartness](https://www.linkedin.com/company/smartness-com/) to power **Smartchat**, Queen solves a unique problem: **unlimited FIFO partitions** where slow processing in one partition doesn't block others.

Perfect for:
- Processing messages in order, without losing them somewhere in the middle
- Do not want that a slow message processing in one partition blocks other partitions 
- When you need thousands of thousands of partitions to process messages in parallel, respecting the order of the messages
- Process the same messages in multiple pipelines
- Have a clear view of the message processing flow and traceability
- Build event-driven microservices with exactly-once delivery guarantees
- Critical systems that need to be highly available and reliable with zero message loss

---

## Quick Start

Run PostgreSQL and Queen server in Docker:

```sh
# Start PostgreSQL and Queen
docker network create queen

docker run --name postgres --network queen \
  -e POSTGRES_PASSWORD=postgres -p 5432:5432 -d postgres

docker run -p 6632:6632 --network queen \
  -e PG_HOST=postgres -e PG_PASSWORD=postgres \
  -e DB_POOL_SIZE=20 \  
  -e NUM_WORKERS=2 \
  -e DEFAULT_SUBSCRIPTION_MODE=new \
  -e LOG_LEVEL=info \
  -e POLL_WORKER_COUNT=2 \
  -e POLL_DB_INTERVAL=100 \
  -e POLL_WORKER_INTERVAL=10 \
  -e QUEUE_MAX_POLL_INTERVAL=2000 \
  smartnessai/queen-mq
```

Install client:

**JavaScript:**
```sh
npm install queen-mq
```

**Python:**
```sh
pip install queen-mq
```

Use the client:

**JavaScript:**
```javascript
import { Queen } from 'queen-mq'

const queen = new Queen('http://localhost:6632')

// Create queue
await queen.queue('orders')
  .config({ leaseTime: 30, retryLimit: 3 })
  .create()

// Push with guaranteed order per partition
await queen.queue('orders')
  .partition('customer-123')
  .push([{ data: { orderId: 'ORD-001', amount: 99.99 } }])

// Consume with consumer groups
await queen.queue('orders')
  .group('order-processor')
  .concurrency(10)
  .batch(20)
  .consume(async (message) => {
    await processOrder(message.data)
  })
```

**Python:**
```python
import asyncio
from queen import Queen

async def main():
    async with Queen('http://localhost:6632') as queen:
        # Create queue
        await queen.queue('orders').config({
            'leaseTime': 30,
            'retryLimit': 3
        }).create()
        
        # Push with guaranteed order per partition
        await queen.queue('orders').partition('customer-123').push([
            {'data': {'orderId': 'ORD-001', 'amount': 99.99}}
        ])
        
        # Consume with consumer groups
        async def handler(message):
            await process_order(message['data'])
        
        await (queen.queue('orders')
            .group('order-processor')
            .concurrency(10)
            .batch(20)
            .consume(handler))

asyncio.run(main())
```

**[Complete Tutorial →](https://smartpricing.github.io/queen/guide/quickstart)**

---

Some example logs:

```bash
[info] [Worker 0] PUSH: Queue 'smartchat.router.incoming' encryption_enabled=true (from cache)
[info] [Worker 0] PUSH: enc_service=true, enabled=true
[info] [Worker 0] PUSH: Encrypted payload for queue 'smartchat.router.incoming'
[info] [Worker 0] PUSH: 1 items to [smartchat.router.incoming]
[info] [Worker 0] [Sidecar] BATCH: PUSH (1 requests, 1 items)
[info] [Worker 0] [Sidecar] NOTIFY: Queue 'smartchat.router.incoming' has activity, waking 1 waiting consumers
[info] [Worker 0] PUSH NOTIFY: 1 queue(s)
[info] [Worker 0] PUSH RESPONSE: (status=201)
[info] [Worker 0] POP_WAIT RESPONSE: smartchat.router.incoming/agentsource:fed2f633-38fc-4f73-8f91-1c7e2a2c9851 (status=200)
[info] [Worker 1] TRANSACTION: Executing transaction (2 operations)
[info] [Worker 1] TRANSACTION: Encrypted payload for queue 'smartchat.translations'
[info] [Worker 1] TRANSACTION RESPONSE: (status=200)
[info] [Worker 0] QPOP: [smartchat.router.incoming/*@__QUEUE_MODE__] batch=1, wait=true | Pool: 9/9 conn (0 in use)
[info] [Worker 0] QPOP: Submitted POP_WAIT 44b91fd6-0ae0-4a84-882c-93f278c3a2c4 for queue smartchat.router.incoming (timeout=30000ms)
[info] [Worker 1] POP_WAIT RESPONSE: smartchat.translations/7f64ced6-8f37-49bb-901a-68cfc8bf58a6 (status=200)
[info] [Worker 0] TRANSACTION: Executing transaction (2 operations)
[info] [Worker 0] TRANSACTION: Encrypted payload for queue 'smartchat.router.history'
[info] [Worker 0] TRANSACTION RESPONSE: (status=200)
[info] [Worker 1] QPOP: [smartchat.translations/*@__QUEUE_MODE__] batch=1, wait=true | Pool: 9/9 conn (0 in use)
[info] [Worker 1] QPOP: Submitted POP_WAIT eb951693-b14c-4071-970d-82919bc5e1ee for queue smartchat.translations (timeout=30000ms)
[info] [Worker 1] POP_WAIT RESPONSE: smartchat.router.history/7f64ced6-8f37-49bb-901a-68cfc8bf58a6 (status=200)
```

## Key Features

- 🎯 **Unlimited FIFO Partitions** - No limits on ordered message streams
- 👥 **Consumer Groups** - Kafka-style with replay from any timestamp
- 🔄 **Transactions** - Atomic push+ack for exactly-once delivery
- 📡 **Streaming** - Window aggregation and processing
- 🛡️ **Zero Message Loss** - Automatic failover to disk buffer
- 💀 **Dead Letter Queue** - Automatic handling of failed messages
- 🔍 **Message Tracing** - Debug distributed workflows
- 📊 **Modern Dashboard** - Vue 3 web interface with analytics
- 🔐 **Encryption** - Per-queue message encryption
- ⚡ **High Performance** - 200K+ msg/s with proper batching

**[Compare with Kafka, RabbitMQ, NATS →](https://smartpricing.github.io/queen/guide/comparison)**

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | 100K+ msg/s sustained |
| **Latency** | 10-50ms (POP/ACK), 50-200ms (TRANSACTION) |
| **Partitions** | Unlimited |

**[Detailed Benchmarks →](https://smartpricing.github.io/queen/server/benchmarks)**

---

## Release History

| Server Version | Description | Compatible Clients |
|----------------|-------------|-------------------|
| **0.8.0** | Added Shared Cache with UDP sync for clustered deployment | JS ≥0.7.4, Python ≥0.7.4 |
| **0.7.5** | First stable release | JS ≥0.7.4, Python ≥0.7.4 |

**[Full Release Notes →](https://smartpricing.github.io/queen/server/releases)**

---

## Webapp

A modern Vue 3 dashboard for monitoring and managing Queen MQ:

<div align="center">

![Dashboard](./assets/new_dashboard.png)

</div>

**[Webapp Documentation →](https://smartpricing.github.io/queen/webapp/overview)**

---

## Documentation

📚 **[Complete Documentation](https://smartpricing.github.io/queen/)**

### Getting Started
- [Quick Start Guide](https://smartpricing.github.io/queen/guide/quickstart)
- [Installation](https://smartpricing.github.io/queen/guide/installation)
- [Basic Concepts](https://smartpricing.github.io/queen/guide/concepts)

### Client Libraries
- [JavaScript Client](https://smartpricing.github.io/queen/clients/javascript)
- [Python Client](https://smartpricing.github.io/queen/clients/python)
- [C++ Client](https://smartpricing.github.io/queen/clients/cpp)
- [HTTP API Reference](https://smartpricing.github.io/queen/api/http)

### Server
- [Architecture](https://smartpricing.github.io/queen/server/architecture)
- [Installation & Build](https://smartpricing.github.io/queen/server/installation)
- [Configuration](https://smartpricing.github.io/queen/server/configuration)
- [Environment Variables](https://smartpricing.github.io/queen/server/environment-variables)
- [Deployment (Docker, K8s, systemd)](https://smartpricing.github.io/queen/server/deployment)
- [Performance Tuning](https://smartpricing.github.io/queen/server/tuning)

### Features
- [Queues & Partitions](https://smartpricing.github.io/queen/guide/queues-partitions)
- [Consumer Groups](https://smartpricing.github.io/queen/guide/consumer-groups)
- [Transactions](https://smartpricing.github.io/queen/guide/transactions)
- [Long Polling](https://smartpricing.github.io/queen/guide/long-polling)
- [Streaming](https://smartpricing.github.io/queen/guide/streaming)
- [Dead Letter Queue](https://smartpricing.github.io/queen/guide/dlq)
- [Message Tracing](https://smartpricing.github.io/queen/guide/tracing)
- [Failover & Recovery](https://smartpricing.github.io/queen/guide/failover)

---

## Production Ready

Queen is currently running in production at [Smartness](https://www.linkedin.com/company/smartness-com/), handling **200k messages daily**.

---

## Community & Support

- 🌐 **Website**: [smartpricing.github.io/queen](https://smartpricing.github.io/queen/)
- 📦 **Docker Hub**: [smartnessai/queen-mq](https://hub.docker.com/r/smartnessai/queen-mq)
- 💼 **LinkedIn**: [Smartness Company Page](https://www.linkedin.com/company/smartness-com/)
- 🐛 **Issues**: [GitHub Issues](https://github.com/smartpricing/queen/issues)

---

## License

Queen MQ is released under the [Apache 2.0 License](LICENSE.md).

---

<div align="center">

**Built with ❤️ by [Smartness](https://www.linkedin.com/company/smartness-com/)**

*Why "Queen"? Because years ago, when I first read "queue", I read it as "queen" in my mind.*

</div>
