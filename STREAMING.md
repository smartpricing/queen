# Queen Streaming: Implementation Plan

## Overview

Add comprehensive stream processing capabilities to Queen with **9 window types**:

1. **Tumbling Count** - Fixed-size non-overlapping windows by message count
2. **Tumbling Time** - Fixed-size non-overlapping windows by time
3. **Sliding Count** - Overlapping windows by message count
4. **Sliding Time** - Overlapping windows by time
5. **Session** - Dynamic windows based on inactivity gaps
6. **Custom Boundaries** - Windows aligned to business logic (hours, shifts, etc.)
7. **Global** - Single unbounded window with periodic snapshots
8. **With Watermarks** - Any time-based window with late arrival handling
9. **With Triggers** - Any window with early firing

Each consumer group defines its own windowing strategy on the same queue. Multiple consumers can process the same queue with different windows simultaneously.

---

## API Preview - All Window Types

```javascript
// 1. Tumbling Count - batch every 100 messages
for await (const window of client.takeWindow('events@batch', {
  type: 'tumblingCount',
  interval: 100
})) {
  await processBatch(window.messages);
  await client.ackWindow(window);
}

// 2. Tumbling Time - bucket every 5 minutes
for await (const window of client.takeWindow('events@hourly', {
  type: 'tumblingTime',
  interval: 300,
  gracePeriod: 10,
  align: 'epoch'
})) {
  await processTimeBucket(window.messages);
  await client.ackWindow(window);
}

// 3. Sliding Count - 100 messages, slide by 50
for await (const window of client.takeWindow('events@rolling', {
  type: 'slidingCount',
  interval: 100,
  slide: 50
})) {
  await calculateRollingAverage(window.messages);
  await client.ackWindow(window);
}

// 4. Sliding Time - 30s windows, slide by 10s
for await (const window of client.takeWindow('events@stream', {
  type: 'slidingTime',
  interval: 30,
  slide: 10,
  gracePeriod: 5
})) {
  await detectAnomalies(window.messages);
  await client.ackWindow(window);
}

// 5. Session - group by inactivity
for await (const window of client.takeWindow('activity@sessions', {
  type: 'session',
  gap: 300,
  maxDuration: 3600
})) {
  await analyzeSession(window.messages);
  await client.ackWindow(window);
}

// 6. Custom Boundaries - business hours
for await (const window of client.takeWindow('trades@shifts', {
  type: 'custom',
  boundaries: ['09:00', '17:00', '23:59'],
  timezone: 'America/New_York'
})) {
  await generateShiftReport(window.messages);
  await client.ackWindow(window);
}

// 7. Global - unbounded with snapshots
for await (const window of client.takeWindow('metrics@totals', {
  type: 'global',
  snapshot: { every: 3600, type: 'time' }
})) {
  await updateGlobalMetrics(window.messages);
  await client.ackWindow(window);
}

// 8. With Watermarks - handle late arrivals
for await (const window of client.takeWindow('sensors@iot', {
  type: 'tumblingTime',
  interval: 60,
  watermark: {
    maxLateness: 30,
    strategy: 'accumulate'
  }
})) {
  console.log(`On-time: ${window.onTimeCount}, Late: ${window.lateCount}`);
  await client.ackWindow(window);
}

// 9. With Triggers - early firing
for await (const window of client.takeWindow('events@realtime', {
  type: 'tumblingTime',
  interval: 300,
  triggers: [
    { type: 'onElement', every: 100 },
    { type: 'onTime', every: 60 },
    { type: 'onWatermark' }
  ],
  accumulationMode: 'accumulating'
})) {
  console.log(`Fired by: ${window.trigger}, Complete: ${window.complete}`);
  await processPartialOrComplete(window);
  await client.ackWindow(window, { final: window.complete });
}

// Multi-partition strategies
for await (const window of client.takeWindow('events@merged', {
  type: 'tumblingCount',
  interval: 100,
  partitionStrategy: 'merged'  // or 'separate'
})) {
  await processAcrossPartitions(window.messages);
  await client.ackWindow(window);
}
```

---

## Server Implementation

### 1. Database Schema

Complete schema supporting all window types:

