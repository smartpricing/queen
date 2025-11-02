Here is the complete and final implementation plan for the Queen Streaming VV3 engine.

This plan incorporates all critical fixes, is multi-instance safe, scalable, and addresses all issues identified during the review.

-----

## 🚀 Queen Streaming V3: Final Implementation Plan

### 1\. Final SQL Schema

This complete script must be added to the `initialize_schema()` function in `server/src/managers/queue_manager.cpp`.

```sql
-- ============================================================================
-- Queen Streaming Schema V3
-- ============================================================================

-- 1. Stream Definitions
CREATE TABLE IF NOT EXISTS queen.streams (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name VARCHAR(255) UNIQUE NOT NULL,
    namespace VARCHAR(255) NOT NULL,
    
    -- TRUE = group by partition_id
    -- FALSE = process all partitions as one global stream
    partitioned BOOLEAN NOT NULL DEFAULT FALSE,

    -- Static Window Configuration
    window_type VARCHAR(50) NOT NULL, -- 'tumbling'
    
    -- For TIME windows
    window_duration_ms BIGINT, 
    
    -- For COUNT windows
    window_size_count INT,
    
    -- For SLIDING windows
    window_slide_ms BIGINT,
    window_slide_count INT,

    -- Common settings
    window_grace_period_ms BIGINT NOT NULL DEFAULT 30000,
    window_lease_timeout_ms BIGINT NOT NULL DEFAULT 30000,

    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- 2. Junction table for stream sources (many-to-many)
CREATE TABLE IF NOT EXISTS queen.stream_sources (
    stream_id UUID NOT NULL REFERENCES queen.streams(id) ON DELETE CASCADE,
    queue_id UUID NOT NULL REFERENCES queen.queues(id) ON DELETE CASCADE,
    PRIMARY KEY (stream_id, queue_id)
);

-- 3. Consumer Offsets (The "Bookmark" table)
CREATE TABLE IF NOT EXISTS queen.stream_consumer_offsets (
    stream_id UUID REFERENCES queen.streams(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) NOT NULL,
    
    -- KEY FIX: Stores partition_id::TEXT or '__GLOBAL__'
    stream_key TEXT NOT NULL,

    -- 'bookmark' for TIME-based windows
    last_acked_window_end TIMESTAMPTZ,
    -- 'bookmark' for COUNT-based windows
    last_acked_message_id UUID,

    total_windows_consumed BIGINT DEFAULT 0,
    last_consumed_at TIMESTAMPTZ,
    
    PRIMARY KEY (stream_id, consumer_group, stream_key)
);

-- 4. Active Leases (The "In-Flight" table)
CREATE TABLE IF NOT EXISTS queen.stream_leases (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    stream_id UUID REFERENCES queen.streams(id) ON DELETE CASCADE,
    consumer_group VARCHAR(255) NOT NULL,

    -- KEY FIX: Stores partition_id::TEXT or '__GLOBAL__'
    stream_key TEXT NOT NULL, 
    
    window_start TIMESTAMPTZ NOT NULL,
    window_end TIMESTAMPTZ NOT NULL,
    
    lease_id UUID NOT NULL UNIQUE,
    lease_consumer_id VARCHAR(255),
    lease_expires_at TIMESTAMPTZ NOT NULL,
    
    UNIQUE(stream_id, consumer_group, stream_key, window_start, window_end)
);

-- 5. Watermark Table (CRITICAL)
CREATE TABLE IF NOT EXISTS queen.queue_watermarks (
    queue_id UUID PRIMARY KEY REFERENCES queen.queues(id) ON DELETE CASCADE,
    queue_name VARCHAR(255) NOT NULL,
    max_created_at TIMESTAMPTZ NOT NULL DEFAULT '-infinity'::timestamptz,
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_queue_watermarks_name ON queen.queue_watermarks(queue_name);

-- 6. Watermark Trigger Function (CRITICAL)
CREATE OR REPLACE FUNCTION update_queue_watermark()
RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO queen.queue_watermarks (queue_id, queue_name, max_created_at)
    SELECT 
        q.id,
        q.name,
        NEW.created_at
    FROM queen.partitions p
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE p.id = NEW.partition_id
    ON CONFLICT (queue_id)
    DO UPDATE SET
        max_created_at = GREATEST(queue_watermarks.max_created_at, EXCLUDED.max_created_at),
        updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- 7. Watermark Trigger (CRITICAL)
DROP TRIGGER IF EXISTS trigger_update_watermark ON queen.messages;
CREATE TRIGGER trigger_update_watermark
AFTER INSERT ON queen.messages
FOR EACH ROW
EXECUTE FUNCTION update_queue_watermark();
```

