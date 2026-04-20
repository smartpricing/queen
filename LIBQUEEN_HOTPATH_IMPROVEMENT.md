# libqueen Hot-Path Improvement Plan — Nagle-style drain with multi-slot parallelism and bounded backpressure

## 1. Summary

The per-worker libqueen event loop is responsible for draining the job queue, fusing same-type requests into micro-batches, dispatching them to PostgreSQL via non-blocking libpq, and returning results. Today:

- A 10 ms `uv_timer_t` is the only drain trigger.
- The `uv_async_t` signal handler `_uv_async_cb` is a no-op (a `// TODO` in the code).
- Each drain fires at most one batch per job type, to a single slot. Overflow requeues the whole batch.
- A freed slot is not proactively used; it waits for the next timer tick.
- No queue-depth bound exists, so DB stalls can cause unbounded memory growth.

This plan introduces a Nagle-style hybrid drain with multi-slot parallelism and bounded backpressure. Targets:

- **Idle latency floor** drops from ~5 ms to ~0 ms.
- **Load-adaptive batching**: batch size grows with DB round-trip time, shrinks at low load.
- **Multi-slot parallelism** within a single job type under load, with a per-type concurrency cap to avoid starvation of other types.
- **Bounded queue depth** with fail-fast rejection beyond a threshold.
- **Preserved correctness** of long-poll POP, deadlock safety, idx-based result dispatch, cancellation via `invalidate_request`, reconnect, file-buffer failover.

## 2. Current state (reference)

File: `lib/queen.hpp`.

Relevant components:

- `_job_queue` — mutex-protected `std::deque<std::shared_ptr<PendingJob>>`.
- `_free_slot_indexes` — event-loop-thread-only free list of DB connection indices.
- `_db_connections` — fixed-size vector of `DBConnection` slots.
- `_queue_timer` — fires every `_queue_interval_ms` (default 10 ms), runs `_uv_timer_cb` which calls `_group_jobs_by_type` and `_send_jobs_to_slot`.
- `_queue_signal` (`uv_async_t`) — initialized but its callback `_uv_async_cb` does nothing.
- `_backoff_signal` (`uv_async_t`) — separate, wakes backoff-tracked POPs on UDP cross-worker push notification.
- `_pop_backoff_tracker` — per-queue map of waiting POP requests with `next_check` and `wait_deadline`.
- `_process_slot_result` — runs on `uv_poll_t` readable events, dispatches per-job callbacks, clears `slot.jobs`, calls `_free_slot(slot)`. Does **not** kick a drain.
- `submit(JobRequest&&, callback)` — appends to `_job_queue` under mutex. Does **not** signal.

Operational invariants that must be preserved:

1. All libuv handles are only touched on the event-loop thread, except `_queue_signal`/`_backoff_signal`/`_reconnect_signal` via `uv_async_send`, which is thread-safe.
2. `_free_slot_indexes`, `_db_connections[*].jobs`, `slot.poll_handle`, `_pop_backoff_tracker` are event-loop-thread-only.
3. `_job_queue`, `_backoff_signal_queue` are cross-thread (guarded by their mutexes).
4. `invalidate_request(request_id)` must be able to remove a queued job so a canceled long-poll never receives a late response.
5. POP long-poll semantics: a POP with `wait=true` has a `wait_deadline` (absolute) and `next_check` (next eligible time). It may be re-queued many times before `wait_deadline` expires.

## 3. Target behavior

### 3.1 Drain triggers

- `submit()` kicks `uv_async_send(&_queue_signal)` after enqueueing.
- `_process_slot_result` kicks `uv_async_send(&_queue_signal)` after freeing the slot.
- `_queue_timer` remains as a **safety-net**: it handles POP `next_check` re-evaluation and defensively retries any leftover work. Its interval can be raised if desired; 10 ms is fine.
- `_backoff_signal` is unchanged (UDP cross-worker notification).
- `_reconnect_signal` is unchanged.

### 3.2 Drain algorithm

