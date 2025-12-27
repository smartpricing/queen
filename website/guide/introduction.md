# Introduction to Queen MQ

Queen MQ is a modern, PostgreSQL-backed message queue system designed for high performance, reliability, and developer happiness. Born from the need to manage many FIFO partitions for [Smartchat](https://www.linkedin.com/company/smartness-com/) with solid guarantees around delivery and failure handling.

## What is Queen MQ?

Queen is a message queue system written in C++ that leverages PostgreSQL's reliability and ACID guarantees. It combines the best features from existing message queue systems like RabbitMQ, Kafka, and NATS into a unified, powerful platform.

:::info Why "Queen"?
Years ago, when the creator first read the word "queue", they read it as "queen" in their mind. The name stuck!
:::

## Key Features

### Unlimited FIFO Partitions
Create as many ordered partitions as you need within each queue. Messages in the same partition are guaranteed to be processed in order, with automatic lock management to ensure only one consumer processes a partition at a time.

### Consumer Groups
Kafka-style consumer groups allow multiple groups to process the same messages independently. Each group tracks its own position and can start from the beginning, from a specific timestamp, or only process new messages.

### Transactions
Atomic operations across queues with exactly-once delivery guarantees. Chain push and ack operations together to build reliable workflows that never lose or duplicate messages.

### Long Polling
Efficient server-side waiting for messages. No busy loops, no wasted resources. Messages are delivered instantly when they become available.

### Zero Message Loss
Automatic failover to disk when PostgreSQL is unavailable. Messages are buffered locally and automatically replayed when the database recovers. Survives crashes and restarts.

### Dead Letter Queue
Automatic handling of failed messages. Configure retry limits and have messages automatically routed to the dead letter queue for debugging and manual intervention.

## Architecture Highlights

- **High Performance**: Handle 10k req/s with houndreds of thousands of messages/second
- **C++17**: Modern C++ with uWebSockets and libuv
- **Async PostgreSQL**: Fully asynchronous, non-blocking database operations thoughout libuv
- **Horizontal Scaling**: Multiple server instances for HA
- **Modern Dashboard**: Beautiful Vue 3 web interface for monitoring and management

## Use Cases

### Enterprise Workflows
Build multi-step workflows with transactional guarantees:
- Order processing pipelines
- Financial transaction systems
- Data transformation pipelines
- Multi-stage approval processes

### Real-time Analytics
Send messages to multiple consumer groups for different purposes:
- Real-time metrics aggregation
- Business intelligence dashboards
- Log aggregation and analysis
- Event monitoring

### Event-Driven Architecture
Decouple microservices with guaranteed message delivery:
- Service-to-service communication
- Event sourcing
- CQRS patterns
- Saga orchestration

### Task Queues
Distribute work across multiple workers:
- Background job processing
- Video transcoding
- Report generation
- Email delivery


## Design Principles

1. **Developer First**: Simple, intuitive APIs that make complex workflows easy
2. **Performance**: Built for speed with async I/O and minimal overhead
3. **Reliability**: ACID guarantees, automatic failover, zero message loss
4. **Flexibility**: Support multiple messaging patterns (queue, pub/sub)
5. **Observability**: Built-in tracing, metrics, and beautiful dashboard

## Getting Started

Ready to dive in? Check out the [Quick Start Guide](/guide/quickstart) to get Queen MQ running in minutes.

Or explore specific topics:
- [Basic Concepts](/guide/concepts) - Understand queues, partitions, and consumer groups
- [JavaScript Client](/clients/javascript) - Start building with the JS client
- [Python Client](/clients/python) - Start building with the Python client
- [C++ Client](/clients/cpp) - High-performance C++ client
- [Server Setup](/server/installation) - Install and configure the server
- [Examples](/clients/examples/basic) - See real-world usage patterns
