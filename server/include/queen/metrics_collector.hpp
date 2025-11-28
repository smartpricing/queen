#pragma once

#include "queen/async_database.hpp"
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

// Forward declaration
class SharedStateManager;

// Single sample of system metrics
struct MetricsSample {
    std::chrono::system_clock::time_point timestamp;
    
    // CPU (microseconds of CPU time)
    uint64_t cpu_user_us = 0;
    uint64_t cpu_system_us = 0;
    
    // Memory (bytes)
    uint64_t memory_rss_bytes = 0;
    uint64_t memory_virtual_bytes = 0;
    
    // Database pool
    int db_pool_size = 0;
    int db_pool_idle = 0;
    int db_pool_active = 0;
    
    // ThreadPools
    int db_threadpool_size = 0;
    int db_threadpool_queue_size = 0;
    int system_threadpool_size = 0;
    int system_threadpool_queue_size = 0;
    
    // Registries
    int poll_intention_registry_size = 0;
    int stream_poll_intention_registry_size = 0;
    int response_registry_size = 0;
    
    // Uptime
    int uptime_seconds = 0;
    
    // SharedState / UDPSYNC metrics
    bool shared_state_enabled = false;
    
    // Queue config cache
    int qc_cache_size = 0;
    uint64_t qc_cache_hits = 0;
    uint64_t qc_cache_misses = 0;
    
    // Partition ID cache
    int pid_cache_size = 0;
    uint64_t pid_cache_hits = 0;
    uint64_t pid_cache_misses = 0;
    uint64_t pid_cache_evictions = 0;
    
    // Lease hints
    int lease_hints_size = 0;
    uint64_t lease_hints_used = 0;
    uint64_t lease_hints_wrong = 0;
    
    // Consumer presence
    int consumer_queues_tracked = 0;
    int consumer_servers_tracked = 0;
    int consumer_total_registrations = 0;
    
    // Server health
    int servers_alive = 0;
    int servers_dead = 0;
    
    // Transport
    uint64_t transport_sent = 0;
    uint64_t transport_received = 0;
    uint64_t transport_dropped = 0;
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
    AggregatedMetric poll_intention_registry_size;
    AggregatedMetric stream_poll_intention_registry_size;
    AggregatedMetric response_registry_size;
    int uptime_seconds;
    
    // SharedState / UDPSYNC metrics
    bool shared_state_enabled = false;
    
    AggregatedMetric qc_cache_size;
    AggregatedMetric qc_cache_hits;
    AggregatedMetric qc_cache_misses;
    
    AggregatedMetric pid_cache_size;
    AggregatedMetric pid_cache_hits;
    AggregatedMetric pid_cache_misses;
    AggregatedMetric pid_cache_evictions;
    
    AggregatedMetric lease_hints_size;
    AggregatedMetric lease_hints_used;
    AggregatedMetric lease_hints_wrong;
    
    AggregatedMetric consumer_queues_tracked;
    AggregatedMetric consumer_servers_tracked;
    AggregatedMetric consumer_total_registrations;
    
    AggregatedMetric servers_alive;
    AggregatedMetric servers_dead;
    
    AggregatedMetric transport_sent;
    AggregatedMetric transport_received;
    AggregatedMetric transport_dropped;
    
    nlohmann::json to_json() const {
        nlohmann::json result = {
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
            {"registries", {
                {"poll_intention", poll_intention_registry_size.to_json()},
                {"stream_poll_intention", stream_poll_intention_registry_size.to_json()},
                {"response", response_registry_size.to_json()}
            }},
            {"uptime_seconds", uptime_seconds}
        };
        
        // Add SharedState metrics if enabled
        if (shared_state_enabled) {
            result["shared_state"] = {
                {"enabled", true},
                {"queue_config_cache", {
                    {"size", qc_cache_size.to_json()},
                    {"hits", qc_cache_hits.to_json()},
                    {"misses", qc_cache_misses.to_json()}
                }},
                {"partition_id_cache", {
                    {"size", pid_cache_size.to_json()},
                    {"hits", pid_cache_hits.to_json()},
                    {"misses", pid_cache_misses.to_json()},
                    {"evictions", pid_cache_evictions.to_json()}
                }},
                {"lease_hints", {
                    {"size", lease_hints_size.to_json()},
                    {"used", lease_hints_used.to_json()},
                    {"wrong", lease_hints_wrong.to_json()}
                }},
                {"consumer_presence", {
                    {"queues_tracked", consumer_queues_tracked.to_json()},
                    {"servers_tracked", consumer_servers_tracked.to_json()},
                    {"total_registrations", consumer_total_registrations.to_json()}
                }},
                {"server_health", {
                    {"alive", servers_alive.to_json()},
                    {"dead", servers_dead.to_json()}
                }},
                {"transport", {
                    {"sent", transport_sent.to_json()},
                    {"received", transport_received.to_json()},
                    {"dropped", transport_dropped.to_json()}
                }}
            };
        }
        
        return result;
    }
};

// Forward declarations
class PollIntentionRegistry;
class StreamPollIntentionRegistry;
class ResponseRegistry;

class MetricsCollector {
private:
    std::shared_ptr<AsyncDbPool> db_pool_;
    std::shared_ptr<astp::ThreadPool> db_thread_pool_;
    std::shared_ptr<astp::ThreadPool> system_thread_pool_;
    std::shared_ptr<PollIntentionRegistry> poll_intention_registry_;
    std::shared_ptr<StreamPollIntentionRegistry> stream_poll_intention_registry_;
    std::shared_ptr<ResponseRegistry> response_registry_;
    std::shared_ptr<SharedStateManager> shared_state_manager_;
    
    std::atomic<bool> running_{false};
    std::chrono::steady_clock::time_point start_time_;
    
    // Server identification
    std::string hostname_;
    int port_;
    std::string worker_id_;
    
    // Configuration
    int sample_interval_ms_;    // Default: 1000ms (1 second)
    int aggregation_window_s_;  // Default: 60s (1 minute)
    
    // Sample buffer
    std::vector<MetricsSample> samples_;
    std::mutex samples_mutex_;
    
public:
    MetricsCollector(
        std::shared_ptr<AsyncDbPool> db_pool,
        std::shared_ptr<astp::ThreadPool> db_thread_pool,
        std::shared_ptr<astp::ThreadPool> system_thread_pool,
        std::shared_ptr<PollIntentionRegistry> poll_intention_registry,
        std::shared_ptr<StreamPollIntentionRegistry> stream_poll_intention_registry,
        std::shared_ptr<ResponseRegistry> response_registry,
        std::shared_ptr<SharedStateManager> shared_state_manager,
        const std::string& hostname,
        int port,
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

