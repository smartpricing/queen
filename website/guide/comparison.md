# Comparison with Other Message Queues

How Queen MQ compares to RabbitMQ, Kafka, NATS, and Redis Streams.

## Why Another Message Queue?

Queen was born from real production needs at [Smartness](https://www.linkedin.com/company/smartness-com/) to power [**Smartchat**](https://www.smartness.com/en/guest-messaging) - an AI-powered guest messaging platform for hospitality.

### The Problem We Faced

We use Kafka extensively and know it well. For Smartchat's message backbone, we initially chose Kafka for its strong reliability guarantees.

However, we encountered a use case mismatch: **in Kafka, a single message processing delay affects the entire partition**. For most workloads this isn't an issue, but our system involves:

- **AI translations** - Can take seconds/minutes
- **Agent responses** - Can take seconds/minutes  
- **Variable processing times** - Inherent to the domain

With potentially 100,000+ concurrent chats, we would need a Kafka partition for each chat conversation - which isn't practical at that scale.

### The Solution

Queen provides **unlimited FIFO partitions** where slow processing in one partition doesn't affect others. This architectural difference makes it ideal for:

- Chat systems with variable response times
- AI processing pipelines with unpredictable latency
- Human-in-the-loop workflows
- Any system with inherently variable message processing

## Conceptual Mapping

How Queen concepts map to other systems:

| Queen Concept | RabbitMQ | Kafka | NATS (JetStream) | Redis Streams |
|---------------|----------|-------|------------------|---------------|
| **Queue** | Queue | Topic | Stream | Stream |
| **Partition** | N/A* | Partition | N/A | Consumer Group |
| **Consumer Group** | Competing Consumers | Consumer Group | Queue Group | Consumer Group |
| **Queue Mode** | Exclusive consumer | N/A** | Single subscriber | Single consumer |
| **Lease** | Message TTL / Visibility | N/A*** | Ack wait / nak delay | N/A |
| **Ack/Nack** | Ack/Nack | Commit offset | Ack/Nak | XACK |
| **Transaction** | Publisher confirms + acks | Transactional API | N/A | MULTI/EXEC |
| **Dead Letter Queue** | Dead Letter Exchange | Manual**** | Manual | Manual |

## Feature Comparison

| Feature | Queen | RabbitMQ | Kafka | NATS | Redis Streams |
|---------|-------|----------|-------|------|---------------|
| **FIFO Partitions** | ✅ Unlimited | ❌ | ✅ Limited | ❌ | ✅ Limited |
| **Consumer Groups** | ✅ Built-in | ⚠️ Manual setup | ✅ Built-in | ✅ Built-in | ✅ Built-in |
| **Transactions** | ✅ Atomic push+ack | ⚠️ Complex | ⚠️ Limited scope | ❌ | ⚠️ Basic MULTI |
| **Long Polling** | ✅ Native | ✅ Native | ❌ | ✅ Native | ✅ XREAD BLOCK |
| **Dead Letter Queue** | ✅ Automatic | ✅ Via DLX | ⚠️ Manual | ⚠️ Manual | ❌ |
| **Message Replay** | ✅ By timestamp | ❌ | ✅ By offset | ✅ By time/seq | ✅ By ID |
| **Persistence** | ✅ PostgreSQL | ✅ Disk | ✅ Disk | ✅ Disk/Memory | ⚠️ Memory-first |
| **Exactly-Once** | ✅ Native | ⚠️ Complex | ⚠️ Complex | ❌ | ❌ |
| **Web Dashboard** | ✅ Modern Vue.js | ⚠️ Basic | ⚠️ External tools | ❌ | ❌ |
| **Query Language** | ✅ SQL | ❌ | ❌ | ❌ | ❌ |
| **Encryption** | ✅ Per-queue | ✅ TLS | ✅ TLS | ✅ TLS | ✅ TLS |
| **Message Tracing** | ✅ Built-in | ⚠️ Plugins | ⚠️ External | ❌ | ❌ |

## Deep Dive Comparisons

### vs RabbitMQ

**When to use Queen over RabbitMQ:**

**Need unlimited partitions** - RabbitMQ queues don't support internal partitioning  
**Want PostgreSQL integration** - Leverage existing DB infrastructure  
**Complex workflows** - Native transaction support for push+ack  
**SQL queries** - Query messages and metadata with SQL  
**Modern dashboard** - Built-in Vue.js web interface

**When to use RabbitMQ over Queen:**

**Need routing complexity** - Exchanges, bindings, routing keys  
**AMQP ecosystem** - Extensive tooling and client libraries  
**Mature feature set** - Decades of production hardening  
**No PostgreSQL dependency** - Standalone message broker

### vs Kafka

**When to use Queen over Kafka:**

**Variable processing times** - One slow message doesn't block partition  
**Need unlimited partitions** - Don't hit Kafka's partition limits  
**Simpler operations** - No ZooKeeper/KRaft to manage  
**SQL integration** - Direct database access  
**Smaller scale** - Easier to operate for smaller deployments  

**When to use Kafka over Queen:**

**Multi-datacenter replication** - Kafka's replication is battle-tested  
**Stream processing** - Kafka Streams ecosystem  
**Massive scale** - Proven at trillion-message scale  
**Log compaction** - Advanced retention strategies  
**Ecosystem** - Connectors, Schema Registry, ksqlDB

**Architecture Difference:**

```
Kafka:
┌─────────────┐
│ Topic       │
│  Partition 0│ ← Consumer 1 (processes ALL messages in P0)
│  Partition 1│ ← Consumer 2 (processes ALL messages in P1)
│  Partition 2│ ← Consumer 3 (processes ALL messages in P2)
└─────────────┘
Problem: Slow message in P0 blocks entire partition

Queen:
┌─────────────┐
│ Queue       │
│  Part chat-1│ ← Consumer 1 (ONLY chat-1 messages)
│  Part chat-2│ ← Consumer 2 (ONLY chat-2 messages)
│  Part chat-3│ ← Consumer 3 (ONLY chat-3 messages)
│  Part...    │ ← Consumer N (N can be unlimited)
└─────────────┘
Solution: Slow message in chat-1 doesn't affect chat-2
```

### vs NATS / JetStream

**When to use Queen over NATS:**

**Need consumer groups** - More advanced than queue groups  
**Complex workflows** - Transaction support  
**PostgreSQL integration** - Direct SQL access  
**Message replay** - Flexible timestamp-based replay  
**Dead letter queue** - Automatic failure handling

**When to use NATS over Queen:**

**Low latency critical** - NATS is extremely fast  
**Pub/sub patterns** - NATS excels at pub/sub  
**Lightweight** - Minimal resource footprint  
**At-most-once delivery** - Fire-and-forget patterns


### vs Redis Streams

**When to use Queen over Redis:**

**Durability requirements** - PostgreSQL ACID guarantees  
**Complex queries** - SQL over messages  
**Large messages** - No memory constraints  
**Automatic DLQ** - Built-in failure handling  
**Transaction support** - Atomic push+ack

**When to use Redis over Queen:**

**Ultra-low latency** - In-memory performance  
**Already using Redis** - Leverage existing infrastructure  
**Simple use cases** - Minimal setup  
**High throughput** - Memory-speed operations

## When to Choose Queen

### Perfect Use Cases

#### 1. Chat Systems
```
Variable response times (AI, agents, users)**
Need per-conversation ordering**
100K+ concurrent conversations**
Message history and replay**
```

#### 2. AI Processing Pipelines
```
Unpredictable processing times
Need per-document/per-user queues
Workflow orchestration
Failure tracking and retry
```

#### 3. Human-in-the-Loop Workflows
```
Variable human response times
Task routing and prioritization
Audit trail requirements
PostgreSQL integration for workflow state
```

#### 4. Event-Driven Microservices
```
Transaction guarantees
Message tracing across services
Dead letter queue handling
SQL queries for debugging
```

### Not Ideal For

**Ultra-high throughput** (>1M msg/s) - Use Kafka  
**Ultra-low latency** (<1ms) - Use NATS  
**Multi-datacenter replication** - Use Kafka  
**Complex routing patterns** - Use RabbitMQ  
**In-memory only** - Use Redis

## Migration Guides

### From Kafka

**Concept mapping:**
- Kafka Topic → Queen Queue
- Kafka Partition → Queen Partition (but unlimited)
- Kafka Consumer Group → Queen Consumer Group
- Kafka Offset → Queen Consumer Position

**Code example:**
```javascript
// Kafka
const consumer = kafka.consumer({ groupId: 'my-group' })
await consumer.subscribe({ topic: 'my-topic' })
await consumer.run({
  eachMessage: async ({ message }) => {
    await process(message.value)
  }
})

// Queen equivalent
await queen
  .queue('my-queue')
  .group('my-group')
  .each()
  .consume(async (message) => {
    await process(message.data)
  })
```

### From RabbitMQ

**Concept mapping:**
- RabbitMQ Queue → Queen Queue
- RabbitMQ Consumer → Queen Consumer
- RabbitMQ Dead Letter Exchange → Queen DLQ
- RabbitMQ TTL → Queen Lease Time

**Code example:**
```javascript
// RabbitMQ
channel.consume('my-queue', async (msg) => {
  await process(msg.content)
  channel.ack(msg)
})

// Queen equivalent
await queen.queue('my-queue').consume(async (message) => {
  await process(message.data)
  // Auto-ack on success
})
```

### From NATS

**Concept mapping:**
- NATS Subject → Queen Queue
- NATS Queue Group → Queen Consumer Group
- NATS Stream → Queen Queue with Consumer Groups

**Code example:**
```javascript
// NATS
js.subscribe('my-subject', {
  queue: 'my-queue-group',
  callback: (err, msg) => {
    process(msg.data())
    msg.ack()
  }
})

// Queen equivalent
await queen
  .queue('my-subject')
  .group('my-queue-group')
  .consume(async (message) => {
    await process(message.data)
  })
```

## Architecture Philosophy

### Queen's Design Principles

1. **Dumbness** - Dumb clients and servers over HTTP
2. **PostgreSQL-First** - Leverage database ACID guarantees
3. **Unlimited Partitions** - No artificial limits
4. **Developer-Friendly** - SQL queries, modern dashboard
5. **Operational Simplicity** - No complex clustering
6. **Zero Message Loss** - Automatic failover to disk

### Trade-offs

**What we optimized for:**
- Ease of operation
- Developer experience  
- PostgreSQL integration
- Variable processing times
- Unlimited partitions

**What we traded off:**
- Raw throughput (vs Kafka)
- Ultra-low latency (vs NATS)
- Multi-DC replication (vs Kafka)

## Conclusion

Choose Queen when you need:

- **Unlimited FIFO partitions** with independent processing
- **PostgreSQL integration** for your infrastructure
- **Variable processing times** that would block Kafka partitions
- **Simple operations** without clustering complexity
- **Modern tooling** (SQL queries, web dashboard, tracing)

Queen is production-ready and processing millions of messages daily at Smartness.

## See Also

- [Quick Start](/guide/quickstart) - Get started with Queen
- [Concepts](/guide/concepts) - Understand core concepts
- [Why Queen MQ](/guide/introduction#why-we-built-queen-mq) - Origin story

