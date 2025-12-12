# Event Loop Metrics Collection Plan

## Problem Statement

The dashboard frontend displays throughput, lag, and queue size metrics. Currently these are computed by the `StatsService` which runs expensive SQL procedures that scan the messages table periodically. This is:
1. **Expensive** — Scans large tables every 10s (fast aggregation) and 2min (full reconciliation)
2. **Inaccurate** — Metrics are snapshots, not real-time
3. **Redundant** — The event loop already has all this information locally

## Solution: Event Loop Native Metrics

The libqueen event loop (`_uv_stats_timer_cb`) already logs these metrics every second:
- `event_loop_lag` — Lag of the event loop (health indicator)
- `free_slot_indexes_size` — Number of free DB connection slots
- `db_connections_size` — Total DB connection pool size
- `job_queue_size` — Size of the pending job queue
- `backoff_size` — Size of the backoff queue
- `jobs_done` — Number of jobs completed in the interval
- `jobs_per_second` — Computed throughput

### What to Add

Track operation types in the event loop:
- `push_count` — Number of push operations
- `pop_count` — Number of pop operations  
- `ack_count` — Number of ack operations (including autoAck pops)
- `transaction_count` — Number of transaction operations

Track lag at pop time (per-queue):
```cpp
struct QueueLagStats {
    uint64_t pop_count;       // Number of pops in this window
    uint64_t total_lag_ms;    // Sum of (now - message.created_at) for all pops
    uint64_t max_lag_ms;      // Worst case lag in this window
};
std::unordered_map<std::string, QueueLagStats> _queue_lag_stats;
```

When a pop completes:
```cpp
auto lag_ms = now_ms - message.created_at_ms;
_queue_lag_stats[queue_name].pop_count++;
_queue_lag_stats[queue_name].total_lag_ms += lag_ms;
_queue_lag_stats[queue_name].max_lag_ms = std::max(_queue_lag_stats[queue_name].max_lag_ms, lag_ms);
```

---

## New Table: `queen.worker_metrics`

```sql
CREATE TABLE IF NOT EXISTS queen.worker_metrics (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    
    -- Worker identity
    hostname VARCHAR(255) NOT NULL,
    worker_id INTEGER NOT NULL,
    pid INTEGER NOT NULL,
    
    -- Time bucket (minute granularity)
    bucket_time TIMESTAMPTZ NOT NULL,
    
    -- Event loop health
    avg_event_loop_lag_ms INTEGER DEFAULT 0,
    max_event_loop_lag_ms INTEGER DEFAULT 0,
    
    -- Connection pool
    avg_free_slots INTEGER DEFAULT 0,
    min_free_slots INTEGER DEFAULT 0,
    db_connections INTEGER DEFAULT 0,
    
    -- Job queue pressure
    avg_job_queue_size INTEGER DEFAULT 0,
    max_job_queue_size INTEGER DEFAULT 0,
    backoff_size INTEGER DEFAULT 0,
    
    -- Throughput (absolute counts for this minute)
    jobs_done BIGINT DEFAULT 0,
    push_count BIGINT DEFAULT 0,
    pop_count BIGINT DEFAULT 0,
    ack_count BIGINT DEFAULT 0,
    transaction_count BIGINT DEFAULT 0,
    
    -- Lag metrics (aggregated across all queues for this worker)
    lag_count BIGINT DEFAULT 0,       -- Number of pops measured (for weighted avg)
    avg_lag_ms BIGINT DEFAULT 0,
    max_lag_ms BIGINT DEFAULT 0,
    
    -- Error metrics
    db_error_count BIGINT DEFAULT 0,  -- DB query failures
    dlq_count BIGINT DEFAULT 0,       -- Messages moved to DLQ
    
    -- Timestamp
    created_at TIMESTAMPTZ DEFAULT NOW(),
    
    UNIQUE(hostname, worker_id, pid, bucket_time)
);

CREATE INDEX idx_worker_metrics_bucket ON queen.worker_metrics(bucket_time DESC);
CREATE INDEX idx_worker_metrics_worker ON queen.worker_metrics(hostname, worker_id);
```

### Per-Queue Lag Table (optional, for queue-level dashboard)

