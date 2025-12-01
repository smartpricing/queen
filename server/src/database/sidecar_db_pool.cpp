#include "queen/sidecar_db_pool.hpp"
#include "queen/shared_state_manager.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>

namespace queen {

SidecarDbPool::SidecarDbPool(const std::string& conn_str, 
                             int pool_size,
                             int statement_timeout_ms,
                             std::shared_ptr<astp::ThreadPool> thread_pool,
                             SidecarResponseCallback response_callback,
                             int worker_id,
                             SidecarTuning tuning)
    : conn_str_(conn_str),
      pool_size_(pool_size),
      statement_timeout_ms_(statement_timeout_ms),
      worker_id_(worker_id),
      tuning_(tuning),
      response_callback_(std::move(response_callback)),
      thread_pool_(thread_pool) {
    
    slots_.resize(pool_size_);
    
    // Initialize libuv event loop
    loop_ = new uv_loop_t;
    if (uv_loop_init(loop_) != 0) {
        spdlog::error("[Worker {}] [Sidecar] Failed to initialize libuv loop", worker_id_);
        delete loop_;
        loop_ = nullptr;
        return;
    }
    loop_initialized_ = true;
    
    // Initialize batch timer (for micro-batching)
    uv_timer_init(loop_, &batch_timer_);
    batch_timer_.data = this;
    
    // Initialize waiting queue timer (for POP_WAIT)
    uv_timer_init(loop_, &waiting_timer_);
    waiting_timer_.data = this;
    
    // Initialize submit signal (for cross-thread wakeup)
    uv_async_init(loop_, &submit_signal_, on_submit_signal);
    submit_signal_.data = this;
    
    spdlog::info("[Worker {}] [Sidecar] Created with {} connections (libuv, batch_wait={}ms, max_items={}, max_batch={}, max_pending={})", 
                 worker_id_, pool_size_, tuning_.micro_batch_wait_ms, tuning_.max_items_per_tx, 
                 tuning_.max_batch_size, tuning_.max_pending_count);
}

// Static callback for handle close (prevents use-after-free)
void SidecarDbPool::on_handle_close(uv_handle_t* handle) {
    // Handle closed, nothing to do - prevents dangling pointer issues
    (void)handle;
}

SidecarDbPool::~SidecarDbPool() {
    stop();
    
    // Stop and close libuv handles
    if (loop_initialized_) {
        uv_timer_stop(&batch_timer_);
        uv_timer_stop(&waiting_timer_);
        
        // Close handles (must be done before loop close)
        if (!uv_is_closing((uv_handle_t*)&batch_timer_)) {
            uv_close((uv_handle_t*)&batch_timer_, on_handle_close);
        }
        if (!uv_is_closing((uv_handle_t*)&waiting_timer_)) {
            uv_close((uv_handle_t*)&waiting_timer_, on_handle_close);
        }
        if (!uv_is_closing((uv_handle_t*)&submit_signal_)) {
            uv_close((uv_handle_t*)&submit_signal_, on_handle_close);
        }
        
        // Close poll handles for all slots
        for (auto& slot : slots_) {
            if (slot.poll_initialized && !uv_is_closing((uv_handle_t*)&slot.poll_handle)) {
                uv_poll_stop(&slot.poll_handle);
                uv_close((uv_handle_t*)&slot.poll_handle, on_handle_close);
            }
        }
        
        // Run loop to process close callbacks
        uv_run(loop_, UV_RUN_DEFAULT);
        
        // Close and free the loop
        uv_loop_close(loop_);
        delete loop_;
        loop_ = nullptr;
        loop_initialized_ = false;
    }
    
    // Disconnect all slots
    for (auto& slot : slots_) {
        disconnect_slot(slot);
    }
    
    // Disconnect the check connection
    if (check_conn_) {
        PQfinish(check_conn_);
        check_conn_ = nullptr;
    }
    
    spdlog::info("[SidecarDbPool] Destroyed (libuv cleanup complete)");
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
    
    // Connect the dedicated check connection (for lightweight has_pending queries)
    {
        std::lock_guard<std::mutex> lock(check_conn_mutex_);
        check_conn_ = PQconnectdb(conn_str_.c_str());
        if (PQstatus(check_conn_) != CONNECTION_OK) {
            spdlog::warn("[SidecarDbPool] Failed to connect check connection: {}", PQerrorMessage(check_conn_));
            PQfinish(check_conn_);
            check_conn_ = nullptr;
        } else {
            // Set short statement timeout for quick checks
            PGresult* res = PQexec(check_conn_, "SET statement_timeout = 1000");  // 1 second
            PQclear(res);
            spdlog::debug("[SidecarDbPool] Connected dedicated check connection");
        }
    }
    
    // Start poller in threadpool
    running_ = true;
    thread_pool_->push([this]() {
        this->poller_loop();
    });
    spdlog::info("[SidecarDbPool] Poller started in threadpool");
}

void SidecarDbPool::stop() {
    if (!running_) return;
    
    spdlog::info("[SidecarDbPool] Stopping...");
    running_ = false;
    // Poller loop will exit on next iteration when running_ is false
    
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
    slot.pool = this;  // Back-reference for callbacks
    
    // Initialize libuv poll handle for this connection's socket
    if (loop_initialized_ && slot.socket_fd >= 0) {
        if (uv_poll_init(loop_, &slot.poll_handle, slot.socket_fd) == 0) {
            slot.poll_handle.data = &slot;
            slot.poll_initialized = true;
        } else {
            spdlog::warn("[SidecarDbPool] Failed to initialize uv_poll for slot");
            slot.poll_initialized = false;
        }
    }
    
    return true;
}

void SidecarDbPool::disconnect_slot(ConnectionSlot& slot) {
    // Stop polling before disconnecting
    if (slot.poll_initialized) {
        uv_poll_stop(&slot.poll_handle);
        // Mark as needing reinitialization for reconnection
        slot.poll_initialized = false;
    }
    
    if (slot.conn) {
        PQfinish(slot.conn);
        slot.conn = nullptr;
        slot.socket_fd = -1;
        slot.busy = false;
    }
}

void SidecarDbPool::submit(SidecarRequest request) {
    request.queued_at = std::chrono::steady_clock::now();
    
    // POP_WAIT goes to waiting queue, not pending queue
    if (request.op_type == SidecarOpType::POP_WAIT) {
        std::lock_guard<std::mutex> lock(waiting_mutex_);
        waiting_requests_.push_back(std::move(request));
        // Wake up the event loop to process waiting queue
        if (loop_initialized_) {
            uv_async_send(&submit_signal_);
        }
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.push_back(std::move(request));
    }
    
    // Wake up the event loop to drain pending requests
    // Note: uv_async_send is thread-safe and coalescing (multiple calls = one callback)
    if (loop_initialized_) {
        uv_async_send(&submit_signal_);
    }
}

void SidecarDbPool::notify_queue_activity(const std::string& queue_name) {
    auto now = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(waiting_mutex_);
    int notified = 0;
    for (auto& req : waiting_requests_) {
        if (req.queue_name == queue_name) {
            req.next_check = now;  // Trigger immediate check
            notified++;
        }
    }
    if (notified > 0) {
        spdlog::info("[Worker {}] [Sidecar] NOTIFY: Queue '{}' has activity, waking {} waiting consumers", 
                     worker_id_, queue_name, notified);
        // Wake up the event loop to process the waiting queue immediately
        if (loop_initialized_) {
            uv_async_send(&submit_signal_);
        }
    }
}

std::string SidecarDbPool::make_group_key(const std::string& queue,
                                           const std::string& partition,
                                           const std::string& consumer_group) {
    return queue + "/" + (partition.empty() ? "*" : partition) + "/" + consumer_group;
}

void SidecarDbPool::process_waiting_queue() {
    auto now = std::chrono::steady_clock::now();
    std::vector<SidecarRequest> to_process;
    
    // Collect requests that are due or expired
    {
        std::lock_guard<std::mutex> lock(waiting_mutex_);
        auto it = waiting_requests_.begin();
        while (it != waiting_requests_.end()) {
            if (now >= it->wait_deadline) {
                // Expired - send empty response immediately
                spdlog::debug("[Worker {}] [Sidecar] POP_WAIT TIMEOUT: {} [{}@{}]", 
                            worker_id_, it->queue_name, it->consumer_group, 
                            it->partition_name.empty() ? "*" : it->partition_name);
                
                SidecarResponse resp;
                resp.op_type = SidecarOpType::POP_WAIT;
                resp.request_id = it->request_id;
                resp.success = true;
                resp.result_json = R"({"messages":[]})";
                resp.query_time_us = 0;
                deliver_response(std::move(resp));
                it = waiting_requests_.erase(it);
            } else if (now >= it->next_check) {
                // Due for check - move to processing list
                to_process.push_back(std::move(*it));
                it = waiting_requests_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Process each due request
    for (auto& req : to_process) {
        std::string group_key = make_group_key(req.queue_name, req.partition_name, req.consumer_group);
        
        // Check backoff via SharedStateManager
        auto current_interval = std::chrono::milliseconds(100);
        if (global_shared_state) {
            current_interval = global_shared_state->get_group_interval(group_key);
            
            if (!global_shared_state->should_check_group(group_key)) {
                // Not due yet - re-queue with updated next_check
                req.next_check = now + current_interval;
                std::lock_guard<std::mutex> lock(waiting_mutex_);
                waiting_requests_.push_back(std::move(req));
                continue;
            }
        }
        
        // OPTIMIZATION: For high backoff intervals (>= 500ms), do lightweight check first
        // This avoids expensive full POP queries when queue is empty during fallback polling
        if (current_interval >= std::chrono::milliseconds(500)) {
            if (!check_has_pending(req.queue_name, req.partition_name, req.consumer_group)) {
                // No messages - skip full query, increase backoff, re-queue
                if (global_shared_state) {
                    global_shared_state->release_group(group_key, false);  // false = no messages
                }
                
                auto new_interval = global_shared_state 
                    ? global_shared_state->get_group_interval(group_key)
                    : std::chrono::milliseconds(1000);
                req.next_check = now + new_interval;
                
                spdlog::debug("[Worker {}] [Sidecar] POP_WAIT SKIP (no pending): {} [{}@{}] - backoff {}ms", 
                            worker_id_, req.queue_name, req.consumer_group,
                            req.partition_name.empty() ? "*" : req.partition_name,
                            new_interval.count());
                
                std::lock_guard<std::mutex> lock(waiting_mutex_);
                waiting_requests_.push_back(std::move(req));
                continue;
            }
            // Messages exist - proceed with full query
            spdlog::debug("[Worker {}] [Sidecar] POP_WAIT HAS_PENDING: {} [{}@{}] - proceeding", 
                        worker_id_, req.queue_name, req.consumer_group,
                        req.partition_name.empty() ? "*" : req.partition_name);
        }
        
        // Try to acquire group lock
        if (global_shared_state && !global_shared_state->try_acquire_group(group_key)) {
            // Another sidecar is querying - wait briefly
            req.next_check = now + std::chrono::milliseconds(10);
            std::lock_guard<std::mutex> lock(waiting_mutex_);
            waiting_requests_.push_back(std::move(req));
            continue;
        }
        
        // Track this POP_WAIT for re-queuing after result
        {
            std::lock_guard<std::mutex> lock(tracker_mutex_);
            PopWaitTracker tracker;
            tracker.wait_deadline = req.wait_deadline;
            tracker.queue_name = req.queue_name;
            tracker.partition_name = req.partition_name;
            tracker.consumer_group = req.consumer_group;
            tracker.batch_size = req.batch_size;
            tracker.subscription_mode = req.subscription_mode;
            tracker.subscription_from = req.subscription_from;
            pop_wait_trackers_[req.request_id] = std::move(tracker);
        }
        
        // Convert POP_WAIT to POP for the actual query
        req.op_type = SidecarOpType::POP;
        
        // Build SQL if not already set
        if (req.sql.empty()) {
            req.sql = "SELECT queen.pop_messages_v2($1, $2, $3, $4, $5, $6, $7)";
            req.params = {
                req.queue_name,
                req.partition_name,
                req.consumer_group,
                std::to_string(req.batch_size),
                "0",  // Use queue's configured lease_time
                req.subscription_mode,
                req.subscription_from
            };
        }
        
        // Log before moving (req will be empty after move)
        spdlog::debug("[Worker {}] [Sidecar] POP_WAIT QUERY: {} [{}@{}]", 
                    worker_id_, req.queue_name, req.consumer_group, 
                    req.partition_name.empty() ? "*" : req.partition_name);
        
        // Move to pending queue for execution
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_requests_.push_back(std::move(req));
        }
    }
}

void SidecarDbPool::deliver_response(SidecarResponse response) {
    // Call the response callback (should use loop->defer internally)
    if (response_callback_) {
        try {
            response_callback_(std::move(response));
        } catch (const std::exception& e) {
            spdlog::error("[SidecarDbPool] Response callback threw exception: {}", e.what());
        }
    }
}

// Helper: Check if an operation type supports micro-batching
// NOTE: POP (specific partition) is NOT batchable because multiple requests
// to the same partition would conflict. However, POP_BATCH (wildcard partition)
// IS batchable because the stored procedure pre-allocates unique partitions.
static bool is_batchable_op(SidecarOpType op) {
    switch (op) {
        case SidecarOpType::PUSH:
        case SidecarOpType::ACK_BATCH:
        case SidecarOpType::RENEW_LEASE:
        case SidecarOpType::POP_BATCH:   // Wildcard POP - uses partition pre-allocation
            return true;
        case SidecarOpType::ACK:
        case SidecarOpType::POP:         // Specific partition POP - each needs own transaction
        case SidecarOpType::POP_WAIT:    // POP_WAIT is handled separately via waiting queue
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
            
        case SidecarOpType::POP_BATCH:
            // Wildcard partition POP - uses true batched procedure with partition pre-allocation
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
            
        case SidecarOpType::POP:       // Specific partition - not batchable
        case SidecarOpType::ACK:
        case SidecarOpType::TRANSACTION:
        case SidecarOpType::POP_WAIT:
            // These are not batchable, should not reach here
            return {"", {}};
    }
    return {"", {}};
}

void SidecarDbPool::poller_loop() {
    spdlog::info("[SidecarDbPool] Poller loop started (libuv event-driven)");
    
    if (!loop_initialized_) {
        spdlog::error("[SidecarDbPool] libuv loop not initialized!");
        return;
    }
    
    // Start batch timer (for micro-batching)
    // Repeat interval enables uv_timer_again() to work
    uv_timer_start(&batch_timer_, on_batch_timer, 
                   tuning_.micro_batch_wait_ms,   // Initial delay
                   tuning_.micro_batch_wait_ms);  // Repeat interval
    
    // Start waiting queue timer (for POP_WAIT)
    uv_timer_start(&waiting_timer_, on_waiting_timer,
                   10,   // Initial delay (10ms)
                   10);  // Repeat interval (check every 10ms)
    
    // Run the libuv event loop
    while (running_) {
        // UV_RUN_ONCE: Run one iteration, block if no callbacks are pending
        int result = uv_run(loop_, UV_RUN_ONCE);
        
        // result == 0 means no more active handles (shouldn't happen normally)
        if (result == 0 && running_) {
            // All handles closed unexpectedly - brief sleep to prevent busy-loop
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    // Stop timers before exiting
    uv_timer_stop(&batch_timer_);
    uv_timer_stop(&waiting_timer_);
    
    spdlog::info("[SidecarDbPool] Poller loop exited (libuv)");
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
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats.op_stats = op_stats_;  // Copy per-op stats
    }
    
    stats.total_queries = total_queries_.load();
    stats.total_query_time_us = total_query_time_us_.load();
    
    return stats;
}

bool SidecarDbPool::check_has_pending(const std::string& queue_name,
                                       const std::string& partition_name,
                                       const std::string& consumer_group) {
    std::lock_guard<std::mutex> lock(check_conn_mutex_);
    
    // If no check connection, assume messages exist (safe default)
    if (!check_conn_) {
        return true;
            }
            
    // Check connection status and reconnect if needed
    if (PQstatus(check_conn_) != CONNECTION_OK) {
        PQreset(check_conn_);
        if (PQstatus(check_conn_) != CONNECTION_OK) {
            spdlog::warn("[SidecarDbPool] Check connection reset failed, assuming messages exist");
            return true;
        }
    }
    
    // Build query - handle empty partition as NULL
    const char* partition_param = partition_name.empty() ? nullptr : partition_name.c_str();
    const char* params[3] = {
        queue_name.c_str(),
        partition_param,
        consumer_group.c_str()
    };
    
    // Execute synchronous query (this is called from poller thread)
    PGresult* result = PQexecParams(
        check_conn_,
        "SELECT queen.has_pending_messages($1, $2, $3)",
        3,
        nullptr,
        params,
        nullptr,
        nullptr,
        0
    );
    
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        spdlog::warn("[SidecarDbPool] has_pending_messages query failed: {}", PQresultErrorMessage(result));
        PQclear(result);
        return true;  // Assume messages exist on error
    }
    
    bool has_messages = false;
    if (PQntuples(result) > 0 && PQnfields(result) > 0) {
        const char* val = PQgetvalue(result, 0, 0);
        has_messages = (val && (val[0] == 't' || val[0] == 'T'));
    }
    
    PQclear(result);
    return has_messages;
}

// ============================================================================
// libuv helper methods
// ============================================================================

void SidecarDbPool::start_watching_slot(ConnectionSlot& slot, int events) {
    if (slot.poll_initialized && slot.socket_fd >= 0) {
        uv_poll_start(&slot.poll_handle, events, on_socket_event);
    }
}

void SidecarDbPool::stop_watching_slot(ConnectionSlot& slot) {
    if (slot.poll_initialized) {
        uv_poll_stop(&slot.poll_handle);
    }
}

void SidecarDbPool::handle_slot_error(ConnectionSlot& slot, const std::string& error_msg) {
    spdlog::error("[SidecarDbPool] Slot error: {}", error_msg);
    
    // Send error responses for all pending requests on this slot
    if (slot.is_batched) {
        for (const auto& info : slot.batched_requests) {
            SidecarResponse error_resp;
            error_resp.op_type = slot.op_type;
            error_resp.request_id = info.request_id;
            error_resp.success = false;
            error_resp.error_message = error_msg;
            deliver_response(std::move(error_resp));
        }
    } else if (!slot.request_id.empty()) {
        SidecarResponse error_resp;
        error_resp.op_type = slot.op_type;
        error_resp.request_id = slot.request_id;
        error_resp.success = false;
        error_resp.error_message = error_msg;
        deliver_response(std::move(error_resp));
    }
    
    // Reset slot state
    stop_watching_slot(slot);
    slot.busy = false;
    slot.is_batched = false;
    slot.batched_requests.clear();
    slot.request_id.clear();
    slot.total_items = 0;
    
    // Mark slot as dead - periodic reconnection will handle it
    // Don't call blocking connect_slot here - it blocks the event loop!
    disconnect_slot(slot);
}

// ============================================================================
// libuv static callbacks
// ============================================================================

void SidecarDbPool::on_batch_timer(uv_timer_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    if (pool && pool->running_) {
        pool->drain_pending_to_slots();
    }
}

void SidecarDbPool::on_waiting_timer(uv_timer_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    if (pool && pool->running_) {
        pool->process_waiting_queue();
    }
}

void SidecarDbPool::on_submit_signal(uv_async_t* handle) {
    (void)handle; 
    /*
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    if (pool && pool->running_) {
        // Drain ALL pending requests (uv_async_send is coalescing!)
        pool->drain_pending_to_slots();
        
        // Reset the batch timer to prevent useless wakeup
        // This gives us MICRO_BATCH_WAIT_MS from NOW
        uv_timer_again(&pool->batch_timer_);
    }*/
}

void SidecarDbPool::on_socket_event(uv_poll_t* handle, int status, int events) {
    auto* slot = static_cast<ConnectionSlot*>(handle->data);
    if (!slot || !slot->pool) return;
    
    auto* pool = slot->pool;
    
    if (status < 0) {
        pool->handle_slot_error(*slot, std::string("Poll error: ") + uv_strerror(status));
        return;
            }
            
    // Handle writable event (flushing send buffer)
    if (events & UV_WRITABLE) {
        int flush_result = PQflush(slot->conn);
        if (flush_result == 0) {
            // All data sent - switch to read-only (CRITICAL: prevents 100% CPU!)
            uv_poll_start(handle, UV_READABLE, on_socket_event);
        } else if (flush_result == -1) {
            // Flush failed - error
            pool->handle_slot_error(*slot, "PQflush failed");
            return;
        }
        // flush_result == 1: more to send, keep watching WRITABLE
    }
    
    // Handle readable event (data from PostgreSQL)
    if (events & UV_READABLE) {
        if (!PQconsumeInput(slot->conn)) {
            pool->handle_slot_error(*slot, std::string("PQconsumeInput failed: ") + PQerrorMessage(slot->conn));
            return;
        }
        
        // Check if query is complete
        if (PQisBusy(slot->conn) == 0) {
            pool->process_slot_result(*slot);
        }
        // If still busy, keep waiting for more data
    }
}

// ============================================================================
// drain_pending_to_slots - Send pending requests to available DB connections
// ============================================================================
void SidecarDbPool::drain_pending_to_slots() {
    const size_t MAX_ITEMS_PER_TX = static_cast<size_t>(tuning_.max_items_per_tx);
    const size_t MAX_BATCH_SIZE = static_cast<size_t>(tuning_.max_batch_size);
    
    // Attempt to reconnect ONE dead slot per cycle (non-blocking approach)
    // This spreads reconnection over time instead of blocking on all at once
    static auto last_reconnect_attempt = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_reconnect_attempt).count() > 1000) {
        for (auto& slot : slots_) {
            if (!slot.busy && !slot.conn) {
                spdlog::info("[SidecarDbPool] Attempting to reconnect dead slot...");
                if (connect_slot(slot)) {
                    spdlog::info("[SidecarDbPool] Slot reconnected successfully");
                } else {
                    spdlog::warn("[SidecarDbPool] Slot reconnection failed, will retry later");
                }
                last_reconnect_attempt = now;
                break;  // Only try ONE slot per cycle
            }
        }
    }
    
    std::lock_guard<std::mutex> lock(pending_mutex_);
    
    // Check if we have ANY working connections
    bool has_working_connection = false;
    for (auto& slot : slots_) {
        if (slot.conn != nullptr) {
            has_working_connection = true;
            break;
        }
    }
    
    // If ALL connections are dead, fail over pending PUSH requests immediately
    // This ensures no messages are lost while waiting for reconnection
    if (!has_working_connection && !pending_requests_.empty()) {
        spdlog::warn("[SidecarDbPool] All connections dead, failing over {} pending requests", 
                    pending_requests_.size());
        
        while (!pending_requests_.empty()) {
            auto req = std::move(pending_requests_.front());
            pending_requests_.pop_front();
            
            SidecarResponse error_resp;
            error_resp.op_type = req.op_type;
            error_resp.request_id = req.request_id;
            error_resp.success = false;
            error_resp.error_message = "No database connections available";
            deliver_response(std::move(error_resp));
        }
        return;  // Nothing more to do until connections recover
    }
            
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
            break;  // All connections busy (but at least some are alive)
                }
                
