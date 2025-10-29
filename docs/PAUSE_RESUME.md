# Queue Pause/Resume Feature - Implementation Plan

## Overview

Implement maintenance mode for queues where:
- **PUSH**: Continues to work, messages go to staging table
- **POP**: Returns empty (queue appears empty)
- **ACK/NACK**: Works normally for in-flight messages
- **RESUME**: Asynchronously flushes staging table to main messages table

**Critical constraint**: Maintain FIFO ordering. Queue remains paused during resume process.

---

## 1. Database Schema Changes

### 1.1 Add Columns to `queen.queues`

```sql
ALTER TABLE queen.queues 
    ADD COLUMN paused BOOLEAN DEFAULT FALSE,
    ADD COLUMN resume_in_progress BOOLEAN DEFAULT FALSE,
    ADD COLUMN last_paused_at TIMESTAMPTZ,
    ADD COLUMN last_resumed_at TIMESTAMPTZ;
```

**Indexes:**
```sql
CREATE INDEX idx_queues_paused 
    ON queen.queues(paused) WHERE paused = true;

CREATE INDEX idx_queues_resume_in_progress 
    ON queen.queues(resume_in_progress) WHERE resume_in_progress = true;
```

### 1.2 Create Staging Table

**No foreign keys** - survives queue/partition deletions.

```sql
CREATE TABLE queen.staged_messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    data JSONB NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_staged_messages_created_at 
    ON queen.staged_messages(created_at);

CREATE INDEX idx_staged_messages_queue 
    ON queen.staged_messages((data->>'queue'));

CREATE INDEX idx_staged_messages_data_gin 
    ON queen.staged_messages USING GIN (data);
```

**JSONB Structure:**
```json
{
  "queue": "myqueue",
  "partition": "Default",
  "payload": {...},
  "transactionId": "tx-12345",
  "traceId": "trace-uuid",
  "stagedReason": "queue_paused"
}
```

### 1.3 Migration Script

Create: `server/migrate_pause_resume.sql`

```sql
BEGIN;

-- Add columns
ALTER TABLE queen.queues 
    ADD COLUMN IF NOT EXISTS paused BOOLEAN DEFAULT FALSE,
    ADD COLUMN IF NOT EXISTS resume_in_progress BOOLEAN DEFAULT FALSE,
    ADD COLUMN IF NOT EXISTS last_paused_at TIMESTAMPTZ,
    ADD COLUMN IF NOT EXISTS last_resumed_at TIMESTAMPTZ;

-- Indexes on queues
CREATE INDEX IF NOT EXISTS idx_queues_paused 
    ON queen.queues(paused) WHERE paused = true;

CREATE INDEX IF NOT EXISTS idx_queues_resume_in_progress 
    ON queen.queues(resume_in_progress) WHERE resume_in_progress = true;

-- Staging table
CREATE TABLE IF NOT EXISTS queen.staged_messages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    data JSONB NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Indexes on staging
CREATE INDEX IF NOT EXISTS idx_staged_messages_created_at 
    ON queen.staged_messages(created_at);

CREATE INDEX IF NOT EXISTS idx_staged_messages_queue 
    ON queen.staged_messages((data->>'queue'));

CREATE INDEX IF NOT EXISTS idx_staged_messages_data_gin 
    ON queen.staged_messages USING GIN (data);

COMMIT;
```

---

## 2. C++ Backend Changes

### 2.1 Header Updates: `server/include/queen/queue_manager.hpp`

Add to `QueueManager` class:

```cpp
public:
    // Pause/Resume structures
    struct PauseResult {
        std::string queue_name;
        bool success;
        std::string status;  // "paused", "already_paused", "not_found", "error"
        int staged_count;
    };
    
    struct ResumeResult {
        std::string queue_name;
        bool success;
        std::string status;  // "resumed_async", "not_paused", "already_resuming", "not_found"
        int staged_count;
    };
    
    // Public methods
    PauseResult pause_queue(const std::string& queue_name);
    std::vector<PauseResult> pause_queues(const std::vector<std::string>& queue_names);
    
    ResumeResult resume_queue(const std::string& queue_name);
    std::vector<ResumeResult> resume_queues(const std::vector<std::string>& queue_names);
    
    void process_staged_messages_async(const std::string& queue_name);
    
    nlohmann::json get_queue_pause_status(const std::string& queue_name);
    
    std::vector<std::string> get_all_queue_names();
    std::vector<std::string> get_all_paused_queue_names();

private:
    bool check_queue_paused(const std::string& queue_name);
    PushResult insert_staged_message(const PushItem& item);
    PushResult push_single_message_internal(const PushItem& item);
    int move_staged_to_main(const std::string& queue_name);
```