```sql
-- Main window consumer configuration (flexible JSONB config)
CREATE TABLE queen.window_consumers (
  id SERIAL PRIMARY KEY,
  queue_name VARCHAR NOT NULL,
  consumer_group VARCHAR NOT NULL,
  partition_name VARCHAR,              -- NULL = all partitions
  
  -- Flexible configuration (stores all window config as JSON)
  window_config JSONB NOT NULL,
  
  -- Quick access fields (denormalized for performance)
  window_type VARCHAR NOT NULL,        -- 'tumblingTime', 'tumblingCount', etc.
  partition_strategy VARCHAR DEFAULT 'separate', -- 'separate' | 'merged'
  
  -- State tracking (type-dependent)
  last_processed_sequence BIGINT DEFAULT 0,
  last_window_end_time TIMESTAMP,
  last_snapshot_time TIMESTAMP,
  
  -- Metadata
  created_at TIMESTAMP DEFAULT NOW(),
  updated_at TIMESTAMP DEFAULT NOW(),
  
  UNIQUE(queue_name, consumer_group, COALESCE(partition_name, ''))
);

CREATE INDEX idx_window_consumers_lookup ON queen.window_consumers(queue_name, consumer_group);
CREATE INDEX idx_window_consumers_type ON queen.window_consumers(window_type);

-- Track delivered windows (for ack/lease management)
CREATE TABLE queen.window_state (
  id SERIAL PRIMARY KEY,
  window_consumer_id INTEGER REFERENCES queen.window_consumers(id),
  
  window_id VARCHAR NOT NULL,
  window_type VARCHAR,
  
  -- Window bounds (type-dependent, some may be NULL)
  start_sequence BIGINT,               -- For count-based
  end_sequence BIGINT,
  start_time TIMESTAMP,                -- For time-based
  end_time TIMESTAMP,
  watermark_time TIMESTAMP,            -- For watermarked windows
  
  -- Lease management
  lease_id VARCHAR UNIQUE,
  lease_expires_at TIMESTAMP,
  delivered_at TIMESTAMP DEFAULT NOW(),
  acked_at TIMESTAMP,
  ack_status VARCHAR,                  -- 'completed', 'failed', 'retry'
  
  -- Metadata
  message_count INTEGER,
  message_ids BIGINT[],                -- Array of message IDs in this window
  is_complete BOOLEAN DEFAULT TRUE,    -- False for triggered incomplete windows
  trigger_reason VARCHAR,              -- 'onElement', 'onTime', 'onWatermark', etc.
  
  UNIQUE(window_consumer_id, window_id)
);

CREATE INDEX idx_window_state_lease ON queen.window_state(lease_id) WHERE lease_id IS NOT NULL;
CREATE INDEX idx_window_state_consumer ON queen.window_state(window_consumer_id, acked_at);
CREATE INDEX idx_window_state_pending ON queen.window_state(window_consumer_id) WHERE acked_at IS NULL;

-- Session window tracking
CREATE TABLE queen.window_sessions (
  id SERIAL PRIMARY KEY,
  window_consumer_id INTEGER REFERENCES queen.window_consumers(id),
  session_id VARCHAR,
  
  first_event_at TIMESTAMP,
  last_event_at TIMESTAMP,
  message_count INTEGER DEFAULT 0,
  message_ids BIGINT[],
  
  closed BOOLEAN DEFAULT FALSE,
  closed_reason VARCHAR,               -- 'gap', 'maxDuration', 'manual'
  closed_at TIMESTAMP,
  
  UNIQUE(window_consumer_id, session_id)
);

CREATE INDEX idx_window_sessions_active ON queen.window_sessions(window_consumer_id) WHERE NOT closed;
CREATE INDEX idx_window_sessions_timeout ON queen.window_sessions(last_event_at) WHERE NOT closed;

-- Watermark tracking (for time-based windows with late arrivals)
CREATE TABLE queen.window_watermarks (
  id SERIAL PRIMARY KEY,
  window_consumer_id INTEGER REFERENCES queen.window_consumers(id),
  window_id VARCHAR,
  
  window_end_time TIMESTAMP,
  watermark_time TIMESTAMP,            -- window_end_time + maxLateness
  
  closed_at TIMESTAMP,
  delivered BOOLEAN DEFAULT FALSE,
  
  UNIQUE(window_consumer_id, window_id)
);

CREATE INDEX idx_window_watermarks_ready ON queen.window_watermarks(watermark_time) WHERE NOT delivered;

-- Trigger tracking (for windows with early firing)
CREATE TABLE queen.window_triggers (
  id SERIAL PRIMARY KEY,
  window_state_id INTEGER REFERENCES queen.window_state(id),
  
  trigger_type VARCHAR,                -- 'onElement', 'onTime', 'onWatermark', 'early'
  trigger_config JSONB,                -- Trigger-specific config
  
  last_fired_at TIMESTAMP,
  last_fired_count INTEGER,
  fire_count INTEGER DEFAULT 0,
  
  next_fire_time TIMESTAMP,            -- For time-based triggers
  next_fire_count INTEGER              -- For count-based triggers
);

CREATE INDEX idx_window_triggers_ready ON queen.window_triggers(next_fire_time) WHERE next_fire_time IS NOT NULL;

-- Global window state (for unbounded windows)
CREATE TABLE queen.window_global_state (
  id SERIAL PRIMARY KEY,
  window_consumer_id INTEGER REFERENCES queen.window_consumers(id),
  
  last_snapshot_at TIMESTAMP,
  last_snapshot_sequence BIGINT,
  total_messages_processed BIGINT DEFAULT 0,
  snapshot_count INTEGER DEFAULT 0,
  
  UNIQUE(window_consumer_id)
);
```

**Note**: Add table creation to `queue_manager.cpp` in the `initialize_database()` function where other tables are created. Keep a migration file `migrations/add_window_streaming.sql` for documentation.

---

### 2. Queue Manager - Data Structures

Add to `server/include/queen/queue_manager.hpp`:

