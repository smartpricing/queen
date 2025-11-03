#pragma once

#include "queen/database.hpp"
#include "queen/config.hpp"
#include <json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <atomic>

namespace queen {

// Forward declarations
class FileBufferManager;

struct Message {
    std::string id;
    std::string transaction_id;
    std::string partition_id;  // Partition UUID for partition-scoped operations
    nlohmann::json payload;
    std::string queue_name;
    std::string partition_name;
    std::string trace_id;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point lease_expires_at;
    std::string status; // "pending", "processing", "completed", "failed"
    int priority = 0;
    int retry_count = 0;
};

struct PushItem {
    std::string queue;
    std::string partition = "Default";
    nlohmann::json payload;
    std::optional<std::string> transaction_id;
    std::optional<std::string> trace_id;
};

struct PushResult {
    std::string transaction_id;
    std::string status; // "queued", "buffered", "failed"
    std::optional<std::string> error;
    std::optional<std::string> message_id;
    std::optional<std::string> trace_id;
};

struct PopOptions {
    bool wait = false;
    int timeout = 30000; // milliseconds
    int batch = 1;
    std::optional<std::string> subscription_mode;
    std::optional<std::string> subscription_from;
    bool auto_ack = false;  // Auto-acknowledge messages on delivery (QoS 0)
};

struct PopResult {
    std::vector<Message> messages;
    std::optional<std::string> lease_id;
};

struct QueueStats {
    std::string queue_name;
    int64_t pending_count = 0;
    int64_t processing_count = 0;
    int64_t completed_count = 0;
    int64_t failed_count = 0;
    double avg_processing_time = 0.0;
};

class QueueManager {
public:
    // Utility methods
    std::string generate_uuid();
    
    // Helper for FileBufferManager - push single message with explicit parameters
    void push_single_message(
        const std::string& queue_name,
        const std::string& partition_name,
        const nlohmann::json& payload,
        const std::string& namespace_name = "",
        const std::string& task = "",
        const std::string& transaction_id = "",
        const std::string& trace_id = ""
    );
    
private:
    std::shared_ptr<DatabasePool> db_pool_;
    std::shared_ptr<FileBufferManager> file_buffer_manager_;
    QueueConfig config_;
    std::string schema_name_;
    
    // Maintenance mode (cached from database for multi-instance support)
    std::atomic<bool> maintenance_mode_cached_{false};
    std::atomic<uint64_t> last_maintenance_check_ms_{0};
    static constexpr int MAINTENANCE_CACHE_TTL_MS = 5000;  // 5 seconds cache TTL
    
    // Internal helper methods
    std::string generate_transaction_id();
    bool check_maintenance_mode_with_cache();
    
    // Queue operations
    bool ensure_queue_exists(const std::string& queue_name, 
                           const std::string& namespace_name = "",
                           const std::string& task_name = "");
    bool ensure_partition_exists(const std::string& queue_name, 
                               const std::string& partition_name);
    
    // Message operations
    PushResult push_single_message(const PushItem& item);
    PopResult pop_from_queue_partition(const std::string& queue_name,
                                     const std::string& partition_name,
                                     const std::string& consumer_group,
                                     const PopOptions& options);
    PopResult pop_from_any_partition(const std::string& queue_name,
                                    const std::string& consumer_group,
                                    const PopOptions& options);
    PopResult pop_with_filters(const std::optional<std::string>& namespace_name,
                             const std::optional<std::string>& task_name,
                             const std::string& consumer_group,
                             const PopOptions& options);
    
    // Lease management
    std::string acquire_partition_lease(const std::string& queue_name,
                                      const std::string& partition_name,
                                      const std::string& consumer_group,
                                      int lease_time_seconds,
                                      const PopOptions& options = PopOptions{});
    std::string acquire_partition_lease(DatabaseConnection* conn,
                                      const std::string& queue_name,
                                      const std::string& partition_name,
                                      const std::string& consumer_group,
                                      int lease_time_seconds,
                                      const PopOptions& options = PopOptions{});
    bool extend_lease(const std::string& lease_id, int seconds);
    void release_lease(const std::string& lease_id);
    
public:
    explicit QueueManager(std::shared_ptr<DatabasePool> db_pool, 
                         const QueueConfig& config = QueueConfig{},
                         const std::string& schema_name = "queen");
    
    // Set file buffer manager (for maintenance mode)
    void set_file_buffer_manager(std::shared_ptr<FileBufferManager> fbm) {
        file_buffer_manager_ = fbm;
    }
    
