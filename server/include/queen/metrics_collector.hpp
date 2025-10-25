#pragma once

#include "queen/database.hpp"
#include "threadpool.hpp"
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <vector>
#include <chrono>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>

#ifdef __linux__
#include <unistd.h>
#include <sys/resource.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/resource.h>
#endif

namespace queen {

// Single sample of system metrics
struct MetricsSample {
    std::chrono::system_clock::time_point timestamp;
    
    // CPU (microseconds of CPU time)
    uint64_t cpu_user_us;
    uint64_t cpu_system_us;
    
    // Memory (bytes)
    uint64_t memory_rss_bytes;
    uint64_t memory_virtual_bytes;
    
    // Database pool
    int db_pool_size;
    int db_pool_idle;
    int db_pool_active;
    
    // ThreadPools
    int db_threadpool_size;
    int db_threadpool_queue_size;
    int system_threadpool_size;
    int system_threadpool_queue_size;
    
    // Uptime
    int uptime_seconds;
};

// Aggregated metric (avg, min, max, last)
struct AggregatedMetric {
    double avg;
    uint64_t min;
    uint64_t max;
    uint64_t last;
    
    nlohmann::json to_json() const {
        return {
            {"avg", avg},
            {"min", min},
            {"max", max},
            {"last", last}
        };
    }
};

struct AggregatedMetrics {
    std::chrono::system_clock::time_point window_start;
    int sample_count;
    
    AggregatedMetric cpu_user_us;
    AggregatedMetric cpu_system_us;
    AggregatedMetric memory_rss_bytes;
    AggregatedMetric memory_virtual_bytes;
    AggregatedMetric db_pool_size;
    AggregatedMetric db_pool_idle;
    AggregatedMetric db_pool_active;
    AggregatedMetric db_threadpool_size;
    AggregatedMetric db_threadpool_queue_size;
    AggregatedMetric system_threadpool_size;
    AggregatedMetric system_threadpool_queue_size;
    int uptime_seconds;
    
    nlohmann::json to_json() const {
        return {
            {"cpu", {
                {"user_us", cpu_user_us.to_json()},
                {"system_us", cpu_system_us.to_json()}
            }},
            {"memory", {
                {"rss_bytes", memory_rss_bytes.to_json()},
                {"virtual_bytes", memory_virtual_bytes.to_json()}
            }},
            {"database", {
                {"pool_size", db_pool_size.to_json()},
                {"pool_idle", db_pool_idle.to_json()},
                {"pool_active", db_pool_active.to_json()}
            }},
            {"threadpool", {
                {"db", {
                    {"pool_size", db_threadpool_size.to_json()},
                    {"queue_size", db_threadpool_queue_size.to_json()}
                }},
                {"system", {
                    {"pool_size", system_threadpool_size.to_json()},
                    {"queue_size", system_threadpool_queue_size.to_json()}
                }}
            }},
            {"uptime_seconds", uptime_seconds}
        };
    }
};

class MetricsCollector {
private:
    std::shared_ptr<DatabasePool> db_pool_;
    std::shared_ptr<astp::ThreadPool> db_thread_pool_;
    std::shared_ptr<astp::ThreadPool> system_thread_pool_;
    
    std::atomic<bool> running_{false};
    std::chrono::steady_clock::time_point start_time_;
    
    // Server identification
    std::string hostname_;
    int port_;
    int process_id_;
    std::string worker_id_;
    
    // Configuration
    int sample_interval_ms_;    // Default: 1000ms (1 second)
    int aggregation_window_s_;  // Default: 60s (1 minute)
    
    // Sample buffer
    std::vector<MetricsSample> samples_;
    std::mutex samples_mutex_;
    
public:
    MetricsCollector(
        std::shared_ptr<DatabasePool> db_pool,
        std::shared_ptr<astp::ThreadPool> db_thread_pool,
        std::shared_ptr<astp::ThreadPool> system_thread_pool,
        const std::string& hostname,
        int port,
        int process_id,
        const std::string& worker_id = "worker-0",
        int sample_interval_ms = 1000,
        int aggregation_window_s = 60
    );
    
    ~MetricsCollector();
    
    void start();
    void stop();
    
private:
    void schedule_next_collection();
    void collection_cycle();
    bool should_aggregate();
    
    MetricsSample collect_sample();
    void collect_linux_metrics(MetricsSample& sample);
    void collect_macos_metrics(MetricsSample& sample);
    
    void aggregate_and_save();
    AggregatedMetrics aggregate(const std::vector<MetricsSample>& samples);
    void store_aggregated_metrics(const AggregatedMetrics& agg);
};

} // namespace queen

