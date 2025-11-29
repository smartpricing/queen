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
    POP,            // Pop messages (NOT batchable - each needs own lease)
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
};

/**
 * Callback type for delivering responses
 * Called from the sidecar poller thread - implementation should use loop->defer()
 * to safely deliver to the HTTP response in the correct thread
 */
using SidecarResponseCallback = std::function<void(SidecarResponse)>;

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
     * Create the sidecar pool
     * @param conn_str PostgreSQL connection string
     * @param pool_size Number of connections (= max concurrent queries)
     * @param statement_timeout_ms Statement timeout in milliseconds
     * @param thread_pool ThreadPool to run the poller loop in
     * @param response_callback Callback for delivering responses (called from poller thread)
     */
    SidecarDbPool(const std::string& conn_str, 
                  int pool_size,
                  int statement_timeout_ms,
                  std::shared_ptr<astp::ThreadPool> thread_pool,
                  SidecarResponseCallback response_callback);
    
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
    // Tracks which items in a batch belong to which original request
    struct BatchedRequestInfo {
        std::string request_id;
        size_t start_index;  // First item index in the combined batch
        size_t item_count;   // Number of items from this request
    };
    
    // Connection slot
    struct ConnectionSlot {
        PGconn* conn = nullptr;
        int socket_fd = -1;
        bool busy = false;
        std::string request_id;  // For single-request mode
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
    
    // Response delivery callback
    SidecarResponseCallback response_callback_;
    
    // Connection slots
    std::vector<ConnectionSlot> slots_;
    
    // Request queue (HTTP worker pushes here)
    std::deque<SidecarRequest> pending_requests_;
    mutable std::mutex pending_mutex_;
    
    // Poller runs in threadpool
    std::shared_ptr<astp::ThreadPool> thread_pool_;
    std::atomic<bool> running_{false};
    
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
};

} // namespace queen
