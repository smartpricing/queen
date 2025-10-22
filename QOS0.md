# QoS 0: At-Most-Once Delivery + PostgreSQL Failover

## Overview

Add two complementary features to Queen:

1. **QoS 0 Batching** - Server-side buffering for high-throughput event streams
2. **PostgreSQL Failover** - Zero message loss during database outages

Both features are implemented using a **file-based buffer** that serves dual purposes:
- **Performance**: Batch events before writing to reduce DB load (10-100x improvement)
- **Reliability**: Buffer messages when PostgreSQL is down, replay when it recovers

**Key Design Decision:** File buffer instead of in-memory to enable:
- ✅ Durability across server crashes
- ✅ Multi-process coordination (multiple workers share same files)
- ✅ Automatic recovery at startup
- ✅ Zero message loss

---

## Use Cases

| Pattern | Options | Flow | Use For |
|---------|---------|------|---------|
| **QoS 0 Events** | `{ bufferMs: 100 }` | File buffer → Batched DB write | Metrics, logs, analytics |
| **Normal Tasks** | None | Direct DB write (FIFO) | Payments, jobs, tasks |
| **Failover** | Automatic | Direct DB → File buffer on error | All messages (zero loss) |

---

## Architecture

### Message Flow

```
┌─────────────────────────────────────────────────────────┐
│                    Push Request                         │
└────────────────────┬────────────────────────────────────┘
                     │
          Has bufferMs/bufferMax?
                     │
        ┌────────────┴──────────────┐
        │                           │
       YES                         NO
  (QoS 0 explicit)           (Normal push)
        │                           │
        ▼                           ▼
┌──────────────────┐      ┌──────────────────┐
│ Write to         │      │ Try: Direct      │
│ qos0.buf         │      │   PostgreSQL     │
│ (always)         │      │                  │
└────────┬─────────┘      └────────┬─────────┘
         │                         │
         │                    Success? │
         │                         │   │
         │                     ┌───┘   └───┐
         │                    YES         NO (DB down)
         │                     │           │
         │                     │           ▼
         │                     │    ┌──────────────────┐
         │                     │    │ Write to         │
         │                     │    │ failover.buf     │
         │                     │    └────────┬─────────┘
         │                     │             │
         └─────────────────────┴─────────────┘
                               │
                    Return success to client
                               │
                               ▼
         ┌──────────────────────────────────────────┐
         │      Background Processor Thread         │
         │      (runs every 100ms)                  │
         └────────────┬────────────┬────────────────┘
                      │            │
         ┌────────────┘            └────────────┐
         │                                      │
         ▼                                      ▼
┌──────────────────────┐            ┌──────────────────────┐
│ Process QoS 0 Files  │            │ Process Failover     │
│ - Read batch (100)   │            │   Files (if DB up)   │
│ - Group by queue     │            │ - Read one-by-one    │
│ - Batch INSERT       │            │ - INSERT individually│
│ - Delete from file   │            │ - Preserve FIFO      │
└──────────────────────┘            └──────────────────────┘
```

### File Structure

```
/var/lib/queen/buffers/
├── qos0.buf                       # Active QoS 0 buffer (append-only)
├── qos0_processing.buf            # Being processed by background thread
├── failover.buf                   # Active failover buffer (append-only)
├── failover_processing.buf        # Being processed by background thread
└── failed/
    ├── failover_2024-01-15T10:30:00.buf  # Failed batches (DB was down)
    └── qos0_2024-01-15T10:30:00.buf
```

### File Format (Length-Prefixed)

```
[4 bytes: length][N bytes: JSON event][4 bytes: length][N bytes: JSON event]...
```

Example:
```
[52]{"queue":"events","payload":{"type":"login"}}[48]{"queue":"events","payload":{"type":"logout"}}
```

---

## Startup Sequence

**Critical:** Recovery BEFORE accepting requests

```
1. Server starts
   │
2. Initialize DatabasePool
   │
3. Create FileBufferManager
   │
   ├─→ Open buffer files (qos0.buf, failover.buf)
   │
   ├─→ STARTUP RECOVERY (BLOCKING - critical!)
   │   │
   │   ├─ Phase 1: Recover Failover Files (FIFO order)
   │   │  ├─ Process failed/failover_*.buf (oldest first)
   │   │  ├─ Process failover_processing.buf
   │   │  ├─ Process failover.buf
   │   │  └─ Insert ONE BY ONE to preserve order
   │   │
   │   ├─ Phase 2: Recover QoS 0 Files (batched)
   │   │  ├─ Process failed/qos0_*.buf
   │   │  ├─ Process qos0_processing.buf
   │   │  ├─ Process qos0.buf
   │   │  └─ Batch INSERT (100 events at a time)
   │   │
   │   └─ Log recovery stats
   │
   ├─→ Start background processor thread
   │   └─ Runs continuously every 100ms
   │
   └─→ Mark as ready
       │
4. Start accepting HTTP requests
```

