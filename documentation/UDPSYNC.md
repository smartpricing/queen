# UDPSYNC: Distributed Cache for Queen MQ

## Overview

UDPSYNC is a UDP-based distributed cache layer that shares state between Queen MQ server instances to reduce database queries and improve notification targeting. 

**Critical Design Principle:** This is a **performance optimization only**. PostgreSQL remains the single source of truth. Stale or missing cache data must NEVER break correctness - it only means slower operations (fallback to database).

## Goals

1. **Reduce DB queries** - Cache queue configs, partition IDs to avoid repeated lookups
2. **Targeted notifications** - Only notify servers that have consumers for a queue
3. **Lease coordination** - Skip DB query when partition is known to be leased elsewhere
4. **Fast failover** - Detect dead servers via heartbeat instead of waiting for lease expiry

## Non-Goals

1. **Replacing PostgreSQL** - DB is always authoritative
2. **Strong consistency** - Eventual consistency is fine, correctness comes from DB
3. **Caching messages** - Only metadata, never message payloads
4. **Caching all partitions** - Only cache hot/active partitions (LRU)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              SERVER INSTANCE                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │                        SharedStateManager                               │ │
│  │                                                                         │ │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐ │ │
│  │  │ QueueConfigCache│  │ConsumerPresence │  │    LeaseHintCache       │ │ │
│  │  │   (full sync)   │  │  (queue-level)  │  │   (broadcast hints)     │ │ │
│  │  │                 │  │                 │  │                         │ │ │
│  │  │ queue → config  │  │ queue → servers │  │ part:group → server     │ │ │
│  │  └─────────────────┘  └─────────────────┘  └─────────────────────────┘ │ │
│  │                                                                         │ │
│  │  ┌─────────────────┐  ┌─────────────────┐                              │ │
│  │  │PartitionIdCache │  │  ServerHealth   │                              │ │
│  │  │  (local, LRU)   │  │  (heartbeats)   │                              │ │
│  │  │                 │  │                 │                              │ │
│  │  │ q:p → part_id   │  │ server → alive? │                              │ │
│  │  └─────────────────┘  └─────────────────┘                              │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                              ↑                                               │
│                              │ UDP                                           │
│                              ↓                                               │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │                      UDPSyncTransport                                   │ │
│  │  - Send/receive UDP messages                                            │ │
│  │  - Serialize/deserialize                                                │ │
│  │  - Peer management                                                      │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
                                    ↕ UDP
┌─────────────────────────────────────────────────────────────────────────────┐
│                            OTHER SERVERS                                     │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Cache Tiers

### Tier 1: Queue Config Cache (Full Sync)
- **What:** Queue configurations (lease_time, encryption, window_buffer, etc.)
- **Size:** ~1000 queues × 500 bytes = 500KB
- **Sync:** Broadcast on CREATE/UPDATE/DELETE
- **Fallback:** Query DB, cache result, proceed

### Tier 2: Consumer Presence (Queue-Level)
- **What:** Which servers have consumers for which QUEUES (not partitions!)
- **Size:** ~1000 queues × ~10 servers × 50 bytes = 500KB
- **Sync:** Broadcast on consumer register/deregister
- **Fallback:** Broadcast to ALL servers (current behavior)

### Tier 3: Partition ID Cache (Local, LRU + TTL)
- **What:** Mapping of queue:partition → partition_id
- **Size:** 10,000 entries × 100 bytes = 1MB (capped by LRU)
- **TTL:** 5 minutes (entries expire even if not evicted by LRU)
- **Sync:** NEVER broadcast (local cache only); invalidated on PARTITION_DELETED message
- **Fallback:** Query DB, cache result, proceed
- **On DB error:** Invalidate entry, retry

