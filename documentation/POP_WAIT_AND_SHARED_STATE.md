# POP_WAIT via Sidecar + SharedStateManager

## Executive Summary

Replace the current poll worker architecture for long-polling (`wait=true`) with a unified sidecar-based approach, using `SharedStateManager` as the single coordination layer for both local (in-memory) and cluster (UDP) notifications.

**Current Architecture:**
- `PollIntentionRegistry` tracks waiting requests
- Poll worker threads continuously query DB
- `ResponseQueue` per worker for cross-thread delivery
- Timer callbacks poll response queues
- `AsyncQueueManager::pop_messages_sp()` separate from sidecar

**New Architecture:**
- Each sidecar has its own waiting queue
- `SharedStateManager` coordinates notifications (local + UDP)
- Push triggers immediate notification to all waiters
- Group backoff prevents redundant queries
- Single code path: all POPs go through sidecar

## Components to Modify

### 1. SharedStateManager (`server/include/queen/shared_state_manager.hpp`)

Add new tiers for sidecar coordination:

```cpp
// ============================================================
// Tier 6: Local Sidecar Registry
// ============================================================
private:
    std::vector<SidecarDbPool*> local_sidecars_;
    mutable std::shared_mutex sidecar_mutex_;

public:
    void register_sidecar(SidecarDbPool* sidecar);
    void unregister_sidecar(SidecarDbPool* sidecar);

// ============================================================
// Tier 7: Group Backoff Coordination (local only, not synced via UDP)
// ============================================================
private:
    struct GroupBackoffState {
        std::chrono::steady_clock::time_point last_checked;
        int consecutive_empty = 0;
        std::chrono::milliseconds current_interval{100};
        std::atomic<bool> in_flight{false};
    };
    std::unordered_map<std::string, GroupBackoffState> group_backoff_;
    mutable std::shared_mutex backoff_mutex_;

    // Configuration (from InterInstanceConfig or hardcoded defaults)
    int backoff_threshold_ = 3;
    double backoff_multiplier_ = 2.0;
    int max_interval_ms_ = 1000;
    int base_interval_ms_ = 100;

public:
    bool should_check_group(const std::string& group_key);
    bool try_acquire_group(const std::string& group_key);
    void release_group(const std::string& group_key, bool had_messages);
    std::chrono::milliseconds get_group_interval(const std::string& group_key);
    void reset_backoff_for_queue(const std::string& queue_name);

private:
    void notify_local_sidecars(const std::string& queue_name);
```

### 2. SharedStateManager (`server/src/services/shared_state_manager.cpp`)

#### 2.1 Sidecar Registration

```cpp
void SharedStateManager::register_sidecar(SidecarDbPool* sidecar) {
    std::unique_lock lock(sidecar_mutex_);
    local_sidecars_.push_back(sidecar);
    spdlog::debug("SharedState: Registered sidecar (total: {})", local_sidecars_.size());
}

void SharedStateManager::unregister_sidecar(SidecarDbPool* sidecar) {
    std::unique_lock lock(sidecar_mutex_);
    local_sidecars_.erase(
        std::remove(local_sidecars_.begin(), local_sidecars_.end(), sidecar),
        local_sidecars_.end()
    );
}

void SharedStateManager::notify_local_sidecars(const std::string& queue_name) {
    std::shared_lock lock(sidecar_mutex_);
    for (auto* sidecar : local_sidecars_) {
        if (sidecar) {
            sidecar->notify_queue_activity(queue_name);
        }
    }
}
```

#### 2.2 Modified notify_message_available

```cpp
void SharedStateManager::notify_message_available(const std::string& queue, const std::string& partition) {
    // ALWAYS: Local notification (works in single-node mode)
    notify_local_sidecars(queue);
    reset_backoff_for_queue(queue);
    
    // CLUSTER: UDP broadcast (if enabled)
    if (running_ && transport_) {
        nlohmann::json payload = {
            {"queue", queue},
            {"partition", partition},
            {"ts", now_ms()}
        };
        
        auto servers = get_servers_for_queue(queue);
        if (servers.empty()) {
            transport_->broadcast(UDPSyncMessageType::MESSAGE_AVAILABLE, payload);
        } else {
            servers.erase(server_id_);
            if (!servers.empty()) {
                transport_->send_to(servers, UDPSyncMessageType::MESSAGE_AVAILABLE, payload);
            }
        }
    }
}
```