```
_drain():
    # 0. Dispatch queued rejections (see §3.4)
    drain _rejected_jobs (event-loop-only, sync callback invocations are safe here)

    # 1. Snapshot _job_queue under mutex
    lock _mutex_job_queue
        batch = std::move(_job_queue)
    unlock

    if batch.empty() and all _concurrent_by_type == 0:
        return

    # 2. Group by type
    grouped = group_by_type(batch)
    to_requeue = []

    # 3. Per-type dispatch
    for (type, jobs) in grouped:
        if type == POP:
            # Split by timing
            (ready, not_ready_keep, expired) = partition_pop_by_timing(jobs)
            for j in expired:
                _send_empty_response(j)
            to_requeue.extend(not_ready_keep)
            jobs = ready

        if type == CUSTOM:
            # One-at-a-time; no fusion
            for j in jobs:
                if _has_free_slot() and _concurrent_by_type[CUSTOM] < _max_concurrent_by_type[CUSTOM]:
                    _send_custom_job_to_slot(j)  # unchanged internals
                    _concurrent_by_type[CUSTOM] += 1
                else:
                    to_requeue.push_back(j)
            continue

        # 4. Pack items into multi-slot batches
        while !jobs.empty() and _has_free_slot() and _concurrent_by_type[type] < _max_concurrent_by_type[type]:
            sub_batch = take_until_item_cap(jobs, _max_items_per_batch)
            ok = _fire_batch(type, sub_batch, to_requeue)
            if ok:
                _concurrent_by_type[type] += 1

        # Anything left (no more slots, or type cap hit)
        to_requeue.insert(to_requeue.end(), jobs.begin(), jobs.end())

    # 5. Requeue leftovers at the FRONT to preserve ordering bias
    if !to_requeue.empty():
        lock _mutex_job_queue
            for j in reverse(to_requeue):
                _job_queue.push_front(j)
        unlock
        # Do NOT re-kick async here — the next slot-free or timer tick will pick up
```

The key invariants:

- A **single PendingJob is the atom**. We never split one PendingJob across two slots.
- A batch consists of 1+ PendingJobs of the same type, with total `item_count` ≤ `_max_items_per_batch`. An oversized single PendingJob (item_count > cap) fires alone.
- Per-type concurrency cap prevents one job type from starving others when the pool is small.
- Item cap prevents one batch from monopolizing a slot for too long, bounding tail latency during bursts.

### 3.3 `take_until_item_cap`

```cpp
std::vector<std::shared_ptr<PendingJob>>
take_until_item_cap(std::deque<std::shared_ptr<PendingJob>>& jobs, size_t cap) {
    std::vector<std::shared_ptr<PendingJob>> taken;
    size_t total = 0;
    while (!jobs.empty()) {
        auto& front = jobs.front();
        size_t count = std::max<size_t>(front->job.item_count, 1);
        if (taken.empty() || total + count <= cap) {
            taken.push_back(std::move(front));
            total += count;
            jobs.pop_front();
        } else {
            break;
        }
    }
    return taken;
}
```

Rationale:

- `std::max<size_t>(item_count, 1)` because many single-op jobs leave `item_count` at zero. Treat them as 1 item for the cap purposes.
- `taken.empty() || total + count <= cap` ensures we always take at least one job, even if it alone exceeds the cap. Alternative: split huge jobs into multiple SQL calls — rejected because it breaks the "one PendingJob atomic" invariant and complicates result dispatch.

### 3.4 Backpressure

Two mechanisms:

**(a) Bounded queue depth**

- At `submit()`, if `_job_queue.size() >= _max_queue_depth`, enqueue into a secondary `_rejected_jobs` list (mutex-protected) and kick `_queue_signal`.
- `_drain()` dispatches rejections on the event-loop thread via `job->callback(R"({"success":false,"error":"queue_full","overloaded":true})")` before processing anything else.
- Status code mapping happens in the caller (e.g. `push.cpp` callback), which already differentiates DB errors from success. Add a new branch for `overloaded=true` → HTTP 503.

**(b) Per-batch item cap**

- `_max_items_per_batch` (default 1000). See §3.3 above.

### 3.5 POP long-poll interaction

A POP job with `wait_deadline` and `next_check` may be in one of four states at drain time:

| `next_check <= now` | `wait_deadline > now` | Action |
|---|---|---|
| true | true | Dispatch (ready) |
| false | true | Requeue; wait for next trigger |
| true | false | Send empty response (deadline expired) |
| false | false | Send empty response |

