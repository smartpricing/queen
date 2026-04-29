# 04 — libqueen

`lib/` contains **libqueen**, the header-only C++ core that the broker uses to talk to Postgres. This page is a code-walkthrough for people who need to change the broker's hot path.

If you're trying to tune throughput at the operator level (env vars, pool sizes), see `[server/README.md](../server/README.md)`. This page is for people changing the *code*.

---

## What libqueen is, what it isn't

**It is:**

- A header-only C++17 library (`#include "queen.hpp"`)
- The "engine" inside the broker that batches **PUSH / POP / ACK / TRANSACTION / RENEW_LEASE / CUSTOM** requests across many in-flight HTTP connections, executes them as one Postgres call (per type, per drain tick), and routes responses back to waiters. TRANSACTION and CUSTOM are not batchable — they go one job per call.
- Self-tested via `lib/test_suite.cpp` (integration, needs Postgres) and `lib/queen_test.cpp` (unit, no Postgres)

**It isn't:**

- A standalone server. The broker (`server/`) provides the network, the routes, the JWT auth, the file-buffer failover, the metrics collector, the dashboard, etc.
- A public client API. End users use one of the SDKs in `clients/`.

---

## File layout

```
lib/
├── queen.hpp                              ← single entry header. Defines the
│                                            `Queen` class which owns the drain
│                                            loop itself (Layer 4).
├── worker_metrics.hpp                     ← per-worker counters
├── queen/
│   ├── pending_job.hpp                    ← JobType enum + JobRequest + PendingJob
│   ├── per_type_queue.hpp                 ← Layer 1: thread-safe job queue
│   ├── batch_policy.hpp                   ← Layer 2: pure FIRE/HOLD decision
│   ├── slot_pool.hpp                      ← DBConnection struct (a "slot" IS a
│   │                                          PG connection; the pool itself is
│   │                                          owned by the Queen class)
│   ├── drain_orchestrator.hpp             ← `PerTypeState` aggregate + concurrency-
│   │                                          controller construction (NOT the loop)
│   ├── metrics.hpp                        ← orchestrator counters
│   └── concurrency/
│       ├── concurrency_controller.hpp     ← Layer 3 interface
│       ├── static_limit.hpp               ← fixed cap
│       └── vegas_limit.hpp                ← TCP Vegas-style adaptive cap
├── schema/                                ← see chapter 05
├── test.cpp                               ← basic smoke test
├── test_suite.cpp                         ← full integration tests (~2.6k lines)
├── test_contention.cpp                    ← PUSH+POP throughput benchmark
├── queen_test.cpp                         ← unit tests (no PG)
└── Makefile                               ← test build targets
```

The headers are layered explicitly (the comments inside each file call out
their layer number):

- **Layer 1** — `PerTypeQueue` — thread-safe job queue, reports state
- **Layer 2** — `BatchPolicy` — pure function: should we fire? how many?
- **Layer 3** — `ConcurrencyController` — gates in-flight batch count
- **Layer 4** — drain loop — lives in the `Queen` class itself in `queen.hpp`,
  because it has to touch `DBConnection` slots, libpq, and libuv handles that
  the `Queen` owns.

`drain_orchestrator.hpp` is therefore *not* the drain loop — it's the per-type
state aggregate (`PerTypeState`) plus the helper that builds a concurrency
controller from env vars.

---

## Mental model: drain orchestrator

The broker has hundreds of HTTP connections in flight at once but only ~142 DB connections. libqueen's job is to bridge that gap **without** turning every request into a per-request DB round-trip:

```
┌────────────────────────────────────────────────────────────────┐
│                       Per-worker libqueen                      │
│                                                                │
│   PerTypeQueue[PUSH]  PerTypeQueue[POP]   PerTypeQueue[ACK]    │
│        │                     │                    │            │
│        ▼                     ▼                    ▼            │
│   ┌─────────────────────────────────────────────────────┐      │
│   │             Queen drain loop (Layer 4)               │     │
│   │   - polls queues, applies BatchPolicy                │     │
│   │   - asks ConcurrencyController for permission        │     │
│   │   - grabs an idle DBConnection slot                  │     │
│   │   - assembles 1 JSONB array, calls 1 SQL function    │     │
│   │   - libuv-poll-driven response → fans out callbacks  │     │
│   └─────────────────────────────────────────────────────┘      │
│                              │                                 │
│                              ▼                                 │
│                  DBConnection slots (libpq async)              │
│                              │                                 │
│                              ▼                                 │
│                          PostgreSQL                            │
└────────────────────────────────────────────────────────────────┘
```

Result: a worker handling 1000 concurrent push HTTP requests typically issues **1** SQL call with a 1000-element JSONB array. The stored procedure `push_messages_v3` is built to do exactly this in a single transaction.

---

## Walkthrough — the key types

### `JobType` enum — `lib/queen/pending_job.hpp`

The enumeration of operations libqueen knows how to dispatch. The order matters
for round-robin fairness in the drain loop:

| `JobType`     | Batchable? | SQL function libqueen calls       |
|---------------|------------|-----------------------------------|
| `PUSH`        | yes        | `queen.push_messages_v3($1::jsonb)` |
| `POP`         | yes (in groups) | `queen.pop_unified_batch_v4($1::jsonb)` |
| `ACK`         | yes        | `queen.ack_messages_v2($1::jsonb)` |
| `TRANSACTION` | **no — serial** | `queen.execute_transaction_v2($1::jsonb)` |
| `RENEW_LEASE` | yes        | `queen.renew_lease_v2($1::jsonb)` |
| `CUSTOM`      | **no — per-job SQL** | each job carries its own `sql` |

`_COUNT` and `_SENTINEL` are array-sizing / "idle slot" markers, not real
operations. The dispatch table is `JobTypeToSql` in `pending_job.hpp`.

### `PendingJob` and `JobRequest` — `lib/queen/pending_job.hpp`

A single in-flight request. Not templated — one `struct` for all op types.

