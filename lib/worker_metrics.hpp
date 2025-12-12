#ifndef _WORKER_METRICS_HPP_
#define _WORKER_METRICS_HPP_

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include <functional>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <json.hpp>
#include <unistd.h>

namespace queen {

/**
 * WorkerMetrics - Collects and buffers metrics for a single worker.
 * 
 * Thread-safety: This class is designed to be used from a SINGLE event loop thread.
 * All operations (record_*, check_and_flush) must be called from the same thread.
 * The atomic counters are used only for potential external monitoring reads.
 * 
 * Buffers metrics in memory and flushes to database at minute boundaries.
 */
class WorkerMetrics {
public:
    // Callback type for flushing metrics to database
    // sql: The INSERT statement
    // params: Vector of parameter values
    using FlushCallback = std::function<void(const std::string& sql, const std::vector<std::string>& params)>;

    WorkerMetrics(
        const std::string& hostname,
        uint16_t worker_id,
        FlushCallback flush_callback
    ) : _hostname(hostname),
        _worker_id(worker_id),
        _pid(getpid()),
        _flush_callback(std::move(flush_callback)),
        _current_minute_start(std::chrono::floor<std::chrono::minutes>(
            std::chrono::system_clock::now()
        ))
    {}

    // --- Request counters (one per API call) ---
    void record_push_request() { _push_request_count++; }
    void record_pop_request() { _pop_request_count++; }
    void record_ack_request() { _ack_request_count++; }
    void record_transaction() { _transaction_count++; }
    
    // --- Message counters (actual messages processed) ---
    void record_push_messages(size_t count) { _push_message_count += count; }
    void record_ack_messages(size_t total, size_t success, size_t failed) { 
        _ack_message_count += total;
        _ack_success_count += success;
        _ack_failed_count += failed;
    }
    
    // --- Error counters ---
    void record_db_error() { _db_error_count++; }
    void record_dlq() { _dlq_count++; }

    // Record pop with lag measurement (called once per message popped)
    // Note: Same message can be popped by multiple consumer groups
    void record_pop_with_lag(const std::string& queue_name, uint64_t lag_ms) {
        _pop_message_count++;
        
        // NOTE: No mutex needed! All operations run on the same event loop thread.
        // The Queen event loop is single-threaded (libuv), so:
        // - record_* are called from job result handlers
        // - record_stats_sample is called from _uv_stats_timer_cb
        // - check_and_flush is called from _uv_stats_timer_cb
        // All on the same thread, no races possible.
        
        auto& stats = _queue_lag_stats[queue_name];
        stats.pop_count++;
        stats.total_lag_ms += lag_ms;
        if (lag_ms > stats.max_lag_ms) {
            stats.max_lag_ms = lag_ms;
        }
    }

    // --- Event loop health (called from stats timer) ---
    void record_stats_sample(
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
        if (event_loop_lag_ms > _aggregates.max_event_loop_lag) {
            _aggregates.max_event_loop_lag = event_loop_lag_ms;
        }
        _aggregates.sum_free_slots += free_slots;
        if (free_slots < _aggregates.min_free_slots) {
            _aggregates.min_free_slots = free_slots;
        }
        _aggregates.db_connections = db_connections;
        _aggregates.sum_job_queue_size += job_queue_size;
        if (job_queue_size > _aggregates.max_job_queue_size) {
            _aggregates.max_job_queue_size = job_queue_size;
        }
        if (backoff_size > _aggregates.max_backoff_size) {
            _aggregates.max_backoff_size = backoff_size;
        }
        _aggregates.jobs_done += jobs_done;
    }

    // --- Minute boundary check (called from stats timer) ---
    // Returns true if metrics were flushed
    bool check_and_flush() {
        auto now = std::chrono::system_clock::now();
        auto current_minute = std::chrono::floor<std::chrono::minutes>(now);
        
        if (current_minute <= _current_minute_start) {
            return false;  // Still in same minute
        }
        
        // Crossed minute boundary - flush and reset
        flush_to_database();
        
        // Reset all counters
        _current_minute_start = current_minute;
        _push_request_count = 0;
        _pop_request_count = 0;
        _ack_request_count = 0;
        _transaction_count = 0;
        _push_message_count = 0;
        _pop_message_count = 0;
        _ack_message_count = 0;
        _ack_success_count = 0;
        _ack_failed_count = 0;
        _db_error_count = 0;
        _dlq_count = 0;
        
        _aggregates.reset();
        _queue_lag_stats.clear();
        
        return true;
    }

private:
    // Worker identity
    std::string _hostname;
    uint16_t _worker_id;
    pid_t _pid;
    
    // Flush callback (submits SQL to Queen)
    FlushCallback _flush_callback;
    
    // --- Request counters (one per API call) ---
    std::atomic<uint64_t> _push_request_count{0};
    std::atomic<uint64_t> _pop_request_count{0};
    std::atomic<uint64_t> _ack_request_count{0};
    std::atomic<uint64_t> _transaction_count{0};
    
    // --- Message counters (actual messages processed) ---
    std::atomic<uint64_t> _push_message_count{0};
    std::atomic<uint64_t> _pop_message_count{0};
    std::atomic<uint64_t> _ack_message_count{0};
    std::atomic<uint64_t> _ack_success_count{0};
    std::atomic<uint64_t> _ack_failed_count{0};
    
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
    