#### 2.3 Modified handle_message_available

```cpp
void SharedStateManager::handle_message_available(const std::string& sender, const nlohmann::json& payload) {
    std::string queue = payload.value("queue", "");
    spdlog::debug("SharedState: MESSAGE_AVAILABLE from {} for {}", sender, queue);
    
    // Forward to local sidecars
    notify_local_sidecars(queue);
    reset_backoff_for_queue(queue);
}
```

#### 2.4 Group Backoff Implementation

```cpp
bool SharedStateManager::should_check_group(const std::string& group_key) {
    std::shared_lock lock(backoff_mutex_);
    auto it = group_backoff_.find(group_key);
    if (it == group_backoff_.end()) return true;
    
    auto now = std::chrono::steady_clock::now();
    return (now - it->second.last_checked) >= it->second.current_interval;
}

bool SharedStateManager::try_acquire_group(const std::string& group_key) {
    std::unique_lock lock(backoff_mutex_);
    auto& state = group_backoff_[group_key];
    bool expected = false;
    return state.in_flight.compare_exchange_strong(expected, true);
}

void SharedStateManager::release_group(const std::string& group_key, bool had_messages) {
    std::unique_lock lock(backoff_mutex_);
    auto it = group_backoff_.find(group_key);
    if (it == group_backoff_.end()) return;
    
    auto& state = it->second;
    state.last_checked = std::chrono::steady_clock::now();
    
    if (had_messages) {
        state.consecutive_empty = 0;
        state.current_interval = std::chrono::milliseconds(base_interval_ms_);
    } else {
        state.consecutive_empty++;
        if (state.consecutive_empty >= backoff_threshold_) {
            int new_interval = static_cast<int>(state.current_interval.count() * backoff_multiplier_);
            state.current_interval = std::chrono::milliseconds(std::min(new_interval, max_interval_ms_));
        }
    }
    state.in_flight = false;
}

std::chrono::milliseconds SharedStateManager::get_group_interval(const std::string& group_key) {
    std::shared_lock lock(backoff_mutex_);
    auto it = group_backoff_.find(group_key);
    if (it == group_backoff_.end()) {
        return std::chrono::milliseconds(base_interval_ms_);
    }
    return it->second.current_interval;
}

void SharedStateManager::reset_backoff_for_queue(const std::string& queue_name) {
    std::unique_lock lock(backoff_mutex_);
    std::string prefix = queue_name + "/";
    for (auto& [key, state] : group_backoff_) {
        if (key.rfind(prefix, 0) == 0) {  // starts_with
            state.consecutive_empty = 0;
            state.current_interval = std::chrono::milliseconds(base_interval_ms_);
        }
    }
}
```

#### 2.5 Stats Extension

Add to `get_stats()`:

```cpp
{
    std::shared_lock lock(sidecar_mutex_);
    stats["local_sidecars"] = local_sidecars_.size();
}
{
    std::shared_lock lock(backoff_mutex_);
    int in_flight = 0, backed_off = 0;
    for (const auto& [k, s] : group_backoff_) {
        if (s.in_flight) in_flight++;
        if (s.consecutive_empty >= backoff_threshold_) backed_off++;
    }
    stats["group_backoff"] = {
        {"groups_tracked", group_backoff_.size()},
        {"groups_in_flight", in_flight},
        {"groups_backed_off", backed_off}
    };
}
```

### 3. SidecarDbPool (`server/include/queen/sidecar_db_pool.hpp`)

#### 3.1 New Operation Type

```cpp
enum class SidecarOpType {
    PUSH,
    POP,            // Immediate pop (wait=false)
    POP_WAIT,       // Long-poll pop (wait=true) - NEW
    ACK,
    ACK_BATCH,
    TRANSACTION,
    RENEW_LEASE
};
```

#### 3.2 Extended Request Structure

