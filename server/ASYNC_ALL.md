# Complete Async Migration Plan

## Overview

This document outlines the complete migration of all Queen server routes to use the `AsyncDbPool` and `AsyncQueueManager`, maintaining all existing behavior while eliminating thread pool overhead for quick operations.

## Current State

### Architecture
- ✅ **PUSH**: Uses `AsyncQueueManager` in uWS event loop (DONE)
- ⏳ **POP**: Uses `QueueManager` + `DatabasePool` in thread pool
  - `wait=true`: Poll workers + PollIntentionRegistry
  - `wait=false`: Direct thread pool submission
- ⏳ **ACK**: Uses `QueueManager` + `DatabasePool` in thread pool
- ⏳ **TRANSACTION**: Uses `QueueManager` + `DatabasePool` in thread pool

### Connection Pools
- `AsyncDbPool` (85 connections): Used only for PUSH
- `DatabasePool` (57 connections): Used for POP/ACK/TRANSACTION

## Target State

### Architecture
- ✅ **PUSH**: `AsyncQueueManager` in uWS event loop (DONE)
- 🎯 **ACK**: `AsyncQueueManager` in uWS event loop (NEW)
- 🎯 **TRANSACTION**: `AsyncQueueManager` in uWS event loop (NEW)
- 🎯 **POP `wait=false`**: `AsyncQueueManager` in uWS event loop (NEW)
- 🎯 **POP `wait=true`**: `AsyncQueueManager` in poll workers (CHANGED)

### Connection Pools
- `AsyncDbPool` (142 connections): Used by ALL routes
- `DatabasePool`: **REMOVED**

## Implementation Phases

---

## PHASE 1: Add Async POP Methods

### File: `server/include/queen/async_queue_manager.hpp`

**WHAT**: Add async pop method declarations

**WHERE**: Public methods section (after `push_messages()`)

**HOW**: Add these declarations:

```cpp
// Pop operations - async versions
PopResult pop_messages_from_partition(
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group,
    const PopOptions& options
);

PopResult pop_messages_from_queue(
    const std::string& queue_name,
    const std::string& consumer_group,
    const PopOptions& options
);

PopResult pop_messages_filtered(
    const std::optional<std::string>& namespace_name,
    const std::optional<std::string>& task_name,
    const std::string& consumer_group,
    const PopOptions& options
);
```

**WHERE**: Private helper methods section

**HOW**: Add these declarations:

```cpp
// Lease management - async versions
std::string acquire_partition_lease(
    PGconn* conn,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group,
    int lease_time_seconds,
    const PopOptions& options
);

void release_partition_lease(
    PGconn* conn,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group
);

bool ensure_consumer_group_exists(
    PGconn* conn,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group
);
```

### File: `server/src/managers/async_queue_manager.cpp`

**WHAT**: Implement async pop methods by copying from `queue_manager.cpp` and adapting to async I/O

**WHERE**: Add after `push_messages_chunk()` implementation

**HOW**: 
1. Copy `QueueManager::pop_messages_from_partition()` → adapt to use:
   - `sendQueryParamsAsync()` instead of `conn->exec_params()`
   - `getTuplesResult()` instead of `QueryResult`
   - `sendAndWait("BEGIN")` instead of `conn->begin_transaction()`
   
2. Copy `QueueManager::pop_messages_from_queue()` → same adaptations

3. Copy `QueueManager::pop_messages_filtered()` → same adaptations

4. Copy `QueueManager::acquire_partition_lease()` → same adaptations

5. Copy helper methods with same adaptations

**CRITICAL**: Maintain ALL behavior:
- Lease acquisition logic
- Consumer group creation
- Subscription mode handling (earliest/latest/from)
- Window buffer filtering
- Delayed processing filtering
- Max wait time filtering
- Auto-ack behavior
- Message ordering
- Partition selection logic

---

## PHASE 2: Add Async ACK Methods

### File: `server/include/queen/async_queue_manager.hpp`

**WHAT**: Add async ack method declarations

**WHERE**: Public methods section

**HOW**: Add these declarations:

```cpp
// Acknowledgment operations - async versions
struct AckResult {
    bool success;
    std::string message;
    std::optional<std::string> error;
};

AckResult acknowledge_message(
    const std::string& transaction_id,
    const std::string& status,
    const std::optional<std::string>& error,
    const std::string& consumer_group,
    const std::optional<std::string>& lease_id,
    const std::optional<std::string>& partition_id
);

struct BatchAckResult {
    int successful_acks;
    int failed_acks;
    std::vector<AckResult> results;
};

BatchAckResult acknowledge_messages_batch(
    const std::vector<nlohmann::json>& acknowledgments
);
```

### File: `server/src/managers/async_queue_manager.cpp`

**WHAT**: Implement async ack methods

**WHERE**: Add after pop methods

**HOW**: Copy from `queue_manager.cpp` and adapt:
1. Copy `QueueManager::acknowledge_message()` 
   - Replace `conn->exec_params()` with `sendQueryParamsAsync()`
   - Replace `QueryResult` with `getTuplesResult()`
   - Replace transaction methods with `sendAndWait()`

2. Copy `QueueManager::acknowledge_messages_batch()`
   - Same adaptations
   - Preserve batch processing logic
   - Preserve partition_id validation

**CRITICAL**: Maintain ALL behavior:
- Partition ID validation (prevent wrong message ack)
- Consumer group tracking
- Lease release on ack
- Error status handling
- Batch transaction processing
- Consumer progress tracking (last_consumed_created_at, last_consumed_id)

---

## PHASE 3: Add Async TRANSACTION Method

### File: `server/include/queen/async_queue_manager.hpp`

**WHAT**: Add async transaction method declaration

**WHERE**: Public methods section

**HOW**: Add this declaration:

```cpp
// Transaction operations - async version
struct TransactionResult {
    bool success;
    std::string transaction_id;
    std::vector<nlohmann::json> results;  // Results for each operation
    std::optional<std::string> error;
};

TransactionResult execute_transaction(
    const std::vector<nlohmann::json>& operations
);
```

### File: `server/src/managers/async_queue_manager.cpp`

**WHAT**: Implement async transaction method

**WHERE**: Add after ack methods

**HOW**: Copy from acceptor_server.cpp transaction route logic and adapt:
1. Parse operations array (pop/ack mixed)
2. For each operation:
   - If `type == "pop"`: Call `pop_messages_from_partition()` or `pop_messages_from_queue()`
   - If `type == "ack"`: Call `acknowledge_message()`
3. Execute all in a single transaction context
4. Return results array matching operations order

**CRITICAL**: Maintain ALL behavior:
- Transaction atomicity (all or nothing)
- Operation ordering
- Mixed pop/ack support
- Error propagation
- Transaction ID generation

---

## PHASE 4: Update POP Routes (wait=false only)

### File: `server/src/acceptor_server.cpp`

**WHAT**: Update POP routes to use `async_queue_manager` for immediate pops

**WHERE**: Three POP route handlers:
1. `app->get("/api/v1/pop/queue/:queue/partition/:partition", ...)` (line ~998)
2. `app->get("/api/v1/pop/queue/:queue", ...)` (line ~1247)
3. `app->get("/api/v1/pop", ...)` (line ~1477)

**HOW**: Replace the `wait=false` branch in each route:

**BEFORE**:
```cpp
if (wait) {
    // Register intention - KEEP UNCHANGED
    global_poll_intention_registry->register_intention(intention);
    return;
}

// Non-waiting mode: use ThreadPool
std::string request_id = global_response_registry->register_response(res, worker_id);
db_thread_pool->push([queue_manager, request_id, worker_id, ...] {
    auto result = queue_manager->pop_messages_from_partition(...);
    worker_response_queues[worker_id]->push(request_id, result.to_json(), false, 200);
});
```

**AFTER**:
```cpp
if (wait) {
    // Register intention - UNCHANGED
    global_poll_intention_registry->register_intention(intention);
    return;
}

// Non-waiting mode: use AsyncQueueManager directly in uWS thread
try {
    auto result = async_queue_manager->pop_messages_from_partition(...);
    send_json_response(res, result.to_json(), 200);
} catch (const std::exception& e) {
    send_error_response(res, e.what(), 500);
}
```