-----

### 2\. Complete SQL Query List

This is the full set of prepared statements for the new `StreamManager`.

  * **Q1: Create/Update Stream**

      * **SQL:**
        ```sql
        INSERT INTO queen.streams (
            name, namespace, partitioned, window_type, 
            window_duration_ms, window_grace_period_ms, window_lease_timeout_ms
        )
        VALUES ($1, $2, $3, $4, $5, $6, $7)
        ON CONFLICT (name) 
        DO UPDATE SET
            namespace = EXCLUDED.namespace,
            partitioned = EXCLUDED.partitioned,
            window_type = EXCLUDED.window_type,
            window_duration_ms = EXCLUDED.window_duration_ms,
            window_grace_period_ms = EXCLUDED.window_grace_period_ms,
            window_lease_timeout_ms = EXCLUDED.window_lease_timeout_ms,
            updated_at = NOW()
        RETURNING id;
        ```
      * **Params:** `name`, `namespace`, `partitioned`, `window_type`, `window_duration_ms`, `window_grace_period_ms`, `window_lease_timeout_ms`

  * **Q2: Link Stream to Queues**

      * **SQL:**
        ```sql
        INSERT INTO queen.stream_sources (stream_id, queue_id)
        SELECT $1, q.id FROM queen.queues q WHERE q.name = $2
        ON CONFLICT (stream_id, queue_id) DO NOTHING;
        ```
      * **Params:** `stream_id`, `queue_name`

  * **Q3: Get Stream by Name**

      * **SQL:** `SELECT * FROM queen.streams WHERE name = $1;`
      * **Params:** `stream_name`

  * **Q4: Get Partitions and Offsets**

      * **SQL (Partitioned):**
        ```sql
        SELECT 
            p.id::TEXT as stream_key,
            p.name as partition_name,
            COALESCE(
                o.last_acked_window_end, 
                '-infinity'::timestamptz
            ) as last_acked_window_end
        FROM queen.partitions p
        JOIN queen.stream_sources ss ON p.queue_id = ss.queue_id
        LEFT JOIN queen.stream_consumer_offsets o 
            ON ss.stream_id = o.stream_id
            AND p.id::TEXT = o.stream_key
            AND o.consumer_group = $2
        WHERE ss.stream_id = $1;
        ```
      * **SQL (Global):**
        ```sql
        SELECT 
            '__GLOBAL__' as stream_key,
            '__GLOBAL__' as partition_name,
            COALESCE(
                o.last_acked_window_end, 
                '-infinity'::timestamptz
            ) as last_acked_window_end
        FROM queen.stream_consumer_offsets o
        WHERE o.stream_id = $1
          AND o.consumer_group = $2
          AND o.stream_key = '__GLOBAL__';
        ```
      * **Params:** `stream_id`, `consumer_group`

  * **Q5: Get Watermarks for Source Queues**

      * **SQL:**
        ```sql
        SELECT 
            MIN(w.max_created_at) as current_watermark
        FROM queen.queue_watermarks w
        JOIN queen.stream_sources ss ON w.queue_id = ss.queue_id
        WHERE ss.stream_id = $1;
        ```
      * **Params:** `stream_id`

  * **Q6: Check for Active Lease**

      * **SQL:**
        ```sql
        SELECT 1 FROM queen.stream_leases
        WHERE stream_id = $1
          AND consumer_group = $2
          AND stream_key = $3
          AND window_start = $4
          AND lease_expires_at > NOW()
        LIMIT 1;
        ```
      * **Params:** `stream_id`, `consumer_group`, `stream_key`, `window_start`

  * **Q7: Create Lease**

      * **SQL:**
        ```sql
        INSERT INTO queen.stream_leases (
            stream_id, consumer_group, stream_key,
            window_start, window_end, 
            lease_id, lease_expires_at
        )
        VALUES ($1, $2, $3, $4, $5, gen_random_uuid(), NOW() + $6::interval)
        RETURNING lease_id;
        ```
      * **Params:** `stream_id`, `consumer_group`, `stream_key`, `window_start`, `window_end`, `lease_interval` (e.g., '60 seconds')

  * **Q8: Get Messages for Window**

      * **SQL (Partitioned):**
        ```sql
        SELECT m.id, m.payload, m.created_at
        FROM queen.messages m
        WHERE m.partition_id = $1::UUID
          AND m.created_at >= $2
          AND m.created_at < $3
        ORDER BY m.created_at, m.id;
        ```
      * **SQL (Global):**
        ```sql
        SELECT m.id, m.payload, m.created_at
        FROM queen.messages m
        JOIN queen.partitions p ON m.partition_id = p.id
        JOIN queen.stream_sources ss ON p.queue_id = ss.queue_id
        WHERE ss.stream_id = $1
          AND m.created_at >= $2
          AND m.created_at < $3
        ORDER BY m.created_at, m.id;
        ```
      * **Params (Partitioned):** `stream_key` (as `partition_id`), `window_start`, `window_end`
      * **Params (Global):** `stream_id`, `window_start`, `window_end`

  * **Q9: Validate Lease for ACK**

      * **SQL:**
        ```sql
        SELECT stream_id, consumer_group, stream_key, window_start, window_end
        FROM queen.stream_leases
        WHERE lease_id = $1 AND lease_expires_at > NOW();
        ```
      * **Params:** `lease_id`

  * **Q10: ACK Window (Update Offset)**

      * **SQL:**
        ```sql
        INSERT INTO queen.stream_consumer_offsets (
            stream_id, consumer_group, stream_key,
            last_acked_window_end, 
            total_windows_consumed, last_consumed_at
        )
        VALUES ($1, $2, $3, $4, 1, NOW())
        ON CONFLICT (stream_id, consumer_group, stream_key)
        DO UPDATE SET
            last_acked_window_end = EXCLUDED.last_acked_window_end,
            total_windows_consumed = stream_consumer_offsets.total_windows_consumed + 1,
            last_consumed_at = NOW();
        ```
      * **Params:** `stream_id`, `consumer_group`, `stream_key`, `window_end`

  * **Q11: Delete Lease**

      * **SQL:** `DELETE FROM queen.stream_leases WHERE lease_id = $1;`
      * **Params:** `lease_id`

  * **Q12: Renew Lease**

      * **SQL:**
        ```sql
        UPDATE queen.stream_leases
        SET lease_expires_at = NOW() + $2::interval
        WHERE lease_id = $1 AND lease_expires_at > NOW()
        RETURNING lease_expires_at;
        ```
      * **Params:** `lease_id`, `extend_interval` (e.g., '30 seconds')

  * **Q13: Seek Offset**

      * **SQL:**
        ```sql
        INSERT INTO queen.stream_consumer_offsets (
            stream_id, consumer_group, stream_key,
            last_acked_window_end
        )
        SELECT 
            ss.stream_id, $2, p.id::TEXT, $3
        FROM queen.stream_sources ss
        JOIN queen.partitions p ON ss.queue_id = p.queue_id
        WHERE ss.stream_id = $1
        UNION
        SELECT $1, $2, '__GLOBAL__', $3

        ON CONFLICT (stream_id, consumer_group, stream_key)
        DO UPDATE SET
            last_acked_window_end = EXCLUDED.last_acked_window_end,
            total_windows_consumed = 0,
            last_consumed_at = NOW();
        ```
      * **Params:** `stream_id`, `consumer_group`, `seek_timestamp`

  * **Q14: Background Lease Cleanup**

      * **SQL:** `DELETE FROM queen.stream_leases WHERE lease_expires_at < NOW();`
      * **Params:** None

  * **Q15: Get First Message Time (Bootstrap)**

      * **SQL (Partitioned):**
        ```sql
        SELECT MIN(m.created_at) as first_time 
        FROM queen.messages m 
        WHERE m.partition_id = $1::UUID;
        ```
      * **SQL (Global):**
        ```sql
        SELECT MIN(m.created_at) as first_time
        FROM queen.messages m
        JOIN queen.partitions p ON m.partition_id = p.id
        JOIN queen.stream_sources ss ON p.queue_id = ss.queue_id
        WHERE ss.stream_id = $1;
        ```
      * **Params (Partitioned):** `stream_key` (as `partition_id`)
      * **Params (Global):** `stream_id`

