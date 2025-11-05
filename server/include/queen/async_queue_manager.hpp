#pragma once

#include "queen/async_database.hpp"
#include "queen/queue_manager.hpp"  // Use structs from here
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

// Note: Message, PushItem, PushResult are defined in queue_manager.hpp

/**
 * @brief Async Queue Manager using AsyncDbPool for non-blocking database operations.
 * 
 * This is the async version of QueueManager, designed for high-throughput scenarios.
 * It uses the AsyncDbPool for all database interactions, with non-blocking I/O.
 * 
 * Focus: Push operations (push_single_message, push_messages, push_messages_batch, push_messages_chunk)
 */
class AsyncQueueManager {
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
    std::shared_ptr<AsyncDbPool> async_db_pool_;
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
    
    // Queue operations - async versions
    bool ensure_queue_exists(PGconn* conn,
                           const std::string& queue_name, 
                           const std::string& namespace_name = "",
                           const std::string& task_name = "");
    bool ensure_partition_exists(PGconn* conn,
                               const std::string& queue_name, 
                               const std::string& partition_name);
    
    // Message operations - async versions
    PushResult push_single_message(const PushItem& item);
    
    // Internal batch processing - async versions
    std::vector<PushResult> push_messages_batch(const std::vector<PushItem>& items);
    std::vector<PushResult> push_messages_chunk(const std::vector<PushItem>& items,
                                                const std::string& queue_name,
                                                const std::string& partition_name);
    
    // Size estimation for dynamic batching
    size_t estimate_row_size(const PushItem& item, bool encryption_enabled) const;
    
    // Lease management - async versions
    std::string acquire_partition_lease(
        PGconn* conn,
        const std::string& queue_name,
        const std::string& partition_name,
        const std::string& consumer_group,
        int lease_time_seconds,
        const PopOptions& options
    );
    
    void release_partition_lease(
        PGconn* conn,
        const std::string& queue_name,
        const std::string& partition_name,
        const std::string& consumer_group
    );
    
    bool ensure_consumer_group_exists(
        PGconn* conn,
        const std::string& queue_name,
        const std::string& partition_name,
        const std::string& consumer_group
    );
    
    // Helper for sending queries and getting results asynchronously
    void send_query_async(PGconn* conn, const std::string& sql, const std::vector<std::string>& params);
    PGResultPtr get_query_result_async(PGconn* conn);
    
public:
    explicit AsyncQueueManager(std::shared_ptr<AsyncDbPool> async_db_pool, 
                              const QueueConfig& config = QueueConfig{},
                              const std::string& schema_name = "queen");
    
    // Set file buffer manager (for maintenance mode)
    void set_file_buffer_manager(std::shared_ptr<FileBufferManager> fbm) {
        file_buffer_manager_ = fbm;
    }
    
    // Core message operations - async versions
    std::vector<PushResult> push_messages(const std::vector<PushItem>& items);
    
    // Pop operations - async versions
    PopResult pop_messages_from_partition(
        const std::string& queue_name,
        const std::string& partition_name,
        const std::string& consumer_group,
        const PopOptions& options
    );
    
    PopResult pop_messages_from_queue(
        const std::string& queue_name,
        const std::string& consumer_group,
        const PopOptions& options
    );
    
    PopResult pop_messages_filtered(
        const std::optional<std::string>& namespace_name,
        const std::optional<std::string>& task_name,
        const std::string& consumer_group,
        const PopOptions& options
    );
    
    // Acknowledgment operations - async versions
    struct AckResult {
        bool success;
        std::string message;
        std::optional<std::string> error;
    };
    
    AckResult acknowledge_message(
        const std::string& transaction_id,
        const std::string& status,
        const std::optional<std::string>& error,
        const std::string& consumer_group,
        const std::optional<std::string>& lease_id,
        const std::optional<std::string>& partition_id
    );
    
    struct BatchAckResult {
        int successful_acks;
        int failed_acks;
        std::vector<AckResult> results;
    };
    
    BatchAckResult acknowledge_messages_batch(
        const std::vector<nlohmann::json>& acknowledgments
    );
    
    // Transaction operations - async version
    struct TransactionResult {
        bool success;
        std::string transaction_id;
        std::vector<nlohmann::json> results;
        std::optional<std::string> error;
    };
    
    TransactionResult execute_transaction(
        const std::vector<nlohmann::json>& operations
    );
    
    // Health check
    bool health_check();
    
    // Pool statistics
    struct PoolStats {
        size_t total;
        size_t available;
        size_t in_use;
    };
    PoolStats get_pool_stats() const;
};

} // namespace queen

