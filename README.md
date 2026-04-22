# Queen MQ - Partitioned Message Queue backed by PostgreSQL



**Unlimited ordered partitions that never block each other. Consumer groups, replay, transactional delivery — ACID-guaranteed.**

*Note that queen-mq is available also as a experimental PostgreSQL extension, see [pg_qpubsub](pg_qpubsub/README.md) for more details.*

[License](LICENSE.md)
[Node](https://nodejs.org/)
[Python](https://www.python.org/)
[Go](https://go.dev/)
[PHP](https://www.php.net/)
[C++](https://en.cppreference.com/w/cpp/17)
[libuv](https://libuv.org/)
[libpq](https://www.postgresql.org/)
[uWebSockets](https://github.com/uNetworking/uWebSockets)

📚 **[Complete Documentation](https://smartpricing.github.io/queen/)** • 🚀 **[Quick Start](https://smartpricing.github.io/queen/guide/quickstart)** • ⚖️ **[Comparison](https://smartpricing.github.io/queen/guide/comparison)**





---

Queen MQ is a partitioned message queue backed by PostgreSQL, built with uWebSockets, libuv, and libpq async API. It features unlimited FIFO partitions that process independently, consumer groups with replay, transactional delivery, tracing, and ACID-guaranteed durability. With Queen you get Kafka semantics on PostgreSQL, using stateless clients that speak HTTP to a stateless server, easy to deploy and manage, but still powerful and flexible enough to sustain tens of thousands of requests per second across thousands of partitions.

For a experimental PostgreSQL extension version of Queen MQ, see [pg_qpubsub](pg_qpubsub/README.md).

See [examples/base.js](examples/base.js) for a complete (push, consume, transactionally ack and push to another queue) example.

## Why Queen?

Born at [Smartness](https://www.linkedin.com/company/smartness-com/) to power **Smartchat**, Queen solves a unique problem: **unlimited FIFO partitions** where slow processing in one partition doesn't block others.



Perfect for:

- Processing messages in order, without losing them somewhere in the middle
- Avoid slow message processing blocking other partitions, solving Head of Line Blocking problem
- When you need tens of thousands of partitions to process messages in parallel, respecting the order of the messages
- Process the same messages in multiple pipelines
- Have a clear view of the message processing flow and traceability
- Build event-driven microservices with exactly-once delivery guarantees
- Critical systems that need to be highly available and reliable with zero message loss

On a 32-core server with 24 PostgreSQL cores, Queen sustains **~47,300 msg/s** push throughput (batch=100, 1 KB payload) and **~194 MB/s** ingestion at 10 KB payloads, with zero errors and PostgreSQL running at 36-40 % of its cap. Sustained mixed PUSH+POP is around 10k req/s, and consumer-group fan-out reaches ~60k msg/s across 10 groups. See [test-perf/results.md](test-perf/results.md) for the full ledger.

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
docker run -p 6632:6632 --network queen -e PG_HOST=qpg -e PG_PORT=5432 -e PG_PASSWORD=postgres -e NUM_WORKERS=2 -e DB_POOL_SIZE=5 -e SIDECAR_POOL_SIZE=30 smartnessai/queen-mq:0.13.0
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

Then go to the dashboard ([http://localhost:6632](http://localhost:6632)) to see the messages and the status of the queue.

## Documentation

📚 **[Complete Documentation](https://smartpricing.github.io/queen/)**

### Getting Started

- [Quick Start Guide](https://smartpricing.github.io/queen/guide/quickstart)
- [Basic Concepts](https://smartpricing.github.io/queen/guide/concepts)

### Client Libraries

- [JavaScript Client](https://smartpricing.github.io/queen/clients/javascript)
- [Python Client](https://smartpricing.github.io/queen/clients/python)
- [Go Client](https://smartpricing.github.io/queen/clients/go)
- [PHP / Laravel Client](https://smartpricing.github.io/queen/clients/laravel)
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
- `client-go`: Go client library (go 1.21+)
- `client-laravel`: PHP / Laravel client library (php 8.1+)
- `client-cpp`: C++ client library (cpp 17)
- `proxy`: Proxy server (authentication)
- `app`: Vue.js dashboard (vue 3)
- `website`: Documentation website (vitepress)
- `examples`: JS client examples

---

## Release History

**JS clients from version 0.12.0 can be run inside a browser**


| Server Version | Description                                                                                                                     | Compatible Clients                                          |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------- |
| **0.13.0**     | Major release: new libqueen with adaptive batch/concurrency/scheduling engine (S1 ~2x, S3 ~3x push throughput), new `push_messages_v3` stored procedure, new Vue 3 dashboard, and server-stamped `producerSub` from the JWT on every message (closes #23) | All ≥0.12.x work unchanged — 0.13.0 pop responses add a new `producerSub` field that older clients silently ignore. Upgrade to 0.13.0 clients only if you want typed access to `producerSub` (Go struct field, Python TypedDict hint) |
| **0.12.19**    | Fix bug that on seek or cg delete do not deleted the watermark                                                                  | JS ≥0.7.4, Python ≥0.7.4                                    |
| **0.12.18**    | Improved charts and filters                                                                                                     | JS ≥0.7.4, Python ≥0.7.4                                    |
| **0.12.17**    | Improved stats                                                                                                                  | JS ≥0.7.4, Python ≥0.7.4                                    |
| **0.12.13**    | Added watermark tracking for efficient wildcard POP discovery. x20 faster pop on high partition count queues                    | JS ≥0.7.4, Python ≥0.7.4                                    |
| **0.12.12**    | Built-in database migration (pg_dump | pg_restore, no temp file, selective table groups, row count validation)                  | JS ≥0.7.4, Python ≥0.7.4                                    |
| **0.12.10**    | Fixed JWKS fetch over HTTPS (cpp-httplib TLS support)                                                                           | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use            |
| **0.12.9**     | Fixed server crash (SIGSEGV) on lease renewal, added EdDSA/JWKS auth, fixed examples                                            | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use            |
| **0.12.8**     | Added single partition move to now to frontend                                                                                  | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use            |
| **0.12.7**     | Optimized cg metadata creation for new consumer groups                                                                          | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use            |
| **0.12.6**     | Improved slow cg discovery when there are tons of partitions                                                                    | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use            |
| **0.12.5**     | Fixed cg lag calculation for "new" cg at first message                                                                          | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use            |
| **0.12.4**     | Fixed window buffer debounce behavior                                                                                           | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use proxy auth |
| **0.12.3**     | Added JWT authentication                                                                                                        | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use proxy auth |
| **0.12.x**     | New frontend and docs                                                                                                           | JS ≥0.7.4, Python ≥0.7.4, 0.12.0 if needs to use proxy auth |
| **0.11.x**     | Libqueen 0.11.0; added stats tables and optimized analytics procedures, added DB statement timeout and stats reconcile interval | JS ≥0.7.4, Python ≥0.7.4                                    |
| **0.10.x**     | Total rewrite of the engine with libuv and stored procedures, removed streaming engine                                          | JS ≥0.7.4, Python ≥0.7.4                                    |
| **0.8.0**      | Added Shared Cache with UDP sync for clustered deployment                                                                       | JS ≥0.7.4, Python ≥0.7.4                                    |
| **0.7.5**      | First stable release                                                                                                            | JS ≥0.7.4, Python ≥0.7.4                                    |


**[Full Release Notes →](https://smartpricing.github.io/queen/server/releases)**

---

## Bug fixing and improvements

- Server 0.13.0: **New libqueen with adaptive engine.** Per-worker push/ack drain factored into three independently-tuned concerns — batching, concurrency, scheduling — glued by an event-driven orchestrator. Fixes two long-standing bottlenecks: per-commit overhead amortization on small-batch workloads, and the single-slot-per-drain cap on high-fanout workloads. Perf harness numbers: S1 ~6.2k → ~13k pg_ins/s, S3 ~4.7k → ~20k pg_ins/s, PG pinned instead of idle. Design notes in `cdocs/LIBQUEEN_IMPROVEMENTS.md`.
- Server 0.13.0: **New push stored procedure.** `queen.push_messages_v2` rewritten around a temp-table + batched-insert pipeline that feeds cleanly into the adaptive engine. HTTP contract (queued/duplicate/failed) unchanged.
- Server 0.13.0: **New Vue 3 dashboard.** Reworked queues, analytics, DLQ management, and maintenance-mode views. Served by the same C++ acceptor at `/`.
- Server 0.13.0: Added server-stamped `producerSub` to close the impersonation vector from GitHub issue #23. When JWT auth is enabled the server stamps the validated `sub` claim on every pushed message; clients cannot set this field and it is exposed on pop responses and admin message APIs. Schema migration is additive and metadata-only (no table rewrite), safe on tables with millions of rows.
- Clients 0.13.0: All clients (JS, Python, Go, Laravel, C++) expose `producerSub` on popped messages; Go adds a typed `Message.ProducerSub` field.
- Server 0.12.19: Fix bug that on seek or cg delete do not deleted the watermark
- Server 0.12.17: Improved stats
- Server 0.12.13: Added watermark tracking for efficient wildcard POP discovery. x20 faster pop on high partition count queues
- Server 0.12.12: Added built-in database migration — stream pg_dump | pg_restore directly from the dashboard, no temp file, selective table groups, row count validation, PG 18 client in Docker image
- Clients 0.12.2: Added custom `headers` option to JS, Python, and Go clients for API gateway authentication
- Server 0.12.10: Fixed JWKS fetch over HTTPS (added CPPHTTPLIB_OPENSSL_SUPPORT to enable TLS in cpp-httplib)
- Server 0.12.9: Fixed server crash (SIGSEGV) on lease renewal caused by use-after-free of HttpRequest pointer
- Server 0.12.9: Added native EdDSA and JWKS JWT authentication (auto-discovery via JWT_JWKS_URL)
- Server 0.12.9: Fixed quickstart consumer example (autoAck + onError silent conflict)
- Server 0.12.9: Fixed examples 03-transactional-pipeline.js and 08-consumer-groups.js (missing .each())
- Server 0.12.8: Added single partition move to now to frontend
- Server 0.12.7: Optimized cg metadata creation for new consumer groups
- Server 0.12.6: Improved slow cg discovery when there are tons of partitions
- Server 0.12.5: Fixed cg lag calculation for "new" cg at first message
- Server 0.12.4: Fixed window buffer debounce behavior
- Clients 0.12.1: Fixed bug in transaction with consumer groups

---

## License

Queen MQ is released under the [Apache 2.0 License](LICENSE.md).

---



**Built with ❤️ by [Smartness](https://www.linkedin.com/company/smartness-com/)**

*Why "Queen"? Because years ago, when I first read "queue", I read it as "queen" in my mind.*

