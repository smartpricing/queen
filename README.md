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

## Performance

All the following beanchmarks are without client side batching, so each operation is a single request. **With client side batching, the throughput is much higher (x4, x5)**. 

On Linux machines on DO. Ulimit extended and Postgres 16 tuned with 300 connections, high memory buffer and wal and 12 workers.

──────────────────────────────────────────────────
Server Core        16    
NUM_WORKERS        8
SIDECAR_MICRO_BATCH_WAIT_MS 25
──────────────────────────────────────────────────
Loader Core        32
Workers:           32
Connections:       3200
Duration:          30s
──────────────────────────────────────────────────

### Push

Throughput:        45,015 req/s
Latency avg:       72.71ms
Latency p50:       69.41ms
Latency p90:       96.28ms
Latency p99:       119.41ms

## Pop

Using serverAutoAck at true (so we can pop without ack and so it'easier to setup a very concurrent benchmark), with the same setup as above:

Throughput: 27,000 req/s 
Latency p50: 28ms
Latency p75: 51ms
Latency p90: 106ms
Latency p99: 531ms


## Beanchmark

16.27

docker run -d --ulimit nofile=65535:65535 --name postgres --network queen -e POSTGRES_PASSWORD=postgres -p 5432:5432 postgres -c shared_buffers=4GB -c max_connections=300 -c temp_buffers=128MB -c work_mem=64MB -c max_parallel_workers=18 -c max_worker_processes=18 -c checkpoint_timeout=30min -c checkpoint_completion_target=0.9 -c max_wal_size=16GB -c min_wal_size=4GB -c wal_buffers=64MB -c wal_compression=on -c synchronous_commit=off -c autovacuum_vacuum_cost_limit=2000 -c autovacuum_vacuum_cost_delay=2ms -c autovacuum_max_workers=4 -c maintenance_work_mem=2GB -c effective_io_concurrency=200 -c random_page_cost=1.1 -c effective_cache_size=32GB -c huge_pages=try


docker stop queen && docker rm queen
docker run -d --ulimit nofile=65535:65535 --name queen -p 6632:6632 --network queen -e PG_HOST=postgres -e PG_PASSWORD=postgres -e NUM_WORKERS=10  -e DB_POOL_SIZE=50  -e SIDECAR_POOL_SIZE=250  -e SIDECAR_MICRO_BATCH_WAIT_MS=20  -e POP_WAIT_INITIAL_INTERVAL_MS=10  -e POP_WAIT_BACKOFF_THRESHOLD=3  -e POP_WAIT_BACKOFF_MULTIPLIER=2.0  -e POP_WAIT_MAX_INTERVAL_MS=1000  -e DEFAULT_SUBSCRIPTION_MODE=new  -e RETENTION_INTERVAL=300000  -e RETENTION_BATCH_SIZE=500000 -e LOG_LEVEL=info -e DB_STATEMENT_TIMEOUT=300000 -e STATS_RECONCILE_INTERVAL_MS=8640000  smartnessai/queen-mq:0.11.0-dev-7

docker stop queen postgres && docker rm queen postgres && docker volume prune --all

SELECT pid, now() - query_start AS duration, state, query 
FROM pg_stat_activity                                          
WHERE state != 'idle' 
ORDER BY duration DESC;

SELECT schemaname, relname, n_dead_tup, n_live_tup, 
       round(n_dead_tup::numeric / nullif(n_live_tup, 0) * 100, 2) as dead_pct,
       last_vacuum, last_autovacuum
FROM pg_stat_user_tables
ORDER BY n_dead_tup DESC
LIMIT 10;

-- Check for any slow queries
SELECT pid, now() - query_start AS duration, left(query, 80) 
FROM pg_stat_activity 
WHERE state = 'active' AND query_start < now() - interval '1 second'
ORDER BY duration DESC;

-- Check autovacuum isn't stuck
SELECT relname, last_autovacuum, n_dead_tup 
FROM pg_stat_user_tables 
WHERE n_dead_tup > 1000 
ORDER BY n_dead_tup DESC;

-- HOT check
SELECT 
    n_tup_upd,
    n_tup_hot_upd,
    round(n_tup_hot_upd::numeric / nullif(n_tup_upd, 0) * 100, 2) as hot_update_pct
FROM pg_stat_user_tables 
WHERE relname = 'partition_lookup';


## Quick Start

Run PostgreSQL and Queen server in Docker:

```sh
# Start PostgreSQL and Queen
docker network create queen

docker run --name postgres --network queen \
  -e POSTGRES_PASSWORD=postgres -p 5432:5432 -d postgres

docker run -p 6632:6632 --network queen \
  -e PG_HOST=postgres 
  -e PG_PASSWORD=postgres \
  -e NUM_WORKERS=2 \
  -e DB_POOL_SIZE=5 \  
  -e SIDECAR_POOL_SIZE=30 \  
  -e SIDECAR_MICRO_BATCH_WAIT_MS=10 \
  -e POP_WAIT_INITIAL_INTERVAL_MS=500 \
  -e POP_WAIT_BACKOFF_THRESHOLD=1 \
  -e POP_WAIT_BACKOFF_MULTIPLIER=3.0 \
  -e POP_WAIT_MAX_INTERVAL_MS=5000 \
  -e DEFAULT_SUBSCRIPTION_MODE=new \
  -e LOG_LEVEL=info \
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

```

## Key Features

- 🎯 **Unlimited FIFO Partitions** - No limits on ordered message streams
- 👥 **Consumer Groups** - Kafka-style with replay from any timestamp
- 🔄 **Transactions** - Atomic push+ack for exactly-once delivery
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
| **Throughput** | 20K+ req/s sustained |
| **Latency** | 10-50ms (POP/ACK), 50-200ms (TRANSACTION) |
| **Partitions** | Unlimited |

**[Detailed Benchmarks →](https://smartpricing.github.io/queen/server/benchmarks)**

---

## Release History

| Server Version | Description | Compatible Clients |
|----------------|-------------|-------------------|
| **0.10.0** | Total rewrite of the engine with libuv and stored procedures, removed streaming engine | JS ≥0.7.4, Python ≥0.7.4 |
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