---

## Implementation Plan

### Phase 1: File Buffer Manager (8-10 hours)

#### 1.1 Create FileBufferManager Class

**New file:** `server/include/queen/file_buffer.hpp`

```cpp
#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <json.hpp>

namespace queen {

class QueueManager;

class FileBufferManager {
public:
    FileBufferManager(
        std::shared_ptr<QueueManager> queue_manager,
        const std::string& buffer_dir = "/var/lib/queen/buffers",
        int flush_interval_ms = 100,
        size_t max_batch_size = 100
    );
    
    ~FileBufferManager();
    
    // Check if startup recovery is complete
    bool is_ready() const { return ready_; }
    
    // Write event to buffer (fast, durable)
    // Returns false only if disk is full
    bool write_event(const nlohmann::json& event);
    
    // Get stats
    size_t get_pending_count() const { return pending_count_.load(); }
    size_t get_failed_count() const { return failed_count_.load(); }
    bool is_db_healthy() const { return db_healthy_.load(); }

private:
    // Startup recovery (blocking)
    void startup_recovery();
    size_t recover_failover_files();
    size_t recover_qos0_files();
    
    // Background processing (continuous)
    void background_processor();
    void process_qos0_events();
    void process_failover_events();
    
    // File operations
    void rotate_file(const std::string& active_file, int& fd);
    bool flush_batched_to_db(const std::vector<nlohmann::json>& events);
    bool flush_single_to_db(const nlohmann::json& event);
    void move_to_failed(const std::string& file, const std::string& type);
    void retry_failed_files();
    
    std::shared_ptr<QueueManager> queue_manager_;
    std::string buffer_dir_;
    
    // Separate files for QoS 0 vs failover
    std::string qos0_file_;
    std::string failover_file_;
    int qos0_fd_;
    int failover_fd_;
    
    // Thread control
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};  // True after startup recovery
    std::thread processor_thread_;
    
    // Configuration
    int flush_interval_ms_;
    size_t max_batch_size_;
    
    // Stats
    std::atomic<size_t> pending_count_{0};
    std::atomic<size_t> failed_count_{0};
    std::atomic<bool> db_healthy_{true};
};

} // namespace queen
```

**New file:** `server/src/services/file_buffer.cpp`

Implementation includes:
- Constructor with startup recovery
- `write_event()` - atomic append to file using `writev()`
- `startup_recovery()` - process existing files at startup
- `recover_failover_files()` - FIFO replay of failover events
- `recover_qos0_files()` - batched replay of QoS 0 events
- `background_processor()` - continuous processing loop
- `process_qos0_events()` - batch processing
- `process_failover_events()` - one-by-one processing
- File rotation and cleanup logic

See detailed implementation in appendix.

#### 1.2 Add Helper to QueueManager

**File:** `server/include/queen/queue_manager.hpp`

```cpp
// Add public method
void push_single_message(
    const std::string& queue_name,
    const std::string& partition_name,
    const nlohmann::json& payload,
    const std::string& namespace_name = "",
    const std::string& task = "",
    const std::string& transaction_id = "",
    const std::string& trace_id = ""
);
```

**File:** `server/src/managers/queue_manager.cpp`

Extract single-message insert logic from existing `push_messages()` batch method.

#### 1.3 Update Makefile

**File:** `server/Makefile`

```makefile
# Add to build targets
$(BUILD_DIR)/services/file_buffer.o: src/services/file_buffer.cpp include/queen/file_buffer.hpp
	@mkdir -p $(BUILD_DIR)/services
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Add to OBJS
OBJS += $(BUILD_DIR)/services/file_buffer.o
```

---

### Phase 2: HTTP Integration (2-3 hours)

#### 2.1 Modify Push Endpoint

**File:** `server/src/acceptor_server.cpp`

