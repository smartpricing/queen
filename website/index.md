---
layout: home

hero:
  name: "Queen MQ"
  text: "Modern PostgreSQL-backed Message Queue"
  tagline: High-performance, feature-rich message queue system with unlimited FIFO partitions, consumer groups, and transactions
  image:
    src: /queen_logo.png
    alt: Queen MQ Logo
  actions:
    - theme: brand
      text: Get Started
      link: /guide/quickstart
    - theme: alt
      text: View on GitHub
      link: https://github.com/smartpricing/queen
    - theme: alt
      text: Docker Hub
      link: https://hub.docker.com/r/smartnessai/queen-mq

features:
  - title: High Performance
    details: Handle 200K+ messages/sec with proper batching. Built with C++17, uWebSockets, and async PostgreSQL for minimal latency.
    link: /server/benchmark-results
  
  - title: Unlimited FIFO Partitions
    details: Create as many ordered partitions as you need. Messages in each partition are guaranteed to be processed in order with automatic lock management.
    link: /guide/queues-partitions
  
  - title: Consumer Groups
    details: Kafka-style consumer groups with independent position tracking. Process the same messages for different purposes with replay from any timestamp.
    link: /guide/consumer-groups
  
  - title: Transactions
    details: Atomic operations across queues with exactly-once delivery guarantees. Chain push and ack operations to build reliable workflows.
    link: /guide/transactions
  
  - title: Long Polling
    details: Efficient server-side waiting for messages. No busy loops, no wasted resources. Messages delivered instantly when available.
    link: /guide/long-polling
  
  - title: Zero Message Loss
    details: Automatic failover to disk when PostgreSQL is unavailable. Automatic replay when database recovers. Survives crashes and restarts.
    link: /guide/failover
  
  - title: Dead Letter Queue
    details: Automatic handling of failed messages. Configurable retry limits and DLQ routing. Debug and replay failed messages easily.
    link: /guide/dlq
  
  - title: Message Encryption
    details: Optional at-rest encryption at the database level. Secure your sensitive messages with per-queue encryption configuration.
    link: /guide/queues-partitions
  
  - title: Beautiful Dashboard
    details: Modern Vue 3 web interface with real-time metrics, message browser, trace explorer, and analytics. Monitor everything in one place.
    link: /webapp/overview
  
  - title: Message Tracing
    details: End-to-end tracing across queues and workflows. Debug complex distributed systems with visual trace timelines.
    link: /guide/tracing
  
  - title: Multi-Language Clients
    details: JavaScript, Python, and C++ clients with idiomatic APIs. Or use the HTTP API directly from any language.
    link: /clients/javascript
  
  - title: Distributed Cache (UDPSYNC)
    details: Multi-server deployments share state via UDP sync to reduce database queries by 80-90%. Real-time peer notifications for instant message delivery.
    link: /server/deployment#distributed-cache-udpsync
---

<style>
.VPFeatures {
  margin-top: 3rem;
}

.custom-block {
  margin: 2rem 0;
}
</style>

## 📚 Documentation

<div class="doc-links">
  <a href="./guide/quickstart" class="doc-link">
    <div class="doc-link-title">
      <span class="doc-link-icon">🚀</span>
      Quick Start
    </div>
    <div>Get started in minutes</div>
  </a>
  
  <a href="./clients/javascript" class="doc-link">
    <div class="doc-link-title">
      <span class="doc-link-icon">💻</span>
      Client Libraries
    </div>
    <div>JavaScript, Python, C++, and HTTP API</div>
  </a>
  
  <a href="./server/installation" class="doc-link">
    <div class="doc-link-title">
      <span class="doc-link-icon">⚙️</span>
      Server Setup
    </div>
    <div>Install and configure</div>
  </a>
  
  <a href="./server/deployment" class="doc-link">
    <div class="doc-link-title">
      <span class="doc-link-icon">☸️</span>
      Deployment
    </div>
    <div>Docker, K8s, systemd</div>
  </a>
  
  <a href="./webapp/overview" class="doc-link">
    <div class="doc-link-title">
      <span class="doc-link-icon">📊</span>
      Web Dashboard
    </div>
    <div>Monitor and manage</div>
  </a>
  
  <a href="./guide/comparison" class="doc-link">
    <div class="doc-link-title">
      <span class="doc-link-icon">⚖️</span>
      Comparison
    </div>
    <div>vs Kafka, RabbitMQ, NATS</div>
  </a>
</div>

## Why We Built Queen MQ