-----

### 3\. Server-Side Implementation (C++)

#### `main_acceptor.cpp`

A new `StreamManager` must be instantiated and wired up, sharing the existing `global_db_pool`, the shared `global_db_thread_pool`, and the per-worker response queues while maintaining its own dedicated stream poll registry.

```cpp
// In main_acceptor.cpp
#include "managers/stream_manager.hpp" // New file
#include "queen/stream_poll_intention_registry.hpp" // New file

int main() {
    // ... (existing setup: global_db_pool, global_db_thread_pool, worker_response_queues, etc.) ...
    
    static std::shared_ptr<queen::StreamPollIntentionRegistry> global_stream_poll_registry;
    std::call_once(global_stream_registry_init_flag, [&]() {
        global_stream_poll_registry = std::make_shared<queen::StreamPollIntentionRegistry>();
    });

    // NEW: Instantiate StreamManager with existing components
    auto stream_manager = std::make_shared<StreamManager>(
        global_db_pool,
        global_db_thread_pool,
        worker_response_queues,
        global_stream_poll_registry
    );
    
    // ... (existing route setup) ...

    // --- NEW STREAMING ROUTES ---
    app.post("/api/v1/stream/define", [stream_manager](auto *res, auto *req) {
        stream_manager->handle_define(res, req);
    });
    
    app.post("/api/v1/stream/poll", [stream_manager](auto *res, auto *req) {
        // Registers a long-poll intention with the dedicated registry
        stream_manager->handle_poll(res, req);
    });
    
    app.post("/api/v1/stream/ack", [stream_manager](auto *res, auto *req) {
        stream_manager->handle_ack(res, req);
    });
    
    app.post("/api/v1/stream/renew-lease", [stream_manager](auto *res, auto *req) {
        stream_manager->handle_renew(res, req);
    });
    
    app.post("/api/v1/stream/seek", [stream_manager](auto *res, auto *req) {
        stream_manager->handle_seek(res, req);
    });
    
    // ... (rest of main) ...
}
```

