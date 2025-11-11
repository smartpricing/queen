# Long Polling in Queen MQ

## Overview

Long polling is a technique that allows clients to wait server-side for messages to become available, rather than repeatedly polling the server. Queen MQ implements an efficient long-polling mechanism that provides low-latency message delivery while minimizing resource usage.

## How It Works

### Traditional Polling vs Long Polling

**Traditional Polling (inefficient):**
```
Client: "Any messages?" → Server: "No"  (100ms later)
Client: "Any messages?" → Server: "No"  (100ms later)
Client: "Any messages?" → Server: "No"  (100ms later)
Client: "Any messages?" → Server: "Yes, here they are"

Problems:
- High network traffic
- High CPU usage (both client and server)
- Increased latency
- Resource waste
```

**Long Polling (efficient):**
```
Client: "Any messages? I'll wait up to 30 seconds"
  ↓
Server: Holds request open
  ↓
(waits... waits... message arrives)
  ↓
Server: "Yes, here they are" (after 5 seconds)

Benefits:
- Low network traffic (one request)
- Low CPU usage (server waits efficiently)
- Lower latency (immediate delivery)
- Resource efficient
```

## Architecture

### Components

```
┌─────────────────────────────────────────────────────────────┐
│                    CLIENT                                    │
│  Sends POP request with wait=true&timeout=30000              │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                 WORKER THREAD (uWebSockets)                  │
│  1. Registers PollIntention in Registry                      │
│  2. Returns immediately (doesn't block event loop)           │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│              POLL INTENTION REGISTRY                         │
│  Stores: request_id, queue, partition, consumer_group,      │
│          batch_size, deadline, worker_id                     │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│          POLL WORKERS (2-50 dedicated threads)               │
│  1. Wake every 50ms                                          │
│  2. Group intentions by (queue, partition, group)            │
│  3. Execute POP queries directly (one per client)            │
│  4. Each client gets own lease and messages                  │
│  5. Distribute results to waiting clients                    │
│                                                              │
│  ✅ SEMANTIC GUARANTEE:                                      │
│     One intention = One pop = One lease                      │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│              RESPONSE QUEUE (per worker)                     │
│  Delivers responses back to worker threads                   │
│  Worker sends HTTP response to client                        │
└─────────────────────────────────────────────────────────────┘
```

### Flow Diagram

```
Step 1: Client Request
┌─────────┐
│ Client  │ ─── GET /api/v1/pop/queue/events?wait=true&timeout=30000
└─────────┘
     ↓
Step 2: Worker Registration
┌─────────┐
│ Worker  │ ─── Creates PollIntention
│ Thread  │ ─── Stores in Registry
└─────────┘ ─── Returns (thread free!)
     ↓
Step 3: Poll Worker Loop (every 50ms)
┌──────────┐
│ Poll     │ ─── Fetch all intentions from registry
│ Worker   │ ─── Group by (queue, partition, consumer_group)
└──────────┘ ─── Execute POP query for each group
     ↓
Step 4: Query Database
┌──────────┐
│ AsyncDB  │ ─── Non-blocking query execution
│ Pool     │ ─── Socket-based waiting
└──────────┘
     ↓
Step 5: Results Distribution
┌──────────┐
│ Poll     │ ─── If messages found: distribute to waiting clients
│ Worker   │ ─── If no messages: apply backoff, check again
└──────────┘ ─── If timeout: send 204 No Content
     ↓
Step 6: Response Delivery
┌──────────┐
│Response  │ ─── Push response to worker's queue
│ Queue    │ ─── Worker thread sends HTTP response
└──────────┘ ─── Client receives messages
```

## Implementation Details

### 1. Poll Intention

A poll intention represents a waiting client request:

```cpp
struct PollIntention {
    std::string request_id;                          // Unique request identifier
    int worker_id;                                   // Which worker owns this request
    std::string queue_name;                          // Queue to poll
    std::string partition_name;                      // Specific partition (or empty for any)
    std::optional<std::string> namespace_name;       // Optional namespace filter
    std::optional<std::string> task_name;            // Optional task filter
    std::string consumer_group;                      // Consumer group (or __QUEUE_MODE__)
    int batch_size;                                  // How many messages to fetch
    std::chrono::steady_clock::time_point deadline;  // When to timeout
    std::chrono::steady_clock::time_point created_at;// When registered
};
```

