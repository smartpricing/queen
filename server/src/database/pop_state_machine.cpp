#include "queen/pop_state_machine.hpp"  // Includes sidecar_db_pool.hpp for ConnectionSlot
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace queen {

// Helper: Flush any pending results on a connection to ensure clean state
static void flush_pending_results(PGconn* conn, int worker_id) {
    if (!conn) return;
    
    // Consume input first
    PQconsumeInput(conn);
    
    // If connection is busy, wait for it to finish
    while (PQisBusy(conn)) {
        PQconsumeInput(conn);
    }
    
    // Drain all pending results
    PGresult* result;
    int flushed = 0;
    while ((result = PQgetResult(conn)) != nullptr) {
        flushed++;
        PQclear(result);
    }
    
    if (flushed > 0) {
        spdlog::warn("[Worker {}] Flushed {} stale results from connection", worker_id, flushed);
    }
}

// ============================================================================
// UUID Generation (UUIDv7 format for time-ordering)
// ============================================================================

std::string PopBatchStateMachine::generate_uuid() {
    static std::mutex uuid_mutex;
    static uint64_t last_ms = 0;
    static uint16_t sequence = 0;
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    
    std::lock_guard<std::mutex> lock(uuid_mutex);
    
    auto now = std::chrono::system_clock::now();
    uint64_t current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    if (current_ms <= last_ms) {
        sequence++;
    } else {
        last_ms = current_ms;
        sequence = 0; 
    }

    std::array<uint8_t, 16> bytes;

    // 48-bit unix_ts_ms (big-endian)
    bytes[0] = (last_ms >> 40) & 0xFF;
    bytes[1] = (last_ms >> 32) & 0xFF;
    bytes[2] = (last_ms >> 24) & 0xFF;
    bytes[3] = (last_ms >> 16) & 0xFF;
    bytes[4] = (last_ms >> 8) & 0xFF;
    bytes[5] = last_ms & 0xFF;

    // 4-bit version (0111) and 12-bit sequence
    uint16_t sequence_and_version = sequence & 0x0FFF;
    bytes[6] = 0x70 | (sequence_and_version >> 8);
    bytes[7] = sequence_and_version & 0xFF;

    // 2-bit variant (10) and 62-bits of random data
    uint64_t rand_data = gen();
    bytes[8] = 0x80 | ((rand_data >> 56) & 0x3F);
    bytes[9] = (rand_data >> 48) & 0xFF;
    bytes[10] = (rand_data >> 40) & 0xFF;
    bytes[11] = (rand_data >> 32) & 0xFF;
    bytes[12] = (rand_data >> 24) & 0xFF;
    bytes[13] = (rand_data >> 16) & 0xFF;
    bytes[14] = (rand_data >> 8) & 0xFF;
    bytes[15] = rand_data & 0xFF;

    // Format to string
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) ss << '-';
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    
    return ss.str();
}

// ============================================================================
// Constructor
// ============================================================================

PopBatchStateMachine::PopBatchStateMachine(
    std::vector<PopRequestState> requests,
    CompletionCallback on_complete,
    int worker_id)
    : requests_(std::move(requests))
    , on_complete_(std::move(on_complete))
    , worker_id_(worker_id)
    , created_at_(std::chrono::steady_clock::now()) {
    
    // Generate unique ID for this state machine (use last 8 chars - random portion)
    std::string uuid = generate_uuid();
    id_ = uuid.substr(uuid.length() - 8);  // Last 8 chars are random
    
    // Initialize all requests
    for (auto& req : requests_) {
        req.lease_id = generate_uuid();
        req.started_at = created_at_;
        req.state = PopState::PENDING;
        req.messages = nlohmann::json::array();
    }
    
    spdlog::debug("[Worker {}] [SM {}] Created with {} requests", 
                  worker_id_, id_, requests_.size());
}

// ============================================================================
// Connection Assignment
// ============================================================================

bool PopBatchStateMachine::assign_connection(ConnectionSlot* slot) {
    // Find first PENDING request that doesn't have a slot
    for (auto& req : requests_) {
        if (req.state == PopState::PENDING && req.assigned_slot == nullptr) {
            req.assigned_slot = slot;
            slot->state_machine_request = &req;
            
            spdlog::debug("[Worker {}] [SM {}] Assigned slot to request {} (queue={}, partition={})", 
                          worker_id_, id_, req.idx, req.queue_name, 
                          req.partition_name.empty() ? "*" : req.partition_name);
            
            // Start processing this request
            advance_state(req);
            return true;
        }
    }
    return false;
}

