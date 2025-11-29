#include "queen/sidecar_db_pool.hpp"
#include <spdlog/spdlog.h>
#include <sys/select.h>
#include <algorithm>
#include <cstring>

namespace queen {

SidecarDbPool::SidecarDbPool(const std::string& conn_str, 
                             int pool_size,
                             int statement_timeout_ms)
    : conn_str_(conn_str),
      pool_size_(pool_size),
      statement_timeout_ms_(statement_timeout_ms) {
    
    slots_.resize(pool_size_);
    spdlog::info("[SidecarDbPool] Created with {} connection slots", pool_size_);
}

SidecarDbPool::~SidecarDbPool() {
    stop();
    for (auto& slot : slots_) {
        disconnect_slot(slot);
    }
    spdlog::info("[SidecarDbPool] Destroyed");
}

void SidecarDbPool::start() {
    if (running_) return;
    
    // Connect all slots
    spdlog::info("[SidecarDbPool] Connecting {} database connections...", pool_size_);
    int connected = 0;
    for (auto& slot : slots_) {
        if (connect_slot(slot)) {
            connected++;
        }
    }
    spdlog::info("[SidecarDbPool] Connected {}/{} database connections", connected, pool_size_);
    
    if (connected == 0) {
        spdlog::error("[SidecarDbPool] Failed to connect any database connections!");
        return;
    }
    
    // Start poller thread
    running_ = true;
    poller_thread_ = std::thread(&SidecarDbPool::poller_loop, this);
    spdlog::info("[SidecarDbPool] Poller thread started");
}

void SidecarDbPool::stop() {
    if (!running_) return;
    
    spdlog::info("[SidecarDbPool] Stopping...");
    running_ = false;
    
    if (poller_thread_.joinable()) {
        poller_thread_.join();
    }
    
    spdlog::info("[SidecarDbPool] Stopped");
}

bool SidecarDbPool::connect_slot(ConnectionSlot& slot) {
    slot.conn = PQconnectdb(conn_str_.c_str());
    
    if (PQstatus(slot.conn) != CONNECTION_OK) {
        spdlog::error("[SidecarDbPool] Connection failed: {}", PQerrorMessage(slot.conn));
        PQfinish(slot.conn);
        slot.conn = nullptr;
        return false;
    }
    
    // Set non-blocking mode
    if (PQsetnonblocking(slot.conn, 1) != 0) {
        spdlog::error("[SidecarDbPool] Failed to set non-blocking: {}", PQerrorMessage(slot.conn));
        PQfinish(slot.conn);
        slot.conn = nullptr;
        return false;
    }
    
    // Set statement timeout
    std::string timeout_sql = "SET statement_timeout = " + std::to_string(statement_timeout_ms_);
    PGresult* res = PQexec(slot.conn, timeout_sql.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::warn("[SidecarDbPool] Failed to set statement timeout: {}", PQerrorMessage(slot.conn));
    }
    PQclear(res);
    
    slot.socket_fd = PQsocket(slot.conn);
    slot.busy = false;
    
    return true;
}

void SidecarDbPool::disconnect_slot(ConnectionSlot& slot) {
    if (slot.conn) {
        PQfinish(slot.conn);
        slot.conn = nullptr;
        slot.socket_fd = -1;
        slot.busy = false;
    }
}

void SidecarDbPool::submit(SidecarRequest request) {
    request.queued_at = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.push_back(std::move(request));
    }
}

size_t SidecarDbPool::pop_responses(std::vector<SidecarResponse>& out, size_t max_count) {
    std::lock_guard<std::mutex> lock(completed_mutex_);
    
    size_t count = max_count > 0 ? 
        std::min(max_count, completed_responses_.size()) : 
        completed_responses_.size();
    
    for (size_t i = 0; i < count; ++i) {
        out.push_back(std::move(completed_responses_.front()));
        completed_responses_.pop_front();
    }
    
    return count;
}

bool SidecarDbPool::has_responses() const {
    std::lock_guard<std::mutex> lock(completed_mutex_);
    return !completed_responses_.empty();
}

