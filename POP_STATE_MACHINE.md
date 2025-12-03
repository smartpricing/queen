# POP State Machine Architecture

## Executive Summary

Replace the current PostgreSQL-loop-based batch POP with a **libuv-driven state machine** that processes POP requests in parallel across multiple database connections.

**Expected improvement:** 5,000 ops/s → 50,000+ ops/s (10x)

---

## Table of Contents

1. [Current Architecture & Problem](#current-architecture--problem)
2. [New Architecture Overview](#new-architecture-overview)
3. [State Machine Design](#state-machine-design)
4. [Concrete Examples](#concrete-examples)
5. [Implementation Plan](#implementation-plan)
6. [File Changes](#file-changes)
7. [SQL Changes](#sql-changes)
8. [Testing Strategy](#testing-strategy)

---

## Current Architecture & Problem

### Current Flow

```
500 POP requests arrive
        ↓
    Sidecar combines into JSON array
        ↓
    1 SQL call: SELECT queen.pop_messages_batch_v2($1::jsonb)
        ↓
    PostgreSQL executes FOR LOOP (500 iterations)
        ↓
    Each iteration: advisory_lock → UPDATE → SELECT → UPDATE
        ↓
    ~1500ms total (500 × 3ms per iteration)
```

### The Problem

The stored procedure processes requests **sequentially in a loop**:

```sql
FOR v_request IN SELECT * FROM parsed_requests
LOOP
    -- Each partition processed one at a time
    PERFORM pg_advisory_xact_lock(...);
    UPDATE queen.partition_consumers ...;
    SELECT ... FROM queen.messages ...;
END LOOP;
```

**Result:** 50 database connections, but only 1 is used. 98% of capacity wasted.

### Why Batching Doesn't Help POP

| Operation | Batching Type | Why |
|-----------|---------------|-----|
| PUSH | Set-based INSERT | All rows can be inserted in one statement |
| POP | Loop-based | Each partition needs its own cursor, lease, advisory lock |

---

## New Architecture Overview

### Core Insight

**Different partitions have ZERO dependencies.** They can be processed in parallel.

### New Flow

```
500 POP requests arrive
        ↓
    PopBatchStateMachine created
        ↓
    50 connections process in parallel
        ↓
    Connection 1: [Part_0] → [Part_50] → [Part_100] → ...
    Connection 2: [Part_1] → [Part_51] → [Part_101] → ...
    Connection 3: [Part_2] → [Part_52] → [Part_102] → ...
    ...
    Connection 50: [Part_49] → [Part_99] → [Part_149] → ...
        ↓
    All 500 complete in ~30ms (10 rounds × 3ms)
```

### Parallelism Diagram

```
Time →
─────────────────────────────────────────────────────────────────────

CURRENT (Sequential in PostgreSQL):
Conn 1: [P0][P1][P2][P3][P4]...[P499]                    = 1500ms
Conn 2: idle
...
Conn 50: idle

NEW (Parallel State Machine):
Conn 1:  [P0 ][P50 ][P100][P150]...[P450]                = 30ms
Conn 2:  [P1 ][P51 ][P101][P151]...[P451]
Conn 3:  [P2 ][P52 ][P102][P152]...[P452]
...
Conn 50: [P49][P99 ][P149][P199]...[P499]

─────────────────────────────────────────────────────────────────────
```

---

## State Machine Design

### States

```
┌─────────┐     ┌───────────┐     ┌──────────┐     ┌───────────┐
│ PENDING │────▶│ RESOLVING │────▶│ LEASING  │────▶│ FETCHING  │
└─────────┘     └───────────┘     └──────────┘     └───────────┘
     │               │                  │                │
     │               │                  │                ▼
     │               │                  │         ┌───────────┐
     │               │                  └────────▶│ COMPLETED │
     │               │                            └───────────┘
     │               │                                  ▲
     │               ▼                                  │
     │          ┌─────────┐                             │
     └─────────▶│  EMPTY  │◀────────────────────────────┘
                └─────────┘
                     │
                     ▼
                ┌─────────┐
                │ FAILED  │
                └─────────┘
```

### State Descriptions

| State | Description | Next State |
|-------|-------------|------------|
| `PENDING` | Request received, waiting for connection | `RESOLVING` or `LEASING` |
| `RESOLVING` | (Wildcard only) Finding available partition | `LEASING` or `EMPTY` |
| `LEASING` | Acquiring partition lease | `FETCHING` or `EMPTY` |
| `FETCHING` | Reading messages from partition | `COMPLETED` or `EMPTY` |
| `COMPLETED` | Success - has messages | Terminal |
| `EMPTY` | No messages available | Terminal |
| `FAILED` | Error occurred | Terminal |

### State Transitions

```cpp
enum class PopState {
    PENDING,      // Waiting for DB connection
    RESOLVING,    // Wildcard: finding partition
    LEASING,      // Acquiring lease on partition
    FETCHING,     // Reading messages
    COMPLETED,    // Done with messages
    EMPTY,        // Done without messages
    FAILED        // Error
};
```

---

## Concrete Examples

### Example 1: Two Specific Partition POPs

**Setup:**
- Queue: `orders`
- Partition A: `customer-123` (has 5 pending messages)
- Partition B: `customer-456` (has 3 pending messages)
- Consumer group: `processor`
- 2 DB connections available

**Requests:**
```json
[
  {"idx": 0, "queue": "orders", "partition": "customer-123", "consumerGroup": "processor", "batch": 10},
  {"idx": 1, "queue": "orders", "partition": "customer-456", "consumerGroup": "processor", "batch": 10}
]
```

**Timeline:**

```
T=0ms   State Machine receives 2 requests
        Request 0: PENDING → LEASING (assigned to Connection 1)
        Request 1: PENDING → LEASING (assigned to Connection 2)
        
        Connection 1 calls: queen.pop_sm_lease('orders', 'customer-123', 'processor', ...)
        Connection 2 calls: queen.pop_sm_lease('orders', 'customer-456', 'processor', ...)

T=2ms   Both lease queries complete (parallel!)
        Request 0: LEASING → FETCHING
        Request 1: LEASING → FETCHING
        
        Connection 1 calls: queen.pop_sm_fetch(partition_id_123, cursor_ts, cursor_id, 10, ...)
        Connection 2 calls: queen.pop_sm_fetch(partition_id_456, cursor_ts, cursor_id, 10, ...)

T=4ms   Both fetch queries complete (parallel!)
        Request 0: FETCHING → COMPLETED (5 messages)
        Request 1: FETCHING → COMPLETED (3 messages)
        
        State machine aggregates results and delivers response

TOTAL: 4ms (vs 8ms sequential)
```

### Example 2: Mixed Specific + Wildcard POPs

**Setup:**
- Queue: `tasks`
- Partitions: `job-1`, `job-2`, `job-3` (all have messages)
- 3 DB connections available

**Requests:**
```json
[
  {"idx": 0, "queue": "tasks", "partition": "job-1", "consumerGroup": "worker", "batch": 5},
  {"idx": 1, "queue": "tasks", "partition": "",      "consumerGroup": "worker", "batch": 5},
  {"idx": 2, "queue": "tasks", "partition": "",      "consumerGroup": "worker", "batch": 5}
]
```

**Timeline:**

```
T=0ms   State Machine receives 3 requests
        Request 0: PENDING → LEASING (specific partition, skip resolve)
        Request 1: PENDING → RESOLVING (wildcard)
        Request 2: PENDING → RESOLVING (wildcard)
        
        Connection 1 calls: queen.pop_sm_lease('tasks', 'job-1', 'worker', ...)
        Connection 2 calls: queen.pop_sm_resolve('tasks', 'worker') → finds job-2
        Connection 3 calls: queen.pop_sm_resolve('tasks', 'worker') → finds job-3 (SKIP LOCKED)

T=2ms   All resolve/lease queries complete
        Request 0: LEASING → FETCHING
        Request 1: RESOLVING → LEASING (resolved to job-2)
        Request 2: RESOLVING → LEASING (resolved to job-3)
        
        Connection 1 calls: queen.pop_sm_fetch(job-1-id, ...)
        Connection 2 calls: queen.pop_sm_lease('tasks', 'job-2', 'worker', ...)
        Connection 3 calls: queen.pop_sm_lease('tasks', 'job-3', 'worker', ...)

T=4ms   All lease queries complete
        Request 1: LEASING → FETCHING
        Request 2: LEASING → FETCHING
        
        Connection 2 calls: queen.pop_sm_fetch(job-2-id, ...)
        Connection 3 calls: queen.pop_sm_fetch(job-3-id, ...)

T=6ms   All fetch queries complete
        All requests: COMPLETED
        
TOTAL: 6ms (vs 12ms+ sequential)
```

### Example 3: Lease Contention

**Setup:**
- Queue: `exclusive`
- Partition: `singleton` (only 1 partition, already leased by another consumer)
- 1 DB connection

**Request:**
```json
[
  {"idx": 0, "queue": "exclusive", "partition": "singleton", "consumerGroup": "worker", "batch": 5}
]
```

**Timeline:**

```
T=0ms   State Machine receives request
        Request 0: PENDING → LEASING
        
        Connection 1 calls: queen.pop_sm_lease('exclusive', 'singleton', 'worker', ...)
        
        Procedure checks: lease_expires_at > NOW() → lease already held!

T=2ms   Procedure returns empty result (lease already held)
        Request 0: LEASING → EMPTY
        
        Response: {"messages": [], "leaseId": null}
        
TOTAL: 2ms (fast fail, no wasted time)
```

### Example 4: High-Volume Benchmark (500 POPs)

**Setup:**
- Queue: `benchmark`
- 500 partitions: `part-0` through `part-499`
- 500 workers, each targets one partition
- 50 DB connections
- Each partition has 100 messages

**Requests:** 500 specific partition POPs

**Timeline:**

```
T=0ms    Round 1: 50 requests start (one per connection)
         Connections 1-50 call: queen.pop_sm_lease() for parts 0-49

T=3ms    Round 1 leases complete, start fetching
         Connections 1-50 call: queen.pop_sm_fetch() for parts 0-49

T=6ms    Round 1 complete (50 requests done)
         Round 2: 50 more requests start
         Connections 1-50 call: queen.pop_sm_lease() for parts 50-99

T=9ms    Round 2 leases complete, start fetching
         Connections 1-50 call: queen.pop_sm_fetch() for parts 50-99

... (continues for 10 rounds) ...

T=60ms   Round 10 complete
         All 500 requests done

TOTAL: ~60ms
THROUGHPUT: 500 / 0.060s = 8,333 ops/batch
With continuous batches: ~50,000+ ops/s
```

**Comparison:**
- Current (PostgreSQL loop): 500 × 3ms = 1,500ms
- New (State Machine): 10 rounds × 6ms = 60ms
- **Speedup: 25x**

---

## Implementation Plan

### Phase 1: Stored Procedures & Core State Machine (Week 1)

1. Create `server/schema/procedures/008_pop_state_machine.sql`
   - `pop_sm_resolve` - wildcard partition resolution
   - `pop_sm_lease` - lease acquisition
   - `pop_sm_fetch` - message fetching
   - `pop_sm_release` - lease release
2. Create `PopStateMachine` class in C++
3. Implement state transitions
4. Unit tests with mock DB

### Phase 2: Integration (Week 2)

1. Integrate with `SidecarDbPool`
2. Handle connection slot assignment
3. Add libuv event handling
4. Response aggregation

### Phase 3: Wildcard Support (Week 3)

1. Add `RESOLVING` state
2. Wire up `pop_sm_resolve` procedure
3. Handle SKIP LOCKED for concurrent wildcards
4. Integration tests

### Phase 4: Optimization & Testing (Week 4)

1. Benchmark comparison
2. Edge case handling
3. Error recovery
4. Production hardening

---

## File Changes

### New Files

#### `server/schema/procedures/008_pop_state_machine.sql`

Contains all four stored procedures for the state machine:
- `queen.pop_sm_resolve()` - Wildcard partition resolution
- `queen.pop_sm_lease()` - Lease acquisition
- `queen.pop_sm_fetch()` - Message fetching
- `queen.pop_sm_release()` - Lease release

See [SQL Changes](#sql-changes) section for full implementation.

#### `server/include/queen/pop_state_machine.hpp`

```cpp
#pragma once

#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <json.hpp>

namespace queen {

// Forward declarations
struct ConnectionSlot;

enum class PopState {
    PENDING,
    RESOLVING,
    LEASING,
    FETCHING,
    COMPLETED,
    EMPTY,
    FAILED
};

struct PopRequestState {
    // Request identification
    std::string request_id;
    int idx;
    
    // Request parameters
    std::string queue_name;
    std::string partition_name;      // Empty for wildcard
    std::string consumer_group;
    int batch_size;
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
    
    // Results
    nlohmann::json messages;
    std::string error_message;
    
    // Timing
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point completed_at;
};

class PopBatchStateMachine {
public:
    using CompletionCallback = std::function<void(std::vector<PopRequestState>&)>;
    
    PopBatchStateMachine(
        std::vector<PopRequestState> requests,
        CompletionCallback on_complete
    );
    
    // Called by sidecar when a connection becomes available
    void assign_connection(ConnectionSlot* slot);
    
    // Called when a query completes
    void on_query_complete(ConnectionSlot* slot, PGresult* result);
    
    // Called on query error
    void on_query_error(ConnectionSlot* slot, const std::string& error);
    
    // Check if all requests are complete
    bool is_complete() const;
    
    // Get count of pending requests (need connections)
    int pending_count() const;
    
private:
    std::vector<PopRequestState> requests_;
    CompletionCallback on_complete_;
    
    // State transition logic
    void advance_state(PopRequestState& req);
    void submit_resolve_query(PopRequestState& req);
    void submit_lease_query(PopRequestState& req);
    void submit_fetch_query(PopRequestState& req);
    void submit_release_query(PopRequestState& req);
    
    // Result parsing
    void handle_resolve_result(PopRequestState& req, PGresult* result);
    void handle_lease_result(PopRequestState& req, PGresult* result);
    void handle_fetch_result(PopRequestState& req, PGresult* result);
    
    // Completion
    void check_completion();
    void finalize_response(PopRequestState& req);
};

} // namespace queen
```

#### `server/src/pop_state_machine.cpp`

```cpp
#include "queen/pop_state_machine.hpp"
#include "queen/sidecar_db_pool.hpp"
#include <spdlog/spdlog.h>

namespace queen {

PopBatchStateMachine::PopBatchStateMachine(
    std::vector<PopRequestState> requests,
    CompletionCallback on_complete)
    : requests_(std::move(requests))
    , on_complete_(std::move(on_complete)) {
    
    // Generate lease IDs for all requests upfront
    for (auto& req : requests_) {
        req.lease_id = generate_uuid();  // Or use gen_random_uuid() in SQL
        req.started_at = std::chrono::steady_clock::now();
    }
}

void PopBatchStateMachine::assign_connection(ConnectionSlot* slot) {
    // Find first PENDING request
    for (auto& req : requests_) {
        if (req.state == PopState::PENDING && req.assigned_slot == nullptr) {
            req.assigned_slot = slot;
            slot->state_machine_request = &req;
            advance_state(req);
            return;
        }
    }
}

void PopBatchStateMachine::advance_state(PopRequestState& req) {
    switch (req.state) {
        case PopState::PENDING:
            if (req.partition_name.empty()) {
                // Wildcard: need to resolve partition first
                submit_resolve_query(req);
                req.state = PopState::RESOLVING;
            } else {
                // Specific partition: go directly to leasing
                submit_lease_query(req);
                req.state = PopState::LEASING;
            }
            break;
            
        case PopState::RESOLVING:
            // After resolve, try to lease
            if (!req.partition_id.empty()) {
                submit_lease_query(req);
                req.state = PopState::LEASING;
            } else {
                // No partition available
                req.state = PopState::EMPTY;
                release_connection(req);
            }
            break;
            
        case PopState::LEASING:
            // After lease acquired, fetch messages
            submit_fetch_query(req);
            req.state = PopState::FETCHING;
            break;
            
        case PopState::FETCHING:
            // After fetch, we're done
            if (req.messages.empty()) {
                // Release lease if no messages
                submit_release_query(req);
            }
            req.state = req.messages.empty() ? PopState::EMPTY : PopState::COMPLETED;
            release_connection(req);
            break;
            
        default:
            break;
    }
}

void PopBatchStateMachine::submit_resolve_query(PopRequestState& req) {
    // Call stored procedure for wildcard resolution
    std::string sql = "SELECT * FROM queen.pop_sm_resolve($1, $2)";
    
    const char* params[] = {
        req.queue_name.c_str(),
        req.consumer_group.c_str()
    };
    
    PQsendQueryParams(req.assigned_slot->conn, sql.c_str(), 2, 
                      nullptr, params, nullptr, nullptr, 0);
}

void PopBatchStateMachine::submit_lease_query(PopRequestState& req) {
    // Call stored procedure for lease acquisition
    std::string sql = "SELECT * FROM queen.pop_sm_lease($1, $2, $3, $4, $5, $6)";
    
    const char* params[] = {
        req.queue_name.c_str(),
        req.partition_name.c_str(),
        req.consumer_group.c_str(),
        std::to_string(req.lease_seconds).c_str(),
        req.lease_id.c_str(),
        std::to_string(req.batch_size).c_str()
    };
    
    PQsendQueryParams(req.assigned_slot->conn, sql.c_str(), 6, 
                      nullptr, params, nullptr, nullptr, 0);
}

void PopBatchStateMachine::submit_fetch_query(PopRequestState& req) {
    // Call stored procedure for message fetching
    std::string sql = "SELECT queen.pop_sm_fetch($1, $2, $3, $4, $5, $6)";
    
    const char* params[] = {
        req.partition_id.c_str(),
        req.cursor_ts_str.c_str(),  // NULL or timestamp string
        req.cursor_id.c_str(),       // NULL or UUID string
        std::to_string(req.batch_size).c_str(),
        std::to_string(req.window_buffer).c_str(),
        std::to_string(req.delayed_processing).c_str()
    };
    
    PQsendQueryParams(req.assigned_slot->conn, sql.c_str(), 6, 
                      nullptr, params, nullptr, nullptr, 0);
}

void PopBatchStateMachine::submit_release_query(PopRequestState& req) {
    // Call stored procedure for lease release
    std::string sql = "SELECT queen.pop_sm_release($1, $2, $3)";
    
    const char* params[] = {
        req.partition_id.c_str(),
        req.consumer_group.c_str(),
        req.lease_id.c_str()
    };
    
    PQsendQueryParams(req.assigned_slot->conn, sql.c_str(), 3, 
                      nullptr, params, nullptr, nullptr, 0);
}

void PopBatchStateMachine::on_query_complete(ConnectionSlot* slot, PGresult* result) {
    auto* req = static_cast<PopRequestState*>(slot->state_machine_request);
    if (!req) return;
    
    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        req->error_message = PQresultErrorMessage(result);
        req->state = PopState::FAILED;
        release_connection(*req);
        check_completion();
        return;
    }
    
    switch (req->state) {
        case PopState::RESOLVING:
            handle_resolve_result(*req, result);
            break;
        case PopState::LEASING:
            handle_lease_result(*req, result);
            break;
        case PopState::FETCHING:
            handle_fetch_result(*req, result);
            break;
        default:
            break;
    }
    
    advance_state(*req);
    check_completion();
}

void PopBatchStateMachine::handle_resolve_result(PopRequestState& req, PGresult* result) {
    if (PQntuples(result) == 0) {
        // No available partition found
        req.state = PopState::EMPTY;
        return;
    }
    
    // Extract resolved partition info from pop_sm_resolve
    // Returns: partition_id, partition_name, queue_id, lease_time, window_buffer, delayed_processing
    req.partition_id = PQgetvalue(result, 0, 0);
    req.partition_name = PQgetvalue(result, 0, 1);
    req.queue_id = PQgetvalue(result, 0, 2);
    req.queue_lease_time = PQgetisnull(result, 0, 3) ? 0 : atoi(PQgetvalue(result, 0, 3));
    req.window_buffer = PQgetisnull(result, 0, 4) ? 0 : atoi(PQgetvalue(result, 0, 4));
    req.delayed_processing = PQgetisnull(result, 0, 5) ? 0 : atoi(PQgetvalue(result, 0, 5));
}

void PopBatchStateMachine::handle_lease_result(PopRequestState& req, PGresult* result) {
    if (PQntuples(result) == 0) {
        // Lease not acquired (already taken by another consumer)
        req.state = PopState::EMPTY;
        return;
    }
    
    // Extract cursor position and queue config from pop_sm_lease
    // Returns: partition_id, cursor_id, cursor_ts, queue_lease_time, window_buffer, delayed_processing
    req.partition_id = PQgetvalue(result, 0, 0);
    req.cursor_id = PQgetisnull(result, 0, 1) ? "" : PQgetvalue(result, 0, 1);
    req.cursor_ts_str = PQgetisnull(result, 0, 2) ? "" : PQgetvalue(result, 0, 2);
    req.queue_lease_time = PQgetisnull(result, 0, 3) ? 0 : atoi(PQgetvalue(result, 0, 3));
    req.window_buffer = PQgetisnull(result, 0, 4) ? 0 : atoi(PQgetvalue(result, 0, 4));
    req.delayed_processing = PQgetisnull(result, 0, 5) ? 0 : atoi(PQgetvalue(result, 0, 5));
}

void PopBatchStateMachine::handle_fetch_result(PopRequestState& req, PGresult* result) {
    // pop_sm_fetch returns a JSONB array of messages
    if (PQntuples(result) == 0 || PQgetisnull(result, 0, 0)) {
        req.messages = nlohmann::json::array();
        return;
    }
    
    const char* json_str = PQgetvalue(result, 0, 0);
    try {
        req.messages = nlohmann::json::parse(json_str);
        
        // Add additional fields to each message
        for (auto& msg : req.messages) {
            msg["queue"] = req.queue_name;
            msg["partition"] = req.partition_name;
            msg["partitionId"] = req.partition_id;
            msg["leaseId"] = req.lease_id;
            msg["consumerGroup"] = req.consumer_group;
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse pop_sm_fetch result: {}", e.what());
        req.messages = nlohmann::json::array();
    }
}

bool PopBatchStateMachine::is_complete() const {
    for (const auto& req : requests_) {
        if (req.state != PopState::COMPLETED && 
            req.state != PopState::EMPTY && 
            req.state != PopState::FAILED) {
            return false;
        }
    }
    return true;
}

void PopBatchStateMachine::check_completion() {
    if (is_complete() && on_complete_) {
        on_complete_(requests_);
    }
}

} // namespace queen
```

### Modified Files

#### `server/include/queen/sidecar_db_pool.hpp`

Add:
```cpp
// Forward declaration
class PopBatchStateMachine;

struct ConnectionSlot {
    // ... existing fields ...
    
    // State machine support
    PopBatchStateMachine* state_machine = nullptr;
    void* state_machine_request = nullptr;  // Pointer to PopRequestState
};

class SidecarDbPool {
    // ... existing ...
    
    // State machine support
    std::vector<std::shared_ptr<PopBatchStateMachine>> active_state_machines_;
    
    void submit_pop_batch_sm(std::vector<SidecarRequest> requests);
    void process_state_machines();
};
```

#### `server/src/database/sidecar_db_pool.cpp`

Add new method:
```cpp
void SidecarDbPool::submit_pop_batch_sm(std::vector<SidecarRequest> requests) {
    // Convert SidecarRequests to PopRequestStates
    std::vector<PopRequestState> pop_requests;
    for (const auto& req : requests) {
        PopRequestState pr;
        pr.request_id = req.request_id;
        pr.queue_name = req.queue_name;
        pr.partition_name = req.partition_name;
        pr.consumer_group = req.consumer_group;
        pr.batch_size = req.batch_size;
        pr.subscription_mode = req.subscription_mode;
        pr.subscription_from = req.subscription_from;
        pop_requests.push_back(pr);
    }
    
    // Create state machine
    auto sm = std::make_shared<PopBatchStateMachine>(
        std::move(pop_requests),
        [this](std::vector<PopRequestState>& results) {
            // Deliver responses
            for (auto& req : results) {
                SidecarResponse resp;
                resp.request_id = req.request_id;
                resp.op_type = SidecarOpType::POP_BATCH;
                resp.success = (req.state != PopState::FAILED);
                
                nlohmann::json result;
                result["messages"] = req.messages;
                result["leaseId"] = req.messages.empty() ? nullptr : req.lease_id;
                resp.result_json = result.dump();
                
                deliver_response(std::move(resp));
            }
        }
    );
    
    active_state_machines_.push_back(sm);
    
    // Assign available connections
    for (auto& slot : slots_) {
        if (!slot.busy && slot.conn && sm->pending_count() > 0) {
            slot.busy = true;
            slot.state_machine = sm.get();
            sm->assign_connection(&slot);
        }
    }
}

// Modify on_socket_event to handle state machine queries
void SidecarDbPool::on_socket_event(uv_poll_t* handle, int status, int events) {
    auto* slot = static_cast<ConnectionSlot*>(handle->data);
    
    // ... existing code ...
    
    if (events & UV_READABLE) {
        if (!PQconsumeInput(slot->conn)) {
            // ... error handling ...
            return;
        }
        
        if (PQisBusy(slot->conn) == 0) {
            if (slot->state_machine) {
                // State machine query completed
                PGresult* result = PQgetResult(slot->conn);
                slot->state_machine->on_query_complete(slot, result);
                PQclear(result);
                
                // Drain any remaining results
                while ((result = PQgetResult(slot->conn)) != nullptr) {
                    PQclear(result);
                }
                
                // Check if state machine needs more work
                if (slot->state_machine->pending_count() > 0) {
                    slot->state_machine->assign_connection(slot);
                } else {
                    slot->busy = false;
                    slot->state_machine = nullptr;
                    slot->state_machine_request = nullptr;
                }
            } else {
                // Original batched query handling
                pool->process_slot_result(*slot);
            }
        }
    }
}
```

#### `server/src/routes/pop.cpp`

Change the sidecar call for POP_BATCH:
```cpp
// Option: Use state machine for specific-partition POPs
if (!partition_name.empty()) {
    // Specific partition: use state machine (parallel)
    ctx.sidecar->submit_pop_batch_sm({std::move(sidecar_req)});
} else {
    // Wildcard: use existing batched approach or state machine
    ctx.sidecar->submit(std::move(sidecar_req));
}
```

---

## SQL Changes

### New File: `server/schema/procedures/008_pop_state_machine.sql`

Create this file with all four stored procedures:

```sql
-- ============================================================================
-- POP State Machine Procedures
-- ============================================================================
-- These procedures support parallel POP execution via the C++ state machine.
-- Each procedure is simple, focused, and optimized for single-partition operations.
-- 
-- Procedures:
--   1. pop_sm_resolve  - Resolve wildcard to specific partition
--   2. pop_sm_lease    - Acquire lease on specific partition  
--   3. pop_sm_fetch    - Fetch messages from partition
--   4. pop_sm_release  - Release lease when no messages found
-- ============================================================================

-- ============================================================================
-- 1. pop_sm_resolve: Resolve wildcard to specific partition
-- ============================================================================
-- Used when partition_name is empty (wildcard POP).
-- Finds the best available partition using SKIP LOCKED to avoid conflicts.
--
-- Parameters:
--   p_queue_name     - Queue name
--   p_consumer_group - Consumer group name
--
-- Returns: Single row with partition info, or empty if no partition available
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_sm_resolve(
    p_queue_name TEXT,
    p_consumer_group TEXT
) RETURNS TABLE (
    partition_id UUID,
    partition_name TEXT,
    queue_id UUID,
    lease_time INT,
    window_buffer INT,
    delayed_processing INT
) LANGUAGE plpgsql AS $$
BEGIN
    RETURN QUERY
    SELECT 
        pl.partition_id,
        p.name,
        q.id,
        q.lease_time,
        q.window_buffer,
        q.delayed_processing
    FROM queen.partition_lookup pl
    JOIN queen.partitions p ON p.id = pl.partition_id
    JOIN queen.queues q ON q.id = p.queue_id
    LEFT JOIN queen.partition_consumers pc 
        ON pc.partition_id = pl.partition_id 
        AND pc.consumer_group = p_consumer_group
    WHERE pl.queue_name = p_queue_name
      -- Partition must be free (no lease or expired)
      AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
      -- Partition must have unconsumed messages
      AND (pc.last_consumed_created_at IS NULL 
           OR pl.last_message_created_at > pc.last_consumed_created_at
           OR (pl.last_message_created_at = pc.last_consumed_created_at 
               AND pl.last_message_id > pc.last_consumed_id))
    -- Prefer partitions not recently consumed (fair distribution)
    ORDER BY pc.last_consumed_at ASC NULLS FIRST
    -- SKIP LOCKED prevents concurrent wildcards from grabbing same partition
    FOR UPDATE OF pc SKIP LOCKED
    LIMIT 1;
END;
$$;

-- ============================================================================
-- 2. pop_sm_lease: Acquire lease on specific partition
-- ============================================================================
-- Attempts to acquire a lease on a specific partition.
-- Returns cursor position and queue config if successful.
--
-- Parameters:
--   p_queue_name     - Queue name
--   p_partition_name - Partition name (must be specific, not empty)
--   p_consumer_group - Consumer group name
--   p_lease_seconds  - Lease duration (0 = use queue default)
--   p_worker_id      - Unique worker/lease ID
--   p_batch_size     - Number of messages to fetch
--
-- Returns: Single row if lease acquired, empty if lease already held
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_sm_lease(
    p_queue_name TEXT,
    p_partition_name TEXT,
    p_consumer_group TEXT,
    p_lease_seconds INT,
    p_worker_id TEXT,
    p_batch_size INT
) RETURNS TABLE (
    partition_id UUID,
    cursor_id UUID,
    cursor_ts TIMESTAMPTZ,
    queue_lease_time INT,
    window_buffer INT,
    delayed_processing INT
) LANGUAGE plpgsql AS $$
DECLARE
    v_partition_id UUID;
    v_queue_id UUID;
    v_lease_time INT;
    v_window_buffer INT;
    v_delayed_processing INT;
    v_effective_lease INT;
BEGIN
    -- Resolve partition and queue info
    SELECT p.id, q.id, q.lease_time, q.window_buffer, q.delayed_processing
    INTO v_partition_id, v_queue_id, v_lease_time, v_window_buffer, v_delayed_processing
    FROM queen.queues q
    JOIN queen.partitions p ON p.queue_id = q.id
    WHERE q.name = p_queue_name AND p.name = p_partition_name;
    
    IF v_partition_id IS NULL THEN
        -- Partition not found
        RETURN;
    END IF;
    
    -- Calculate effective lease time
    v_effective_lease := COALESCE(NULLIF(p_lease_seconds, 0), NULLIF(v_lease_time, 0), 60);
    
    -- Ensure partition_consumers row exists
    INSERT INTO queen.partition_consumers (partition_id, consumer_group)
    VALUES (v_partition_id, p_consumer_group)
    ON CONFLICT (partition_id, consumer_group) DO NOTHING;
    
    -- Try to acquire lease (atomic check-and-update)
    RETURN QUERY
    UPDATE queen.partition_consumers pc
    SET 
        lease_acquired_at = NOW(),
        lease_expires_at = NOW() + (v_effective_lease * INTERVAL '1 second'),
        worker_id = p_worker_id,
        batch_size = p_batch_size,
        acked_count = 0,
        batch_retry_count = COALESCE(pc.batch_retry_count, 0)
    WHERE pc.partition_id = v_partition_id
      AND pc.consumer_group = p_consumer_group
      AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
    RETURNING 
        pc.partition_id,
        pc.last_consumed_id,
        pc.last_consumed_created_at,
        v_lease_time,
        v_window_buffer,
        v_delayed_processing;
END;
$$;

-- ============================================================================
-- 3. pop_sm_fetch: Fetch messages from partition
-- ============================================================================
-- Fetches messages from a partition starting after the cursor position.
-- Respects window_buffer and delayed_processing settings.
--
-- Parameters:
--   p_partition_id       - Partition UUID
--   p_cursor_ts          - Cursor timestamp (NULL = from beginning)
--   p_cursor_id          - Cursor message ID (NULL = from beginning)
--   p_batch_size         - Max messages to return
--   p_window_buffer      - Window buffer seconds (0 = disabled)
--   p_delayed_processing - Delayed processing seconds (0 = disabled)
--
-- Returns: JSONB array of messages
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_sm_fetch(
    p_partition_id UUID,
    p_cursor_ts TIMESTAMPTZ,
    p_cursor_id UUID,
    p_batch_size INT,
    p_window_buffer INT DEFAULT 0,
    p_delayed_processing INT DEFAULT 0
) RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    v_messages JSONB;
    v_now TIMESTAMPTZ := NOW();
    v_effective_cursor_id UUID;
BEGIN
    -- Handle NULL cursor_id (use zero UUID for comparison)
    v_effective_cursor_id := COALESCE(p_cursor_id, '00000000-0000-0000-0000-000000000000'::uuid);
    
    SELECT COALESCE(jsonb_agg(
        jsonb_build_object(
            'id', m.id::text,
            'transactionId', m.transaction_id,
            'traceId', m.trace_id::text,
            'data', m.payload,
            'createdAt', to_char(m.created_at, 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
        ) ORDER BY m.created_at, m.id
    ), '[]'::jsonb)
    INTO v_messages
    FROM queen.messages m
    WHERE m.partition_id = p_partition_id
      -- After cursor position
      AND (p_cursor_ts IS NULL OR (m.created_at, m.id) > (p_cursor_ts, v_effective_cursor_id))
      -- Respect window_buffer
      AND (p_window_buffer IS NULL OR p_window_buffer = 0 
           OR m.created_at <= v_now - (p_window_buffer || ' seconds')::interval)
      -- Respect delayed_processing
      AND (p_delayed_processing IS NULL OR p_delayed_processing = 0 
           OR m.created_at <= v_now - (p_delayed_processing || ' seconds')::interval)
    LIMIT p_batch_size;
    
    RETURN v_messages;
END;
$$;

-- ============================================================================
-- 4. pop_sm_release: Release lease when no messages found
-- ============================================================================
-- Releases a lease that was acquired but found no messages.
-- Only releases if the worker_id matches (prevents releasing someone else's lease).
--
-- Parameters:
--   p_partition_id   - Partition UUID
--   p_consumer_group - Consumer group name
--   p_worker_id      - Worker ID that acquired the lease
--
-- Returns: TRUE if released, FALSE if not found/not owned
-- ============================================================================
CREATE OR REPLACE FUNCTION queen.pop_sm_release(
    p_partition_id UUID,
    p_consumer_group TEXT,
    p_worker_id TEXT
) RETURNS BOOLEAN LANGUAGE plpgsql AS $$
BEGIN
    UPDATE queen.partition_consumers
    SET 
        lease_expires_at = NULL, 
        worker_id = NULL,
        lease_acquired_at = NULL
    WHERE partition_id = p_partition_id
      AND consumer_group = p_consumer_group
      AND worker_id = p_worker_id;
    
    RETURN FOUND;
END;
$$;

-- ============================================================================
-- Grant Permissions
-- ============================================================================
GRANT EXECUTE ON FUNCTION queen.pop_sm_resolve(TEXT, TEXT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.pop_sm_lease(TEXT, TEXT, TEXT, INT, TEXT, INT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.pop_sm_fetch(UUID, TIMESTAMPTZ, UUID, INT, INT, INT) TO PUBLIC;
GRANT EXECUTE ON FUNCTION queen.pop_sm_release(UUID, TEXT, TEXT) TO PUBLIC;
```

### Procedure Summary

| Procedure | Purpose | Called In State |
|-----------|---------|-----------------|
| `pop_sm_resolve` | Find available partition for wildcard | `RESOLVING` |
| `pop_sm_lease` | Acquire lease on specific partition | `LEASING` |
| `pop_sm_fetch` | Read messages from partition | `FETCHING` |
| `pop_sm_release` | Release empty lease | After `FETCHING` (if empty) |

### C++ Procedure Calls

```cpp
// RESOLVING state
"SELECT * FROM queen.pop_sm_resolve($1, $2)"
// Params: queue_name, consumer_group

// LEASING state  
"SELECT * FROM queen.pop_sm_lease($1, $2, $3, $4, $5, $6)"
// Params: queue_name, partition_name, consumer_group, lease_seconds, worker_id, batch_size

// FETCHING state
"SELECT queen.pop_sm_fetch($1, $2, $3, $4, $5, $6)"
// Params: partition_id, cursor_ts, cursor_id, batch_size, window_buffer, delayed_processing

// Release (if empty)
"SELECT queen.pop_sm_release($1, $2, $3)"
// Params: partition_id, consumer_group, worker_id
```

---

## Testing Strategy

### Unit Tests

1. **State transitions**
   - PENDING → LEASING → FETCHING → COMPLETED
   - PENDING → RESOLVING → LEASING → FETCHING → COMPLETED
   - PENDING → LEASING → EMPTY (lease taken)
   - Any state → FAILED (on error)

2. **Connection management**
   - Connection assigned when available
   - Connection released on completion
   - Connection reused for next pending request

### Integration Tests

1. **Single partition POP**
   - Verify messages returned correctly
   - Verify lease acquired

2. **Multiple parallel POPs**
   - 50 POPs on 50 different partitions
   - Verify all complete in parallel

3. **Wildcard POPs**
   - Verify partition resolution
   - Verify SKIP LOCKED prevents conflicts

4. **Lease contention**
   - POP on already-leased partition
   - Verify returns empty without waiting

### Benchmark Tests

```javascript
// test-state-machine-benchmark.js
const PARTITIONS = 500;
const CONNECTIONS = 50;

// Pre-populate: 100 messages per partition
// Run: 500 POPs (1 per partition)
// Expected: ~60ms total (vs 1500ms current)
// Throughput: 8000+ ops/batch
```

---

## Migration Path

### Phase 1: Parallel Deployment

1. Add state machine code alongside existing batch code
2. Feature flag: `USE_POP_STATE_MACHINE=true`
3. A/B test in staging

### Phase 2: Gradual Rollout

1. Enable for specific-partition POPs only
2. Monitor performance and errors
3. Expand to wildcard POPs

### Phase 3: Full Migration

1. Make state machine the default
2. Remove old batch code
3. Update documentation

---

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| More complex C++ code | Bugs, maintenance | Comprehensive tests, clear state machine |
| More DB connections busy | Resource usage | Already have 50 connections, now actually used |
| Advisory lock handling | Deadlocks | Consistent ordering (partition_id ASC) |
| Partial batch failure | Inconsistent state | Per-request error handling, no global rollback |

---

## Success Metrics

| Metric | Current | Target |
|--------|---------|--------|
| POP throughput | 5,000 ops/s | 50,000 ops/s |
| POP latency p50 | 12ms | 3ms |
| POP latency p99 | 100ms | 15ms |
| Connection utilization | 2% | 80%+ |

---

## Appendix: Full State Diagram

```
                                    ┌──────────────────────┐
                                    │      PENDING         │
                                    │  (waiting for conn)  │
                                    └──────────┬───────────┘
                                               │
                        ┌──────────────────────┴──────────────────────┐
                        │                                             │
                        ▼                                             ▼
            ┌───────────────────┐                         ┌───────────────────┐
            │    RESOLVING      │                         │     LEASING       │
            │ (wildcard only)   │                         │ (acquire lease)   │
            └────────┬──────────┘                         └─────────┬─────────┘
                     │                                              │
         ┌───────────┴───────────┐                      ┌───────────┴───────────┐
         │                       │                      │                       │
         ▼                       ▼                      ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│     EMPTY       │    │    LEASING      │    │    FETCHING     │    │     EMPTY       │
│ (no partition)  │    │ (has partition) │    │ (get messages)  │    │ (lease taken)   │
└─────────────────┘    └────────┬────────┘    └────────┬────────┘    └─────────────────┘
                                │                      │
                                ▼                      │
                       ┌─────────────────┐             │
                       │    FETCHING     │             │
                       └────────┬────────┘             │
                                │                      │
                    ┌───────────┴───────────┐          │
                    │                       │          │
                    ▼                       ▼          ▼
           ┌─────────────────┐    ┌─────────────────┐
           │   COMPLETED     │    │     EMPTY       │
           │ (has messages)  │    │ (no messages)   │
           └─────────────────┘    └─────────────────┘
```

**Error handling:** Any state can transition to `FAILED` on database error.