With `uv_async_send` in `submit()` and in `_process_slot_result`, a requeued POP will be drained again whenever:

- A new request of any type arrives (async kick), or
- A slot frees, or
- The 10 ms safety-net timer fires.

If only long-poll POPs are queued and nothing else happens, the 10 ms timer is what picks them up. The 10 ms worst-case jitter on `next_check` enforcement is identical to today. Acceptable.

Optimization for a later iteration: instead of fixed 10 ms, re-arm the timer to fire at `min(next_check)` across queued POPs. Skip for v1.

## 4. Concrete code changes in `lib/queen.hpp`

### 4.1 New member state

```cpp
// Per-type concurrency accounting. Event-loop thread only.
std::unordered_map<JobType, uint16_t> _concurrent_by_type;
std::unordered_map<JobType, uint16_t> _max_concurrent_by_type;

// Drain tuning. Event-loop thread only (set at ctor).
size_t _max_items_per_batch = 1000;
size_t _max_queue_depth = 10000;

// Rejection queue for overloaded submits. Cross-thread.
uv_mutex_t _mutex_rejected;
std::deque<std::shared_ptr<PendingJob>> _rejected_jobs;

// Sentinel for slot.current_type (see §4.5)
// JobType::CUSTOM is fine as sentinel since we count it separately.
```

### 4.2 Ctor additions

Initialize `_max_concurrent_by_type` defaults:

```cpp
// Reasonable defaults; expose via ctor args later.
_max_concurrent_by_type[JobType::PUSH]         = _db_connection_count;
_max_concurrent_by_type[JobType::POP]          = _db_connection_count;
_max_concurrent_by_type[JobType::ACK]          = _db_connection_count;
_max_concurrent_by_type[JobType::TRANSACTION]  = 1;  // keep serial
_max_concurrent_by_type[JobType::RENEW_LEASE]  = 2;
_max_concurrent_by_type[JobType::CUSTOM]       = 1;

uv_mutex_init(&_mutex_rejected);
```

Dtor cleanup:

```cpp
uv_mutex_destroy(&_mutex_rejected);
```

Add optional ctor arguments (with defaults) so tuning is possible without API break:

```cpp
Queen(const std::string& conn_str,
      uint16_t statement_timeout_ms = 30000,
      uint16_t db_connection_count = 10,
      uint16_t queue_interval_ms = 10,
      uint16_t pop_wait_initial_interval_ms = 100,
      uint16_t pop_wait_backoff_threshold = 3,
      double   pop_wait_backoff_multiplier = 3.0,
      uint16_t pop_wait_max_interval_ms = 5000,
      uint16_t worker_id = 0,
      const std::string& hostname = "localhost",
      size_t   max_items_per_batch = 1000,
      size_t   max_queue_depth = 10000);
```

### 4.3 `submit()`

```cpp
void
submit(JobRequest&& job, std::function<void(std::string result)> cb) {
    auto pending = std::make_shared<PendingJob>(PendingJob{std::move(job), std::move(cb)});
    bool reject = false;

    uv_mutex_lock(&_mutex_job_queue);
    if (_max_queue_depth > 0 && _job_queue.size() >= _max_queue_depth) {
        reject = true;
    } else {
        _job_queue.push_back(pending);
    }
    uv_mutex_unlock(&_mutex_job_queue);

    if (reject) {
        uv_mutex_lock(&_mutex_rejected);
        _rejected_jobs.push_back(pending);
        uv_mutex_unlock(&_mutex_rejected);
    }

    uv_async_send(&_queue_signal);
}
```

Rejection is async: the callback runs on the event-loop thread in `_drain()`. This preserves the current thread-safety contract of callbacks.

### 4.4 `_uv_async_cb` — new drain implementation

```cpp
static void
_uv_async_cb(uv_async_t* handle) noexcept {
    auto* self = static_cast<Queen*>(uv_handle_get_data((uv_handle_t*)handle));
    self->_drain();
}
```

### 4.5 `_uv_timer_cb` — becomes safety-net