    // Maintenance mode (multi-instance support via database)
    void set_maintenance_mode(bool enabled);
    bool get_maintenance_mode() const { return maintenance_mode_cached_.load(); }
    bool get_maintenance_mode_fresh();  // Force fresh check from DB
    size_t get_buffer_pending_count() const;
    bool is_buffer_healthy() const;
    nlohmann::json get_buffer_stats() const;
    
    // Queue configuration
    struct QueueOptions {
        int lease_time = 300;
        int max_size = 10000;
        int ttl = 3600;
        int retry_limit = 3;
        int retry_delay = 1000;
        bool dead_letter_queue = false;
        bool dlq_after_max_retries = false;
        int priority = 0;
        int delayed_processing = 0;
        int window_buffer = 0;
        int retention_seconds = 0;
        int completed_retention_seconds = 0;
        bool retention_enabled = false;
        bool encryption_enabled = false;
        int max_wait_time_seconds = 0;
    };
    
    bool configure_queue(const std::string& queue_name, 
                        const QueueOptions& options,
                        const std::string& namespace_name = "",
                        const std::string& task_name = "");
    
    bool delete_queue(const std::string& queue_name);
    
    // Core message operations
    std::vector<PushResult> push_messages(const std::vector<PushItem>& items);
    
private:
    // Internal batch processing
    std::vector<PushResult> push_messages_batch(const std::vector<PushItem>& items);
    std::vector<PushResult> push_messages_chunk(const std::vector<PushItem>& items,
                                                const std::string& queue_name,
                                                const std::string& partition_name);
    
    // Size estimation for dynamic batching
    size_t estimate_row_size(const PushItem& item, bool encryption_enabled) const;
    
public:
    
    PopResult pop_messages(const std::string& queue_name,
                          const std::optional<std::string>& partition_name,
                          const std::string& consumer_group,
                          const PopOptions& options);
    
    PopResult pop_with_namespace_task(const std::optional<std::string>& namespace_name,
                                    const std::optional<std::string>& task_name,
                                    const std::string& consumer_group,
                                    const PopOptions& options);
    
    // Long-polling support helpers
    int count_available_messages(const std::optional<std::string>& queue_name,
                                  const std::optional<std::string>& partition_name,
                                  const std::string& consumer_group);
    
    int count_available_messages_namespace(const std::optional<std::string>& namespace_name,
                                           const std::optional<std::string>& task_name,
                                           const std::string& consumer_group);
    
    // Acknowledgment
    struct AckItem {
        std::string transaction_id;
        std::string status; // "completed" or "failed"
        std::optional<std::string> error;
        std::optional<std::string> partition_id;  // Optional partition_id for efficiency
    };
    
    struct AckResult {
        std::string transaction_id;
        std::string status; // "completed", "failed_dlq", "failed_retry", "not_found"
        bool success = false;
    };
    
    AckResult acknowledge_message(const std::string& transaction_id,
                           const std::string& status,
                           const std::optional<std::string>& error = std::nullopt,
                           const std::string& consumer_group = "__QUEUE_MODE__",
                           const std::optional<std::string>& lease_id = std::nullopt,
                           const std::optional<std::string>& partition_id = std::nullopt);
    
    std::vector<AckResult> acknowledge_messages(const std::vector<AckItem>& acks,
                                         const std::string& consumer_group = "__QUEUE_MODE__");
    
    // Lease management
    bool extend_message_lease(const std::string& lease_id, int seconds = 60);
    int reclaim_expired_leases();
    
    // Statistics and monitoring
    QueueStats get_queue_stats(const std::string& queue_name);
    std::vector<QueueStats> get_all_queue_stats();
    
    // Pool statistics
    struct PoolStats {
        size_t total;
        size_t available;
        size_t in_use;
    };
    PoolStats get_pool_stats() const;
    
    // Health check
    bool health_check();
    
    // Initialize database schema
    bool initialize_schema();
    
    // Message tracing
    bool record_trace(
        const std::string& transaction_id,
        const std::string& partition_id,
        const std::string& consumer_group,
        const std::vector<std::string>& trace_names,
        const std::string& event_type,
        const nlohmann::json& data,
        const std::string& worker_id
    );
    
    nlohmann::json get_message_traces(
        const std::string& partition_id,
        const std::string& transaction_id
    );
    
    nlohmann::json get_traces_by_name(
        const std::string& trace_name,
        int limit,
        int offset
    );
    
    nlohmann::json get_available_trace_names(
        int limit,
        int offset
    );
    
    // Database pool access (for status/analytics queries)
    std::shared_ptr<DatabasePool> get_db_pool() const { return db_pool_; }
};

} // namespace queen
