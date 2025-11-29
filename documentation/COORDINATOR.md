# Coordinator Pattern Implementation

## Overview

Each Queen server acts as a coordinator that can route requests to the optimal server based on load and affinity. Clients become dumb (round-robin or K8s Service). Local requests have zero overhead; only imbalanced requests are forwarded.

## Architecture

```
Client → Any Queen → Coordinator Decision → Local or Forward
```

## Components

### 1. ClusterLoadTracker

**File:** `server/include/queen/cluster_load_tracker.hpp`

Tracks load metrics from all servers via UDPSYNC heartbeats.

```cpp
enum class RouteType { CONSUMER, PRODUCER };

class ClusterLoadTracker {
    struct ServerLoad {
        std::string server_id;
        std::string url;
        int poll_intentions;
        int outstanding_requests;
        double cpu_percent;
        int64_t last_seen_ms;
        bool healthy;
        int64_t circuit_open_until_ms;  // Circuit breaker
    };
    
    std::string local_server_id_;
    std::unordered_map<std::string, ServerLoad> loads_;
    std::unordered_map<std::string, std::string> affinity_cache_;  // key → server_id
    std::shared_ptr<ConsumerPresence> consumer_presence_;  // From SharedStateManager
    
public:
    void update_peer_load(const std::string& server_id, const nlohmann::json& load);
    void update_local_load();
    
    // Consumer routes: load-based + affinity cache
    bool should_handle_locally(const std::string& affinity_key);
    std::string get_best_server_for_consumer(const std::string& affinity_key);
    
    // Producer routes: route to server with consumers
    std::string get_best_server_for_producer(const std::string& queue);
    
    void mark_unhealthy(const std::string& server_id);
    void mark_healthy(const std::string& server_id);
    nlohmann::json get_balance_info();
};
```

**Key methods:**

Consumer routing:
- `should_handle_locally()`: Returns true if local server is best or within 10% of best
- `get_best_server_for_consumer()`: Lowest load score + affinity cache
- Load score: `0.4 * (outstanding/100) + 0.3 * (poll_intentions/200) + 0.3 * (cpu/100)`

Producer routing:
- `get_best_server_for_producer(queue)`: Returns server with active consumers for queue
- If local server has consumers → handle locally (instant notify)
- If no consumers anywhere → handle locally (no benefit to forward)
- Uses `ConsumerPresence` from existing UDPSYNC infrastructure

### 2. Extend UDPSYNC Heartbeat

**File:** `server/src/services/shared_state_manager.cpp`

Add load metrics to existing heartbeat payload.

```cpp
// In send_heartbeat()
nlohmann::json payload = {
    {"session_id", session_id_},
    {"sequence", sequence++},
    {"load", {
        {"poll_intentions", poll_intention_registry_->size()},
        {"outstanding", outstanding_requests_.load()},
        {"cpu", metrics_collector_->get_cpu_percent()}
    }}
};
```

**File:** `server/src/services/shared_state_manager.cpp`

Handle incoming load data.

```cpp
// In handle_heartbeat()
if (payload.contains("load") && cluster_load_tracker_) {
    cluster_load_tracker_->update_peer_load(sender, payload["load"]);
}
```

### 3. RequestForwarder

**File:** `server/include/queen/request_forwarder.hpp`

HTTP client using `select()` for socket polling. **Runs directly in uWS worker thread** (same pattern as async DB - no thread pool needed).

```cpp
class RequestForwarder {
    std::shared_ptr<ClusterLoadTracker> load_tracker_;
    std::string local_server_id_;
    
public:
    // Called directly from route handler (like async_queue_manager)
    ForwardResult forward_request(
        const std::string& target_url,
        const std::string& method,
        const std::string& path,
        const std::map<std::string, std::string>& headers,
        const std::optional<std::string>& body
    );
};
```

**Same pattern as async_database.cpp - runs in uWS thread:**

```cpp
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

// Same as async_database.cpp
void waitForSocket(int sock_fd, bool for_reading, int timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock_fd, &fds);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    int ret;
    if (for_reading) {
        ret = select(sock_fd + 1, &fds, nullptr, nullptr, &tv);
    } else {
        ret = select(sock_fd + 1, nullptr, &fds, nullptr, &tv);
    }
    
    if (ret < 0) throw std::runtime_error("select() failed");
    if (ret == 0) throw std::runtime_error("timeout");
}

ForwardResult RequestForwarder::forward_request(...) {
    // 1. Create non-blocking socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(sock, F_SETFL, O_NONBLOCK);
    
    // 2. Async connect (returns EINPROGRESS)
    connect(sock, addr, addrlen);
    waitForSocket(sock, false, connect_timeout_ms);  // Wait writable
    
    // 3. Send request
    while (sent < request.size()) {
        waitForSocket(sock, false, timeout_ms);
        sent += send(sock, ...);
    }
    
    // 4. Receive response
    while (!complete) {
        waitForSocket(sock, true, timeout_ms);
        recv(sock, buf, ...);
    }
    
    close(sock);
    return parse_response();
}
```