- `JobRequest` carries the payload, op type, batch sizing, POP long-poll deadline (`wait_deadline`), backoff state, queue/partition/consumer-group strings (used to group jobs into one SQL call when they're compatible), etc.
- `PendingJob` adds the completion callback: `std::function<void(std::string result)>`. The result is the raw stringified Postgres response — parsing happens in the route handlers.

### `PerTypeQueue` — `lib/queen/per_type_queue.hpp`

Thread-safe FIFO of `std::shared_ptr<PendingJob>`. One instance per (worker, op type). Not templated. Uses `uv_mutex_t` so it composes with libuv handles without bouncing through `std::mutex`. Maintains `front_enqueue_time` so the drain loop can compute "age of oldest job" in O(1).

`push_back` returns `was_empty` so the orchestrator can implement a **submit-kick** heuristic — a new job arriving on a previously empty queue can immediately wake the drain loop instead of waiting for the next libuv timer tick.

### `BatchPolicy` — `lib/queen/batch_policy.hpp`

A pure function — knows nothing about slots, connections, or libuv. Two
independent FIRE triggers:

1. `queue_size >= preferred_batch_size` → FIRE
2. `oldest_age >= max_hold_ms` → FIRE
3. otherwise (including empty queue) → HOLD

The "submit-kick" on transition-from-empty is implemented at the orchestrator level (using `PerTypeQueue::push_back`'s return value), not inside `BatchPolicy`.

Defaults baked into `default_batch_policy_for(JobType)` (note `max_concurrent` was raised from the original 4 to the values below in 2026-04-22 — see `cdocs/LIBQUEEN_IMPROVEMENTS.md`):

| JobType       | preferred | max_hold_ms | max_batch | max_concurrent |
|---------------|-----------|-------------|-----------|----------------|
| `PUSH`        | 50        | 20          | 500       | **24**         |
| `POP`         | 20        | 5           | 500       | **16**         |
| `ACK`         | 50        | 20          | 500       | **16**         |
| `TRANSACTION` | 1         | 0           | 1         | 1              |
| `RENEW_LEASE` | 10        | 100         | 100       | 2              |
| `CUSTOM`      | 1         | 0           | 1         | 1              |

Every field is overridable via env vars `QUEEN_<TYPE>_PREFERRED_BATCH_SIZE`, `QUEEN_<TYPE>_MAX_HOLD_MS`, `QUEEN_<TYPE>_MAX_BATCH_SIZE`, `QUEEN_<TYPE>_MAX_CONCURRENT`. There's also a global `SIDECAR_MICRO_BATCH_WAIT_MS` that acts as the fallback for any per-type `MAX_HOLD_MS` not explicitly set.

### `DBConnection` ("a slot") — `lib/queen/slot_pool.hpp`

A "slot" in libqueen is **the libpq connection itself** plus its libuv poll
handle, not a permit on top of a connection. Each `DBConnection` carries:

- `PGconn*` + `socket_fd` — the libpq async connection
- `uv_poll_t poll_handle` — libuv watches the socket for readability
- `JobType current_type` + `FireRecord current_fire` — what's running, for metrics
- `std::vector<std::shared_ptr<PendingJob>> jobs` — the batch attached to this in-flight call
- `needs_poll_init` atomic — used by the reconnect thread to hand a fresh socket back to the event loop

The pool of `DBConnection`s is owned by the `Queen` class in `queen.hpp`. Per-op-type concurrency is enforced separately by `ConcurrencyController` — it gates how many slots a given type may simultaneously hold, but the slots themselves are a single shared array.

### `ConcurrencyController` — `lib/queen/concurrency/`

Decides how many concurrent batches a given op type may have in flight:

- `static_limit.hpp` — fixed cap
- `vegas_limit.hpp` — adaptive, modeled on TCP Vegas:
  - Maintains `limit ∈ [min_limit, max_limit]`
  - Computes `queue_load = in_flight × (1 − rtt_min/rtt_recent)`
  - `queue_load < alpha` → grow (DB has spare capacity)
  - `queue_load > beta` → shrink (DB is overloaded)
  - `rtt_min` is a 30 s sliding minimum (so a single fast outlier doesn't pin it)
  - `rtt_recent` is an EMA over the last N completions
  - Limit is adjusted at most once per `update_interval_ms` (default 1 s) to avoid thrashing

Default mode is **Vegas** (`QUEEN_CONCURRENCY_MODE=vegas`). Vegas envs:

| Env var                          | Default | Meaning                                    |
|----------------------------------|---------|--------------------------------------------|
| `QUEEN_VEGAS_MIN_LIMIT`          | 1       | Floor                                      |
| `QUEEN_VEGAS_MAX_LIMIT`          | 32      | Global ceiling (raised from 16 in 2026-04-22) |
| `QUEEN_VEGAS_ALPHA`              | 3       | Grow threshold                             |
| `QUEEN_VEGAS_BETA`               | 12      | Shrink threshold (raised from 6)           |
| `QUEEN_VEGAS_RTT_WINDOW_SAMPLES` | 50      | EMA window over recent completions         |
| `QUEEN_VEGAS_RTT_MIN_WINDOW_SEC` | 30      | Sliding-minimum window for rtt_min         |
| `QUEEN_VEGAS_UPDATE_INTERVAL_MS` | 1000    | Min time between limit adjustments         |

The effective per-type max is `min(QUEEN_<TYPE>_MAX_CONCURRENT, QUEEN_VEGAS_MAX_LIMIT)`. If you raise per-type, also raise `QUEEN_VEGAS_MAX_LIMIT` or you'll be clipped.

### Drain loop — inside the `Queen` class in `queen.hpp`

The loop itself is **not** in `drain_orchestrator.hpp`. It lives on the `Queen`
class because it needs the `DBConnection` array, the libuv loop handle, and
the libpq sockets. Roughly:

```text
on libuv timer or submit-kick wakeup:
  for each JobType in round-robin order:
    s = queue.snapshot(now)
    if BatchPolicy(s.size, s.oldest_age) == FIRE:
      if ConcurrencyController.try_acquire():
        slot = idle_slots.pop()       // a DBConnection
        if no slot available: rollback acquire, continue
        jobs = queue.take_front(batch_size_to_take(s.size))
        slot.current_type = type
        slot.jobs = jobs
        sendQueryParamsAsync(slot.conn, JobTypeToSql[type], [jsonb_array_of(jobs)])
        // libuv poll handler will fire when result is ready,
        // dispatch to each job's callback, then mark slot idle.
```

### `worker_metrics.hpp`, `queen/metrics.hpp`

Atomic counters exposed on the broker's `/metrics` endpoint. Prometheus
scraping happens in `server/src/routes/prometheus.cpp`. Important counters
when debugging contention:

- queue size + oldest age, per type
- batches fired, per type
- in-flight slots, per type
- Vegas current limit, per type
- slot acquisition wait time

---

## How the broker uses libqueen

`server/src/managers/async_queue_manager.cpp` is the only file in the broker that talks to libqueen directly. Look there for:

- How `PerTypeQueue` instances are created (one per worker × op type)
- How HTTP route handlers (`push.cpp` etc.) submit jobs (`queue.push(PendingJob{...})` and await on a promise)
- How the orchestrator is started/stopped on broker shutdown
- How schema initialization runs once on startup

Quick pointer:

```cpp
// server/src/managers/async_queue_manager.cpp (~line 170)
bool AsyncQueueManager::initialize_schema() {
    // looks for lib/schema/schema.sql, applies it, then loads
    // every lib/schema/procedures/*.sql with CREATE OR REPLACE
}
```

---

## Modifying libqueen — guidelines

1. **Don't add new dependencies.** libqueen is intentionally header-only and uses only what `queen.hpp` already imports (libuv, libpq, spdlog, nlohmann/json). Adding Boost or anything else means every consumer of libqueen has to re-vendor it.
2. **Keep the orchestrator wait-free on the hot path.** Use `std::atomic` for counters, `uv_mutex_t` for queues. Lock-free queues have been considered but the current `PerTypeQueue` is plenty fast.
3. **Don't move logic from SQL into C++.** The procedures are the source of truth for push/pop/ack semantics. libqueen's job is *transport*, not business logic.
4. **Add unit tests.** `lib/queen_test.cpp` runs without Postgres — add a case there for any new layer (PerTypeQueue, BatchPolicy, SlotPool, DrainOrchestrator). Then add an integration scenario in `lib/test_suite.cpp` if you also need to verify against the real DB.
5. **Benchmark.** Run `lib/test_contention.cpp` before and after. PUSH/POP balance is the metric that matters most. See `[cdocs/PUSHPOPLOOKUPSOL.md](../cdocs/PUSHPOPLOOKUPSOL.md)` for what "good" looks like.

---

## Building & running libqueen tests

`lib/Makefile` builds against `server/vendor/` (so build the broker first to ensure deps are downloaded):

```bash
# Once: download vendor deps via the broker build
cd server && make deps && cd ..

# Then in lib/
cd lib

make debug          # show detected paths
make test-unit      # unit tests, no Postgres needed
make test-suite     # integration tests, needs PG (set PG_* envs)
make test-contention # PUSH+POP throughput benchmark
make clean
```

Full guide in [07 — Testing](07-testing.md).

---

## See also

- `[cdocs/PUSHVSPOP.md](../cdocs/PUSHVSPOP.md)` — diagnosis of why combined PUSH+POP load used to collapse
- `[cdocs/PUSHPOPLOOKUPSOL.md](../cdocs/PUSHPOPLOOKUPSOL.md)` — the design that fixed it (drives why `partition_lookup` is updated post-response)
- `[cdocs/LIBQUEEN_TUNING.md](../cdocs/LIBQUEEN_TUNING.md)` — env vars for batch policy + concurrency controllers
- `[cdocs/LIBQUEEN_IMPROVEMENTS.md](../cdocs/LIBQUEEN_IMPROVEMENTS.md)` — log of orchestrator-level improvements