**CRITICAL**: 
- Keep `wait=true` branch UNCHANGED (uses poll workers)
- Only change `wait=false` branch
- Remove response registry and thread pool for wait=false
- Add `async_queue_manager` to route lambda captures

---

## PHASE 5: Update ACK Routes

### File: `server/src/acceptor_server.cpp`

**WHAT**: Update ACK routes to use `async_queue_manager` directly

**WHERE**: 
1. `app->post("/api/v1/ack", ...)` (line ~1373)
2. `app->post("/api/v1/ack/batch", ...)` (line ~1126)

**HOW**: Replace thread pool logic with direct async calls

**BEFORE (single ack)**:
```cpp
std::string request_id = global_response_registry->register_response(res, worker_id);

db_thread_pool->push([queue_manager, request_id, worker_id, transaction_id, status, error, consumer_group, lease_id, partition_id]() {
    auto ack_result = queue_manager->acknowledge_message(...);
    nlohmann::json result = { ... };
    worker_response_queues[worker_id]->push(request_id, result, false, 200);
});
```

**AFTER (single ack)**:
```cpp
try {
    auto ack_result = async_queue_manager->acknowledge_message(
        transaction_id, status, error, consumer_group, lease_id, partition_id
    );
    
    nlohmann::json result = {
        {"success", ack_result.success},
        {"message", ack_result.message}
    };
    if (ack_result.error.has_value()) {
        result["error"] = ack_result.error.value();
    }
    
    send_json_response(res, result, 200);
} catch (const std::exception& e) {
    send_error_response(res, e.what(), 500);
}
```

**BEFORE (batch ack)**:
```cpp
std::string request_id = global_response_registry->register_response(res, worker_id);

db_thread_pool->push([queue_manager, request_id, worker_id, acknowledgments]() {
    auto results = queue_manager->acknowledge_messages_batch(acknowledgments);
    nlohmann::json response = { ... };
    worker_response_queues[worker_id]->push(request_id, response, false, 200);
});
```

**AFTER (batch ack)**:
```cpp
try {
    auto batch_result = async_queue_manager->acknowledge_messages_batch(acknowledgments);
    
    nlohmann::json response = {
        {"successful", batch_result.successful_acks},
        {"failed", batch_result.failed_acks},
        {"results", nlohmann::json::array()}
    };
    
    for (const auto& r : batch_result.results) {
        nlohmann::json ack_json = {{"success", r.success}, {"message", r.message}};
        if (r.error.has_value()) ack_json["error"] = r.error.value();
        response["results"].push_back(ack_json);
    }
    
    send_json_response(res, response, 200);
} catch (const std::exception& e) {
    send_error_response(res, e.what(), 500);
}
```

**CRITICAL**:
- Remove response registry registration
- Remove thread pool submission
- Remove lambda captures
- Add `async_queue_manager` to route lambda captures
- Preserve exact same JSON response format
- Preserve exact same error handling

---

## PHASE 6: Update TRANSACTION Route

### File: `server/src/acceptor_server.cpp`

**WHAT**: Update transaction route to use `async_queue_manager` directly

**WHERE**: `app->post("/api/v1/transaction", ...)` (line ~1593)

**HOW**: Replace thread pool logic with direct async call

**BEFORE**:
```cpp
std::string request_id = global_response_registry->register_response(res, worker_id);

db_thread_pool->push([queue_manager, request_id, worker_id, operations]() {
    std::string transaction_id = queue_manager->generate_transaction_id();
    // ... complex operation processing ...
    worker_response_queues[worker_id]->push(request_id, response, has_error, 200);
});
```

**AFTER**:
```cpp
try {
    auto txn_result = async_queue_manager->execute_transaction(operations);
    
    nlohmann::json response = {
        {"transactionId", txn_result.transaction_id},
        {"results", txn_result.results}
    };
    
    int status_code = txn_result.success ? 200 : 400;
    send_json_response(res, response, status_code);
} catch (const std::exception& e) {
    send_error_response(res, e.what(), 500);
}
```