```cpp
// Window configuration (supports all window types)
struct WindowConfig {
    std::string type;              // "tumblingTime", "tumblingCount", "slidingTime", "slidingCount", 
                                   // "session", "custom", "global"
    
    // Common options
    int interval = 0;              // Window size (seconds or count)
    int slide = 0;                 // Slide amount (0 for tumbling)
    int grace_period = 0;          // Seconds to wait after window ends
    std::string align = "first";   // "epoch", "first", "wall"
    bool allow_incomplete = false;
    
    // Partition strategy
    std::string partition_strategy = "separate";  // "separate" | "merged"
    
    // Session-specific
    int gap = 0;                   // Inactivity gap (seconds)
    int max_duration = 0;          // Max session duration (seconds)
    
    // Custom boundary-specific
    std::vector<std::string> boundaries;  // ["09:00", "17:00", "23:59"]
    std::string timezone;          // "America/New_York"
    
    // Global-specific
    std::string snapshot_type;     // "time" | "count"
    int snapshot_interval = 0;
    
    // Watermark configuration
    struct {
        int max_lateness = 0;      // Seconds
        std::string strategy;      // "accumulate" | "discard" | "sideOutput"
    } watermark;
    
    // Trigger configuration
    struct Trigger {
        std::string type;          // "onElement" | "onTime" | "onWatermark" | "early"
        int every = 0;             // For onElement/onTime
        int after = 0;             // For early triggers
    };
    std::vector<Trigger> triggers;
    std::string accumulation_mode = "accumulating";  // "accumulating" | "discarding"
    
    // Serialization
    nlohmann::json to_json() const;
    static WindowConfig from_json(const nlohmann::json& j);
};

// Window result returned to client
struct WindowResult {
    std::string window_id;
    int window_state_id;
    std::string lease_id;
    std::string window_type;
    
    // Bounds (populated based on window type)
    int64_t start_sequence = 0;
    int64_t end_sequence = 0;
    std::string start_time;
    std::string end_time;
    std::string watermark_time;
    
    // Status
    bool complete = true;
    std::string trigger_reason;
    
    // Messages
    std::vector<nlohmann::json> messages;
    
    // Watermark-specific
    int on_time_count = 0;
    int late_count = 0;
    std::vector<nlohmann::json> late_arrivals;
    
    // Session-specific
    std::string session_id;
    std::string closed_reason;
    
    // Partition info
    std::string partition_name;
    std::vector<std::string> partitions;  // For merged strategy
    
    // Serialization
    nlohmann::json to_json() const;
};

// Queue manager methods
class QueueManager {
public:
    // Main windowing API
    WindowResult take_window(const std::string& queue_name,
                            const std::string& consumer_group,
                            const WindowConfig& config,
                            const std::optional<std::string>& partition_name = std::nullopt);
    
    bool ack_window(int window_state_id, 
                   const std::string& lease_id,
                   const std::string& status,
                   bool final = true);
    
    bool renew_window_lease(int window_state_id,
                           const std::string& lease_id);
    
    bool trigger_window_manual(const std::string& queue_name,
                              const std::string& consumer_group);

private:
    // Window type implementations
    WindowResult take_tumbling_count(int consumer_id, const WindowConfig& config);
    WindowResult take_tumbling_time(int consumer_id, const WindowConfig& config);
    WindowResult take_sliding_count(int consumer_id, const WindowConfig& config);
    WindowResult take_sliding_time(int consumer_id, const WindowConfig& config);
    WindowResult take_session(int consumer_id, const WindowConfig& config);
    WindowResult take_custom(int consumer_id, const WindowConfig& config);
    WindowResult take_global(int consumer_id, const WindowConfig& config);
    
    // Helpers
    void apply_watermark(WindowResult& result, const WindowConfig& config);
    bool check_triggers(WindowResult& result, const WindowConfig& config);
    std::vector<nlohmann::json> query_messages_in_range(
        const std::string& queue_name,
        const std::optional<std::string>& partition_name,
        int64_t start_seq, int64_t end_seq,
        const std::string& start_time, const std::string& end_time);
};
```

---

### 3. Queue Manager - Implementation Logic

Add to `server/src/managers/queue_manager.cpp`:

#### Main Entry Point: `take_window()`

```cpp
WindowResult QueueManager::take_window(
    const std::string& queue_name,
    const std::string& consumer_group,
    const WindowConfig& config,
    const std::optional<std::string>& partition_name
) {
    ScopedConnection conn(db_pool_.get());
    
    // 1. Look up or create window_consumers entry
    std::string lookup_sql = R"(
        SELECT id, window_config, last_processed_sequence, last_window_end_time
        FROM queen.window_consumers
        WHERE queue_name = $1 AND consumer_group = $2 
          AND COALESCE(partition_name, '') = $3
    )";
    
    auto result = QueryResult(conn->exec_params(lookup_sql, {
        queue_name, consumer_group, partition_name.value_or("")
    }));
    
    int consumer_id;
    if (result.num_rows() == 0) {
        // Create new consumer entry
        std::string insert_sql = R"(
            INSERT INTO queen.window_consumers 
            (queue_name, consumer_group, partition_name, window_config, window_type, partition_strategy)
            VALUES ($1, $2, $3, $4, $5, $6)
            RETURNING id
        )";
        
        auto insert_result = QueryResult(conn->exec_params(insert_sql, {
            queue_name,
            consumer_group,
            partition_name.value_or(""),
            config.to_json().dump(),
            config.type,
            config.partition_strategy
        }));
        
        consumer_id = std::stoi(insert_result.get_value(0, "id"));
    } else {
        consumer_id = std::stoi(result.get_value(0, "id"));
    }
    
    // 2. Route to appropriate window type handler
    WindowResult window_result;
    
    if (config.type == "tumblingCount") {
        window_result = take_tumbling_count(consumer_id, config);
    } else if (config.type == "tumblingTime") {
        window_result = take_tumbling_time(consumer_id, config);
    } else if (config.type == "slidingCount") {
        window_result = take_sliding_count(consumer_id, config);
    } else if (config.type == "slidingTime") {
        window_result = take_sliding_time(consumer_id, config);
    } else if (config.type == "session") {
        window_result = take_session(consumer_id, config);
    } else if (config.type == "custom") {
        window_result = take_custom(consumer_id, config);
    } else if (config.type == "global") {
        window_result = take_global(consumer_id, config);
    } else {
        throw std::runtime_error("Unknown window type: " + config.type);
    }
    
    // 3. Apply watermark logic if configured
    if (config.watermark.max_lateness > 0) {
        apply_watermark(window_result, config);
    }
    
    // 4. Check triggers if configured
    if (!config.triggers.empty()) {
        check_triggers(window_result, config);
    }
    
    // 5. If window is ready, create window_state entry and lease
    if (!window_result.messages.empty() || window_result.complete) {
        std::string lease_id = generate_uuid();
        int lease_time = get_queue_lease_time(queue_name);
        
        std::string insert_state_sql = R"(
            INSERT INTO queen.window_state
            (window_consumer_id, window_id, window_type, start_sequence, end_sequence,
             start_time, end_time, watermark_time, lease_id, lease_expires_at,
             message_count, message_ids, is_complete, trigger_reason)
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, NOW() + INTERVAL '1 second' * $10,
                    $11, $12, $13, $14)
            RETURNING id
        )";
        
        // Prepare message IDs array
        std::string msg_ids_array = "{";
        for (size_t i = 0; i < window_result.messages.size(); i++) {
            if (i > 0) msg_ids_array += ",";
            msg_ids_array += std::to_string(window_result.messages[i]["id"].get<int64_t>());
        }
        msg_ids_array += "}";
        
        auto state_result = QueryResult(conn->exec_params(insert_state_sql, {
            std::to_string(consumer_id),
            window_result.window_id,
            window_result.window_type,
            std::to_string(window_result.start_sequence),
            std::to_string(window_result.end_sequence),
            window_result.start_time,
            window_result.end_time,
            window_result.watermark_time,
            lease_id,
            std::to_string(lease_time),
            std::to_string(window_result.messages.size()),
            msg_ids_array,
            window_result.complete ? "true" : "false",
            window_result.trigger_reason
        }));
        
        window_result.window_state_id = std::stoi(state_result.get_value(0, "id"));
        window_result.lease_id = lease_id;
    }
    
    return window_result;
}
```

