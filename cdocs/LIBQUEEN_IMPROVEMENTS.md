# libqueen improvements plan — separated batch policy, concurrency control, event-driven drain

## 1. Summary

Redesign the libqueen per-worker drain into three independently-owned concerns — **batching**, **concurrency**, and **scheduling** — each implementing a well-understood industry pattern, glued together by a minimal event-driven orchestrator.

Grounded in `test-perf/RESULTS.md` §1.6: the current single-knob timer drain is simultaneously under-tuned on S1 (per-commit overhead not amortized) and structurally broken on S3 (single-slot-per-drain limit that no tuning knob reaches).

**Headline targets** (measured against the §1.6 push_v3 baseline, same hardware, n=3 × 150 s):

| Scenario | §1.6 best             | Target                                                        |
| -------- | --------------------- | ------------------------------------------------------------- |
| S0       | ~6,600 pg_ins/s       | ≥ 6,600 pg_ins/s, p99 ≤ 500 ms                                |
| S1       | 13,215 pg_ins/s (n=1) | ≥ 13,000 pg_ins/s, p99 ≤ 300 ms, stddev < 10% of mean         |
| S3       | 6,670 pg_ins/s (n=1)  | ≥ 20,000 pg_ins/s, p99 ≤ 500 ms                               |

**Scope**: a single coherent refactor of `lib/queen.hpp` and the files it pulls in. No phased rollout, no feature flag for the old code path, no intermediate milestones. Land the whole thing on a branch, validate against the perf harness, ship.

**Out of scope** (explicitly deferred, not part of this work): queue-depth backpressure, HTTP 503, file-buffer policy changes, ACK advisory-lock pre-grouping, cross-worker slot sharing, replacing libpq.

**Supersedes** `LIBQUEEN_HOTPATH_IMPROVEMENT.md`: keeps its structural ideas (multi-slot, slot-free-kick, submit-kick), drops the queue-depth/503 scope, replaces the single-mechanism framing with the three-layer factoring the §1.6 data motivates.

## 2. Motivation

From `test-perf/RESULTS.md`:

- **§1.4** push-improvement baseline: S1 = 6,185 pg_ins/s, S3 = 4,654 pg_ins/s at the 5 ms default. Both far below PG capacity.
- **§1.6** wait_ms sweep on push_v3 (same binary, only the knob changes):
  - S1 at wait=20 reaches 13,215 pg_ins/s with p99 267 ms. PG just below pinned. **On S1, per-commit overhead amortization was the bottleneck.**
  - S3 at wait=10 gets 6,670 pg_ins/s with p99 2,040 ms. PG pinned at 400% but per-message cost is 3.5× S1's — batches average 41 msgs on S3 vs 66 on S1 because the drain fires one batch per type per tick regardless of how many slots are idle. **On S3, single-slot-per-drain was the bottleneck.**
- **§2.1** correction: the two bottlenecks are independent. Neither `wait_ms` tuning alone nor a "fire on every slot-free" Nagle alone addresses both.

Industry precedent for the factoring: Kafka producer batching (`linger.ms` + `batch.size`), NVIDIA Triton Dynamic Batcher, PostgreSQL group commit, Netflix `concurrency-limits`, Envoy Adaptive Concurrency filter, TCP BBR/Vegas. None ship one monolithic algorithm; all separate "how big a batch", "how many in flight", "who goes first".

## 3. Architectural principles

1. **Three concerns, three components.** Batching, concurrency, scheduling are independent policies with independent signals and independent knobs. Orchestration is a thin layer on top.
2. **No component does more than one job.** The component that decides "fire now?" does not also decide "how many concurrent fires?". The component that decides "start another batch?" does not also decide "which type?".
3. **Measurement is a feature.** Every fire emits a structured record (batch size, type, oldest-queued-age, round-trip). Records feed the adaptive concurrency controller and the stats log.
4. **Correctness-preserving.** No changes to POP long-poll semantics, `invalidate_request`, reconnect, file-buffer failover, cross-worker UDP wake-up.

## 4. Current state (reference)

File: `lib/queen.hpp`. Relevant pieces:

