# Queen MQ - Partitioned Message Queue backed by PostgreSQL

<div align="center">

**Unlimited ordered partitions that never block each other. Consumer groups, replay, transactional delivery — ACID-guaranteed.**

*Note that queen-mq is available also as a experimental PostgreSQL extension, see [pg_qpubsub](pg_qpubsub/README.md) for more details.*


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

Queen MQ is a partitioned message queue backed by PostgreSQL, built with uWebSockets, libuv, and libpq async API. It features unlimited FIFO partitions that process independently, consumer groups with replay, transactional delivery, tracing, and ACID-guaranteed durability. With Queen you get Kafka semantics on PostgreSQL, using stateless clients that speak HTTP to a stateless server, easy to deploy and manage, but still powerful and flexible enough to sustain tens of thousands of requests per second across thousands of partitions.

For a experimental PostgreSQL extension version of Queen MQ, see [pg_qpubsub](pg_qpubsub/README.md).

## Why Queen?

Born at [Smartness](https://www.linkedin.com/company/smartness-com/) to power **Smartchat**, Queen solves a unique problem: **unlimited FIFO partitions** where slow processing in one partition doesn't block others.

<p align="center">
  <img src="assets/dashboard.png" alt="Queen MQ Dashboard" height="350" />
</p>

Perfect for:
- Processing messages in order, without losing them somewhere in the middle
- Avoid slow message processing blocking other partitions, solving Head of Line Blocking problem
- When you need tens of thousands of partitions to process messages in parallel, respecting the order of the messages
- Process the same messages in multiple pipelines
- Have a clear view of the message processing flow and traceability
- Build event-driven microservices with exactly-once delivery guarantees
- Critical systems that need to be highly available and reliable with zero message loss

Its push peaks for single request is around 45k req/s, with sustatined load (PUSH+POP) around 10k req/s, and with consumer groups around 60k req/s.

## Quick Start

Create a Docker network and start PostgreSQL and Queen Server:

```bash
# Create a Docker network for Queen components
docker network create queen

# Start PostgreSQL
docker run --name qpg --network queen -e POSTGRES_PASSWORD=postgres -p 5433:5432 -d postgres

# Wait for PostgreSQL to start
sleep 2

# Start Queen Server
docker run -p 6632:6632 --network queen -e PG_HOST=qpg -e PG_PORT=5432 -e PG_PASSWORD=postgres -e NUM_WORKERS=2 -e DB_POOL_SIZE=5 -e SIDECAR_POOL_SIZE=30 smartnessai/queen-mq:0.12.2
```

Then in another terminal, use cURL (or the client libraries) to push and consume messages

**Push message:**
```bash
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{"items": [{"queue": "demo", "payload": {"hello": "world"}}]}'
```

that returns something like:
```json
[{"message_id": "...", "status": "queued", ...}]
```

**Consume message:**
```bash
curl "http://localhost:6632/api/v1/pop/queue/demo?autoAck=true"
```

that returns something like:
```json
{"messages": [{"data": {"hello": "world"}, ...}], "success": true}
```

Then go to the dashboard (http://localhost:6632) to see the messages and the status of the queue.

## Documentation

📚 **[Complete Documentation](https://smartpricing.github.io/queen/)**

### Getting Started
- [Quick Start Guide](https://smartpricing.github.io/queen/guide/quickstart)
- [Basic Concepts](https://smartpricing.github.io/queen/guide/concepts)

### Client Libraries
- [JavaScript Client](https://smartpricing.github.io/queen/clients/javascript)
- [Python Client](https://smartpricing.github.io/queen/clients/python)
- [C++ Client](https://smartpricing.github.io/queen/clients/cpp)
- [HTTP API Reference](https://smartpricing.github.io/queen/api/http)

--- 

## Structure of the repository

The repository is structured as follows:

- `lib`: C++ core queen library (libqueen), implementing libuv loops, sql schema and procedures
- `server`: Queen MQ server, implementing the HTTP API that talks to the libqueen library
- `pg_qpubsub`: PostgreSQL extension for using queen-mq semantics as a PostgreSQL extension
- `client-js`: JavaScript client library (browser and node.js)
- `client-py`: Python client library (python 3.8+)
- `client-cpp`: C++ client library (cpp 17)
- `proxy`: Proxy server (authentication)
- `app`: Vue.js dashboard (vue 3)
- `website`: Documentation website (vitepress)
- `examples`: JS client examples
---

## Release History

**JS clients from version 0.12.0 can be run inside a browser**

| Server Version | Description | Compatible Clients |
|----------------|-------------|-------------------|
| **0.12.3** | Added JWT authentication | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use proxy auth |
| **0.12.x** | New frontend and docs | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use proxy auth |
| **0.11.x** | Libqueen 0.11.0; added stats tables and optimized analytics procedures, added DB statement timeout and stats reconcile interval | JS ≥0.7.4, Python ≥0.7.4 |
| **0.10.x** | Total rewrite of the engine with libuv and stored procedures, removed streaming engine | JS ≥0.7.4, Python ≥0.7.4 |
| **0.8.0** | Added Shared Cache with UDP sync for clustered deployment | JS ≥0.7.4, Python ≥0.7.4 |
| **0.7.5** | First stable release | JS ≥0.7.4, Python ≥0.7.4 |

**[Full Release Notes →](https://smartpricing.github.io/queen/server/releases)**

--- 

## TODO

- Evaluate to replace udp sync with tcp with libuv
- Deploy pipeline
- Manually test pg_qpubsub extension

---

## License

Queen MQ is released under the [Apache 2.0 License](LICENSE.md).

---

<div align="center">

**Built with ❤️ by [Smartness](https://www.linkedin.com/company/smartness-com/)**

*Why "Queen"? Because years ago, when I first read "queue", I read it as "queen" in my mind.*

</div>