> **Note:** Declare `std::once_flag global_stream_registry_init_flag;` alongside the other global flags near the top of `acceptor_server.cpp`.

#### `StreamManager.cpp` (New Class)

This class will manage all streaming logic, use `ScopedConnection` for all DB calls, and implement the poll worker loop. Each HTTP handler (`handle_define`, `handle_poll`, `handle_ack`, `handle_renew`, `handle_seek`) must enqueue its work onto the shared `global_db_thread_pool` before touching the database so streaming requests respect the same back-pressure and instrumentation as queue operations. For example:

```cpp
void StreamManager::handle_define(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    read_json_body(res,
        [this, res](const nlohmann::json& body) {
            db_thread_pool_->enqueue([this, res, body]() {
                ScopedConnection conn(db_pool_.get());
                DefineStream(conn.get(), res, body); // Executes Q1/Q2 inside transactions
            });
        },
        [res](const std::string& error) {
            send_error_response(res, error, 400);
        });
}
```

`handle_poll` follows the same pattern: parse the JSON, register the intention with `stream_intention_registry_`, then enqueue a task on `db_thread_pool_` that either finds a ready window immediately (calling `FindAndDeliverWindow`) or leaves the intention parked for the background poll workers.

The manager owns a dedicated `StreamPollIntentionRegistry`, mirroring the queue poll registry but isolated to streaming workloads. Its constructor signature should be:

```cpp
StreamManager(std::shared_ptr<DatabasePool> db_pool,
              std::shared_ptr<astp::ThreadPool> db_thread_pool,
              const std::vector<std::shared_ptr<ResponseQueue>>& worker_response_queues,
              std::shared_ptr<StreamPollIntentionRegistry> intention_registry);
```

It stores the `worker_response_queues` reference to route responses back to the worker that accepted the HTTP request, and a pointer to the stream poll registry so it can register intentions and coordinate poll workers.

Create the new registry in `server/include/queen/stream_poll_intention_registry.hpp` and `server/src/managers/stream_poll_intention_registry.cpp` (or equivalent) by copying the semantics of `PollIntentionRegistry` but keyed by `{streamName, consumerGroup}` and including the owning worker ID. `StreamPollIntention` must capture:

```cpp
struct StreamPollIntention {
    std::string request_id;
    int worker_id;
    std::string stream_name;
    std::string consumer_group;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point created_at;
};
```

The registry API mirrors the queue version (`register_intention`, `remove_intention`, `cleanup_expired`, `mark_group_in_flight`, etc.), but grouping keys are simply `stream_name + ":" + consumer_group`.

**Poll Worker C++ Pseudocode (Fixing `-infinity`):**

```cpp
// This logic runs inside the StreamManager's poll worker
void StreamManager::FindAndDeliverWindow(StreamPollIntention intention) {
    ScopedConnection conn(db_pool_);
    
    // 1. Get Stream, Watermark, and Partitions/Offsets
    auto stream = GetStream(conn, intention.stream_name); // Q3
    auto watermark = GetWatermark(conn, stream.id); // Q5
    auto partitions = GetPartitionsAndOffsets(conn, stream.id, intention.consumer_group); // Q4

    for (const auto& partition : partitions) {
        auto stream_key = partition["stream_key"]; // This is partition_id or '__GLOBAL__'
        auto partition_name = partition["partition_name"]; // This is for the client JSON
        auto last_end_str = partition["last_acked_window_end"];
        
        auto duration = stream.window_duration_ms;
        auto grace = stream.window_grace_period_ms;
        
        time_point window_start;

        // --- FIX for -infinity ---
        if (last_end_str == "-infinity") {
            // Bootstrap: Get the first message time for this key
            auto first_msg_time = GetFirstMessageTime(conn, stream.id, stream_key, stream.partitioned); // Q15
            
            if (!first_msg_time.has_value()) {
                continue; // No messages in this partition yet
            }
            
            // Align the time to a tumbling window boundary
            window_start = AlignToBoundary(first_msg_time.value(), duration);
        } else {
            // Normal case: Start at the end of the last window
            window_start = ParseTimestamp(last_end_str);
        }
        // --- END FIX ---
        
        auto window_end = window_start + std::chrono::milliseconds(duration);
        
        // Check 1: Is window ready? (Watermark has passed window + grace)
        if (watermark < (window_end + std::chrono::milliseconds(grace))) {
            continue; // Not ready
        }
        
        // Check 2: Is it already leased?
        if (CheckLeaseExists(conn, stream.id, intention.consumer_group, stream_key, window_start)) { // Q6
            continue; // Leased by another consumer/server
        }
        
        // --- FOUND WORK ---
        // 1. Create Lease
        auto lease_id = CreateLease(conn, stream.id, intention.consumer_group, stream_key, window_start, window_end, stream.lease_timeout); // Q7
        
        // 2. Get Messages
        auto messages = GetMessages(conn, stream.id, stream_key, stream.partitioned, window_start, window_end); // Q8
        
        // 3. Build JSON Response
        nlohmann::json window_json = {
            {"id", GenerateWindowID(stream.id, partition_name, window_start, window_end)},
            {"leaseId", lease_id},
            {"key", partition_name}, // The human-readable partition name
            {"start", FormatTimestamp(window_start)},
            {"end", FormatTimestamp(window_end)},
            {"messages", messages}
        };
        
        // 4. Respond to Client using the originating worker queue
        auto &response_queue = worker_response_queues_[intention.worker_id];
        response_queue->push(intention.request_id, {{"window", window_json}}, 200);
        return; // Work is done
    }
    
    // No window was found for any partition
    // The poll intention will remain and be re-checked later
}
```