### Tier 4: Lease Hint Cache (Broadcast Hints)
- **What:** Hints about which server holds which partition lease
- **Size:** 10,000 active × 100 bytes = 1MB
- **Sync:** Broadcast on acquire/release (HINT only, not authoritative)
- **TTL:** Dynamic based on queue's lease_time (lease_time / 5)
- **Fallback:** Try DB anyway (may fail, that's OK)

### Tier 5: Server Health (Heartbeats)
- **What:** Last heartbeat timestamp per server
- **Size:** ~10 servers × 50 bytes = negligible
- **Sync:** Every 1 second
- **Dead threshold:** 5 seconds (5 missed heartbeats)
- **Use:** Detect dead servers for faster lease invalidation hints

---

## Correctness Guarantees

### The Golden Rule

> **Cache is ALWAYS advisory. PostgreSQL is ALWAYS authoritative.**

### Scenario Analysis

| Scenario | Cache Says | Reality | Behavior | Correct? |
|----------|-----------|---------|----------|----------|
| Queue config lookup | Config X | Config X | Use cached | ✅ |
| Queue config lookup | Config X | Config Y (updated) | Use stale, works, next query refreshes | ✅ |
| Queue config lookup | Not found | Exists | Query DB, cache, proceed | ✅ |
| Lease hint | "Server A has it" | Server A has it | Skip DB, try other partition | ✅ |
| Lease hint | "Server A has it" | Lease expired | Try DB, succeed, update cache | ✅ |
| Lease hint | "Available" | Server B has it | Try DB, fail, backoff | ✅ |
| Consumer presence | "Only Server B" | Server B + C | Notify B, C missed → C polls via backoff | ✅ |
| Consumer presence | "Server B, C" | Only Server B | Notify both, C ignores | ✅ |

**Key insight:** False negatives cause slightly more work (DB query, missed notification). False positives cause wasted work (extra notification, failed lease). Neither causes incorrect behavior.

---

## Message Protocol

### Message Format

```cpp
// Logical structure (serialized to buffer, NOT a C++ struct with flexible array)
struct UDPSyncMessage {
    uint8_t version;           // Protocol version (1)
    uint8_t type;              // MessageType enum
    uint16_t payload_length;   // Length of MessagePack payload
    uint8_t signature[32];     // HMAC-SHA256 of (version + type + sender_id + session_id + sequence + payload)
    char sender_id[32];        // Server ID (null-terminated, e.g., "queen-mq-0")
    char session_id[16];       // Random ID generated at startup (for restart detection)
    uint64_t sequence;         // Monotonic sequence for ordering (per session)
    // Followed by: MessagePack payload (payload_length bytes)
};
// Total header: 1 + 1 + 2 + 32 + 32 + 16 + 8 = 92 bytes
// Max safe payload: ~1308 bytes (to stay under 1400 MTU)
```

**Security:** All packets are signed with HMAC-SHA256 using a shared secret (`QUEEN_SYNC_SECRET`). Packets with invalid signatures are dropped silently.

**Replay Protection:** Each server generates a random `session_id` at startup. Receivers track `(sender_id, session_id, max_sequence)` and reject:
- Packets with sequence ≤ last seen sequence (duplicates, replays, reordering)
- When session_id changes, sequence tracking resets (server restarted)

**Serialization:** Payloads use MessagePack (not JSON) for compact binary encoding. nlohmann/json supports both:
```cpp
// Serialize
std::vector<uint8_t> payload = nlohmann::json::to_msgpack(data);

// Deserialize  
nlohmann::json data = nlohmann::json::from_msgpack(payload);
```

### Message Types

```cpp
enum class UDPSyncMessageType : uint8_t {
    // === Existing (from InterInstanceComms) ===
    MESSAGE_AVAILABLE = 1,      // New message pushed
    PARTITION_FREE = 2,         // Partition acknowledged
    HEARTBEAT = 3,              // Server alive
    
    // === Queue Config (Tier 1) ===
    QUEUE_CONFIG_SET = 10,      // Queue created or updated
    QUEUE_CONFIG_DELETE = 11,   // Queue deleted
    
    // === Consumer Presence (Tier 2) ===
    CONSUMER_REGISTERED = 20,   // Server has consumer for queue
    CONSUMER_DEREGISTERED = 21, // Server no longer has consumer for queue
    
    // === Partition (Tier 3) ===
    PARTITION_DELETED = 25,     // Partition deleted by retention (invalidate cache)
    
    // === Lease Hints (Tier 4) ===
    LEASE_HINT_ACQUIRED = 30,   // Hint: server acquired lease
    LEASE_HINT_RELEASED = 31,   // Hint: server released lease
};
```

### Payload Examples

```json
// QUEUE_CONFIG_SET
{
    "queue": "orders",
    "config": {
        "lease_time": 300,
        "encryption_enabled": true,
        "window_buffer": 0,
        "delayed_processing": 0
    },
    "version": 42
}

// CONSUMER_REGISTERED
{
    "queue": "orders",
    "server_id": "queen-mq-1"
}

// LEASE_HINT_ACQUIRED
{
    "partition_id": "abc-123",
    "consumer_group": "processor",
    "server_id": "queen-mq-2",
    "expires_at_ms": 1700000000000
}

// PARTITION_DELETED (from retention service)
{
    "queue": "orders",
    "partition": "customer-123"
}

// MESSAGE_AVAILABLE (existing, unchanged)
{
    "queue": "orders",
    "partition": "customer-123",
    "ts": 1700000000000
}
```

---

## Components to Create

### 1. `SharedStateManager` (NEW)
**Location:** `server/include/queen/shared_state_manager.hpp`

Central manager for all shared state caches.

```cpp
class SharedStateManager {
public:
    // Lifecycle
    void start();
    void stop();
    
    // Queue Config (Tier 1)
    std::optional<QueueConfig> get_queue_config(const std::string& queue);
    void set_queue_config(const std::string& queue, const QueueConfig& config);
    void delete_queue_config(const std::string& queue);
    
    // Consumer Presence (Tier 2)
    std::set<std::string> get_servers_for_queue(const std::string& queue);
    void register_consumer(const std::string& queue);   // Registers THIS server
    void deregister_consumer(const std::string& queue); // Deregisters THIS server
    
    // Partition ID (Tier 3) - LOCAL ONLY
    std::optional<std::string> get_partition_id(const std::string& queue, const std::string& partition);
    void cache_partition_id(const std::string& queue, const std::string& partition, const std::string& id);
    
    // Lease Hints (Tier 4)
    bool is_likely_leased_elsewhere(const std::string& partition_id, const std::string& consumer_group);
    void hint_lease_acquired(const std::string& partition_id, const std::string& consumer_group);
    void hint_lease_released(const std::string& partition_id, const std::string& consumer_group);
    
    // Server Health (Tier 5)
    bool is_server_alive(const std::string& server_id);
    std::vector<std::string> get_dead_servers();
    
    // Stats
    nlohmann::json get_stats() const;
    
private:
    std::shared_ptr<UDPSyncTransport> transport_;
    
    // Caches
    QueueConfigCache queue_configs_;
    ConsumerPresenceCache consumer_presence_;
    PartitionIdCache partition_ids_;      // LRU, local only
    LeaseHintCache lease_hints_;
    ServerHealthTracker server_health_;
};
```

### 2. `UDPSyncTransport` (REFACTOR from InterInstanceComms)
**Location:** `server/include/queen/udp_sync_transport.hpp`

Low-level UDP send/receive, extracted from current InterInstanceComms.

```cpp
class UDPSyncTransport {
public:
    using MessageHandler = std::function<void(const UDPSyncMessage&)>;
    
    UDPSyncTransport(const UDPSyncConfig& config, const std::string& server_id);
    
    void start();
    void stop();
    
    // Send to specific servers
    void send(const std::string& target_server, const UDPSyncMessage& msg);
    
    // Broadcast to all peers
    void broadcast(const UDPSyncMessage& msg);
    
    // Send to servers with consumers for queue (uses SharedStateManager)
    void send_to_queue_consumers(const std::string& queue, const UDPSyncMessage& msg);
    
    // Register handler for message type
    void on_message(UDPSyncMessageType type, MessageHandler handler);
    
private:
    void recv_loop();
    void dispatch_message(const UDPSyncMessage& msg);
};
```

### 3. Cache Implementations
**Location:** `server/include/queen/caches/`

```
server/include/queen/caches/
├── queue_config_cache.hpp      # Thread-safe map with version tracking
├── consumer_presence_cache.hpp # Queue → set<server_id>
├── partition_id_cache.hpp      # LRU cache with TTL
├── lease_hint_cache.hpp        # TTL-based hint storage
└── server_health_tracker.hpp   # Heartbeat tracking
```

---

## Components to Modify

### 1. `InterInstanceComms` → Integrate with SharedStateManager

**Current:** Standalone, handles MESSAGE_AVAILABLE and PARTITION_FREE only.

**Change:** 
- Move UDP transport logic to `UDPSyncTransport`
- InterInstanceComms becomes a thin wrapper that uses SharedStateManager
- Add consumer presence tracking for targeted notifications

```cpp
// Before
void InterInstanceComms::notify_message_available(queue, partition) {
    broadcast_to_all_peers({MESSAGE_AVAILABLE, queue, partition});
}

// After
void InterInstanceComms::notify_message_available(queue, partition) {
    auto servers = shared_state_->get_servers_for_queue(queue);
    if (servers.empty()) {
        // Fallback: broadcast to all (no presence info yet)
        transport_->broadcast({MESSAGE_AVAILABLE, queue, partition});
    } else {
        for (const auto& server : servers) {
            transport_->send(server, {MESSAGE_AVAILABLE, queue, partition});
        }
    }
}
```

### 2. `AsyncQueueManager` → Use Caches

**File:** `server/src/managers/async_queue_manager.cpp`

#### 2.1 Queue Config Lookups

```cpp
// Before (PUSH)
sendQueryParamsAsync(conn, "SELECT id FROM queen.queues WHERE name = $1", {queue});
sendQueryParamsAsync(conn, "SELECT encryption_enabled FROM queen.queues WHERE name = $1", {queue});

// After
auto cached = shared_state_->get_queue_config(queue);
if (cached) {
    // Use cached config
    bool encryption_enabled = cached->encryption_enabled;
} else {
    // Cache miss: query DB, cache result
    auto config = query_queue_config_from_db(conn, queue);
    shared_state_->set_queue_config(queue, config);
}
```

#### 2.2 Partition ID Lookups

```cpp
// Before
ensure_partition_exists(conn, queue, partition);
// Always queries DB

// After
auto partition_id = shared_state_->get_partition_id(queue, partition);
if (!partition_id) {
    // Cache miss: query/create in DB
    partition_id = ensure_partition_exists_and_get_id(conn, queue, partition);
    shared_state_->cache_partition_id(queue, partition, *partition_id);
}
```

#### 2.3 Lease Acquisition (Hints Only)

```cpp
// Before
std::string lease_id = acquire_partition_lease(conn, queue, partition, group, lease_time);
// Always tries DB, may fail if already leased

// After
// Check hint first (optimization, not authoritative!)
if (shared_state_->is_likely_leased_elsewhere(partition_id, group)) {
    // Hint says another server has it - skip DB, try different partition
    return "";  // or try next partition
}

// Hint says available (or no hint) - still must try DB!
std::string lease_id = acquire_partition_lease(conn, queue, partition, group, lease_time);

if (!lease_id.empty()) {
    // Success! Broadcast hint to peers
    shared_state_->hint_lease_acquired(partition_id, group);
}
```

### 3. `PollIntentionRegistry` → Trigger Consumer Presence

**File:** `server/src/services/poll_intention_registry.cpp`

```cpp
void PollIntentionRegistry::register_intention(const PollIntention& intention) {
    // Existing logic...
    
    // NEW: Track consumer presence at queue level
    if (intention.queue_name.has_value()) {
        std::string queue = *intention.queue_name;
        
        // If this is the first intention for this queue, register presence
        if (!has_intentions_for_queue(queue)) {
            shared_state_->register_consumer(queue);
        }
    }
}

void PollIntentionRegistry::remove_intention(const std::string& request_id) {
    // Existing logic...
    
    // NEW: If no more intentions for this queue, deregister presence
    if (removed_intention.queue_name.has_value()) {
        std::string queue = *removed_intention.queue_name;
        
        if (!has_intentions_for_queue(queue)) {
            shared_state_->deregister_consumer(queue);
        }
    }
}
```

### 4. Queue CRUD Routes → Broadcast Config Changes

**File:** `server/src/routes/configure.cpp`

```cpp
// After successful queue CREATE/UPDATE
shared_state_->set_queue_config(queue_name, config);  // Broadcasts to peers

// After successful queue DELETE
shared_state_->delete_queue_config(queue_name);  // Broadcasts to peers
```

### 5. ACK Route → Broadcast Lease Release

**File:** `server/src/routes/ack.cpp`

```cpp
// After successful ACK
shared_state_->hint_lease_released(partition_id, consumer_group);  // Broadcasts hint
```

### 6. Retention Service → Broadcast Partition Deletion

**File:** `server/src/services/retention_service.cpp`

```cpp
// After deleting partitions
for (const auto& deleted : deleted_partitions) {
    shared_state_->invalidate_partition(deleted.queue_name, deleted.partition_name);
    // This removes from local cache AND broadcasts PARTITION_DELETED to peers
}
```

---

## Error Handling Patterns

### Pattern 1: Cache Miss → DB Query → Cache

```cpp
std::optional<std::string> get_partition_id_with_cache(
    const std::string& queue, 
    const std::string& partition
) {
    // 1. Try cache first
    auto cached = shared_state_->get_partition_id(queue, partition);
    if (cached) {
        return cached;  // Cache hit!
    }
    
    // 2. Cache miss: query DB
    auto conn = db_pool_->acquire();
    auto partition_id = query_partition_id_from_db(conn.get(), queue, partition);
    
    if (partition_id) {
        // 3. Cache for next time
        shared_state_->cache_partition_id(queue, partition, *partition_id);
    }
    
    return partition_id;
}
```

### Pattern 2: DB Error → Invalidate → Retry

```cpp
PushResult push_with_cache_recovery(const PushItem& item) {
    int retries = 0;
    const int max_retries = 2;
    
    while (retries < max_retries) {
        try {
            // Get partition ID (from cache or DB)
            auto partition_id = get_partition_id_with_cache(item.queue, item.partition);
            
            if (!partition_id) {
                // Partition doesn't exist, create it
                partition_id = create_partition(item.queue, item.partition);
                shared_state_->cache_partition_id(item.queue, item.partition, *partition_id);
            }
            
            // Try to insert message
            return insert_message(*partition_id, item);
            
        } catch (const PartitionNotFoundError& e) {
            // Partition was deleted (cached ID is stale)
            spdlog::warn("Partition not found (stale cache?), invalidating and retrying");
            
            // Invalidate cache entry
            shared_state_->invalidate_partition(item.queue, item.partition);
            
            retries++;
            // Loop will retry with fresh DB query
            
        } catch (const std::exception& e) {
            // Other errors: don't retry
            throw;
        }
    }
    
    throw std::runtime_error("Max retries exceeded for partition lookup");
}
```

### Pattern 3: Lease Hint → Skip or Try DB

```cpp
std::string try_acquire_lease_with_hints(
    const std::string& partition_id,
    const std::string& consumer_group
) {
    // Check hint FIRST (optimization)
    if (shared_state_->is_likely_leased_elsewhere(partition_id, consumer_group)) {
        // Hint says another server has it
        // Skip DB query - try a different partition
        spdlog::debug("Lease hint: {} likely leased elsewhere, skipping", partition_id);
        return "";  // Signal caller to try another partition
    }
    
    // Hint says available (or no hint) - MUST try DB
    // The hint could be wrong!
    auto lease_id = acquire_lease_in_db(partition_id, consumer_group);
    
    if (!lease_id.empty()) {
        // Success! Update hint for peers
        shared_state_->hint_lease_acquired(partition_id, consumer_group);
    }
    
    return lease_id;
}
```

### Pattern 4: Graceful Degradation

```cpp
void notify_message_available(const std::string& queue, const std::string& partition) {
    // Try to get targeted server list
    auto servers = shared_state_->get_servers_for_queue(queue);
    
    if (servers.empty()) {
        // No presence info - FALLBACK to broadcast
        spdlog::debug("No consumer presence for {}, broadcasting to all", queue);
        transport_->broadcast({MESSAGE_AVAILABLE, queue, partition});
    } else {
        // Targeted notification - more efficient
        for (const auto& server : servers) {
            if (server != own_server_id_) {
                transport_->send(server, {MESSAGE_AVAILABLE, queue, partition});
            }
        }
    }
}

---

## Configuration

### Environment Variables

```bash
# Enable/disable shared state sync (default: enabled if peers configured)
QUEEN_SYNC_ENABLED=true

# === Security ===
QUEEN_SYNC_SECRET=<64-char-hex-key>     # HMAC secret for packet signing (REQUIRED if sync enabled)

# === Cache Sizes ===
QUEEN_CACHE_PARTITION_MAX=10000         # Max partition IDs to cache (LRU)

# === TTL Settings ===
QUEEN_CACHE_PARTITION_TTL_MS=300000     # Partition ID cache TTL (5 minutes)
# Note: Lease hint TTL is dynamic: queue.lease_time / 5

# === Periodic Refresh ===
QUEEN_CACHE_REFRESH_INTERVAL_MS=60000   # Queue config refresh from DB (60 seconds)

# === Heartbeat ===
QUEEN_SYNC_HEARTBEAT_MS=1000            # Heartbeat interval (1 second)
QUEEN_SYNC_DEAD_THRESHOLD_MS=5000       # Server dead after no heartbeat (5 seconds = 5 missed heartbeats)

# === Performance Tuning ===
QUEEN_SYNC_RECV_BUFFER_MB=8             # UDP receive buffer size (default: 8MB)
QUEEN_SYNC_CACHE_SHARDS=16              # Number of cache shards for lock distribution
```

### Config Struct

```cpp
struct SharedStateConfig {
    bool enabled = true;
    
    // Security
    std::string sync_secret;            // HMAC secret (required if enabled)
    
    // Cache limits
    int partition_cache_max = 10000;
    int cache_shards = 16;              // Number of shards for lock distribution
    
    // TTL settings (milliseconds)
    int partition_cache_ttl_ms = 300000;  // 5 minutes
    // lease_hint_ttl is dynamic: queue.lease_time * 1000 / 5
    
    // Periodic refresh
    int queue_config_refresh_ms = 60000;  // 60 seconds
    
    // Heartbeat
    int heartbeat_interval_ms = 1000;   // 1 second
    int dead_threshold_ms = 5000;       // 5 seconds (5 missed heartbeats)
    
    // Performance
    int recv_buffer_mb = 8;             // UDP receive buffer size
    
    static SharedStateConfig from_env();
    
    bool validate() const {
        if (enabled && sync_secret.empty()) {
            spdlog::error("QUEEN_SYNC_SECRET required when sync is enabled");
            return false;
        }
        if (enabled && sync_secret.length() != 64) {
            spdlog::error("QUEEN_SYNC_SECRET must be 64 hex characters");
            return false;
        }
        return true;
    }
};
```

---

## Implementation Phases

### Phase 1: Infrastructure (Foundation) ✅ COMPLETE
**Goal:** Set up the framework without changing behavior

1. ✅ Create `UDPSyncTransport` (refactor from InterInstanceComms)
2. ✅ Create `SharedStateManager` skeleton
3. ✅ Create cache data structures (empty implementations)
4. ✅ Wire up to `acceptor_server.cpp`
5. ✅ Add stats endpoint (`/internal/api/shared-state/stats`)

**Deliverables:**
- ✅ New files created
- ✅ Existing behavior unchanged
- ✅ Stats endpoint shows cache states

### Phase 2: Queue Config Cache ✅ COMPLETE
**Goal:** Reduce DB queries for queue config on PUSH/POP

1. ✅ Implement `QueueConfigCache` (RCU pattern for lock-free reads)
2. ✅ Broadcast on queue CREATE/UPDATE/DELETE (`configure_queue`, `delete_queue`)
3. ✅ Modify `AsyncQueueManager` to use cache (encryption check)
4. ✅ Add cache miss → DB fallback
5. ✅ Periodic DB refresh (60s)

**Deliverables:**
- ✅ Queue config cached after first access
- ✅ Stats show cache hit rate

### Phase 3: Consumer Presence & Targeted Notifications ✅ COMPLETE
**Goal:** Only notify servers that have consumers

1. ✅ Implement `ConsumerPresenceCache`
2. ✅ Modify `routes/pop.cpp` to register consumer presence on poll
3. ✅ `SharedStateManager` uses targeted notifications via learned identities
4. ✅ Fallback to broadcast if no presence info

**Deliverables:**
- ✅ Notifications only to servers with consumers
- ✅ Stats show registrations/deregistrations

### Phase 4: Partition ID Cache ✅ COMPLETE
**Goal:** Reduce DB queries for partition IDs

1. ✅ Implement LRU `PartitionIdCache` with TTL and sharding
2. ✅ Modify `AsyncQueueManager::push_messages_chunk` to cache partition IDs
3. ✅ Cache on first access, LRU eviction

**Deliverables:**
- ✅ Partition IDs cached
- ✅ Stats show LRU eviction rate, hit rate

### Phase 5: Lease Hints ✅ COMPLETE
**Goal:** Skip DB query when partition is known to be leased elsewhere

1. ✅ Implement `LeaseHintCache` with TTL
2. ✅ Broadcast hints on acquire (`acquire_partition_lease`)
3. ✅ Broadcast hints on release (`routes/ack.cpp`)
4. ✅ Check hints before partition selection in `pop_messages_from_queue`
5. ✅ Probabilistic DB check (1% chance to ignore hint) for self-healing

**Deliverables:**
- ✅ Faster partition selection on filtered POP
- ✅ Stats show hint accuracy

### Phase 6: Server Health & Fast Failover ✅ COMPLETE
**Goal:** Detect dead servers for faster rebalancing

1. ✅ Implement `ServerHealthTracker`
2. ✅ Heartbeat messages (1s interval, 5s dead threshold)
3. ✅ Track last heartbeat per server
4. ✅ Invalidate lease hints for dead servers (in `is_likely_leased_elsewhere`)
5. ✅ Broadcast `PARTITION_DELETED` from retention service
6. ✅ Learned identity system (heartbeat-based identity discovery)

**Deliverables:**
- ✅ Dead server detection in ~5 seconds
- ✅ Lease hints invalidated for dead servers
- ✅ Stats show server health

---

## File Structure

```
server/
├── include/queen/
│   ├── shared_state_manager.hpp       # NEW: Central manager
│   ├── udp_sync_transport.hpp         # NEW: UDP layer (from InterInstanceComms)
│   ├── udp_sync_message.hpp           # NEW: Message types and serialization
│   ├── caches/
│   │   ├── queue_config_cache.hpp     # NEW
│   │   ├── consumer_presence_cache.hpp # NEW
│   │   ├── partition_id_cache.hpp     # NEW (LRU + TTL)
│   │   ├── lease_hint_cache.hpp       # NEW
│   │   ├── server_health_tracker.hpp  # NEW
│   │   └── lru_cache.hpp              # NEW: Generic LRU implementation with TTL
│   ├── inter_instance_comms.hpp       # MODIFY: Use transport + targeted notifications
│   └── config.hpp                     # MODIFY: Add SharedStateConfig
├── src/
│   ├── services/
│   │   ├── shared_state_manager.cpp   # NEW
│   │   ├── udp_sync_transport.cpp     # NEW
│   │   ├── inter_instance_comms.cpp   # MODIFY: Refactor to use transport
│   │   ├── poll_intention_registry.cpp # MODIFY: Track consumer presence
│   │   └── retention_service.cpp      # MODIFY: Broadcast PARTITION_DELETED
│   ├── managers/
│   │   └── async_queue_manager.cpp    # MODIFY: Use caches, error recovery
│   ├── routes/
│   │   ├── configure.cpp              # MODIFY: Broadcast config changes
│   │   └── ack.cpp                    # MODIFY: Broadcast lease release
│   └── acceptor_server.cpp            # MODIFY: Initialize SharedStateManager, start refresh thread
```

---

## Testing Strategy

### Unit Tests
- Cache operations (get/set/evict)
- LRU eviction behavior
- TTL expiration
- Message serialization/deserialization

### Integration Tests
- Multi-server cache sync
- Cache miss → DB fallback
- Stale cache → correct behavior
- Network partition → graceful degradation

### Stress Tests
- 100k partitions, verify memory stays bounded
- High message rate, verify no message loss
- Server restart, verify sync recovery

### Correctness Tests
- **Critical:** Verify that incorrect/stale cache NEVER causes:
  - Message loss
  - Duplicate message delivery
  - Lease corruption
  - Incorrect consumer group behavior

---

## Metrics & Observability

### Stats Endpoint Enhancement

```json
GET /internal/api/shared-state/stats

{
    "queue_config_cache": {
        "size": 150,
        "hits": 10000,
        "misses": 50,
        "hit_rate": 0.995
    },
    "consumer_presence": {
        "queues_tracked": 150,
        "servers_tracked": 3,
        "notifications_targeted": 5000,
        "notifications_broadcast": 50
    },
    "partition_id_cache": {
        "size": 8500,
        "max_size": 10000,
        "hits": 50000,
        "misses": 1000,
        "evictions": 500
    },
    "lease_hints": {
        "size": 1200,
        "hints_used": 3000,
        "hints_wrong": 50,
        "accuracy": 0.983
    },
    "server_health": {
        "servers_alive": 3,
        "servers_dead": 0,
        "last_heartbeat": {
            "queen-mq-0": "2024-01-01T00:00:00Z",
            "queen-mq-1": "2024-01-01T00:00:00Z",
            "queen-mq-2": "2024-01-01T00:00:00Z"
        }
    }
}
```

---

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Stale queue config | Use old lease_time, encryption setting | Periodic DB refresh (60s); version tracking |
| Stale partition ID | FK violation on insert | TTL (5min); invalidate on error + retry; PARTITION_DELETED broadcast |
| Missed notification | Consumer polls later via backoff | Existing backoff still works; slight latency increase |
| Wrong lease hint | Wasted DB query or skipped partition | Hints are advisory; DB is authoritative |
| Memory growth | OOM | LRU eviction for partitions; TTL for leases |
| Network partition | Servers diverge | Heartbeat detects; periodic DB refresh |
| Message loss (UDP) | Missing updates | TTL expiration; periodic DB refresh; no single-point-of-failure |
| Retention deletes cached partition | Stale cache points to deleted | PARTITION_DELETED broadcast + TTL + error recovery |

---

## Implementation Concerns & Solutions

### 1. UDP Packet Size (MTU Limits)

**Problem:** UDP has ~1400 byte safe payload limit in datacenter. JSON is verbose. Large payloads cause IP fragmentation → packet loss.

**Solution:**
- Use **MessagePack** instead of JSON for serialization (10x smaller, faster to parse)
- Add payload size validation: if `size > 1200`, log warning and consider compression
- Keep payloads minimal (no redundant fields)

```cpp
// Use MessagePack (nlohmann/json supports it)
std::vector<uint8_t> payload = nlohmann::json::to_msgpack(msg.to_json());

// On receive
nlohmann::json parsed = nlohmann::json::from_msgpack(buffer, buffer + size);
```

### 2. Security: Poisoned Cache Attack

**Problem:** Attacker could send fake UDP packets to inject false configs, delete hints, or cause DoS via cache thrashing.

**Solution:** Add HMAC-SHA256 signature to all messages.

```cpp
struct UDPSyncMessage {
    uint8_t version;
    uint8_t type;
    uint16_t payload_length;
    uint8_t signature[32];     // HMAC-SHA256 of payload
    char sender_id[32];
    uint64_t sequence;
    uint8_t payload[];         // MessagePack encoded
};

// Signing (sender)
auto hmac = compute_hmac_sha256(shared_secret_, payload);
memcpy(msg.signature, hmac.data(), 32);

// Verification (receiver)
auto expected = compute_hmac_sha256(shared_secret_, payload);
if (memcmp(msg.signature, expected.data(), 32) != 0) {
    spdlog::warn("UDPSync: Invalid signature, dropping packet from {}", sender_ip);
    return;  // Drop packet
}
```

**Config:**
```bash
QUEEN_SYNC_SECRET=<64-char-hex-key>  # Shared secret for HMAC
```

### 3. Stuck Hint Starvation

**Problem:** Server A acquires lease, broadcasts hint, then crashes. Server B sees hint and skips DB for up to `lease_time/5` seconds, even though lease is actually available.

**Solution:** Two-layer protection:

1. **Check server health before trusting hint**
```cpp
bool is_likely_leased_elsewhere(const std::string& partition_id, const std::string& group) {
    auto hint = lease_hints_.get(partition_id + ":" + group);
    if (!hint) return false;  // No hint, try DB
    
    // If the hinted server is DEAD, ignore the hint!
    if (!server_health_.is_alive(hint->server_id)) {
        spdlog::debug("Hint says {} has lease, but server is dead - ignoring hint", 
                     hint->server_id);
        lease_hints_.remove(partition_id + ":" + group);  // Clean up stale hint
        return false;  // Try DB
    }
    
    return hint->expires_at_ms > now_ms();
}
```

2. **Probabilistic DB check (1% chance to ignore hint)**
```cpp
// Even with valid hint, 1% chance to check DB anyway
// This catches edge cases and provides self-healing
static thread_local std::mt19937 rng(std::random_device{}());
if (std::uniform_int_distribution<>(1, 100)(rng) == 1) {
    spdlog::debug("Probabilistic DB check (ignoring hint)");
    return false;  // Check DB anyway
}
```

### 4. Lock Contention

**Problem:** Single `shared_mutex` on hot caches becomes bottleneck at high throughput (10k+ ops/sec).

**Solution:** Sharded locks + RCU for read-heavy caches.

```cpp
// Sharded cache (16 shards)
template<typename K, typename V>
class ShardedCache {
    static constexpr int NUM_SHARDS = 16;
    
    struct Shard {
        std::unordered_map<K, V> data;
        mutable std::shared_mutex mutex;
    };
    
    std::array<Shard, NUM_SHARDS> shards_;
    
    Shard& get_shard(const K& key) {
        return shards_[std::hash<K>{}(key) % NUM_SHARDS];
    }
    
public:
    std::optional<V> get(const K& key) {
        auto& shard = get_shard(key);
        std::shared_lock lock(shard.mutex);
        auto it = shard.data.find(key);
        return it != shard.data.end() ? std::optional(it->second) : std::nullopt;
    }
    
    void set(const K& key, const V& value) {
        auto& shard = get_shard(key);
        std::unique_lock lock(shard.mutex);
        shard.data[key] = value;
    }
};
```

**For QueueConfigCache (read-heavy, rarely updated):** Use atomic shared_ptr swap (RCU-style):

```cpp
class QueueConfigCache {
    std::atomic<std::shared_ptr<const std::unordered_map<std::string, QueueConfig>>> data_;
    std::mutex write_mutex_;  // Only for writers
    
public:
    std::optional<QueueConfig> get(const std::string& queue) {
        // Lock-free read!
        auto snapshot = data_.load();
        auto it = snapshot->find(queue);
        return it != snapshot->end() ? std::optional(it->second) : std::nullopt;
    }
    
    void set(const std::string& queue, const QueueConfig& config) {
        std::lock_guard lock(write_mutex_);
        // Copy-on-write
        auto old = data_.load();
        auto new_map = std::make_shared<std::unordered_map<std::string, QueueConfig>>(*old);
        (*new_map)[queue] = config;
        data_.store(new_map);  // Atomic swap
    }
};
```

### 5. String Allocation in Hot Paths

**Problem:** `map[queue + ":" + partition]` allocates a new string on every lookup.

**Solution:** Use `std::pair<std::string_view, std::string_view>` for lookups, or pre-hash:

```cpp
// Option 1: Composite key with custom hash
struct PartitionKey {
    std::string queue;
    std::string partition;
    
    bool operator==(const PartitionKey& o) const {
        return queue == o.queue && partition == o.partition;
    }
};

struct PartitionKeyHash {
    size_t operator()(const PartitionKey& k) const {
        size_t h1 = std::hash<std::string>{}(k.queue);
        size_t h2 = std::hash<std::string>{}(k.partition);
        return h1 ^ (h2 << 1);
    }
};

std::unordered_map<PartitionKey, std::string, PartitionKeyHash> partition_cache_;

// Option 2: Pre-compute hash for lookup (no allocation)
size_t compute_key_hash(std::string_view queue, std::string_view partition) {
    size_t h1 = std::hash<std::string_view>{}(queue);
    size_t h2 = std::hash<std::string_view>{}(partition);
    return h1 ^ (h2 << 1);
}
```

### 6. UDP Receive Buffer Overflow

**Problem:** Burst of packets (e.g., after server restart) can overflow kernel's UDP receive buffer, causing silent packet loss.

**Solution:** Set large receive buffer in `UDPSyncTransport::init_sockets()`:

```cpp
bool UDPSyncTransport::init_sockets() {
    udp_recv_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    
    // Set large receive buffer (8MB)
    int recv_buf_size = 8 * 1024 * 1024;
    if (setsockopt(udp_recv_sock_, SOL_SOCKET, SO_RCVBUF, 
                   &recv_buf_size, sizeof(recv_buf_size)) < 0) {
        spdlog::warn("UDPSync: Failed to set SO_RCVBUF to 8MB: {}", strerror(errno));
    }
    
    // Verify actual size (kernel may cap it)
    int actual_size;
    socklen_t len = sizeof(actual_size);
    getsockopt(udp_recv_sock_, SOL_SOCKET, SO_RCVBUF, &actual_size, &len);
    spdlog::info("UDPSync: Receive buffer size = {} bytes", actual_size);
    
    // ... rest of initialization
}
```

**Note:** On Linux, may need to increase system limit:
```bash
# /etc/sysctl.conf or runtime
sysctl -w net.core.rmem_max=8388608
```

### 7. Cache Stampede (Thundering Herd)

**Problem:** On cold start or cache expiration, thousands of concurrent requests for the same key all miss cache and all hit DB simultaneously.

**Scenario:**
```
Deploy new version (empty cache)
5,000 consumers connect, all want "orders" queue config
→ 5,000 simultaneous DB queries for same row
→ DB CPU spikes, connections timeout
```

**Solution: Request Coalescing (Singleflight Pattern)**

Only ONE request queries DB; others wait for the result.

```cpp
template<typename K, typename V>
class CoalescingCache {
    struct InFlightRequest {
        std::promise<std::optional<V>> promise;
        std::shared_future<std::optional<V>> future;
    };
    
    std::unordered_map<K, V> cache_;
    std::unordered_map<K, std::shared_ptr<InFlightRequest>> inflight_;
    std::mutex mutex_;
    
public:
    using Loader = std::function<std::optional<V>(const K&)>;
    
    std::optional<V> get_or_load(const K& key, Loader loader) {
        // Fast path: check cache
        {
            std::lock_guard lock(mutex_);
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                return it->second;  // Cache hit
            }
            
            // Check if someone else is already loading
            auto inflight_it = inflight_.find(key);
            if (inflight_it != inflight_.end()) {
                // Wait for the other request
                auto future = inflight_it->second->future;
                lock.~lock_guard();  // Release lock while waiting
                return future.get();
            }
            
            // We are the first - create inflight marker
            auto req = std::make_shared<InFlightRequest>();
            req->future = req->promise.get_future().share();
            inflight_[key] = req;
        }
        
        // We are the loader - query DB (outside lock!)
        std::optional<V> result;
        try {
            result = loader(key);
        } catch (...) {
            // Clean up and rethrow
            std::lock_guard lock(mutex_);
            inflight_.erase(key);
            throw;
        }
        
        // Populate cache and notify waiters
        {
            std::lock_guard lock(mutex_);
            if (result) {
                cache_[key] = *result;
            }
            auto req = inflight_[key];
            inflight_.erase(key);
            req->promise.set_value(result);
        }
        
        return result;
    }
};
```

### 8. Replay Attack / Zombie Packets

**Problem:** Old or duplicate UDP packets can corrupt state.

**Scenario:**
```
1. Attacker captures valid LEASE_HINT_ACQUIRED (Seq 100) from Server A
2. Server A releases lease (Seq 101)
3. Attacker replays Seq 100 five minutes later
4. Receiver updates cache: "Server A has lease" (WRONG!)
```

**Solution: Sequence Tracking with Session ID**

Track highest sequence per sender. Reject old/duplicate packets.

```cpp
class UDPSyncTransport {
    struct SenderState {
        std::string session_id;     // Random UUID, changes on restart
        uint64_t max_seen_seq = 0;
    };
    
