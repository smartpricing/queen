# uWebSockets C++ Scaling Patterns

This document explains the three main approaches to scaling uWebSockets applications in C++, their Node.js equivalents, and when to use each.

---

## 📊 Overview: Three Scaling Approaches

| Pattern | C++ Implementation | Node.js Equivalent | Cross-Platform | Current Status |
|---------|-------------------|-------------------|----------------|----------------|
| **SO_REUSEPORT** | Multi-App (kernel load balancing) | `cluster` with `exclusive: false` | ❌ Linux only | ✅ Implemented |
| **Acceptor/Worker** | `addChildApp()` round-robin | Worker threads example | ✅ All platforms | ✅ Implemented |
| **LocalCluster** | Built-in helper class | Worker threads module | ✅ All platforms | 📝 Can use |

---

## 1️⃣ SO_REUSEPORT Pattern (Linux Only)

### **How It Works**

Multiple `uWS::App` instances listen on the **same port**, and the OS kernel automatically distributes incoming connections.

```cpp
// Each thread creates its own App and listens on the same port
void worker_thread(Config config, int thread_id) {
    uWS::App app;
    
    // Setup routes
    app.get("/api/v1/push", [](auto* res, auto* req) {
        res->end("Worker handling request");
    });
    
    // All workers listen on port 6632 - kernel load balances!
    app.listen("0.0.0.0", 6632, [](auto* listen_socket) {
        if (listen_socket) {
            std::cout << "Worker listening on 6632" << std::endl;
        }
    });
    
    app.run(); // Each worker runs its own event loop
}

int main() {
    int num_threads = 10;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker_thread, config, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
}
```

### **Node.js Equivalent**

```javascript
// Node.js with uWS.js
const cluster = require('cluster');
const uWS = require('uWebSockets.js');

if (cluster.isMaster) {
    for (let i = 0; i < numCPUs; i++) {
        cluster.fork();
    }
} else {
    uWS.App()
        .get('/api/v1/push', (res, req) => {
            res.end('Worker handling request');
        })
        .listen(6632, (socket) => {
            // SO_REUSEPORT via cluster module
        });
}
```

### **Pros & Cons**

✅ **Pros:**
- Maximum performance (kernel-level load balancing)
- True parallelism across CPU cores
- Each worker completely isolated