- `_job_queue` — single cross-thread `std::deque<std::shared_ptr<PendingJob>>` under `_mutex_job_queue`.
- `_db_connections` — fixed-size `DBConnection` slots; `_free_slot_indexes` is event-loop-only.
- `_queue_timer` — `uv_timer_t`, fires every `_queue_interval_ms` (env `SIDECAR_MICRO_BATCH_WAIT_MS`, default 5).
- `_uv_timer_cb` — groups queue by type, per type takes all jobs, fires **one** batch into **one** slot via `_send_jobs_to_slot`. Remaining jobs requeued.
- `_uv_async_cb` — registered but body is a no-op.
- `_process_slot_result` — on poll readable, dispatches callbacks, clears `slot.jobs`, calls `_free_slot`. Does not kick drain.
- `submit()` — appends to `_job_queue`; does not signal.
- `_pop_backoff_tracker` — per-queue map of waiting POPs with `next_check` and `wait_deadline`.
- `invalidate_request()` — scans `_job_queue` under mutex; also removes from backoff tracker.

Event-loop-only state (must stay so): `_free_slot_indexes`, `_db_connections[*].jobs`, `slot.poll_handle`, `_pop_backoff_tracker`.
Cross-thread state: `_job_queue`, `_backoff_signal_queue`, reconnect-queue signals.

## 5. Target architecture

Four layers. Each a single responsibility, each unit-testable without PG.

### 5.1 Layer 1 — Per-type job intake and queues

Replace the single `_job_queue` with one queue per `JobType`. Each is a `std::deque` under its own `uv_mutex_t`.

Responsibilities:
- Thread-safe `push_back(pending)` from HTTP workers.
- Event-loop-side `take_front(n)`.
- `size()` O(1).
- `oldest_age()` — duration since the front job was enqueued; zero when empty. Tracked by updating `front_enqueue_time` on transition-from-empty and on `take_front` if the queue is non-empty afterward.
- `erase_by_request_id(id)` — for `invalidate_request`.

This layer does **not** decide when to fire. It holds jobs and reports state.

### 5.2 Layer 2 — Per-type batch policy (Triton-style)

One instance per `JobType`. Pure function of the per-type queue state; no ownership of state itself. Three parameters:

- `preferred_batch_size` — if queue size ≥ this, fire.
- `max_hold_ms` — if oldest queued job waited this long, fire even if below preferred.
- `max_batch_size` — hard cap per fire.

```cpp
enum class FireDecision { FIRE, HOLD };

struct BatchPolicy {
    size_t preferred_batch_size;
    std::chrono::milliseconds max_hold_ms;
    size_t max_batch_size;

    FireDecision should_fire(size_t queue_size,
                             std::chrono::milliseconds oldest_age) const noexcept;

    size_t batch_size_to_take(size_t queue_size) const noexcept;
};
```

`should_fire` returns `FIRE` iff `queue_size >= preferred_batch_size` OR `oldest_age >= max_hold_ms`. Otherwise `HOLD`. `batch_size_to_take` returns `min(queue_size, max_batch_size)`.

The batch policy knows nothing about slots or concurrency. It answers one question per type.

### 5.3 Layer 3 — Per-type concurrency controller

One instance per `JobType`. Tracks in-flight batches and gates new starts. Abstract base with two implementations; choice via env `QUEEN_CONCURRENCY_MODE ∈ {static, vegas}`. Default `vegas`.

```cpp
class ConcurrencyController {
  public:
    virtual ~ConcurrencyController() = default;
    virtual bool try_acquire() noexcept = 0;
    virtual void release() noexcept = 0;
    virtual void on_completion(const CompletionRecord& r) noexcept = 0;
    virtual uint16_t current_limit() const noexcept = 0;
    virtual uint16_t in_flight() const noexcept = 0;
};
```

**Static** (`StaticLimit`): one integer `limit`, atomic counter `in_flight`. `try_acquire` does a CAS against `limit`. `on_completion` is a no-op. For operators who want predictable behavior or to disable adaptation for debugging.

**Vegas** (`VegasLimit`, the default): per-type, maintains `limit` in `[min_limit, max_limit]`. Initial `limit = min_limit`. Maintains `rtt_min` (windowed minimum of completion RTT, window e.g. 30 s) and `rtt_recent` (EMA of last `N` completions). After each completion:

1. Compute `queue_load = in_flight × (1 − rtt_min / rtt_recent)`. This is the Little's-Law-inspired estimate of how many in-flight batches are queueing behind PG contention.
2. If `queue_load < alpha` (default 3) and `limit < max_limit`: `limit += 1`.
3. If `queue_load > beta` (default 6) and `limit > min_limit`: `limit -= 1`.
4. Otherwise: unchanged.