```cpp
static void
_uv_timer_cb(uv_timer_t* handle) noexcept {
    auto* self = static_cast<Queen*>(uv_handle_get_data((uv_handle_t*)handle));

    // Existing lag bookkeeping unchanged
    auto now = std::chrono::steady_clock::now();
    uint64_t lag = std::chrono::duration_cast<std::chrono::milliseconds>(now - self->_last_timer_expected).count();
    self->_event_loop_lag = lag;
    self->_last_timer_expected = now;
    if (lag > 100) {
        spdlog::warn("[Worker {}] [libqueen] Event loop lag: {}ms (expected {}ms)",
                     self->_worker_id, lag, self->_queue_interval_ms);
    }

    self->_drain();
}
```

The timer body moves into a shared `_drain()`. The bodies of the old `_uv_timer_cb` (grouping, POP partitioning, `_send_jobs_to_slot` calls, `_requeue_jobs`) are all captured in `_drain()`.

### 4.6 `_drain()` (new)

```cpp
void
_drain() noexcept {
    // 0. Dispatch rejections
    {
        std::deque<std::shared_ptr<PendingJob>> rejected;
        uv_mutex_lock(&_mutex_rejected);
        rejected.swap(_rejected_jobs);
        uv_mutex_unlock(&_mutex_rejected);
        for (auto& j : rejected) {
            // Match shape of other error responses
            j->callback(R"({"success":false,"error":"queue_full","overloaded":true})");
            _jobs_done++;
        }
    }

    // 1. Snapshot
    std::deque<std::shared_ptr<PendingJob>> snapshot;
    uv_mutex_lock(&_mutex_job_queue);
    snapshot.swap(_job_queue);
    uv_mutex_unlock(&_mutex_job_queue);

    if (snapshot.empty()) return;

    // 2. Group
    std::map<JobType, std::deque<std::shared_ptr<PendingJob>>> grouped;
    for (auto& j : snapshot) grouped[j->job.op_type].push_back(std::move(j));

    std::vector<std::shared_ptr<PendingJob>> to_requeue;

    // 3. Per-type
    for (auto& [type, jobs] : grouped) {
        if (type == JobType::POP) {
            std::vector<std::shared_ptr<PendingJob>> ready;
            auto now = std::chrono::steady_clock::now();
            std::deque<std::shared_ptr<PendingJob>> keep;
            for (auto& j : jobs) {
                bool has_wait = (j->job.wait_deadline != std::chrono::steady_clock::time_point{});
                if (!has_wait) {
                    ready.push_back(j);
                } else if (j->job.wait_deadline <= now) {
                    _send_empty_response(j);
                } else if (j->job.next_check <= now) {
                    ready.push_back(j);
                } else {
                    keep.push_back(j);
                }
            }
            jobs = std::move(keep);
            // Now drain `ready` as if it were the sole group
            while (!ready.empty() && _has_free_slot()
                   && _concurrent_by_type[type] < _max_concurrent_by_type[type]) {
                auto sub = _take_until_item_cap(ready, _max_items_per_batch);
                if (_fire_batch(type, sub)) {
                    _concurrent_by_type[type]++;
                } else {
                    to_requeue.insert(to_requeue.end(), sub.begin(), sub.end());
                }
            }
            to_requeue.insert(to_requeue.end(), ready.begin(), ready.end());
            to_requeue.insert(to_requeue.end(), jobs.begin(), jobs.end());
            continue;
        }

        if (type == JobType::CUSTOM) {
            while (!jobs.empty() && _has_free_slot()
                   && _concurrent_by_type[type] < _max_concurrent_by_type[type]) {
                auto j = jobs.front(); jobs.pop_front();
                // _send_custom_job_to_slot already handles its own failure via jobs_to_requeue
                std::vector<std::shared_ptr<PendingJob>> requeue_out;
                _send_custom_job_to_slot(j, requeue_out);
                if (requeue_out.empty()) {
                    _concurrent_by_type[type]++;
                } else {
                    to_requeue.insert(to_requeue.end(), requeue_out.begin(), requeue_out.end());
                }
            }
            to_requeue.insert(to_requeue.end(), jobs.begin(), jobs.end());
            continue;
        }

        while (!jobs.empty() && _has_free_slot()
               && _concurrent_by_type[type] < _max_concurrent_by_type[type]) {
            auto sub = _take_until_item_cap(jobs, _max_items_per_batch);
            if (_fire_batch(type, sub)) {
                _concurrent_by_type[type]++;
            } else {
                to_requeue.insert(to_requeue.end(), sub.begin(), sub.end());
            }
        }
        to_requeue.insert(to_requeue.end(), jobs.begin(), jobs.end());
    }

    // 4. Requeue leftovers at the FRONT
    if (!to_requeue.empty()) {
        uv_mutex_lock(&_mutex_job_queue);
        for (auto it = to_requeue.rbegin(); it != to_requeue.rend(); ++it) {
            _job_queue.push_front(*it);
        }
        uv_mutex_unlock(&_mutex_job_queue);
    }
}
```