// Helper: Check if an operation type supports micro-batching
// NOTE: POP is NOT batchable because each POP needs its own transaction
// to properly acquire a lease. Batching POPs causes only the first one
// to succeed (it acquires the lease, making subsequent ones fail).
static bool is_batchable_op(SidecarOpType op) {
    switch (op) {
        case SidecarOpType::PUSH:
        case SidecarOpType::ACK_BATCH:
        case SidecarOpType::RENEW_LEASE:
            return true;
        case SidecarOpType::ACK:
        case SidecarOpType::POP:         // Each POP needs its own transaction for lease acquisition
        case SidecarOpType::TRANSACTION:
            return false;
    }
    return false;
}

// Helper: Get SQL and params for a batched operation
static std::pair<std::string, std::vector<const char*>> get_batched_sql(
    SidecarOpType op_type, 
    const std::string& combined_json,
    std::vector<std::string>& param_storage) {
    
    param_storage.clear();
    std::vector<const char*> params;
    
    switch (op_type) {
        case SidecarOpType::PUSH:
            param_storage = {combined_json, "true", "true"};
            return {"SELECT queen.push_messages_v2($1::jsonb, $2::boolean, $3::boolean)", 
                    {param_storage[0].c_str(), param_storage[1].c_str(), param_storage[2].c_str()}};
            
        case SidecarOpType::POP:
            param_storage = {combined_json};
            return {"SELECT queen.pop_messages_batch_v2($1::jsonb)",
                    {param_storage[0].c_str()}};
            
        case SidecarOpType::ACK_BATCH:
            param_storage = {combined_json};
            return {"SELECT queen.ack_messages_v2($1::jsonb)",
                    {param_storage[0].c_str()}};
            
        case SidecarOpType::RENEW_LEASE:
            param_storage = {combined_json};
            return {"SELECT queen.renew_lease_v2($1::jsonb)",
                    {param_storage[0].c_str()}};
            
        default:
            return {"", {}};
    }
}