#### Type-Specific Implementations (Pseudocode)

Each window type implementation follows this pattern:
1. Load consumer state
2. Calculate next window bounds
3. Check if window is ready
4. Query messages in window
5. Update consumer state
6. Return result

**See detailed implementation notes in code comments for each type.**

---

### 4. HTTP Endpoints

Add to `server/src/acceptor_server.cpp`:

```cpp
// POST /api/v1/window/take
ws->route("/api/v1/window/take", [&](auto* res, auto* req) {
    std::string body = std::string(req->getBody());
    auto json = nlohmann::json::parse(body);
    
    std::string queue_name = json["queue"];
    std::string consumer_group = json["consumerGroup"];
    std::optional<std::string> partition = json.value("partition", std::optional<std::string>());
    
    // Parse window config from JSON
    WindowConfig config = WindowConfig::from_json(json["windowConfig"]);
    
    try {
        WindowResult result = queue_manager->take_window(
            queue_name, consumer_group, config, partition
        );
        
        if (result.messages.empty() && !result.complete) {
            // No window ready yet
            nlohmann::json response = {
                {"window", nullptr},
                {"nextCheckIn", 1000}  // Suggest 1s poll interval
            };
            res->writeStatus("200 OK")
               ->writeHeader("Content-Type", "application/json")
               ->end(response.dump());
        } else {
            // Window ready
            nlohmann::json response = {
                {"window", result.to_json()}
            };
            res->writeStatus("200 OK")
               ->writeHeader("Content-Type", "application/json")
               ->end(response.dump());
        }
    } catch (const std::exception& e) {
        nlohmann::json error = {{"error", e.what()}};
        res->writeStatus("500 Internal Server Error")
           ->writeHeader("Content-Type", "application/json")
           ->end(error.dump());
    }
});

// POST /api/v1/window/ack
ws->route("/api/v1/window/ack", [&](auto* res, auto* req) {
    std::string body = std::string(req->getBody());
    auto json = nlohmann::json::parse(body);
    
    int window_state_id = json["windowStateId"];
    std::string lease_id = json["leaseId"];
    std::string status = json["status"];
    bool final = json.value("final", true);
    
    bool success = queue_manager->ack_window(window_state_id, lease_id, status, final);
    
    nlohmann::json response = {
        {"acked", success},
        {"final", final}
    };
    
    res->writeStatus("200 OK")
       ->writeHeader("Content-Type", "application/json")
       ->end(response.dump());
});

// POST /api/v1/window/renew
ws->route("/api/v1/window/renew", [&](auto* res, auto* req) {
    std::string body = std::string(req->getBody());
    auto json = nlohmann::json::parse(body);
    
    int window_state_id = json["windowStateId"];
    std::string lease_id = json["leaseId"];
    
    bool success = queue_manager->renew_window_lease(window_state_id, lease_id);
    
    nlohmann::json response = {{"renewed", success}};
    
    res->writeStatus("200 OK")
       ->writeHeader("Content-Type", "application/json")
       ->end(response.dump());
});

// POST /api/v1/window/trigger (for manual global window triggers)
ws->route("/api/v1/window/trigger", [&](auto* res, auto* req) {
    std::string body = std::string(req->getBody());
    auto json = nlohmann::json::parse(body);
    
    std::string queue_name = json["queue"];
    std::string consumer_group = json["consumerGroup"];
    
    bool success = queue_manager->trigger_window_manual(queue_name, consumer_group);
    
    nlohmann::json response = {{"triggered", success}};
    
    res->writeStatus("200 OK")
       ->writeHeader("Content-Type", "application/json")
       ->end(response.dump());
});
```