Note: `take_until_item_cap` here operates on `std::deque` and a `std::vector<...>` form — pick one consistently; the prototype in §3.3 uses `std::deque` for O(1) pop-front.

### 4.7 `_fire_batch` (extracted from `_send_jobs_to_slot`)

```cpp
bool
_fire_batch(JobType type, std::vector<std::shared_ptr<PendingJob>>& jobs) noexcept {
    if (!_has_free_slot()) return false;  // defensive
    DBConnection& slot = _get_free_slot();

    // Build combined JSONB + idx ranges (unchanged logic from current _send_jobs_to_slot)
    nlohmann::json combined = nlohmann::json::array();
    std::vector<std::pair<int,int>> idx_ranges;
    int sequential_idx = 0;
    for (const auto& pending : jobs) {
        int start = sequential_idx;
        int count = 0;
        if (!pending->job.params.empty()) {
            auto arr = nlohmann::json::parse(pending->job.params[0]);
            for (auto& item : arr) {
                item["idx"] = sequential_idx;
                item["index"] = sequential_idx;
                sequential_idx++;
                combined.push_back(std::move(item));
                count++;
            }
        }
        idx_ranges.push_back({start, count});
    }

    std::string merged = combined.dump();
    const char* param_ptrs[] = { merged.c_str() };
    auto sql = JobTypeToSql.at(type);

    // Assign BEFORE PQsendQueryParams so _process_slot_result sees them
    slot.jobs.assign(jobs.begin(), jobs.end());
    slot.job_idx_ranges = std::move(idx_ranges);
    slot.current_type = type;   // new field on DBConnection

    int sent = PQsendQueryParams(slot.conn, sql.c_str(), 1, nullptr, param_ptrs, nullptr, nullptr, 0);
    if (!sent) {
        spdlog::error("[Worker {}] [libqueen] Failed to send: {}",
                      _worker_id, PQerrorMessage(slot.conn));
        slot.jobs.clear();
        slot.job_idx_ranges.clear();
        if (PQstatus(slot.conn) != CONNECTION_OK) {
            _disconnect_slot(slot);
        } else {
            _free_slot(slot);
        }
        return false;  // caller handles requeue
    }

    _start_watching_slot(slot, UV_WRITABLE | UV_READABLE);
    return true;
}
```

New field on `DBConnection`:

```cpp
JobType current_type = JobType::CUSTOM;  // sentinel; set in _fire_batch, cleared in _process_slot_result
```

### 4.8 `_process_slot_result` — kick drain on slot-free

At the point where the current code calls `_free_slot(slot)`:

```cpp
// existing:
// uv_poll_stop(&slot.poll_handle);
// _free_slot(slot);

JobType freed_type = slot.current_type;
uv_poll_stop(&slot.poll_handle);
_free_slot(slot);

if (_concurrent_by_type[freed_type] > 0) {
    _concurrent_by_type[freed_type]--;
}
slot.current_type = JobType::CUSTOM; // reset sentinel

uv_async_send(&_queue_signal);
```

Same thing at the error path that also frees the slot.

### 4.9 `invalidate_request`

No change required. The method scans `_job_queue` under mutex; in-flight jobs (on a slot) are not in the queue and cannot be canceled mid-flight (same as today).

### 4.10 `_send_custom_job_to_slot`

Keep as-is but make `_send_custom_job_to_slot` not call `_free_slot` on the "no free slot" path — that path is now prevented by the caller check. The existing failure-to-send path that calls `_free_slot` on post-PQsendQueryParams failure is unchanged.

