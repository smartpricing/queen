# Long-Polling Architecture Redesign

## Problem with Current Implementation

**Current flow:**
1. Client sends long-polling request
2. Request registered in ResponseRegistry
3. Submitted to ThreadPool
4. **Thread blocks for up to 30 seconds** polling the database
5. Thread completes, pushes to response queue
6. Response timer sends result

**Issues:**
- Each long-polling request blocks a ThreadPool thread
- With N concurrent requests, need N threads
- Thread pool exhaustion when many clients poll empty queues
- Poor resource utilization (threads sleeping 99% of the time)

**Example failure:**
- 6 concurrent long-poll requests
- Only 4 ThreadPool threads available
- 2 requests queue up, adding 1-2s latency
- Total time: 1-2s + 30s = 31-32s → client timeout at 35s

## Proposed Solution: Registry of Intentions

### Architecture Overview

**New flow:**
1. Client sends long-polling request
2. **Register intention** (queue/partition/timeout) in `PollIntentionRegistry`
3. Return immediately to uWebSockets thread (no ThreadPool submission)
4. **Background worker loop** (1-2 dedicated threads):
   - Periodically scans all registered intentions
   - Batches DB queries for multiple intentions
   - When data found, sends response immediately
   - Cleans up expired intentions
5. Response delivered via existing ResponseQueue mechanism

### Components

#### 1. PollIntentionRegistry
```cpp
struct PollIntention {
    std::string request_id;
    std::string queue_name;
    std::optional<std::string> partition_name;
    std::string consumer_group;
    int batch_size;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point created_at;
};

class PollIntentionRegistry {
private:
    std::unordered_map<std::string, PollIntention> intentions_;
    mutable std::mutex mutex_;
    
public:
    void register_intention(const PollIntention& intention);
    void remove_intention(const std::string& request_id);
    std::vector<PollIntention> get_active_intentions();
    void cleanup_expired();
};
```

#### 2. PollWorker Loop
```cpp
void poll_worker_thread(
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<ResponseQueue> response_queue,
    std::shared_ptr<QueueManager> queue_manager
) {
    while (!shutdown) {
        auto intentions = registry->get_active_intentions();
        
        // Group by queue/partition for batch processing
        auto grouped = group_intentions_by_queue(intentions);
        
        for (auto& [queue_key, batch] : grouped) {
            try {
                // Single DB query for all intentions on this queue
                auto result = queue_manager->pop_messages(
                    batch[0].queue_name,
                    batch[0].partition_name,
                    batch[0].consumer_group,
                    {.wait = false, .batch = total_batch_size}
                );
                
                if (!result.messages.empty()) {
                    // Distribute messages to waiting clients
                    distribute_messages(result.messages, batch, response_queue);
                    
                    // Remove fulfilled intentions
                    for (auto& intention : batch) {
                        registry->remove_intention(intention.request_id);
                    }
                }
            } catch (const std::exception& e) {
                // Handle errors
            }
        }
        
        // Check for expired intentions
        for (auto& intention : intentions) {
            if (std::chrono::steady_clock::now() >= intention.deadline) {
                // Send empty response (timeout)
                send_empty_response(intention.request_id, response_queue);
                registry->remove_intention(intention.request_id);
            }
        }
        
        // Adaptive sleep based on queue activity
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval));
    }
}
```

#### 3. Updated Request Handler
```cpp
app->get("/api/v1/pop/queue/:queue", [registry](auto* res, auto* req) {
    std::string queue_name = std::string(req->getParameter(0));
    bool wait = get_query_param_bool(req, "wait", false);
    int timeout_ms = get_query_param_int(req, "timeout", 30000);
    int batch = get_query_param_int(req, "batch", 1);
    
    if (wait) {
        // Register intention instead of blocking
        std::string request_id = global_response_registry->register_response(res);
        
        PollIntention intention{
            .request_id = request_id,
            .queue_name = queue_name,
            .partition_name = std::nullopt,
            .consumer_group = consumer_group,
            .batch_size = batch,
            .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
            .created_at = std::chrono::steady_clock::now()
        };
        
        registry->register_intention(intention);
        
        spdlog::info("[Worker {}] Registered poll intention {} for queue {}", 
                    worker_id, request_id, queue_name);
        
        // Return immediately - worker loop will handle it
        return;
    } else {
        // Non-waiting mode: use existing ThreadPool approach
        // ... existing code ...
    }
});
```

### Benefits

1. **Resource Efficiency**
   - 1-2 worker threads handle ALL long-polling requests
   - No thread per request
   - ThreadPool freed for other operations

2. **Better Batching**
   - Can query once for multiple clients waiting on same queue
   - Reduce DB load

3. **Predictable Performance**
   - No ThreadPool exhaustion
   - Consistent latency regardless of concurrent requests
   - Can handle 100+ concurrent long-polling clients

4. **Adaptive Polling**
   - Increase poll interval when queues are empty
   - Decrease when activity detected
   - Reduce unnecessary DB queries

### Implementation Plan

**Phase 1: Core Components**
- [ ] Create `PollIntentionRegistry` class
- [ ] Implement `poll_worker_thread` function
- [ ] Add intention registration to request handlers

**Phase 2: Optimization**
- [ ] Implement batched DB queries for multiple intentions
- [ ] Add adaptive polling intervals
- [ ] Message distribution logic for batch requests

**Phase 3: Testing & Migration**
- [ ] Add feature flag to switch between old/new implementation
- [ ] Performance testing with many concurrent clients
- [ ] Gradual rollout

**Phase 4: Advanced Features**
- [ ] PostgreSQL LISTEN/NOTIFY integration (future)
- [ ] Priority-based intention processing
- [ ] Per-queue worker pools

## Configuration

```cpp
struct LongPollingConfig {
    bool use_intention_registry = true;  // Feature flag
    int poll_worker_threads = 2;         // Number of worker threads
    int min_poll_interval = 100;         // Minimum poll interval (ms)
    int max_poll_interval = 2000;        // Maximum poll interval (ms)
    int max_intentions_per_worker = 1000; // Scale limit
};
```

## Migration Strategy

1. Implement new architecture alongside old one
2. Use environment variable to toggle: `USE_INTENTION_REGISTRY=true`
3. Test in staging with gradual rollout
4. Monitor metrics: latency, throughput, resource usage
5. Full migration when proven stable
6. Remove old code

## Estimated Impact

**Current (DB_POOL_SIZE=20):**
- 19 ThreadPool threads
- Max ~15 concurrent long-polling requests (with headroom)
- High memory usage (19 blocked threads)

**New (Registry of Intentions):**
- 2 dedicated poll workers
- Unlimited concurrent long-polling requests
- Low memory footprint
- Better DB efficiency through batching

## Notes

- This is similar to how Redis, NATS, and modern message queues work
- Future: Could integrate with PostgreSQL NOTIFY for true event-driven polling
- Backward compatible - can run both implementations simultaneously