**Note:** Update `PushResult` struct to include `staged` status option.

### 2.2 Implementation: `server/src/managers/queue_manager.cpp`

#### 2.2.1 Check Pause Status

```cpp
bool QueueManager::check_queue_paused(const std::string& queue_name) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string sql = "SELECT paused FROM queen.queues WHERE name = $1";
        auto result = QueryResult(conn->exec_params(sql, {queue_name}));
        
        if (result.num_rows() == 0) return false;
        return result.get_value(0, "paused") == "t";
        
    } catch (const std::exception& e) {
        spdlog::error("Error checking pause status for {}: {}", queue_name, e.what());
        return false;  // Fail open - allow operations
    }
}
```

#### 2.2.2 Modify Existing `push_single_message()`

**Location:** Find existing `push_single_message()` method

**Change:** Add pause check at the beginning:

```cpp
PushResult QueueManager::push_single_message(const PushItem& item) {
    // Ensure queue exists
    ensure_queue_exists(item.queue, /* namespace */, /* task */);
    ensure_partition_exists(item.queue, item.partition);
    
    // CHECK IF PAUSED
    bool is_paused = check_queue_paused(item.queue);
    
    if (is_paused) {
        // Route to staging table
        return insert_staged_message(item);
    }
    
    // EXISTING LOGIC - rename to push_single_message_internal()
    // Move all the current push logic to a new internal method
    return push_single_message_internal(item);
}
```

**Refactor:** Extract current push logic to `push_single_message_internal()`:

```cpp
PushResult QueueManager::push_single_message_internal(const PushItem& item) {
    // ALL THE CURRENT PUSH LOGIC GOES HERE
    // This bypasses pause check - used by resume process
    
    // ... existing implementation from push_single_message ...
}
```

#### 2.2.3 Insert to Staging Table

```cpp
PushResult QueueManager::insert_staged_message(const PushItem& item) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Build JSONB data
        nlohmann::json staged_data = {
            {"queue", item.queue},
            {"partition", item.partition},
            {"payload", item.payload},
            {"transactionId", item.transaction_id.value_or(generate_transaction_id())},
            {"stagedReason", "queue_paused"}
        };
        
        if (item.trace_id.has_value() && !item.trace_id->empty()) {
            staged_data["traceId"] = *item.trace_id;
        }
        
        std::string sql = R"(
            INSERT INTO queen.staged_messages (data)
            VALUES ($1::jsonb)
            RETURNING id
        )";
        
        std::vector<std::string> params = {staged_data.dump()};
        auto result = QueryResult(conn->exec_params(sql, params));
        
        if (!result.is_success()) {
            return {
                staged_data["transactionId"],
                "failed",
                "Failed to stage message",
                std::nullopt,
                std::nullopt
            };
        }
        
        return {
            staged_data["transactionId"],
            "staged",
            std::nullopt,
            result.get_value(0, "id"),
            staged_data.contains("traceId") ? 
                std::optional<std::string>(staged_data["traceId"]) : 
                std::nullopt
        };
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to stage message: {}", e.what());
        throw;
    }
}
```

#### 2.2.4 Modify POP Operations

**Update all POP methods:** Add pause check that returns empty immediately.

**Methods to modify:**
- `pop_messages()`
- `pop_from_queue_partition()`
- `pop_from_any_partition()`
- `pop_with_filters()`

**Example for `pop_messages()`:**