```cpp
// In setup_worker_routes()
void setup_worker_routes(
    uWS::App* app,
    std::shared_ptr<QueueManager> queue_manager,
    std::shared_ptr<AnalyticsManager> analytics_manager,
    std::shared_ptr<FileBufferManager> file_buffer,  // 🆕 Add parameter
    const Config& config,
    int worker_id
) {
    // ... existing routes ...
    
    // Modify POST /api/v1/push
    app->post("/api/v1/push", [file_buffer, queue_manager](auto* res, auto* req) {
        read_json_body(res,
            [file_buffer, queue_manager, res](const nlohmann::json& json) {
                try {
                    // Check if QoS 0 (explicit buffering requested)
                    bool qos0_buffering = json.contains("bufferMs") || json.contains("bufferMax");
                    
                    for (const auto& item : json["items"]) {
                        if (qos0_buffering) {
                            // QoS 0: Always use file buffer for batching
                            nlohmann::json buffered_event = {
                                {"queue", item["queue"]},
                                {"partition", item.value("partition", "Default")},
                                {"payload", item["payload"]},
                                {"namespace", item.value("namespace", "")},
                                {"task", item.value("task", "")},
                                {"traceId", item.value("traceId", "")},
                                {"transactionId", item.value("transactionId", generate_uuid())}
                            };
                            
                            bool success = file_buffer->write_event(buffered_event);
                            if (!success) {
                                throw std::runtime_error("Buffer write failed (disk full?)");
                            }
                            
                        } else {
                            // Normal push: Try direct DB write first (FIFO ordering)
                            try {
                                queue_manager->push_single_message(
                                    item["queue"],
                                    item.value("partition", "Default"),
                                    item["payload"],
                                    item.value("namespace", ""),
                                    item.value("task", ""),
                                    item.value("transactionId", generate_uuid()),
                                    item.value("traceId", "")
                                );
                                
                            } catch (const std::exception& db_error) {
                                // PostgreSQL is down - fallback to file buffer
                                spdlog::warn("PostgreSQL unavailable, using file buffer: {}", db_error.what());
                                
                                nlohmann::json buffered_event = {
                                    {"queue", item["queue"]},
                                    {"partition", item.value("partition", "Default")},
                                    {"payload", item["payload"]},
                                    {"namespace", item.value("namespace", "")},
                                    {"task", item.value("task", "")},
                                    {"traceId", item.value("traceId", "")},
                                    {"transactionId", item.value("transactionId", generate_uuid())},
                                    {"failover", true}  // Mark as failover event
                                };
                                
                                file_buffer->write_event(buffered_event);
                            }
                        }
                    }
                    
                    nlohmann::json response = {
                        {"pushed", true},
                        {"qos0", qos0_buffering},
                        {"dbHealthy", file_buffer->is_db_healthy()}
                    };
                    send_json_response(res, response);
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what());
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
}
```

#### 2.2 Update Worker Thread Initialization

**File:** `server/src/acceptor_server.cpp`

```cpp
static void worker_thread(const Config& config, int worker_id, int num_workers,
                         std::mutex& init_mutex,
                         std::vector<uWS::App*>& worker_apps) {
    spdlog::info("[Worker {}] Starting...", worker_id);
    
    try {
        // Create database pool
        int pool_per_thread = std::max(5, config.database.pool_size / num_workers);
        auto db_pool = std::make_shared<DatabasePool>(
            config.database.connection_string(),
            pool_per_thread,
            config.database.pool_acquisition_timeout,
            config.database.statement_timeout,
            config.database.lock_timeout,
            config.database.idle_timeout
        );
        
        // Create queue manager
        auto queue_manager = std::make_shared<QueueManager>(db_pool, config.queue);
        
        // Create analytics manager
        auto analytics_manager = std::make_shared<AnalyticsManager>(db_pool);
        
        // Initialize schema (only worker 0)
        if (worker_id == 0) {
            spdlog::info("[Worker 0] Initializing database schema...");
            queue_manager->initialize_schema();
        }
        
        // 🆕 Create file buffer manager (BLOCKS during startup recovery)
        spdlog::info("[Worker {}] Creating file buffer manager...", worker_id);
        auto file_buffer = std::make_shared<FileBufferManager>(
            queue_manager,
            config.file_buffer_dir,
            config.file_buffer_flush_ms,
            config.file_buffer_max_batch
        );
        
        // Wait for recovery to complete
        while (!file_buffer->is_ready()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        spdlog::info("[Worker {}] File buffer ready - Pending: {}, Failed: {}, DB: {}", 
                     worker_id,
                     file_buffer->get_pending_count(),
                     file_buffer->get_failed_count(),
                     file_buffer->is_db_healthy() ? "healthy" : "down");
        
        // Create worker app
        auto worker_app = new uWS::App();
        
        // Setup routes (pass file_buffer)
        setup_worker_routes(worker_app, queue_manager, analytics_manager,
                           file_buffer, config, worker_id);
        
        // Register with acceptor
        {
            std::lock_guard<std::mutex> lock(init_mutex);
            worker_apps.push_back(worker_app);
            spdlog::info("[Worker {}] Registered with acceptor", worker_id);
        }
        
        // Listen on dummy port
        int dummy_port = 50000 + worker_id;
        worker_app->listen("127.0.0.1", dummy_port, [worker_id, dummy_port](auto* listen_socket) {
            if (listen_socket) {
                spdlog::debug("[Worker {}] Listening on dummy port {}", worker_id, dummy_port);
            }
        });
        
        spdlog::info("[Worker {}] Event loop ready", worker_id);
        
        // Run worker event loop (blocks forever)
        worker_app->run();
        
    } catch (const std::exception& e) {
        spdlog::error("[Worker {}] FATAL: {}", worker_id, e.what());
    }
}
```

