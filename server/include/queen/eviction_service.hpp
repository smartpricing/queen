#pragma once

#include "queen/async_database.hpp"
#include "threadpool.hpp"
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>

namespace queen {

/**
 * EvictionService - Background service for evicting messages that exceed max_wait_time
 * 
 * Runs periodic eviction jobs:
 * - Delete messages that have been waiting longer than max_wait_time_seconds
 * 
 * Uses the global system thread pool for scheduling.
 */
class EvictionService {
private:
    std::shared_ptr<AsyncDbPool> db_pool_;
    std::shared_ptr<astp::ThreadPool> db_thread_pool_;
    std::shared_ptr<astp::ThreadPool> system_thread_pool_;
    
    std::atomic<bool> running_{false};
    
    // Configuration (from JobsConfig)
    int eviction_interval_ms_;      // How often to run eviction (default: 60000ms = 1 min)
    int eviction_batch_size_;       // Max messages to evict per batch (default: 1000)
    
public:
    EvictionService(
        std::shared_ptr<AsyncDbPool> db_pool,
        std::shared_ptr<astp::ThreadPool> db_thread_pool,
        std::shared_ptr<astp::ThreadPool> system_thread_pool,
        int eviction_interval_ms,
        int eviction_batch_size
    );
    
    ~EvictionService();
    
    void start();
    void stop();
    
private:
    void schedule_next_run();
    void eviction_cycle();
    
    // Eviction method
    int evict_expired_waiting_messages();
};

} // namespace queen