### 4.11 Ordering and starvation

- Requeuing to the front preserves bias for older jobs, preventing starvation when the queue is always full.
- Per-type cap ensures no type monopolizes all slots. Default cap == `_db_connection_count`, so no actual cap unless explicitly configured lower. If operators observe starvation in practice, lower the caps.

## 5. Route-layer changes for 503

### 5.1 `server/src/routes/push.cpp`

The submit callback parses `result`. Add a branch for `overloaded=true`:

```cpp
if (json_response.is_object() && json_response.value("overloaded", false)) {
    worker_response_registries[worker_id]->send_response(
        request_id,
        nlohmann::json{{"error","queue_full"}},
        /* is_error= */ true,
        /* status_code= */ 503);
    return;
}
```

Similar additions to `pop.cpp`, `ack.cpp`, and `transactions.cpp` callbacks.

### 5.2 File-buffer interaction on push

Today, a DB error triggers file-buffer failover. Decision: should `queue_full` (server too busy) also trigger failover?

- Arguments **for** failover: the request is safe to buffer and replay later; the client gets a success response.
- Arguments **against**: the purpose of 503 is to signal "slow down"; buffering masks backpressure and lets memory grow on disk instead of in memory.

Recommended: do NOT failover on `queue_full`. Return 503 and let the client back off. Document this explicitly.

## 6. Correctness invariants revisited

| Invariant | Preserved by |
|---|---|
| libuv handles touched only on event-loop thread | All new code runs inside `_drain()` (async/timer callbacks) or `_process_slot_result` (poll callback). |
| `_free_slot_indexes`, `_concurrent_by_type`, `_pop_backoff_tracker` event-loop-only | Only read/written inside `_drain`, `_fire_batch`, `_process_slot_result`, `_set_next_backoff_time`. |
| `_job_queue` thread-safety | All accesses under `_mutex_job_queue`. |
| `_rejected_jobs` thread-safety | All accesses under `_mutex_rejected`. |
| POP `next_check`/`wait_deadline` respected | Explicit branching in §4.6. |
| `invalidate_request` still works | Unchanged; operates on `_job_queue` and `_pop_backoff_tracker`. |
| `_process_slot_result` error handling | Unchanged; disconnect/requeue path intact. |
| Reconnect thread interaction | Unchanged; uses `_reconnect_signal`. |
| File-buffer failover on DB error | Unchanged; triggered in route-layer callbacks. |
| Idx-based result dispatch | Unchanged; `_fire_batch` still renumbers and `_process_slot_result` still uses `job_idx_ranges`. |
| UDP cross-worker wake-up | Unchanged; `_backoff_signal` is separate. |

### 6.1 One new invariant to verify: `_concurrent_by_type` never drifts

Concern: if `_fire_batch` returns true but the query eventually errors out, or if `_disconnect_slot` is called in an edge case without decrementing, the counter could monotonically grow and block drains.

Mitigation:

1. `_fire_batch` only returns true AFTER `PQsendQueryParams` returns non-zero and `_start_watching_slot` is called. Increment is in the caller, tied to that true.
2. `_process_slot_result` decrements on **every** code path that calls `_free_slot(slot)` or `_disconnect_slot(slot)` on that slot. Trace these carefully during code review.
3. `_handle_slot_error` — which calls `_disconnect_slot` and `_requeue_jobs(slot.jobs)` — must also decrement `_concurrent_by_type` for the type of the jobs being unwound.

Add a stats log every N seconds printing `_concurrent_by_type` so drift becomes observable.

### 6.2 Reconnect path

When a slot is disconnected, its `current_type` counter must be decremented. Today, `_handle_slot_error` calls `_disconnect_slot` and `_requeue_jobs`. Add `_concurrent_by_type` decrement at the entry of `_handle_slot_error`, reading the type from `slot.current_type`, and reset `slot.current_type`.

After the reconnect thread re-establishes the connection, `_finalize_reconnected_slots` adds the slot back to `_free_slot_indexes`. No counter change needed because the slot has no jobs in flight.

## 7. Configuration and flagging

Environment variables (documented in `server/ENV_VARIABLES.md`):

