# Per-Worker Response Queue Implementation Guide

## Difficulty Assessment: **MODERATE** ⚙️⚙️

**Estimated Time**: 2-3 hours for implementation + 1 hour testing  
**Lines of Code Changed**: ~100-150 lines  
**Risk Level**: Low (well-isolated change, easy to test and rollback)

---

## Why This Is Relatively Easy

✅ **worker_id is already captured everywhere** - flows through all lambdas  
✅ **Clean architecture** - response queue/registry are well-separated  
✅ **No database schema changes** - purely in-memory structures  
✅ **Good test surface** - can verify with benchmark tool  
✅ **Backwards compatible** - doesn't affect external APIs

---

## Implementation Plan

### Phase 1: Update Data Structures (30 min)

#### File: `server/include/queen/response_queue.hpp`

**Change 1: Add worker_id to ResponseEntry**
```cpp
struct ResponseEntry {
    uWS::HttpResponse<false>* response;
    std::chrono::steady_clock::time_point created_at;
    std::atomic<bool> valid{true};
    std::mutex mutex;
    int worker_id;  // ← ADD THIS
    
    ResponseEntry(uWS::HttpResponse<false>* res, int wid)  // ← ADD worker_id parameter
        : response(res), 
          created_at(std::chrono::steady_clock::now()),
          worker_id(wid) {}  // ← INITIALIZE IT
};
```

**Change 2: Update ResponseRegistry to store worker_id**
```cpp
class ResponseRegistry {
public:
    // Update signature
    std::string register_response(uWS::HttpResponse<false>* res, int worker_id);  // ← ADD worker_id
    
    // Keep existing methods unchanged
    bool send_response(const std::string& request_id, const nlohmann::json& data, 
                      bool is_error = false, int status_code = 200);
    // ...
};
```

**Change 3: Replace single queue with per-worker queues**
```cpp
// REMOVE:
// static std::shared_ptr<queen::ResponseQueue> global_response_queue;

// ADD:
static std::vector<std::shared_ptr<queen::ResponseQueue>> worker_response_queues;
```

---

### Phase 2: Update ResponseRegistry Implementation (30 min)

#### File: `server/src/services/response_queue.cpp`

**Change 1: Update register_response to accept worker_id**
```cpp
std::string ResponseRegistry::register_response(uWS::HttpResponse<false>* res, int worker_id) {
    std::string request_id = generate_uuid();
    auto entry = std::make_shared<ResponseEntry>(res, worker_id);  // ← Pass worker_id
    
    // Set up abort handler to mark response as invalid
    res->onAborted([entry]() {
        std::lock_guard<std::mutex> lock(entry->mutex);
        entry->valid = false;
        entry->response = nullptr;
        spdlog::debug("Response aborted, marked as invalid");
    });
    
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        responses_[request_id] = entry;
    }
    
    spdlog::debug("Registered response with ID: {} (worker {})", request_id, worker_id);
    return request_id;
}
```

**Change 2: Add method to get worker_id for a request**
```cpp
// Add this new method to ResponseRegistry class
std::optional<int> ResponseRegistry::get_worker_id(const std::string& request_id) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = responses_.find(request_id);
    if (it == responses_.end()) {
        return std::nullopt;
    }
    return it->second->worker_id;
}
```

**Also add to header file (response_queue.hpp)**:
```cpp
class ResponseRegistry {
public:
    // ... existing methods ...
    std::optional<int> get_worker_id(const std::string& request_id) const;  // ← ADD THIS
};
```

---

### Phase 3: Update Global Initialization (30 min)

#### File: `server/src/acceptor_server.cpp`

**Change 1: Update global variables (around line 30-40)**
```cpp
// REMOVE:
// static std::shared_ptr<queen::ResponseQueue> global_response_queue;

// ADD:
static std::vector<std::shared_ptr<queen::ResponseQueue>> worker_response_queues;
static int num_workers_global = 0;  // Track number of workers for queue array sizing
```