### 2. Intention Registry

Thread-safe registry that stores and manages poll intentions:

```cpp
class PollIntentionRegistry {
    std::mutex mtx_;
    std::unordered_map<std::string, PollIntention> intentions_;  // request_id → intention
    std::unordered_set<std::string> in_flight_groups_;          // Groups being processed
    
public:
    void register_intention(const PollIntention& intention);
    void remove_intention(const std::string& request_id);
    std::vector<PollIntention> get_all_intentions();
    bool mark_group_in_flight(const std::string& group_key);
    void unmark_group_in_flight(const std::string& group_key);
};
```

**Grouping Key:**
```cpp
std::string grouping_key() const {
    return queue_name + "|" + partition_name + "|" + consumer_group;
}
```

### 3. Poll Worker Loop

Each poll worker runs this loop continuously in its own dedicated thread:

```cpp
void poll_worker_loop(int worker_id, int total_workers, ...) {
    std::unordered_map<std::string, GroupBackoffState> backoff_state;
    
    while (true) {
        // 1. Get all intentions from registry
        auto all_intentions = registry->get_all_intentions();
        
        // 2. Check for timeouts
        auto now = std::chrono::steady_clock::now();
        for (const auto& intention : all_intentions) {
            if (now >= intention.deadline) {
                send_timeout_response(intention);
                registry->remove_intention(intention.request_id);
            }
        }
        
        // 3. Load-balance: filter to this worker's share
        auto my_intentions = filter_by_worker(all_intentions, worker_id, total_workers);
        
        if (my_intentions.empty()) {
            sleep(poll_worker_interval_ms);
            continue;
        }
        
        // 4. Group intentions by (queue, partition, consumer_group)
        auto grouped = group_intentions(my_intentions);
        
        // 5. Process each group
        for (const auto& [group_key, intentions] : grouped) {
            // Check backoff state
            auto& backoff = backoff_state[group_key];
            if (should_skip_due_to_backoff(backoff)) {
                continue;
            }
            
            // Try to mark group as in-flight (prevents duplicate work)
            if (!registry->mark_group_in_flight(group_key)) {
                continue;  // Another worker is handling this
            }
            
            // ✅ SEMANTIC GUARANTEE: Process each intention independently
            // Each client gets its own pop operation with its own lease
            for (const auto& intention : intentions) {
                PopOptions opts;
                opts.batch = intention.batch_size;  // Use THIS client's batch size
                
                // Execute POP query for THIS client
                auto result = execute_pop_query(intention, opts);
                
                if (!result.messages.empty()) {
                    // Send to THIS client only
                    send_to_single_client(intention, result);
                    registry->remove_intention(intention.request_id);
                    backoff.consecutive_empty_pops = 0;
                    backoff.current_interval_ms = base_interval;
                } else {
                    // No messages - increment backoff
                    backoff.consecutive_empty_pops++;
                    if (backoff.consecutive_empty_pops >= backoff_threshold) {
                        backoff.current_interval_ms = std::min(
                            backoff.current_interval_ms * backoff_multiplier,
                            max_poll_interval_ms
                        );
                    }
                }
            }
            
            registry->unmark_group_in_flight(group_key);
        }
        
        sleep(poll_worker_interval_ms);
    }
}
```

**Key Changes in Architecture:**
- ✅ Each intention gets **its own pop operation** with **its own lease**
- ✅ No sharing of leases across HTTP connections
- ✅ Proper semantic guarantees for both queue and bus modes
- ✅ Natural partition distribution across clients
- ✅ Scales via worker count (not ThreadPool indirection)

### 4. Exponential Backoff

To prevent excessive database load when queues are empty:

```cpp
struct GroupBackoffState {
    int consecutive_empty_pops = 0;
    int current_interval_ms = 100;  // Start at 100ms
    std::chrono::steady_clock::time_point last_accessed;
};

// Backoff progression:
// Empty check 1-4: 100ms interval
// Empty check 5: 200ms interval
// Empty check 6: 400ms interval
// Empty check 7: 800ms interval
// Empty check 8: 1600ms interval
// Empty check 9+: 2000ms interval (max)
```

**Benefits:**
- Reduces database load on empty queues
- Per-group backoff (doesn't affect other queues)
- Automatic reset when messages arrive
- Configurable thresholds

### 5. Response Queue

Each worker has a response queue for receiving results from poll workers:

```cpp
class ResponseQueue {
    std::mutex mtx_;
    std::condition_variable cv_;
    std::unordered_map<std::string, ResponseEntry> pending_responses_;
    
public:
    void push(const std::string& request_id, 
              const nlohmann::json& data, 
              bool is_error, 
              int status_code);
    
    bool try_send_response(const std::string& request_id);
};
```

**Flow:**
```
Poll Worker finds messages
  ↓
Poll Worker calls worker_response_queues[intention.worker_id]->push(...)
  ↓
Response queued for correct worker
  ↓
Worker thread picks up response
  ↓
Worker sends HTTP response to client
```

## Usage Examples

### Basic Long Polling

```bash
# Wait up to 30 seconds for messages
curl "http://localhost:6632/api/v1/pop/queue/events?wait=true&timeout=30000&batch=10"
```

**What happens:**
1. Request received by worker thread
2. Worker registers poll intention
3. Worker returns immediately
4. Poll worker checks every 50-100ms
5. When messages arrive OR timeout reached, response sent
6. Client receives response

### JavaScript Client

```javascript
// Automatically uses long polling
await queen
  .queue('events')
  .group('processors')
  .batch(10)
  .consume(async (message) => {
    // Process message
  })

// Under the hood, client sends:
// GET /api/v1/pop/queue/events?wait=true&timeout=30000&batch=10&consumerGroup=processors
```

### Consumer Group with Long Polling

```bash
# Wait for messages from consumer group perspective
curl "http://localhost:6632/api/v1/pop/queue/orders?consumerGroup=workers&wait=true&timeout=30000&batch=5"
```

### Namespace/Task Filtering

```bash
# Wait for messages from specific namespace
curl "http://localhost:6632/api/v1/pop?namespace=billing&wait=true&timeout=30000&batch=10"
```

## Performance Characteristics

### Latency

| Scenario | Latency | Notes |
|----------|---------|-------|
| Messages immediately available | 10-50ms | No waiting |
| Messages arrive during wait | 50-150ms | Poll interval + query time |
| Timeout (no messages) | 30s | Full timeout period |

### Throughput

| Metric | Value | Configuration |
|--------|-------|---------------|
| Concurrent long-poll requests | 1000+ | Limited by registry size |
| Poll worker query rate | 10-20 queries/s | With backoff |
| Response delivery rate | 1000+ responses/s | Limited by worker threads |

### Resource Usage

| Resource | Usage | Notes |
|----------|-------|-------|
| Poll Worker Threads | 2-50 | Configurable via `POLL_WORKER_COUNT` |
| Database Connections | 0 per worker | Uses shared pool |
| Memory per Intention | ~200 bytes | Registry overhead |
| CPU (empty queues) | <1% | Due to backoff |
| CPU (active queues) | 5-15% | Query processing, scales with workers |

## Configuration

### Environment Variables

```bash
# Poll worker configuration
export POLL_WORKER_COUNT=10           # Number of poll worker threads (scale for load)

# Poll worker timing
export POLL_WORKER_INTERVAL=50        # How often to check registry (ms)
export POLL_DB_INTERVAL=100           # Min time between DB queries (ms)
export QUEUE_MAX_POLL_INTERVAL=2000   # Max backoff interval (ms)

# Backoff settings
export QUEUE_BACKOFF_THRESHOLD=1      # Empty pops before backoff (1 = immediate)
export QUEUE_BACKOFF_MULTIPLIER=2.0   # Backoff multiplier
export QUEUE_BACKOFF_CLEANUP_THRESHOLD=3600  # Cleanup after 3600s inactive
```

### Tuning Guidelines

**For high-volume queues (many active clients):**
```bash
export POLL_WORKER_COUNT=20           # More workers for parallelism
export POLL_WORKER_INTERVAL=25        # Check more frequently
export POLL_DB_INTERVAL=50            # Query more frequently
export QUEUE_BACKOFF_THRESHOLD=10     # More tolerant of empty results
```

**For low-volume queues (few clients):**
```bash
export POLL_WORKER_COUNT=5            # Fewer workers needed
export POLL_WORKER_INTERVAL=100       # Check less frequently
export POLL_DB_INTERVAL=200           # Query less frequently
export QUEUE_BACKOFF_THRESHOLD=1      # Backoff quickly (immediate)
export QUEUE_MAX_POLL_INTERVAL=5000   # Longer max backoff
```

**For many concurrent clients:**
```bash
# Scale poll workers based on active groups
# Rule of thumb: Each worker handles ~5-10 groups efficiently
export POLL_WORKER_COUNT=30           # For 150-300 active groups
```

**Scaling Guidelines:**

| Concurrent Waiting Clients | Active Groups | Recommended Workers |
|----------------------------|---------------|---------------------|
| < 20 | < 10 | 2-5 |
| 20-50 | 10-30 | 5-10 |
| 50-100 | 30-60 | 10-20 |
| 100-200 | 60-120 | 20-40 |
| 200+ | 120+ | 40-50 |

## Advanced Topics

### Load Balancing

Poll workers use hash-based load balancing:

```cpp
std::vector<PollIntention> filter_by_worker(
    const std::vector<PollIntention>& intentions,
    int worker_id,
    int total_workers
) {
    std::vector<PollIntention> result;
    for (const auto& intention : intentions) {
        size_t hash = std::hash<std::string>{}(intention.grouping_key());
        if (hash % total_workers == worker_id) {
            result.push_back(intention);
        }
    }
    return result;
}
```

**Benefits:**
- Even distribution across poll workers
- Same group always handled by same worker (cache friendly)
- No coordination overhead

### In-Flight Protection

Prevents duplicate work when multiple poll workers exist:

```cpp
// Before processing a group:
if (!registry->mark_group_in_flight(group_key)) {
    continue;  // Another worker is handling this
}

// After processing:
registry->unmark_group_in_flight(group_key);
```

### Timeout Handling

```cpp
auto now = std::chrono::steady_clock::now();
for (const auto& intention : all_intentions) {
    if (now >= intention.deadline) {
        // Send 204 No Content
        nlohmann::json empty_response;
        worker_response_queues[intention.worker_id]->push(
            intention.request_id, 
            empty_response, 
            false, 
            204
        );
        registry->remove_intention(intention.request_id);
    }
}
```

### Message Distribution

```cpp
void distribute_messages(
    const std::vector<Message>& messages,
    const std::vector<PollIntention>& intentions
) {
    // Distribute messages fairly across waiting clients
    int msg_idx = 0;
    for (const auto& intention : intentions) {
        std::vector<Message> batch;
        for (int i = 0; i < intention.batch_size && msg_idx < messages.size(); i++) {
            batch.push_back(messages[msg_idx++]);
        }
        
        if (!batch.empty()) {
            nlohmann::json response = convert_to_json(batch);
            worker_response_queues[intention.worker_id]->push(
                intention.request_id,
                response,
                false,
                200
            );
            registry->remove_intention(intention.request_id);
        }
        
        if (msg_idx >= messages.size()) break;
    }
}
```

## Best Practices

### 1. Use Appropriate Timeouts

```javascript
// Short timeout for interactive applications
await queen.queue('ui-events')
  .timeout(5000)  // 5 seconds
  .consume(...)

// Long timeout for background workers
await queen.queue('batch-jobs')
  .timeout(30000)  // 30 seconds
  .consume(...)
```

### 2. Batch Size Optimization

```javascript
// Balance between latency and throughput
await queen.queue('events')
  .batch(10)  // Good for most cases
  .consume(...)

// High throughput
await queen.queue('logs')
  .batch(100)  // Larger batches
  .consume(...)
```

### 3. Consumer Group Usage

```javascript
// Multiple consumers in same group
// Only one receives each message
await queen.queue('tasks')
  .group('workers')
  .concurrency(5)  // 5 parallel consumers
  .consume(...)
```

### 4. Error Handling

```javascript
await queen.queue('critical')
  .consume(async (msg) => {
    // Process message
  })
  .onError(async (msg, error) => {
    console.error('Long poll error:', error)
    // Will automatically retry
  })
```

## Troubleshooting

### Long Polling Not Working

**Symptoms:** Client times out immediately or doesn't wait

**Check:**
```bash
# Verify wait parameter
curl "http://localhost:6632/api/v1/pop/queue/test?wait=true&timeout=30000"

# Check server logs for poll worker activity
grep "Poll worker" /var/log/queen/server.log
```

### High CPU Usage

**Symptoms:** Poll workers consuming too much CPU

**Solution:**
```bash
# Increase backoff threshold
export POLL_BACKOFF_THRESHOLD=3
export POLL_MAX_INTERVAL=5000

# Reduce poll frequency
export POLL_WORKER_INTERVAL=100
export POLL_DB_INTERVAL=200
```

### Delayed Message Delivery

**Symptoms:** Messages take several seconds to be delivered

**Check:**
```bash
# Verify poll intervals aren't too high
echo $POLL_WORKER_INTERVAL  # Should be 25-100ms
echo $POLL_DB_INTERVAL      # Should be 50-200ms

# Check for backoff state
# Look for "backoff" in logs
```

### Timeouts Too Frequent

**Symptoms:** Clients receive 204 No Content too often

**Solutions:**
```javascript
// Increase timeout
await queen.queue('events')
  .timeout(60000)  // 60 seconds instead of 30
  .consume(...)

// Or switch to traditional polling with shorter intervals
await queen.queue('events')
  .polling(true)
  .pollInterval(1000)
  .consume(...)
```

## Comparison with Alternatives

### Long Polling vs Traditional Polling

| Aspect | Long Polling | Traditional Polling |
|--------|-------------|---------------------|
| Network Requests | 1 per message batch | Many empty requests |
| Latency | Low (50-150ms) | High (poll interval) |
| Server Load | Low | High |
| Client Complexity | Low | Medium |
| Resource Usage | Efficient | Wasteful |

### Long Polling vs WebSockets

| Aspect | Long Polling | WebSockets |
|--------|-------------|------------|
| Connection Model | Request/Response | Persistent Connection |
| Compatibility | Universal (HTTP) | Requires WebSocket support |
| Firewall Friendly | Yes | Sometimes blocked |
| Complexity | Low | Medium |
| Use Case | POP operations | Streaming (Queen uses both!) |

## Future Enhancements

- [ ] Server-Sent Events (SSE) support
- [ ] Priority-based intention processing
- [ ] Adaptive backoff based on queue patterns
- [ ] Intention batching for improved efficiency
- [ ] Metrics for backoff behavior
- [ ] Circuit breaker pattern integration

## Related Documentation

- [Architecture](ARCHITECTURE.md) - System architecture overview
- [Server Features](SERVER_FEATURES.md) - All server capabilities
- [Push Operations](PUSH.md) - Push operation details
- [ACK System](ACK.md) - Acknowledgment mechanisms
- [Streams](STREAMS.md) - Streaming with WebSockets (different from long polling)

## Summary

Long polling in Queen MQ provides:

- ✅ **Low Latency**: Messages delivered within 50-150ms of arrival
- ✅ **High Efficiency**: Minimal resource usage with exponential backoff
- ✅ **Scalability**: Supports 1000+ concurrent waiting clients
- ✅ **Reliability**: Timeout handling, error recovery
- ✅ **Flexibility**: Works with queues, partitions, consumer groups
- ✅ **Performance**: Non-blocking implementation, load balancing

It's the recommended way to consume messages in Queen MQ for real-time applications.