Update the integer `limit` at most once per second to avoid thrashing. `rtt_min` uses a 30-s sliding minimum (a single fast outlier doesn't pin it low).

When `limit` decreases below `in_flight`, existing batches are not cancelled; future `try_acquire` calls return false until in-flight drains naturally.

The concurrency controller knows nothing about queue contents or batch sizes. It answers one question per type.

### 5.4 Layer 4 — Drain orchestrator

The only component that touches slots, libpq, and libuv handles (other than the public signalling primitives). Entry points are the event sources (§5.5). Each entry point calls `_drain_orchestrator()`.

Pseudocode in §7.4. Loop structure: iterate types in round-robin (offset advances each drain pass for fairness), per type pack batches into free slots as long as policy says FIRE and concurrency says yes. Re-arm safety-net timer at end based on earliest pending max-hold.

### 5.5 Event model

Three event sources, one unified entry point (`_drain_orchestrator()`).

| Event               | Source                                    | Mechanism                                                   |
| ------------------- | ----------------------------------------- | ----------------------------------------------------------- |
| **submit-kick**     | HTTP worker thread, after enqueue         | `uv_async_send(&_queue_signal)` → `_uv_async_cb` → drain    |
| **slot-free-kick**  | event loop, end of `_process_slot_result` | direct call on event-loop thread                            |
| **timer-kick**      | max-hold safety net, or POP `next_check`  | `uv_timer_t` expiry → `_uv_timer_cb` → drain                |

`uv_async_send` coalesces. The drain handles that by re-reading queue state on every entry.

The timer is no longer fixed-interval. It is re-armed at the end of every drain pass to fire at:

```
next_timer_fire = min(
    min_over_types(front_enqueue_time[t] + max_hold_ms[t]),    // max-hold
    earliest_pop_next_check                                     // POP backoff
)
```

with a floor of 5 ms to bound timer wakeup cost. If neither source has a pending deadline, the timer is stopped entirely (no spurious wakeups on idle workers).

## 6. Data structures

### 6.1 Per-type state

```cpp
struct PerTypeState {
    // Layer 1
    std::deque<std::shared_ptr<PendingJob>> queue;
    mutable uv_mutex_t                      queue_mutex;
    std::chrono::steady_clock::time_point   front_enqueue_time;

    // Layer 2
    BatchPolicy policy;

    // Layer 3
    std::unique_ptr<ConcurrencyController> concurrency;

    // Layer metrics
    PerTypeMetrics metrics; // histograms, counters (see §10)
};

std::array<PerTypeState, JobType::_COUNT> _types;
```

### 6.2 Fire and completion records

```cpp
struct FireRecord {
    std::chrono::steady_clock::time_point fire_time;
    JobType                               type;
    uint32_t                              batch_size;
    uint32_t                              queue_size_at_fire;
    std::chrono::milliseconds             oldest_age_at_fire;
    uint16_t                              in_flight_at_fire;
    uint16_t                              concurrency_limit_at_fire;
    uint8_t                               slot_idx;
};

struct CompletionRecord {
    FireRecord                            fired;
    std::chrono::steady_clock::time_point complete_time;
    bool                                  ok;
    int                                   pg_error_code;
};
```

`FireRecord` is stored on the `DBConnection` slot itself (one per in-flight batch). On completion, the orchestrator composes a `CompletionRecord`, hands it to the type's `concurrency->on_completion(...)`, and pushes it to the per-type ring buffer for metrics.

Per-type ring buffer size: 1024. Bounded, event-loop-only, no allocations on the hot path.

### 6.3 Slot state addition

Add one field to `DBConnection`:

```cpp
JobType    current_type = JobType::_SENTINEL;
FireRecord current_fire;                   // set in _fire_batch, read in _process_slot_result
```

## 7. Code organization

```
lib/
  queen.hpp                              # public API facade; Queen class (thin)
  queen/
    pending_job.hpp                      # PendingJob, JobRequest, JobType enum (incl. _SENTINEL, _COUNT)
    per_type_queue.hpp                   # Layer 1
    batch_policy.hpp                     # Layer 2
    concurrency/
      concurrency_controller.hpp         # abstract base
      static_limit.hpp                   # Layer 3 (static)
      vegas_limit.hpp                    # Layer 3 (adaptive, default)
    drain_orchestrator.hpp               # Layer 4
    slot_pool.hpp                        # DBConnection lifecycle, free list
    metrics.hpp                          # FireRecord, CompletionRecord, ring buffers, histograms
  queen_test.cpp                         # unit tests (§11.1)
```

`queen.hpp` re-exports the public API (`Queen`, `PendingJob`, `JobType`, `invalidate_request`, constructor) unchanged so callers (`server/src/routes/*.cpp`, `server/src/acceptor_server.cpp`) are not forced to re-include.

## 8. API and behavior

### 8.1 `submit()`

```cpp
void Queen::submit(JobRequest&& job, std::function<void(std::string)> cb) {
    auto pending = std::make_shared<PendingJob>(PendingJob{std::move(job), std::move(cb)});
    auto& ts = _types[static_cast<size_t>(pending->job.op_type)];

    uv_mutex_lock(&ts.queue_mutex);
    bool was_empty = ts.queue.empty();
    ts.queue.push_back(pending);
    if (was_empty) ts.front_enqueue_time = std::chrono::steady_clock::now();
    uv_mutex_unlock(&ts.queue_mutex);

    uv_async_send(&_queue_signal);
}
```

Differences from today: per-type queues, per-type mutex (finer than current shared mutex), `front_enqueue_time` tracking for max-hold, always signals.

### 8.2 `invalidate_request()`

```cpp
bool Queen::invalidate_request(uint64_t request_id) {
    for (auto& ts : _types) {
        uv_mutex_lock(&ts.queue_mutex);
        for (auto it = ts.queue.begin(); it != ts.queue.end(); ++it) {
            if ((*it)->job.request_id == request_id) {
                ts.queue.erase(it);
                if (ts.queue.empty()) ts.front_enqueue_time = {};
                else if (it == ts.queue.begin()) ts.front_enqueue_time = std::chrono::steady_clock::now();
                uv_mutex_unlock(&ts.queue_mutex);
                return true;
            }
        }
        uv_mutex_unlock(&ts.queue_mutex);
    }
    return _pop_backoff_tracker_remove(request_id);
}
```

Walks N small queues; same O(total) worst case. Maintains `front_enqueue_time` if the erased element was the front.

### 8.3 `_on_slot_freed()`

Single helper called from every path that returns a slot to the free list.

```cpp
void Queen::_on_slot_freed(DBConnection& slot, bool ok, int pg_error_code) noexcept {
    if (slot.current_type != JobType::_SENTINEL) {
        CompletionRecord rec{
            slot.current_fire,
            std::chrono::steady_clock::now(),
            ok,
            pg_error_code
        };
        auto& ts = _types[static_cast<size_t>(slot.current_type)];
        ts.concurrency->release();
        ts.concurrency->on_completion(rec);
        ts.metrics.record_completion(rec);
        slot.current_type = JobType::_SENTINEL;
    }
    _free_slot(slot);
    _drain_orchestrator();   // slot-free kick, sync on event-loop thread
}
```

This is the only place `concurrency->release()` is called. Centralizing it eliminates the counter-drift risk that the previous hotpath plan called out.

Called from:
- `_process_slot_result` on normal completion (`ok=true`).
- `_handle_slot_error` on PG errors (`ok=false`, error code from `PQresultErrorField`).
- `_disconnect_slot` on connection loss (`ok=false`, `pg_error_code=-1`).

### 8.4 `_fire_batch()`

Extracted from today's `_send_jobs_to_slot`. Unchanged logic for JSONB composition, idx renumbering, `PQsendQueryParams`. Added: populate `slot.current_type` and `slot.current_fire` before calling `_start_watching_slot`.

```cpp
bool Queen::_fire_batch(DBConnection& slot, JobType type,
                       std::vector<std::shared_ptr<PendingJob>>&& batch,
                       const FireRecord& fire) noexcept {
    slot.current_type = type;
    slot.current_fire = fire;
    slot.jobs = std::move(batch);
    // ... existing idx-renumbering and PQsendQueryParams path ...
}
```

### 8.5 Drain algorithm

```cpp
void Queen::_drain_orchestrator() noexcept {
    // 1. POP-specific pre-pass: move ready POPs from backoff tracker into the POP queue
    _evaluate_pop_backoff_ready();

    // 2. Main drain loop: round-robin across types, pack into free slots
    auto now = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_timer = now + std::chrono::hours(1);

    size_t start = (_drain_pass_counter++) % JobType::_COUNT;
    for (size_t i = 0; i < JobType::_COUNT; i++) {
        size_t   idx = (start + i) % JobType::_COUNT;
        JobType  t   = static_cast<JobType>(idx);
        auto&    ts  = _types[idx];

        while (true) {
            uv_mutex_lock(&ts.queue_mutex);
            size_t queue_size = ts.queue.size();
            auto   oldest_age = queue_size == 0
                ? std::chrono::milliseconds(0)
                : std::chrono::duration_cast<std::chrono::milliseconds>(now - ts.front_enqueue_time);
            uv_mutex_unlock(&ts.queue_mutex);

            if (queue_size == 0) break;

            if (ts.policy.should_fire(queue_size, oldest_age) == FireDecision::HOLD) {
                auto fire_at = ts.front_enqueue_time + ts.policy.max_hold_ms;
                if (fire_at < next_timer) next_timer = fire_at;
                break;
            }

            if (!ts.concurrency->try_acquire()) break;

            DBConnection* slot = _try_get_free_slot();
            if (!slot) { ts.concurrency->release(); return; }

            size_t take = ts.policy.batch_size_to_take(queue_size);
            auto   batch = _take_front(ts, take);

            FireRecord fire{
                now, t, static_cast<uint32_t>(batch.size()),
                static_cast<uint32_t>(queue_size), oldest_age,
                ts.concurrency->in_flight(),
                ts.concurrency->current_limit(),
                static_cast<uint8_t>(slot->idx)
            };
            _fire_batch(*slot, t, std::move(batch), fire);
            ts.metrics.record_fire(fire);
        }
    }

    // 3. Fold in earliest POP next_check deadline
    next_timer = std::min(next_timer, _earliest_pop_next_check());

    // 4. Re-arm timer
    _rearm_safety_timer(next_timer);
}
```

Complexity per pass: O(N_types × fires_per_pass), where fires_per_pass is bounded by `min(free_slots, sum of max_concurrent)`. In practice ≤ N_slots.

### 8.6 POP long-poll integration

POPs in the `_pop_backoff_tracker` have a `next_check` in the future. At each drain, `_evaluate_pop_backoff_ready()` walks the tracker and moves any whose `next_check <= now` into the POP type queue (treating them exactly like newly-submitted POPs from that point). POPs whose `wait_deadline` expired are sent the empty response directly and removed from the tracker. The main drain loop then treats the POP queue uniformly.

The safety timer's next-fire includes `earliest_pop_next_check` so POPs waiting on `next_check` still wake up promptly without a fixed-interval timer.

## 9. Configuration

### 9.1 Existing variables (unchanged semantics)

| Var                              | Default | Meaning                                                   |
| -------------------------------- | ------: | --------------------------------------------------------- |
| `SIDECAR_POOL_SIZE`              | 200     | Total libqueen slots across workers                       |
| `DB_POOL_SIZE`                   | 20      | AsyncDbPool connections                                   |
| `SIDECAR_MICRO_BATCH_WAIT_MS`    | 5       | Legacy knob. Used as default for any type-specific `MAX_HOLD_MS` that isn't explicitly set. |
| `POP_WAIT_*`                     | …       | Unchanged; POP backoff tracker                            |

### 9.2 New per-type variables

Format: `QUEEN_<TYPE>_<KNOB>`, where `<TYPE>` ∈ {`PUSH`, `POP`, `ACK`, `TRANSACTION`, `RENEW_LEASE`, `CUSTOM`}.

| Knob                      | PUSH | POP  | ACK  | TRANSACTION | RENEW_LEASE | CUSTOM |
| ------------------------- | ---: | ---: | ---: | ----------: | ----------: | -----: |
| `PREFERRED_BATCH_SIZE`    |   50 |   20 |   50 |           1 |          10 |      1 |
| `MAX_HOLD_MS`             |   20 |    5 |   20 |           0 |         100 |      0 |
| `MAX_BATCH_SIZE`          |  500 |  500 |  500 |           1 |         100 |      1 |
| `MAX_CONCURRENT` (static) |    4 |    4 |    4 |           1 |           2 |      1 |

Defaults rationale:
- PUSH / ACK `preferred=50` sits above the §1.6 break-even (~33). `max_hold=20` matches the sweet spot found on S1. `max_concurrent=4` lets S3's 4-core PG be fully fed; on S1's 2-core PG the Vegas controller naturally caps at ~2 and static caps at 4 harmlessly (extra capacity never consumed because no slot contention occurs).
- POP: latency-sensitive; tighter hold and smaller preferred batch.
- TRANSACTION, CUSTOM: atomic units, no fusion, concurrency 1 (serial).
- RENEW_LEASE: modestly batched, longer hold; background work.

### 9.3 Concurrency-controller mode

| Var                               | Default | Meaning                                                   |
| --------------------------------- | ------: | --------------------------------------------------------- |
| `QUEEN_CONCURRENCY_MODE`          | `vegas` | `vegas` or `static`. Applies globally to all types.       |
| `QUEEN_VEGAS_MIN_LIMIT`           | 1       | Vegas lower bound                                         |
| `QUEEN_VEGAS_MAX_LIMIT`           | 16      | Vegas upper bound                                         |
| `QUEEN_VEGAS_ALPHA`               | 3       | "Good" queueing threshold (batches)                       |
| `QUEEN_VEGAS_BETA`                | 6       | "Bad" queueing threshold (batches)                        |
| `QUEEN_VEGAS_RTT_WINDOW_SAMPLES`  | 50      | EMA window over recent completions                        |
| `QUEEN_VEGAS_RTT_MIN_WINDOW_SEC`  | 30      | Sliding-minimum window for `rtt_min`                      |
| `QUEEN_VEGAS_UPDATE_INTERVAL_MS`  | 1000    | Minimum time between `limit` updates                      |

When `QUEEN_CONCURRENCY_MODE=static`, the per-type `MAX_CONCURRENT` is the hard limit. When `vegas`, `MAX_CONCURRENT` is the upper bound Vegas can grow to (tighter than `QUEEN_VEGAS_MAX_LIMIT` if smaller).

### 9.4 Precedence

Per-type env var > `SIDECAR_MICRO_BATCH_WAIT_MS` (for `MAX_HOLD_MS` only) > plan default.

## 10. Observability

Per-worker, sampled once per second and exposed via the existing metrics path.

Per-type counters and gauges:

- `batches_fired_total{type}`, `batch_items_fired_total{type}`
- `batch_items_hist{type}` — p50/p95/p99 from the ring buffer
- `batch_rtt_ms_hist{type}` — p50/p95/p99 of completion RTTs
- `oldest_age_at_fire_ms_hist{type}` — how long the oldest job waited
- `in_flight_gauge{type}`, `concurrency_limit_gauge{type}` (shows Vegas convergence in real time)
- `queue_depth_gauge{type}`
- `completions_ok_total{type}`, `completions_err_total{type}`

Worker-wide:

- `drain_passes_total{trigger}` — trigger ∈ {submit, slot_free, timer}
- `slots_free_gauge`, `slots_total_gauge`
- `safety_timer_rearms_total`

Log line (1 Hz steady-state; replaces today's `EVL:`):

```
[libqueen W0] push(q=12 f=2/4 p99rtt=18ms) pop(q=0 f=0/4) ack(q=3 f=1/2) slots=14/100 drains=340/s
```

`f=X/Y` is `in_flight/limit`. Drains/sec gives immediate visibility into the event rate.

## 11. Testing strategy

### 11.1 Unit tests (no PG)

New file `lib/queen_test.cpp` using the existing test harness style in the repo. Tests per layer:

**BatchPolicy**:
- `should_fire(q=0, age=0)` → HOLD
- `should_fire(q=preferred-1, age=max_hold-1)` → HOLD
- `should_fire(q=preferred, age=0)` → FIRE
- `should_fire(q=1, age=max_hold)` → FIRE
- `batch_size_to_take(q > max_batch)` → max_batch

**StaticLimit**:
- Sequential `try_acquire` up to `limit`; next returns false
- `release` restores capacity
- Concurrent `try_acquire` from threads: total successes ≤ limit, exact

**VegasLimit**:
- Synthetic RTT stream: rtt_min=1ms, rtt_recent=1ms → limit grows to max
- Synthetic RTT stream: rtt_recent >> rtt_min → limit shrinks toward min
- Alternating stream: limit oscillates within hysteresis band
- Update-interval throttle: multiple completions within `update_interval_ms` cause at most one `limit` change

**PerTypeQueue**:
- push / take FIFO order
- `oldest_age` zero on empty, tracks front on non-empty, updates correctly on `take_front`
- `invalidate_request` removes correct job and fixes `front_enqueue_time`

**Drain orchestrator** with stubbed slot pool:
- 100 PUSH + 10 POP + 10 ACK queued at once → one drain pass fires at least one batch of each type (respecting concurrency)
- Round-robin fairness: with perpetual PUSH flood + periodic POP, POP fires within one pass
- Max-hold: single submit, no follow-up → safety timer fires after `max_hold_ms`
- No spurious fires when queues empty: drain returns without firing

**Counter-drift stress** (the critical one):
- 10,000 random fires with random success/error; `in_flight == 0` at end
- 1,000 random disconnect/reconnect cycles mid-batch; `in_flight == 0` when PG is back

### 11.2 Integration tests (libqueen + PG, gtest-style)

Use the existing docker PG from `test-perf/scripts/pg-up.sh`. Run as part of `make check`:

- 1000 PUSH with 100 partitions: all callbacks fire, all messages present in `queen.messages`.
- Long-poll POP: `wait=true`, wait 50 ms, push matching; POP returns within 50 + T_pg ms.
- Cancellation under load: 100 concurrent long-poll POPs, abort 50 mid-poll, verify 50 callbacks never fire.
- Chaos: kill PG mid-batch via `docker kill`, assert slots disconnect + reconnect + pending jobs eventually succeed.

### 11.3 Performance gates (`test-perf/`)

`n=3 × 150 s` per scenario. Archive under `~/queen-perf-archives/` with a descriptive name, and append a new `RESULTS.md` campaign section.

Gate:

| Scenario | pg_ins/s                        | p99 push (ms) | Stddev                  |
| -------- | ------------------------------- | ------------: | ----------------------- |
| S0       | ≥ 6,600                         | ≤ 500         | < 20% of mean           |
| S1       | ≥ 13,000                        | ≤ 300         | **< 10% of mean**       |
| S3       | ≥ 20,000                        | ≤ 500         | < 15% of mean           |

Additional: Vegas convergence test — step-up submit rate 3× and verify `concurrency_limit_gauge{PUSH}` restabilizes within 30 s.

## 12. Correctness invariants

All preserved; one new.

| Invariant                                                                              | Preserved by                                                                                  |
| -------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| libuv handles touched only on event-loop thread                                        | All new code runs in `_drain_orchestrator` (from async/timer cb) or `_process_slot_result`    |
| `_free_slot_indexes`, `_db_connections[*].jobs`, `_pop_backoff_tracker` event-loop-only | Touched only inside orchestrator and `_process_slot_result`                                   |
| Per-type queues thread-safe                                                            | One `uv_mutex_t` per type, held only for O(1) ops                                             |
| POP `wait_deadline` / `next_check` respected                                           | `_evaluate_pop_backoff_ready` pre-pass; safety timer includes earliest `next_check`           |
| `invalidate_request` works                                                             | Walks all per-type queues; also checks `_pop_backoff_tracker`                                 |
| Reconnect path intact                                                                  | `_handle_slot_error` and `_disconnect_slot` both route through `_on_slot_freed`               |
| File-buffer failover on DB error                                                       | Unchanged; in route-layer callbacks                                                           |
| Idx-based result dispatch                                                              | Unchanged; `_fire_batch` renumbers, `_process_slot_result` reads `job_idx_ranges`             |
| UDP cross-worker wake-up                                                               | Unchanged; `_backoff_signal` is separate from `_queue_signal`                                 |
| **NEW**: per-type `in_flight` counter never drifts                                     | Exactly one release per acquire, centralized in `_on_slot_freed`; asserted in tests           |

## 13. Risks and mitigations

| Risk                                                              | Mitigation                                                                                          |
| ----------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| Counter drift between `try_acquire` and `release`                 | Single `_on_slot_freed` path; `in_flight == 0` assertion in tests                                  |
| Submit-kick fires tiny batches at low load                        | `preferred_batch_size` floor; only max-hold fires below-floor batches                               |
| Slot-free-kick collapses batch size under moderate load           | Same floor; the kick is an opportunity to check, not a command to fire                              |
| Per-type queue mutex contention under bursty mixed-type load      | Per-type mutex is finer than today's single `_mutex_job_queue`; if profiling shows contention, further split (per-worker-thread partitions) |
| Vegas oscillation near operating point                            | Per-second update cap; EMA smoothing on `rtt_recent`; hysteresis via alpha/beta gap; static fallback mode |
| Vegas `rtt_min` bias from measurements taken under contention     | Windowed minimum over 30 s picks up naturally-unsaturated samples; documented in the vegas impl     |
| Timer re-arm bug leaving a request stuck                          | Unit test for single-submit-no-follow-up scenario; 5 ms safety floor on re-arm                      |
| ACK advisory-lock contention at `max_concurrent[ACK] > 2`         | Observable via `batch_rtt_ms_hist{ACK}` vs in_flight; lower default if contention seen             |
| Event-loop wakeup cost at high submit rate                        | `uv_async_send` coalesces; profile during integration tests to confirm                              |

## 14. Open questions to resolve during implementation

1. **Measured `(c, m)` per type**: §1.6 cost estimates come from aggregated `pg_stat` counters. Fit `T = c + m·batch_size` per type from actual `FireRecord`/`CompletionRecord` data during early integration testing. Adjust defaults for `PREFERRED_BATCH_SIZE` if the break-even differs from the assumed ~33.
2. **ACK concurrency ceiling**: whether 4 is achievable or advisory-lock contention forces lower. Adjust default based on the first S3 run that shows `batch_rtt_ms_hist{ACK}` growing with in_flight.
3. **Event-loop wakeup cost**: profile one S3 integration run; if `uv_async_send` frequency shows up as hot, coalesce further (e.g., mark a "drain needed" flag and only `uv_async_send` on the 0→1 transition).
4. **POP next_check precision**: ensure the safety timer's inclusion of `earliest_pop_next_check` gives ≤ 10 ms worst-case jitter comparable to today. Unit test the re-arm math explicitly.
5. **Whether to keep legacy `SIDECAR_MICRO_BATCH_WAIT_MS`**: recommend keep as fallback for `MAX_HOLD_MS`; deprecate in docs but don't remove.

## 15. Code locations touched

| File                                                        | Change                                                                    |
| ----------------------------------------------------------- | ------------------------------------------------------------------------- |
| `lib/queen.hpp`                                             | Facade only: public API, ctor, `submit`, `invalidate_request`, reconnect machinery |
| `lib/queen/pending_job.hpp`                                 | New: extract `PendingJob`, `JobRequest`, `JobType` (with `_SENTINEL`, `_COUNT`)  |
| `lib/queen/per_type_queue.hpp`                              | New: Layer 1                                                              |
| `lib/queen/batch_policy.hpp`                                | New: Layer 2                                                              |
| `lib/queen/concurrency/concurrency_controller.hpp`          | New: Layer 3 abstract base                                                |
| `lib/queen/concurrency/static_limit.hpp`                    | New: Layer 3 static impl                                                  |
| `lib/queen/concurrency/vegas_limit.hpp`                     | New: Layer 3 Vegas impl (default)                                         |
| `lib/queen/drain_orchestrator.hpp`                          | New: Layer 4                                                              |
| `lib/queen/slot_pool.hpp`                                   | New: DBConnection lifecycle, free list (extracted from `queen.hpp`)      |
| `lib/queen/metrics.hpp`                                     | New: FireRecord, CompletionRecord, ring buffers, per-type histograms     |
| `lib/queen_test.cpp`                                        | New: unit tests per layer                                                 |
| `server/src/acceptor_server.cpp`                            | Read new per-type and Vegas env vars; pass to Queen ctor                  |
| `server/include/queen/config.hpp`                           | Add config fields for new env vars                                        |
| `server/ENV_VARIABLES.md`                                   | Document new env vars                                                     |
| `test-perf/RESULTS.md`                                      | Append new campaign section after perf gate run                           |

## 16. References

- TCP Vegas: L. Brakmo, L. Peterson, IEEE JSAC, 1995.
- BBR: N. Cardwell et al., ACM Queue 14(5), 2016.
- Little's Law: J. D. C. Little, Operations Research, 1961.
- Netflix `concurrency-limits`: https://github.com/Netflix/concurrency-limits
- Envoy Adaptive Concurrency: https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/adaptive_concurrency_filter
- Triton Dynamic Batcher: https://github.com/triton-inference-server/server/blob/main/docs/user_guide/batcher.md
- Clipper: D. Crankshaw et al., NSDI 2017.
- PostgreSQL group commit: https://www.postgresql.org/docs/current/runtime-config-wal.html
- Internal: `test-perf/RESULTS.md` §1.4, §1.6, §2, §2.1.
- Superseded: `LIBQUEEN_HOTPATH_IMPROVEMENT.md`.