---

### Phase 3: Auto-Ack Implementation (2-3 hours)

**Note:** Auto-ack is independent of file buffering. Implementation remains the same as previous plan.

#### 3.1 Add AutoAck to PopOptions

**File:** `server/include/queen/queue_manager.hpp`

```cpp
struct PopOptions {
    int batch = 1;
    bool wait = false;
    int timeout = 30000;
    std::string consumer_group = "__QUEUE_MODE__";
    bool auto_ack = false;  // 🆕 Add this
};
```

#### 3.2 Modify Pop Logic

See previous implementation - no changes needed for file buffer integration.

---

### Phase 4: Configuration (1-2 hours)

#### 4.1 Add Config Fields

**File:** `server/include/queen/config.hpp`

```cpp
struct ServerConfig {
    // ... existing fields ...
    
    std::string file_buffer_dir = "/var/lib/queen/buffers";
    int file_buffer_flush_ms = 100;
    size_t file_buffer_max_batch = 100;
};
```

#### 4.2 Add Environment Variables

**File:** `server/ENV_VARIABLES.md`

```markdown
### File Buffer Configuration

- `FILE_BUFFER_DIR` (default: /var/lib/queen/buffers)
  - Directory for file buffers
  - Must be writable by queen-server process
  - Persists across restarts for recovery

- `FILE_BUFFER_FLUSH_MS` (default: 100)
  - Process buffered events every N milliseconds
  - Lower = lower latency, more DB writes
  - Higher = higher latency, fewer DB writes

- `FILE_BUFFER_MAX_BATCH` (default: 100)
  - Maximum events per batch write
  - Affects memory usage and DB batch size

Example:
```bash
FILE_BUFFER_DIR=/data/queen/buffers \
FILE_BUFFER_FLUSH_MS=50 \
FILE_BUFFER_MAX_BATCH=200 \
./bin/queen-server
```
```

#### 4.3 Parse Environment Variables

**File:** `server/src/main_acceptor.cpp`

```cpp
config.server.file_buffer_dir = get_env("FILE_BUFFER_DIR", "/var/lib/queen/buffers");
config.server.file_buffer_flush_ms = get_env_int("FILE_BUFFER_FLUSH_MS", 100);
config.server.file_buffer_max_batch = get_env_int("FILE_BUFFER_MAX_BATCH", 100);
```

---

### Phase 5: Client API (1-2 hours)

**File:** `client-js/client/client.js`

```javascript
class Queen {
  async push(address, data, options = {}) {
    await this.#ensureConnected();
    
    const { queue, partition, namespace, task } = this.#parseAddress(address);
    
    const payload = {
      items: [{
        queue,
        partition: partition || 'Default',
        payload: data,
        namespace: namespace || null,
        task: task || null
      }]
    };
    
    // Add buffer options if specified (QoS 0)
    if (options.bufferMs !== undefined || options.bufferMax !== undefined) {
      payload.bufferMs = options.bufferMs !== undefined ? options.bufferMs : 100;
      payload.bufferMax = options.bufferMax !== undefined ? options.bufferMax : 100;
    }
    
    const response = await withRetry(
      () => this.#http.post('/api/v1/push', payload),
      this.#config.retryAttempts,
      this.#config.retryDelay
    );
    
    return response;
  }
  
  async *take(address, options = {}) {
    // Same as before - add autoAck to params
    const params = {
      batch: options.batch || 1,
      wait: options.wait || false,
      timeout: options.timeout || 30000,
      consumerGroup: consumerGroup || '__QUEUE_MODE__'
    };
    
    if (options.autoAck) {
      params.autoAck = true;
    }
    
    // ... rest of take logic
  }
}
```

---

### Phase 6: Documentation (2-3 hours)

#### 6.1 Update README.md

Add section after existing examples:

```markdown
## QoS 0: At-Most-Once Event Streaming

For high-throughput event streams and database failover, Queen supports:
- **Server-side buffering** - Batch events for 10-100x fewer DB writes
- **Auto-acknowledgment** - Skip manual ack for fire-and-forget consumption
- **PostgreSQL failover** - Zero message loss during database outages

### Server-Side Buffering (QoS 0)

Batch events on the server before writing to database:

```javascript
// Publish events (buffered on server)
await client.push('metrics', { cpu: 45, memory: 67 }, {
  bufferMs: 100,      // Flush after 100ms
  bufferMax: 100      // Or after 100 events
});

// Server batches multiple events into single DB write
// 10-100x reduction in database load
```

**Performance:**
- Without buffering: 1000 events = 1000 DB writes
- With buffering: 1000 events = ~10 DB writes (100x improvement)

### Auto-Acknowledgment

Skip manual ack for lightweight consumption:

```javascript
// Consume with auto-ack
for await (const msg of client.take('metrics', { autoAck: true })) {
  updateDashboard(msg.data);
  // No ack() needed - automatically completed
}
```

### PostgreSQL Failover (Automatic)

Queen automatically buffers messages to disk when PostgreSQL is unavailable:

```javascript
// Normal push
await client.push('tasks', { work: 'process-payment' });

// If PostgreSQL is down:
// 1. Queen writes to file buffer instead
// 2. Returns success to client (zero message loss)
// 3. Replays to DB when PostgreSQL recovers
// 4. FIFO ordering preserved
```

**Benefits:**
- ✅ Zero message loss during DB outages
- ✅ Automatic recovery when DB comes back
- ✅ No client changes needed
- ✅ Messages persist across server crashes

### Event Streaming Pattern

Combine buffering + auto-ack + consumer groups:

```javascript
// Publisher (buffered)
await client.push('user:events', { action: 'login' }, {
  bufferMs: 100,
  bufferMax: 100
});

// Multiple subscribers (fan-out)
for await (const event of client.take('user:events@dashboard', { autoAck: true })) {
  updateUI(event.data);
}

for await (const event of client.take('user:events@analytics', { autoAck: true })) {
  trackEvent(event.data);
}
```

### When to Use

| Feature | Use For | Don't Use For |
|---------|---------|---------------|
| **Buffering (QoS 0)** | Metrics, logs, analytics, UI updates | Critical tasks, payments, jobs requiring retries |
| **Auto-Ack** | Fire-and-forget events, notifications | Tasks that need error handling and retries |
| **Failover** | Automatic (all messages) | N/A - always beneficial |
```

#### 6.2 Create Example

**New file:** `examples/09-event-streaming.js`

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({ baseUrls: ['http://localhost:6632'] });

// ============================================
// Example 1: QoS 0 Event Streaming
// ============================================

async function qos0Events() {
  console.log('Publishing high-frequency events...');
  
  for (let i = 0; i < 1000; i++) {
    await client.push('metrics', {
      cpu: Math.random() * 100,
      memory: Math.random() * 100,
      timestamp: Date.now()
    }, {
      bufferMs: 100,      // Server batches for 100ms
      bufferMax: 100      // Or until 100 events
    });
  }
  
  console.log('Published 1000 events (buffered on server)');
  console.log('Result: ~10 DB writes instead of 1000 (100x improvement)');
}

// ============================================
// Example 2: Multiple Consumer Groups
// ============================================

async function consumerGroups() {
  // Consumer 1: Dashboard (auto-ack)
  for await (const event of client.take('metrics@dashboard', { 
    autoAck: true,
    batch: 10 
  })) {
    console.log('[Dashboard] Update:', event.data);
  }
  
  // Consumer 2: Analytics (auto-ack)
  for await (const event of client.take('metrics@analytics', { 
    autoAck: true,
    batch: 10 
  })) {
    console.log('[Analytics] Track:', event.data);
  }
  
  // Consumer 3: Alerts (manual ack for reliability)
  for await (const event of client.take('metrics@alerts', { batch: 10 })) {
    if (event.data.cpu > 90) {
      console.log('[Alerts] High CPU!', event.data);
      await sendAlert(event.data);
    }
    await client.ack(event, true, { group: 'alerts' });
  }
}

// ============================================
// Example 3: PostgreSQL Failover
// ============================================

async function failoverDemo() {
  console.log('Pushing critical tasks...');
  
  // Normal push - automatic failover if DB is down
  for (let i = 0; i < 100; i++) {
    await client.push('tasks', { 
      taskId: i, 
      type: 'process-payment' 
    });
  }
  
  console.log('Pushed 100 tasks');
  console.log('If PostgreSQL was down:');
  console.log('  - Tasks buffered to disk');
  console.log('  - Zero message loss');
  console.log('  - Auto-replay when DB recovers');
  console.log('  - FIFO order preserved');
}