- `QUEEN_HOTPATH_MODE=legacy|hybrid` (default `legacy` for rollout phase 2; flip to `hybrid` in phase 3).
- `QUEEN_MAX_ITEMS_PER_BATCH` (default 1000).
- `QUEEN_MAX_QUEUE_DEPTH` (default 10000, 0 = unbounded).
- `QUEEN_MAX_CONCURRENT_PUSH`, `_POP`, `_ACK` (default = `QUEEN_DB_CONNECTIONS`).

The `legacy` mode preserves the current `_uv_timer_cb` body and no-op `_uv_async_cb`. Flag-gating is a small wrapper at the head of `_drain()` that returns immediately if `legacy` is selected — no, better: flag-gating is compile-time absent and runtime chosen via ctor arguments.

Actual mechanism: Queen is constructed per worker in `acceptor_server.cpp`. Read env vars there, pass into ctor. Add a boolean `_hybrid_drain` member; `_uv_async_cb` is a no-op if false, `_uv_timer_cb` uses either old body or calls `_drain()` depending on flag.

## 8. Observability additions

Per-worker counters (exposed via existing metrics infrastructure or spdlog):

- `drain_kicks_total` — how many times `_drain()` was invoked.
- `batches_fired_total{type}`.
- `batch_items_histogram{type}` — p50/p95/p99 of batch item count.
- `concurrent_by_type_gauge{type}` — current value.
- `queue_depth_gauge` — current size of `_job_queue`.
- `rejections_total` — count of `queue_full` responses.
- `slot_utilization_gauge` — `_db_connection_count - _free_slot_indexes.size()`.

Log every N seconds when any counter is non-zero. Reuse `_uv_stats_timer_cb` infrastructure.

## 9. Rollout plan

### Phase 1 — implementation behind flag

- Add all new code.
- Ctor default `_hybrid_drain = false`.
- CI runs tests in both modes (matrix).
- Staging deploy with flag off. Smoke test.

### Phase 2 — canary

- Flip one worker to hybrid mode. Observe 24 h.
- Compare metrics (p50/p99 latencies, throughput, counters above) to legacy workers on the same process.

### Phase 3 — flip default

- Default `hybrid` on. Env var remains for kill-switch.

### Phase 4 — clean up

- After 1 month of stability, remove legacy code paths and the flag.

## 10. Test plan

### 10.1 Unit / structural

- `_take_until_item_cap` unit tests (add a gtest or similar in `lib/test_suite.cpp`):
  - empty input,
  - one job under cap,
  - one job over cap (alone),
  - many small jobs under cap (all taken),
  - many small jobs crossing cap (partial take),
  - mix of zero `item_count` jobs counted as 1 each.

### 10.2 Idle latency

- Single push on a cold worker; measure end-to-end latency. Legacy ≈ 5 ms floor; hybrid target < 1 ms + DB round-trip.

### 10.3 Long-poll latency

- Issue a POP with `wait=true`, wait 50 ms, push a matching message. Measure POP return time. Target: ≈ 50 ms + DB round-trip; no 10 ms jitter.

### 10.4 Throughput

- Microbench push at max rate, measure p50/p99 and aggregate throughput. Compare legacy vs hybrid.
- Same for ack (especially combined with the ACK rewrite plan).

### 10.5 Saturation

- Drive 10× DB throughput for 60 s. Verify:
  - No OOM.
  - Queue depth stabilizes at or below `_max_queue_depth`.
  - Excess requests get 503.
  - After load drop, throughput recovers to full.

### 10.6 Multi-slot utilization

- During a sustained burst, capture `pg_stat_activity` and confirm multiple concurrent `push_messages_v2` queries from the same worker. Record `slot_utilization_gauge`.

### 10.7 Per-type starvation

- Issue a continuous push flood. Concurrently issue one ACK. Verify the ACK runs within a few DB round-trips (not starved). Adjust caps if needed.

### 10.8 Cancellation under load

- Issue 100 long-poll POPs with `wait=true`, abort them mid-poll. Verify `invalidate_request` is called and no stale responses arrive after the connection abort.

### 10.9 Chaos

- Kill the PG primary mid-batch while hybrid mode is active. Verify:
  - Slots disconnect, `_concurrent_by_type` counters decrement correctly.
  - Reconnect thread restores slots.
  - Pending jobs requeue and eventually complete on reconnection.
  - No double-decrement or under-decrement (monitor gauges).

