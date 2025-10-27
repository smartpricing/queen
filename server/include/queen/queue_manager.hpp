#pragma once

#include "queen/database.hpp"
#include "queen/config.hpp"
#include <json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>

namespace queen {

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
    std::string status; // "queued", "failed"
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
    QueueConfig config_;
    
    // Internal helper methods
    std::string generate_transaction_id();
    
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
                         const QueueConfig& config = QueueConfig{});
    
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
                           const std::optional<std::string>& lease_id = std::nullopt);
    
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
    
    // Database pool access (for status/analytics queries)
    std::shared_ptr<DatabasePool> get_db_pool() const { return db_pool_; }
};

} // namespace queen
