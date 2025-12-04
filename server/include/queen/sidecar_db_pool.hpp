#pragma once

#include <uv.h>
#include <libpq-fe.h>
#include <deque>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <string>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <json.hpp>
#include "threadpool.hpp"

namespace queen {

/**
 * Operation type for sidecar requests
 * Determines how the request is processed and batched
 */
enum class SidecarOpType {
    PUSH,           // Push messages (batchable)
    POP,            // Pop messages - specific partition (NOT batchable)
    POP_BATCH,      // Pop messages - wildcard partition (batchable via partition pre-allocation)
    POP_WAIT,       // Long-poll pop (NOT batchable, managed by waiting queue)
    ACK,            // Single acknowledge
    ACK_BATCH,      // Batch acknowledge (batchable)
    TRANSACTION,    // Atomic transaction (NOT batchable)
    RENEW_LEASE     // Renew message lease (batchable)
};

/**
 * Request to be executed by the sidecar
 */
struct SidecarRequest {
    SidecarOpType op_type = SidecarOpType::PUSH;  // Operation type
    std::string request_id;
    std::string sql;
    std::vector<std::string> params;
    size_t item_count = 0;  // Number of items in this request (for micro-batching decisions)
    std::chrono::steady_clock::time_point queued_at;
    
    // For PUSH: queues to notify after success (queue -> partition list)
    std::vector<std::pair<std::string, std::string>> push_targets;
    
    // For POP_WAIT only:
    std::chrono::steady_clock::time_point wait_deadline;  // Absolute timeout
    std::chrono::steady_clock::time_point next_check;     // When to check next
    std::string queue_name;       // For notification matching
    std::string partition_name;   // For grouping
    std::string consumer_group;   // For grouping
    int batch_size = 1;           // Client's requested batch size
    std::string subscription_mode;
    std::string subscription_from;
    bool auto_ack = false;        // Auto-acknowledge messages on delivery (QoS 0)
};

/**
 * Response from the sidecar (passed to callback)
 */
struct SidecarResponse {
    SidecarOpType op_type = SidecarOpType::PUSH;  // Operation type (for response parsing)
    std::string request_id;
    bool success;
    std::string result_json;  // JSON string result from stored procedure
    std::string error_message;
    int64_t query_time_us;
    
    // For PUSH: targets to notify (copied from request)
    std::vector<std::pair<std::string, std::string>> push_targets;
};

/**
 * Callback type for delivering responses
 * Called from the sidecar poller thread - implementation should use loop->defer()
 * to safely deliver to the HTTP response in the correct thread
 */
using SidecarResponseCallback = std::function<void(SidecarResponse)>;

// Forward declaration
class SidecarDbPool;

/**
 * Tracks which items in a batch belong to which original request
 */
struct BatchedRequestInfo {
    std::string request_id;
    size_t start_index;  // First item index in the combined batch
    size_t item_count;   // Number of items from this request
    std::vector<std::pair<std::string, std::string>> push_targets;  // For PUSH: queue/partition pairs to notify
};

/**
 * Connection slot for async PostgreSQL operations
 */
struct ConnectionSlot {
    PGconn* conn = nullptr;
    int socket_fd = -1;
    bool busy = false;
    std::string request_id;  // For single-request mode
    std::chrono::steady_clock::time_point query_start;
    int64_t queue_wait_us = 0;  // Time spent waiting in queue before DB execution
    
    // Micro-batching: tracks multiple requests in one SP call
    std::vector<BatchedRequestInfo> batched_requests;
    bool is_batched = false;
    
    // Operation type for this slot
    SidecarOpType op_type = SidecarOpType::PUSH;
    size_t total_items = 0;  // Total items in this batch
    
    // libuv poll handle for this connection's socket
    uv_poll_t poll_handle;
    SidecarDbPool* pool = nullptr;  // Back-reference for callbacks
    bool poll_initialized = false;  // Track if poll handle is initialized
};

/**
 * High-performance async PostgreSQL connection pool using the sidecar pattern.
 * 
 * Architecture (per-worker sidecar):
 * - Each HTTP worker has its own SidecarDbPool instance
 * - Single poller thread per sidecar manages its connections
 * - Queries are fired in parallel, waited on with single select()
 * - Results delivered via callback (which should call loop->defer())
 * - Zero lock contention between workers (each has own sidecar)
 * 
 * Response delivery:
 * - Callback is invoked from poller thread
 * - Callback should use loop->defer() to deliver to event loop
 * - ResponseRegistry::send_response() handles lifecycle safety
 */
class SidecarDbPool {
public:
    /**
     * Sidecar tuning configuration
     */
    struct SidecarTuning {
        int micro_batch_wait_ms;    // Target cycle time for micro-batching (ms)
        int max_items_per_tx;       // Max items per database transaction
        int max_batch_size;         // Max requests per micro-batch
        int max_pending_count;      // Max pending requests before forcing immediate send
        
        // Default constructor with default values
        SidecarTuning(int batch_wait = 5, int items_per_tx = 1000, 
                      int batch_size = 1000, int pending_count = 50)
            : micro_batch_wait_ms(batch_wait)
            , max_items_per_tx(items_per_tx)
            , max_batch_size(batch_size)
            , max_pending_count(pending_count) {}
    };
    
    /**
     * Create the sidecar pool
     * @param conn_str PostgreSQL connection string
     * @param pool_size Number of connections (= max concurrent queries)
     * @param statement_timeout_ms Statement timeout in milliseconds
     * @param thread_pool ThreadPool to run the poller loop in
     * @param response_callback Callback for delivering responses (called from poller thread)
     * @param worker_id Worker ID for logging
     * @param tuning Micro-batching tuning parameters
     */
    SidecarDbPool(const std::string& conn_str, 
                  int pool_size,
                  int statement_timeout_ms,
                  std::shared_ptr<astp::ThreadPool> thread_pool,
                  SidecarResponseCallback response_callback,
                  int worker_id = -1,
                  SidecarTuning tuning = {});
    
