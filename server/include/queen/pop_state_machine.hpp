#pragma once

#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <atomic>
#include <memory>
#include <json.hpp>
#include <libpq-fe.h>

// Need SidecarDbPool::ConnectionSlot for PopRequestState
// Note: sidecar_db_pool.hpp only forward-declares PopBatchStateMachine, avoiding circular include
#include "queen/sidecar_db_pool.hpp"

namespace queen {

/**
 * State of a single POP request within the state machine
 */
enum class PopState {
    PENDING,      // Waiting for DB connection
    RESOLVING,    // Wildcard: finding partition
    LEASING,      // Acquiring lease on partition
    FETCHING,     // Reading messages
    RELEASING,    // Releasing empty lease (optional)
    COMPLETED,    // Done with messages
    EMPTY,        // Done without messages
    FAILED        // Error
};

/**
 * Convert PopState to string for logging
 */
inline const char* pop_state_to_string(PopState state) {
    switch (state) {
        case PopState::PENDING: return "PENDING";
        case PopState::RESOLVING: return "RESOLVING";
        case PopState::LEASING: return "LEASING";
        case PopState::FETCHING: return "FETCHING";
        case PopState::RELEASING: return "RELEASING";
        case PopState::COMPLETED: return "COMPLETED";
        case PopState::EMPTY: return "EMPTY";
        case PopState::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

/**
 * Represents a single POP request and its state
 */
struct PopRequestState {
    // Request identification
    std::string request_id;
    int idx = 0;
    
    // Request parameters
    std::string queue_name;
    std::string partition_name;      // Empty for wildcard
    std::string consumer_group;
    int batch_size = 10;
    int lease_seconds = 0;           // 0 = use queue default
    std::string subscription_mode;
    std::string subscription_from;
    
    // Resolved values (filled during processing)
    std::string partition_id;        // UUID as string
    std::string queue_id;
    int queue_lease_time = 0;
    int window_buffer = 0;
    int delayed_processing = 0;
    
    // Cursor position (from partition_consumers via pop_sm_lease)
    std::string cursor_id;           // UUID of last consumed message
    std::string cursor_ts_str;       // Timestamp as string for SQL parameter
    
    // Lease info
    std::string lease_id;            // Generated UUID for this lease
    
    // State
    PopState state = PopState::PENDING;
    ConnectionSlot* assigned_slot = nullptr;
    
    // Query tracking
    enum class QueryType {
        NONE,
        RESOLVE,
        LEASE,
        FETCH,
        RELEASE,
        UPDATE_CURSOR
    };
    QueryType pending_query = QueryType::NONE;
    
    // Results
    nlohmann::json messages;
    std::string error_message;
    
    // Timing
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point completed_at;
    
    // Is this request terminal (done processing)?
    bool is_terminal() const {
        return state == PopState::COMPLETED || 
               state == PopState::EMPTY || 
               state == PopState::FAILED;
    }
};

/**
 * State machine for processing a batch of POP requests in parallel.
 * 
 * Key Design:
 * - Each request goes through states: PENDING -> [RESOLVING] -> LEASING -> FETCHING -> COMPLETED
 * - Different partitions can be processed in parallel on different connections
 * - State transitions are driven by query completions
 * - Completion callback is called when all requests are terminal
 */
class PopBatchStateMachine : public std::enable_shared_from_this<PopBatchStateMachine> {
public:
    using CompletionCallback = std::function<void(std::vector<PopRequestState>&)>;
    
    /**
     * Create a state machine for a batch of POP requests
     * @param requests Vector of POP requests to process
     * @param on_complete Callback when all requests are complete
     * @param worker_id Worker ID for logging
     */
    PopBatchStateMachine(
        std::vector<PopRequestState> requests,
        CompletionCallback on_complete,
        int worker_id = -1
    );
    
    ~PopBatchStateMachine() = default;
    
    // Non-copyable, non-movable (due to atomic member)
    PopBatchStateMachine(const PopBatchStateMachine&) = delete;
    PopBatchStateMachine& operator=(const PopBatchStateMachine&) = delete;
    PopBatchStateMachine(PopBatchStateMachine&&) = delete;
    PopBatchStateMachine& operator=(PopBatchStateMachine&&) = delete;
    
    /**
     * Called by sidecar when a connection becomes available.
     * Assigns connection to next pending request and starts query.
     * @param slot The available connection slot
     * @return true if slot was assigned to a request, false if no pending requests
     */
    bool assign_connection(ConnectionSlot* slot);
    
    /**
     * Called when a query completes on a connection slot.
     * Parses result, advances state, and potentially submits next query.
     * @param slot The slot that completed
     * @param result The PostgreSQL result (ownership remains with caller)
     */
    void on_query_complete(ConnectionSlot* slot, PGresult* result);
    
    /**
     * Called on query error for a connection slot.
     * Marks request as failed and releases connection.
     * @param slot The slot that errored
     * @param error Error message
     */
    void on_query_error(ConnectionSlot* slot, const std::string& error);
    
    /**
     * Check if all requests are complete (terminal state)
     */
    bool is_complete() const;
    
    /**
     * Get count of requests that need a connection (PENDING state)
     */
    int pending_count() const;
    
    /**
     * Get count of requests currently executing queries
     */
    int active_count() const;
    
    /**
     * Get the requests (for inspection/debugging)
     */
    const std::vector<PopRequestState>& requests() const { return requests_; }
    
    /**
     * Unique ID for this state machine (for logging)
     */
    const std::string& id() const { return id_; }
    
private:
    std::string id_;  // Unique ID for logging
    std::vector<PopRequestState> requests_;
    CompletionCallback on_complete_;
    int worker_id_;
    
    std::chrono::steady_clock::time_point created_at_;
    
    // Tracking
    std::atomic<int> completed_count_{0};
    
    /**
     * Generate a unique UUID for lease identification
     */
    static std::string generate_uuid();
    
    /**
     * Find the request associated with a slot (by pointer comparison)
     */
    PopRequestState* find_request_for_slot(ConnectionSlot* slot);
    
    /**
     * Advance the state of a request after a query completes.
     * May submit the next query if there's more work.
     * Returns true if a new query was submitted.
     */
    bool advance_state(PopRequestState& req);
    
    /**
     * Submit queries to PostgreSQL
     */
    void submit_resolve_query(PopRequestState& req);
    void submit_lease_query(PopRequestState& req);
    void submit_fetch_query(PopRequestState& req);
    void submit_release_query(PopRequestState& req);
    
    /**
     * Parse query results
     */
    void handle_resolve_result(PopRequestState& req, PGresult* result);
    void handle_lease_result(PopRequestState& req, PGresult* result);
    void handle_fetch_result(PopRequestState& req, PGresult* result);
    void handle_release_result(PopRequestState& req, PGresult* result);
    
    /**
     * Mark request as complete and release its slot
     */
    void mark_complete(PopRequestState& req, PopState final_state);
    
    /**
     * Release the connection slot back to the pool
     */
    void release_slot(PopRequestState& req);
    
    /**
     * Check if all requests are done and call completion callback
     */
    void check_completion();
    
    /**
     * Build the final response message for a completed request
     */
    void finalize_response(PopRequestState& req);
};

} // namespace queen

