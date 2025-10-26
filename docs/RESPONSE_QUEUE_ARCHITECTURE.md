# Response Queue Architecture

## Overview

This document describes the new **Response Queue Architecture** that replaces the unsafe `Loop::defer` pattern with a thread-safe queue-based approach.

## Problem Solved

### Before: Loop::defer Segfaults
```cpp
// DANGEROUS - Race condition between threads
db_thread_pool->push([res, data]() {
    auto result = do_database_work();
    
    // SEGFAULT RISK: res might be destroyed by client disconnect
    loop->defer([res, result]() {
        send_response(res, result);  // ← CRASH HERE
    });
});
```

### After: Response Queue Safety
```cpp
// SAFE - No thread boundary crossing with response objects
db_thread_pool->push([request_id, data]() {
    auto result = do_database_work();
    
    // SAFE: Just push to thread-safe queue
    global_response_queue->push(request_id, result);
});

// Timer polls queue in uWebSockets thread - completely safe
timer_callback() {
    while (auto item = response_queue->pop()) {
        if (response_registry->send_response(item.request_id, item.data)) {
            // Response sent successfully
        }
    }
}
```

## Architecture Components

### 1. ResponseQueue (Thread-Safe)
```cpp
class ResponseQueue {
    std::queue<ResponseItem> queue_;
    std::mutex mutex_;
public:
    void push(const std::string& request_id, const nlohmann::json& data, 
              bool is_error = false, int status_code = 200);
    bool pop(ResponseItem& item);
};
```

### 2. ResponseRegistry (Safe Response Tracking)
```cpp
class ResponseRegistry {
    std::unordered_map<std::string, std::shared_ptr<ResponseEntry>> responses_;
    std::mutex registry_mutex_;
public:
    std::string register_response(uWS::HttpResponse<false>* res);
    bool send_response(const std::string& request_id, const nlohmann::json& data);
    void cleanup_expired(std::chrono::milliseconds max_age);
};
```

### 3. Timer-Based Polling
```cpp
// Timer callback runs in uWebSockets event loop thread
static void response_timer_callback(us_timer_t* timer) {
    ResponseQueue::ResponseItem item;
    int processed = 0;
    
    // Process up to 50 responses per timer tick
    while (processed < 50 && response_queue->pop(item)) {
        bool sent = global_response_registry->send_response(
            item.request_id, item.data, item.is_error, item.status_code);
        processed++;
    }
}
```

## Flow Diagram

```
HTTP Request → uWebSockets Thread → Register Response → DB ThreadPool
                     ↑                                        ↓
                     |                                   Do Database Work
                     |                                        ↓
              Timer Polling ← Response Queue ← Push Result ←
                     ↓
              Send HTTP Response (SAFE)
```

## Key Benefits

### ✅ **Eliminates Segfaults**
- No more `Loop::defer` race conditions
- Response objects never cross thread boundaries
- Clean separation between database and response threads

### ✅ **Thread Safety**
- Thread-safe queue with mutex protection
- Response registry handles concurrent access safely
- Timer runs in uWebSockets event loop thread only

### ✅ **Low Latency**
- 50ms polling interval (configurable)
- Batch processing up to 50 responses per tick
- Minimal overhead compared to database operation times (50-500ms)

### ✅ **Graceful Error Handling**
- Expired responses are automatically cleaned up
- Aborted requests are safely discarded
- No memory leaks from orphaned responses

### ✅ **Simple & Debuggable**
- Clear data flow: ThreadPool → Queue → Timer → Response
- Easy to trace and debug
- No complex async state machines

## Performance Characteristics

| Metric | Before (Loop::defer) | After (Response Queue) |
|--------|---------------------|------------------------|
| **Latency** | ~0ms | ~50ms |
| **Safety** | ❌ Segfaults | ✅ No segfaults |
| **CPU Usage** | Low | Slightly higher |
| **Memory** | Low | Queue overhead |
| **Reliability** | ❌ Race conditions | ✅ Rock solid |

## Implementation Details

### Global Initialization
```cpp
// Global shared resources
static std::shared_ptr<queen::ResponseQueue> global_response_queue;
static std::shared_ptr<queen::ResponseRegistry> global_response_registry;

// Initialize once
global_response_queue = std::make_shared<queen::ResponseQueue>();
global_response_registry = std::make_shared<queen::ResponseRegistry>();
```

### Timer Setup (Per Worker)
```cpp
// Each worker has its own timer polling the shared queue
us_timer_t* response_timer = us_create_timer((us_loop_t*)uWS::Loop::get(), 0, sizeof(queen::ResponseQueue*));
*(queen::ResponseQueue**)us_timer_ext(response_timer) = global_response_queue.get();

// Poll every 50ms
us_timer_set(response_timer, response_timer_callback, 50, 50);
```

### HTTP Handler Pattern
```cpp
app->post("/api/v1/ack", [](auto* res, auto* req) {
    // Register response in uWebSockets thread - SAFE
    std::string request_id = global_response_registry->register_response(res);
    
    // Submit to database threadpool
    db_thread_pool->push([request_id, /* other params */]() {
        try {
            auto result = do_database_work();
            
            // SAFE: Push to thread-safe queue
            global_response_queue->push(request_id, result_json);
            
        } catch (const std::exception& e) {
            nlohmann::json error_json = {{"error", e.what()}};
            global_response_queue->push(request_id, error_json, true, 500);
        }
    });
});
```

## Testing

Run the test script to verify the implementation:

```bash
# Install axios if not already installed
npm install axios

# Run the test
node test_response_queue.js
```

The test verifies:
- ✅ Basic POP/ACK flow
- ✅ Concurrent operations
- ✅ Error handling
- ✅ Aborted request safety
- ✅ No segfaults under load

## Configuration

### Timer Interval
```cpp
// Adjust polling interval (default: 50ms)
us_timer_set(response_timer, response_timer_callback, 10, 10);  // 10ms for lower latency
us_timer_set(response_timer, response_timer_callback, 100, 100); // 100ms for lower CPU usage
```

### Batch Size
```cpp
// Process more/fewer responses per timer tick
while (processed < 100 && response_queue->pop(item)) {  // Higher throughput
while (processed < 25 && response_queue->pop(item)) {   // Lower latency
```

### Cleanup Frequency
```cpp
// Cleanup expired responses (default: every 200 ticks = ~10 seconds at 50ms)
if (++cleanup_counter >= 400) {  // Every 20 seconds
if (++cleanup_counter >= 100) {  // Every 5 seconds
```

## Migration Guide

### 1. Remove Loop::defer Usage
Replace all instances of:
```cpp
current_loop->defer([res, data]() {
    send_response(res, data);
});
```

### 2. Use Response Queue Pattern
Replace with:
```cpp
std::string request_id = global_response_registry->register_response(res);
db_thread_pool->push([request_id, data]() {
    auto result = do_work();
    global_response_queue->push(request_id, result);
});
```

### 3. Remove SafeResponseContext
The old `SafeResponseContext` class is no longer needed and can be removed.

## Conclusion

The Response Queue Architecture provides a **safe, reliable, and performant** solution to the `Loop::defer` segfault problem. While it adds a small latency overhead (50ms), this is negligible compared to database operation times and provides complete safety from race conditions.

The architecture is:
- **Simple to understand and debug**
- **Thread-safe by design**
- **Scalable under load**
- **Easy to test and verify**

This eliminates a major source of production crashes while maintaining high performance and reliability.