    ~SidecarDbPool();
    
    // Non-copyable
    SidecarDbPool(const SidecarDbPool&) = delete;
    SidecarDbPool& operator=(const SidecarDbPool&) = delete;
    
    /**
     * Start the poller thread
     */
    void start();
    
    /**
     * Stop the poller thread gracefully
     */
    void stop();
    
    /**
     * Queue a query for async execution
     * Returns immediately - result delivered via callback
     */
    void submit(SidecarRequest request);
    
    /**
     * Notify this sidecar that a queue has activity (new messages available)
     * Called by SharedStateManager when push notification is received
     * Sets next_check to now for all waiting requests for this queue
     */
    void notify_queue_activity(const std::string& queue_name);
    
    /**
     * Submit a batch of POP requests using the unified procedure.
     * Uses a single DB round-trip for efficient batch processing.
     * 
     * @param requests Vector of sidecar requests (POP_BATCH type)
     */
    void submit_pop_batch(std::vector<SidecarRequest> requests);
    
    // Per-operation statistics
    struct OpStats {
        uint64_t count = 0;           // Number of operations
        uint64_t total_time_us = 0;   // Total time in microseconds
        uint64_t items_processed = 0; // Total items processed (for batched ops)
    };
    
    // Statistics
    struct Stats {
        size_t total_connections;
        size_t busy_connections;
        size_t pending_requests;
        uint64_t total_queries;
        uint64_t total_query_time_us;
        
        // Per-operation stats
        std::unordered_map<SidecarOpType, OpStats> op_stats;
    };
    Stats get_stats() const;
    
private:
    // Configuration
    std::string conn_str_;
    int pool_size_;
    int statement_timeout_ms_;
    int worker_id_;  // For logging
    SidecarTuning tuning_;  // Micro-batching tuning
    
    // Response delivery callback
    SidecarResponseCallback response_callback_;
    
    // Connection slots
    std::vector<ConnectionSlot> slots_;
    
    // Request queue (HTTP worker pushes here)
    std::deque<SidecarRequest> pending_requests_;
    mutable std::mutex pending_mutex_;
    
    // Waiting queue for POP_WAIT requests
    std::deque<SidecarRequest> waiting_requests_;
    mutable std::mutex waiting_mutex_;
    
    // Track POP_WAIT requests that are currently being executed as POPs
    // Key: request_id, Value: original POP_WAIT request data for re-queuing
    struct PopWaitTracker {
        std::chrono::steady_clock::time_point wait_deadline;
        std::string queue_name;
        std::string partition_name;
        std::string consumer_group;
        int batch_size;
        std::string subscription_mode;
        std::string subscription_from;
        std::string sql;
        std::vector<std::string> params;
        bool auto_ack = false;
    };
    std::unordered_map<std::string, PopWaitTracker> pop_wait_trackers_;
    mutable std::mutex tracker_mutex_;
    
    // Poller runs in threadpool
    std::shared_ptr<astp::ThreadPool> thread_pool_;
    std::atomic<bool> running_{false};
    
    // libuv event loop and handles
    uv_loop_t* loop_ = nullptr;
    uv_timer_t batch_timer_;
    uv_timer_t waiting_timer_;
    uv_async_t submit_signal_;
    bool loop_initialized_ = false;
    
    // Statistics
    std::atomic<uint64_t> total_queries_{0};
    std::atomic<uint64_t> total_query_time_us_{0};
    
    // Per-operation statistics (protected by stats_mutex_)
    mutable std::unordered_map<SidecarOpType, OpStats> op_stats_;
    mutable std::mutex stats_mutex_;
    
    // Internal methods
    bool connect_slot(ConnectionSlot& slot);
    void disconnect_slot(ConnectionSlot& slot);
    void poller_loop();
    
    // Deliver response via callback
    void deliver_response(SidecarResponse response);
    
    // Process waiting queue for POP_WAIT requests
    void process_waiting_queue();
    
    // Build group key for backoff coordination
    static std::string make_group_key(const std::string& queue, 
                                       const std::string& partition,
                                       const std::string& consumer_group);
    
    /**
     * Lightweight check if queue/partition has pending messages
     * Used during fallback polling to avoid expensive full POP when no messages exist
     * @return true if messages are available, false otherwise
     */
    bool check_has_pending(const std::string& queue_name,
                           const std::string& partition_name,
                           const std::string& consumer_group);
    
    // Connection dedicated for quick synchronous checks (has_pending)
    PGconn* check_conn_ = nullptr;
    std::mutex check_conn_mutex_;
    
    // ========== libuv methods ==========
    
    // Drain pending requests to available slots
    void drain_pending_to_slots();
    
    // Process result from a completed query
    void process_slot_result(ConnectionSlot& slot);
    
    // Handle connection error on a slot
    void handle_slot_error(ConnectionSlot& slot, const std::string& error_msg);
    
    // Start/stop watching a slot's socket
    void start_watching_slot(ConnectionSlot& slot, int events);
    void stop_watching_slot(ConnectionSlot& slot);
    
    // Static libuv callbacks (called from event loop)
    static void on_batch_timer(uv_timer_t* handle);
    static void on_waiting_timer(uv_timer_t* handle);
    static void on_submit_signal(uv_async_t* handle);
    static void on_socket_event(uv_poll_t* handle, int status, int events);
    static void on_handle_close(uv_handle_t* handle);
};

} // namespace queen