---

## Client Implementation

### Complete Client API

Add to `client-js/client/client.js`:

```javascript
/**
 * Take messages as windows (all types supported)
 * @param {string} address - Queue address with consumer group (required: "queue@group")
 * @param {Object} config - Window configuration
 * @yields {Object} Window objects
 */
async *takeWindow(address, config = {}) {
  await this.#ensureConnected();
  
  const { queue, partition, consumerGroup, namespace, task } = this.#parseAddress(address);
  
  if (!consumerGroup) {
    throw new Error('Consumer group required for windowing (use queue@group)');
  }
  
  const { type, limit = null } = config;
  if (!type) {
    throw new Error('Window type required');
  }
  
  let windowCount = 0;
  
  while (true) {
    const result = await withRetry(
      () => this.#http.post('/api/v1/window/take', {
        queue,
        partition: partition || null,
        consumerGroup,
        namespace: namespace || null,
        task: task || null,
        windowConfig: this.#buildWindowConfig(config)
      }),
      this.#config.retryAttempts,
      this.#config.retryDelay
    );
    
    if (!result || !result.window) {
      const pollInterval = config.pollInterval || result?.nextCheckIn || 1000;
      await new Promise(resolve => setTimeout(resolve, pollInterval));
      continue;
    }
    
    const window = {
      ...result.window,
      queueName: queue,
      consumerGroup,
      partition,
      _windowStateId: result.window.windowStateId,
      _leaseId: result.window.leaseId
    };
    
    yield window;
    
    windowCount++;
    if (limit && windowCount >= limit) {
      return;
    }
  }
}

/**
 * Build window config for server
 * @private
 */
#buildWindowConfig(config) {
  const windowConfig = {
    type: config.type,
    interval: config.interval || 0,
    slide: config.slide || 0,
    gracePeriod: config.gracePeriod || 0,
    align: config.align || 'first',
    allowIncomplete: config.allowIncomplete || false,
    partitionStrategy: config.partitionStrategy || 'separate'
  };
  
  // Session-specific
  if (config.gap) windowConfig.gap = config.gap;
  if (config.maxDuration) windowConfig.maxDuration = config.maxDuration;
  
  // Custom-specific
  if (config.boundaries) windowConfig.boundaries = config.boundaries;
  if (config.timezone) windowConfig.timezone = config.timezone;
  
  // Global-specific
  if (config.snapshot) windowConfig.snapshot = config.snapshot;
  
  // Watermark
  if (config.watermark) windowConfig.watermark = config.watermark;
  
  // Triggers
  if (config.triggers) {
    windowConfig.triggers = config.triggers;
    windowConfig.accumulationMode = config.accumulationMode || 'accumulating';
  }
  
  return windowConfig;
}

/**
 * Take a single window (non-blocking)
 */
async takeSingleWindow(address, config = {}) {
  for await (const window of this.takeWindow(address, { ...config, limit: 1 })) {
    return window;
  }
  return null;
}

/**
 * Acknowledge a window
 */
async ackWindow(window, status = true, options = {}) {
  await this.#ensureConnected();
  
  let windowStateId, leaseId;
  
  if (typeof window === 'object') {
    windowStateId = window.windowStateId || window._windowStateId;
    leaseId = window.leaseId || window._leaseId;
  } else {
    windowStateId = window;
    leaseId = null;
  }
  
  const ackStatus = status === true ? 'completed' 
                  : status === false ? 'failed'
                  : status;
  
  const result = await withRetry(
    () => this.#http.post('/api/v1/window/ack', {
      windowStateId,
      leaseId,
      status: ackStatus,
      final: options.final !== undefined ? options.final : true
    }),
    this.#config.retryAttempts,
    this.#config.retryDelay
  );
  
  return result;
}

/**
 * Renew window lease (for long processing)
 */
async renewWindowLease(window) {
  await this.#ensureConnected();
  
  let windowStateId, leaseId;
  
  if (typeof window === 'object') {
    windowStateId = window.windowStateId || window._windowStateId;
    leaseId = window.leaseId || window._leaseId;
  } else {
    windowStateId = window;
    leaseId = null;
  }
  
  const result = await withRetry(
    () => this.#http.post('/api/v1/window/renew', {
      windowStateId,
      leaseId
    }),
    this.#config.retryAttempts,
    this.#config.retryDelay
  );
  
  return result;
}

/**
 * Manually trigger a global window
 */
async triggerWindow(address) {
  await this.#ensureConnected();
  
  const { queue, consumerGroup } = this.#parseAddress(address);
  
  if (!consumerGroup) {
    throw new Error('Consumer group required (use queue@group)');
  }
  
  const result = await withRetry(
    () => this.#http.post('/api/v1/window/trigger', {
      queue,
      consumerGroup
    }),
    this.#config.retryAttempts,
    this.#config.retryDelay
  );
  
  return result;
}
```

---

## Examples

### Example 1: All Window Types Demo

Create `examples/09-streaming-windows.js`:

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({ baseUrls: ['http://localhost:6632'] });
const queue = 'stream-demo';

await client.queue(queue);

// Producer: Generate events
async function producer() {
  for (let i = 0; i < 200; i++) {
    await client.push(queue, {
      id: i,
      value: Math.random() * 100,
      timestamp: new Date().toISOString()
    });
    await new Promise(r => setTimeout(r, 100));
  }
}

// Consumer 1: Tumbling Count
async function tumblingCount() {
  for await (const window of client.takeWindow(`${queue}@tumbling-count`, {
    type: 'tumblingCount',
    interval: 10
  })) {
    console.log(`[Tumbling Count] Window ${window.id}: ${window.messages.length} msgs`);
    await client.ackWindow(window);
  }
}