// Run examples
await qos0Events();
// await consumerGroups();
// await failoverDemo();
```

#### 6.3 Update API.md

Add to push endpoint:

```markdown
#### QoS 0 Buffering

Add `bufferMs` and `bufferMax` to enable server-side batching:

```bash
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{
    "items": [
      { "queue": "events", "payload": {"type": "login"} }
    ],
    "bufferMs": 100,
    "bufferMax": 100
  }'
```

Response:
```json
{
  "pushed": true,
  "qos0": true,
  "dbHealthy": true
}
```

Server batches events and flushes to database after 100ms or 100 events.
```

Add to pop endpoint:

```markdown
#### Auto-Ack

Add `autoAck=true` to skip manual acknowledgment:

```bash
curl "http://localhost:6632/api/v1/pop/queue/events?autoAck=true&batch=10"
```

Messages are immediately marked as completed upon delivery.
```

---

### Phase 7: Testing (4-6 hours)

#### 7.1 Unit Tests

**New file:** `client-js/test/qos0-tests.js`

```javascript
import { startTest, passTest, failTest, assert } from './utils.js';

export async function testQoS0Buffering(client) {
  startTest('QoS 0 server-side buffering', 'qos0');
  
  const queue = 'test-qos0-' + Date.now();
  
  // Push with buffering
  for (let i = 0; i < 10; i++) {
    await client.push(queue, { index: i }, {
      bufferMs: 100,
      bufferMax: 10
    });
  }
  
  // Wait for flush
  await new Promise(r => setTimeout(r, 200));
  
  // Consume
  const messages = [];
  for await (const msg of client.take(queue, { batch: 20 })) {
    messages.push(msg);
    await client.ack(msg);
    if (messages.length >= 10) break;
  }
  
  assert(messages.length === 10, 'Should receive all 10 events');
  
  passTest('QoS 0 buffering works');
}

export async function testAutoAck(client) {
  startTest('Auto-acknowledgment', 'qos0');
  
  const queue = 'test-autoack-' + Date.now();
  
  await client.push(queue, { test: 'data' });
  
  // Take with auto-ack
  let msg;
  for await (const m of client.take(queue, { autoAck: true })) {
    msg = m;
    break;
  }
  
  assert(msg.data.test === 'data', 'Should receive message');
  
  // Should NOT be available again
  await new Promise(r => setTimeout(r, 100));
  
  let count = 0;
  for await (const m of client.take(queue, { wait: false })) {
    count++;
    break;
  }
  
  assert(count === 0, 'Message auto-acked');
  
  passTest('Auto-ack works');
}

export async function testConsumerGroupAutoAck(client) {
  startTest('Consumer group auto-ack', 'qos0');
  
  const queue = 'test-cg-' + Date.now();
  
  await client.push(queue, { value: 42 });
  
  let msgA, msgB;
  for await (const msg of client.take(`${queue}@groupA`, { autoAck: true })) {
    msgA = msg;
    break;
  }
  
  for await (const msg of client.take(`${queue}@groupB`, { autoAck: true })) {
    msgB = msg;
    break;
  }
  
  assert(msgA.data.value === 42 && msgB.data.value === 42, 'Both groups got message');
  
  passTest('Consumer group auto-ack works');
}

export async function testNormalPushFIFO(client) {
  startTest('Normal push preserves FIFO', 'qos0');
  
  const queue = 'test-fifo-' + Date.now();
  
  // Push without buffering
  for (let i = 0; i < 10; i++) {
    await client.push(queue, { order: i });
  }
  
  // Verify order
  const messages = [];
  for await (const msg of client.take(queue, { batch: 10 })) {
    messages.push(msg);
    await client.ack(msg);
    if (messages.length >= 10) break;
  }
  
  for (let i = 0; i < 10; i++) {
    assert(messages[i].data.order === i, `Order preserved: ${i}`);
  }
  
  passTest('FIFO ordering works');
}

export async function runQoS0Tests(client) {
  await testQoS0Buffering(client);
  await testAutoAck(client);
  await testConsumerGroupAutoAck(client);
  await testNormalPushFIFO(client);
}
```

Add to test runner:
```javascript
// In client-js/test/test-new.js
import { runQoS0Tests } from './qos0-tests.js';

// After other tests
await runQoS0Tests(client);
```

#### 7.2 Startup Recovery Test

**New file:** `server/tests/file_buffer_recovery_test.cpp`

Test scenarios:
- Clean shutdown and restart
- Crash with pending events
- DB down at startup
- Multiple crash/restart cycles

#### 7.3 Performance Benchmark

**New file:** `client-js/benchmark/qos0_benchmark.js`

```javascript
import { Queen } from '../client/index.js';