                SidecarOpType batch_op_type = pending_requests_.front().op_type;
                
        // For non-batchable operations, send one at a time
                if (!is_batchable_op(batch_op_type)) {
                    auto req = std::move(pending_requests_.front());
                    pending_requests_.pop_front();
                    
                    std::vector<const char*> param_ptrs;
                    for (const auto& p : req.params) {
                        param_ptrs.push_back(p.c_str());
                    }
                    
                    // Track queue wait time for POP operations
                    auto queue_wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - req.queued_at).count();
                    
                    int sent = PQsendQueryParams(
                        free_slot->conn,
                        req.sql.c_str(),
                        static_cast<int>(param_ptrs.size()),
                        nullptr, param_ptrs.data(),
                        nullptr, nullptr, 0
                    );
                    
                    if (sent) {
                        free_slot->busy = true;
                        free_slot->is_batched = false;
                        free_slot->request_id = req.request_id;
                        free_slot->op_type = req.op_type;
                        free_slot->total_items = req.item_count;
                        free_slot->query_start = std::chrono::steady_clock::now();
                        free_slot->queue_wait_us = queue_wait_us;
                        total_queries_++;
                
                // Start watching for write (to flush) and read (for results)
                start_watching_slot(*free_slot, UV_WRITABLE | UV_READABLE);
                    } else {
                        spdlog::warn("[SidecarDbPool] Non-batch PQsendQuery failed: {}", PQerrorMessage(free_slot->conn));
                        SidecarResponse error_resp;
                        error_resp.op_type = req.op_type;
                        error_resp.request_id = req.request_id;
                        error_resp.success = false;
                        error_resp.error_message = "PQsendQuery failed";
                        deliver_response(std::move(error_resp));
                        
                        // Mark slot as needing reconnection (will be handled by next cycle)
                        // Don't reconnect here to avoid blocking the event loop
                        if (PQstatus(free_slot->conn) != CONNECTION_OK) {
                            disconnect_slot(*free_slot);
                            free_slot->conn = nullptr;  // Mark as needing reconnection
                        }
                    }
                    continue;
                }
                
                // Collect requests of the SAME operation type into one micro-batch
                std::vector<SidecarRequest> batch;
                size_t total_items_in_batch = 0;
                
        while (!pending_requests_.empty() && batch.size() < MAX_BATCH_SIZE) {
                    auto& next_req = pending_requests_.front();
                    
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
                
        // Combine JSON arrays
                std::string combined_json;
                combined_json.reserve(2 * 1024 * 1024);
                combined_json += "[";
                
                std::vector<BatchedRequestInfo> batch_info;
                size_t current_index = 0;
                bool first_item = true;
                bool needs_idx_rewrite = (batch_op_type == SidecarOpType::POP_BATCH);
                
                for (const auto& req : batch) {
                    if (req.params.empty()) continue;
                    
                    const std::string& raw = req.params[0];
                    if (raw.size() < 2) continue;
                    
                    BatchedRequestInfo info;
                    info.request_id = req.request_id;
                    info.start_index = current_index;
                    info.item_count = req.item_count;
                    info.push_targets = req.push_targets;
                    
                    if (needs_idx_rewrite) {
                        try {
                            auto items = nlohmann::json::parse(raw);
                            if (items.is_array()) {
                                for (auto& item : items) {
                                    item["idx"] = static_cast<int>(current_index);
                            if (!first_item) combined_json += ",";
                                    combined_json += item.dump();
                                    first_item = false;
                                    current_index++;
                                }
                                info.item_count = items.size();
                            }
                        } catch (const std::exception& e) {
                            SidecarResponse error_resp;
                            error_resp.op_type = req.op_type;
                            error_resp.request_id = req.request_id;
                            error_resp.success = false;
                            error_resp.error_message = "Invalid JSON format";
                            deliver_response(std::move(error_resp));
                            continue;
                        }
                    } else {
                        size_t start = raw.find('[');
                        size_t end = raw.rfind(']');
                        
                        if (start == std::string::npos || end == std::string::npos || end <= start) {
                            SidecarResponse error_resp;
                            error_resp.op_type = req.op_type;
                            error_resp.request_id = req.request_id;
                            error_resp.success = false;
                            error_resp.error_message = "Invalid JSON array format";
                            deliver_response(std::move(error_resp));
                            continue;
                        }
                        
                        std::string_view inner(raw.data() + start + 1, end - start - 1);
                        
                        bool has_content = false;
                        for (char c : inner) {
                            if (!std::isspace(static_cast<unsigned char>(c))) {
                                has_content = true;
                                break;
                            }
                        }
                        
                        if (!has_content) continue;
                        
                if (!first_item) combined_json += ",";
                        combined_json.append(inner.data(), inner.size());
                        first_item = false;
                        current_index += req.item_count;
                    }
                    
                    batch_info.push_back(info);
                }
                
                combined_json += "]";
                
                if (batch_info.empty() || current_index == 0) {
                    continue;
                }
                
        // Log batch
                const char* op_name = "UNKNOWN";
                switch (batch_op_type) {
                    case SidecarOpType::PUSH: op_name = "PUSH"; break;
                    case SidecarOpType::POP: op_name = "POP"; break;
                    case SidecarOpType::POP_BATCH: op_name = "POP_BATCH"; break;
                    case SidecarOpType::POP_WAIT: op_name = "POP_WAIT"; break;
                    case SidecarOpType::ACK: op_name = "ACK"; break;
                    case SidecarOpType::ACK_BATCH: op_name = "ACK_BATCH"; break;
                    case SidecarOpType::TRANSACTION: op_name = "TRANSACTION"; break;
                    case SidecarOpType::RENEW_LEASE: op_name = "RENEW_LEASE"; break;
                }
                spdlog::info("[Worker {}] [Sidecar] BATCH: {} ({} requests, {} items)", 
                            worker_id_, op_name, batch_info.size(), current_index);
                
                // Get SQL for this operation type
                std::vector<std::string> param_storage;
                auto [sql, param_ptrs_vec] = get_batched_sql(batch_op_type, combined_json, param_storage);
                
                std::vector<const char*> param_values;
                for (const auto& s : param_storage) {
                    param_values.push_back(s.c_str());
                }
                
                int sent = PQsendQueryParams(
                    free_slot->conn,
                    sql.c_str(),
                    static_cast<int>(param_values.size()),
                    nullptr,
                    param_values.data(),
                    nullptr, nullptr, 0
                );
                
                if (sent) {
                    // Track queue wait time for the batch (use first request's queued_at)
                    auto now = std::chrono::steady_clock::now();
                    int64_t max_queue_wait_us = 0;
                    for (const auto& req : batch) {
                        auto wait_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            now - req.queued_at).count();
                        if (wait_us > max_queue_wait_us) max_queue_wait_us = wait_us;
                    }
                    
                    free_slot->busy = true;
                    free_slot->is_batched = true;
                    free_slot->batched_requests = std::move(batch_info);
                    free_slot->op_type = batch_op_type;
                    free_slot->total_items = current_index;
                    free_slot->query_start = now;
                    free_slot->queue_wait_us = max_queue_wait_us;
                    total_queries_++;
            
            // Start watching for write (to flush) and read (for results)
            start_watching_slot(*free_slot, UV_WRITABLE | UV_READABLE);
                } else {
                    spdlog::error("[SidecarDbPool] PQsendQuery failed: {}", PQerrorMessage(free_slot->conn));
                    // IMPORTANT: Use batch_info here, NOT free_slot->batched_requests
                    // because batched_requests is only set in the if(sent) branch above
                    for (const auto& info : batch_info) {
                        SidecarResponse error_resp;
                        error_resp.op_type = batch_op_type;
                        error_resp.request_id = info.request_id;
                        error_resp.success = false;
                        error_resp.error_message = "PQsendQuery failed";
                        deliver_response(std::move(error_resp));
                    }
                    
                    // Mark slot as needing reconnection (will be handled by next cycle)
                    // Don't reconnect here to avoid blocking the event loop
                    if (PQstatus(free_slot->conn) != CONNECTION_OK) {
                        disconnect_slot(*free_slot);
                        free_slot->conn = nullptr;  // Mark as needing reconnection
                    }
                }
            }
        }
        