```cpp
PopResult QueueManager::pop_messages(
    const std::string& queue_name,
    const std::optional<std::string>& partition_name,
    const std::string& consumer_group,
    const PopOptions& options
) {
    // CHECK PAUSE STATUS FIRST
    bool is_paused = check_queue_paused(queue_name);
    
    if (is_paused) {
        // Return empty immediately
        PopResult result;
        result.messages = {};
        result.lease_id = std::nullopt;
        return result;
    }
    
    // EXISTING LOGIC
    // ... rest of current implementation ...
}
```

**Repeat for all POP variants.**

#### 2.2.5 Pause Queue

```cpp
PauseResult QueueManager::pause_queue(const std::string& queue_name) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Update to paused state
        std::string sql = R"(
            UPDATE queen.queues 
            SET paused = true,
                last_paused_at = NOW()
            WHERE name = $1 
              AND paused = false
            RETURNING id
        )";
        
        auto result = QueryResult(conn->exec_params(sql, {queue_name}));
        
        if (result.num_rows() == 0) {
            // Check if already paused or doesn't exist
            std::string check_sql = "SELECT paused FROM queen.queues WHERE name = $1";
            auto check = QueryResult(conn->exec_params(check_sql, {queue_name}));
            
            if (check.num_rows() == 0) {
                return {queue_name, false, "not_found", 0};
            }
            
            return {queue_name, false, "already_paused", 0};
        }
        
        // Count current staged messages
        std::string count_sql = R"(
            SELECT COUNT(*) as count 
            FROM queen.staged_messages 
            WHERE data->>'queue' = $1
        )";
        auto count_result = QueryResult(conn->exec_params(count_sql, {queue_name}));
        int staged = std::stoi(count_result.get_value(0, "count"));
        
        spdlog::info("Queue '{}' paused. Current staged: {}", queue_name, staged);
        
        return {queue_name, true, "paused", staged};
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pause queue {}: {}", queue_name, e.what());
        return {queue_name, false, "error", 0};
    }
}

std::vector<PauseResult> QueueManager::pause_queues(const std::vector<std::string>& queue_names) {
    std::vector<PauseResult> results;
    for (const auto& name : queue_names) {
        results.push_back(pause_queue(name));
    }
    return results;
}
```

#### 2.2.6 Resume Queue (Initiates Async Process)

```cpp
ResumeResult QueueManager::resume_queue(const std::string& queue_name) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Atomically mark as resuming
        std::string sql = R"(
            UPDATE queen.queues 
            SET resume_in_progress = true,
                last_resumed_at = NOW()
            WHERE name = $1 
              AND paused = true 
              AND resume_in_progress = false
            RETURNING id
        )";
        
        auto result = QueryResult(conn->exec_params(sql, {queue_name}));
        
        if (result.num_rows() == 0) {
            // Check state
            std::string check_sql = R"(
                SELECT paused, resume_in_progress 
                FROM queen.queues 
                WHERE name = $1
            )";
            auto check = QueryResult(conn->exec_params(check_sql, {queue_name}));
            
            if (check.num_rows() == 0) {
                return {queue_name, false, "not_found", 0};
            }
            
            bool paused = check.get_value(0, "paused") == "t";
            bool resuming = check.get_value(0, "resume_in_progress") == "t";
            
            if (!paused) {
                return {queue_name, false, "not_paused", 0};
            }
            if (resuming) {
                return {queue_name, false, "already_resuming", 0};
            }
        }
        
        // Count staged messages
        std::string count_sql = R"(
            SELECT COUNT(*) as count 
            FROM queen.staged_messages 
            WHERE data->>'queue' = $1
        )";
        auto count_result = QueryResult(conn->exec_params(count_sql, {queue_name}));
        int staged_count = std::stoi(count_result.get_value(0, "count"));
        
        spdlog::info("Queue '{}' resume initiated. Staged messages: {}", queue_name, staged_count);
        
        return {queue_name, true, "resumed_async", staged_count};
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to initiate resume for {}: {}", queue_name, e.what());
        return {queue_name, false, "error", 0};
    }
}

std::vector<ResumeResult> QueueManager::resume_queues(const std::vector<std::string>& queue_names) {
    std::vector<ResumeResult> results;
    for (const auto& name : queue_names) {
        results.push_back(resume_queue(name));
    }
    return results;
}
```