    std::unordered_map<std::string, SenderState> sender_states_;
    std::mutex sender_mutex_;
    
    bool validate_sequence(const UDPSyncMessage& msg) {
        std::lock_guard lock(sender_mutex_);
        
        auto& state = sender_states_[msg.sender_id];
        
        // Check session ID (detect server restarts)
        if (state.session_id.empty()) {
            // First packet from this sender
            state.session_id = msg.session_id;
            state.max_seen_seq = msg.sequence;
            return true;
        }
        
        if (state.session_id != msg.session_id) {
            // Server restarted - reset sequence tracking
            spdlog::info("UDPSync: Server {} restarted (new session)", msg.sender_id);
            state.session_id = msg.session_id;
            state.max_seen_seq = msg.sequence;
            return true;
        }
        
        // Same session - check sequence
        if (msg.sequence <= state.max_seen_seq) {
            // Old or duplicate packet - drop it
            spdlog::debug("UDPSync: Dropping old packet from {} (seq {} <= {})",
                         msg.sender_id, msg.sequence, state.max_seen_seq);
            return false;
        }
        
        state.max_seen_seq = msg.sequence;
        return true;
    }
    
    void dispatch_message(const UDPSyncMessage& msg) {
        // Validate HMAC first
        if (!validate_hmac(msg)) return;
        
        // Validate sequence (reject old/duplicate/reordered)
        if (!validate_sequence(msg)) return;
        
        // Process message...
    }
};
```

**Note:** This also solves **UDP packet reordering**. If packets arrive out of order (Seq 51 before Seq 50), Seq 50 will be rejected because `50 < 51`.

### 9. Cascade Deletion on Queue Delete

**Problem:** When a queue is deleted, orphan entries remain in PartitionIdCache and LeaseHintCache.

**Solution:** Cascade the deletion:

```cpp
void SharedStateManager::delete_queue_config(const std::string& queue_name) {
    // 1. Remove from queue config cache
    queue_configs_.remove(queue_name);
    
    // 2. Remove all partitions for this queue from partition cache
    partition_ids_.remove_by_prefix(queue_name + ":");
    
    // 3. Remove all lease hints for this queue
    // (Lease hints are keyed by partition_id, so we need to track queue→partitions
    //  or just let them TTL out naturally)
    
    // 4. Broadcast deletion to peers
    broadcast({QUEUE_CONFIG_DELETE, queue_name});
}

