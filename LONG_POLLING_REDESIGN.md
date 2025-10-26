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
4. **Reserved ThreadPool workers** (2 threads reserved at startup):
   - Periodically scans all registered intentions
   - Groups intentions by (queue, partition, consumer_group)
   - Executes one query per group (batch processing)
   - When messages found, submits pop to ThreadPool (optionally with priority)
   - Cleans up expired intentions
5. Response delivered via existing ResponseQueue mechanism

**Key difference from current:** Instead of N threads blocking for 30s each, 2 threads actively check all intentions every 100ms.

### Components

#### 1. PollIntentionRegistry
```cpp
struct PollIntention {
    std::string request_id;
    
    // Queue-based polling
    std::optional<std::string> queue_name;
    std::optional<std::string> partition_name;
    
    // Namespace/task-based polling
    std::optional<std::string> namespace_name;
    std::optional<std::string> task_name;
    
    std::string consumer_group;
    int batch_size;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point created_at;
    
    // Helper to create grouping key
    std::string grouping_key() const {
        if (queue_name.has_value()) {
            return queue_name.value() + ":" + 
                   partition_name.value_or("*") + ":" + 
                   consumer_group;
        } else {
            return namespace_name.value_or("*") + ":" + 
                   task_name.value_or("*") + ":" + 
                   consumer_group;
        }
    }
};

class PollIntentionRegistry {
private:
    std::unordered_map<std::string, PollIntention> intentions_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_{true};
    
public:
    void register_intention(const PollIntention& intention);
    void remove_intention(const std::string& request_id);
    std::vector<PollIntention> get_active_intentions();
    void cleanup_expired();
    void shutdown() { running_ = false; }
    bool is_running() const { return running_; }
};
```

#### 2. ThreadPool Startup (Reserve Workers)
```cpp
// Called at server startup
void init_long_polling(
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<QueueManager> queue_manager
) {
    // Push 2 never-returning jobs to ThreadPool
    // This reserves 2 threads for polling
    for (int worker_id = 0; worker_id < 2; worker_id++) {
        thread_pool->push([=]() {
            poll_worker_loop(worker_id, registry, queue_manager, thread_pool);
        });
    }
    
    spdlog::info("Long-polling: 2 poll workers reserved from ThreadPool");
}
```

#### 3. PollWorker Loop
```cpp
void poll_worker_loop(
    int worker_id,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<QueueManager> queue_manager,
    std::shared_ptr<astp::ThreadPool> thread_pool
) {
    spdlog::info("Poll worker {} started", worker_id);
    
    while (registry->is_running()) {
        auto intentions = registry->get_active_intentions();
        
        // Split work between workers (load balancing)
        auto my_intentions = filter_by_worker(intentions, worker_id, 2);
        
        // Group by (queue, partition, consumer_group) for batching
        auto grouped = group_intentions(my_intentions);
        
        for (auto& [key, batch] : grouped) {
            try {
                // Lightweight check: count available messages
                int available = queue_manager->count_available_messages(
                    batch[0].queue_name,
                    batch[0].partition_name,
                    batch[0].consumer_group
                );
                
                if (available > 0) {
                    // Messages available! Submit actual pop to ThreadPool
                    // Using dg_now() for priority (jumps to front of queue)
                    std::string job_id = "pop_" + generate_unique_id();
                    
                    thread_pool->dg_now(job_id, [=]() {
                        // Calculate total batch size for all waiting clients
                        int total_batch = 0;
                        for (auto& intention : batch) {
                            total_batch += intention.batch_size;
                        }
                        
                        // Execute the actual pop with locking
                        PopResult result;
                        if (batch[0].queue_name.has_value()) {
                            result = queue_manager->pop_from_queue_partition(
                                batch[0].queue_name.value(),
                                batch[0].partition_name.value_or(""),
                                batch[0].consumer_group,
                                {.wait = false, .batch = total_batch}
                            );
                        } else {
                            result = queue_manager->pop_with_namespace_task(
                                batch[0].namespace_name,
                                batch[0].task_name,
                                batch[0].consumer_group,
                                {.wait = false, .batch = total_batch}
                            );
                        }
                        
                        if (!result.messages.empty()) {
                            // Distribute messages to waiting clients
                            distribute_to_clients(result, batch);
                            
                            // Remove fulfilled intentions
                            for (auto& intention : batch) {
                                registry->remove_intention(intention.request_id);
                            }
                        }
                    });
                }
            } catch (const std::exception& e) {
                spdlog::error("Poll worker {} error: {}", worker_id, e.what());
            }
        }
        
        // Check for expired intentions (timeouts)
        auto now = std::chrono::steady_clock::now();
        for (auto& intention : my_intentions) {
            if (now >= intention.deadline) {
                // Timeout - send empty response
                global_response_queue->push(
                    intention.request_id, 
                    nlohmann::json{}, 
                    false, 
                    204  // No Content
                );
                registry->remove_intention(intention.request_id);
            }
        }
        
        // Adaptive sleep (future: adjust based on load)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    spdlog::info("Poll worker {} stopped", worker_id);
}

// Helper: Group intentions by unique (queue, partition, consumer_group)
std::map<std::string, std::vector<PollIntention>> 
group_intentions(const std::vector<PollIntention>& intentions) {
    std::map<std::string, std::vector<PollIntention>> grouped;
    for (auto& intention : intentions) {
        grouped[intention.grouping_key()].push_back(intention);
    }
    return grouped;
}

// Helper: Filter intentions for this worker (load balancing)
std::vector<PollIntention> 
filter_by_worker(const std::vector<PollIntention>& intentions, 
                 int worker_id, int total_workers) {
    std::vector<PollIntention> result;
    for (auto& intention : intentions) {
        // Hash-based distribution
        size_t hash = std::hash<std::string>{}(intention.grouping_key());
        if (hash % total_workers == worker_id) {
            result.push_back(intention);
        }
    }
    return result;
}
```