**CRITICAL**:
- Preserve transaction ID generation
- Preserve operation execution order
- Preserve mixed pop/ack support
- Preserve error handling and rollback behavior
- Preserve exact JSON response format

---

## PHASE 7: Update Poll Workers to Use AsyncQueueManager

### File: `server/include/queen/poll_worker.hpp`

**WHAT**: Change function signatures to accept `AsyncQueueManager`

**WHERE**: Line 31-42 and 69-82

**HOW**: Replace all `std::shared_ptr<QueueManager>` with `std::shared_ptr<AsyncQueueManager>`

**BEFORE**:
```cpp
void init_long_polling(
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<QueueManager> queue_manager,  // ← Change this
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    ...
);

void poll_worker_loop(
    int worker_id,
    int total_workers,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<QueueManager> queue_manager,  // ← Change this
    std::shared_ptr<astp::ThreadPool> thread_pool,
    ...
);
```

**AFTER**:
```cpp
void init_long_polling(
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<AsyncQueueManager> async_queue_manager,  // ← Changed
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    ...
);

void poll_worker_loop(
    int worker_id,
    int total_workers,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<AsyncQueueManager> async_queue_manager,  // ← Changed
    std::shared_ptr<astp::ThreadPool> thread_pool,
    ...
);
```

### File: `server/src/services/poll_worker.cpp`

**WHAT**: Update implementation to use async methods

**WHERE**: Throughout the file, every call to `queue_manager->pop_*(...)`

**HOW**: Replace all method calls:
- `queue_manager->pop_messages_from_partition()` → `async_queue_manager->pop_messages_from_partition()`
- `queue_manager->pop_messages_from_queue()` → `async_queue_manager->pop_messages_from_queue()`
- `queue_manager->pop_messages_filtered()` → `async_queue_manager->pop_messages_filtered()`

**CRITICAL**:
- Keep ALL poll worker logic unchanged
- Keep intention registry logic unchanged
- Keep backoff logic unchanged
- Keep rate limiting unchanged
- Keep response distribution unchanged
- Only change the database calls to async versions

### File: `server/src/acceptor_server.cpp`

**WHAT**: Update poll worker initialization to pass `async_queue_manager`

**WHERE**: In `worker_thread()` function where `init_long_polling()` is called

**HOW**: 
1. Find the `init_long_polling()` call (search for "init_long_polling")
2. Change parameter from `queue_manager` to `async_queue_manager`

**BEFORE**:
```cpp
queen::init_long_polling(
    db_thread_pool,
    global_poll_intention_registry,
    queue_manager,  // ← Change this
    worker_response_queues,
    config.queue.poll_worker_interval,
    ...
);
```

**AFTER**:
```cpp
queen::init_long_polling(
    db_thread_pool,
    global_poll_intention_registry,
    async_queue_manager,  // ← Changed
    worker_response_queues,
    config.queue.poll_worker_interval,
    ...
);
```

---

## PHASE 8: Update Route Lambda Captures

### File: `server/src/acceptor_server.cpp`

**WHAT**: Add `async_queue_manager` to lambda captures where needed

**WHERE**: All POP, ACK, and TRANSACTION routes

**HOW**: Update lambda captures:

**Route 1**: `/api/v1/pop/queue/:queue/partition/:partition`
- **Before**: `[queue_manager, config, worker_id, db_thread_pool]`
- **After**: `[async_queue_manager, config, worker_id]`

**Route 2**: `/api/v1/pop/queue/:queue`
- **Before**: `[queue_manager, config, worker_id, db_thread_pool]`
- **After**: `[async_queue_manager, config, worker_id]`

**Route 3**: `/api/v1/pop`
- **Before**: `[queue_manager, config, worker_id, db_thread_pool]`
- **After**: `[async_queue_manager, config, worker_id]`

**Route 4**: `/api/v1/ack`
- **Before**: `[queue_manager, worker_id, db_thread_pool]`
- **After**: `[async_queue_manager, worker_id]`