// ============================================================================
// Query Completion Handler
// ============================================================================

void PopBatchStateMachine::on_query_complete(ConnectionSlot* slot, PGresult* result) {
    PopRequestState* req = find_request_for_slot(slot);
    if (!req) {
        spdlog::warn("[Worker {}] [SM {}] Query complete but no request found for slot", 
                     worker_id_, id_);
        return;
    }
    
    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        std::string error = PQresultErrorMessage(result);
        spdlog::error("[Worker {}] [SM {}] Query failed for request {}: {}", 
                      worker_id_, id_, req->idx, error);
        req->error_message = error;
        mark_complete(*req, PopState::FAILED);
        return;
    }
    
    // Handle result based on pending query type
    switch (req->pending_query) {
        case PopRequestState::QueryType::RESOLVE:
            handle_resolve_result(*req, result);
            break;
        case PopRequestState::QueryType::LEASE:
            handle_lease_result(*req, result);
            break;
        case PopRequestState::QueryType::FETCH:
            handle_fetch_result(*req, result);
            break;
        case PopRequestState::QueryType::RELEASE:
            handle_release_result(*req, result);
            break;
        default:
            spdlog::warn("[Worker {}] [SM {}] Unexpected query completion for request {}", 
                         worker_id_, id_, req->idx);
            break;
    }
    
    req->pending_query = PopRequestState::QueryType::NONE;
    
    // Advance to next state (may submit another query)
    if (!req->is_terminal()) {
        advance_state(*req);
    }
    
    check_completion();
}

// ============================================================================
// Error Handler
// ============================================================================

void PopBatchStateMachine::on_query_error(ConnectionSlot* slot, const std::string& error) {
    PopRequestState* req = find_request_for_slot(slot);
    if (!req) {
        spdlog::warn("[Worker {}] [SM {}] Query error but no request found for slot: {}", 
                     worker_id_, id_, error);
        return;
    }
    
    spdlog::error("[Worker {}] [SM {}] Query error for request {}: {}", 
                  worker_id_, id_, req->idx, error);
    
    req->error_message = error;
    mark_complete(*req, PopState::FAILED);
    check_completion();
}

// ============================================================================
// State Queries
// ============================================================================

bool PopBatchStateMachine::is_complete() const {
    for (const auto& req : requests_) {
        if (!req.is_terminal()) {
            return false;
        }
    }
    return true;
}

int PopBatchStateMachine::pending_count() const {
    int count = 0;
    for (const auto& req : requests_) {
        if (req.state == PopState::PENDING && req.assigned_slot == nullptr) {
            count++;
        }
    }
    return count;
}