#### 4. Updated Request Handlers

All three POP routes need to be updated to support the registry:

**Route 1: `/api/v1/pop/queue/:queue` (any partition)**
```cpp
app->get("/api/v1/pop/queue/:queue", [registry](auto* res, auto* req) {
    std::string queue_name = std::string(req->getParameter(0));
    bool wait = get_query_param_bool(req, "wait", false);
    int timeout_ms = get_query_param_int(req, "timeout", config.queue.default_timeout);
    int batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
    std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
    
    if (wait) {
        // Register intention instead of blocking in ThreadPool
        std::string request_id = global_response_registry->register_response(res);
        
        PollIntention intention{
            .request_id = request_id,
            .queue_name = queue_name,
            .partition_name = std::nullopt,  // Any partition
            .namespace_name = std::nullopt,
            .task_name = std::nullopt,
            .consumer_group = consumer_group,
            .batch_size = batch,
            .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
            .created_at = std::chrono::steady_clock::now()
        };
        
        poll_intention_registry->register_intention(intention);
        
        spdlog::info("[Worker {}] Registered poll intention {} for queue {}", 
                    worker_id, request_id, queue_name);
        
        // Return immediately - poll workers will handle it
        return;
    } else {
        // Non-waiting mode: use existing ThreadPool approach (unchanged)
        std::string request_id = global_response_registry->register_response(res);
        db_thread_pool->push([=]() {
            auto result = queue_manager->pop_messages(queue_name, std::nullopt, 
                                                     consumer_group, 
                                                     {.wait = false, .batch = batch});
            // ... send response ...
        });
    }
});
```

**Route 2: `/api/v1/pop/queue/:queue/partition/:partition` (specific partition)**
```cpp
app->get("/api/v1/pop/queue/:queue/partition/:partition", [registry](auto* res, auto* req) {
    std::string queue_name = std::string(req->getParameter(0));
    std::string partition_name = std::string(req->getParameter(1));
    bool wait = get_query_param_bool(req, "wait", false);
    int timeout_ms = get_query_param_int(req, "timeout", config.queue.default_timeout);
    int batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
    std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
    
    if (wait) {
        std::string request_id = global_response_registry->register_response(res);
        
        PollIntention intention{
            .request_id = request_id,
            .queue_name = queue_name,
            .partition_name = partition_name,  // Specific partition
            .namespace_name = std::nullopt,
            .task_name = std::nullopt,
            .consumer_group = consumer_group,
            .batch_size = batch,
            .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
            .created_at = std::chrono::steady_clock::now()
        };
        
        poll_intention_registry->register_intention(intention);
        return;
    } else {
        // Non-waiting mode (unchanged)
        std::string request_id = global_response_registry->register_response(res);
        db_thread_pool->push([=]() {
            auto result = queue_manager->pop_messages(queue_name, partition_name, 
                                                     consumer_group, 
                                                     {.wait = false, .batch = batch});
            // ... send response ...
        });
    }
});
```