#### 2.2.7 Process Staged Messages (Async Background Job)

```cpp
void QueueManager::process_staged_messages_async(const std::string& queue_name) {
    try {
        spdlog::info("Starting staged message processing for queue: {}", queue_name);
        
        const int BATCH_SIZE = 500;
        int total_processed = 0;
        int total_failed = 0;
        
        while (true) {
            ScopedConnection conn(db_pool_.get());
            
            // Fetch batch (FIFO order)
            std::string fetch_sql = R"(
                SELECT id, data 
                FROM queen.staged_messages 
                WHERE data->>'queue' = $1
                ORDER BY created_at ASC
                LIMIT $2
            )";
            
            std::vector<std::string> params = {queue_name, std::to_string(BATCH_SIZE)};
            auto result = QueryResult(conn->exec_params(fetch_sql, params));
            
            if (result.num_rows() == 0) {
                break;  // No more staged messages
            }
            
            std::vector<std::string> processed_ids;
            
            // Process each message
            for (int i = 0; i < result.num_rows(); i++) {
                try {
                    std::string id = result.get_value(i, "id");
                    nlohmann::json data = nlohmann::json::parse(result.get_value(i, "data"));
                    
                    // Reconstruct PushItem
                    PushItem item;
                    item.queue = data.value("queue", "");
                    item.partition = data.value("partition", "Default");
                    item.payload = data.value("payload", nlohmann::json::object());
                    item.transaction_id = data.value("transactionId", "");
                    
                    if (data.contains("traceId") && !data["traceId"].get<std::string>().empty()) {
                        item.trace_id = data["traceId"].get<std::string>();
                    }
                    
                    // Push using internal method (bypasses pause check)
                    auto push_result = push_single_message_internal(item);
                    
                    if (push_result.status == "queued") {
                        processed_ids.push_back(id);
                        total_processed++;
                    } else {
                        total_failed++;
                        spdlog::warn("Failed to process staged message {}: {}", 
                                   id, push_result.error.value_or("unknown"));
                    }
                    
                } catch (const std::exception& e) {
                    total_failed++;
                    spdlog::error("Error processing staged message: {}", e.what());
                }
            }
            
            // Delete processed messages
            if (!processed_ids.empty()) {
                std::string delete_sql = "DELETE FROM queen.staged_messages WHERE id = ANY($1::uuid[])";
                
                std::string array_str = "{";
                for (size_t i = 0; i < processed_ids.size(); i++) {
                    if (i > 0) array_str += ",";
                    array_str += processed_ids[i];
                }
                array_str += "}";
                
                conn->exec_params(delete_sql, {array_str});
            }
            
            if (total_processed % 1000 == 0) {
                spdlog::info("Resume progress for '{}': {} processed, {} failed", 
                           queue_name, total_processed, total_failed);
            }
        }
        
        // Verify staging empty before unpausing
        ScopedConnection conn(db_pool_.get());
        
        std::string verify_sql = R"(
            SELECT COUNT(*) as count 
            FROM queen.staged_messages 
            WHERE data->>'queue' = $1
        )";
        auto verify_result = QueryResult(conn->exec_params(verify_sql, {queue_name}));
        int remaining = std::stoi(verify_result.get_value(0, "count"));
        
        if (remaining == 0) {
            // Safe to unpause
            std::string finish_sql = R"(
                UPDATE queen.queues 
                SET paused = false,
                    resume_in_progress = false
                WHERE name = $1
            )";
            conn->exec_params(finish_sql, {queue_name});
            
            spdlog::info("Resume COMPLETED for '{}': {} processed, {} failed", 
                        queue_name, total_processed, total_failed);
        } else {
            spdlog::warn("Resume for '{}' ended with {} remaining staged messages", 
                        queue_name, remaining);
            
            // Reset flag so operator can retry
            std::string reset_sql = R"(
                UPDATE queen.queues 
                SET resume_in_progress = false 
                WHERE name = $1
            )";
            conn->exec_params(reset_sql, {queue_name});
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Fatal error during resume for '{}': {}", queue_name, e.what());
        
        try {
            ScopedConnection conn(db_pool_.get());
            std::string reset_sql = R"(
                UPDATE queen.queues 
                SET resume_in_progress = false 
                WHERE name = $1
            )";
            conn->exec_params(reset_sql, {queue_name});
        } catch (...) {
            spdlog::error("Failed to reset resume_in_progress flag");
        }
    }
}
```