```cpp
struct SidecarRequest {
    SidecarOpType op_type = SidecarOpType::PUSH;
    std::string request_id;
    std::string sql;
    std::vector<std::string> params;
    size_t item_count = 0;
    std::chrono::steady_clock::time_point queued_at;
    
    // For POP_WAIT only:
    std::chrono::steady_clock::time_point wait_deadline;  // Absolute timeout
    std::chrono::steady_clock::time_point next_check;     // When to check next
    std::string queue_name;       // For notification matching
    std::string partition_name;   // For grouping
    std::string consumer_group;   // For grouping
    int batch_size = 1;           // Client's requested batch size
    std::string subscription_mode;
    std::string subscription_from;
};
```

#### 3.3 New Members

```cpp
class SidecarDbPool {
    // ... existing members ...
    
    // Waiting queue for POP_WAIT requests
    std::deque<SidecarRequest> waiting_requests_;
    mutable std::mutex waiting_mutex_;
    
public:
    // Called by SharedStateManager when messages may be available
    void notify_queue_activity(const std::string& queue_name);
    
private:
    // Process waiting requests that are due
    void process_waiting_queue();
    
    // Build group key for backoff coordination
    static std::string make_group_key(const std::string& queue, 
                                       const std::string& partition,
                                       const std::string& consumer_group);
};
```

### 4. SidecarDbPool (`server/src/database/sidecar_db_pool.cpp`)

#### 4.1 notify_queue_activity

```cpp
void SidecarDbPool::notify_queue_activity(const std::string& queue_name) {
    auto now = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(waiting_mutex_);
    for (auto& req : waiting_requests_) {
        if (req.queue_name == queue_name) {
            req.next_check = now;  // Trigger immediate check
        }
    }
}
```

#### 4.2 Modified poller_loop

Add at the beginning of the main loop:

```cpp
void SidecarDbPool::poller_loop() {
    while (running_) {
        // ============================================================
        // STEP 0: PROCESS WAITING QUEUE (POP_WAIT)
        // ============================================================
        process_waiting_queue();
        
        // ... rest of existing loop (STEP 1-4) ...
    }
}
```

#### 4.3 process_waiting_queue

```cpp
void SidecarDbPool::process_waiting_queue() {
    auto now = std::chrono::steady_clock::now();
    std::vector<SidecarRequest> to_process;
    
    // Collect requests that are due or expired
    {
        std::lock_guard<std::mutex> lock(waiting_mutex_);
        auto it = waiting_requests_.begin();
        while (it != waiting_requests_.end()) {
            if (now >= it->wait_deadline) {
                // Expired - send empty response
                SidecarResponse resp;
                resp.op_type = SidecarOpType::POP_WAIT;
                resp.request_id = it->request_id;
                resp.success = true;
                resp.result_json = R"({"messages":[]})";
                resp.query_time_us = 0;
                deliver_response(std::move(resp));
                it = waiting_requests_.erase(it);
            } else if (now >= it->next_check) {
                // Due for check
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
        if (!queen::global_shared_state->should_check_group(group_key)) {
            // Not due yet - re-queue with updated next_check
            req.next_check = now + queen::global_shared_state->get_group_interval(group_key);
            std::lock_guard<std::mutex> lock(waiting_mutex_);
            waiting_requests_.push_back(std::move(req));
            continue;
        }
        
        // Try to acquire group lock
        if (!queen::global_shared_state->try_acquire_group(group_key)) {
            // Another sidecar is querying - wait briefly
            req.next_check = now + std::chrono::milliseconds(10);
            std::lock_guard<std::mutex> lock(waiting_mutex_);
            waiting_requests_.push_back(std::move(req));
            continue;
        }
        
        // Move to pending queue for execution
        // Convert POP_WAIT to POP for the actual query
        req.op_type = SidecarOpType::POP;
        
        // Store original data for post-processing
        auto original_deadline = req.wait_deadline;
        auto original_queue = req.queue_name;
        auto original_partition = req.partition_name;
        auto original_consumer_group = req.consumer_group;
        
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_requests_.push_back(std::move(req));
        }
        
        // Note: release_group and re-queuing happens in result handling
        // We need to track this request to handle its result specially
    }
}

std::string SidecarDbPool::make_group_key(const std::string& queue,
                                           const std::string& partition,
                                           const std::string& consumer_group) {
    return queue + "/" + (partition.empty() ? "*" : partition) + "/" + consumer_group;
}
```