**Route 3: `/api/v1/pop` (namespace/task filtering)**
```cpp
app->get("/api/v1/pop", [registry](auto* res, auto* req) {
    std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
    std::string namespace_param = get_query_param(req, "namespace", "");
    std::string task_param = get_query_param(req, "task", "");
    
    std::optional<std::string> namespace_name = namespace_param.empty() ? 
        std::nullopt : std::optional<std::string>(namespace_param);
    std::optional<std::string> task_name = task_param.empty() ? 
        std::nullopt : std::optional<std::string>(task_param);
    
    bool wait = get_query_param_bool(req, "wait", false);
    int timeout_ms = get_query_param_int(req, "timeout", config.queue.default_timeout);
    int batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
    
    if (wait) {
        std::string request_id = global_response_registry->register_response(res);
        
        PollIntention intention{
            .request_id = request_id,
            .queue_name = std::nullopt,
            .partition_name = std::nullopt,
            .namespace_name = namespace_name,  // Namespace filtering
            .task_name = task_name,            // Task filtering
            .consumer_group = consumer_group,
            .batch_size = batch,
            .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
            .created_at = std::chrono::steady_clock::now()
        };
        
        poll_intention_registry->register_intention(intention);
        return;
    } else {
        // Non-waiting mode (unchanged)
        std::string request_id = global_response_registry->register_response(res);
        db_thread_pool->push([=]() {
            auto result = queue_manager->pop_with_namespace_task(namespace_name, task_name, 
                                                                 consumer_group, 
                                                                 {.wait = false, .batch = batch});
            // ... send response ...
        });
    }
});
```

### Benefits

1. **Resource Efficiency**
   - 2 reserved ThreadPool threads handle ALL long-polling requests
   - No thread per request (N long-polls → 2 workers, not N threads)
   - Remaining ThreadPool threads freed for PUSH/ACK/transactions

2. **Better Batching**
   - Groups intentions by (queue, partition, consumer_group)
   - One query per group instead of one per client
   - Example: 100 clients on same queue = 1 query, not 100

3. **Predictable Performance**
   - No ThreadPool exhaustion
   - Consistent 100-200ms latency (vs 30s blocking)
   - Can handle 1000+ concurrent long-polling clients

4. **Multi-Instance Compatible**
   - Works across load-balanced server instances
   - PostgreSQL `FOR UPDATE SKIP LOCKED` handles coordination
   - Consumer group leasing prevents duplicate delivery
   - No need for external coordination (Redis, etc.)

5. **Priority Execution**
   - Uses `dg_now()` to push poll-triggered pops to front of queue
   - Ensures long-poll responses get priority over normal operations

6. **Scalable**
   - Add more poll workers as load increases (see Capacity Planning)
   - Worker count configurable per deployment

### Capacity Planning

The system can handle varying loads by adjusting the number of poll workers:

| Concurrent Long-Polls | Unique Queue Groups | Poll Workers | Check Latency | Notes |
|----------------------|---------------------|--------------|---------------|-------|
| 100 | 50 | 2 | ~100ms | Light load, default config |
| 500 | 200 | 5 | ~150ms | Medium load |
| 1000 | 500 | 10 | ~200ms | High load |
| 5000 | 2000 | 20 | ~300ms | Very high load |

**Calculation:**
- Each unique (queue, partition, consumer_group) = 1 group
- Each lightweight check = ~2ms
- Workers split groups evenly via hash-based distribution
- Example: 1000 unique groups, 10 workers = 100 groups/worker × 2ms = 200ms

**Worst case:** 1000 clients all on different queues = 1000 groups = 2s with 2 workers
**Best case:** 1000 clients all on same queue = 1 group = 2ms with any number of workers

### Implementation Plan

**Phase 1: Core Components**
- [ ] Create `PollIntentionRegistry` class with thread-safe operations
- [ ] Implement `init_long_polling()` to reserve ThreadPool workers
- [ ] Implement `poll_worker_loop()` with grouping logic
- [ ] Add intention registration to POP request handlers (queue and namespace/task)
- [ ] Add `count_available_messages()` helper to QueueManager

**Phase 2: Distribution & Cleanup**
- [ ] Implement `distribute_to_clients()` message distribution logic
- [ ] Add timeout handling for expired intentions
- [ ] Implement `filter_by_worker()` for load balancing across workers

**Phase 3: Optimization**
- [ ] Add adaptive polling intervals (faster when active, slower when idle)
- [ ] Monitor and tune poll worker count based on load
- [ ] Add metrics for intention count, average latency, query batching efficiency

## Configuration

```cpp
struct LongPollingConfig {
    int poll_worker_count = 2;              // Number of reserved ThreadPool workers
    int poll_interval_ms = 100;             // How often workers check intentions
    bool use_priority_dispatch = true;      // Use dg_now() for priority
    int max_intentions = 10000;             // Safety limit for registry size
};
```

**Environment Variables:**
```bash
POLL_WORKER_COUNT=2                # Number of poll workers (default: 2)
POLL_INTERVAL_MS=100               # Poll interval in milliseconds (default: 100)
USE_PRIORITY_DISPATCH=true         # Use dg_now() for poll-triggered pops
```