    // Format timestamp for SQL
    std::string format_bucket_time() const {
        auto time_t_val = std::chrono::system_clock::to_time_t(_current_minute_start);
        std::tm tm_buf;
#ifdef _WIN32
        gmtime_s(&tm_buf, &time_t_val);
#else
        gmtime_r(&time_t_val, &tm_buf);
#endif
        std::ostringstream ss;
        ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:00Z");
        return ss.str();
    }
    
    void flush_to_database() {
        // No locks needed - all on event loop thread
        
        // Snapshot current values (atomics for potential external reads)
        uint64_t push_req = _push_request_count.load();
        uint64_t pop_req = _pop_request_count.load();
        uint64_t ack_req = _ack_request_count.load();
        uint64_t txn = _transaction_count.load();
        uint64_t push_msg = _push_message_count.load();
        uint64_t pop_msg = _pop_message_count.load();
        uint64_t ack_msg = _ack_message_count.load();
        uint64_t ack_success = _ack_success_count.load();
        uint64_t ack_failed = _ack_failed_count.load();
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
            if (stats.max_lag_ms > max_lag) {
                max_lag = stats.max_lag_ms;
            }
        }
        
        uint64_t avg_lag = total_lag_count > 0 ? total_lag_sum / total_lag_count : 0;
        
        // Build SQL with both request and message counts
        std::string sql = R"(
            INSERT INTO queen.worker_metrics (
                hostname, worker_id, pid, bucket_time,
                avg_event_loop_lag_ms, max_event_loop_lag_ms,
                avg_free_slots, min_free_slots, db_connections,
                avg_job_queue_size, max_job_queue_size, backoff_size,
                jobs_done, push_request_count, pop_request_count, ack_request_count, transaction_count,
                push_message_count, pop_message_count, ack_message_count, ack_success_count, ack_failed_count,
                lag_count, avg_lag_ms, max_lag_ms,
                db_error_count, dlq_count
            ) VALUES ($1, $2, $3, $4::timestamptz, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22, $23, $24, $25, $26, $27)
            ON CONFLICT (hostname, worker_id, pid, bucket_time) DO UPDATE SET
                jobs_done = queen.worker_metrics.jobs_done + EXCLUDED.jobs_done,
                push_request_count = queen.worker_metrics.push_request_count + EXCLUDED.push_request_count,
                pop_request_count = queen.worker_metrics.pop_request_count + EXCLUDED.pop_request_count,
                ack_request_count = queen.worker_metrics.ack_request_count + EXCLUDED.ack_request_count,
                transaction_count = queen.worker_metrics.transaction_count + EXCLUDED.transaction_count,
                push_message_count = queen.worker_metrics.push_message_count + EXCLUDED.push_message_count,
                pop_message_count = queen.worker_metrics.pop_message_count + EXCLUDED.pop_message_count,
                ack_message_count = queen.worker_metrics.ack_message_count + EXCLUDED.ack_message_count,
                ack_success_count = queen.worker_metrics.ack_success_count + EXCLUDED.ack_success_count,
                ack_failed_count = queen.worker_metrics.ack_failed_count + EXCLUDED.ack_failed_count,
                lag_count = queen.worker_metrics.lag_count + EXCLUDED.lag_count,
                max_lag_ms = GREATEST(queen.worker_metrics.max_lag_ms, EXCLUDED.max_lag_ms),
                db_error_count = queen.worker_metrics.db_error_count + EXCLUDED.db_error_count,
                dlq_count = queen.worker_metrics.dlq_count + EXCLUDED.dlq_count
        )";
        
        std::string bucket_time = format_bucket_time();
        
        std::vector<std::string> params = {
            _hostname,
            std::to_string(_worker_id),
            std::to_string(_pid),
            bucket_time,
            std::to_string(agg.samples > 0 ? agg.sum_event_loop_lag / agg.samples : 0),
            std::to_string(agg.max_event_loop_lag),
            std::to_string(agg.samples > 0 ? agg.sum_free_slots / agg.samples : 0),
            std::to_string(agg.min_free_slots == UINT64_MAX ? 0 : agg.min_free_slots),
            std::to_string(agg.db_connections),
            std::to_string(agg.samples > 0 ? agg.sum_job_queue_size / agg.samples : 0),
            std::to_string(agg.max_job_queue_size),
            std::to_string(agg.max_backoff_size),
            std::to_string(agg.jobs_done),
            std::to_string(push_req),
            std::to_string(pop_req),
            std::to_string(ack_req),
            std::to_string(txn),
            std::to_string(push_msg),
            std::to_string(pop_msg),
            std::to_string(ack_msg),
            std::to_string(ack_success),
            std::to_string(ack_failed),
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
    
    void flush_queue_lag_metrics() {
        // No lock needed - single-threaded event loop
        if (_queue_lag_stats.empty()) return;
        
        std::string bucket_time = format_bucket_time();
        
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
            _flush_callback(sql, {bucket_time, batch.dump()});
        }
    }
};

} // namespace queen

#endif // _WORKER_METRICS_HPP_