**Why this works (same as Postgres):**
- `select()` is OS-level efficient waiting (not CPU blocking)
- Peer servers respond in ~1-5ms (same datacenter)
- Pattern identical to `push_messages()` which runs in uWS thread

**Route handler usage:**
```cpp
// Runs directly in uWS worker thread - same as async DB calls
auto result = ctx.request_forwarder->forward_request(target, "POST", path, headers, body);
if (result.success) {
    send_response(res, result.status, result.body);
} else {
    local_handler();  // Fallback
}
```

**Retry logic:**
1. Try preferred server (1s connect timeout, 5s total timeout)
2. On failure: mark unhealthy, try next-best server
3. On second failure: execute local_handler as fallback
4. Add headers: `X-Served-By`, `X-Forwarded-By`, `X-Fallback`

**Dependencies:** libcurl (already in vendor/)

### 4. Coordinator Middleware

**File:** `server/src/routes/pop.cpp` (and other consumer routes)

Wrap existing handlers with coordinator logic.

```cpp
// Consumer route (load-based)
void setup_pop_routes(uWS::App* app, const RouteContext& ctx) {
    app->get("/api/v1/pop/queue/:queue", [ctx](auto* res, auto* req) {
        std::string queue = std::string(req->getParameter(0));
        std::string group = get_query_param(req, "group").value_or("__QUEUE_MODE__");
        std::string affinity_key = queue + ":" + group;
        
        if (!ctx.coordinator_enabled || 
            ctx.cluster_load_tracker->should_handle_locally(affinity_key)) {
            handle_pop(res, req, ctx);
        } else {
            std::string target = ctx.cluster_load_tracker->get_best_server_for_consumer(affinity_key);
            ctx.request_forwarder->forward_with_fallback(res, req, target, 
                [=]() { handle_pop(res, req, ctx); });
        }
    });
}

// Producer route (consumer-presence based)
void setup_push_routes(uWS::App* app, const RouteContext& ctx) {
    app->post("/api/v1/push", [ctx](auto* res, auto* req) {
        // Parse queue from body (need to buffer body first)
        read_json_body(res, [=](const nlohmann::json& body) {
            std::string queue = body.value("queue", "");
            std::string target = ctx.cluster_load_tracker->get_best_server_for_producer(queue);
            
            if (!ctx.coordinator_enabled || target == ctx.local_server_id) {
                handle_push(res, body, ctx);
            } else {
                ctx.request_forwarder->forward_with_fallback(res, req, target,
                    [=]() { handle_push(res, body, ctx); });
            }
        });
    });
}
```

### 5. Balance Endpoint

**File:** `server/src/routes/internal.cpp`

```cpp
app->get("/api/v1/cluster/balance", [ctx](auto* res, auto* req) {
    send_json_response(res, ctx.cluster_load_tracker->get_balance_info());
});
```

Response format:
```json
{
  "balanced": true,
  "imbalance_score": 0.15,
  "servers": [
    {"server_id": "queen-0", "url": "http://queen-0:6632", "load_score": 0.45, "healthy": true},
    {"server_id": "queen-1", "url": "http://queen-1:6632", "load_score": 0.52, "healthy": true}
  ]
}
```

### 6. Configuration

**File:** `server/include/queen/config.hpp`

```cpp
struct CoordinatorConfig {
    bool enabled = false;                    // QUEEN_COORDINATOR_ENABLED
    double forward_threshold = 0.1;          // Only forward if peer is 10% better
    int forward_timeout_ms = 5000;           // Forward request timeout
    int connect_timeout_ms = 1000;           // TCP connect timeout
    int circuit_open_after_failures = 3;     // Failures before circuit opens
    int circuit_retry_ms = 5000;             // Initial circuit retry interval
    
    // Affinity
    int affinity_ttl_ms = 300000;            // Cache TTL (5 min)
    double affinity_migrate_threshold = 0.70; // Consider migration above 70%
    double affinity_break_threshold = 0.90;   // Force break above 90%
    double affinity_migrate_min_improvement = 0.15; // Need 15% better to migrate
    int queue_affinity_threshold = 10;       // Use queue-level affinity if > N groups
};
```