```sql
CREATE TABLE IF NOT EXISTS queen.queue_lag_metrics (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    bucket_time TIMESTAMPTZ NOT NULL,
    queue_name VARCHAR(512) NOT NULL,
    
    pop_count BIGINT DEFAULT 0,
    avg_lag_ms BIGINT DEFAULT 0,
    max_lag_ms BIGINT DEFAULT 0,
    
    UNIQUE(bucket_time, queue_name)
);

CREATE INDEX idx_queue_lag_bucket ON queen.queue_lag_metrics(bucket_time DESC);
```

---

## Implementation: libqueen Changes

### 1. New `WorkerMetrics` Class

Create a separate class to handle all metrics collection. This keeps the `Queen` class focused on job processing.

**File: `lib/worker_metrics.hpp`**

```cpp
#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <functional>

namespace queen {

/**
 * WorkerMetrics - Collects and buffers metrics for a single worker.
 * 
 * Thread-safe: can be called from any thread.
 * Buffers metrics in memory and flushes to database at minute boundaries.
 */
class WorkerMetrics {
public:
    // Callback type for flushing metrics to database
    using FlushCallback = std::function<void(const std::string& sql, const std::vector<std::string>& params)>;

    WorkerMetrics(
        const std::string& hostname,
        uint16_t worker_id,
        pid_t pid,
        FlushCallback flush_callback
    );

    // --- Operation counters (called from job handlers) ---
    void record_push() { _push_count++; }
    void record_pop() { _pop_count++; }
    void record_ack() { _ack_count++; }
    void record_transaction() { _transaction_count++; }
    
    // Record pop with lag measurement
    void record_pop_with_lag(const std::string& queue_name, uint64_t lag_ms);
    
    // --- Error counters ---
    void record_db_error() { _db_error_count++; }
    void record_dlq() { _dlq_count++; }
    
    // --- Event loop health (called from stats timer) ---
    void record_stats_sample(
        uint64_t event_loop_lag_ms,
        uint16_t free_slots,
        uint16_t db_connections,
        size_t job_queue_size,
        size_t backoff_size,
        uint64_t jobs_done
    );
    
    // --- Minute boundary check (called from stats timer) ---
    // Returns true if metrics were flushed
    bool check_and_flush();

private:
    // Worker identity
    std::string _hostname;
    uint16_t _worker_id;
    pid_t _pid;
    
    // Flush callback (submits SQL to Queen)
    FlushCallback _flush_callback;
    
    // --- Operation counters (atomic, lock-free) ---
    std::atomic<uint64_t> _push_count{0};
    std::atomic<uint64_t> _pop_count{0};
    std::atomic<uint64_t> _ack_count{0};
    std::atomic<uint64_t> _transaction_count{0};
    
    // --- Error counters ---
    std::atomic<uint64_t> _db_error_count{0};
    std::atomic<uint64_t> _dlq_count{0};
    
    // --- Per-queue lag tracking (no mutex needed - single-threaded event loop) ---
    struct QueueLagStats {
        uint64_t pop_count = 0;
        uint64_t total_lag_ms = 0;
        uint64_t max_lag_ms = 0;
    };
    std::unordered_map<std::string, QueueLagStats> _queue_lag_stats;
    
    // --- Running aggregates for current minute (no mutex needed) ---
    struct MinuteAggregates {
        uint64_t samples = 0;
        
        // Event loop health
        uint64_t sum_event_loop_lag = 0;
        uint64_t max_event_loop_lag = 0;
        
        // Connection pool
        uint64_t sum_free_slots = 0;
        uint64_t min_free_slots = UINT64_MAX;
        uint16_t db_connections = 0;
        
        // Job queue pressure
        uint64_t sum_job_queue_size = 0;
        uint64_t max_job_queue_size = 0;
        uint64_t max_backoff_size = 0;
        
        // Jobs done (accumulated)
        uint64_t jobs_done = 0;
        
        void reset() {
            samples = 0;
            sum_event_loop_lag = 0;
            max_event_loop_lag = 0;
            sum_free_slots = 0;
            min_free_slots = UINT64_MAX;
            sum_job_queue_size = 0;
            max_job_queue_size = 0;
            max_backoff_size = 0;
            jobs_done = 0;
        }
    };
    MinuteAggregates _aggregates;
    
    // Current minute boundary
    std::chrono::system_clock::time_point _current_minute_start;
    
    // --- Internal methods ---
    void flush_to_database();
    std::string build_insert_sql() const;
};

} // namespace queen
```

### 2. Implementation

**File: `lib/worker_metrics.cpp`** (or inline in header)