// Consumer 2: Sliding Time
async function slidingTime() {
  for await (const window of client.takeWindow(`${queue}@sliding-time`, {
    type: 'slidingTime',
    interval: 5,
    slide: 2,
    gracePeriod: 1
  })) {
    console.log(`[Sliding Time] ${window.startTime} to ${window.endTime}: ${window.messages.length} msgs`);
    await client.ackWindow(window);
  }
}

// Consumer 3: Session
async function sessionWindows() {
  for await (const window of client.takeWindow(`${queue}@sessions`, {
    type: 'session',
    gap: 3,
    maxDuration: 30
  })) {
    console.log(`[Session] Duration ${window.messages.length} events, closed: ${window.closedReason}`);
    await client.ackWindow(window);
  }
}

// Consumer 4: With Triggers
async function withTriggers() {
  for await (const window of client.takeWindow(`${queue}@triggered`, {
    type: 'tumblingTime',
    interval: 20,
    triggers: [
      { type: 'onElement', every: 5 },
      { type: 'onTime', every: 5 }
    ],
    accumulationMode: 'accumulating'
  })) {
    console.log(`[Triggered] Fired by ${window.trigger}, complete: ${window.complete}, msgs: ${window.messages.length}`);
    await client.ackWindow(window, true, { final: window.complete });
  }
}

// Run all in parallel
await Promise.all([
  producer(),
  tumblingCount(),
  slidingTime(),
  sessionWindows(),
  withTriggers()
]);
```

### Example 2: Custom Business Hours

Create `examples/10-business-hours-windows.js`:

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({ baseUrls: ['http://localhost:6632'] });

// Process orders in business hour windows
for await (const window of client.takeWindow('orders@shift-reports', {
  type: 'custom',
  boundaries: ['09:00', '13:00', '17:00', '23:59'],
  timezone: 'America/New_York',
  gracePeriod: 60
})) {
  console.log(`Shift: ${window.startTime} to ${window.endTime}`);
  console.log(`Orders: ${window.messages.length}`);
  
  await generateShiftReport(window.messages);
  await client.ackWindow(window);
}
```

### Example 3: IoT with Late Arrivals

Create `examples/11-iot-watermarks.js`:

```javascript
import { Queen } from 'queen-mq';

const client = new Queen({ baseUrls: ['http://localhost:6632'] });

// Handle sensor data with late arrivals
for await (const window of client.takeWindow('sensors@analytics', {
  type: 'tumblingTime',
  interval: 60,
  watermark: {
    maxLateness: 30,
    strategy: 'accumulate'
  }
})) {
  console.log(`Window: ${window.startTime} to ${window.endTime}`);
  console.log(`On-time: ${window.onTimeCount}, Late: ${window.lateCount}`);
  
  // Process all readings
  const avg = window.messages.reduce((sum, m) => sum + m.data.reading, 0) / window.messages.length;
  
  if (window.lateCount > 0) {
    console.log(`Note: ${window.lateCount} readings arrived late`);
  }
  
  await saveSensorMetrics(avg, window);
  await client.ackWindow(window);
}
```

---

## Testing

Create `client-js/test/streaming-tests.js` with comprehensive tests for all window types:

```javascript
export async function testAllWindowTypes(client) {
  await testTumblingCount(client);
  await testTumblingTime(client);
  await testSlidingCount(client);
  await testSlidingTime(client);
  await testSessionWindows(client);
  await testCustomBoundaries(client);
  await testGlobalWindows(client);
  await testWatermarks(client);
  await testTriggers(client);
  await testMultiPartitionStrategies(client);
}

// Individual test functions...
export async function testTumblingCount(client) {
  startTest('Tumbling Count Windows', 'streaming');
  
  const queue = 'test-tumbling-count-' + Date.now();
  await client.queue(queue);
  
  // Push 25 messages
  for (let i = 0; i < 25; i++) {
    await client.push(queue, { index: i });
  }
  
  // Take 2 windows of 10 each
  let windows = [];
  for await (const window of client.takeWindow(`${queue}@test`, {
    type: 'tumblingCount',
    interval: 10,
    limit: 2
  })) {
    windows.push(window);
    assert(window.messages.length === 10);
    await client.ackWindow(window);
  }
  
  assert(windows.length === 2);
  assert(windows[0].messages[0].data.index === 0);
  assert(windows[1].messages[0].data.index === 10);
  
  passTest('Tumbling count windows work correctly');
}

// ... more tests for each window type
```

---

## Implementation Order

Build as a **stream processor from day one**. Watermarks and sessions are fundamental, not add-ons.

### Phase 1: Complete Core Windows (Foundation)
**Goal**: Tumbling, Session, Sliding × Count, Time + Watermarks = Full stream processor

Build all fundamental window types together. Time and count logic share the same infrastructure.

#### **Step 1: Database Schema** - All tables at once
   - `window_consumers`, `window_state` (core)
   - `window_watermarks` (for late arrivals)
   - `window_sessions` (for session windows)
   - Create indexes for both count and time queries

#### **Step 2: Tumbling Count** - Simplest starting point
   - Window by message count (e.g., every 100 messages)
   - Learn the window state machine
   - No time complexity, just counting
   - Test: batch processing, message accumulation
   - **Foundation for all other windows**