// In PartitionIdCache - support prefix removal
void remove_by_prefix(const std::string& prefix) {
    std::unique_lock lock(mutex_);
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->first.queue.find(prefix) == 0) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}
```

### 10. C++ Implementation Notes

#### A. Avoid Flexible Array Members

Flexible array members (`uint8_t payload[]`) are C99, not standard C++.

**Solution:** Use a serializer class instead of casting raw memory:

```cpp
class UDPSyncMessageBuilder {
    std::vector<uint8_t> buffer_;
    
public:
    void set_version(uint8_t v) { /* write to buffer */ }
    void set_type(UDPSyncMessageType t) { /* ... */ }
    void set_payload(const std::vector<uint8_t>& msgpack_data) { /* ... */ }
    void sign(const std::string& secret) { /* compute and write HMAC */ }
    
    const uint8_t* data() const { return buffer_.data(); }
    size_t size() const { return buffer_.size(); }
};

class UDPSyncMessageParser {
    const uint8_t* data_;
    size_t size_;
    
public:
    UDPSyncMessageParser(const uint8_t* data, size_t size);
    
    uint8_t version() const;
    UDPSyncMessageType type() const;
    std::string_view sender_id() const;
    std::string_view session_id() const;
    uint64_t sequence() const;
    std::span<const uint8_t> payload() const;
    bool verify_signature(const std::string& secret) const;
};
```

#### B. Avoid String Allocation in Shard Selection

```cpp
// Bad: Creates temporary string
auto& shard = get_shard(queue + ":" + partition);

