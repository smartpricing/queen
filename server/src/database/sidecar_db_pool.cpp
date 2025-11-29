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

void SidecarDbPool::poller_loop() {
    spdlog::info("[SidecarDbPool] Poller loop started with MICRO-BATCHING enabled (5ms collection window)");
    
    constexpr int MICRO_BATCH_WAIT_MS = 5;  // Wait up to 5ms to collect requests
    constexpr int MAX_ITEMS_PER_TX = 500;
    
    while (running_) {
        // ============================================================
        // STEP 1: MICRO-BATCH COLLECTION & SEND
        // ============================================================
        {
            // Check if we have pending requests
            bool has_pending = false;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                has_pending = !pending_requests_.empty();
            }
            
            // If we have pending requests, wait a bit to collect more (micro-batching)
            if (has_pending && pending_requests_.size() < 5) {
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
                
                // Collect ALL pending requests into one micro-batch
                std::vector<SidecarRequest> batch;
                size_t total_items_in_batch = 0;
                while (!pending_requests_.empty() && batch.size() < 1000) {  // Cap at 1000 requests per batch
                    auto& next_req = pending_requests_.front();

                    if (total_items_in_batch > 0 && 
                        (total_items_in_batch + next_req.item_count > MAX_ITEMS_PER_TX)) {
                        // Adding this request would make the batch too big. 
                        // Stop here and process what we have.
                        break; 
                    }
                
                    total_items_in_batch += next_req.item_count;                    
                    batch.push_back(std::move(pending_requests_.front()));
                    pending_requests_.pop_front();
                }
                
                if (batch.empty()) break;
                
                // Build combined JSON array from all requests
                nlohmann::json combined_items = nlohmann::json::array();
                std::vector<BatchedRequestInfo> batch_info;
                size_t current_index = 0;
                
                for (const auto& req : batch) {
                    // Parse the items JSON from params[0]
                    if (!req.params.empty()) {
                        try {
                            nlohmann::json items = nlohmann::json::parse(req.params[0]);
                            size_t item_count = items.size();
                            
                            // Track this request's range in the combined batch
                            BatchedRequestInfo info;
                            info.request_id = req.request_id;
                            info.worker_id = req.worker_id;
                            info.start_index = current_index;
                            info.item_count = item_count;
                            batch_info.push_back(info);
                            
                            // Add items to combined array
                            for (const auto& item : items) {
                                combined_items.push_back(item);
                            }
                            current_index += item_count;
                        } catch (const std::exception& e) {
                            spdlog::error("[SidecarDbPool] Failed to parse request JSON: {}", e.what());
                            // Return error for this request
                            SidecarResponse error_resp;
                            error_resp.request_id = req.request_id;
                            error_resp.worker_id = req.worker_id;
                            error_resp.success = false;
                            error_resp.error_message = std::string("JSON parse error: ") + e.what();
                            
                            std::lock_guard<std::mutex> resp_lock(completed_mutex_);
                            completed_responses_.push_back(std::move(error_resp));
                        }
                    }
                }
                
                if (combined_items.empty() || batch_info.empty()) {
                    continue;
                }
                
                spdlog::info("[SidecarDbPool] MICRO-BATCH: Combined {} requests into {} items", 
                            batch_info.size(), combined_items.size());
                
                // Build the SP call with combined items
                std::string combined_json = combined_items.dump();
                std::string sql = "SELECT queen.push_messages_v2($1::jsonb, $2::boolean, $3::boolean)";
                std::vector<const char*> param_values = {
                    combined_json.c_str(),
                    "true",   // check duplicates
                    "true"    // check capacity
                };
                
                // SEND QUERY (non-blocking!)
                int sent = PQsendQueryParams(
                    free_slot->conn,
                    sql.c_str(),
                    3,
                    nullptr,  // paramTypes
                    param_values.data(),
                    nullptr,  // paramLengths
                    nullptr,  // paramFormats
                    0         // resultFormat (text)
                );
                
                if (sent) {
                    // Flush the send buffer
                    int flush_result;
                    while ((flush_result = PQflush(free_slot->conn)) == 1) {
                        fd_set write_fds;
                        FD_ZERO(&write_fds);
                        FD_SET(free_slot->socket_fd, &write_fds);
                        struct timeval tv = {0, 1000};  // 1ms timeout
                        select(free_slot->socket_fd + 1, nullptr, &write_fds, nullptr, &tv);
                    }
                    
                    if (flush_result == -1) {
                        spdlog::error("[SidecarDbPool] PQflush failed: {}", PQerrorMessage(free_slot->conn));
                        // Return error for all requests in batch
                        std::lock_guard<std::mutex> resp_lock(completed_mutex_);
                        for (const auto& info : batch_info) {
                            SidecarResponse error_resp;
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
                    free_slot->query_start = std::chrono::steady_clock::now();
                    
                    total_queries_++;
                } else {
                    // Send failed - return error for all requests
                    spdlog::error("[SidecarDbPool] PQsendQuery failed: {}", PQerrorMessage(free_slot->conn));
                    
                    std::lock_guard<std::mutex> resp_lock(completed_mutex_);
                    for (const auto& info : batch_info) {
                        SidecarResponse error_resp;
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
                        
                        spdlog::info("[SidecarDbPool] MICRO-BATCH complete: {} requests, {} total items in {}ms",
                                    slot.batched_requests.size(), all_results.size(), query_time / 1000);
                        
                        // Split results by request
                        for (const auto& info : slot.batched_requests) {
                            SidecarResponse resp;
                            resp.request_id = info.request_id;
                            resp.worker_id = info.worker_id;
                            resp.query_time_us = query_time;
                            
                            if (db_success && all_results.is_array()) {
                                // Extract this request's results by index range
                                nlohmann::json request_results = nlohmann::json::array();
                                for (size_t i = info.start_index; i < info.start_index + info.item_count && i < all_results.size(); i++) {
                                    request_results.push_back(all_results[i]);
                                }
                                resp.success = true;
                                resp.result_json = request_results.dump();
                            } else {
                                resp.success = false;
                                resp.error_message = error_msg.empty() ? "Unknown error" : error_msg;
                            }
                            
                            response_batch.push_back(std::move(resp));
                        }
                        
                        slot.busy = false;
                        slot.is_batched = false;
                        slot.batched_requests.clear();
                        slot.request_id.clear();
                        continue;
                    }
                    
                    // Non-batched single request response (legacy path)
                    SidecarResponse resp;
                    resp.request_id = slot.request_id;
                    resp.worker_id = slot.worker_id;
                    resp.query_time_us = query_time;
                    
                    if (last_result) {
                        ExecStatusType status = PQresultStatus(last_result);
                        resp.success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
                        
                        if (resp.success && status == PGRES_TUPLES_OK && PQntuples(last_result) > 0) {
                            // Get the result (should be JSON from stored procedure)
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
                    
                    response_batch.push_back(std::move(resp));
                    
                    // Slot is now free
                    slot.busy = false;
                    slot.request_id.clear();
                }
            }
            
            // ============================================================
            // STEP 5: DELIVER RESPONSES (Timer will pick them up)
            // ============================================================
            if (!response_batch.empty()) {
                std::lock_guard<std::mutex> lock(completed_mutex_);
                for (auto& resp : response_batch) {
                    completed_responses_.push_back(std::move(resp));
                }
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
    }
    
    stats.total_queries = total_queries_.load();
    stats.total_query_time_us = total_query_time_us_.load();
    
    return stats;
}

} // namespace queen

