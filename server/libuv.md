# Sidecar libuv Migration Plan

## Overview

This document outlines the engineering plan to migrate the sidecar database pool from `select()` to libuv for improved scalability and cleaner async architecture.

**Current State:** `select()` based polling in `sidecar_db_pool.cpp`  
**Target State:** libuv event loop with `uv_poll_t`, `uv_timer_t`, `uv_async_t`

---

## Benefits

| Aspect | Before (`select`) | After (libuv) |
|--------|-------------------|---------------|
| Scalability | 1024 FD limit | Unlimited |
| Complexity | O(n) per iteration | O(1) event-driven |
| Timer precision | `sleep_for()` drift | Native timers |
| Cross-thread signaling | Mutex + manual wake | `uv_async_t` |
| Code clarity | Manual state machine | Callback-based |

---

## Dependencies

### 1. Add libuv to Build System

**File:** `server/Makefile`

```makefile
# Add to LDFLAGS
LDFLAGS += -luv

# Or if using pkg-config
LDFLAGS += $(shell pkg-config --libs libuv)
CXXFLAGS += $(shell pkg-config --cflags libuv)
```

### 2. Install libuv

```bash
# macOS
brew install libuv

# Ubuntu/Debian
apt-get install libuv1-dev

# From source
git clone https://github.com/libuv/libuv.git
cd libuv && mkdir build && cd build
cmake .. && make && make install
```

---

## Files to Modify

### Primary Changes

| File | Changes |
|------|---------|
| `include/queen/sidecar_db_pool.hpp` | Add libuv handles, change method signatures |
| `src/database/sidecar_db_pool.cpp` | Replace `poller_loop()` with libuv event loop |
| `Makefile` | Add `-luv` linker flag |

### No Changes Required

| File | Reason |
|------|--------|
| `acceptor_server.cpp` | Constructor interface unchanged |
| `config.hpp` | Tuning parameters still apply |
| HTTP handlers | `submit()` API unchanged |

---

## Architecture

### Current Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    poller_loop() Thread                      │
│                                                              │
│   while (running_) {                                         │
│       process_waiting_queue();          // POP_WAIT          │
│       sleep_for(remaining_ms);          // Micro-batch wait  │
│       drain_pending_to_slots();         // Send queries      │
│       select(max_fd, &read_fds, ...);   // Wait for results  │
│       for (slot : slots_) {             // Check each slot   │
│           if (FD_ISSET(slot.fd)) {                          │
│               process_result(slot);                          │
│           }                                                  │
│       }                                                      │
│   }                                                          │
└─────────────────────────────────────────────────────────────┘
```

### Target Flow (libuv)

```
┌─────────────────────────────────────────────────────────────┐
│                    libuv Event Loop                          │
│                                                              │
│   uv_run(loop_, UV_RUN_DEFAULT)                             │
│       │                                                      │
│       ├── uv_timer_t (batch_timer_)                         │
│       │   └── on_batch_timer()                              │
│       │       ├── process_waiting_queue()                   │
│       │       └── drain_pending_to_slots()                  │
│       │                                                      │
│       ├── uv_poll_t[0] (slot 0 socket)                      │
│       │   └── on_socket_event() → process_result()          │
│       │                                                      │
│       ├── uv_poll_t[1] (slot 1 socket)                      │
│       │   └── on_socket_event() → process_result()          │
│       │                                                      │
│       ├── ... (one uv_poll_t per connection)                │
│       │                                                      │
│       └── uv_async_t (submit_signal_)                       │
│           └── on_submit_signal()                            │
│               └── drain_pending_to_slots()                  │
└─────────────────────────────────────────────────────────────┘
```

---

## Implementation Details

### Step 1: Header Changes (`sidecar_db_pool.hpp`)

```cpp
#pragma once

#include <uv.h>  // ADD THIS
#include <libpq-fe.h>
// ... existing includes ...

namespace queen {

class SidecarDbPool {
public:
    // ... existing public interface (unchanged) ...
    
private:
    // === REMOVE ===
    // (nothing to remove from header, select is in cpp)
    
    // === ADD: libuv handles ===
    uv_loop_t* loop_ = nullptr;
    uv_timer_t batch_timer_;
    uv_timer_t waiting_timer_;
    uv_async_t submit_signal_;
    
    // === MODIFY: ConnectionSlot ===
    struct ConnectionSlot {
        PGconn* conn = nullptr;
        int socket_fd = -1;
        bool busy = false;
        std::string request_id;
        std::chrono::steady_clock::time_point query_start;
        
        // Batched request tracking
        std::vector<BatchedRequestInfo> batched_requests;
        bool is_batched = false;
        SidecarOpType op_type = SidecarOpType::PUSH;
        size_t total_items = 0;
        
