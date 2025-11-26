#pragma once

#include "queen/async_database.hpp"
#include "queen/queue_types.hpp"
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
    
    // Consumer group subscription metadata tracking
    void record_consumer_group_subscription(
        const std::string& consumer_group,
        const std::string& queue_name,
        const std::string& partition_name,
        const std::string& namespace_name,
        const std::string& task_name,
        const std::string& subscription_mode,
        const std::string& subscription_timestamp_sql
    );
    
    // Consumer group management
    void delete_consumer_group(
        const std::string& consumer_group,
        bool delete_metadata = true
    );
    
    void update_consumer_group_subscription(
        const std::string& consumer_group,
        const std::string& new_timestamp
    );
    
private:
    std::shared_ptr<AsyncDbPool> async_db_pool_;
    std::shared_ptr<FileBufferManager> file_buffer_manager_;
    QueueConfig config_;
    std::string schema_name_;
    
    // Maintenance mode (cached from database for multi-instance support)
    std::atomic<bool> maintenance_mode_cached_{false};
    std::atomic<uint64_t> last_maintenance_check_ms_{0};
    static constexpr int MAINTENANCE_CACHE_TTL_MS = 100;  // 100ms cache TTL (was 5000ms)
    
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
    
    // Transaction helpers - use provided connection for atomicity
    PushResult push_single_message_transactional(
        PGconn* conn,
        const PushItem& item
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
    
    // Schema initialization
    bool initialize_schema();
    
    // Core message operations - async versions
    std::vector<PushResult> push_messages(const std::vector<PushItem>& items);
    
    // INTERNAL ONLY: Push messages directly to database, bypassing maintenance mode
    // Used by: file buffer drain, internal operations, admin tools
    // NEVER call this from user-facing code paths!
    std::vector<PushResult> push_messages_internal(const std::vector<PushItem>& items);
    
    // Pop operations - async versions
    PopResult pop_messages_from_partition(
        const std::string& queue_name,
        const std::string& partition_name,
        const std::string& consumer_group,
        const PopOptions& options
    );
    
    // Optimized version using partition_id directly (avoids name->id lookup)
    PopResult pop_messages_from_partition_by_id(
        const std::string& queue_name,
        const std::string& partition_id,
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
        // For peer notification support - populated on successful ack
        std::optional<std::string> queue_name;
        std::optional<std::string> partition_name;
    };
    
    AckResult acknowledge_message(
        const std::string& transaction_id,
        const std::string& status,
        const std::optional<std::string>& error,
        const std::string& consumer_group,
        const std::optional<std::string>& lease_id,
        const std::optional<std::string>& partition_id
    );
    
private:
    // Transactional version - uses provided connection (no retry logic)
    AckResult acknowledge_message_transactional(
        PGconn* conn,
        const std::string& transaction_id,
        const std::string& status,
        const std::optional<std::string>& error,
        const std::string& consumer_group,
        const std::optional<std::string>& partition_id
    );
    
public:
    
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
    
    // Maintenance mode (multi-instance support via database)
    void set_maintenance_mode(bool enabled);
    bool get_maintenance_mode() const { return maintenance_mode_cached_.load(); }
    bool get_maintenance_mode_fresh();  // Force fresh check from DB
    size_t get_buffer_pending_count() const;
    bool is_buffer_healthy() const;
    nlohmann::json get_buffer_stats() const;
    
    // Queue configuration
    bool configure_queue(const std::string& queue_name,
                        const QueueOptions& options,
                        const std::string& namespace_name = "",
                        const std::string& task_name = "");
    
    bool delete_queue(const std::string& queue_name);
    
    // Lease management
    bool extend_message_lease(const std::string& lease_id, int seconds = 60);
    
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
    
    // Stream management
    nlohmann::json list_streams();
    nlohmann::json get_stream_stats();
    nlohmann::json get_stream_details(const std::string& stream_name);
    nlohmann::json get_stream_consumers(const std::string& stream_name);
    bool delete_stream(const std::string& stream_name);
};

} // namespace queen