#### **Step 3: Tumbling Time** - Add time dimension
   - Window by time (e.g., every 5 minutes)
   - Grace period handling
   - Time alignment (epoch, wall, first)
   - Query messages by `created_at` instead of sequence
   - Test: time-series buckets, periodic processing
   - **Build time logic alongside count logic**

#### **Step 4: Session Windows** - Inactivity-based
   - Group messages by inactivity gap
   - Track `last_event_at` per session
   - Close when `NOW() - last_event_at > gap`
   - Maximum duration limit
   - Test: user sessions, conversation threads
   - **Time-based but simpler than tumbling time**

#### **Step 5: Watermarks** - Late arrival handling
   - Apply to all time-based windows (tumbling time, sessions)
   - Track watermark = `window_end_time + max_lateness`
   - Separate on-time vs late messages
   - Three strategies: accumulate, discard, sideOutput
   - Test: out-of-order message arrival, IoT sensors
   - **Essential stream processing feature**

#### **Step 6: Sliding Count** - Overlapping windows
   - Windows overlap by message count
   - Example: 100-message windows, slide by 50
   - Message retention: don't delete until all windows acked
   - Track which windows need which messages
   - Test: rolling averages, moving windows
   - **Builds on tumbling count logic**

#### **Step 7: Sliding Time** - Overlapping time windows
   - Windows overlap by time period
   - Example: 30-second windows, slide by 10 seconds
   - Combine sliding + time + watermark logic
   - Most complex, but all pieces already exist
   - Test: continuous monitoring, real-time analytics
   - **Combines all previous concepts**

#### **Step 8: Multi-partition Strategies**
   - **Separate** (default): each partition has independent windows
   - **Merged**: aggregate all partitions into single window
   - Works with all window types
   - Test: per-stream vs cross-stream processing

#### **Step 9: Client API** - Complete implementation
   - `takeWindow(address, config)` - all window types
   - `ackWindow(window, status, options)` - with final flag
   - `renewWindowLease(window)` - for long processing
   - Support all config options from day one

#### **Step 10: Comprehensive Examples**
   - Batch processing (tumbling count)
   - Time-series analytics (tumbling time)
   - User sessions (session windows)
   - IoT with late arrivals (tumbling time + watermarks)
   - Rolling metrics (sliding count)
   - Real-time dashboards (sliding time + watermarks)

#### **Step 11: Testing Suite**
   - Test each window type independently
   - Test watermarks with all time-based windows
   - Test partition strategies
   - Test message retention for sliding windows
   - Edge cases: empty windows, lease expiration, late arrivals

**Why build it all in Phase 1?**
- Time and count logic share 80% of the code
- Building them together is easier than retrofitting
- Sliding windows need retention logic - build it once
- Watermarks work with all time windows - implement once
- Sessions are just a different flavor of time windows
- **Result**: Complete, production-ready stream processor

**What you get after Phase 1:**
- ✅ All 6 core window types (tumbling/session/sliding × count/time)
- ✅ Watermark support for late arrivals
- ✅ Multi-partition strategies
- ✅ Complete client API
- ✅ Real-world examples
- ✅ **Ship it to production!**

---

### Phase 2: Advanced Window Types
**Goal**: Business-logic windows + unbounded streams + early firing

Now that all core windowing is complete, add specialized window types for specific use cases.

1. **Custom Boundaries** - Business hours, shifts, market hours
   - Time parsing with timezones
   - Calculate next boundary from config
   - Handle DST transitions
   - Examples: trading windows, shift reports, business day processing
   - Test: timezone handling, boundary calculations
   
2. **Global Windows** - Unbounded streams with snapshots
   - Single window that never closes
   - Periodic snapshots (time or count-based)
   - Manual trigger support via `client.triggerWindow()`
   - Examples: leaderboards, running totals, cumulative metrics
   - Test: snapshot timing, manual triggers
   
3. **Triggers** - Early window firing
   - Fire windows before completion
   - Types: onElement, onTime, onWatermark, early
   - Accumulating vs discarding mode
   - Works with ANY window type (tumbling, sliding, session)
   - Examples: progressive dashboards, early alerts, speculative execution
   - Test: trigger conditions, accumulation modes
   
4. **Tests** for all advanced types
   - Custom boundary parsing and calculation
   - Global window snapshots
   - Trigger combinations with base windows
   - Edge cases: DST transitions, manual triggers

5. **Examples**:
   - Trading hours windows (custom)
   - Global leaderboard (global + triggers)
   - Progressive analytics (tumbling time + triggers)
   - Business hour processing (custom + watermarks)

**Why Phase 2?**
- Builds on complete core windowing from Phase 1
- Custom, global, and triggers are orthogonal features
- Each enhances rather than changes core windows
- Real-world use cases for production systems

---

### Phase 3: Production Readiness
**Goal**: Performance, monitoring, cleanup, scale

Make it production-ready and optimized for real-world workloads.

1. **Message Cleanup** - Smart retention for sliding windows
   - Don't delete messages still needed by sliding windows
   - Track per consumer group: which messages are still needed
   - Efficient cleanup queries that check all window consumers
   - Vacuum old completed windows from `window_state`
   
2. **Performance Optimization**
   - Index tuning based on real usage patterns
   - Query optimization for large windows (1000+ messages)
   - Connection pooling for window queries
   - Batch operations where possible
   - Consider pagination for very large windows
   
3. **Lease Management**
   - Automatic lease renewal for long-running window processing
   - Lease expiration handling and recovery
   - Window re-delivery on failure
   - Retry limits for failed windows
   