// ============================================================================
// process_slot_result - Handle completed PostgreSQL query
// ============================================================================
void SidecarDbPool::process_slot_result(ConnectionSlot& slot) {
                    auto query_end = std::chrono::steady_clock::now();
                    auto query_time = std::chrono::duration_cast<std::chrono::microseconds>(
                        query_end - slot.query_start).count();
                    
                    total_query_time_us_ += query_time;
    
    // Stop watching this slot
    stop_watching_slot(slot);
                    
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
                        
                        const char* batch_op_name = "UNKNOWN";
                        switch (slot.op_type) {
                            case SidecarOpType::PUSH: batch_op_name = "PUSH"; break;
                            case SidecarOpType::POP: batch_op_name = "POP"; break;
                            case SidecarOpType::POP_BATCH: batch_op_name = "POP_BATCH"; break;
                            case SidecarOpType::POP_WAIT: batch_op_name = "POP_WAIT"; break;
                            case SidecarOpType::ACK: batch_op_name = "ACK"; break;
                            case SidecarOpType::ACK_BATCH: batch_op_name = "ACK_BATCH"; break;
                            case SidecarOpType::TRANSACTION: batch_op_name = "TRANSACTION"; break;
                            case SidecarOpType::RENEW_LEASE: batch_op_name = "RENEW_LEASE"; break;
                        }
                        spdlog::info("[Worker {}] [Sidecar] {} TIMING: queue_wait={}us, db_exec={}us | {} requests, {} items",
                                    worker_id_, batch_op_name, slot.queue_wait_us, query_time,
                                    slot.batched_requests.size(), slot.total_items);
                        
                        // Update per-op stats
                        {
                            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                            auto& stats = op_stats_[slot.op_type];
                            stats.count++;
                            stats.total_time_us += query_time;
                            stats.items_processed += slot.total_items;
                        }
                        
                        // Split results by request using the 'index' or 'idx' field from stored procedure
                        for (const auto& info : slot.batched_requests) {
                            SidecarResponse resp;
                            resp.op_type = slot.op_type;
                            resp.request_id = info.request_id;
                            resp.query_time_us = query_time;
            resp.push_targets = info.push_targets;
                            
                            if (db_success && all_results.is_array()) {
                                nlohmann::json request_results = nlohmann::json::array();
                                
                for (const auto& res : all_results) {
                    int idx = res.value("index", res.value("idx", -1));
                                    if (idx >= static_cast<int>(info.start_index) && 
                                        idx < static_cast<int>(info.start_index + info.item_count)) {
                        request_results.push_back(res);
                                    }
                                }
                                resp.success = true;
                                resp.result_json = request_results.dump();
                            } else {
                                resp.success = false;
                                resp.error_message = error_msg.empty() ? "Unknown error" : error_msg;
                            }
                            
                            deliver_response(std::move(resp));
                        }
                        
                        slot.busy = false;
                        slot.is_batched = false;
                        slot.batched_requests.clear();
                        slot.request_id.clear();
                        slot.total_items = 0;
        return;
                    }
                    
    // Non-batched single request response
                    SidecarResponse resp;
                    resp.op_type = slot.op_type;
                    resp.request_id = slot.request_id;
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
                    
                    // Check if this POP was originally a POP_WAIT
                    bool was_pop_wait = false;
                    PopWaitTracker tracker;
                    if (slot.op_type == SidecarOpType::POP) {
                        std::lock_guard<std::mutex> lock(tracker_mutex_);
                        auto it = pop_wait_trackers_.find(slot.request_id);
                        if (it != pop_wait_trackers_.end()) {
                            was_pop_wait = true;
                            tracker = std::move(it->second);
                            pop_wait_trackers_.erase(it);
                        }
                    }
                    
                    // Log timing for single POP requests
                    {
                        const char* single_op_name = "UNKNOWN";
                        switch (slot.op_type) {
                            case SidecarOpType::PUSH: single_op_name = "PUSH"; break;
                            case SidecarOpType::POP: single_op_name = "POP"; break;
                            case SidecarOpType::POP_BATCH: single_op_name = "POP_BATCH"; break;
                            case SidecarOpType::POP_WAIT: single_op_name = "POP_WAIT"; break;
                            case SidecarOpType::ACK: single_op_name = "ACK"; break;
                            case SidecarOpType::ACK_BATCH: single_op_name = "ACK_BATCH"; break;
                            case SidecarOpType::TRANSACTION: single_op_name = "TRANSACTION"; break;
                            case SidecarOpType::RENEW_LEASE: single_op_name = "RENEW_LEASE"; break;
                        }
                        spdlog::info("[Worker {}] [Sidecar] {} TIMING: queue_wait={}us, db_exec={}us | single request",
                                    worker_id_, single_op_name, slot.queue_wait_us, query_time);
                    }
                    
                    if (was_pop_wait) {
                        std::string group_key = make_group_key(
                            tracker.queue_name, tracker.partition_name, tracker.consumer_group);
                        
                        bool has_messages = false;
                        if (resp.success && !resp.result_json.empty()) {
                            try {
                                auto json = nlohmann::json::parse(resp.result_json);
                                if (json.contains("messages") && json["messages"].is_array()) {
                                    has_messages = !json["messages"].empty();
                                }
            } catch (...) {}
                        }
                        
                        if (global_shared_state) {
                            global_shared_state->release_group(group_key, has_messages);
                        }
                        
                        auto now = std::chrono::steady_clock::now();
                        
                        if (has_messages || now >= tracker.wait_deadline || !resp.success) {
            resp.op_type = SidecarOpType::POP_WAIT;
                            {
                                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                                auto& stats = op_stats_[SidecarOpType::POP_WAIT];
                                stats.count++;
                                stats.total_time_us += query_time;
                            }
                            deliver_response(std::move(resp));
                        } else {
            // Re-queue for later check
                            SidecarRequest new_req;
                            new_req.op_type = SidecarOpType::POP_WAIT;
                            new_req.request_id = slot.request_id;
                            new_req.wait_deadline = tracker.wait_deadline;
                            new_req.queue_name = tracker.queue_name;
                            new_req.partition_name = tracker.partition_name;
                            new_req.consumer_group = tracker.consumer_group;
                            new_req.batch_size = tracker.batch_size;
                            new_req.subscription_mode = tracker.subscription_mode;
                            new_req.subscription_from = tracker.subscription_from;
                            new_req.sql = tracker.sql;
                            new_req.params = tracker.params;
                            
                            auto interval_ms = std::chrono::milliseconds(100);
                            if (global_shared_state) {
                                interval_ms = global_shared_state->get_group_interval(group_key);
                            }
            new_req.next_check = now + interval_ms;
                            
                                std::lock_guard<std::mutex> lock(waiting_mutex_);
                                waiting_requests_.push_back(std::move(new_req));
                        }
                    } else {
                        {
                            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                            auto& stats = op_stats_[slot.op_type];
                            stats.count++;
                            stats.total_time_us += query_time;
                            stats.items_processed += slot.total_items;
                        }
                        deliver_response(std::move(resp));
                    }
                    
                    slot.busy = false;
                    slot.request_id.clear();
                    slot.total_items = 0;
}

} // namespace queen
