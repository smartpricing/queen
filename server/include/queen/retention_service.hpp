#pragma once

#include "queen/async_database.hpp"
#include "threadpool.hpp"
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>

namespace queen {

/**
 * RetentionService - Background service for cleaning up old data
 * 
 * Runs periodic cleanup jobs:
 * 1. Delete messages older than retention_seconds
 * 2. Delete completed messages older than completed_retention_seconds  
 * 3. Delete inactive partitions
 * 4. Delete old metrics data
 * 
 * Uses the global system thread pool for scheduling.
 */
class RetentionService {
private:
    std::shared_ptr<AsyncDbPool> db_pool_;
    std::shared_ptr<astp::ThreadPool> db_thread_pool_;
    std::shared_ptr<astp::ThreadPool> system_thread_pool_;
    
    std::atomic<bool> running_{false};
    
    // Configuration (from JobsConfig)
    int retention_interval_ms_;     // How often to run cleanup (default: 300000ms = 5 min)
    int retention_batch_size_;      // Max messages to delete per batch (default: 1000)
    int partition_cleanup_days_;    // Delete partitions inactive for N days (default: 7)
    int metrics_retention_days_;    // Keep metrics for N days (default: 90)
    
public:
    RetentionService(
        std::shared_ptr<AsyncDbPool> db_pool,
        std::shared_ptr<astp::ThreadPool> db_thread_pool,
        std::shared_ptr<astp::ThreadPool> system_thread_pool,
        int retention_interval_ms,
        int retention_batch_size,
        int partition_cleanup_days,
        int metrics_retention_days
    );
    
    ~RetentionService();
    
    void start();
    void stop();
    
private:
    void schedule_next_run();
    void cleanup_cycle();
    
    // Cleanup methods
    int cleanup_expired_messages();
    int cleanup_completed_messages();
    int cleanup_inactive_partitions();
    int cleanup_old_metrics();
};

} // namespace queen

