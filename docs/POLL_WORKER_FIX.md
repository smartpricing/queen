# ✅ Poll Worker Cross-Thread Fix - COMPLETE

## Problem Summary

After fixing the initial per-worker response queue issue for PUSH operations, a second segfault occurred during **consumer operations with long-polling**.

### Root Cause

**Same cross-thread access pattern, different location:**

1. **Worker 1, 2, 3** receive long-poll requests → register responses with their worker_id
2. **PollIntention** created but **didn't track worker_id** 
3. **Poll workers** (running in threadpool) fulfill intentions
4. **Poll workers** pushed ALL responses to `worker_response_queues[0]` (only had access to Worker 0's queue)
5. **Worker 0's timer** pops responses and tries to send them
6. **Worker 0** accesses response objects from Workers 1/2/3's event loops
7. **SEGFAULT** 💥

### Error Pattern in Logs

```
[Worker 3] SPOP: Registered poll intention 22e880de-... (wait=true)
[Worker 2] SPOP: Registered poll intention 1a27896f-... (wait=true)
[Worker 1] SPOP: Registered poll intention fd342974-... (wait=true)
...
[info] Long-poll fulfilled 1 intentions with 1000 messages...
Segmentation fault: 11
```

---

## Solution Implemented

### 1. Add worker_id to PollIntention
**File**: `server/include/queen/poll_intention_registry.hpp`

```cpp
struct PollIntention {
    std::string request_id;
    int worker_id;  // ← ADDED: Track which worker owns this request
    // ... rest of fields
};
```

### 2. Update Poll Worker Initialization
**Files**: `server/include/queen/poll_worker.hpp`, `server/src/services/poll_worker.cpp`

**Before**: Passed single queue (Worker 0's queue)
```cpp
void init_long_polling(
    ...
    std::shared_ptr<ResponseQueue> response_queue,
    ...
);
```

**After**: Pass ALL worker queues
```cpp
void init_long_polling(
    ...
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    ...
);
```

### 3. Update Poll Worker Loop
**File**: `server/src/services/poll_worker.cpp`

**Before**: Used single queue
```cpp
void poll_worker_loop(
    ...
    std::shared_ptr<ResponseQueue> response_queue,
    ...
) {
    // Timeout response
    response_queue->push(intention.request_id, empty_response, false, 204);
}
```

**After**: Route to correct worker's queue
```cpp
void poll_worker_loop(
    ...
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    ...
) {
    // Timeout response - route to correct worker!
    worker_response_queues[intention.worker_id]->push(intention.request_id, empty_response, false, 204);
}
```

### 4. Update Message Distribution
**File**: `server/src/services/poll_worker.cpp`

**Before**: All messages sent to same queue
```cpp
std::vector<std::string> distribute_to_clients(
    const PopResult& result,
    const std::vector<PollIntention>& batch,
    std::shared_ptr<ResponseQueue> response_queue
) {
    ...
    response_queue->push(intention.request_id, response, false, 200);
}
```

**After**: Route each response to its owner's queue
```cpp
std::vector<std::string> distribute_to_clients(
    const PopResult& result,
    const std::vector<PollIntention>& batch,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues
) {
    ...
    worker_response_queues[intention.worker_id]->push(intention.request_id, response, false, 200);
}
```

### 5. Set worker_id When Creating Intentions
**File**: `server/src/acceptor_server.cpp`

**Before**: worker_id not tracked
```cpp
queen::PollIntention intention{
    .request_id = request_id,
    .queue_name = queue_name,
    // ... no worker_id
};
```

**After**: worker_id included
```cpp
queen::PollIntention intention{
    .request_id = request_id,
    .worker_id = worker_id,  // ← ADDED
    .queue_name = queue_name,
    // ...
};
```

Updated in 3 locations:
- SPOP (specific partition long-poll)
- QPOP (queue-level long-poll)
- Namespace/task long-poll

### 6. Pass All Queues to Poll Workers
**File**: `server/src/acceptor_server.cpp`

**Before**:
```cpp
queen::init_long_polling(
    ...
    worker_response_queues[0],  // Only Worker 0's queue
    ...
);
```

**After**:
```cpp
queen::init_long_polling(
    ...
    worker_response_queues,  // All worker queues
    ...
);
```

---

## Files Modified

1. `server/include/queen/poll_intention_registry.hpp` - Added worker_id field
2. `server/include/queen/poll_worker.hpp` - Updated signatures (2 functions)
3. `server/src/services/poll_worker.cpp` - Updated routing logic (3 locations)
4. `server/src/acceptor_server.cpp` - Updated poll worker init + 3 intention creations

**Total Changes**: ~25 lines across 4 files

---

## How It Works Now

### Correct Flow

```
1. Worker 2 receives long-poll request
   ↓
2. Worker 2 calls register_response(res, 2) → response registered with worker_id=2
   ↓
3. Worker 2 creates PollIntention with worker_id=2
   ↓
4. Poll worker (in threadpool) fulfills intention
   ↓
5. Poll worker routes to: worker_response_queues[2]->push(...)
   ↓
6. Worker 2's timer pops from worker_response_queues[2]
   ↓
7. Worker 2 sends response using its own event loop
   ✅ No cross-thread access - NO SEGFAULT
```

### Key Principle

**Response Affinity**: Every response stays with its owning worker from registration through sending.

---

## Build Status

```
✅ Compilation: SUCCESS
✅ No errors
✅ Binary: bin/queen-server ready
```

---

## Testing

Run consumer benchmark with multiple workers:

```bash
cd server
export NUM_WORKERS=4
./bin/queen-server
```

Then in another terminal:
```bash
cd benchmark
./bin/benchmark consumer --threads 10 --count 1000000 --batch 1000
```

**Expected Result**: No segfaults during long-polling operations under high load.

---

## Verification Checklist

- [x] PollIntention includes worker_id
- [x] Poll workers receive all worker queues
- [x] Timeout responses route to correct queue
- [x] Success responses route to correct queue
- [x] All 3 PollIntention creation sites set worker_id
- [x] init_long_polling passes all queues
- [x] Code compiles without errors

---

## Summary

This fix completes the per-worker queue architecture by extending it to the long-polling system:

- **PUSH operations**: ✅ Fixed (first fix)
- **Long-poll operations**: ✅ Fixed (this fix)

Both fixes follow the same principle: **responses must only be accessed by the worker that registered them**.

**Status**: Ready for production testing with multiple workers and consumer load.