**Scaling Guidelines:**
- Default (2 workers): Handles up to ~500 concurrent long-polls
- Medium (5 workers): Handles up to ~1500 concurrent long-polls
- High (10 workers): Handles up to ~3000 concurrent long-polls
- Very high (20 workers): Handles 5000+ concurrent long-polls

## Migration Strategy

1. Implement `PollIntentionRegistry` and poll worker infrastructure
2. Update all three POP routes to use registry when `wait=true`
3. Remove old `wait_for_messages()` blocking implementation
4. Deploy to staging and test:
   - Single client long-polling
   - Multiple clients on same queue (batching)
   - Multiple clients on different queues (load distribution)
   - Timeout handling
   - Multi-instance coordination
5. Monitor metrics: intention count, poll latency, query batching ratio, ThreadPool availability
6. Deploy to production

## Estimated Impact

**Current Implementation (DB_POOL_SIZE=20):**
- ThreadPool: 19 threads
- Max concurrent long-polls: ~15 (with headroom for other ops)
- Thread utilization: Poor (threads sleep 99% of time)
- Memory: High (19 × thread stack size)
- Latency: 30 seconds when queue empty
- Problem: ThreadPool exhaustion with many concurrent long-polls

**New Implementation (Registry of Intentions):**
- ThreadPool: 19 threads (2 reserved for polling, 17 for operations)
- Max concurrent long-polls: 1000+ (limited by worker count, not threads)
- Thread utilization: Excellent (poll workers always active, op threads never blocked)
- Memory: Same as current (no extra threads)
- Latency: 100-200ms typical (poll interval + query time)
- Benefit: No ThreadPool exhaustion, scales to thousands of clients

**Performance Comparison:**

| Metric | Current | New | Improvement |
|--------|---------|-----|-------------|
| Max concurrent long-polls | ~15 | 1000+ | **66x** |
| ThreadPool availability | Low (often exhausted) | High (17 always free) | **∞** |
| Long-poll latency | 30s (blocking) | 100-200ms | **150-300x faster** |
| DB queries (100 clients, same queue) | 100 | 1 | **100x fewer** |
| Scalability | Limited by threads | Limited by workers | Configurable |

## Notes

### Architecture Decisions

**Why reserve ThreadPool threads instead of separate threads?**
- Reuses existing ThreadPool infrastructure (no new thread management)
- Leverages `dg_now()` for priority dispatch (poll-triggered pops jump to front)
- Simpler code - all threading in one place
- Can scale worker count without managing separate thread pools

**Why use lightweight checks before actual pops?**
- Poll workers stay responsive (1-2ms checks vs 5ms full pops)
- With 1000 unique groups: 2s of checks vs 5s of pops
- Actual pops (with locking) only happen when messages exist
- Reduces database load when queues are empty

**How does it work across multiple server instances?**
- Each server instance has its own `PollIntentionRegistry`
- Each server's poll workers independently query PostgreSQL
- PostgreSQL's `FOR UPDATE SKIP LOCKED` prevents duplicate delivery
- Consumer group leasing ensures only one server gets each message
- No cross-instance coordination needed

### Similar Patterns
- Redis BLPOP/BRPOP: Registry + blocking wait
- NATS JetStream: Pull consumers with pending requests
- AWS SQS Long Polling: Registry of receive requests
- RabbitMQ: Consumer prefetch with pending deliveries

### Future Enhancements

**1. PostgreSQL NOTIFY Integration**
```cpp
// Instant wake-up when messages arrive
void poll_worker_loop() {
    while (running) {
        if (pg_wait_for_notify(100ms)) {
            // Message notification received, check immediately
            process_intentions();
        } else {
            // Timeout, periodic check
            process_intentions();
        }
    }
}
```

**2. Adaptive ThreadPool Sizing**
```cpp
// Monitor workload and adjust ThreadPool size dynamically
void adaptive_threadpool_monitor() {
    if (queue_size > pool_size * 2) {
        thread_pool->resize(pool_size + 5);  // Scale up
    } else if (queue_size == 0) {
        thread_pool->resize(pool_size - 5);  // Scale down
    }
}
```

**3. Adaptive Poll Intervals**
- Start at 100ms
- Increase to 500ms when queues idle
- Decrease to 50ms when active
- Reduces unnecessary DB queries during quiet periods

### Compatibility
- Direct replacement of old `wait_for_messages()` blocking implementation
- No breaking API changes - clients continue using `?wait=true` parameter
- Works seamlessly across multiple server instances (load balanced)
- Old behavior for `wait=false` remains unchanged (still uses ThreadPool directly)