```cpp
WorkerMetrics::WorkerMetrics(
    const std::string& hostname,
    uint16_t worker_id,
    pid_t pid,
    FlushCallback flush_callback
) : _hostname(hostname),
    _worker_id(worker_id),
    _pid(pid),
    _flush_callback(std::move(flush_callback)),
    _current_minute_start(std::chrono::floor<std::chrono::minutes>(
        std::chrono::system_clock::now()
    ))
{}

void WorkerMetrics::record_pop_with_lag(const std::string& queue_name, uint64_t lag_ms) {
    _pop_count++;
    
    // NOTE: No mutex needed! All operations run on the same event loop thread.
    // The Queen event loop is single-threaded (libuv), so:
    // - record_push/pop/ack are called from job result handlers
    // - record_stats_sample is called from _uv_stats_timer_cb
    // - check_and_flush is called from _uv_stats_timer_cb
    // All on the same thread, no races possible.
    
    auto& stats = _queue_lag_stats[queue_name];
    stats.pop_count++;
    stats.total_lag_ms += lag_ms;
    stats.max_lag_ms = std::max(stats.max_lag_ms, lag_ms);
}

void WorkerMetrics::record_stats_sample(
    uint64_t event_loop_lag_ms,
    uint16_t free_slots,
    uint16_t db_connections,
    size_t job_queue_size,
    size_t backoff_size,
    uint64_t jobs_done
) {
    // No mutex needed - called from event loop thread only
    _aggregates.samples++;
    _aggregates.sum_event_loop_lag += event_loop_lag_ms;
    _aggregates.max_event_loop_lag = std::max(_aggregates.max_event_loop_lag, event_loop_lag_ms);
    _aggregates.sum_free_slots += free_slots;
    _aggregates.min_free_slots = std::min(_aggregates.min_free_slots, (uint64_t)free_slots);
    _aggregates.db_connections = db_connections;
    _aggregates.sum_job_queue_size += job_queue_size;
    _aggregates.max_job_queue_size = std::max(_aggregates.max_job_queue_size, job_queue_size);
    _aggregates.max_backoff_size = std::max(_aggregates.max_backoff_size, backoff_size);
    _aggregates.jobs_done += jobs_done;
}

bool WorkerMetrics::check_and_flush() {
    auto now = std::chrono::system_clock::now();
    auto current_minute = std::chrono::floor<std::chrono::minutes>(now);
    
    if (current_minute <= _current_minute_start) {
        return false;  // Still in same minute
    }
    
    // Crossed minute boundary - flush and reset
    flush_to_database();
    
    // Reset all counters
    _current_minute_start = current_minute;
    _push_count = 0;
    _pop_count = 0;
    _ack_count = 0;
    _transaction_count = 0;
    _db_error_count = 0;
    _dlq_count = 0;
    
    {
        std::lock_guard<std::mutex> lock(_aggregates_mutex);
        _aggregates.reset();
    }
    {
        std::lock_guard<std::mutex> lock(_queue_lag_mutex);
        _queue_lag_stats.clear();
    }
    
    return true;
}

void WorkerMetrics::flush_to_database() {
    // No locks needed - all on event loop thread
    
    // Snapshot current values (atomics for potential external reads)
    uint64_t push = _push_count.load();
    uint64_t pop = _pop_count.load();
    uint64_t ack = _ack_count.load();
    uint64_t txn = _transaction_count.load();
    uint64_t db_errors = _db_error_count.load();
    uint64_t dlq = _dlq_count.load();
    
    // Copy aggregates
    MinuteAggregates agg = _aggregates;
    
    // Compute lag aggregates
    uint64_t total_lag_count = 0;
    uint64_t total_lag_sum = 0;
    uint64_t max_lag = 0;
    for (const auto& [queue, stats] : _queue_lag_stats) {
        total_lag_count += stats.pop_count;
        total_lag_sum += stats.total_lag_ms;
        max_lag = std::max(max_lag, stats.max_lag_ms);
    }
    
    uint64_t avg_lag = total_lag_count > 0 ? total_lag_sum / total_lag_count : 0;
    
    // Build SQL
    std::string sql = R"(
        INSERT INTO queen.worker_metrics (
            hostname, worker_id, pid, bucket_time,
            avg_event_loop_lag_ms, max_event_loop_lag_ms,
            avg_free_slots, min_free_slots, db_connections,
            avg_job_queue_size, max_job_queue_size, backoff_size,
            jobs_done, push_count, pop_count, ack_count, transaction_count,
            lag_count, avg_lag_ms, max_lag_ms,
            db_error_count, dlq_count
        ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22)
        ON CONFLICT (hostname, worker_id, pid, bucket_time) DO UPDATE SET
            jobs_done = queen.worker_metrics.jobs_done + EXCLUDED.jobs_done,
            push_count = queen.worker_metrics.push_count + EXCLUDED.push_count,
            pop_count = queen.worker_metrics.pop_count + EXCLUDED.pop_count,
            ack_count = queen.worker_metrics.ack_count + EXCLUDED.ack_count,
            transaction_count = queen.worker_metrics.transaction_count + EXCLUDED.transaction_count,
            lag_count = queen.worker_metrics.lag_count + EXCLUDED.lag_count,
            max_lag_ms = GREATEST(queen.worker_metrics.max_lag_ms, EXCLUDED.max_lag_ms),
            db_error_count = queen.worker_metrics.db_error_count + EXCLUDED.db_error_count,
            dlq_count = queen.worker_metrics.dlq_count + EXCLUDED.dlq_count
    )";
    
    // Format bucket_time as ISO string
    auto bucket_time = std::chrono::system_clock::to_time_t(_current_minute_start);
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:00Z", std::gmtime(&bucket_time));
    
    std::vector<std::string> params = {
        _hostname,
        std::to_string(_worker_id),
        std::to_string(_pid),
        time_buf,
        std::to_string(agg.samples > 0 ? agg.sum_event_loop_lag / agg.samples : 0),
        std::to_string(agg.max_event_loop_lag),
        std::to_string(agg.samples > 0 ? agg.sum_free_slots / agg.samples : 0),
        std::to_string(agg.min_free_slots == UINT64_MAX ? 0 : agg.min_free_slots),
        std::to_string(agg.db_connections),
        std::to_string(agg.samples > 0 ? agg.sum_job_queue_size / agg.samples : 0),
        std::to_string(agg.max_job_queue_size),
        std::to_string(agg.max_backoff_size),
        std::to_string(agg.jobs_done),
        std::to_string(push),
        std::to_string(pop),
        std::to_string(ack),
        std::to_string(txn),
        std::to_string(total_lag_count),
        std::to_string(avg_lag),
        std::to_string(max_lag),
        std::to_string(db_errors),
        std::to_string(dlq)
    };
    
    // Submit via callback (fire and forget)
    if (_flush_callback) {
        _flush_callback(sql, params);
    }
    
    // Also flush per-queue lag metrics
    flush_queue_lag_metrics();
}

void WorkerMetrics::flush_queue_lag_metrics() {
    // No lock needed - single-threaded event loop
    if (_queue_lag_stats.empty()) return;
    
    // Work with current data (will be cleared in check_and_flush after this)
    
    auto bucket_time = std::chrono::system_clock::to_time_t(_current_minute_start);
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:00Z", std::gmtime(&bucket_time));
    
    // Build SINGLE batched INSERT with UNNEST (one SQL call for all queues)
    // This avoids N separate SQL submissions
    nlohmann::json batch = nlohmann::json::array();
    for (const auto& [queue_name, stats] : _queue_lag_stats) {
        if (stats.pop_count == 0) continue;
        batch.push_back({
            {"queue", queue_name},
            {"pop_count", stats.pop_count},
            {"avg_lag_ms", stats.total_lag_ms / stats.pop_count},
            {"max_lag_ms", stats.max_lag_ms}
        });
    }
    
    if (batch.empty()) return;
    
    // Single SQL with JSON array parameter
    std::string sql = R"(
        INSERT INTO queen.queue_lag_metrics (bucket_time, queue_name, pop_count, avg_lag_ms, max_lag_ms)
        SELECT 
            $1::timestamptz,
            item->>'queue',
            (item->>'pop_count')::bigint,
            (item->>'avg_lag_ms')::bigint,
            (item->>'max_lag_ms')::bigint
        FROM jsonb_array_elements($2::jsonb) AS item
        ON CONFLICT (bucket_time, queue_name) DO UPDATE SET
            pop_count = queen.queue_lag_metrics.pop_count + EXCLUDED.pop_count,
            avg_lag_ms = (queen.queue_lag_metrics.avg_lag_ms * queen.queue_lag_metrics.pop_count 
                        + EXCLUDED.avg_lag_ms * EXCLUDED.pop_count) 
                        / NULLIF(queen.queue_lag_metrics.pop_count + EXCLUDED.pop_count, 0),
            max_lag_ms = GREATEST(queen.queue_lag_metrics.max_lag_ms, EXCLUDED.max_lag_ms)
    )";
    
    if (_flush_callback) {
        _flush_callback(sql, {time_buf, batch.dump()});
    }
}
```

