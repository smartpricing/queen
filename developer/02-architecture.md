# 02 — Architecture

This page is a developer's mental model of how Queen is laid out, what runs where, and how a request travels through the system. It complements the user-facing `[docs/architecture.html](../docs/architecture.html)`, but here we focus on **code structure** and where to look when you want to change something.

---

## Two-binary architecture

In production Queen runs as **one process** alongside one Postgres:

```
            ┌─────────────────────┐
            │   queen-server      │   ← C++ broker (server/)
            │  ┌──────────────┐   │
            │  │   libqueen   │   │   ← header-only core (lib/)
            │  └──────┬───────┘   │
            │         │           │
            │   uWebSockets       │
            │   libuv + libpq     │
            └────────┬────────────┘
                     │
                     ▼
              ┌────────────┐
              │ PostgreSQL │       ← stores everything
              │  (queen    │
              │   schema)  │
              └────────────┘
```

There are no other services: no Redis, no ZooKeeper, no internal cluster gossip. Horizontal scaling is "run more `queen-server` processes pointed at the same Postgres."

Optional companion processes:

- `**proxy/**` — Node.js auth proxy in front of the broker (JWT, Google OAuth, RBAC). See [11 — Auth proxy](11-proxy.md).
- `**app/**` — Vue 3 dashboard. The broker embeds the prebuilt `dist/` and serves it at `/`. See [10 — Frontend dashboard](10-frontend-dashboard.md).

---

## Inside the broker (`server/`)

### Threading model: acceptor + workers + services

```
                 ┌────────────┐
   incoming ────▶│  Acceptor  │  one thread, listens on PORT (default 6632)
   TCP/HTTP      └─────┬──────┘
                       │ round-robin
              ┌────────┼───────────────┐
              ▼        ▼               ▼
         ┌────────┐ ┌────────┐    ┌────────┐
         │Worker 1│ │Worker 2│ …  │Worker N│   N = NUM_WORKERS (default 10)
         └───┬────┘ └───┬────┘    └───┬────┘
             │          │             │
             └──────────┴─────────────┘
                        │
              ┌─────────▼─────────────┐
              │  AsyncQueueManager     │  one per worker
              │  + AsyncDbPool         │  ~142 conns (95% of DB_POOL_SIZE)
              │  + libqueen orchestr.  │
              └─────────┬─────────────┘
                        │
                        ▼
                 ┌─────────────┐
                 │ PostgreSQL  │
                 └─────────────┘

  ┌────────────────────────────────────────┐
  │           Background services           │  separate threadpool, ~5% of DB_POOL_SIZE
  ├────────────────────────────────────────┤
  │ MetricsCollector       ── stats roll-up │
  │ RetentionService       ── cleanup       │
  │ EvictionService        ── max_wait_time │
  │ PartitionLookupReconcileService         │
  │ FileBuffer              ── PG failover  │
  │ SharedStateManager      ── POP_WAIT     │
  └────────────────────────────────────────┘
```

Where this lives in code:


| Concern                                        | File(s)                                                          |
| ---------------------------------------------- | ---------------------------------------------------------------- |
| Acceptor (`uv_listen`, round-robin to workers) | `server/src/acceptor_server.cpp`, `server/src/main_acceptor.cpp` |
| Worker event loop + HTTP routes                | `server/src/routes/*.cpp` (push, pop, ack, …)                    |
| Async DB pool (non-blocking libpq)             | `server/src/database/async_database.cpp`                         |
| Per-worker queue manager                       | `server/src/managers/async_queue_manager.cpp`                    |
| Background services                            | `server/src/services/*.cpp`                                      |
| JWT auth (when enabled)                        | `server/src/auth/auth_middleware.cpp`, `server/src/auth/jwt_validator.cpp` |


### HTTP routes

Each HTTP route is one `*.cpp` file in `server/src/routes/`. Notable ones:


| Route file                                    | What it does                                                |
| --------------------------------------------- | ----------------------------------------------------------- |
| `push.cpp`                                    | `POST /api/v1/push` — calls `queen.push_messages_v3`        |
| `pop.cpp`                                     | `POST/GET /api/v1/pop` — calls `queen.pop_unified_batch_v4` |
| `ack.cpp`                                     | `POST /api/v1/ack` — calls `queen.ack_messages_v2`          |
| `transactions.cpp`                            | atomic ack + push — calls `queen.execute_transaction_v2`    |
| `leases.cpp`                                  | `POST /api/v1/renew` — calls `queen.renew_lease_v2`         |
| `dlq.cpp`                                     | dead-letter inspection + replay                             |
| `consumer_groups.cpp`                         | subscription mode, replay-from-timestamp                    |
| `health.cpp`, `metrics.cpp`, `prometheus.cpp` | observability                                               |
| `static_files.cpp`                            | serves the embedded Vue dashboard                           |