        // === ADD: libuv poll handle ===
        uv_poll_t poll_handle;
        SidecarDbPool* pool = nullptr;  // Back-reference for callbacks
    };
    
    // === ADD: static callbacks for libuv ===
    static void on_batch_timer(uv_timer_t* handle);
    static void on_waiting_timer(uv_timer_t* handle);
    static void on_submit_signal(uv_async_t* handle);
    static void on_socket_event(uv_poll_t* handle, int status, int events);
    
    // === ADD: internal methods ===
    void start_watching_slot(ConnectionSlot& slot, int events);
    void stop_watching_slot(ConnectionSlot& slot);
    void drain_pending_to_slots();
};

} // namespace queen
```

### Step 2: Implementation Changes (`sidecar_db_pool.cpp`)

#### 2.1 Constructor — Initialize libuv

```cpp
SidecarDbPool::SidecarDbPool(/* ... existing params ... */)
    : /* ... existing initializers ... */
{
    slots_.resize(pool_size_);
    
    // Initialize libuv loop
    loop_ = new uv_loop_t;
    uv_loop_init(loop_);
    
    // Initialize batch timer
    uv_timer_init(loop_, &batch_timer_);
    batch_timer_.data = this;
    
    // Initialize waiting queue timer
    uv_timer_init(loop_, &waiting_timer_);
    waiting_timer_.data = this;
    
    // Initialize submit signal (for cross-thread wakeup)
    uv_async_init(loop_, &submit_signal_, on_submit_signal);
    submit_signal_.data = this;
    
    spdlog::info("[Worker {}] [Sidecar] Created with {} connections (libuv)", 
                 worker_id_, pool_size_);
}
```

#### 2.2 Destructor — Cleanup libuv

```cpp
SidecarDbPool::~SidecarDbPool() {
    stop();
    
    // Close all handles
    uv_timer_stop(&batch_timer_);
    uv_timer_stop(&waiting_timer_);
    uv_close((uv_handle_t*)&batch_timer_, nullptr);
    uv_close((uv_handle_t*)&waiting_timer_, nullptr);
    uv_close((uv_handle_t*)&submit_signal_, nullptr);
    
    for (auto& slot : slots_) {
        if (slot.conn) {
            uv_poll_stop(&slot.poll_handle);
            uv_close((uv_handle_t*)&slot.poll_handle, nullptr);
        }
        disconnect_slot(slot);
    }
    
    // Run loop to process close callbacks
    uv_run(loop_, UV_RUN_DEFAULT);
    uv_loop_close(loop_);
    delete loop_;
    
    spdlog::info("[SidecarDbPool] Destroyed (libuv)");
}
```

#### 2.3 connect_slot — Initialize uv_poll_t

```cpp
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
        spdlog::error("[SidecarDbPool] Failed to set non-blocking");
        PQfinish(slot.conn);
        slot.conn = nullptr;
        return false;
    }
    
    // Set statement timeout
    std::string timeout_sql = "SET statement_timeout = " + std::to_string(statement_timeout_ms_);
    PGresult* res = PQexec(slot.conn, timeout_sql.c_str());
    PQclear(res);
    
    slot.socket_fd = PQsocket(slot.conn);
    slot.busy = false;
    slot.pool = this;
    
    // === ADD: Initialize libuv poll handle ===
    uv_poll_init(loop_, &slot.poll_handle, slot.socket_fd);
    slot.poll_handle.data = &slot;
    
    return true;
}
```

#### 2.4 submit — Signal the loop

```cpp
void SidecarDbPool::submit(SidecarRequest request) {
    request.queued_at = std::chrono::steady_clock::now();
    
    if (request.op_type == SidecarOpType::POP_WAIT) {
        std::lock_guard<std::mutex> lock(waiting_mutex_);
        waiting_requests_.push_back(std::move(request));
    } else {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.push_back(std::move(request));
    }
    
    // === ADD: Wake up the event loop ===
    uv_async_send(&submit_signal_);
}
```

#### 2.5 poller_loop — Replace with libuv

```cpp
void SidecarDbPool::poller_loop() {
    spdlog::info("[SidecarDbPool] Starting libuv event loop");
    
    // Start batch timer (micro-batching interval)
    uv_timer_start(&batch_timer_, on_batch_timer, 
                   0,  // Initial delay
                   tuning_.micro_batch_wait_ms);  // Repeat interval
    
    // Start waiting queue timer (for POP_WAIT)
    uv_timer_start(&waiting_timer_, on_waiting_timer,
                   10,   // Initial delay
                   10);  // Check every 10ms
    
    // Run the event loop
    while (running_) {
        uv_run(loop_, UV_RUN_ONCE);
    }
    
    spdlog::info("[SidecarDbPool] libuv event loop exited");
}
```

#### 2.6 Static Callbacks

```cpp
void SidecarDbPool::on_batch_timer(uv_timer_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    pool->drain_pending_to_slots();
}

