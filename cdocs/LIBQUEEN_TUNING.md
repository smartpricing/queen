# libqueen — how it works and how to tune it

This doc explains the internal design of libqueen (the HTTP-to-PostgreSQL sidecar inside `queen-server`) and how to tune it for production workloads. The tuning guidance is grounded in the measured data from `test-perf/RESULTS.md` §1.7 and §1.8, not in theory.

If you only read one thing, read **[§3 "Should I tune at all?"](#3-should-i-tune-at-all)** — on almost any workload, the answer is "no".

---

## 1. What libqueen does

libqueen sits inside each Queen worker and owns the *hot path* — the conversion of HTTP-level PUSH / POP / ACK / TRANSACTION calls into efficient PostgreSQL round-trips. It replaces a naïve "one commit per HTTP request" pattern with an **adaptive, event-driven, per-type batching pipeline**.

Its job is to hit three goals simultaneously:

1. **Throughput** — merge many small HTTP requests into few fat PG commits (amortize per-commit overhead).
2. **Tail latency** — never let a request sit waiting for a fixed timer tick when work could already be flowing.
3. **Adaptivity** — react to PG slowing down (heavy queries, co-tenant noise, lock contention) by shrinking concurrency automatically, and grow it back when PG recovers.

It achieves these through four independent layers, each implementing a well-known industry pattern.

---

## 2. Architecture in one page

```
   HTTP request arrives on Queen worker
                 │
                 ▼
   submit() ──► Per-type queue  (Layer 1)
                 │                          One queue per JobType:
                 │                          PUSH, POP, ACK, TRANSACTION,
                 │                          RENEW_LEASE, CUSTOM.
                 │                          Thread-safe (uv_mutex per type).
                 │
                 │   uv_async_send ("submit-kick")
                 │
                 ▼
   ┌─────────────────────────────────────────────────────────────────┐
   │ DRAIN ORCHESTRATOR  (Layer 4, event-loop thread only)           │
   │                                                                 │
   │ for each type in round-robin:                                   │
   │   snap = queue.snapshot()                                       │
   │   if snap.size == 0: skip                                       │
   │   if BatchPolicy.should_fire(snap) == HOLD:                     │
   │       schedule safety-timer wake  →  continue                   │
   │   if !ConcurrencyController.try_acquire(): break                │
   │   batch = queue.take_front(policy.batch_size_to_take())         │
   │   _fire_batch(batch) ──► PG via libpq (async)                   │
   └─────────────────────────────────────────────────────────────────┘
                 │                                    │
                 │ BatchPolicy (Layer 2):            │ ConcurrencyController (Layer 3):
                 │   preferred_batch_size             │   Vegas (default) or Static.
                 │   max_hold_ms                      │   Adapts per-type in_flight cap
                 │   max_batch_size                   │   based on observed RTT.
                 ▼                                    ▼
   PG async query ──────────────────────────► Completion callback
                 │                                    │
                 │        slot-free-kick              │
                 └────────────────────────────────────┘
                 │
                 ▼
   _on_slot_freed() → release concurrency, record completion,
                      drain again (event-driven loop continues)
```

**Three event sources** feed the drain orchestrator, and only these three:


| Event              | When                                         | How                                  |
| ------------------ | -------------------------------------------- | ------------------------------------ |
| **submit-kick**    | HTTP worker calls `submit()`                 | `uv_async_send()` — coalesced, cheap |
| **slot-free-kick** | a PG batch completes                         | direct call on the event-loop thread |
| **timer-kick**     | `max_hold_ms` safety net or POP `next_check` | `uv_timer_t`, re-armed dynamically   |


The drain is otherwise silent — no fixed-interval timer wastes CPU on idle workers.

### 2.1 The four layers


| Layer                      | File                                                                    | Responsibility                                                                                                                                                                           |
| -------------------------- | ----------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1 · Per-type queue         | `lib/queen/per_type_queue.hpp`                                          | Thread-safe FIFO per JobType. Tracks oldest-job age in O(1) so the batch policy can consult "how long has the front of the queue been waiting?" without scanning.                        |
| 2 · Batch policy           | `lib/queen/batch_policy.hpp`                                            | Pure function: *given queue state, should we fire and how many?* Triton-style: fire if `queue_size ≥ preferred_batch_size` OR `oldest_age ≥ max_hold_ms`. Take at most `max_batch_size`. |
| 3 · Concurrency controller | `lib/queen/concurrency/vegas_limit.hpp` (default) or `static_limit.hpp` | Gates in-flight batch count per type. Vegas adapts: grows when observed RTT is stable (queue_load < α), shrinks when RTT rises (queue_load > β).                                         |
| 4 · Drain orchestrator     | `lib/queen.hpp`                                                         | The only component that touches DB slots, libpq, and libuv handles. Event-driven; never polls.                                                                                           |


The three layers 1-3 are **inspectable in isolation** — each is unit-tested without PG (`lib/queen_test.cpp`, 50/50 passing).

### 2.2 What Vegas actually does (with measured examples)

Every batch completion feeds a `CompletionRecord` (fire time, complete time, success flag) into the Vegas controller, which maintains:

- `**rtt_min`** — 30-second sliding minimum of batch RTTs (best-case time under no contention).
- `**rtt_recent**` — EMA over the last ~50 completions (current operating time).
- `**limit**` — the current in-flight cap; adjusted at most once per second to avoid thrashing.

The decision rule is:

```
queue_load = in_flight × (1 − rtt_min / rtt_recent)

if queue_load < α (default 3):   limit++  (if below max)
if queue_load > β (default 12):  limit--  (if above min)
otherwise:                        hold
```

Interpretation: `queue_load` is roughly "how many batches are queueing behind the bottleneck". Under `α` the system has slack, so expand. Above `β` there's real queueing, so back off.

Measured steady-states from the campaign (§1.8):


| Workload                       | Vegas `limit` converges to | Why                                                                           |
| ------------------------------ | -------------------------- | ----------------------------------------------------------------------------- |
| 1 KB payload, batch=10         | **~24** (= PUSH cap)       | Fast commits (~5 ms) → `queue_load < α` → grows to ceiling.                   |
| 10 KB payload, batch=10        | **~6-22** (scales with PG) | Slow commits (~20-80 ms) → `queue_load` hits `α` earlier → settles below cap. |
| 10 KB payload, 1-core PG       | **~5**                     | RTT inflation from saturation pushes `queue_load` up fast → holds low.        |
| 1 KB payload, `push_batch=100` | **~23**                    | Fat batches keep slots busy longer → Vegas stays near cap but just below.     |


The same Vegas algorithm converges to different operating points on different workloads — that's the whole point.

---

## 3. Should I tune at all?

**For 95 % of deployments: no.** The default values are calibrated on real PostgreSQL running the actual `queen.`* stored procedures, and Vegas adapts on top of them.

Situations where you should leave defaults alone:


| Condition                                             | Reason                                                            |
| ----------------------------------------------------- | ----------------------------------------------------------------- |
| PG has ≤ 16 cores                                     | Current defaults (`MAX_CONCURRENT=24`) are sized for this.        |
| Average payload ≤ 2 KB                                | `MAX_BATCH_SIZE=500` keeps per-batch payload ≤ 1 MB.              |
| You have ≤ 10 Queen workers per server instance       | Fragmentation penalty is minimal.                                 |
| You care about latency first, throughput second       | Event-driven drain delivers near-optimal tail latency at default. |
| Your deployment is single-node queen + single-node PG | Defaults fit this exactly.                                        |


Situations where tuning *might* matter:


| Condition                                                               | Knob to consider                                     | See  |
| ----------------------------------------------------------------------- | ---------------------------------------------------- | ---- |
| PG has ≥ 32 cores                                                       | `QUEEN_VEGAS_MAX_LIMIT`, `QUEEN_PUSH_MAX_CONCURRENT` | §5.3 |
| Your average payload is ≥ 10 KB                                         | `QUEEN_PUSH_MAX_BATCH_SIZE`                          | §5.4 |
| You're running many tiny Queen servers (e.g., per-tenant)               | `QUEEN_PUSH_PREFERRED_BATCH_SIZE`                    | §5.5 |
| You want strictly predictable behaviour (e.g., a regulated environment) | `QUEEN_CONCURRENCY_MODE=static`                      | §5.6 |


---

## 4. The knobs at a glance

See `server/ENV_VARIABLES.md` for the full reference. The ones that matter most, grouped by *axis they tune*:

### 4.1 Capacity axis — scales with PG cores


| Env var                     | Default | What it does                                                                     |
| --------------------------- | ------- | -------------------------------------------------------------------------------- |
| `QUEEN_PUSH_MAX_CONCURRENT` | **24**  | Upper bound on in-flight PUSH batches per worker.                                |
| `QUEEN_ACK_MAX_CONCURRENT`  | **16**  | Same for ACK. Lower because advisory-lock contention caps real parallelism.      |
| `QUEEN_POP_MAX_CONCURRENT`  | **16**  | Same for POP.                                                                    |
| `QUEEN_VEGAS_MAX_LIMIT`     | **32**  | Global Vegas ceiling. Effective cap is `min(MAX_CONCURRENT, this)`.              |
| `QUEEN_VEGAS_BETA`          | **12**  | Vegas shrinks when `queue_load > beta`. Must be < `MAX_CONCURRENT` to ever fire. |


### 4.2 Amortization axis — NOT core-dependent


| Env var                           | Default | What it does                                                          |
| --------------------------------- | ------- | --------------------------------------------------------------------- |
| `QUEEN_PUSH_PREFERRED_BATCH_SIZE` | **50**  | Fire immediately when the queue reaches this many items.              |
| `QUEEN_PUSH_MAX_HOLD_MS`          | **20**  | Fire even below preferred if the front of the queue waited this long. |


### 4.3 Safety caps — scale with *payload size*, not cores


| Env var                     | Default | What it does                                                         |
| --------------------------- | ------- | -------------------------------------------------------------------- |
| `QUEEN_PUSH_MAX_BATCH_SIZE` | **500** | Hard cap on items per fire. Main purpose: bound single-batch memory. |


### 4.4 Worker count (server-wide, not libqueen per se)


| Env var       | Default | What it does                                                                                            |
| ------------- | ------- | ------------------------------------------------------------------------------------------------------- |
| `NUM_WORKERS` | **10**  | Number of independent Queen workers. Each has its own libqueen. HTTP traffic is sharded across workers. |


---

## 5. Tuning by deployment shape

Numbers below come from the 2026-04-22 server-box campaign (§1.8) on a 32-core Ubuntu 24.04 host with PG running in Docker, default `synchronous_commit=on`. `msg/s` values are PG ground truth from `pg_stat_user_tables`.

### 5.1 Small deployment — PG with 2-4 cores

**Recommendation: use all defaults. Don't touch anything.**

Measured ceilings (2 Queen workers × 4-core PG):


| Workload         | msg/s           | p99    | PG util |
| ---------------- | --------------- | ------ | ------- |
| 1 KB / batch=10  | 19,307          | 355 ms | 64 %    |
| 10 KB / batch=10 | 7,141 (73 MB/s) | 589 ms | 54 %    |


Going to `NUM_WORKERS=10` on a 2-core PG actually *regresses* throughput by ~10 % because of batch fragmentation — the opposite of what you'd expect. Stick with 2-4 workers at this scale.

### 5.2 Medium deployment — PG with 8-16 cores

**Recommendation: `NUM_WORKERS=10`, everything else default.**

Measured ceilings (16-core PG, 10 Queen workers, `batch=10`):


| Workload         | msg/s             | p99      | PG util | Notes                       |
| ---------------- | ----------------- | -------- | ------- | --------------------------- |
| 1 KB / batch=10  | 33,783            | 211 ms   | 47 %    | plenty of PG headroom       |
| 10 KB / batch=10 | 18,043 (185 MB/s) | 451 ms   | 39 %    |                             |
| 1 KB / batch=1   | 6,386             | 57 ms    | 35 %    | single-msg workload         |
| 1 KB / batch=100 | 47,300            | 2,162 ms | 40 %    | high throughput, worse tail |


Notice: at this scale, PG is still not the bottleneck. Producer/client pressure is.

### 5.3 Large deployment — PG with 24+ cores

**Recommendation: start with `NUM_WORKERS=10`. Only raise `MAX_CONCURRENT` / `VEGAS_MAX_LIMIT` if you observe the `[libqueen]` log showing Vegas `limit` pinned at 24 with `in_flight` also at the cap** (meaning Vegas wants to grow further but we're clipping it).

On our 24-core PG with default `MAX_CONCURRENT=24`, Vegas converged to ~24 on light workloads but PG utilization was only 27-40 %. The bottleneck was the producer (autocannon), not the server stack — so raising `MAX_CONCURRENT` further wouldn't help in that bench. In a real deployment with many real clients, PG-side contention may become the limit before the cap does.

Guidance for PG with C cores:

```
QUEEN_PUSH_MAX_CONCURRENT  = max(24, C)       # 1 slot per PG core, up to Vegas cap
QUEEN_ACK_MAX_CONCURRENT   = max(16, C // 2)  # advisory-lock bound
QUEEN_POP_MAX_CONCURRENT   = max(16, C // 2)  # advisory-lock bound
QUEEN_VEGAS_MAX_LIMIT      = max(32, 2 * QUEEN_PUSH_MAX_CONCURRENT)
QUEEN_VEGAS_BETA           = max(12, QUEEN_PUSH_MAX_CONCURRENT // 2)
```

Above ~32 PG cores, measurement on real hardware is essential — we lack empirical data past 24 cores with this harness (we hit client-side saturation first).

### 5.4 Heavy payloads — average ≥ 10 KB

Each in-flight batch holds `MAX_BATCH_SIZE × payload` bytes in memory. At defaults (500 × 10 KB = 5 MB per batch × 24 concurrent = **120 MB per worker** of in-flight memory).

**Rule of thumb**: keep per-batch payload ≤ 500 KB.

```
QUEEN_PUSH_MAX_BATCH_SIZE  =  500_000 / avg_payload_bytes
                           (clamped to [1, 500])
```

Examples:

- Avg 1 KB:  `MAX_BATCH_SIZE = 500` (default)
- Avg 4 KB:  `MAX_BATCH_SIZE = 125`
- Avg 10 KB: `MAX_BATCH_SIZE = 50`
- Avg 50 KB: `MAX_BATCH_SIZE = 10`

You may also want to slightly raise `MAX_HOLD_MS` for heavy payloads (more serialization latency per message means a longer hold still produces good batches). `25-30` is reasonable above ~5 KB.

### 5.5 Many small Queen servers (per-tenant, per-shard)

If you run one Queen server per tenant and each server sees low traffic (e.g., < 500 req/s), every batch is likely triggered by `MAX_HOLD_MS` rather than `PREFERRED_BATCH_SIZE`. In this case:

- Consider `NUM_WORKERS=2` or `4` (not 10) — fewer workers means each sees a larger slice of the already-small traffic, so batching has something to work with.
- `MAX_HOLD_MS` becomes your dominant latency floor. Lower it if your SLA is tight (e.g., 10 ms), but expect smaller batches.

### 5.6 Strictly predictable behaviour

Set `QUEEN_CONCURRENCY_MODE=static` to disable Vegas. `MAX_CONCURRENT` becomes a hard cap and doesn't adapt. Useful for regulated environments or for diagnosing whether Vegas adaptation is masking a problem.

```
QUEEN_CONCURRENCY_MODE=static
QUEEN_PUSH_MAX_CONCURRENT=8   # pick a number, live with it
```

Performance will be slightly lower than Vegas on average workloads but has zero run-time variability.

---

## 6. Reading the `[libqueen]` stats log

Once per second per worker, libqueen emits a line like:

```
[Worker 0] [libqueen] push(q=3 f=4/24 p99rtt=18ms) ack(q=0 f=1/16 p99rtt=12ms) \
    slots=14/100 jobs/s=3472 drains=812 evl=0ms
```

Decoding:


| Field                          | Meaning                                                                          |
| ------------------------------ | -------------------------------------------------------------------------------- |
| `push(q=3 f=4/24 p99rtt=18ms)` | PUSH type: queue depth 3, in-flight 4 of limit 24, recent-batch RTT p99 = 18 ms. |
| `ack(q=0 f=1/16 ...)`          | ACK type: only 1 in flight.                                                      |
| `slots=14/100`                 | 14 of 100 DB connection slots currently idle.                                    |
| `jobs/s=3472`                  | Callback invocations per second (one per job, not per message).                  |
| `drains=812`                   | Drain-loop invocations this second.                                              |
| `evl=0ms`                      | Event-loop lag. Non-zero = libqueen is falling behind.                           |


### Healthy patterns

- `f=X/Y` where Y is **less** than the configured `MAX_CONCURRENT` → Vegas is actively managing the limit; you're not clipped.
- `p99rtt` stable under load → PG is not saturated.
- `q=0` most of the time → the drain is keeping up.
- `drains` on the order of `submits + completions` → expected; the event-driven loop is working.
- `evl=0-5ms` → event loop is healthy.

### Problem signatures


| Signature                                            | Likely cause                                              | Remedy                                                                                           |
| ---------------------------------------------------- | --------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| `f=Y/Y` consistently where Y = your `MAX_CONCURRENT` | Vegas wants more capacity than the cap allows             | Raise `MAX_CONCURRENT` for that type.                                                            |
| `p99rtt` climbing monotonically                      | PG slowing down, Vegas will shrink eventually             | Investigate PG (locks, vacuum, co-tenant).                                                       |
| `q` growing without bound                            | drain can't keep up, producer outpaces PG throughput      | Raise `MAX_CONCURRENT` (if PG has headroom) or `MAX_BATCH_SIZE`, or apply backpressure upstream. |
| `evl > 10ms` routinely                               | event loop overloaded (too many HTTP requests per worker) | More `NUM_WORKERS`, or cap connection count at the LB.                                           |
| `slots=0/N` consistently                             | all DB connections held in flight, new types blocked      | Raise `SIDECAR_POOL_SIZE` or lower total `MAX_CONCURRENT`.                                       |
| `jobs/s` near zero despite `q > 0`                   | PG not responding, reconnect loop may be active           | Check `server.log` for reconnect / slot error messages.                                          |


---

## 7. When NOT to tune

If your system shows any of the following, **libqueen is probably not the bottleneck** and tuning its knobs will make things worse by masking the real issue:

- **PG CPU pinned at `cores × 100 %`**: PG-side contention or I/O. Fix PG first (indexes, autovacuum, hardware, `synchronous_commit`).
- **Server CPU pinned at `NUM_WORKERS × 100 %`**: HTTP server is bottlenecked. Add workers, or scale horizontally.
- **Network saturation**: look at `bytes/s` logged in perf runs; if you're near your NIC limit, no libqueen knob helps.
- **High `errors` / `non2xx` in autocannon**: clients are failing. libqueen tuning doesn't fix this — fix the error source first.
- `**queue_full hits > 0`**: the intake queue is hitting a cap somewhere upstream. Look at HTTP-layer backpressure before libqueen.

---

## 8. How it all ties together — one example

Say you run Queen on a 16-core host with an 8-core PG, pushing 1 KB messages with 5-item HTTP batches from ~100 concurrent clients. Defaults in effect:

1. **Layer 1**: messages arrive into the PUSH queue. Queue grows during HTTP arrival bursts.
2. **Layer 2**: `PREFERRED_BATCH_SIZE=50` → Vegas sees the queue hit 50 and the drain fires a batch. Or if traffic is lighter, `MAX_HOLD_MS=20` fires it at age 20 ms instead.
3. **Layer 3**: Vegas (starting at `min_limit=1`) observes RTT. As PG stays stable, queue_load < 3 → grows. Over ~30 seconds, Vegas settles around `limit=8-12` for this workload (enough to keep 8 PG cores busy, but not more than that).
4. **Layer 4**: the drain orchestrator, triggered by each submit-kick and slot-free-kick, iterates types round-robin and packs as many batches as Vegas allows into free DB slots.
5. When PG happens to slow down (checkpoint, autovacuum, whatever), `rtt_recent` climbs, `queue_load > 12`, Vegas shrinks `limit` to 6, and the system quietly backs off. When PG recovers, Vegas grows back.

You didn't configure any of this per-workload. That's the point of Vegas.

---

## 9. References

- Plan: `LIBQUEEN_IMPROVEMENTS.md` (the design doc the refactor implements)
- Perf data: `test-perf/RESULTS.md` §1.7 (plan validation) and §1.8 (server-box saturation campaign)
- Env vars: `server/ENV_VARIABLES.md`
- Unit tests (50 pass, no PG): `lib/queen_test.cpp`
- TCP Vegas: L. Brakmo and L. Peterson, IEEE JSAC 13(8), 1995.
- Triton Dynamic Batcher: [https://github.com/triton-inference-server/server](https://github.com/triton-inference-server/server)
- Netflix `concurrency-limits`: [https://github.com/Netflix/concurrency-limits](https://github.com/Netflix/concurrency-limits)