Each route validates input, then delegates to a stored procedure via `AsyncQueueManager`. **The actual logic of push/pop/ack lives in SQL**, not in C++. See [05 — Database schema](05-database-schema.md).

---

## libqueen (`lib/`)

libqueen is a **header-only C++ library** that lives alongside the broker. The broker `#include`s it (`server/Makefile` adds `-I../lib`) and uses it for:

- **UUIDv7 generation** (time-ordered IDs for ordering guarantees)
- **Per-worker drain orchestrator** that batches push/pop/ack/transaction/renew requests across many concurrent HTTP connections, calls the stored procedures with one round-trip, and routes responses back to the original waiter
- **Adaptive concurrency control** (TCP Vegas-style — grow when DB has spare capacity, shrink when overloaded) to keep latency bounded under load

### The drain orchestrator

This is the trick that lets ~142 DB connections serve hundreds of thousands of concurrent in-flight HTTP requests:

```
many HTTP handlers ──▶ PerTypeQueue (per JobType)
                            │
                            │  BatchPolicy says FIRE when:
                            │   - queue.size >= preferred_batch_size
                            │   - or oldest_age >= max_hold_ms
                            ▼
                    ConcurrencyController.try_acquire()
                            │  (Vegas-adaptive cap)
                            ▼
                    grab idle DBConnection slot
                            │
                            ▼
                    one stored procedure call,
                    JSONB array of N requests
                            │
                            ▼
                    libuv-poll fires on socket-readable
                    fan-out responses to PendingJob.callback
```

The relevant files (small, readable, all in `lib/queen/`):

| File                           | Layer | Purpose                                               |
| ------------------------------ | ----- | ----------------------------------------------------- |
| `pending_job.hpp`              | —     | `JobType` enum, `JobRequest`, `PendingJob` (callback) |
| `per_type_queue.hpp`           | 1     | thread-safe FIFO of pending jobs, per op type         |
| `batch_policy.hpp`             | 2     | pure FIRE/HOLD decision on (size, oldest_age)         |
| `concurrency/static_limit.hpp` | 3     | fixed concurrency cap                                 |
| `concurrency/vegas_limit.hpp`  | 3     | adaptive cap based on RTT (TCP Vegas-style)           |
| `slot_pool.hpp`                | —     | `DBConnection` struct (a "slot" *is* a libpq conn)    |
| `drain_orchestrator.hpp`       | —     | `PerTypeState` aggregate + concurrency-ctrl factory   |
| `metrics.hpp`                  | —     | atomics for drain-loop stats                          |
| (drain loop itself)            | 4     | inside the `Queen` class in `queen.hpp`               |


See [04 — libqueen](04-libqueen.md) for a code-walkthrough.

---

## Database layer (`lib/schema/`)

Two parts:

1. `**schema.sql**` — base tables, indexes, constraints. Idempotent (`IF NOT EXISTS`). ~488 lines.
2. `**procedures/*.sql**` — stored procedures, all `CREATE OR REPLACE FUNCTION`. ~18 files, ~10k lines.

The broker discovers these on startup (`AsyncQueueManager::initialize_schema`) by walking up the directory tree looking for `schema/schema.sql`. Then it loads every `procedures/*.sql` in lexical order.

The most important procedures:


| File                      | Procedure                  | What it does                              |
| ------------------------- | -------------------------- | ----------------------------------------- |
| `001_push.sql`            | `push_messages_v3` (v2 also defined for rollback) | batch insert, dedup, partition upsert |
| `002d_pop_unified_v4.sql` | `pop_unified_batch_v4`     | wildcard multi-partition pop with leases  |
| `003_ack.sql`             | `ack_messages_v2`          | mark consumed, advance `last_consumed_id` |
| `004_transaction.sql`     | `execute_transaction_v2`   | atomic ack + push (transactional outbox)  |
| `005_renew_lease.sql`     | `renew_lease_v2`           | extends a lease for long-running consumers |
| `008_consumer_groups.sql` | replay, subscription modes | per-group state                           |
| `013_stats.sql`           | rolls up `queen.stats`     | dashboard data                            |
| `016_partition_lookup.sql` | `update_partition_lookup_v1`, `reconcile_partition_lookup_v1` | post-push lookup maintenance |