#### 4.4 Handle POP_WAIT Results

In the result handling section (after query completes), add special handling:

```cpp
// After getting result for a POP that was originally POP_WAIT:
if (was_pop_wait) {
    std::string group_key = make_group_key(queue_name, partition_name, consumer_group);
    bool had_messages = !result.messages.empty();
    
    // Release group and update backoff
    queen::global_shared_state->release_group(group_key, had_messages);
    
    if (had_messages || now >= original_deadline) {
        // Done - deliver response
        deliver_response(std::move(resp));
    } else {
        // No messages, not expired - re-queue
        SidecarRequest new_req;
        new_req.op_type = SidecarOpType::POP_WAIT;
        new_req.request_id = original_request_id;
        // ... copy all original fields ...
        new_req.next_check = now + queen::global_shared_state->get_group_interval(group_key);
        
        std::lock_guard<std::mutex> lock(waiting_mutex_);
        waiting_requests_.push_back(std::move(new_req));
    }
}
```

### 5. Routes

#### 5.1 pop.cpp - Submit POP_WAIT to Sidecar

For `wait=true` requests, instead of registering with `PollIntentionRegistry`:

```cpp
// In pop route handler, when wait=true:
if (wait_param) {
    SidecarRequest req;
    req.op_type = SidecarOpType::POP_WAIT;
    req.request_id = request_id;
    req.queue_name = queue_name;
    req.partition_name = partition_name;
    req.consumer_group = consumer_group;
    req.batch_size = batch_size;
    req.subscription_mode = subscription_mode;
    req.subscription_from = subscription_from;
    req.wait_deadline = std::chrono::steady_clock::now() + 
                        std::chrono::seconds(timeout_seconds);
    req.next_check = std::chrono::steady_clock::now();  // Check immediately
    
    // Build SQL for pop_messages_v2
    req.sql = "SELECT queen.pop_messages_v2($1, $2, $3, $4, $5, $6, $7, $8)";
    req.params = { /* ... same as regular POP ... */ };
    
    ctx.sidecar->submit(std::move(req));
    return;  // Response will be delivered via callback
}
```

#### 5.2 push.cpp - Notify on Success

After successful push:

```cpp
// In sidecar callback, after successful push:
if (resp.success) {
    // Notify via SharedStateManager (handles both local + UDP)
    if (queen::global_shared_state) {
        queen::global_shared_state->notify_message_available(queue_name, partition_name);
    }
}
```

#### 5.3 ack.cpp - Notify Partition Free

After successful ACK (partition may now be available for other consumers):

```cpp
if (resp.success) {
    if (queen::global_shared_state) {
        queen::global_shared_state->notify_partition_free(queue_name, partition_name, consumer_group);
    }
}
```

### 6. acceptor_server.cpp

#### 6.1 Register Sidecars with SharedStateManager

After creating each worker's sidecar:

```cpp
// After worker_sidecar->start():
if (queen::global_shared_state) {
    queen::global_shared_state->register_sidecar(worker_sidecar.get());
}

// Store reference for cleanup
worker_sidecars.push_back(worker_sidecar.get());
```

#### 6.2 Remove Poll Worker Initialization

Remove or comment out:

```cpp
// REMOVE: queen::init_long_polling(...);
```

#### 6.3 Remove Response Queue Timer for Long-Polling

The response timer can be simplified - only needed for stream poll workers now, not regular long-polling.

### 7. Components to Remove/Deprecate

| File | What to Remove |
|------|----------------|
| `poll_worker.cpp` | `poll_worker_loop()` function (keep `init_long_polling` but empty or remove) |
| `poll_intention_registry.hpp/cpp` | Backoff state (moved to SharedStateManager), possibly entire file if only used for long-polling |
| `acceptor_server.cpp` | `init_long_polling()` call, response timer long-poll handling |
| `async_queue_manager.hpp/cpp` | `pop_messages_sp()` if only used by poll workers |