const client = new Queen({ baseUrls: ['http://localhost:6632'] });

async function benchmarkNormal() {
  const queue = 'bench-normal-' + Date.now();
  const count = 1000;
  const start = Date.now();
  
  for (let i = 0; i < count; i++) {
    await client.push(queue, { index: i });
  }
  
  const elapsed = Date.now() - start;
  console.log(`Normal: ${count} events in ${elapsed}ms (${(count/elapsed*1000).toFixed(0)} events/sec)`);
}

async function benchmarkQoS0() {
  const queue = 'bench-qos0-' + Date.now();
  const count = 1000;
  const start = Date.now();
  
  for (let i = 0; i < count; i++) {
    await client.push(queue, { index: i }, { bufferMs: 100 });
  }
  
  await new Promise(r => setTimeout(r, 200));
  const elapsed = Date.now() - start;
  console.log(`QoS 0: ${count} events in ${elapsed}ms (${(count/elapsed*1000).toFixed(0)} events/sec)`);
}

console.log('QoS 0 Performance Benchmark\n');
await benchmarkNormal();
await benchmarkQoS0();
```

---

### Phase 8: Monitoring (1-2 hours)

#### 8.1 Add Stats Endpoint

**File:** `server/src/acceptor_server.cpp`

```cpp
app->get("/api/v1/status/buffers", [file_buffer](auto* res, auto* req) {
    try {
        nlohmann::json response = {
            {"pending", file_buffer->get_pending_count()},
            {"failed", file_buffer->get_failed_count()},
            {"dbHealthy", file_buffer->is_db_healthy()}
        };
        
        send_json_response(res, response);
    } catch (const std::exception& e) {
        send_error_response(res, e.what());
    }
});
```

#### 8.2 Add Logging

Key events to log:
- Startup recovery stats (events recovered)
- Buffer flush operations (events/batch)
- DB failover events
- Recovery after DB outage
- File rotation and cleanup

---

## Implementation Checklist

### Phase 1: File Buffer
- [ ] Create `file_buffer.hpp` header
- [ ] Implement `file_buffer.cpp`
  - [ ] Constructor with startup recovery
  - [ ] `write_event()` with atomic append
  - [ ] `startup_recovery()` blocking recovery
  - [ ] `recover_failover_files()` FIFO replay
  - [ ] `recover_qos0_files()` batched replay
  - [ ] `background_processor()` continuous thread
  - [ ] File rotation and cleanup
- [ ] Add `push_single_message()` to QueueManager
- [ ] Update Makefile
- [ ] Test compilation

### Phase 2: HTTP Integration
- [ ] Modify push endpoint (QoS 0 + failover logic)
- [ ] Update worker initialization (create FileBufferManager)
- [ ] Wait for startup recovery before accepting requests
- [ ] Test push endpoint with buffering
- [ ] Test push endpoint with DB down

### Phase 3: Auto-Ack
- [ ] Add `auto_ack` to PopOptions
- [ ] Modify queue mode pop logic
- [ ] Modify consumer group pop logic
- [ ] Update all pop endpoints
- [ ] Test auto-ack

### Phase 4: Configuration
- [ ] Add config fields
- [ ] Add environment variables
- [ ] Parse env vars
- [ ] Document configuration

### Phase 5: Client
- [ ] Update `push()` method
- [ ] Update `take()` method
- [ ] Test client methods

### Phase 6: Documentation
- [ ] Update README.md
- [ ] Create event streaming example
- [ ] Update API.md
- [ ] Document startup recovery

### Phase 7: Testing
- [ ] Write unit tests (4 scenarios)
- [ ] Write recovery tests
- [ ] Write performance benchmarks
- [ ] Load testing
- [ ] Test DB failover scenarios

### Phase 8: Monitoring
- [ ] Add buffer stats endpoint
- [ ] Add logging
- [ ] Test monitoring

---

## Performance & Reliability

### Performance (QoS 0)

**Without Buffering:**
```
1000 events/sec = 1000 DB writes/sec
Latency: ~1ms per write
Total: ~1000ms
```

**With Buffering (100ms, batch=100):**
```
1000 events/sec = ~10 DB writes/sec
Client latency: ~10μs (file write)
Background latency: ~100ms buffer + ~10ms DB
Total throughput: 10-100x improvement
```

### Reliability (Failover)

**Scenario: DB Outage**
```
1. PostgreSQL goes down
2. Client pushes continue (write to failover.buf)
3. Zero message loss
4. Messages persist on disk
5. PostgreSQL recovers
6. Background processor replays from failover.buf (FIFO order)
7. Normal operation resumes
```

**Scenario: Server Crash During DB Outage**
```
1. PostgreSQL is down
2. Events buffered in failover.buf
3. Server crashes (disk persists)
4. Server restarts
5. Startup recovery:
   - Finds failover.buf with 10,000 events
   - DB still down → keeps in buffer
   - Background thread polls
   - DB recovers
   - Replays all 10,000 events (FIFO)
