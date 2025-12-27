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

It push peaks for single request is around 45k req/s, with sustatined load (PUSH+POP) around 10k req/s.

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

## Release History

| Server Version | Description | Compatible Clients |
|----------------|-------------|-------------------|
| **0.12.0** | New frontend and docs | JS ≥0.7.4, Python ≥0.7.4 |
| **0.11.0** | Libqueen 0.11.0; added stats tables and optimized analytics procedures, added DB statement timeout and stats reconcile interval | JS ≥0.7.4, Python ≥0.7.4 |
| **0.10.0** | Total rewrite of the engine with libuv and stored procedures, removed streaming engine | JS ≥0.7.4, Python ≥0.7.4 |
| **0.8.0** | Added Shared Cache with UDP sync for clustered deployment | JS ≥0.7.4, Python ≥0.7.4 |
| **0.7.5** | First stable release | JS ≥0.7.4, Python ≥0.7.4 |

**[Full Release Notes →](https://smartpricing.github.io/queen/server/releases)**

--- 

## TODO

- Evaluate to replace udp sync witch tcp with libuv
- Proxy new with auth
- Retention data on frontend
- Deploy pipeline

---

## License

Queen MQ is released under the [Apache 2.0 License](LICENSE.md).

---

<div align="center">

**Built with ❤️ by [Smartness](https://www.linkedin.com/company/smartness-com/)**

*Why "Queen"? Because years ago, when I first read "queue", I read it as "queen" in my mind.*

</div>