#### 2.2.8 Helper Methods

```cpp
std::vector<std::string> QueueManager::get_all_queue_names() {
    std::vector<std::string> names;
    try {
        ScopedConnection conn(db_pool_.get());
        std::string sql = "SELECT name FROM queen.queues ORDER BY name";
        auto result = QueryResult(conn->exec(sql));
        
        for (int i = 0; i < result.num_rows(); i++) {
            names.push_back(result.get_value(i, "name"));
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get queue names: {}", e.what());
    }
    return names;
}

std::vector<std::string> QueueManager::get_all_paused_queue_names() {
    std::vector<std::string> names;
    try {
        ScopedConnection conn(db_pool_.get());
        std::string sql = "SELECT name FROM queen.queues WHERE paused = true ORDER BY name";
        auto result = QueryResult(conn->exec(sql));
        
        for (int i = 0; i < result.num_rows(); i++) {
            names.push_back(result.get_value(i, "name"));
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get paused queue names: {}", e.what());
    }
    return names;
}

nlohmann::json QueueManager::get_queue_pause_status(const std::string& queue_name) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string sql = R"(
            SELECT paused, resume_in_progress, last_paused_at, last_resumed_at
            FROM queen.queues
            WHERE name = $1
        )";
        auto result = QueryResult(conn->exec_params(sql, {queue_name}));
        
        if (result.num_rows() == 0) {
            return {{"error", "Queue not found"}};
        }
        
        bool paused = result.get_value(0, "paused") == "t";
        bool resuming = result.get_value(0, "resume_in_progress") == "t";
        
        // Count staged messages
        std::string count_sql = R"(
            SELECT COUNT(*) as count 
            FROM queen.staged_messages 
            WHERE data->>'queue' = $1
        )";
        auto count_result = QueryResult(conn->exec_params(count_sql, {queue_name}));
        int staged = std::stoi(count_result.get_value(0, "count"));
        
        nlohmann::json status = {
            {"queue", queue_name},
            {"paused", paused},
            {"resumeInProgress", resuming},
            {"stagedCount", staged}
        };
        
        if (!result.is_null(0, 2)) {
            status["lastPausedAt"] = result.get_value(0, "last_paused_at");
        }
        if (!result.is_null(0, 3)) {
            status["lastResumedAt"] = result.get_value(0, "last_resumed_at");
        }
        
        return status;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to get pause status: {}", e.what());
        return {{"error", e.what()}};
    }
}
```

### 2.3 API Routes: `server/src/acceptor_server.cpp`

Add after existing routes:

