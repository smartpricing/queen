#pragma once

#include "queen/async_database.hpp"
#include "threadpool.hpp"
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>

namespace queen {

/**
 * StatsService - Background service for pre-computing analytics stats
 * 
 * Runs periodic jobs to maintain the queen.stats table:
 * 1. Compute partition-level stats from messages (full reconciliation)
 * 2. Aggregate queue stats from partition stats
 * 3. Aggregate namespace/task/system stats from queue stats
 * 4. Write historical snapshots for throughput charts
 * 5. Cleanup old history and orphaned stats
 * 
 * This enables O(1) analytics queries instead of expensive message table scans.
 * 
 * Two modes:
 * - Fast aggregation (every stats_interval_ms): Only roll-up existing partition stats
 * - Full reconciliation (every reconcile_interval_ms): Recompute partition stats from messages
 */
class StatsService {
private:
    std::shared_ptr<AsyncDbPool> db_pool_;
    std::shared_ptr<astp::ThreadPool> db_thread_pool_;
    std::shared_ptr<astp::ThreadPool> system_thread_pool_;
    
    std::atomic<bool> running_{false};
    
    // Configuration
    int stats_interval_ms_;           // Fast aggregation interval (default: 10s)
    int reconcile_interval_ms_;       // Full reconciliation interval (default: 60s)
    int history_bucket_minutes_;      // History bucket size (default: 1 min)
    int history_retention_days_;      // How long to keep history (default: 7 days)
    
    // Tracking
    std::chrono::steady_clock::time_point last_reconcile_;
    int cycle_count_ = 0;
    
public:
    StatsService(
        std::shared_ptr<AsyncDbPool> db_pool,
        std::shared_ptr<astp::ThreadPool> db_thread_pool,
        std::shared_ptr<astp::ThreadPool> system_thread_pool,
        int stats_interval_ms = 10000,
        int reconcile_interval_ms = 60000,
        int history_bucket_minutes = 1,
        int history_retention_days = 7
    );
    
    ~StatsService();
    
    void start();
    void stop();
    
    // Manual triggers (for testing/admin)
    void trigger_full_refresh();
    void trigger_aggregation();
    
private:
    void schedule_next_run();
    void stats_cycle();
    
    // Stats computation methods
    void run_full_reconciliation();
    void run_fast_aggregation();
    void write_history_snapshot();
    void cleanup_old_history();
    void cleanup_orphaned_stats();
};

} // namespace queen