void SidecarDbPool::on_waiting_timer(uv_timer_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    pool->process_waiting_queue();
}

void SidecarDbPool::on_submit_signal(uv_async_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    // Immediate drain on submit (low latency path)
    pool->drain_pending_to_slots();
}

void SidecarDbPool::on_socket_event(uv_poll_t* handle, int status, int events) {
    auto* slot = static_cast<ConnectionSlot*>(handle->data);
    auto* pool = slot->pool;
    
    if (status < 0) {
        spdlog::error("[SidecarDbPool] Poll error: {}", uv_strerror(status));
        // Handle connection error...
        return;
    }
    
    if (events & UV_WRITABLE) {
        // Flush send buffer
        int flush_result = PQflush(slot->conn);
        if (flush_result == 0) {
            // All sent - switch to read-only
            uv_poll_start(handle, UV_READABLE, on_socket_event);
        } else if (flush_result == -1) {
            spdlog::error("[SidecarDbPool] PQflush failed");
            // Handle error...
        }
        // flush_result == 1 means more to send, keep watching writable
    }
    
    if (events & UV_READABLE) {
        if (!PQconsumeInput(slot->conn)) {
            spdlog::error("[SidecarDbPool] PQconsumeInput failed: {}", 
                         PQerrorMessage(slot->conn));
            // Handle connection error...
            return;
        }
        
        if (PQisBusy(slot->conn) == 0) {
            // Query complete - process results
            pool->process_slot_result(*slot);
            
            // Stop watching until next query
            uv_poll_stop(handle);
        }
    }
}
```

#### 2.7 Send Query — Start Watching

```cpp
void SidecarDbPool::start_watching_slot(ConnectionSlot& slot, int events) {
    uv_poll_start(&slot.poll_handle, events, on_socket_event);
}

void SidecarDbPool::stop_watching_slot(ConnectionSlot& slot) {
    uv_poll_stop(&slot.poll_handle);
}

// In drain_pending_to_slots(), after PQsendQueryParams():
if (sent) {
    // Start watching for write (flush) then read (result)
    start_watching_slot(*free_slot, UV_WRITABLE | UV_READABLE);
    free_slot->busy = true;
    // ... rest of setup ...
}
```

---

## Refactoring Strategy

### Phase 1: Extract Result Processing (Prep Work)

Before libuv migration, refactor `poller_loop()` to extract:

1. `drain_pending_to_slots()` — Move pending requests to slots
2. `process_slot_result(ConnectionSlot&)` — Handle query completion

This makes the libuv migration cleaner.

### Phase 2: Add libuv Scaffolding

1. Add libuv to Makefile
2. Add libuv handles to header
3. Initialize/cleanup in constructor/destructor
4. Keep `select()` loop working

### Phase 3: Replace select() Loop

1. Replace `poller_loop()` with `uv_run()`
2. Replace `select()` waiting with `uv_poll_t` callbacks
3. Replace `sleep_for()` with `uv_timer_t`
4. Add `uv_async_t` for cross-thread signaling

### Phase 4: Testing & Optimization

1. Run existing benchmarks
2. Compare CPU usage
3. Test reconnection handling
4. Test under high load

---

## Code Diff Summary

```diff
# Makefile
+ LDFLAGS += -luv

# sidecar_db_pool.hpp
+ #include <uv.h>
+ uv_loop_t* loop_;
+ uv_timer_t batch_timer_;
+ uv_timer_t waiting_timer_;
+ uv_async_t submit_signal_;
+ uv_poll_t poll_handle;  // in ConnectionSlot

# sidecar_db_pool.cpp
- #include <sys/select.h>
+ #include <uv.h>

- void poller_loop() {
-     while (running_) {
-         select(...);
-         // manual FD checking
-     }
- }
+ void poller_loop() {
+     uv_timer_start(&batch_timer_, ...);
+     while (running_) {
+         uv_run(loop_, UV_RUN_ONCE);
+     }
+ }