> ⚠ **The procedures are load-bearing and tightly coupled to each other.** See the warning in [05 — Database schema](05-database-schema.md#warning-stored-procedures-are-load-bearing) before changing anything.

---

## Request flow — what happens on a `push`

1. **Client** sends `POST /api/v1/push` to the broker.
2. **Acceptor** picks a worker (round-robin) and hands off the connection.
3. **Worker** parses JSON, validates, hands off to `AsyncQueueManager`.
4. **libqueen drain loop** (Layer 4 in the `Queen` class) queues the request in the per-worker push `PerTypeQueue`. The `BatchPolicy` (Layer 2) decides when to fire — `queue.size >= preferred_batch_size` (default 50 for push) or `oldest_age >= max_hold_ms` (default 20 ms). The `ConcurrencyController` (Layer 3) decides whether there's headroom to start a new in-flight batch. If yes, the drain loop grabs an idle `DBConnection` slot.
5. The slot is an idle libpq connection. The drain loop builds one big JSONB array of all queued push requests and calls `queen.push_messages_v3($1::jsonb)` with a single async query.
6. **Postgres** runs the procedure: it upserts queues + partitions, deduplicates by `(partition_id, transaction_id)`, batch-inserts into `queen.messages`, and returns a JSONB summary.
7. The worker fan-outs results back to each waiter. Each HTTP handler writes its response.
8. **After the response is written**, libqueen fires a fire-and-forget call to `queen.update_partition_lookup_v1()` to refresh the `partition_lookup` table outside the hot path. This is the design from `[cdocs/PUSHPOPLOOKUPSOL.md](../cdocs/PUSHPOPLOOKUPSOL.md)`.

---

## Request flow — what happens on a `pop`

1. Same as 1–3 above for `pop`.
2. `pop_unified_batch_v4` is called with up to N partition candidates. Each candidate is probed with `pg_try_advisory_xact_lock(partition_id)` so concurrent consumers don't fight over the same partition.
3. Won partitions are leased (consumer's `worker_id` written into `queen.partition_consumers`, `lease_expires_at = NOW() + lease_time`).
4. Messages above `last_consumed_id` are returned to the consumer.
5. On `ack`, `last_consumed_id` advances and the lease is released (or auto-released when `acked_count >= batch_size`).

If the consumer used `wait=true` and the queue is empty, the long-poll path kicks in: `SharedStateManager` re-checks every 100 ms (with exponential backoff up to 1 s) until either data arrives or the timeout elapses.

---

## Failover path (when Postgres goes down)

A separate code path keeps PUSH durable even when Postgres is unreachable:

```
push request ──▶ try DB ──▶ timeout ──▶ FileBuffer.write()
                                              │
                                              ▼
                                  /var/lib/queen/buffers/*.buf
                                              │
   background processor (every 100 ms) ──▶ try to flush ──▶ DB
```

See [12 — Failover](12-failover.md) for details. Code: `server/src/services/file_buffer.cpp`.

---

## Where to look when…


| Symptom / task                   | Look here                                                                                                     |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| Broker won't compile             | `server/Makefile`, [03 — Building the server](03-build-server.md)                                             |
| Adding/changing an HTTP endpoint | `server/src/routes/<topic>.cpp`                                                                               |
| Adding/changing a SQL procedure  | `lib/schema/procedures/`, then [05 — Database schema](05-database-schema.md)                                  |
| Pop/push contention              | `lib/schema/procedures/002d_pop_unified_v4.sql` + `[cdocs/PUSHPOPLOOKUPSOL.md](../cdocs/PUSHPOPLOOKUPSOL.md)` |
| Broker latency / throughput      | `lib/queen/drain_orchestrator.hpp`, `concurrency/vegas_limit.hpp`                                             |
| Cleanup / retention              | `server/src/services/retention_service.cpp` + [09 — Retention](09-retention.md)                               |
| Dashboard                        | `app/src/` + [10 — Frontend dashboard](10-frontend-dashboard.md)                                              |
| Auth                             | `proxy/` + [11 — Auth proxy](11-proxy.md)                                                                     |