**Route 5**: `/api/v1/ack/batch`
- **Before**: `[queue_manager, worker_id, db_thread_pool]`
- **After**: `[async_queue_manager, worker_id]`

**Route 6**: `/api/v1/transaction`
- **Before**: `[queue_manager, worker_id, db_thread_pool]`
- **After**: `[async_queue_manager, worker_id]`

**Note**: `db_thread_pool` can be removed from these captures since operations run directly in event loop

---

## PHASE 9: Consolidate Connection Pools

### File: `server/src/acceptor_server.cpp`

**WHAT**: Increase async pool size and remove regular pool

**WHERE**: Worker thread initialization (line ~2536-2574)

**HOW**: 

**BEFORE**:
```cpp
int total_connections = static_cast<int>(config.database.pool_size * 0.95);
int async_connections = static_cast<int>(total_connections * 0.6);  // 60%
int regular_connections = total_connections - async_connections;    // 40%

// Create regular pool
global_db_pool = std::make_shared<DatabasePool>(..., regular_connections, ...);

// Create async pool
global_async_db_pool = std::make_shared<AsyncDbPool>(..., async_connections, ...);
```

**AFTER**:
```cpp
int total_connections = static_cast<int>(config.database.pool_size * 0.95);

// Create ONLY async pool (100% of connections)
global_async_db_pool = std::make_shared<AsyncDbPool>(
    config.database.connection_string(),
    total_connections,  // ← Use ALL connections
    config.database.statement_timeout,
    config.database.lock_timeout,
    config.database.idle_timeout,
    config.database.schema
);

// Remove global_db_pool entirely
```

**WHAT**: Remove `QueueManager` creation

**WHERE**: Same section in worker_thread()

**HOW**: Remove this line:
```cpp
auto queue_manager = std::make_shared<QueueManager>(db_pool, config.queue, config.database.schema);
```

**CRITICAL**:
- Keep thread pool creation (still needed for poll workers)
- Update thread pool size calculation (fewer operations need it)
- Remove all references to `queue_manager` variable
- Keep `async_queue_manager` creation

---

## PHASE 10: Remove Legacy Infrastructure

### File: `server/src/acceptor_server.cpp`

**WHAT**: Remove unused global variables and infrastructure

**WHERE**: Top of file (line ~34-47)

**HOW**: Remove these declarations:
```cpp
static std::shared_ptr<queen::DatabasePool> global_db_pool;  // ← Remove
static std::shared_ptr<queen::ResponseRegistry> global_response_registry;  // ← May remove if unused
```

**WHAT**: Remove legacy routes

**WHERE**: Route definitions

**HOW**: Remove or comment out:
- `/api/v1/push-legacy` route (if exists)
- Any other legacy routes marked for removal

**WHAT**: Update Analytics and other services

**WHERE**: Services that use DatabasePool

**HOW**: Check these services and update if they use DatabasePool:
- `AnalyticsManager` - update to use `AsyncDbPool`
- `RetentionService` - update to use `AsyncDbPool`
- `EvictionService` - update to use `AsyncDbPool`
- `MetricsCollector` - check if uses DB, update if needed

**Note**: These may require creating async versions or keeping a small separate pool

---

## PHASE 11: Update Thread Pool Sizing

### File: `server/src/acceptor_server.cpp`

**WHAT**: Reduce thread pool size (fewer operations need it)

**WHERE**: Thread pool creation (line ~2576-2580)

**HOW**:

**BEFORE**:
```cpp
int total_db_threads = regular_connections;  // 1:1 ratio
int system_threads = 4;

global_db_thread_pool = std::make_shared<astp::ThreadPool>(total_db_threads);
global_system_thread_pool = std::make_shared<astp::ThreadPool>(system_threads);
```

**AFTER**:
```cpp
// Only poll workers need thread pool now (not ack/transaction)
int poll_worker_threads = 4;  // 2-4 poll workers for long polling
int system_threads = 4;  // Background jobs (retention, eviction, metrics)

global_db_thread_pool = std::make_shared<astp::ThreadPool>(poll_worker_threads);
global_system_thread_pool = std::make_shared<astp::ThreadPool>(system_threads);
```