:::tip 🏨 Born from Real Production Needs
Queen was created at [Smartness](https://www.linkedin.com/company/smartness-com/) to power [**Smartchat**](https://www.smartness.com/en/guest-messaging) - an AI-powered guest messaging platform for the hospitality industry.
:::

### The Context

At Smartness, we use Kafka extensively across our infrastructure and know it well. For Smartchat's message backbone, we initially chose Kafka for its strong reliability guarantees.

However, we encountered a use case mismatch: **in Kafka, a single message processing delay affects the entire partition**. For most workloads this isn't an issue, but our system involves:

- **AI translations** - Can take seconds/minutes
- **Agent responses** - Can take seconds/minutes
- **Variable processing times** - Inherent to the domain

With potentially 100,000+ concurrent chats, we would need a Kafka partition for each chat conversation - which isn't practical at that scale.

### From Custom Tables to Queen

We started moving long-running operations to custom PostgreSQL queue tables. As we built out the system, we needed:

- **Consumer groups** - For different processing pipelines
- **Window buffering** - To aggregate messages before AI processing
- **Retry limits** - For better error visibility
- **Message tracing** - For debugging distributed workflows

We realized we had built a complete message queue system that better fit our specific requirements.

### The Result

Queen now handles Smartchat's message infrastructure:

- **One partition per chat** - Slow processing in one chat doesn't affect others
- **Unlimited partitions** - Scale to any number of concurrent conversations
- **Window buffering** - Aggregate messages to optimize API calls
- **Built-in observability** - Retry mechanisms and tracing for debugging
- **PostgreSQL-backed** - ACID guarantees with automatic failover

Queen processes 100,000+ messages daily in production.

:::info Technical Note
If you're building systems where message processing has inherently variable latency (chat systems, AI pipelines, human-in-the-loop workflows), Queen's partition model may be a better fit than traditional streaming platforms.
:::


## Quick Example

```javascript
import { Queen } from 'queen-mq'

// Connect
const queen = new Queen('http://localhost:6632')

// Create queue with configuration
await queen.queue('orders')
  .config({ leaseTime: 30, retryLimit: 3 })
  .create()

// Push messages with guaranteed order per partition
await queen.queue('orders')
  .partition('customer-123')
  .push([{ data: { orderId: 'ORD-001', amount: 99.99 } }])

// Consume with consumer groups for scalability
await queen.queue('orders')
  .group('order-processor')
  .concurrency(10)
  .batch(20)
  .autoAck(false)
  .each()
  .consume(async (message) => {
    await processOrder(message.data)
  })
  .onSuccess(async (message) => {
    await queen.ack(message, true, { group: 'order-processor' })
  }).onError(async (message, error) => {
    await queen.ack(message, false, { group: 'order-processor' })
  })
```

## Performance Metrics

<div class="custom-stats">
  <div class="custom-stat">
    <div class="custom-stat-value">200K+</div>
    <div class="custom-stat-label">Messages/Second</div>
  </div>
  <div class="custom-stat">
    <div class="custom-stat-value">10-50ms</div>
    <div class="custom-stat-label">Latency</div>
  </div>
  <div class="custom-stat">
    <div class="custom-stat-value">Unlimited</div>
    <div class="custom-stat-label">Partitions</div>
  </div>
  <div class="custom-stat">
    <div class="custom-stat-value">Zero</div>
    <div class="custom-stat-label">Message Loss</div>
  </div>
</div>

## Key Features Comparison

| Feature | Queen | RabbitMQ | Kafka | Redis Streams |
|---------|-------|----------|-------|---------------|
| FIFO Partitions | ✅ Unlimited | ❌ | ✅ Limited | ✅ Limited |
| Consumer Groups | ✅ | ⚠️ Manual | ✅ | ✅ |
| Transactions | ✅ Atomic | ⚠️ Complex | ⚠️ Limited | ❌ |
| Long Polling | ✅ Built-in | ✅ | ❌ | ✅ |
| Dead Letter Queue | ✅ Automatic | ✅ | ⚠️ Manual | ❌ |
| Message Replay | ✅ Timestamp | ❌ | ✅ | ✅ |
| Persistence | ✅ PostgreSQL | ✅ Disk | ✅ Disk | ⚠️ Memory |
| Distributed Cache | ✅ UDPSYNC | ❌ | ✅ ZooKeeper | ❌ |
| Web Dashboard | ✅ Modern | ⚠️ Basic | ⚠️ External | ❌ |

## Use Cases

### 🏢 Enterprise Workflows
Build reliable multi-step workflows with transactions and exactly-once delivery. Perfect for financial systems, order processing, and data pipelines.

### 📊 Real-time Analytics
Send messages to multiple consumer groups for different analytics purposes. Replay historical data from any timestamp.

### 🔄 Event-Driven Architecture
Decouple microservices with guaranteed message delivery. Built-in tracing helps debug distributed systems.

### 💼 Task Queues
Distribute work across multiple workers with automatic load balancing. Long-running tasks supported with lease renewal.

## Architecture

Queen uses a high-performance **acceptor/worker pattern** with fully asynchronous, non-blocking PostgreSQL architecture:

- **Network Layer**: uWebSockets with configurable workers (default: 10)
- **Database Layer**: Async connection pool (142 non-blocking connections)
- **Failover Layer**: Automatic disk buffering when PostgreSQL unavailable
- **Distributed Cache**: UDP-based state sync between servers (UDPSYNC)
- **Background Services**: Poll workers, metrics, retention, eviction

[Learn more about the architecture →](/server/architecture)

## Built by Developers, for Developers

Queen was created by [Smartness](https://www.linkedin.com/company/smartness-com/) to power their hospitality platform. It's built with modern C++17, uses PostgreSQL for ACID guarantees, and includes a beautiful Vue.js dashboard.

:::tip 🎉 Production Ready
Queen is currently running in production, handling millions of messages daily. Version {{VERSION}} is stable and ready for your projects.
:::

## Get Started in Minutes

```bash
# Start PostgreSQL and Queen server
docker network create queen
docker run --name postgres --network queen \
  -e POSTGRES_PASSWORD=postgres -p 5432:5432 -d postgres

docker run -p 6632:6632 --network queen \
  -e PG_HOST=postgres \
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
  smartnessai/queen-mq:{{VERSION}}

# Install JavaScript client
npm install queen-mq

# Or install Python client
pip install queen-mq

# Start building!
```

[Full Quick Start Guide →](/guide/quickstart)

## Community & Support

- **GitHub**: [github.com/smartpricing/queen](https://github.com/smartpricing/queen)
- **LinkedIn**: [Smartness Company Page](https://www.linkedin.com/company/smartness-com/)
- **Docker Hub**: [smartnessai/queen-mq](https://hub.docker.com/r/smartnessai/queen-mq)

## License

Queen MQ is released under the [Apache 2.0 License](https://github.com/smartpricing/queen/blob/master/LICENSE.md).