`FindAndDeliverWindow` runs inside jobs scheduled onto `db_thread_pool_` (e.g., in `handle_poll` after registering an intention). The poll worker loop should never execute on the uWS thread; always enqueue the database work.

-----

### 4\. Client-Side Implementation (Net-New)

The following files must be **created new** in the `client-js/client-v2/stream/` directory.

#### `client-js/client-v2/Queen.js` (Modifications)

```javascript
// ... (existing class)
import { StreamBuilder } from './stream/StreamBuilder';
import { StreamConsumer } from './stream/StreamConsumer';

class Queen {
  // ... (existing methods)

  stream(name, namespace) {
    return new StreamBuilder(this.#httpClient, this, name, namespace);
  }

  consumer(streamName, consumerGroup) {
    return new StreamConsumer(this.#httpClient, this, streamName, consumerGroup);
  }
}
```

#### `client-js/client-v2/stream/StreamBuilder.js` (New File)

```javascript
export class StreamBuilder {
  constructor(httpClient, queen, name, namespace) {
    this.httpClient = httpClient;
    this.queen = queen;
    this.config = {
      name,
      namespace,
      source_queue_names: [],
      partitioned: false,
      window_type: 'tumbling',
      window_duration_ms: 60000,
      window_grace_period_ms: 30000,
      window_lease_timeout_ms: 60000
    };
  }

  sources(queueNames = []) {
    this.config.source_queue_names = queueNames;
    return this;
  }
  
  partitioned() {
    this.config.partitioned = true;
    return this;
  }

  tumblingTime(seconds) {
    this.config.window_type = 'tumbling';
    this.config.window_duration_ms = seconds * 1000;
    return this;
  }
  
  gracePeriod(seconds) {
    this.config.window_grace_period_ms = seconds * 1000;
    return this;
  }
  
  leaseTimeout(seconds) {
    this.config.window_lease_timeout_ms = seconds * 1000;
    return this;
  }

  async define() {
    return this.httpClient.post('/api/v1/stream/define', this.config);
  }
}
```

#### `client-js/client-v2/stream/StreamConsumer.js` (New File)