### 3. Integration with Queen Class

In `Queen` class, add a single member and wire up the callbacks:

```cpp
// In queen.hpp private section:
std::unique_ptr<WorkerMetrics> _metrics;

// In Queen constructor:
_metrics = std::make_unique<WorkerMetrics>(
    hostname,
    _worker_id,
    getpid(),
    [this](const std::string& sql, const std::vector<std::string>& params) {
        // Submit as internal job (excluded from metrics)
        JobRequest job;
        job.op_type = JobType::CUSTOM;
        job.sql = sql;
        job.params = params;
        job.internal = true;  // Flag to exclude from counters
        submit(std::move(job), [](std::string) {});
    }
);
```

### 4. Call Sites in Queen

In push handler (line ~952):
```cpp
_metrics->record_push();
```

In pop handler (after successful pop):
```cpp
auto lag_ms = /* compute from message.created_at */;
_metrics->record_pop_with_lag(queue_name, lag_ms);
if (message.auto_ack) {
    _metrics->record_ack();
}
```

In ack handler:
```cpp
_metrics->record_ack();

// Check if result indicates DLQ
if (result.contains("dlq") && result["dlq"].get<bool>()) {
    _metrics->record_dlq();
}
```

In transaction handler:
```cpp
_metrics->record_transaction();

// Check for DLQ in any operation result
for (const auto& op_result : results) {
    if (op_result.contains("dlq") && op_result["dlq"].get<bool>()) {
        _metrics->record_dlq();
    }
}
```