4. **Background Jobs**
   - **Session timeout checker**: Close inactive sessions every 1-5s
   - **Watermark advancement**: Check for ready windows
   - **Cleanup job**: Delete old acked window_state entries
   - **Metrics collector**: Track window processing stats
   
5. **Monitoring & Observability**
   - Window processing metrics (windows/sec, messages/window)
   - Lag tracking (how far behind real-time)
   - Watermark progress per consumer
   - Session statistics (active, closed, avg duration)
   - Late message counts and percentages
   - Trigger fire counts
   
6. **Documentation**
   - Update API.md with complete window API
   - Add streaming section to README.md
   - Performance tuning guide
   - Window type selection guide
   - Troubleshooting common issues

**Why Phase 3?**
- Features are complete, now optimize
- Need real usage to find bottlenecks
- Production concerns after functionality proven
- Iterate based on load testing results

---

## Phase Summary

| Phase | What You Get | Ready for Production? |
|-------|--------------|----------------------|
| **Phase 1** | All 6 core windows (tumbling/session/sliding × count/time)<br>+ Watermarks + Multi-partition | ✅ **YES - Full stream processor** |
| **Phase 2** | Custom boundaries + Global windows + Triggers<br>Advanced use cases | ✅ **YES - Advanced features** |
| **Phase 3** | Optimization + Monitoring + Background jobs<br>Production polish | ✅ **YES - Scale to high loads** |

### What Each Phase Delivers

**Phase 1 (Core Windows)**: 
- Tumbling Count & Time
- Session Windows  
- Sliding Count & Time
- Watermarks (late arrivals)
- Multi-partition strategies
- Complete client API
- **→ Ship this!** Full stream processing capability

**Phase 2 (Advanced Types)**:
- Custom business boundaries
- Global unbounded windows
- Window triggers (early firing)
- **→ Specialized use cases**

**Phase 3 (Production)**:
- Performance optimization
- Monitoring & metrics
- Background jobs
- Smart cleanup
- **→ Scale to millions of events**

**Key Insight**: Phase 1 is not an MVP - it's a **complete, production-ready stream processor**. Phases 2-3 add specialized features and optimization, but Phase 1 is already powerful enough for most real-world streaming use cases.

---

## Edge Cases & Special Handling

### 1. Empty Windows
- Time windows with no messages: Return `{messages: [], complete: true}` if `allowIncomplete: true`
- Otherwise return `null` and wait

### 2. Lease Expiration
- If window lease expires before ack, return to available state
- Another consumer can pick it up
- Implement lease renewal for long processing

### 3. Failed Windows
- On `ack(window, 'failed')`, reset window state
- Window can be retried (re-delivered)
- Track retry count

### 4. Sliding Window Retention
- Don't delete messages until all sliding windows consuming them are acked
- Track per-message: which window consumers need it
- Delete when `min(all consumers' last_processed) > message.sequence`

### 5. Session Timeout
- Background job checks for inactive sessions every N seconds
- Close sessions where `NOW() - last_event_at > gap`
- Make them available for consumption

### 6. Watermark Calculation
- For time windows: `watermark = window_end_time + max_lateness`
- Query messages where `created_at <= watermark`
- Mark messages as on-time or late based on `created_at` vs `window_end_time`

### 7. Trigger Coordination
- Multiple triggers can fire the same window
- Track last fire time/count per trigger
- `accumulationMode` controls whether messages are duplicated or only new ones

### 8. Partition Strategies
- **Separate**: Track window state per partition (default)
- **Merged**: Track window state at queue level, query across all partitions
- Ensure sequence ordering works correctly for merged

### 9. Custom Boundary Parsing
- Parse time strings with timezone support
- Calculate next boundary from current time
- Handle DST transitions

### 10. Global Window Snapshots
- Track snapshot count and timing
- Manual trigger: `client.triggerWindow('queue@group')`
- Automatic: fire based on snapshot config

---

## Performance Considerations

1. **Indexing**:
   - `messages.sequence` (already exists)
   - `messages.created_at` for time-based queries
   - `window_consumers` lookup index
   - `window_state` lease and consumer indexes

2. **Query Optimization**:
   - Use `LIMIT` for count windows
   - Use time ranges for time windows
   - Avoid full table scans

3. **Polling vs Push**:
   - Start with polling (simpler)
   - Client suggests poll interval based on window type
   - Future: Consider server-side notifications

4. **Window State Cleanup**:
   - Periodically delete old acked `window_state` entries
   - Keep for audit trail or delete after retention period

5. **Large Windows**:
   - Consider pagination for windows with 1000+ messages
   - Or: Use triggers to process incrementally

6. **Session Tracking**:
   - Background job to check for timed-out sessions
   - Run every 1-5 seconds
   - Index on `last_event_at` for quick lookups

7. **Watermark Tracking**:
   - Index on `watermark_time` for quick "ready to close" checks
   - Background job or check on each `takeWindow()` call

---

## Summary

Queen now supports **complete stream processing** with all major window types:

✅ **4 basic windows**: Tumbling/Sliding × Time/Count  
✅ **Session windows**: Inactivity-based grouping  
✅ **Custom boundaries**: Business logic alignment  
✅ **Global windows**: Unbounded streams  
✅ **Watermarks**: Late arrival handling  
✅ **Triggers**: Early window firing  
✅ **Multi-partition**: Separate or merged strategies  

All built on:
- PostgreSQL (simple, reliable)
- Consumer groups (natural isolation)
- Flexible JSONB config (extensible)
- SQL queries (powerful, maintainable)

Queen becomes a **full-featured stream processor** with operational simplicity that Kafka Streams and Flink can't match. 🚀