**CRITICAL**: Ensure poll_worker_threads >= number of poll workers initialized

---

## Validation Checklist

After each phase, verify:

### Functional Testing
- [ ] POP with wait=false returns immediately
- [ ] POP with wait=true uses long polling correctly
- [ ] POP returns correct messages in order
- [ ] ACK single message works
- [ ] ACK batch works
- [ ] TRANSACTION with mixed pop/ack works
- [ ] Duplicate transaction ID detection works
- [ ] Encryption works (if enabled)
- [ ] Trace ID propagation works
- [ ] Consumer groups work correctly
- [ ] Lease management works
- [ ] Subscription modes work (earliest/latest/from)
- [ ] Window buffer filtering works
- [ ] Delayed processing works
- [ ] Auto-ack works

### Performance Testing
- [ ] wait=false pops have lower latency (no thread pool)
- [ ] wait=true pops still work with long polling
- [ ] ACK has lower latency
- [ ] TRANSACTION has lower latency
- [ ] No connection pool exhaustion under load
- [ ] No deadlocks with large batches

### Regression Testing
- [ ] Run existing test suite
- [ ] Benchmark producer/consumer still works
- [ ] Maintenance mode failover works
- [ ] File buffer integration works

---

## Environment Variables

No changes to defaults required. All existing configurations work:

```bash
# Connection pool (now 100% async)
DB_POOL_SIZE=150  # All connections go to AsyncDbPool

# Batch push (unchanged)
BATCH_PUSH_TARGET_SIZE_MB=4
BATCH_PUSH_MAX_SIZE_MB=8

# Long polling (unchanged)
POLL_WORKER_INTERVAL=50
POLL_DB_INTERVAL=100

# All other settings unchanged
```

---

## Migration Order

**Recommended order to minimize risk:**

1. ✅ **PHASE 1**: Implement async POP methods in AsyncQueueManager
2. ✅ **PHASE 2**: Implement async ACK methods in AsyncQueueManager
3. ✅ **PHASE 3**: Implement async TRANSACTION method in AsyncQueueManager
4. ✅ **PHASE 4**: Update POP routes (wait=false only) - test immediately
5. ✅ **PHASE 5**: Update ACK routes - test immediately
6. ✅ **PHASE 6**: Update TRANSACTION route - test immediately
7. ✅ **PHASE 7**: Update poll workers to use AsyncQueueManager - test long polling
8. ✅ **PHASE 8**: Update route lambda captures - cleanup
9. ✅ **PHASE 9**: Consolidate connection pools - test under load
10. ✅ **PHASE 10**: Remove legacy infrastructure - final cleanup
11. ✅ **PHASE 11**: Optimize thread pool sizing - performance tuning

---

## Rollback Plan

If issues arise, rollback is easy since we're adding new methods, not removing old ones:

1. Revert route changes to use `queue_manager` + thread pool
2. Restore `global_db_pool` creation
3. Revert poll worker signatures
4. Keep `AsyncQueueManager` methods (no harm)

---

## Code Reuse Strategy

### From QueueManager to AsyncQueueManager

**Mechanical Transformation**:

1. **Database Queries**:
   - `conn->exec_params(sql, params)` → `sendQueryParamsAsync(conn, sql, params); getTuplesResult(conn)`
   - `conn->exec(sql)` → `sendAndWait(conn, sql); getCommandResult(conn)`

2. **Connection Handling**:
   - `ScopedConnection conn(db_pool_.get())` → `auto conn = async_db_pool_->acquire()`
   - Connection automatically released when `conn` goes out of scope

3. **Transactions**:
   - `conn->begin_transaction()` → `sendAndWait(conn.get(), "BEGIN"); getCommandResult(conn.get())`
   - `conn->commit_transaction()` → `sendAndWait(conn.get(), "COMMIT"); getCommandResult(conn.get())`
   - `conn->rollback_transaction()` → `sendAndWait(conn.get(), "ROLLBACK"); getCommandResult(conn.get())`