void SidecarDbPool::poller_loop() {
    spdlog::info("[SidecarDbPool] Poller loop started with MULTI-OP SUPPORT enabled");
    
    constexpr int MICRO_BATCH_WAIT_MS = 5;  // Wait for micro-batching
    constexpr int MAX_ITEMS_PER_TX = 100;   // Max items per batch
    
    while (running_) {
        // ============================================================
        // STEP 1: MICRO-BATCH COLLECTION & SEND
        // ============================================================
        {
            // Check if we have pending requests
            bool has_pending = false;
            size_t pending_count = 0;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                has_pending = !pending_requests_.empty();
                pending_count = pending_requests_.size();
            }
            
            // If we have pending requests, wait a bit to collect more (micro-batching)
            if (MICRO_BATCH_WAIT_MS > 0 && has_pending && pending_count < 5) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MICRO_BATCH_WAIT_MS));
            }
            
            // Now drain and batch all collected requests
            std::lock_guard<std::mutex> lock(pending_mutex_);
            
            while (!pending_requests_.empty()) {
                // Find a free slot
                ConnectionSlot* free_slot = nullptr;
                for (auto& slot : slots_) {
                    if (!slot.busy && slot.conn) {
                        free_slot = &slot;
                        break;
                    }
                }
                
                if (!free_slot) {
                    // All connections busy - stop draining
                    break;
                }
                
                // Get the first request to determine operation type
                SidecarOpType batch_op_type = pending_requests_.front().op_type;
                
                // For non-batchable operations (TRANSACTION), send one at a time
                if (!is_batchable_op(batch_op_type)) {
                    auto req = std::move(pending_requests_.front());
                    pending_requests_.pop_front();
                    
                    // Send single request directly
                    std::vector<const char*> param_ptrs;
                    for (const auto& p : req.params) {
                        param_ptrs.push_back(p.c_str());
                    }
                    
                    int sent = PQsendQueryParams(
                        free_slot->conn,
                        req.sql.c_str(),
                        static_cast<int>(param_ptrs.size()),
                        nullptr, param_ptrs.data(),
                        nullptr, nullptr, 0
                    );
                    
                    if (sent) {
                        // Flush
                        int flush_result;
                        while ((flush_result = PQflush(free_slot->conn)) == 1) {
                            fd_set write_fds;
                            FD_ZERO(&write_fds);
                            FD_SET(free_slot->socket_fd, &write_fds);
                            struct timeval tv = {0, 1000};
                            select(free_slot->socket_fd + 1, nullptr, &write_fds, nullptr, &tv);
                        }
                        
                        if (flush_result == -1) {
                            SidecarResponse error_resp;
                            error_resp.op_type = req.op_type;
                            error_resp.request_id = req.request_id;
                            error_resp.worker_id = req.worker_id;
                            error_resp.success = false;
                            error_resp.error_message = "PQflush failed";
                            
                            std::lock_guard<std::mutex> resp_lock(completed_mutex_);
                            completed_responses_.push_back(std::move(error_resp));
                            continue;
                        }
                        
                        free_slot->busy = true;
                        free_slot->is_batched = false;
                        free_slot->request_id = req.request_id;
                        free_slot->worker_id = req.worker_id;
                        free_slot->op_type = req.op_type;
                        free_slot->total_items = req.item_count;
                        free_slot->query_start = std::chrono::steady_clock::now();
                        total_queries_++;
                    } else {
                        SidecarResponse error_resp;
                        error_resp.op_type = req.op_type;
                        error_resp.request_id = req.request_id;
                        error_resp.worker_id = req.worker_id;
                        error_resp.success = false;
                        error_resp.error_message = "PQsendQuery failed";
                        
                        std::lock_guard<std::mutex> resp_lock(completed_mutex_);
                        completed_responses_.push_back(std::move(error_resp));
                    }
                    continue;
                }
                
                // Collect requests of the SAME operation type into one micro-batch
                std::vector<SidecarRequest> batch;
                size_t total_items_in_batch = 0;
                
                while (!pending_requests_.empty() && batch.size() < 1000) {
                    auto& next_req = pending_requests_.front();
                    
                    // Only batch same operation type
                    if (next_req.op_type != batch_op_type) {
                        break;
                    }
                    
                    if (total_items_in_batch > 0 && 
                        (total_items_in_batch + next_req.item_count > MAX_ITEMS_PER_TX)) {
                        break; 
                    }
                
                    total_items_in_batch += next_req.item_count;                    
                    batch.push_back(std::move(pending_requests_.front()));
                    pending_requests_.pop_front();
                }
                
                if (batch.empty()) break;
                
                // ============================================================
                // STRING SPLICING: Combine JSON arrays WITHOUT parsing
                // Each req.params[0] is "[{...},{...}]" - we strip brackets and concatenate
                // ============================================================
                std::string combined_json;
                combined_json.reserve(2 * 1024 * 1024);  // 2MB reserve for large batches
                combined_json += "[";
                
                std::vector<BatchedRequestInfo> batch_info;
                size_t current_index = 0;
                bool first_item = true;
                
                for (const auto& req : batch) {
                    if (req.params.empty()) continue;
                    
                    const std::string& raw = req.params[0];
                    
                    // Minimum valid JSON array is "[]" (2 chars)
                    if (raw.size() < 2) continue;
                    
                    // Strip outer brackets: "[{...}]" -> "{...}"
                    size_t start = raw.find('[');
                    size_t end = raw.rfind(']');
                    
                    if (start == std::string::npos || end == std::string::npos || end <= start) {
                        spdlog::error("[SidecarDbPool] Invalid JSON array format for request {}", req.request_id);
                        SidecarResponse error_resp;
                        error_resp.op_type = req.op_type;
                        error_resp.request_id = req.request_id;
                        error_resp.worker_id = req.worker_id;
                        error_resp.success = false;
                        error_resp.error_message = "Invalid JSON array format";
                        
                        std::lock_guard<std::mutex> resp_lock(completed_mutex_);
                        completed_responses_.push_back(std::move(error_resp));
                        continue;
                    }
                    
                    // Extract inner content (between [ and ])
                    std::string_view inner(raw.data() + start + 1, end - start - 1);
                    
                    // Skip empty arrays
                    bool has_content = false;
                    for (char c : inner) {
                        if (!std::isspace(static_cast<unsigned char>(c))) {
                            has_content = true;
                            break;
                        }
                    }
                    
                    if (!has_content) continue;
                    
                    // Track this request's range in the combined batch
                    BatchedRequestInfo info;
                    info.request_id = req.request_id;
                    info.worker_id = req.worker_id;
                    info.start_index = current_index;
                    info.item_count = req.item_count;
                    batch_info.push_back(info);
                    
                    // Append with comma separator
                    if (!first_item) {
                        combined_json += ",";
                    }
                    combined_json.append(inner.data(), inner.size());
                    first_item = false;
                    
                    current_index += req.item_count;
                }
                
                combined_json += "]";
                
                if (batch_info.empty() || current_index == 0) {
                    continue;
                }
                
                spdlog::info("[SidecarDbPool] MICRO-BATCH: {} op, {} requests, {} items", 
                            static_cast<int>(batch_op_type), batch_info.size(), current_index);
                
                // Get SQL for this operation type
                std::vector<std::string> param_storage;
                auto [sql, param_ptrs_vec] = get_batched_sql(batch_op_type, combined_json, param_storage);
                
                // Convert to C array
                std::vector<const char*> param_values;
                for (const auto& s : param_storage) {
                    param_values.push_back(s.c_str());
                }
                
                // SEND QUERY (non-blocking!)
                int sent = PQsendQueryParams(
                    free_slot->conn,
                    sql.c_str(),
                    static_cast<int>(param_values.size()),
                    nullptr,
                    param_values.data(),
                    nullptr, nullptr, 0
                );
                
                if (sent) {
                    // Flush the send buffer
                    int flush_result;
                    while ((flush_result = PQflush(free_slot->conn)) == 1) {
                        fd_set write_fds;
                        FD_ZERO(&write_fds);
                        FD_SET(free_slot->socket_fd, &write_fds);
                        struct timeval tv = {0, 1000};
                        select(free_slot->socket_fd + 1, nullptr, &write_fds, nullptr, &tv);
                    }
                    
                    if (flush_result == -1) {
                        spdlog::error("[SidecarDbPool] PQflush failed: {}", PQerrorMessage(free_slot->conn));
                        std::lock_guard<std::mutex> resp_lock(completed_mutex_);
                        for (const auto& info : batch_info) {
                            SidecarResponse error_resp;
                            error_resp.op_type = batch_op_type;
                            error_resp.request_id = info.request_id;
                            error_resp.worker_id = info.worker_id;
                            error_resp.success = false;
                            error_resp.error_message = "PQflush failed";
                            completed_responses_.push_back(std::move(error_resp));
                        }
                        continue;
                    }
                    
                    free_slot->busy = true;
                    free_slot->is_batched = true;
                    free_slot->batched_requests = std::move(batch_info);
                    free_slot->op_type = batch_op_type;
                    free_slot->total_items = current_index;
                    free_slot->query_start = std::chrono::steady_clock::now();
                    
                    total_queries_++;
                } else {
                    spdlog::error("[SidecarDbPool] PQsendQuery failed: {}", PQerrorMessage(free_slot->conn));
                    
                    std::lock_guard<std::mutex> resp_lock(completed_mutex_);
                    for (const auto& info : batch_info) {
                        SidecarResponse error_resp;
                        error_resp.op_type = batch_op_type;
                        error_resp.request_id = info.request_id;
                        error_resp.worker_id = info.worker_id;
                        error_resp.success = false;
                        error_resp.error_message = "PQsendQuery failed";
                        completed_responses_.push_back(std::move(error_resp));
                    }
                }
            }
        }
        
        // ============================================================
        // STEP 2: PREPARE fd_set FOR SELECT
        // ============================================================
        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = 0;
        int active_count = 0;
        
        for (const auto& slot : slots_) {
            if (slot.busy && slot.socket_fd >= 0) {
                FD_SET(slot.socket_fd, &read_fds);
                max_fd = std::max(max_fd, slot.socket_fd);
                active_count++;
            }
        }
        
        // Log parallel query count (only when there's activity)
        static int last_logged_count = -1;
        if (active_count > 0 && active_count != last_logged_count) {
            size_t pending_count = 0;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                pending_count = pending_requests_.size();
            }
            spdlog::info("[SidecarDbPool] Queries in flight: {}/{} connections, {} pending in queue",
                        active_count, pool_size_, pending_count);
            last_logged_count = active_count;
        } else if (active_count == 0) {
            last_logged_count = -1;  // Reset so we log again when activity resumes
        }
        
        // If nothing is happening, sleep briefly to avoid 100% CPU
        bool has_pending = false;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            has_pending = !pending_requests_.empty();
        }
        
        if (active_count == 0 && !has_pending) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        
        // If we have pending requests but no active queries, loop back to send them
        if (active_count == 0 && has_pending) {
            continue;
        }
        
        // ============================================================
        // STEP 3: WAIT FOR ANSWERS (SELECT)
        // ============================================================
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 5000;  // 5ms timeout for responsiveness
        
        int ready = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);
        
        if (ready < 0) {
            if (errno != EINTR) {
                spdlog::error("[SidecarDbPool] select() failed: {}", strerror(errno));
            }
            continue;
        }
        
        // ============================================================
        // STEP 4: PROCESS READY CONNECTIONS
        // ============================================================
        if (ready > 0) {
            std::vector<SidecarResponse> response_batch;
            
            for (auto& slot : slots_) {
                if (!slot.busy || slot.socket_fd < 0) continue;
                if (!FD_ISSET(slot.socket_fd, &read_fds)) continue;
                
                // Consume data from kernel buffer
                if (!PQconsumeInput(slot.conn)) {
                    spdlog::error("[SidecarDbPool] PQconsumeInput failed: {}", PQerrorMessage(slot.conn));
                    
                    // Connection broken - return errors and reconnect
                    if (slot.is_batched) {
                        for (const auto& info : slot.batched_requests) {
                            SidecarResponse error_resp;
                            error_resp.request_id = info.request_id;
                            error_resp.worker_id = info.worker_id;
                            error_resp.success = false;
                            error_resp.error_message = "Connection lost";
                            response_batch.push_back(std::move(error_resp));
                        }
                    } else {
                        SidecarResponse error_resp;
                        error_resp.request_id = slot.request_id;
                        error_resp.worker_id = slot.worker_id;
                        error_resp.success = false;
                        error_resp.error_message = "Connection lost";
                        response_batch.push_back(std::move(error_resp));
                    }
                    
                    slot.busy = false;
                    slot.is_batched = false;
                    slot.batched_requests.clear();
                    disconnect_slot(slot);
                    connect_slot(slot);
                    continue;
                }
                
                // Check if query is complete
                if (PQisBusy(slot.conn) == 0) {
                    auto query_end = std::chrono::steady_clock::now();
                    auto query_time = std::chrono::duration_cast<std::chrono::microseconds>(
                        query_end - slot.query_start).count();
                    
                    total_query_time_us_ += query_time;
                    
                    // Get ALL results (loop until null)
                    PGresult* result = nullptr;
                    PGresult* last_result = nullptr;
                    
                    while ((result = PQgetResult(slot.conn)) != nullptr) {
                        if (last_result) {
                            PQclear(last_result);
                        }
                        last_result = result;
                    }
                    
                    // Handle micro-batched response
                    if (slot.is_batched && !slot.batched_requests.empty()) {
                        bool db_success = false;
                        std::string error_msg;
                        nlohmann::json all_results;
                        
                        if (last_result) {
                            ExecStatusType status = PQresultStatus(last_result);
                            db_success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
                            
                            if (db_success && status == PGRES_TUPLES_OK && PQntuples(last_result) > 0) {
                                const char* val = PQgetvalue(last_result, 0, 0);
                                if (val) {
                                    try {
                                        all_results = nlohmann::json::parse(val);
                                    } catch (const std::exception& e) {
                                        db_success = false;
                                        error_msg = std::string("JSON parse error: ") + e.what();
                                    }
                                }
                            }
                            
                            if (!db_success && error_msg.empty()) {
                                error_msg = PQresultErrorMessage(last_result);
                            }
                            
                            PQclear(last_result);
                        }
                        
                        spdlog::info("[SidecarDbPool] MICRO-BATCH complete: op={} {} requests, {} total items in {}ms (db_success={})",
                                    static_cast<int>(slot.op_type), slot.batched_requests.size(), 
                                    slot.total_items, query_time / 1000, db_success);
                        
                        // Update per-op stats
                        {
                            std::lock_guard<std::mutex> stats_lock(completed_mutex_);
                            auto& stats = op_stats_[slot.op_type];
                            stats.count++;
                            stats.total_time_us += query_time;
                            stats.items_processed += slot.total_items;
                        }
                        
                        // Split results by request using the 'index' or 'idx' field from stored procedure
                        // The SP returns results in unpredictable order, so we MUST match by index
                        for (const auto& info : slot.batched_requests) {
                            SidecarResponse resp;
                            resp.op_type = slot.op_type;
                            resp.request_id = info.request_id;
                            resp.worker_id = info.worker_id;
                            resp.query_time_us = query_time;
                            
                            if (db_success && all_results.is_array()) {
                                // Filter by 'index' or 'idx' field
                                nlohmann::json request_results = nlohmann::json::array();
                                
                                // For POP batch responses, the structure is different:
                                // [{idx: 0, result: {...}}, {idx: 1, result: {...}}]
                                // vs PUSH: [{index: 0, ...}, {index: 1, ...}]
                                for (const auto& result : all_results) {
                                    int idx = result.value("index", result.value("idx", -1));
                                    if (idx >= static_cast<int>(info.start_index) && 
                                        idx < static_cast<int>(info.start_index + info.item_count)) {
                                        request_results.push_back(result);
                                    }
                                }
                                resp.success = true;
                                resp.result_json = request_results.dump();
                                spdlog::debug("[SidecarDbPool] Request {} (worker {}): matched {} results (start={}, count={})", 
                                            info.request_id, info.worker_id, request_results.size(), 
                                            info.start_index, info.item_count);
                            } else {
                                resp.success = false;
                                resp.error_message = error_msg.empty() ? "Unknown error" : error_msg;
                                spdlog::warn("[SidecarDbPool] Request {} failed: {}", info.request_id, resp.error_message);
                            }
                            
                            response_batch.push_back(std::move(resp));
                        }
                        
                        slot.busy = false;
                        slot.is_batched = false;
                        slot.batched_requests.clear();
                        slot.request_id.clear();
                        slot.total_items = 0;
                        continue;
                    }
                    
                    // Non-batched single request response (TRANSACTION, single ACK)
                    SidecarResponse resp;
                    resp.op_type = slot.op_type;
                    resp.request_id = slot.request_id;
                    resp.worker_id = slot.worker_id;
                    resp.query_time_us = query_time;
                    
                    if (last_result) {
                        ExecStatusType status = PQresultStatus(last_result);
                        resp.success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
                        
                        if (resp.success && status == PGRES_TUPLES_OK && PQntuples(last_result) > 0) {
                            const char* val = PQgetvalue(last_result, 0, 0);
                            if (val) {
                                resp.result_json = val;
                            }
                        }
                        
                        if (!resp.success) {
                            resp.error_message = PQresultErrorMessage(last_result);
                        }
                        
                        PQclear(last_result);
                    } else {
                        resp.success = false;
                        resp.error_message = "No result";
                    }
                    
                    // Update per-op stats
                    {
                        std::lock_guard<std::mutex> stats_lock(completed_mutex_);
                        auto& stats = op_stats_[slot.op_type];
                        stats.count++;
                        stats.total_time_us += query_time;
                        stats.items_processed += slot.total_items;
                    }
                    
                    response_batch.push_back(std::move(resp));
                    
                    // Slot is now free
                    slot.busy = false;
                    slot.request_id.clear();
                    slot.total_items = 0;
                }
            }
            
            // ============================================================
            // STEP 5: DELIVER RESPONSES (Timer will pick them up)
            // ============================================================
            if (!response_batch.empty()) {
                std::lock_guard<std::mutex> lock(completed_mutex_);
                size_t before_size = completed_responses_.size();
                for (auto& resp : response_batch) {
                    completed_responses_.push_back(std::move(resp));
                }
                spdlog::info("[SidecarDbPool] Added {} responses to queue (queue size: {} -> {})", 
                            response_batch.size(), before_size, completed_responses_.size());
                // Responses will be picked up by the existing response timer
                // No async wakeup needed - timer polls at configured interval
            }
        }
    }
    
    spdlog::info("[SidecarDbPool] Poller loop exited");
}

SidecarDbPool::Stats SidecarDbPool::get_stats() const {
    Stats stats;
    stats.total_connections = slots_.size();
    stats.busy_connections = 0;
    for (const auto& slot : slots_) {
        if (slot.busy) stats.busy_connections++;
    }
    
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        stats.pending_requests = pending_requests_.size();
    }
    {
        std::lock_guard<std::mutex> lock(completed_mutex_);
        stats.completed_responses = completed_responses_.size();
        stats.op_stats = op_stats_;  // Copy per-op stats
    }
    
    stats.total_queries = total_queries_.load();
    stats.total_query_time_us = total_query_time_us_.load();
    
    return stats;
}

} // namespace queen