**Change 2: Initialize per-worker queues in worker_thread (around line 2170-2210)**
```cpp
// Inside worker_thread function, in the std::call_once block:

std::call_once(global_pool_init_flag, [&config, num_workers]() {  // ← ADD num_workers
    // ... existing initialization ...
    
    // REMOVE:
    // global_response_queue = std::make_shared<ResponseQueue>();
    
    // ADD:
    num_workers_global = num_workers;
    worker_response_queues.resize(num_workers);
    for (int i = 0; i < num_workers; i++) {
        worker_response_queues[i] = std::make_shared<queen::ResponseQueue>();
        spdlog::info("Created response queue for worker {}", i);
    }
    
    global_response_registry = std::make_shared<queen::ResponseRegistry>();
    // ... rest of initialization ...
});
```

**Change 3: Update timer setup to use worker-specific queue (around line 2363-2368)**
```cpp
// REMOVE:
// us_timer_t* response_timer = us_create_timer((us_loop_t*)uWS::Loop::get(), 0, sizeof(queen::ResponseQueue*));
// *(queen::ResponseQueue**)us_timer_ext(response_timer) = global_response_queue.get();

// ADD:
us_timer_t* response_timer = us_create_timer((us_loop_t*)uWS::Loop::get(), 0, sizeof(queen::ResponseQueue*));
*(queen::ResponseQueue**)us_timer_ext(response_timer) = worker_response_queues[worker_id].get();
spdlog::info("[Worker {}] Timer configured to process own response queue", worker_id);
```

---

### Phase 4: Update All register_response Calls (30 min)

#### File: `server/src/acceptor_server.cpp`

Find all occurrences of `global_response_registry->register_response(res)` and add `worker_id`:

**Example locations and changes:**

```cpp
// Line ~618 - PUSH endpoint
// BEFORE:
std::string request_id = global_response_registry->register_response(res);

// AFTER:
std::string request_id = global_response_registry->register_response(res, worker_id);
```

**All locations to update (search for `register_response(res)`):**
1. `/api/v1/push` - line ~618
2. `/api/v1/ack` - line ~1065
3. `/api/v1/ack-one` - line ~1306
4. `/api/v1/pop` with wait - probably multiple locations
5. `/api/v1/qpop` - line ~1189
6. `/api/v1/pop-namespace-task` - check around line ~1400
7. `/api/v1/transaction` - line ~1498
8. Any other async endpoints

**Search command to find all:**
```bash
grep -n "register_response(res)" server/src/acceptor_server.cpp
```

---

### Phase 5: Update All Queue Push Calls (30 min)

#### File: `server/src/acceptor_server.cpp`

Find all occurrences of `global_response_queue->push(...)` and route to correct worker:

**Pattern to replace:**
```cpp
// BEFORE:
global_response_queue->push(request_id, response_data, is_error, status_code);

// AFTER:
// Option A: If worker_id is in scope (most cases, it's captured in lambda)
worker_response_queues[worker_id]->push(request_id, response_data, is_error, status_code);

// Option B: If worker_id is NOT in scope (unlikely, but safe fallback)
auto wid = global_response_registry->get_worker_id(request_id);
if (wid.has_value()) {
    worker_response_queues[*wid]->push(request_id, response_data, is_error, status_code);
} else {
    spdlog::error("Cannot route response {}: worker_id not found", request_id);
}
```

**Good news**: In your codebase, `worker_id` is already captured in all the threadpool lambdas! For example:
```cpp
// Line 623 - worker_id is already in the lambda capture
db_thread_pool->push([request_id, queue_manager, file_buffer, worker_id, body]() {
    // ...
    global_response_queue->push(request_id, json_results, false, 201);  // ← Just change this
});
```

So you'll mostly use **Option A** (direct access via `worker_id`).

**All locations to update (search for `global_response_queue->push`):**
```bash
grep -n "global_response_queue->push" server/src/acceptor_server.cpp
```