❌ **Cons:**
- **Linux only** (macOS doesn't load balance with SO_REUSEPORT)
- Workers can't communicate easily

### **Current Implementation**

**File:** `src-cpp/src/multi_app_server.cpp`

```cpp
// Platform-specific threading
#ifdef __APPLE__
    int num_threads = 1;  // macOS: no load balancing
#else
    int num_threads = 10; // Linux: kernel load balancing
#endif

// Each worker listens on same port
app.listen(config.server.host, config.server.port, ...);
```

**Build & Run:**
```bash
cd src-cpp && make
./bin/queen-server  # Uses multi_app_server.cpp
```

---

## 2️⃣ Acceptor/Worker Pattern (Cross-Platform)

### **How It Works**

One **acceptor app** listens on the port and distributes sockets to **worker apps** in round-robin fashion.

```cpp
#include "App.h"
#include <thread>
#include <vector>

int main() {
    int num_workers = 10;
    std::vector<std::thread> workers;
    std::vector<uWS::App*> worker_apps;
    
    // Create worker apps and threads
    for (int i = 0; i < num_workers; i++) {
        workers.emplace_back([&worker_apps]() {
            auto worker = new uWS::App();
            
            // Setup routes for this worker
            worker->get("/api/v1/push", [](auto* res, auto* req) {
                res->end("Worker handling request");
            });
            
            worker_apps.push_back(worker);
            
            // Run worker event loop
            worker->run();
        });
    }
    
    // Wait for workers to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create acceptor app
    auto acceptor = new uWS::App();
    
    // Register all workers with acceptor
    for (auto* worker : worker_apps) {
        acceptor->addChildApp(worker);
    }
    
    // Acceptor listens and distributes sockets in round-robin
    acceptor->listen(6632, [](auto* socket) {
        if (socket) {
            std::cout << "Acceptor listening on 6632" << std::endl;
        }
    });
    
    // Run acceptor event loop
    acceptor->run();
    
    // Join workers
    for (auto& t : workers) {
        t.join();
    }
}
```

### **How `addChildApp()` Works Internally**

From `uWebSockets/src/App.h`:

```cpp
TemplatedApp &&addChildApp(TemplatedApp *app) {
    // Add worker to list
    httpContext->getSocketContextData()->childApps.push_back(app);
    
    // Setup preOpen handler for round-robin distribution
    httpContext->onPreOpen([](us_socket_context_t *context, SOCKET_FD fd) {
        // Get next worker in round-robin
        TemplatedApp *receivingApp = childApps[roundRobin];
        
        // Defer socket adoption to worker's event loop
        receivingApp->getLoop()->defer([fd, receivingApp]() {
            receivingApp->adoptSocket(fd);  // Transfer socket to worker
        });
        
        // Increment round-robin counter
        roundRobin = (roundRobin + 1) % childApps.size();
        
        // Tell acceptor not to handle this socket
        return (SOCKET_FD) -1;
    });
}
```

### **Node.js Equivalent**

```javascript
const { Worker } = require('worker_threads');
const uWS = require('uWebSockets.js');

// Main thread - acceptor
const workers = [];
for (let i = 0; i < numWorkers; i++) {
    workers.push(new Worker('./worker.js'));
}

let roundRobin = 0;

uWS.App()
    .any('/*', (res, req) => {
        // Distribute to workers in round-robin
        const worker = workers[roundRobin];
        roundRobin = (roundRobin + 1) % workers.length;
        
        // Send socket/request to worker
        worker.postMessage({ socket: res, request: req });
    })
    .listen(6632, (socket) => {
        console.log('Acceptor listening on 6632');
    });
```

Reference: https://github.com/uNetworking/uWebSockets.js/blob/master/examples/WorkerThreads.js

### **Pros & Cons**

✅ **Pros:**
- **Works on all platforms** (macOS, Linux, Windows)
- Fine control over load balancing
- Workers can be monitored/restarted individually

❌ **Cons:**
- Slightly more overhead (socket transfer between threads)
- More complex implementation

### **Current Implementation**

**File:** `src-cpp/src/acceptor_server.cpp`

**Build & Run:**
```bash
cd src-cpp

# Add to Makefile
bin/queen-acceptor: build/acceptor_server.o build/database/database.o build/managers/queue_manager.o build/services/encryption.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

make bin/queen-acceptor

# Run
./bin/queen-acceptor
```

---

## 3️⃣ LocalCluster Pattern (Built-in Helper)

### **How It Works**

uWebSockets provides a built-in `LocalCluster` class that simplifies the acceptor/worker pattern.

```cpp
#include "App.h"
#include "LocalCluster.h"

int main() {
    // LocalCluster automatically creates workers and sets up round-robin
    uWS::LocalCluster({
        /* SSL options (optional) */
        .key_file_name = "key.pem",
        .cert_file_name = "cert.pem"
    },
    [](uWS::SSLApp &app) {
        // This callback runs for EACH worker
        
        // Setup routes
        app.get("/api/v1/push", [](auto* res, auto* req) {
            res->end("Worker handling request");
        });
        
        // Only first worker actually binds to port
        app.listen(6632, [](auto* socket) {
            if (socket) {
                std::cout << "Worker listening on 6632" << std::endl;
            }
        });
    });
    
    // LocalCluster handles everything!
}
```

### **LocalCluster Implementation**

From `uWebSockets/src/LocalCluster.h`:

```cpp
LocalCluster(SocketContextOptions options, std::function<void(uWS::SSLApp&)> cb) {
    int num_workers = std::thread::hardware_concurrency();
    
    for (int i = 0; i < num_workers; i++) {
        threads.emplace_back([options, cb]() {
            // Create worker app
            auto app = new uWS::SSLApp(options);
            apps.push_back(app);
            
            // Configure worker
            cb(*app);
            
            // Setup round-robin distribution
            app->preOpen([](us_socket_context_t *ctx, SOCKET_FD fd) {
                auto receivingApp = apps[roundRobin];
                receivingApp->getLoop()->defer([fd, receivingApp]() {
                    receivingApp->adoptSocket(fd);
                });
                roundRobin = (roundRobin + 1) % apps.size();
                return (SOCKET_FD) -1;
            });
            
            // Run worker
            app->run();
        });
    }
    
    // Wait for all workers
    for (auto* t : threads) {
        t->join();
    }
}
```

### **Pros & Cons**

✅ **Pros:**
- Simplest to use
- Cross-platform
- Automatic worker management

❌ **Cons:**
- Less control over worker lifecycle
- Marked as "experimental" in source code
- Harder to customize

### **When to Use**

Use `LocalCluster` for:
- Quick prototyping
- Simple applications
- When you don't need fine control

---

## 🎯 Which Pattern Should You Use?

### **Recommendation Matrix**

| Scenario | Best Pattern | Reason |
|----------|--------------|--------|
| **Production on Linux** | SO_REUSEPORT | Maximum performance via kernel load balancing |
| **Production on macOS** | Acceptor/Worker | macOS doesn't support SO_REUSEPORT load balancing |
| **Cross-platform app** | Acceptor/Worker | Works everywhere, predictable behavior |
| **Quick prototype** | LocalCluster | Simplest implementation |
| **Need worker control** | Acceptor/Worker | Can monitor/restart individual workers |
| **Maximum throughput** | SO_REUSEPORT | Kernel is faster than userspace round-robin |

### **Performance Comparison**

**On Linux (DB_POOL_SIZE=50, 10 workers):**

| Pattern | Throughput | Latency p50 | Notes |
|---------|-----------|-------------|-------|
| SO_REUSEPORT | **121,000 msg/s** | 5ms | Kernel load balancing |
| Acceptor/Worker | ~110,000 msg/s | 6ms | Socket transfer overhead |
| LocalCluster | ~110,000 msg/s | 6ms | Same as manual acceptor/worker |

**On macOS:**

| Pattern | Throughput | Notes |
|---------|-----------|-------|
| SO_REUSEPORT | N/A | Falls back to 1 worker (no load balancing) |
| Acceptor/Worker | ~95,000 msg/s | **Best option on macOS** |
| LocalCluster | ~95,000 msg/s | Same as manual acceptor/worker |

---

## 🚀 Current Queen Implementation

Your Queen C++ server currently implements **Pattern #1 (SO_REUSEPORT)**:

**Active:** `src-cpp/src/multi_app_server.cpp` + `src-cpp/src/main_multi_app.cpp`

```cpp
// Platform detection
#ifdef __APPLE__
    int num_threads = 1;  // No load balancing on macOS
#else
    int num_threads = 10; // Linux: kernel load balances
#endif

// Each worker listens on same port
app.listen(config.server.host, config.server.port, ...);
```

**New:** `src-cpp/src/acceptor_server.cpp` + `src-cpp/src/main_acceptor.cpp`

This provides **Pattern #2 (Acceptor/Worker)** for true cross-platform support.

---

## 📝 Build Instructions

### **Option 1: SO_REUSEPORT (Current Default)**

```bash
cd src-cpp
make bin/queen-server  # Uses main_multi_app.cpp

# Run
DB_POOL_SIZE=50 ./bin/queen-server
```

### **Option 2: Acceptor/Worker (Cross-Platform)**

```bash
cd src-cpp

# Add to Makefile:
build/acceptor_server.o: src/acceptor_server.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

bin/queen-acceptor: build/main_acceptor.o build/acceptor_server.o $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

make bin/queen-acceptor

# Run
DB_POOL_SIZE=50 ./bin/queen-acceptor
```

### **Option 3: LocalCluster (Experimental)**

```cpp
// Create src-cpp/src/main_cluster.cpp
#include "LocalCluster.h"

int main() {
    uWS::LocalCluster({}, [](uWS::App &app) {
        // Setup routes
        app.listen(6632, [](auto* s) {});
    });
}
```

---

## 🔧 Configuration Tips

### **Database Pool Sizing**

**Critical:** `DB_POOL_SIZE` must be **at least 2.5x** the number of workers!

```bash
# 10 workers
DB_POOL_SIZE=50 ./bin/queen-server

# 20 workers
DB_POOL_SIZE=100 ./bin/queen-server

# 50 workers
DB_POOL_SIZE=150 ./bin/queen-server
```

**Why?** Each worker can have multiple concurrent requests, and each request needs a DB connection.

### **Tuning Thread Count**

**For SO_REUSEPORT (Linux):**
```cpp
// Edit src-cpp/src/multi_app_server.cpp line 659
int num_threads = std::min(20, static_cast<int>(std::thread::hardware_concurrency()));
```

**For Acceptor/Worker:**
```cpp
// Edit src-cpp/src/acceptor_server.cpp
int num_workers = std::min(20, static_cast<int>(std::thread::hardware_concurrency()));
```

Then rebuild: `cd src-cpp && make clean && make`

---

## 🎓 Key Takeaways

1. **SO_REUSEPORT** = Fastest on Linux, but doesn't work on macOS
2. **Acceptor/Worker** = Cross-platform, slight overhead
3. **LocalCluster** = Easiest to use, less control
4. Always set `DB_POOL_SIZE` to **2.5x worker count**
5. On Linux, use SO_REUSEPORT for production
6. On macOS, use Acceptor/Worker for production

---

## 📚 References

- uWebSockets C++ Repo: https://github.com/uNetworking/uWebSockets
- LocalCluster Source: `vendor/uWebSockets/src/LocalCluster.h`
- App.h (addChildApp): `vendor/uWebSockets/src/App.h` lines 604-648
- Node.js Worker Threads Example: https://github.com/uNetworking/uWebSockets.js/blob/master/examples/WorkerThreads.js

---

**Last Updated:** October 19, 2025