// Good: Hash without allocation
size_t shard_idx = (std::hash<std::string_view>{}(queue) ^ 
                    std::hash<std::string_view>{}(partition)) % NUM_SHARDS;
auto& shard = shards_[shard_idx];
```

---

## Success Criteria

### Performance
- [ ] 50%+ reduction in PUSH DB queries (queue config + partition cache)
- [ ] 30%+ reduction in POP DB queries (lease hints)
- [ ] N× reduction in notification traffic (targeted notifications)

### Correctness
- [ ] Zero message loss under any cache state
- [ ] Zero duplicate deliveries under any cache state
- [ ] Lease acquisition always correct (DB authoritative)

### Reliability
- [ ] Server restart: cache repopulates on first access
- [ ] Dead server detection: ~5 seconds (5 missed heartbeats)
- [ ] Network partition: graceful degradation to current behavior

---

## Timeline Estimate

| Phase | Effort | Dependencies |
|-------|--------|--------------|
| Phase 1: Infrastructure | 2-3 days | None |
| Phase 2: Queue Config Cache | 2-3 days | Phase 1 |
| Phase 3: Consumer Presence | 2-3 days | Phase 1 |
| Phase 4: Partition ID Cache | 1-2 days | Phase 1 |
| Phase 5: Lease Hints | 2-3 days | Phases 1, 3 |
| Phase 6: Server Health | 1-2 days | Phase 1 |
| Testing & Hardening | 3-5 days | All phases |

**Total: 2-3 weeks**

---

## Design Decisions

1. **Startup behavior:** Start with empty cache, populate on first access or from notifications. No full sync request on startup - simpler and avoids thundering herd.

2. **Partition ID cache:** LRU cache for ALL accessed partitions. Hot partitions naturally stay cached; cold ones get evicted.

3. **Lease hint TTL:** Dynamic based on queue's `lease_time`. Formula: `lease_time_seconds * 1000 / 5` milliseconds. Example: 300s lease → 60s hint TTL.

4. **Version conflicts:** LAST WRITE WINS. Queue create/update happens in transaction, so conflicts are rare. Higher timestamp wins.

---

## Cache Consistency & Maintenance

### Handling Deleted Partitions

The retention/cleanup jobs periodically delete old partitions. Cached partition IDs could become stale.

**Problem:**
```
1. Cache has: "orders:customer-123" → partition_id "abc-def-123"
2. Retention job: DELETE FROM partitions WHERE id = 'abc-def-123'
3. PUSH tries: INSERT message with partition_id = 'abc-def-123'
4. DB error: Foreign key violation!
```

**Solution: Multi-layer protection**

1. **TTL on partition cache entries**
   - Not just LRU eviction by count, but also by time
   - Entries expire after `QUEEN_CACHE_PARTITION_TTL_MS` (default: 300000 = 5 minutes)
   - Stale entries self-expire even without notifications

2. **Invalidation on DB error**
   - If any operation fails with "partition not found" or FK violation
   - Invalidate that cache entry
   - Re-query DB for partition (may need to recreate)
   - Retry operation

3. **Retention service broadcasts deletion**
   - When retention deletes partitions, broadcast `PARTITION_DELETED` message
   - Receiving servers remove entry from partition cache
   - Best effort - if message lost, TTL handles it

```cpp
// In retention service, after deleting partition
shared_state_->invalidate_partition(queue_name, partition_name);
// This removes from local cache AND broadcasts to peers
```

### Periodic Database Refresh

UDP is unreliable. To ensure eventual consistency, caches must periodically validate against DB.

**Refresh Strategy by Tier:**

| Tier | Strategy | Interval | Why |
|------|----------|----------|-----|
| Queue configs | Full refresh from DB | 60 seconds | Small dataset (~100KB), critical for correctness |
| Consumer presence | No DB refresh | N/A | Live state from poll registry, not persisted |
| Partition IDs | TTL expiration | 5 min TTL | Self-invalidating, DB queried on miss |
| Lease hints | TTL expiration | lease_time/5 | Self-invalidating, DB is authoritative anyway |
| Server health | Heartbeats | 1 second | Real-time, detect dead servers in ~5 seconds |

**Queue Config Refresh (Background Job):**

```cpp
void SharedStateManager::start_background_refresh() {
    refresh_thread_ = std::thread([this] {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            
            if (!running_) break;
            
            try {
                // Query all queue configs from DB
                auto configs = query_all_queue_configs_from_db();
                
                // Update local cache
                for (const auto& config : configs) {
                    queue_configs_.set(config.name, config);
                }
                
                // Remove deleted queues (in cache but not in DB)
                auto cached_names = queue_configs_.keys();
                for (const auto& name : cached_names) {
                    if (!configs.contains(name)) {
                        queue_configs_.remove(name);
                    }
                }
                
                spdlog::debug("SharedState: Refreshed {} queue configs from DB", configs.size());
            } catch (const std::exception& e) {
                spdlog::warn("SharedState: Failed to refresh queue configs: {}", e.what());
            }
        }
    });
}
```

### Cache Entry Lifecycle

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    PARTITION ID CACHE ENTRY LIFECYCLE                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────────┐  │
│  │ Cache    │     │ Entry    │     │ Entry    │     │ Entry        │  │
│  │ Miss     │────→│ Created  │────→│ Active   │────→│ Expired/     │  │
│  │          │     │          │     │          │     │ Evicted      │  │
│  └──────────┘     └──────────┘     └──────────┘     └──────────────┘  │
│       │                                  │                  │          │
│       │                                  │                  │          │
│       ↓                                  ↓                  ↓          │
│  Query DB                         Access resets       Remove from      │
│  Cache result                     LRU position        cache            │
│                                                                         │
│  Expiration triggers:                                                   │
│  1. TTL exceeded (5 minutes)                                           │
│  2. LRU eviction (cache full)                                          │
│  3. DB error (partition not found)                                     │
│  4. PARTITION_DELETED message received                                 │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Appendix: Data Structures

### QueueConfig

```cpp
struct QueueConfig {
    std::string id;
    std::string name;
    int lease_time = 300;
    int retry_limit = 3;
    int retry_delay = 1000;
    int delayed_processing = 0;
    int window_buffer = 0;
    bool encryption_enabled = false;
    bool dlq_enabled = false;
    bool dlq_after_max_retries = false;
    int max_size = 10000;
    int ttl = 3600;
    int priority = 0;
    std::string namespace_name;
    std::string task;
    uint64_t version = 0;  // For conflict resolution
};
```

### LRU Cache

```cpp
template<typename K, typename V>
class LRUCache {
public:
    LRUCache(size_t max_size);
    
    std::optional<V> get(const K& key);
    void put(const K& key, const V& value);
    void remove(const K& key);
    void clear();
    
    size_t size() const;
    size_t max_size() const;
    size_t evictions() const;
    
private:
    std::list<std::pair<K, V>> items_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> lookup_;
    size_t max_size_;
    std::atomic<size_t> evictions_{0};
    mutable std::shared_mutex mutex_;
};
```

### Consumer Presence

```cpp
class ConsumerPresenceCache {
public:
    std::set<std::string> get_servers_for_queue(const std::string& queue);
    void register_consumer(const std::string& queue, const std::string& server_id);
    void deregister_consumer(const std::string& queue, const std::string& server_id);
    void clear_server(const std::string& server_id);  // Called when server dies
    
private:
    // queue → set<server_id>
    std::unordered_map<std::string, std::set<std::string>> presence_;
    mutable std::shared_mutex mutex_;
};
```