There are likely 20-30 occurrences, but they're straightforward replacements.

---

### Phase 6: Update PollWorker (if applicable) (15 min)

#### File: `server/src/services/poll_worker.cpp`

Check if poll_worker uses response queues. If it does, update similarly:
```bash
grep -n "global_response_queue" server/src/services/poll_worker.cpp
```

If found, apply same pattern as Phase 5.

---

## Testing Plan

### 1. Compilation Test
```bash
cd server
make clean
make
```

Should compile without errors.

### 2. Single Worker Test (Baseline)
```bash
# Set in config or environment
export NUM_WORKERS=1
./bin/queen-server
```

Run benchmark - should work as before.

### 3. Multi-Worker Test (The Fix)
```bash
export NUM_WORKERS=4
./bin/queen-server
```

Run benchmark in producer mode:
```bash
cd ../benchmark
./bin/benchmark --mode producer --duration 60 --rate 1000
```

**Success criteria**: No segfaults for 60+ seconds under high load.

### 4. Verify Queue Isolation
Add temporary debug logging to confirm each worker only processes its own responses:
```cpp
// In response_timer_callback (line ~2141)
while (processed < 50 && response_queue->pop(item)) {
    spdlog::debug("Timer processing response {} (should be from this worker's queue)", item.request_id);
    // ... existing code
}
```

Check logs - should see each worker only processing its own IDs.

---

## Potential Issues & Solutions

### Issue 1: Forgotten global_response_queue reference
**Symptom**: Compilation error "global_response_queue not defined"  
**Solution**: Search and replace all occurrences

### Issue 2: worker_id not in scope in some lambda
**Symptom**: Compilation error "worker_id not captured"  
**Solution**: Add to lambda capture list: `[..., worker_id]`

### Issue 3: Queue array not sized correctly
**Symptom**: Segfault or out-of-bounds access  
**Solution**: Ensure `worker_response_queues.resize(num_workers)` is called before any worker starts

---

## File Checklist

Files you need to modify:
- [ ] `server/include/queen/response_queue.hpp` (3 changes)
- [ ] `server/src/services/response_queue.cpp` (2 changes)
- [ ] `server/src/acceptor_server.cpp` (~30-40 changes)
- [ ] `server/src/services/poll_worker.cpp` (if used - check first)

---

## Rollback Plan

If something goes wrong:
1. Keep `NUM_WORKERS=1` as immediate fallback
2. Use git to revert changes: `git checkout -- server/`
3. The old code still works fine with single worker

---

## Alternative: Simpler "Designated Worker" Approach

If per-worker queues feel too complex, here's a **5-minute alternative**:

**Only Worker 0 processes responses:**

```cpp
// In worker_thread, around line 2361-2368
// BEFORE: All workers create timers
us_timer_t* response_timer = us_create_timer(...);

// AFTER: Only worker 0 creates timer
if (worker_id == 0) {
    us_timer_t* response_timer = us_create_timer(...);
    *(queen::ResponseQueue**)us_timer_ext(response_timer) = global_response_queue.get();
    us_timer_set(response_timer, response_timer_callback, timer_interval, timer_interval);
    spdlog::info("[Worker 0] Sole response processor - timer configured");
} else {
    spdlog::info("[Worker {}] Not processing responses (Worker 0 handles all)", worker_id);
}
```

**Pros**: 
- 5 lines of code
- Zero risk
- Works immediately

**Cons**: 
- Worker 0 becomes bottleneck for responses (probably fine for most workloads)
- Not as scalable as per-worker queues

---

## Recommendation

**For Production**: Implement per-worker queues (this document)  
**For Quick Fix**: Use designated worker approach above  

The per-worker implementation is not difficult and gives you the best performance and scalability. The code changes are mechanical and low-risk.

Would you like me to generate the actual code changes for you, or do you prefer to implement following this guide?