### 8. Thread Pool Sizing

Update the calculation in `acceptor_server.cpp`:

```cpp
// BEFORE:
int db_thread_pool_size = poll_workers + stream_workers + stream_concurrent + sidecar_pollers + service_threads;

// AFTER (no poll_workers needed):
int db_thread_pool_size = stream_workers + stream_concurrent + sidecar_pollers + service_threads;
```

### 9. Configuration

#### 9.1 New Config Options (config.hpp)

```cpp
struct QueueConfig {
    // ... existing ...
    
    // POP_WAIT backoff configuration
    int pop_wait_base_interval_ms = 100;
    int pop_wait_max_interval_ms = 1000;
    int pop_wait_backoff_threshold = 3;
    double pop_wait_backoff_multiplier = 2.0;
};
```

#### 9.2 Environment Variables

```
POP_WAIT_BASE_INTERVAL_MS=100
POP_WAIT_MAX_INTERVAL_MS=1000
POP_WAIT_BACKOFF_THRESHOLD=3
POP_WAIT_BACKOFF_MULTIPLIER=2.0
```

### 10. Flow Diagrams

#### 10.1 POP with wait=true (New)

```
Client                Route              Sidecar           SharedStateManager
  │                     │                   │                      │
  │── POP wait=true ───▶│                   │                      │
  │                     │── submit ────────▶│                      │
  │                     │   POP_WAIT        │                      │
  │                     │                   │── should_check? ────▶│
  │                     │                   │◀── yes ──────────────│
  │                     │                   │── try_acquire ──────▶│
  │                     │                   │◀── ok ───────────────│
  │                     │                   │                      │
  │                     │                   │── query DB           │
  │                     │                   │                      │
  │                     │                   │── release_group ────▶│
  │                     │                   │   (had_messages)     │
  │                     │                   │                      │
  │◀── defer(response) ─┼───────────────────│                      │
```

#### 10.2 Push Notification Flow

```
Producer             Route              Sidecar           SharedStateManager        Other Sidecars
   │                   │                   │                      │                      │
   │── PUSH ──────────▶│                   │                      │                      │
   │                   │── submit ────────▶│                      │                      │
   │                   │                   │── query DB           │                      │
   │                   │                   │                      │                      │
   │                   │                   │── notify_message ───▶│                      │
   │                   │                   │   _available()       │                      │
   │                   │                   │                      │── notify_local ─────▶│
   │                   │                   │                      │   _sidecars()        │
   │                   │                   │                      │                      │
   │                   │                   │                      │── UDP broadcast ────▶│(other servers)
   │                   │                   │                      │                      │
   │◀── 201 ───────────┼───────────────────│                      │                      │
```

### 11. Testing Checklist

- [ ] Single-node mode (no UDP) - notifications still work locally
- [ ] Multi-node mode - notifications propagate via UDP
- [ ] Backoff increases after consecutive empty results
- [ ] Backoff resets on push notification
- [ ] Group locking prevents duplicate queries
- [ ] Timeout fires correctly (empty response at deadline)
- [ ] Early return when messages found
- [ ] Consumer group isolation
- [ ] Partition-specific long-polling
- [ ] Mixed wait=true and wait=false requests
- [ ] High concurrency (many consumers waiting)
- [ ] Server restart (sidecars re-register)

### 12. Migration Steps

1. **Phase 1: Add infrastructure**
   - Add Tier 6 & 7 to SharedStateManager
   - Add waiting queue to SidecarDbPool
   - Add `notify_queue_activity()` method

2. **Phase 2: Wire up notifications**
   - Modify `notify_message_available()` to call local sidecars
   - Add notification calls in push.cpp and ack.cpp

3. **Phase 3: Implement POP_WAIT**
   - Add POP_WAIT op type and request fields
   - Implement `process_waiting_queue()`
   - Modify pop.cpp to submit POP_WAIT to sidecar

4. **Phase 4: Remove old system**
   - Remove `init_long_polling()` call
   - Remove poll worker thread pool reservation
   - Clean up unused code

5. **Phase 5: Test and tune**
   - Run existing long-polling tests
   - Tune backoff parameters
   - Load test with many concurrent consumers