```cpp
// POST /api/v1/queues/pause
app->post("/api/v1/queues/pause", [queue_manager, worker_id](auto* res, auto* req) {
    read_json_body(res,
        [res, queue_manager, worker_id](const nlohmann::json& body) {
            try {
                bool pause_all = body.value("all", false);
                std::vector<std::string> queue_names;
                
                if (pause_all) {
                    queue_names = queue_manager->get_all_queue_names();
                } else if (body.contains("queues") && body["queues"].is_array()) {
                    queue_names = body["queues"].get<std::vector<std::string>>();
                } else if (body.contains("queue")) {
                    queue_names.push_back(body["queue"].get<std::string>());
                } else {
                    send_error_response(res, "Missing 'queue', 'queues', or 'all' parameter", 400);
                    return;
                }
                
                auto results = queue_manager->pause_queues(queue_names);
                
                nlohmann::json response = nlohmann::json::array();
                for (const auto& r : results) {
                    response.push_back({
                        {"queue", r.queue_name},
                        {"success", r.success},
                        {"status", r.status},
                        {"stagedCount", r.staged_count}
                    });
                }
                
                send_json_response(res, {{"results", response}});
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
            }
        },
        [res](const std::string& error) {
            send_error_response(res, error, 400);
        }
    );
});

// POST /api/v1/queues/resume
app->post("/api/v1/queues/resume", [queue_manager, db_thread_pool, worker_id](auto* res, auto* req) {
    read_json_body(res,
        [res, queue_manager, db_thread_pool, worker_id](const nlohmann::json& body) {
            try {
                bool resume_all = body.value("all", false);
                std::vector<std::string> queue_names;
                
                if (resume_all) {
                    queue_names = queue_manager->get_all_paused_queue_names();
                } else if (body.contains("queues") && body["queues"].is_array()) {
                    queue_names = body["queues"].get<std::vector<std::string>>();
                } else if (body.contains("queue")) {
                    queue_names.push_back(body["queue"].get<std::string>());
                } else {
                    send_error_response(res, "Missing 'queue', 'queues', or 'all' parameter", 400);
                    return;
                }
                
                nlohmann::json response = nlohmann::json::array();
                
                for (const auto& queue_name : queue_names) {
                    auto result = queue_manager->resume_queue(queue_name);
                    
                    if (result.success && result.status == "resumed_async") {
                        // Spawn background job in DB thread pool
                        db_thread_pool->push([queue_manager, queue_name](int thread_id) {
                            queue_manager->process_staged_messages_async(queue_name);
                        });
                    }
                    
                    response.push_back({
                        {"queue", result.queue_name},
                        {"success", result.success},
                        {"status", result.status},
                        {"stagedCount", result.staged_count}
                    });
                }
                
                // Return 202 Accepted (async operation)
                send_json_response(res, {{"results", response}}, 202);
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
            }
        },
        [res](const std::string& error) {
            send_error_response(res, error, 400);
        }
    );
});

// GET /api/v1/queues/:queue/pause-status
app->get("/api/v1/queues/:queue/pause-status", [queue_manager](auto* res, auto* req) {
    try {
        std::string queue_name = std::string(req->getParameter(0));
        auto status = queue_manager->get_queue_pause_status(queue_name);
        send_json_response(res, status);
    } catch (const std::exception& e) {
        send_error_response(res, e.what(), 500);
    }
});
```

---

## 3. Frontend Changes

### 3.1 API Client: `webapp/src/api/queues.js`

Add methods:

```javascript
export const queuesApi = {
  // ... existing methods ...
  
  pauseQueue(queueName, reason = '') {
    return axios.post(`${API_BASE_URL}/api/v1/queues/pause`, {
      queue: queueName,
      reason
    });
  },
  
  pauseQueues(queueNames) {
    return axios.post(`${API_BASE_URL}/api/v1/queues/pause`, {
      queues: queueNames
    });
  },
  
  pauseAllQueues() {
    return axios.post(`${API_BASE_URL}/api/v1/queues/pause`, {
      all: true
    });
  },
  
  resumeQueue(queueName) {
    return axios.post(`${API_BASE_URL}/api/v1/queues/resume`, {
      queue: queueName
    });
  },
  
  resumeQueues(queueNames) {
    return axios.post(`${API_BASE_URL}/api/v1/queues/resume`, {
      queues: queueNames
    });
  },
  
  resumeAllQueues() {
    return axios.post(`${API_BASE_URL}/api/v1/queues/resume`, {
      all: true
    });
  },
  
  getQueuePauseStatus(queueName) {
    return axios.get(`${API_BASE_URL}/api/v1/queues/${queueName}/pause-status`);
  }
};
```

### 3.2 Update Queue List: `webapp/src/views/Queues.vue`

**Add pause/resume buttons to actions column:**