**Environment variables:**
```bash
# Core
QUEEN_COORDINATOR_ENABLED=true
QUEEN_COORDINATOR_FORWARD_THRESHOLD=0.1
QUEEN_COORDINATOR_TIMEOUT_MS=5000

# Affinity
QUEEN_AFFINITY_TTL_MS=300000
QUEEN_AFFINITY_MIGRATE_THRESHOLD=0.70
QUEEN_AFFINITY_BREAK_THRESHOLD=0.90
QUEEN_AFFINITY_MIGRATE_MIN_IMPROVEMENT=0.15
QUEEN_QUEUE_AFFINITY_THRESHOLD=10  # Queue-level affinity if > N consumer groups
```

### 7. RouteContext Extension

**File:** `server/include/queen/routes/route_context.hpp`

```cpp
struct RouteContext {
    // ... existing fields ...
    bool coordinator_enabled;
    std::shared_ptr<ClusterLoadTracker> cluster_load_tracker;
    std::shared_ptr<RequestForwarder> request_forwarder;
};
```

## File Changes Summary

| File | Change |
|------|--------|
| `include/queen/cluster_load_tracker.hpp` | **NEW** |
| `src/services/cluster_load_tracker.cpp` | **NEW** |
| `include/queen/request_forwarder.hpp` | **NEW** |
| `src/services/request_forwarder.cpp` | **NEW** |
| `include/queen/config.hpp` | Add CoordinatorConfig |
| `include/queen/routes/route_context.hpp` | Add coordinator fields |
| `src/services/shared_state_manager.cpp` | Add load to heartbeat |
| `src/routes/pop.cpp` | Add coordinator middleware |
| `src/routes/push.cpp` | Add coordinator middleware |
| `src/routes/ack.cpp` | Add coordinator middleware |
| `src/routes/transaction.cpp` | Add coordinator middleware |
| `src/routes/internal.cpp` | Add /cluster/balance |
| `src/acceptor_server.cpp` | Initialize coordinator components |
| `Makefile` | Add new source files |

## Circuit Breaker States

```
CLOSED → 3 failures → OPEN → wait 5s → HALF-OPEN → success → CLOSED
                                      → failure → OPEN (backoff 2x)
```

Max backoff: 5 minutes.

## Routes to Wrap

### Consumer Routes (Load + Affinity Based)

| Route | Affinity Key | Strategy |
|-------|--------------|----------|
| `GET /api/v1/pop/queue/:queue` | `queue:group` | Least loaded + affinity cache |
| `GET /api/v1/pop/queue/:queue/partition/:partition` | `queue:partition:group` | Least loaded + affinity cache |
| `GET /api/v1/pop` (namespace) | `namespace:task:group` | Least loaded + affinity cache |

### Producer Routes (Consumer Presence Based)

| Route | Key | Strategy |
|-------|-----|----------|
| `POST /api/v1/push` | `queue` | Route to server with consumers (instant notify) |
| `POST /api/v1/ack` | `queue` | Route to server with consumers (instant notify) |
| `POST /api/v1/ack/batch` | `queue` | Route to server with consumers |
| `POST /api/v1/transaction` | `queue` (from payload) | Route to server with consumers |

**Why route writes to consumer servers:**
- Local notification is instant (0ms) vs UDPSYNC (~0.3ms)
- Better cache hit rate (partition IDs, queue config)
- Reduces cross-server UDPSYNC traffic

**When NOT to forward writes:**
- No consumers for queue anywhere → handle locally (no benefit to forward)
- Local server has consumers → handle locally (already optimal)

## Affinity Assignment

### How Affinity is Computed

Unlike client-side consistent hashing, the coordinator assigns affinity **dynamically based on load at assignment time**:

```cpp
std::string ClusterLoadTracker::assign_affinity(const std::string& affinity_key) {
    // Check cache first
    auto it = affinity_cache_.find(affinity_key);
    if (it != affinity_cache_.end()) {
        return evaluate_existing_affinity(affinity_key, it->second);
    }
    
    // NEW affinity: assign to least loaded healthy server
    std::string best = find_least_loaded_server();
    
    affinity_cache_[affinity_key] = {
        .server_id = best,
        .assigned_at = now_ms(),
        .request_count = 0
    };
    
    return best;
}
```

### Affinity Key Format

Dynamic key based on consumer group count per queue:

```cpp
std::string ClusterLoadTracker::get_affinity_key(
    const std::string& queue,
    const std::string& group
) {
    int group_count = consumer_presence_->count_groups_for_queue(queue);
    
    if (group_count > config_.queue_affinity_threshold) {  // Default: 10
        // Many consumers: queue-level affinity (optimal DB queries)
        return queue;
    }
    
    // Few consumers: group-level affinity
    return queue + ":" + group;
}
```

| Consumer Groups | Affinity Key | Effect |
|-----------------|--------------|--------|
| ≤ 10 groups | `queue:group` | Distributed across servers |
| > 10 groups | `queue` | All groups → same server |

**Why:** Queues with many consumer groups benefit from single-server poll consolidation (1 DB query instead of N).

### Affinity Lifecycle

```
1. ASSIGNMENT (first request)
   └─ Key "orders:processor" not in cache
   └─ Find least loaded server → Node 2
   └─ Cache: {"orders:processor" → Node 2, assigned: now}

2. USAGE (subsequent requests)
   └─ Key found in cache → Node 2
   └─ Check load thresholds (see below)
   └─ Return Node 2 (or migrate if overloaded)

3. EXPIRY (TTL or server death)
   └─ Entry expires after 5 minutes of no use
   └─ Or: Server dies → all its affinities invalidated
   └─ Next request triggers new assignment
```

## Affinity vs Load Thresholds

When affinity target is overloaded, use thresholds to decide:

```cpp
std::string ClusterLoadTracker::evaluate_existing_affinity(
    const std::string& key, 
    const std::string& assigned
) {
    double load = calculate_load_score(loads_[assigned]);
    
    // Thresholds
    const double MIGRATE_THRESHOLD = 0.70;   // Consider migration
    const double BREAK_THRESHOLD = 0.90;     // Force break affinity
    
    if (load < MIGRATE_THRESHOLD) {
        // Normal: respect affinity
        return assigned;
    }
    
    if (load >= BREAK_THRESHOLD) {
        // Critical: BREAK affinity immediately
        std::string new_server = find_least_loaded_server();
        affinity_cache_[key].server_id = new_server;
        spdlog::warn("BREAK affinity {} from {} (load {:.0f}%) to {}", 
                    key, assigned, load * 100, new_server);
        return new_server;
    }
    
    // High load: probabilistic migration
    std::string best = find_least_loaded_server();
    double improvement = load - calculate_load_score(loads_[best]);
    
    if (improvement > 0.15 && random_double() < improvement) {
        // Migrate with probability proportional to improvement
        affinity_cache_[key].server_id = best;
        spdlog::info("Migrate affinity {} to {} (improvement {:.0f}%)",
                    key, best, improvement * 100);
        return best;
    }
    
    return assigned;  // Keep current
}
```

### Decision Table

| Target Load | Alternative Better By | Action |
|-------------|----------------------|--------|
| < 70% | Any | Respect affinity |
| 70-90% | < 15% | Respect affinity |
| 70-90% | 15-30% | 15-30% chance migrate |
| 70-90% | > 30% | 30-50% chance migrate |
| > 90% | Any | **BREAK** affinity |

### Configuration

```bash
QUEEN_AFFINITY_TTL_MS=300000              # 5 min cache TTL
QUEEN_AFFINITY_MIGRATE_THRESHOLD=0.70     # Consider migration above 70%
QUEEN_AFFINITY_BREAK_THRESHOLD=0.90       # Force break above 90%
QUEEN_AFFINITY_MIGRATE_MIN_IMPROVEMENT=0.15  # Need 15% improvement to migrate
```

## Anti-Oscillation

1. **Threshold**: Only forward if peer is 10% better (not 1% better)
2. **Affinity cache**: Stick to assigned server until threshold exceeded
3. **Probabilistic migration**: Don't migrate all at once, gradual shift
4. **EMA smoothing**: Load metrics smoothed with α=0.3
5. **Local preference**: Tie-breaker favors local handling
6. **Hysteresis**: Different thresholds for migrate (70%) vs break (90%)

## Testing

```bash
# Enable coordinator
export QUEEN_COORDINATOR_ENABLED=true
export QUEEN_UDP_PEERS="queen-1:6634,queen-2:6634"

# Verify
curl http://queen-0:6632/api/v1/cluster/balance

# Test forwarding (check X-Served-By header)
curl -v http://queen-0:6632/api/v1/pop/queue/test?group=mygroup
```

## Implementation Order

1. `ClusterLoadTracker` class
2. Extend heartbeat with load metrics
3. `RequestForwarder` class
4. Coordinator middleware for pop routes
5. `/cluster/balance` endpoint
6. Configuration + env vars
7. Tests