6. Zero message loss ✅
```

### Ordering Guarantees

| Operation | Ordering |
|-----------|----------|
| **Normal push (DB up)** | ✅ FIFO (direct DB write) |
| **Normal push (DB down)** | ✅ FIFO (replayed one-by-one) |
| **QoS 0 push** | ⚠️ Within batch (batched insert) |
| **Failover replay** | ✅ FIFO (one-by-one processing) |
| **QoS 0 replay** | ⚠️ Within batch |

---

## File Buffer Details

### Thread Safety

✅ **Multi-process safe** - `O_APPEND` flag ensures atomic appends
✅ **Lock-free writes** - Multiple workers append simultaneously
✅ **Single processor** - One background thread per file

### File Rotation

```cpp
// Every 100ms
1. Check if active file has data
2. Close write FD
3. Rename: active.buf → processing.buf
4. Open new active.buf
5. Process processing.buf
6. Delete or move to failed/
```

### Disk Space Management

- Monitor buffer directory size
- Alert if >1GB or >100k events
- Automatic cleanup of processed files
- Failed files kept for manual review

### Recovery Time

```
10 events:      ~1ms
100 events:     ~10ms
1,000 events:   ~100ms
10,000 events:  ~1s
100,000 events: ~10s
```

Failover events processed one-by-one (slower but preserves FIFO).

---

## Deployment

### Directory Setup

```bash
# Create buffer directory
sudo mkdir -p /var/lib/queen/buffers
sudo chown queen:queen /var/lib/queen/buffers
sudo chmod 755 /var/lib/queen/buffers

# Start server
FILE_BUFFER_DIR=/var/lib/queen/buffers ./bin/queen-server
```

### Monitoring

```bash
# Check buffer stats
curl http://localhost:6632/api/v1/status/buffers

# Monitor buffer files
watch -n 1 'ls -lh /var/lib/queen/buffers/'

# Check logs for recovery events
journalctl -u queen-server | grep -i recovery
```

### Troubleshooting

**Problem:** Events stuck in buffer
```bash
# Check DB health
curl http://localhost:6632/health

# Check buffer stats
curl http://localhost:6632/api/v1/status/buffers

# Manual inspection
cat /var/lib/queen/buffers/failover.buf
```

**Problem:** Disk space running out
```bash
# Check buffer size
du -sh /var/lib/queen/buffers/

# Check for large failed files
ls -lh /var/lib/queen/buffers/failed/

# Manual cleanup (careful!)
rm /var/lib/queen/buffers/failed/old_*.buf
```

---

## Timeline Estimate

| Phase | Effort | Notes |
|-------|--------|-------|
| 1. File Buffer | 10 hours | Complex: recovery + threading |
| 2. HTTP Integration | 3 hours | Straightforward |
| 3. Auto-Ack | 3 hours | Same as before |
| 4. Configuration | 2 hours | Simple |
| 5. Client API | 2 hours | Simple |
| 6. Documentation | 3 hours | Examples + guides |
| 7. Testing | 6 hours | Critical: recovery scenarios |
| 8. Monitoring | 2 hours | Stats + logging |

**Total: ~31 hours (~4 days)**

---

## Success Metrics

- [ ] 90%+ reduction in DB writes for QoS 0 queues
- [ ] Zero message loss during DB outages
- [ ] <200ms average latency for buffered events
- [ ] Startup recovery completes in <10s for 10k events
- [ ] FIFO ordering preserved for normal pushes
- [ ] All tests passing
- [ ] Documentation complete

---

## Appendix: Complete FileBufferManager Implementation

See separate file: `server/src/services/file_buffer.cpp` (implementation too large for this doc)

Key methods:
- `startup_recovery()` - ~100 lines
- `recover_failover_files()` - ~80 lines
- `recover_qos0_files()` - ~80 lines
- `background_processor()` - ~50 lines
- `process_failover_events()` - ~60 lines
- `process_qos0_events()` - ~60 lines
- `write_event()` - ~20 lines
- File rotation and cleanup - ~40 lines

**Total: ~500 lines of C++**