### 10.10 Counter drift test

- Run a long stress test (1 h, random kill of DB connections every 5 min). Verify `_concurrent_by_type` ≤ `_max_concurrent_by_type` at all samples and returns to 0 during idle windows.

## 11. Risks

### 11.1 Counter-drift bugs

Decrementing `_concurrent_by_type` in every slot-free code path is the #1 hazard. Mitigation: concentrate the decrement in a single helper `_on_slot_freed(slot)` called from all paths that free a slot.

```cpp
void _on_slot_freed(DBConnection& slot) noexcept {
    JobType t = slot.current_type;
    if (_concurrent_by_type[t] > 0) _concurrent_by_type[t]--;
    slot.current_type = JobType::CUSTOM;
    _free_slot(slot);
    uv_async_send(&_queue_signal);
}
```

Replace all direct `_free_slot` calls in the event-loop thread with `_on_slot_freed`.

### 11.2 Rejection callback thread-safety

`_rejected_jobs` dispatch runs on the event-loop thread inside `_drain`. The callback typically calls `worker_loop->defer(...)`, which is thread-safe. No new issue.

### 11.3 ACK split-batch advisory lock contention

When a large ACK batch is split across slots, two parallel `ack_messages_v*` calls may both attempt `pg_advisory_xact_lock` for overlapping (partition, CG) groups. This is correct but blocks one slot until the other releases. Under normal load this is negligible. Under pathological contention (single hot partition), it could stall a slot.

Mitigation for a later optimization: when splitting ACK, keep ACKs of the same (partition, CG) in the same sub-batch. Implementation: pre-group the queue elements by (partition, CG) before taking sub-batches. For v1, skip this and measure.

### 11.4 Fairness within a type

Today, drains are ordered by queue arrival within a type. With multi-slot parallelism, the order is preserved (we take from the front). Under requeue, leftovers go to front — stable FIFO. No change.

### 11.5 Volatility under low load

If `_max_queue_depth` is small and load is bursty, transient 503s could happen. Tune default generously (10 000) and expose to operators.

### 11.6 `uv_async_send` coalescing and correctness

libuv explicitly documents that multiple `uv_async_send` calls between callback runs are coalesced into one callback invocation. Our algorithm tolerates this: we always re-check `_job_queue` on every drain. No correctness issue.

## 12. Code locations that change

| File | Change |
|---|---|
| `lib/queen.hpp` | Major: new `_drain()`, new `_fire_batch()`, modified `submit()`, modified `_uv_async_cb`, modified `_uv_timer_cb`, new `_on_slot_freed`, new state, new ctor args, `DBConnection::current_type` field. |
| `server/src/acceptor_server.cpp` | Pass new env-var-driven tuning into Queen ctor. |
| `server/src/routes/push.cpp` | Handle `overloaded=true` → 503. |
| `server/src/routes/pop.cpp` | Same. |
| `server/src/routes/ack.cpp` | Same. |
| `server/src/routes/transactions.cpp` | Same. |
| `server/ENV_VARIABLES.md` | Document new env vars. |
| `lib/test_suite.cpp` | Unit tests for `take_until_item_cap`. |
| `benchmark/benchmark-queen.js` | Scenarios for saturation / idle / multi-slot. |

## 13. Out of scope

- Replacing libpq with a different driver.
- Sharing slots across workers.
- Cross-partition ACK batching (covered in the ACK plan).
- Adaptive algorithm tuning (self-tuning caps based on observed throughput).
- Fine-grained POP re-timer (firing at exact `next_check`).

## 14. Open questions to decide before starting

1. Should `_max_queue_depth = 0` mean "unbounded" (warn at startup) or "closed" (reject everything)? Recommendation: 0 = unbounded; document.
2. Should rejection responses count in `_jobs_done` stats? Recommendation: yes, with a separate `_jobs_rejected` counter so operators can distinguish.
3. Should we add `Retry-After` header on 503? Recommendation: yes; set to a modest value like 100 ms so clients back off briefly.
4. Should TRANSACTION cap stay at 1? Recommendation: yes. Transactions are atomic units that don't benefit from within-worker fan-out.