int PopBatchStateMachine::active_count() const {
    int count = 0;
    for (const auto& req : requests_) {
        if (!req.is_terminal() && req.assigned_slot != nullptr) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// Private: Find Request for Slot
// ============================================================================

PopRequestState* PopBatchStateMachine::find_request_for_slot(ConnectionSlot* slot) {
    if (!slot || !slot->state_machine_request) {
        return nullptr;
    }
    return static_cast<PopRequestState*>(slot->state_machine_request);
}

// ============================================================================
// Private: State Advancement
// ============================================================================

bool PopBatchStateMachine::advance_state(PopRequestState& req) {
    switch (req.state) {
        case PopState::PENDING:
            if (req.partition_name.empty()) {
                // Wildcard: need to resolve partition first
                req.state = PopState::RESOLVING;
                submit_resolve_query(req);
                return true;
            } else {
                // Specific partition: go directly to leasing
                req.state = PopState::LEASING;
                submit_lease_query(req);
                return true;
            }
            
        case PopState::RESOLVING:
            // After resolve, try to lease if we found a partition
            if (!req.partition_id.empty()) {
                req.state = PopState::LEASING;
                submit_lease_query(req);
                return true;
            } else {
                // No partition available
                mark_complete(req, PopState::EMPTY);
                return false;
            }
            
        case PopState::LEASING:
            // After lease acquired, fetch messages
            if (!req.partition_id.empty()) {
                req.state = PopState::FETCHING;
                submit_fetch_query(req);
                return true;
            } else {
                // Lease not acquired (already taken)
                mark_complete(req, PopState::EMPTY);
                return false;
            }
            
        case PopState::FETCHING:
            // After fetch, we're done
            // Note: pop_sm_fetch now updates batch_size internally to actual count
            if (req.messages.empty() || req.messages.size() == 0) {
                // No messages - release the lease
                req.state = PopState::RELEASING;
                submit_release_query(req);
                return true;
            } else {
                // Has messages - complete successfully
                finalize_response(req);
                mark_complete(req, PopState::COMPLETED);
                return false;
            }
            
        case PopState::RELEASING:
            // After release, we're done (empty)
            mark_complete(req, PopState::EMPTY);
            return false;
            
        default:
            // Terminal states - nothing to do
            return false;
    }
}

// ============================================================================
// Private: Submit Queries
// ============================================================================

void PopBatchStateMachine::submit_resolve_query(PopRequestState& req) {
    if (!req.assigned_slot || !req.assigned_slot->conn) {
        req.error_message = "No connection available";
        mark_complete(req, PopState::FAILED);
        return;
    }
    
    // Ensure connection is clean before sending new query
    flush_pending_results(req.assigned_slot->conn, worker_id_);
    
    spdlog::debug("[Worker {}] [SM {}] Submitting RESOLVE query for request {} (queue={})", 
                  worker_id_, id_, req.idx, req.queue_name);
    
    const char* sql = "SELECT out_partition_id::text, out_partition_name, out_queue_id::text, "
                      "out_lease_time, out_window_buffer, out_delayed_processing "
                      "FROM queen.pop_sm_resolve($1, $2)";
    
    const char* params[] = {
        req.queue_name.c_str(),
        req.consumer_group.c_str()
    };
    
    int sent = PQsendQueryParams(req.assigned_slot->conn, sql, 2, 
                                  nullptr, params, nullptr, nullptr, 0);
    if (!sent) {
        req.error_message = PQerrorMessage(req.assigned_slot->conn);
        mark_complete(req, PopState::FAILED);
        return;
    }
    
    req.pending_query = PopRequestState::QueryType::RESOLVE;
}

void PopBatchStateMachine::submit_lease_query(PopRequestState& req) {
    if (!req.assigned_slot || !req.assigned_slot->conn) {
        req.error_message = "No connection available";
        mark_complete(req, PopState::FAILED);
        return;
    }
    
    // Ensure connection is clean before sending new query
    flush_pending_results(req.assigned_slot->conn, worker_id_);
    
    spdlog::debug("[Worker {}] [SM {}] Submitting LEASE query for request {} (queue={}, partition={})", 
                  worker_id_, id_, req.idx, req.queue_name, req.partition_name);
    
    const char* sql = "SELECT out_partition_id::text, out_cursor_id::text, out_cursor_ts, "
                      "out_queue_lease_time, out_window_buffer, out_delayed_processing "
                      "FROM queen.pop_sm_lease($1, $2, $3, $4, $5, $6, $7, $8)";
    
    std::string lease_seconds_str = std::to_string(req.lease_seconds);
    std::string batch_size_str = std::to_string(req.batch_size);
    
    const char* params[] = {
        req.queue_name.c_str(),
        req.partition_name.c_str(),
        req.consumer_group.c_str(),
        lease_seconds_str.c_str(),
        req.lease_id.c_str(),
        batch_size_str.c_str(),
        req.subscription_mode.c_str(),
        req.subscription_from.c_str()
    };
    
    int sent = PQsendQueryParams(req.assigned_slot->conn, sql, 8, 
                                  nullptr, params, nullptr, nullptr, 0);
    if (!sent) {
        req.error_message = PQerrorMessage(req.assigned_slot->conn);
        mark_complete(req, PopState::FAILED);
        return;
    }
    
    req.pending_query = PopRequestState::QueryType::LEASE;
}

void PopBatchStateMachine::submit_fetch_query(PopRequestState& req) {
    if (!req.assigned_slot || !req.assigned_slot->conn) {
        req.error_message = "No connection available";
        mark_complete(req, PopState::FAILED);
        return;
    }
    
    // Ensure connection is clean before sending new query
    flush_pending_results(req.assigned_slot->conn, worker_id_);
    
    spdlog::debug("[Worker {}] [SM {}] Submitting FETCH query for request {} (partition_id={})", 
                  worker_id_, id_, req.idx, req.partition_id);
    
    const char* sql = "SELECT queen.pop_sm_fetch($1::uuid, $2::timestamptz, $3::uuid, $4, $5, $6)";
    
    std::string batch_size_str = std::to_string(req.batch_size);
    std::string window_buffer_str = std::to_string(req.window_buffer);
    std::string delayed_processing_str = std::to_string(req.delayed_processing);
    
    // Handle NULL cursor values
    const char* cursor_ts_ptr = req.cursor_ts_str.empty() ? nullptr : req.cursor_ts_str.c_str();
    const char* cursor_id_ptr = req.cursor_id.empty() ? nullptr : req.cursor_id.c_str();
    
    const char* params[] = {
        req.partition_id.c_str(),
        cursor_ts_ptr,
        cursor_id_ptr,
        batch_size_str.c_str(),
        window_buffer_str.c_str(),
        delayed_processing_str.c_str()
    };
    
    int sent = PQsendQueryParams(req.assigned_slot->conn, sql, 6, 
                                  nullptr, params, nullptr, nullptr, 0);
    if (!sent) {
        req.error_message = PQerrorMessage(req.assigned_slot->conn);
        mark_complete(req, PopState::FAILED);
        return;
    }
    
    req.pending_query = PopRequestState::QueryType::FETCH;
}

void PopBatchStateMachine::submit_release_query(PopRequestState& req) {
    if (!req.assigned_slot || !req.assigned_slot->conn) {
        req.error_message = "No connection available";
        mark_complete(req, PopState::FAILED);
        return;
    }
    
    // Ensure connection is clean before sending new query
    flush_pending_results(req.assigned_slot->conn, worker_id_);
    
    spdlog::debug("[Worker {}] [SM {}] Submitting RELEASE query for request {} (partition_id={})", 
                  worker_id_, id_, req.idx, req.partition_id);
    
    const char* sql = "SELECT queen.pop_sm_release($1::uuid, $2, $3)";
    
    const char* params[] = {
        req.partition_id.c_str(),
        req.consumer_group.c_str(),
        req.lease_id.c_str()
    };
    
    int sent = PQsendQueryParams(req.assigned_slot->conn, sql, 3, 
                                  nullptr, params, nullptr, nullptr, 0);
    if (!sent) {
        req.error_message = PQerrorMessage(req.assigned_slot->conn);
        mark_complete(req, PopState::FAILED);
        return;
    }
    
    req.pending_query = PopRequestState::QueryType::RELEASE;
}

// ============================================================================
// Private: Handle Query Results
// ============================================================================

void PopBatchStateMachine::handle_resolve_result(PopRequestState& req, PGresult* result) {
    if (PQntuples(result) == 0) {
        // No available partition found
        spdlog::debug("[Worker {}] [SM {}] RESOLVE returned no partition for request {}", 
                      worker_id_, id_, req.idx);
        req.partition_id.clear();
        return;
    }
    
    // Extract resolved partition info
    // Columns: partition_id, partition_name, queue_id, lease_time, window_buffer, delayed_processing
    req.partition_id = PQgetisnull(result, 0, 0) ? "" : PQgetvalue(result, 0, 0);
    req.partition_name = PQgetisnull(result, 0, 1) ? "" : PQgetvalue(result, 0, 1);
    req.queue_id = PQgetisnull(result, 0, 2) ? "" : PQgetvalue(result, 0, 2);
    req.queue_lease_time = PQgetisnull(result, 0, 3) ? 0 : std::atoi(PQgetvalue(result, 0, 3));
    req.window_buffer = PQgetisnull(result, 0, 4) ? 0 : std::atoi(PQgetvalue(result, 0, 4));
    req.delayed_processing = PQgetisnull(result, 0, 5) ? 0 : std::atoi(PQgetvalue(result, 0, 5));
    
    spdlog::debug("[Worker {}] [SM {}] RESOLVE found partition {} for request {}", 
                  worker_id_, id_, req.partition_name, req.idx);
}

void PopBatchStateMachine::handle_lease_result(PopRequestState& req, PGresult* result) {
    if (PQntuples(result) == 0) {
        // Lease not acquired (already taken by another consumer)
        spdlog::debug("[Worker {}] [SM {}] LEASE not acquired for request {} (partition={})", 
                      worker_id_, id_, req.idx, req.partition_name);
        req.partition_id.clear();
        return;
    }
    
    // Extract cursor position and queue config
    // Columns: partition_id, cursor_id, cursor_ts, queue_lease_time, window_buffer, delayed_processing
    req.partition_id = PQgetisnull(result, 0, 0) ? "" : PQgetvalue(result, 0, 0);
    req.cursor_id = PQgetisnull(result, 0, 1) ? "" : PQgetvalue(result, 0, 1);
    req.cursor_ts_str = PQgetisnull(result, 0, 2) ? "" : PQgetvalue(result, 0, 2);
    req.queue_lease_time = PQgetisnull(result, 0, 3) ? 0 : std::atoi(PQgetvalue(result, 0, 3));
    req.window_buffer = PQgetisnull(result, 0, 4) ? 0 : std::atoi(PQgetvalue(result, 0, 4));
    req.delayed_processing = PQgetisnull(result, 0, 5) ? 0 : std::atoi(PQgetvalue(result, 0, 5));
    
    spdlog::debug("[Worker {}] [SM {}] LEASE acquired for request {} (partition={}, cursor_ts={})", 
                  worker_id_, id_, req.idx, req.partition_name, 
                  req.cursor_ts_str.empty() ? "NULL" : req.cursor_ts_str);
}

void PopBatchStateMachine::handle_fetch_result(PopRequestState& req, PGresult* result) {
    // pop_sm_fetch returns a JSONB array of messages
    if (PQntuples(result) == 0 || PQgetisnull(result, 0, 0)) {
        req.messages = nlohmann::json::array();
        spdlog::debug("[Worker {}] [SM {}] FETCH returned no messages for request {}", 
                      worker_id_, id_, req.idx);
        return;
    }
    
    const char* json_str = PQgetvalue(result, 0, 0);
    try {
        req.messages = nlohmann::json::parse(json_str);
        spdlog::debug("[Worker {}] [SM {}] FETCH returned {} messages for request {}", 
                      worker_id_, id_, req.messages.size(), req.idx);
    } catch (const std::exception& e) {
        spdlog::error("[Worker {}] [SM {}] Failed to parse FETCH result: {}", 
                      worker_id_, id_, e.what());
        req.messages = nlohmann::json::array();
    }
}

void PopBatchStateMachine::handle_release_result(PopRequestState& req, PGresult* result) {
    // Release result is just a boolean, we don't really need to check it
    (void)result;
    spdlog::debug("[Worker {}] [SM {}] RELEASE completed for request {}", 
                  worker_id_, id_, req.idx);
}

// ============================================================================
// Private: Completion Handling
// ============================================================================

void PopBatchStateMachine::mark_complete(PopRequestState& req, PopState final_state) {
    req.state = final_state;
    req.completed_at = std::chrono::steady_clock::now();
    
    // Release the slot
    release_slot(req);
    
    completed_count_++;
    
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        req.completed_at - req.started_at).count();
    
    spdlog::debug("[Worker {}] [SM {}] Request {} completed with state {} in {}ms", 
                  worker_id_, id_, req.idx, pop_state_to_string(final_state), duration_ms);
}

void PopBatchStateMachine::release_slot(PopRequestState& req) {
    if (req.assigned_slot) {
        req.assigned_slot->state_machine_request = nullptr;
        req.assigned_slot = nullptr;
    }
}

void PopBatchStateMachine::check_completion() {
    if (!is_complete()) {
        return;
    }
    
    auto total_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - created_at_).count();
    
    // Build context string from requests and count total messages
    std::string context;
    size_t total_messages = 0;
    if (!requests_.empty()) {
        const auto& req = requests_[0];
        context = req.queue_name;
        if (!req.partition_name.empty()) {
            context += "/" + req.partition_name;
        }
        if (req.consumer_group != "__QUEUE_MODE__" && !req.consumer_group.empty()) {
            context += "/" + req.consumer_group;
        }
        
        // Count total messages across all requests
        for (const auto& r : requests_) {
            total_messages += r.messages.size();
        }
    }
    
    spdlog::info("[Worker {}] [SM {}] All {} requests completed in {}ms | {} | {} msgs", 
                 worker_id_, id_, requests_.size(), total_duration_ms, context, total_messages);
    
    if (on_complete_) {
        on_complete_(requests_);
    }
}

void PopBatchStateMachine::finalize_response(PopRequestState& req) {
    // Add additional fields to each message
    for (auto& msg : req.messages) {
        msg["queue"] = req.queue_name;
        msg["partition"] = req.partition_name;
        msg["partitionId"] = req.partition_id;
        msg["leaseId"] = req.lease_id;
        if (req.consumer_group != "__QUEUE_MODE__") {
            msg["consumerGroup"] = req.consumer_group;
        }
    }
}

} // namespace queen