```vue
<td class="text-right" @click.stop>
  <!-- Pause button (if active) -->
  <button
    v-if="!queue.paused"
    @click="pauseQueue(queue)"
    class="p-1.5 rounded hover:bg-yellow-50 dark:hover:bg-yellow-900/20 
           text-yellow-600 dark:text-yellow-400 transition-colors mr-1"
    title="Pause queue"
  >
    <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" 
            d="M10 9v6m4-6v6m7-3a9 9 0 11-18 0 9 9 0 0118 0z" />
    </svg>
  </button>
  
  <!-- Resume button (if paused) -->
  <button
    v-else
    @click="resumeQueue(queue)"
    class="p-1.5 rounded hover:bg-green-50 dark:hover:bg-green-900/20 
           text-green-600 dark:text-green-400 transition-colors mr-1"
    title="Resume queue"
  >
    <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" 
            d="M14.752 11.168l-3.197-2.132A1 1 0 0010 9.87v4.263a1 1 0 001.555.832l3.197-2.132a1 1 0 000-1.664z" />
      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" 
            d="M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
    </svg>
  </button>
  
  <!-- Existing delete button -->
  <button @click="confirmDelete(queue)" ...>
</td>

<!-- Add badge to queue name -->
<td>
  <div class="flex items-center gap-2">
    <span class="font-medium">{{ queue.name }}</span>
    <span v-if="queue.paused && !queue.resumeInProgress" 
          class="badge badge-warning text-xs">
      PAUSED
    </span>
    <span v-if="queue.resumeInProgress" 
          class="badge badge-info text-xs">
      RESUMING
    </span>
  </div>
</td>
```

**Add methods:**

```javascript
async function pauseQueue(queue) {
  if (!confirm(`Pause queue "${queue.name}"?\n\nPOP operations will return empty. New messages will be staged.`)) {
    return;
  }
  
  try {
    await queuesApi.pauseQueue(queue.name);
    await loadData();
  } catch (err) {
    error.value = err.message;
  }
}

async function resumeQueue(queue) {
  try {
    const status = await queuesApi.getQueuePauseStatus(queue.name);
    const stagedCount = status.data.stagedCount || 0;
    
    if (!confirm(`Resume queue "${queue.name}"?\n\n${stagedCount} staged messages will be processed asynchronously.`)) {
      return;
    }
    
    await queuesApi.resumeQueue(queue.name);
    await loadData();
  } catch (err) {
    error.value = err.message;
  }
}
```

### 3.3 Update Analytics API: `webapp/src/api/analytics.js`

Modify `get_queues()` response to include pause fields:

**Backend changes to `analyticsManager.cpp`:**

Update query in `get_queues()`:

```cpp
std::string query = R"(
    WITH queue_message_stats AS (
        -- existing query
    )
    SELECT
        qms.*,
        q.paused,
        q.resume_in_progress,
        COALESCE((
            SELECT COUNT(*) 
            FROM queen.staged_messages sm 
            WHERE sm.data->>'queue' = q.name
        ), 0) as staged_count
    FROM queue_message_stats qms
    JOIN queen.queues q ON q.id = qms.queue_id
    ORDER BY qms.queue_created_at DESC
)";
```

Add to JSON response:

```cpp
queues.push_back({
    // ... existing fields ...
    {"paused", result.get_value(i, "paused") == "t"},
    {"resumeInProgress", result.get_value(i, "resume_in_progress") == "t"},
    {"stagedCount", safe_stoi(result.get_value(i, "staged_count"))}
});
```

---

## 4. API Documentation

### 4.1 Update `API.md`

Add section:

```markdown
### Queue Maintenance

#### `POST /api/v1/queues/pause`
Pause queue(s) for maintenance.

**Request:**
```json
// Single queue
{"queue": "myqueue"}

// Multiple queues
{"queues": ["queue1", "queue2"]}

// All queues
{"all": true}
```

**Response:**
```json
{
  "results": [
    {
      "queue": "myqueue",
      "success": true,
      "status": "paused",
      "stagedCount": 0
    }
  ]
}
```

#### `POST /api/v1/queues/resume`
Resume paused queue(s). Processes staged messages asynchronously.

**Response:** `202 Accepted`
```json
{
  "results": [
    {
      "queue": "myqueue",
      "success": true,
      "status": "resumed_async",
      "stagedCount": 1247
    }
  ]
}
```

#### `GET /api/v1/queues/:queue/pause-status`
Get pause/resume status.

**Response:**
```json
{
  "queue": "myqueue",
  "paused": true,
  "resumeInProgress": false,
  "stagedCount": 42,
  "lastPausedAt": "2025-10-29T10:30:00.000Z"
}
```
```