In error handling (line ~967-979):
```cpp
} else {
    // Query failed - send error to all jobs
    std::string error_msg = PQresultErrorMessage(res);
    spdlog::error("[Worker {}] [libqueen] Query failed: {}", _worker_id, error_msg);
    
    // Track DB error
    _metrics->record_db_error();
    
    nlohmann::json error_result = {
        {"success", false},
        {"error", error_msg}
    };
    // ...
}
```

In `_uv_stats_timer_cb`:
```cpp
// Record sample
_metrics->record_stats_sample(
    event_loop_lag,
    free_slot_indexes_size,
    db_connections_size,
    job_queue_size,
    backoff_size,
    _jobs_done
);

// Check minute boundary
_metrics->check_and_flush();

// Reset jobs_done after recording
_jobs_done = 0;
```

---

## Reconciliation Strategy

### On Server Startup

1. **Run full reconciliation** — The existing `refresh_all_stats_v1()` procedure computes accurate counts from the messages table
2. **Mark history gap** — Note the gap between last `worker_metrics` entry and now
3. **Optionally backfill** — For short gaps (<10min), interpolate; for long gaps, leave as-is

### Periodic Reconciliation (Keep, but less frequent)

Reduce `StatsService` reconciliation from 2min to 10min or 30min:
- Still needed to reconcile `pending_messages` count (can't track this locally without scanning)
- Still needed to clean up orphaned stats
- Still needed to maintain `queen.stats` for partition-level breakdown

### Crash Recovery

The `ON CONFLICT ... DO UPDATE` clause handles duplicate writes:
- If server crashes and restarts within the same minute, counts are merged
- If server crashes mid-minute, partial data is written on restart with reconciliation

### Data Loss Tolerance

Accept that ~1 minute of metrics may be lost on hard crash. This is acceptable for dashboard purposes.

---

## Routes/Procedures to Change

### Keep As-Is (still needed)
| Component | Reason |
|-----------|--------|
| `get_queue_detail_v2()` | Per-partition stats from `queen.stats` |
| `get_queues_v2()` | Queue list with partition counts |
| `get_namespaces_v2()` | Namespace aggregates |
| `get_tasks_v2()` | Task aggregates |
| `cleanup_orphaned_stats_v1()` | Cleanup |
| `cleanup_stats_history_v1()` | Cleanup |

### Modify: Dashboard Throughput

| Current | New |
|---------|-----|
| `get_status_v2()` reads from `queen.stats_history` | Read from `queen.worker_metrics` aggregated |
| `get_system_overview_v2()` throughput from delta | Sum from `worker_metrics` |

Create new procedure:
```sql
CREATE OR REPLACE FUNCTION queen.get_throughput_v3(
    p_from TIMESTAMPTZ,
    p_to TIMESTAMPTZ,
    p_bucket_minutes INTEGER DEFAULT 1
)
RETURNS JSONB AS $$
BEGIN
    RETURN (
        SELECT jsonb_agg(
            jsonb_build_object(
                'timestamp', bucket,
                'pushPerSecond', push_count / 60.0,
                'popPerSecond', pop_count / 60.0,
                'ackPerSecond', ack_count / 60.0,
                'avgLagMs', avg_lag_ms,
                'maxLagMs', max_lag_ms
            ) ORDER BY bucket DESC
        )
        FROM (
            SELECT 
                date_trunc('minute', bucket_time) as bucket,
                SUM(push_count) as push_count,
                SUM(pop_count) as pop_count,
                SUM(ack_count) as ack_count,
                AVG(avg_lag_ms) as avg_lag_ms,
                MAX(max_lag_ms) as max_lag_ms
            FROM queen.worker_metrics
            WHERE bucket_time >= p_from AND bucket_time <= p_to
            GROUP BY 1
        ) t
    );
END;
$$ LANGUAGE plpgsql;
```

### Modify: StatsService

| Current | New |
|---------|-----|
| `stats_interval_ms = 10000` | Increase to 30000 or 60000 |
| `reconcile_interval_ms = 120000` | Increase to 300000 or 600000 |
| Computes throughput from message table deltas | Reads from `worker_metrics` |

Keep `run_full_reconciliation()` but call it less frequently (10min instead of 2min).

### Remove or Simplify
| Component | Action |
|-----------|--------|
| `increment_message_counts_v1()` | Remove (no longer needed for throughput) |
| `write_stats_history_v1()` | Simplify — only write queue/system, not per-minute deltas |
| Throughput calculation in `aggregate_*_stats_v1()` | Remove — read from `worker_metrics` |

---

## Migration Path

### Phase 1: Add New Infrastructure
1. Create `queen.worker_metrics` table
2. Add tracking to libqueen event loop
3. Add minute-boundary flush logic
4. Deploy — both old and new systems run in parallel

### Phase 2: Switch Dashboard
1. Create `get_throughput_v3()` procedure
2. Update `get_status_v2()` to use worker_metrics for throughput
3. Verify dashboard shows same data
4. Reduce StatsService frequency

### Phase 3: Cleanup
1. Remove `increment_message_counts_v1()` calls
2. Simplify `write_stats_history_v1()`
3. Consider removing `queen.stats_history` (replaced by `worker_metrics`)

---

## Performance Analysis

**Will this slow down the event loop?** No, and here's why:

### Single-Threaded Design

The Queen event loop runs on a **single libuv thread**. All operations happen sequentially:
- `record_push()` called from push result handler
- `record_pop_with_lag()` called from pop result handler  
- `record_stats_sample()` called from `_uv_stats_timer_cb`
- `check_and_flush()` called from `_uv_stats_timer_cb`

**No mutex contention** — everything runs on the same thread, so no locks needed for internal state.

### Cost Per Operation

| Operation | Cost | When |
|-----------|------|------|
| `record_push()` | ~1-2ns (atomic increment) | Every push |
| `record_pop_with_lag()` | ~50-100ns (map lookup + update) | Every pop |
| `record_ack()` | ~1-2ns (atomic increment) | Every ack |
| `record_stats_sample()` | ~50ns (few assignments) | Once per second |
| `check_and_flush()` | ~1-5μs (build JSON, submit job) | Once per minute |

At 100K ops/sec, the overhead is:
- 100K × 100ns = **10ms per second** = 1% overhead
- This is negligible compared to actual DB I/O

### Flush is Non-Blocking

`flush_to_database()` only:
1. Copies values (fast)
2. Builds SQL string (~1μs)
3. Submits via callback (async, returns immediately)

The actual DB write happens **asynchronously** in the job queue — doesn't block the event loop.

### Batched Queue Lag Writes

Instead of N SQL calls for N queues, we use a **single batched INSERT**:
```sql
INSERT INTO queen.queue_lag_metrics ...
SELECT ... FROM jsonb_array_elements($2::jsonb)
```
Even with 1000 queues, this is one job submission.

## Benefits

1. **Real-time metrics** — 1-second granularity in memory, 1-minute writes
2. **Cheap** — No message table scans for throughput
3. **Accurate lag** — Measured at pop time, not estimated from timestamps
4. **Per-worker visibility** — Can see individual worker health
5. **Scalable** — Writes scale with workers, not message volume
6. **Non-blocking** — All DB writes are async, no event loop stalls

## Trade-offs

1. **1 minute of data loss on crash** — Acceptable for dashboards
2. **More memory in event loop** — Minimal (few KB per worker)
3. **Per-queue lag requires coordination** — Workers write independently, need aggregation query
4. **Pending message count still needs reconciliation** — Can't track locally without scanning

---

## Decisions

### 1. Error Counts — YES

Track errors in `WorkerMetrics`:

```cpp
// Add to WorkerMetrics:
std::atomic<uint64_t> _db_error_count{0};   // DB query failures
std::atomic<uint64_t> _dlq_count{0};        // Messages moved to DLQ

void record_db_error() { _db_error_count++; }
void record_dlq() { _dlq_count++; }
```

**Where to call:**
- `record_db_error()` — In `queen.hpp` line ~967 when `PQresultStatus` is not `PGRES_TUPLES_OK`
- `record_dlq()` — Parse ack/transaction result JSON for `"dlq": true` or `"status": "dlq"`

**Add columns to `worker_metrics`:**
```sql
    db_error_count BIGINT DEFAULT 0,
    dlq_count BIGINT DEFAULT 0,
```

### 2. Per-Queue Lag — SEPARATE TABLE

Use `queen.queue_lag_metrics` as defined above. Written at minute boundary alongside `worker_metrics`.

```sql
CREATE TABLE IF NOT EXISTS queen.queue_lag_metrics (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    bucket_time TIMESTAMPTZ NOT NULL,
    queue_name VARCHAR(512) NOT NULL,
    
    -- Aggregated across all workers for this queue
    pop_count BIGINT DEFAULT 0,
    avg_lag_ms BIGINT DEFAULT 0,
    max_lag_ms BIGINT DEFAULT 0,
    
    UNIQUE(bucket_time, queue_name)
);
```

**Flush logic in WorkerMetrics:**
```cpp
void flush_queue_lag_metrics() {
    std::lock_guard<std::mutex> lock(_queue_lag_mutex);
    for (const auto& [queue_name, stats] : _queue_lag_stats) {
        // INSERT ... ON CONFLICT aggregate
        std::string sql = R"(
            INSERT INTO queen.queue_lag_metrics (bucket_time, queue_name, pop_count, avg_lag_ms, max_lag_ms)
            VALUES ($1, $2, $3, $4, $5)
            ON CONFLICT (bucket_time, queue_name) DO UPDATE SET
                pop_count = queen.queue_lag_metrics.pop_count + EXCLUDED.pop_count,
                avg_lag_ms = (queen.queue_lag_metrics.avg_lag_ms * queen.queue_lag_metrics.pop_count 
                            + EXCLUDED.avg_lag_ms * EXCLUDED.pop_count) 
                            / NULLIF(queen.queue_lag_metrics.pop_count + EXCLUDED.pop_count, 0),
                max_lag_ms = GREATEST(queen.queue_lag_metrics.max_lag_ms, EXCLUDED.max_lag_ms)
        )";
        // ... submit
    }
}
```

### 3. Percentiles (p50/p95/p99) — COMPUTE ON READ (LIMITED)

**Problem:** True percentiles require storing all values or using approximation algorithms.

**Decision:** Skip for v1, use avg/max only.

**Future option:** If needed, implement T-Digest in `WorkerMetrics`:
- NOT histogram-based (user said no to histograms)
- ~3KB memory per queue
- Gives accurate p50/p95/p99
- Can be added later without schema changes (just compute and add columns)

```cpp
// Future: Add to QueueLagStats if percentiles needed
#include <tdigest/tdigest.h>  // Would need to add dependency

struct QueueLagStats {
    uint64_t pop_count = 0;
    uint64_t total_lag_ms = 0;
    uint64_t max_lag_ms = 0;
    // tdigest::TDigest percentile_digest{100};  // Future
};
```

### 4. Histogram Buckets — NO

Not needed. Avg/max is sufficient for dashboards. If we need distribution later, use T-Digest instead.
