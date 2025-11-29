#pragma once

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
    POP,            // Pop messages (batchable via pop_batch_v2)
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
    int worker_id;  // For routing response back
    size_t item_count = 0;  // Number of items in this request (for micro-batching decisions)
    std::chrono::steady_clock::time_point queued_at;
};

/**
 * Response from the sidecar
 */
struct SidecarResponse {
    SidecarOpType op_type = SidecarOpType::PUSH;  // Operation type (for response parsing)
    std::string request_id;
    int worker_id;
    bool success;
    std::string result_json;  // JSON string result from stored procedure
    std::string error_message;
    int64_t query_time_us;
};

/**
 * High-performance async PostgreSQL connection pool using the sidecar pattern.
 * 
 * - Single poller thread manages ALL connections
 * - Queries are fired in parallel, waited on with single select()
 * - uWS workers queue requests and return immediately
 * - Results delivered via callback to uWS event loop
 */
class SidecarDbPool {
public:
    /**
     * Create the sidecar pool
     * @param conn_str PostgreSQL connection string
     * @param pool_size Number of connections (= max concurrent queries)
     * @param statement_timeout_ms Statement timeout in milliseconds
     * @param thread_pool ThreadPool to run the poller loop in (1 thread reserved)
     */
    SidecarDbPool(const std::string& conn_str, 
                  int pool_size,
                  int statement_timeout_ms,
                  std::shared_ptr<astp::ThreadPool> thread_pool);
    
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
     * Returns immediately - result delivered via response queue
     */
    void submit(SidecarRequest request);
    
    /**
     * Pop completed responses (called from uWS callback)
     * @param out Vector to append responses to
     * @param max_count Maximum responses to pop (0 = all)
     * @return Number of responses popped
     */
    size_t pop_responses(std::vector<SidecarResponse>& out, size_t max_count = 0);
    
    /**
     * Check if there are pending responses
     */
    bool has_responses() const;
    
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
        size_t completed_responses;
        uint64_t total_queries;
        uint64_t total_query_time_us;
        
        // Per-operation stats
        std::unordered_map<SidecarOpType, OpStats> op_stats;
    };
    Stats get_stats() const;
    
private:
    // Tracks which items in a batch belong to which original request
    struct BatchedRequestInfo {
        std::string request_id;
        int worker_id;
        size_t start_index;  // First item index in the combined batch
        size_t item_count;   // Number of items from this request
    };
    
    // Connection slot
    struct ConnectionSlot {
        PGconn* conn = nullptr;
        int socket_fd = -1;
        bool busy = false;
        std::string request_id;  // For single-request mode
        int worker_id = 0;
        std::chrono::steady_clock::time_point query_start;
        
        // Micro-batching: tracks multiple requests in one SP call
        std::vector<BatchedRequestInfo> batched_requests;
        bool is_batched = false;
        
        // Operation type for this slot
        SidecarOpType op_type = SidecarOpType::PUSH;
        size_t total_items = 0;  // Total items in this batch
    };
    
    // Configuration
    std::string conn_str_;
    int pool_size_;
    int statement_timeout_ms_;
    
    // Connection slots
    std::vector<ConnectionSlot> slots_;
    
    // Request queue (uWS workers push here)
    std::deque<SidecarRequest> pending_requests_;
    mutable std::mutex pending_mutex_;
    
    // Response queue (poller pushes, timer callback pops)
    std::deque<SidecarResponse> completed_responses_;
    mutable std::mutex completed_mutex_;
    
    // Poller runs in threadpool
    std::shared_ptr<astp::ThreadPool> thread_pool_;
    std::atomic<bool> running_{false};
    
    // Statistics
    std::atomic<uint64_t> total_queries_{0};
    std::atomic<uint64_t> total_query_time_us_{0};
    
    // Per-operation statistics (protected by completed_mutex_ for simplicity)
    mutable std::unordered_map<SidecarOpType, OpStats> op_stats_;
    
    // Internal methods
    bool connect_slot(ConnectionSlot& slot);
    void disconnect_slot(ConnectionSlot& slot);
    void poller_loop();
};

} // namespace queen

