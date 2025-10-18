# Multi-App Implementation Plan

## Architecture (Official uWebSockets Pattern)

```
Main Thread:
  - Parses config
  - Spawns N worker threads
  - Waits for shutdown signal

Worker Thread 1:                    Worker Thread 2:                    Worker Thread N:
├─ DatabasePool(5 connections)      ├─ DatabasePool(5 connections)      ├─ DatabasePool(5 connections)
├─ QueueManager(pool)                ├─ QueueManager(pool)                ├─ QueueManager(pool)
├─ uWS::App()                        ├─ uWS::App()                        ├─ uWS::App()
│  ├─ setup_routes()                 │  ├─ setup_routes()                 │  ├─ setup_routes()
│  ├─ listen(6632)                   │  ├─ listen(6632)                   │  ├─ listen(6632)
│  └─ run() [blocks]                 │  └─ run() [blocks]                 │  └─ run() [blocks]
└─ ISOLATED EVENT LOOP               └─ ISOLATED EVENT LOOP               └─ ISOLATED EVENT LOOP

OS Kernel: Load balances incoming connections across all N listeners (SO_REUSEPORT)
```

## Benefits

✅ **Zero Mutexes** - No shared state at all
✅ **Zero Synchronization** - Each thread completely isolated  
✅ **Simple Code** - No defer, no threading complexity
✅ **Perfect Scaling** - Linear with CPU cores
✅ **Official Pattern** - Exactly like EchoServerThreaded.cpp
✅ **Cache Friendly** - Each thread's data stays local

## Implementation Steps

1. Remove ThreadPool dependency
2. Create `worker_thread_main()` static function
3. Each worker creates: DatabasePool → QueueManager → App → routes
4. Main thread spawns N workers
5. All listen on same port
6. Each runs its own event loop

## Configuration

```cpp
// Config determines thread count
int num_threads = 10;  // Or from env: QUEEN_THREADS=10

// Each thread gets:
DB_POOL_SIZE_PER_THREAD = 5  // 10 threads × 5 = 50 total connections

// Total capacity:
50 PostgreSQL connections
10 event loops
Perfect CPU utilization
```

## Expected Performance

- **10 threads × 12K msg/s = 120K msg/s total**
- Zero crashes
- Clean shutdown
- Stable under load
- Handles any batch size

## Code Structure

```cpp
class QueenServer {
    static void worker_thread_main(Config config, int id) {
        // Thread-local everything!
        auto db_pool = make_shared<DatabasePool>(...);
        auto queue_mgr = make_shared<QueueManager>(db_pool);
        
        uWS::App app;
        setup_app_routes(app, queue_mgr, config);
        
        app.listen(config.port, ...).run();  // Blocks forever
    }
    
    static void setup_app_routes(App& app, ...) {
        app.get("/api/v1/pop/...", [queue_mgr](auto *res, auto *req) {
            // Single-threaded - no defer needed!
            auto result = queue_mgr->pop_messages(...);
            send_json_response(res, result);
        });
        // ... all other routes
    }
    
    bool start() {
        for (int i = 0; i < num_threads; i++) {
            worker_threads_.emplace_back(worker_thread_main, config_, i);
        }
        
        // Main thread waits for signal
        signal(SIGINT, handle_shutdown);
        while (!is_shutting_down_) sleep(1);
    }
};
```

This is the purest, simplest, most performant approach!