---

## 5. Testing Plan

### 5.1 Unit Tests

**Test pause:**
- Pause non-existent queue → `not_found`
- Pause active queue → `paused`
- Pause already paused queue → `already_paused`

**Test resume:**
- Resume non-existent queue → `not_found`
- Resume active queue → `not_paused`
- Resume paused queue → `resumed_async`
- Resume already resuming queue → `already_resuming`

**Test PUSH:**
- PUSH to active queue → goes to messages
- PUSH to paused queue → goes to staging
- PUSH during resume → goes to staging

**Test POP:**
- POP from active queue → returns messages
- POP from paused queue → returns empty
- POP during resume → returns empty

### 5.2 Integration Tests

**Scenario 1: Basic pause/resume**
1. Create queue, push 100 messages
2. Pause queue
3. Verify POP returns empty
4. Push 50 more messages
5. Verify 50 messages in staging
6. Resume queue
7. Wait for completion
8. Verify 150 messages in queue
9. POP all 150, verify order

**Scenario 2: Resume with concurrent pushes**
1. Pause queue
2. Push 1000 messages (go to staging)
3. Start resume
4. While resuming, push 500 more messages
5. Verify all 1500 messages processed in order

**Scenario 3: Multiple queues**
1. Pause all queues
2. Push to various queues
3. Resume all
4. Verify each queue processed independently

---

## 6. Monitoring & Observability

### 6.1 Logs

Add structured logging:
- Pause: `Queue 'X' paused. Staged: N`
- Resume start: `Resume initiated for 'X'. Staged: N`
- Resume progress: `Resume progress for 'X': 500/1000 processed`
- Resume complete: `Resume completed for 'X': N processed, M failed`
- Resume error: `Fatal error during resume for 'X': <error>`

### 6.2 Metrics (Future)

Consider tracking:
- `queen.queue.paused{queue="name"}` - gauge (0/1)
- `queen.staged_messages.count{queue="name"}` - gauge
- `queen.resume.duration{queue="name"}` - histogram
- `queen.resume.processed{queue="name"}` - counter

---

## 7. Rollout Plan

1. **Apply migration** - Run `migrate_pause_resume.sql`
2. **Deploy C++ server** - New binary with pause/resume logic
3. **Deploy frontend** - Updated UI with pause/resume controls
4. **Test on staging** - Full integration test suite
5. **Document** - Update API.md, README
6. **Production rollout** - Gradual rollout with monitoring

---

## 8. Edge Cases

### 8.1 Resume Failure Recovery

If resume process crashes:
- `resume_in_progress=true` remains set
- Queue stays paused
- Operator can manually reset: 
  ```sql
  UPDATE queen.queues SET resume_in_progress=false WHERE name='X';
  ```
- Then retry resume

### 8.2 Queue Deletion During Resume

If queue deleted while resuming:
- Background job will fail gracefully
- Staged messages remain (no FK constraint)
- Can be manually cleaned up

### 8.3 Very Large Staging Tables

For queues with millions of staged messages:
- Process in 500-message batches
- Logs show progress every 1000 messages
- Can take minutes/hours - that's OK
- Queue remains paused throughout

---

## 9. Future Enhancements

- **Scheduled pause/resume** - Cron-like scheduling
- **Partial pause** - Pause specific partitions
- **Drain mode** - Stop pushes, allow pops to empty queue
- **Progress API** - Real-time resume progress endpoint
- **Pause reason tracking** - Store why queue was paused
- **Auto-resume** - Resume after N hours automatically

---

## Implementation Order

1. ✅ Database migration
2. ✅ C++ QueueManager methods (pause, resume, check, stage, process)
3. ✅ Modify PUSH logic
4. ✅ Modify POP logic
5. ✅ API routes
6. ✅ Frontend API client
7. ✅ Frontend UI updates
8. ✅ Testing
9. ✅ Documentation
10. ✅ Deploy

---

**Critical Reminder:** Queue must remain `paused=true` during entire resume process to maintain FIFO ordering.