```javascript
import { Window } from './Window';

export class StreamConsumer {
  constructor(httpClient, queen, streamName, consumerGroup) {
    this.httpClient = httpClient;
    this.queen = queen;
    this.streamName = streamName;
    this.consumerGroup = consumerGroup;
    this.pollTimeout = 30000; // 30s long poll
    this.leaseRenewInterval = 20000; // 20s renewal
  }

  async process(callback) {
    while (true) {
      let window = null;
      try {
        window = await this.pollWindow();

        if (!window) { // 204 No Content
          continue;
        }

        await this.executeCallback(window, callback);

      } catch (err) {
        console.error('Stream processing error:', err);
        await new Promise(r => setTimeout(r, 1000)); // Backoff
      }
    }
  }

  async pollWindow() {
    const response = await this.httpClient.post('/api/v1/stream/poll', {
      streamName: this.streamName,
      consumerGroup: this.consumerGroup,
      timeout: this.pollTimeout
    });
    
    if (response.status === 204) return null;
    return new Window(response.data.window);
  }

  async executeCallback(window, callback) {
    let leaseTimer = null;
    let leaseExpired = false;
    
    try {
      leaseTimer = setInterval(async () => {
         try {
           await this.httpClient.post('/api/v1/stream/renew-lease', { 
             leaseId: window.leaseId,
             extend_ms: this.leaseRenewInterval + 10000
           });
         } catch (e) {
           leaseExpired = true;
           console.warn(`Lease ${window.leaseId} expired or failed to renew.`);
         }
      }, this.leaseRenewInterval);

      await callback(window);
      
      if (!leaseExpired) {
        await this.httpClient.post('/api/v1/stream/ack', {
          windowId: window.id,
          leaseId: window.leaseId,
          success: true
        });
      }

    } catch (err) {
      console.error(`Failed to process window ${window.id}:`, err);
      if (!leaseExpired) {
        await this.httpClient.post('/api/v1/stream/ack', {
          windowId: window.id,
          leaseId: window.leaseId,
          success: false // NACK
        });
      }
    } finally {
      if (leaseTimer) clearInterval(leaseTimer);
    }
  }
}
```

#### `client-js/client-v2/stream/Window.js` (New File)

```javascript
const getPath = (obj, path) => 
  path.split('.').reduce((o, k) => (o && o[k] !== undefined ? o[k] : null), obj);

export class Window {
  constructor(rawWindow) {
    Object.assign(this, rawWindow);
    this.allMessages = Object.freeze(rawWindow.messages || []);
    this.messages = [...this.allMessages];
  }

  filter(filterFn) {
    this.messages = this.messages.filter(filterFn);
    return this;
  }

  groupBy(keyPath) {
    const groups = {};
    for (const msg of this.messages) {
      const key = getPath(msg, keyPath) || 'null_key';
      if (!groups[key]) {
        groups[key] = [];
      }
      groups[key].push(msg);
    }
    return groups;
  }

  aggregate(config = {}) {
    const results = {};

    if (config.count) {
      results.count = this.messages.length;
    }

    if (config.sum) {
      results.sum = {};
      for (const path of config.sum) {
        results.sum[path] = this.messages.reduce((total, msg) => {
          const val = getPath(msg, path);
          return total + (typeof val === 'number' ? val : 0);
        }, 0);
      }
    }
    // (Add .avg, .min, .max logic here...)
    return results;
  }
}
```

-----

### 5\. Final Usage Example

**Step 1: Define Stream (Admin)**

```javascript
// define-stream.js
import { Queen } from './queen-client';
const queen = new Queen({ host: '...' });

await queen.stream('user-activity', 'production')
  .sources(['user-clicks', 'user-purchases'])
  .partitioned() // Process by partition_id
  .tumblingTime(300) // 5-minute windows
  .define();
```

**Step 2: Run Consumer (Worker)**

```javascript
// consumer.js
import { Queen } from './queen-client';
const queen = new Queen({ host: '...' });

const consumer = queen.consumer('user-activity', 'analytics-group');

consumer.process(async (window) => {
  // 'window.key' is now the partition_name, e.g., "partition-A"
  console.log(`Processing window for key: ${window.key} (${window.allMessages.length} raw messages)`);

  // Client-side processing
  const stats = window
    .filter(msg => msg.payload.type === 'click')
    .aggregate({ count: true });

  console.log(`Found ${stats.count} clicks.`);
  await sendToAnalytics(window.key, window.start, stats);
});
```