+ static void on_socket_event(uv_poll_t*, int, int);
+ static void on_batch_timer(uv_timer_t*);
+ static void on_submit_signal(uv_async_t*);
```

---

## Estimated Effort

| Task | Effort |
|------|--------|
| Add libuv dependency | 30 min |
| Refactor result processing | 2 hours |
| libuv scaffolding | 1 hour |
| Replace select() loop | 3 hours |
| Error handling & reconnection | 2 hours |
| Testing & debugging | 3 hours |
| **Total** | **~12 hours** |

---

## Critical Implementation Notes (from Code Review)

### 1. The `UV_WRITABLE` Trap — 100% CPU Risk

When a socket is writable (almost always when buffer isn't full), the callback fires **continuously**.

```cpp
void SidecarDbPool::on_socket_event(uv_poll_t* handle, int status, int events) {
    if (events & UV_WRITABLE) {
        int flush_result = PQflush(slot->conn);
        
        if (flush_result == 0) {
            // ⚠️ CRITICAL: MUST switch to read-only!
            // uv_poll_start REPLACES the event mask (doesn't add to it)
            uv_poll_start(handle, UV_READABLE, on_socket_event);
        }
        else if (flush_result == 1) {
            // More data to send - keep watching WRITABLE
            // (already watching, no change needed)
        }
        else {
            // flush_result == -1: Error
            handle_connection_error(slot);
        }
    }
    // ... handle UV_READABLE ...
}
```

**Key insight:** `uv_poll_start()` **replaces** the event mask, it doesn't add to it.

### 2. `uv_async_send` is Coalescing — Don't Lose Data!

```cpp
// If called 100 times quickly, callback may fire only ONCE
uv_async_send(&submit_signal_);
```

**Our design is safe** because:
1. Worker pushes to `pending_requests_` queue (mutex protected)
2. Worker signals async
3. Loop wakes up
4. `on_submit_signal` calls `drain_pending_to_slots()` which loops until queue is **empty**

```cpp
void SidecarDbPool::on_submit_signal(uv_async_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    
    // MUST drain ALL pending, not just one!
    pool->drain_pending_to_slots();  // Loops until queue empty
}
```

### 3. Batch Timer vs Async Signal — Avoid Useless Wakeups

**Problem:** If async signal drains the queue, batch timer fires 1ms later with empty queue.

**Solution:** Reset timer after async drain:

```cpp
void SidecarDbPool::on_submit_signal(uv_async_t* handle) {
    auto* pool = static_cast<SidecarDbPool*>(handle->data);
    pool->drain_pending_to_slots();
    
    // Reset timer to fire MICRO_BATCH_WAIT_MS from NOW
    // This prevents useless wakeup if timer was about to fire
    uv_timer_again(&pool->batch_timer_);
}
```

**Note:** For `uv_timer_again()` to work, timer must be started with a repeat value:
```cpp
uv_timer_start(&batch_timer_, on_batch_timer, 
               tuning_.micro_batch_wait_ms,  // Initial delay
               tuning_.micro_batch_wait_ms); // Repeat interval (required for again())
```

### 4. Batching Strategy Decision

**Option A: Strict Micro-Batching** (always wait to fill the batch)
- `submit()` only starts timer if not running
- `async_signal` not used for immediate drain
- Higher throughput, higher latency

**Option B: Low Latency with Opportunistic Batching** (current design)
- `async_signal` drains immediately
- Timer acts as fallback
- Lower latency, slightly lower throughput under load

**Our choice: Option B** — matches current `MAX_PENDING_COUNT` behavior where we skip wait if queue is large.

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| libuv callback complexity | Keep callback logic minimal, delegate to methods |
| Memory leaks from handles | Use RAII wrapper or ensure all handles closed |
| Reconnection handling | Test thoroughly, keep existing reconnect logic |
| Performance regression | Benchmark before/after |
| Build system issues | Test on both macOS and Linux |
| **UV_WRITABLE 100% CPU** | Always switch to UV_READABLE after flush completes |
| **Coalesced async signals** | Always drain entire queue, never assume 1:1 signal:request |
| **Timer/async race** | Use `uv_timer_again()` to reset after async drain |

---

## Success Criteria

1. ✅ All existing benchmarks pass with same or better throughput
2. ✅ CPU usage in sidecar thread reduced or unchanged
3. ✅ No FD_SETSIZE limit (can scale beyond 1024 connections)
4. ✅ Clean shutdown without handle leaks
5. ✅ Works on both macOS and Linux

---

## References

- [libuv documentation](https://docs.libuv.org/en/v1.x/)
- [uv_poll_t — Poll handle](https://docs.libuv.org/en/v1.x/poll.html)
- [uv_timer_t — Timer handle](https://docs.libuv.org/en/v1.x/timer.html)
- [uv_async_t — Async handle](https://docs.libuv.org/en/v1.x/async.html)
- [libpq Async Command Processing](https://www.postgresql.org/docs/current/libpq-async.html)