4. **Result Handling**:
   - `QueryResult result = conn->exec_params(...)` → `auto result = getTuplesResult(conn)`
   - `result.is_success()` → Check for exceptions (getTuplesResult throws on error)
   - `result.num_rows()` → `PQntuples(result.get())`
   - `result.get_value(row, col)` → `PQgetvalue(result.get(), row, col_idx)`

### Copy-Paste Template

For each QueueManager method:

```cpp
// 1. Copy method signature to async_queue_manager.hpp
// 2. Copy method implementation to async_queue_manager.cpp
// 3. Replace db_pool_ with async_db_pool_
// 4. Apply transformation rules above
// 5. Test the method
```

---

## Files Modified Summary

### New/Modified Files:
1. `server/include/queen/async_queue_manager.hpp` - Add method declarations
2. `server/src/managers/async_queue_manager.cpp` - Add method implementations
3. `server/include/queen/poll_worker.hpp` - Update signatures
4. `server/src/services/poll_worker.cpp` - Update to use AsyncQueueManager
5. `server/src/acceptor_server.cpp` - Update routes and initialization

### Files to Review (may need updates):
6. `server/include/queen/analytics_manager.hpp` - Check if uses DatabasePool
7. `server/src/managers/analyticsManager.cpp` - Update if needed
8. `server/include/queen/retention_service.hpp` - Check if uses DatabasePool
9. `server/src/services/retention_service.cpp` - Update if needed
10. `server/include/queen/eviction_service.hpp` - Check if uses DatabasePool
11. `server/src/services/eviction_service.cpp` - Update if needed

### Files to Remove (after migration complete):
- None (keep QueueManager for reference, mark as deprecated)

---

## Testing Strategy

### Unit Tests
1. Test each new AsyncQueueManager method individually
2. Test with wait=true and wait=false
3. Test with different consumer groups
4. Test with encryption enabled/disabled
5. Test duplicate detection
6. Test error scenarios

### Integration Tests
1. Run benchmark with various batch sizes
2. Test long polling with multiple clients
3. Test concurrent pop/ack/push operations
4. Test maintenance mode failover
5. Test under high load (connection pool pressure)

### Load Tests
1. 10 threads × 10K batch push
2. 10 threads × 1K batch push
3. 100 concurrent long polls
4. Mixed workload (push/pop/ack)

---

## Performance Expectations

### Before Migration
- **POP wait=false**: ~50-100ms (thread pool overhead)
- **ACK**: ~30-80ms (thread pool overhead)
- **TRANSACTION**: ~100-300ms (thread pool overhead)

### After Migration
- **POP wait=false**: ~10-50ms (✅ 2-3x faster, no thread pool)
- **ACK**: ~10-50ms (✅ 2-3x faster, no thread pool)
- **TRANSACTION**: ~50-200ms (✅ 1.5-2x faster, no thread pool)
- **POP wait=true**: Same (still uses poll workers)

### Connection Pool
- **Before**: 85 async + 57 regular = 142 total, 40% wasted
- **After**: 142 async = 142 total, 100% utilization ✅

---

## Success Criteria

✅ All routes functional with same behavior  
✅ No performance regression  
✅ Lower latency for quick operations  
✅ No connection pool exhaustion  
✅ No deadlocks with large batches  
✅ Long polling still works correctly  
✅ All existing tests pass  
✅ Benchmark shows improvement  

---

## Implementation Effort Estimate

- **PHASE 1** (Async POP): 4-6 hours (most complex, lots of logic)
- **PHASE 2** (Async ACK): 2-3 hours (simpler)
- **PHASE 3** (Async TRANSACTION): 2-3 hours (moderate complexity)
- **PHASE 4-6** (Update routes): 2-3 hours (straightforward)
- **PHASE 7** (Poll workers): 1-2 hours (minimal changes)
- **PHASE 8-11** (Cleanup): 1-2 hours (simple refactoring)

**Total**: ~12-19 hours of development + testing

---

## Status: Ready for Implementation

All design decisions finalized. Implementation can proceed phase by phase with testing at each stage.

