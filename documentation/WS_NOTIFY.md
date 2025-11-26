# WS_NOTIFY: Inter-Instance WebSocket Notification System

**Status: ✅ IMPLEMENTED**

## Overview

This document describes the WebSocket-based peer notification system for Queen server, enabling near-instant message availability detection across multiple server instances.

**Goal**: When Server A pushes a message or acknowledges a message, Server B's poll workers wake up immediately instead of waiting for their backoff interval (up to 2000ms).

---

## Table of Contents

1. [Environment Variables](#1-environment-variables)
2. [Configuration Changes](#2-configuration-changes)
3. [New Files](#3-new-files)
4. [PollIntentionRegistry Enhancement](#4-pollintentionregistry-enhancement)
5. [InterInstanceComms Implementation](#5-interinstancecomms-implementation)
6. [WebSocket Endpoint](#6-websocket-endpoint)
7. [Integration into Push Route](#7-integration-into-push-route)
8. [Integration into Ack Route](#8-integration-into-ack-route)
9. [Server Initialization](#9-server-initialization)
10. [Health & Metrics](#10-health--metrics)
11. [Testing](#11-testing)

---

## 1. Environment Variables

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `QUEEN_PEERS` | string | `""` | Comma-separated peer URLs (e.g., `http://queen2:6632,http://queen3:6632`). When set, enables WebSocket peer notifications. |
| `WS_NOTIFY_RECONNECT_MS` | int | `5000` | Reconnect interval after peer disconnect |
| `WS_NOTIFY_BATCH_MS` | int | `5` | Batch notifications for N ms before sending (0 = immediate) |

**How it works:**
- **Local notification** (always active): On push/ack, the server immediately notifies its own poll workers
- **Peer notification** (when `QUEEN_PEERS` is set): Also broadcasts to peer servers via WebSocket

**Single server (default):**
```bash
# Local poll worker notification is automatic - no config needed
./bin/queen-server
```

**Cluster setup:**
```bash
# Just define peers - peer notification activates automatically
export QUEEN_PEERS="http://queen-server-2:6632,http://queen-server-3:6632"
./bin/queen-server
```

---

## 2. Configuration Changes

### File: `include/queen/config.hpp`

Add new config struct after `FileBufferConfig`:

```cpp
struct InterInstanceConfig {
    std::string peers = "";
    int reconnect_ms = 5000;
    int batch_ms = 5;
    
    static InterInstanceConfig from_env() {
        InterInstanceConfig config;
        config.peers = get_env_string("QUEEN_PEERS", "");
        config.reconnect_ms = get_env_int("WS_NOTIFY_RECONNECT_MS", 5000);
        config.batch_ms = get_env_int("WS_NOTIFY_BATCH_MS", 5);
        return config;
    }
    
    bool has_peers() const { return !peers.empty(); }
    
    // Parse comma-separated peer URLs into vector
    std::vector<std::string> parse_peer_urls() const {
        std::vector<std::string> urls;
        if (peers.empty()) return urls;
        
        std::stringstream ss(peers);
        std::string url;
        while (std::getline(ss, url, ',')) {
            // Trim whitespace
            size_t start = url.find_first_not_of(" \t");
            size_t end = url.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                urls.push_back(url.substr(start, end - start + 1));
            }
        }
        return urls;
    }
};
```

Add to `Config` struct:

```cpp
struct Config {
    ServerConfig server;
    DatabaseConfig database;
    QueueConfig queue;
    SystemEventsConfig system_events;
    JobsConfig jobs;
    WebSocketConfig websocket;
    EncryptionConfig encryption;
    ClientConfig client;
    ApiConfig api;
    AnalyticsConfig analytics;
    MonitoringConfig monitoring;
    LoggingConfig logging;
    FileBufferConfig file_buffer;
    InterInstanceConfig inter_instance;  // ADD THIS
    
    static Config load() {
        Config config;
        // ... existing loads ...
        config.inter_instance = InterInstanceConfig::from_env();  // ADD THIS
        return config;
    }
};
```

---

## 3. New Files

Create these new files:

```
server/
├── include/queen/
│   └── inter_instance_comms.hpp    # New: Header file
└── src/services/
    └── inter_instance_comms.cpp    # New: Implementation
```

---

## 4. PollIntentionRegistry Enhancement

### File: `include/queen/poll_intention_registry.hpp`

Add methods to allow external backoff reset:

```cpp
class PollIntentionRegistry {
private:
    std::unordered_map<std::string, PollIntention> intentions_;
    std::unordered_set<std::string> in_flight_groups_;
    
    // ADD: Backoff state storage (moved from poll_worker.cpp local variables)
    struct GroupBackoffState {
        int consecutive_empty_pops = 0;
        int current_interval_ms;
        std::chrono::steady_clock::time_point last_accessed;
        
        GroupBackoffState(int base_interval = 100) 
            : consecutive_empty_pops(0), current_interval_ms(base_interval),
              last_accessed(std::chrono::steady_clock::now()) {}
    };
    std::unordered_map<std::string, GroupBackoffState> backoff_states_;  // ADD
    int base_poll_interval_ms_ = 100;  // ADD: configurable base interval
    
    mutable std::mutex mutex_;
    std::atomic<bool> running_{true};
    
public:
    // ... existing methods ...
    
    // ADD: Set base poll interval (called during init)
    void set_base_poll_interval(int ms) {
        base_poll_interval_ms_ = ms;
    }
    
    // ADD: Get/update backoff state for a group (called by poll workers)
    GroupBackoffState& get_backoff_state(const std::string& group_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backoff_states_.find(group_key);
        if (it == backoff_states_.end()) {
            backoff_states_.emplace(group_key, GroupBackoffState(base_poll_interval_ms_));
            return backoff_states_[group_key];
        }
        it->second.last_accessed = std::chrono::steady_clock::now();
        return it->second;
    }
    
    // ADD: Update backoff state after pop result
    void update_backoff_state(const std::string& group_key, bool had_messages,
                              int backoff_threshold, double backoff_multiplier, 
                              int max_poll_interval) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backoff_states_.find(group_key);
        if (it == backoff_states_.end()) return;
        
        auto& state = it->second;
        state.last_accessed = std::chrono::steady_clock::now();
        
        if (had_messages) {
            // Reset backoff on success
            state.consecutive_empty_pops = 0;
            state.current_interval_ms = base_poll_interval_ms_;
        } else {
            // Increase backoff on empty
            state.consecutive_empty_pops++;
            if (state.consecutive_empty_pops >= backoff_threshold) {
                state.current_interval_ms = std::min(
                    static_cast<int>(state.current_interval_ms * backoff_multiplier),
                    max_poll_interval
                );
            }
        }
    }
    
    // ADD: Reset backoff for specific group (called by InterInstanceComms)
    void reset_backoff_for_group(const std::string& group_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backoff_states_.find(group_key);
        if (it != backoff_states_.end()) {
            it->second.consecutive_empty_pops = 0;
            it->second.current_interval_ms = base_poll_interval_ms_;
            it->second.last_accessed = std::chrono::steady_clock::now();
            spdlog::debug("WS_NOTIFY: Reset backoff for group '{}'", group_key);
        }
    }
    
    // ADD: Reset backoff for all groups matching queue/partition pattern
    // Used when we receive a notification but don't know exact consumer group
    void reset_backoff_for_queue_partition(const std::string& queue_name,
                                           const std::string& partition_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string prefix = queue_name + ":" + partition_name + ":";
        std::string wildcard_prefix = queue_name + ":*:";
        
        int reset_count = 0;
        for (auto& [key, state] : backoff_states_) {
            // Match exact partition or wildcard partition patterns
            if (key.rfind(prefix, 0) == 0 || key.rfind(wildcard_prefix, 0) == 0) {
                state.consecutive_empty_pops = 0;
                state.current_interval_ms = base_poll_interval_ms_;
                state.last_accessed = std::chrono::steady_clock::now();
                reset_count++;
            }
        }
        
        if (reset_count > 0) {
            spdlog::debug("WS_NOTIFY: Reset backoff for {} groups matching {}:{}", 
                         reset_count, queue_name, partition_name);
        }
    }
    
    // ADD: Get current interval for a group (used by poll workers)
    int get_current_interval(const std::string& group_key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = backoff_states_.find(group_key);
        if (it == backoff_states_.end()) {
            return base_poll_interval_ms_;
        }
        return it->second.current_interval_ms;
    }
    
    // ADD: Cleanup old backoff states
    void cleanup_backoff_states(int max_age_seconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        
        for (auto it = backoff_states_.begin(); it != backoff_states_.end();) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - it->second.last_accessed).count();
            if (age > max_age_seconds) {
                it = backoff_states_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // ADD: Get all active group keys (for debugging/metrics)
    std::vector<std::string> get_active_group_keys() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> keys;
        for (const auto& [key, _] : backoff_states_) {
            keys.push_back(key);
        }
        return keys;
    }
};
```

### File: `src/services/poll_intention_registry.cpp`

The implementations are inline in the header above, but if you prefer separate .cpp:

```cpp
#include "queen/poll_intention_registry.hpp"
#include <spdlog/spdlog.h>

namespace queen {

// Implementations go here if not inline

} // namespace queen
```

---

## 5. InterInstanceComms Implementation

### File: `include/queen/inter_instance_comms.hpp`

```cpp
#pragma once

#include "queen/poll_intention_registry.hpp"
#include "queen/stream_poll_intention_registry.hpp"
#include "queen/config.hpp"
#include <json.hpp>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <App.h>

namespace queen {

/**
 * Notification types for inter-instance communication
 */
enum class NotificationType {
    MESSAGE_AVAILABLE,  // New message pushed to queue/partition
    PARTITION_FREE,     // Partition acknowledged (consumer slot available)
    HEARTBEAT           // Keep-alive
};

/**
 * Notification message structure
 */
struct PeerNotification {
    NotificationType type;
    std::string queue_name;
    std::string partition_name;
    std::string consumer_group;  // Only for PARTITION_FREE
    int64_t timestamp_ms;
    
    nlohmann::json to_json() const {
        nlohmann::json j;
        switch (type) {
            case NotificationType::MESSAGE_AVAILABLE:
                j["type"] = "message_available";
                break;
            case NotificationType::PARTITION_FREE:
                j["type"] = "partition_free";
                j["consumer_group"] = consumer_group;
                break;
            case NotificationType::HEARTBEAT:
                j["type"] = "heartbeat";
                break;
        }
        j["queue"] = queue_name;
        j["partition"] = partition_name;
        j["ts"] = timestamp_ms;
        return j;
    }
    
    static PeerNotification from_json(const nlohmann::json& j) {
        PeerNotification n;
        std::string type_str = j.value("type", "");
        
        if (type_str == "message_available") {
            n.type = NotificationType::MESSAGE_AVAILABLE;
        } else if (type_str == "partition_free") {
            n.type = NotificationType::PARTITION_FREE;
            n.consumer_group = j.value("consumer_group", "__QUEUE_MODE__");
        } else {
            n.type = NotificationType::HEARTBEAT;
        }
        
        n.queue_name = j.value("queue", "");
        n.partition_name = j.value("partition", "");
        n.timestamp_ms = j.value("ts", 0);
        return n;
    }
};

/**
 * Per-peer WebSocket connection data
 */
struct PeerConnectionData {
    std::string peer_url;
    bool authenticated = false;
};

/**
 * Inter-Instance Communication Manager
 * 
 * Manages WebSocket connections to peer Queen servers for real-time
 * notification of message availability and partition freeing.
 */
class InterInstanceComms : public std::enable_shared_from_this<InterInstanceComms> {
public:
    InterInstanceComms(
        const InterInstanceConfig& config,
        std::shared_ptr<PollIntentionRegistry> poll_registry,
        std::shared_ptr<StreamPollIntentionRegistry> stream_registry,
        const std::string& own_host,
        int own_port
    );
    
    ~InterInstanceComms();
    
    // Lifecycle
    void start();
    void stop();
    bool is_enabled() const { return enabled_; }
    
    // ============================================================
    // OUTBOUND: Send notifications to peers (called after PUSH/ACK)
    // ============================================================
    
    /**
     * Notify peers that a new message is available
     * Called after successful PUSH
     */
    void notify_message_available(
        const std::string& queue_name,
        const std::string& partition_name
    );
    
    /**
     * Notify peers that a partition is free (lease released)
     * Called after successful ACK
     */
    void notify_partition_free(
        const std::string& queue_name,
        const std::string& partition_name,
        const std::string& consumer_group
    );
    
    // ============================================================
    // INBOUND: Handle notifications from peers
    // ============================================================
    
    /**
     * Process incoming notification from a peer server
     * Resets backoff state for matching poll intentions
     */
    void handle_peer_notification(const PeerNotification& notification);
    
    /**
     * Register WebSocket for inbound peer (called from WS handler)
     */
    template<bool SSL>
    void register_inbound_peer(uWS::WebSocket<SSL, true, PeerConnectionData>* ws, 
                               const std::string& peer_url);
    
    /**
     * Unregister WebSocket for inbound peer (called on close)
     */
    template<bool SSL>
    void unregister_inbound_peer(uWS::WebSocket<SSL, true, PeerConnectionData>* ws);
    
    // ============================================================
    // Stats & Monitoring
    // ============================================================
    
    size_t connected_outbound_peers() const;
    size_t connected_inbound_peers() const;
    nlohmann::json get_stats() const;

private:
    // Configuration
    bool enabled_;
    std::vector<std::string> peer_urls_;
    int reconnect_ms_;
    int batch_ms_;
    std::string own_url_;  // To avoid connecting to self
    
    // Registries to notify on incoming messages
    std::shared_ptr<PollIntentionRegistry> poll_registry_;
    std::shared_ptr<StreamPollIntentionRegistry> stream_registry_;
    
    // Outbound connections (we connect to peers)
    struct OutboundPeer {
        std::string url;
        std::atomic<bool> connected{false};
        std::thread connect_thread;
        std::atomic<bool> should_stop{false};
        // WebSocket pointer managed by uWS
    };
    std::vector<std::unique_ptr<OutboundPeer>> outbound_peers_;
    
    // Inbound connections (peers connect to us)
    std::unordered_set<void*> inbound_peer_sockets_;
    mutable std::mutex inbound_mutex_;
    
    // Notification batching
    std::queue<PeerNotification> pending_notifications_;
    std::mutex notification_mutex_;
    std::condition_variable notification_cv_;
    std::thread batch_thread_;
    std::atomic<bool> running_{false};
    
    // Stats
    std::atomic<uint64_t> notifications_sent_{0};
    std::atomic<uint64_t> notifications_received_{0};
    std::atomic<uint64_t> notifications_dropped_{0};
    
    // Internal methods
    void batch_and_send_loop();
    void broadcast_to_peers(const std::vector<PeerNotification>& notifications);
    void connect_to_peer(OutboundPeer& peer);
    bool is_self_url(const std::string& url) const;
};

// Global instance (set in acceptor_server.cpp)
extern std::shared_ptr<InterInstanceComms> global_inter_instance_comms;

} // namespace queen
```

### File: `src/services/inter_instance_comms.cpp`

```cpp
#include "queen/inter_instance_comms.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

namespace queen {

// Global instance
std::shared_ptr<InterInstanceComms> global_inter_instance_comms;

InterInstanceComms::InterInstanceComms(
    const InterInstanceConfig& config,
    std::shared_ptr<PollIntentionRegistry> poll_registry,
    std::shared_ptr<StreamPollIntentionRegistry> stream_registry,
    const std::string& own_host,
    int own_port
) : enabled_(config.use_ws_notify),
    reconnect_ms_(config.reconnect_ms),
    batch_ms_(config.batch_ms),
    poll_registry_(poll_registry),
    stream_registry_(stream_registry)
{
    // Build own URL for self-detection
    own_url_ = "http://" + own_host + ":" + std::to_string(own_port);
    
    // Parse peer URLs, excluding self
    for (const auto& url : config.parse_peer_urls()) {
        if (!is_self_url(url)) {
            peer_urls_.push_back(url);
        }
    }
    
    spdlog::info("InterInstanceComms: enabled={}, peers={}, own_url={}", 
                 enabled_, peer_urls_.size(), own_url_);
}

InterInstanceComms::~InterInstanceComms() {
    stop();
}

bool InterInstanceComms::is_self_url(const std::string& url) const {
    // Simple check - could be improved with hostname resolution
    return url == own_url_ || 
           url.find("localhost:" + std::to_string(own_port_)) != std::string::npos ||
           url.find("127.0.0.1:" + std::to_string(own_port_)) != std::string::npos;
}

void InterInstanceComms::start() {
    if (!enabled_ || peer_urls_.empty()) {
        spdlog::info("InterInstanceComms: Not starting (enabled={}, peers={})", 
                     enabled_, peer_urls_.size());
        return;
    }
    
    running_ = true;
    
    // Start batch sending thread
    batch_thread_ = std::thread(&InterInstanceComms::batch_and_send_loop, this);
    
    // Initialize outbound peer connections
    // Note: Actual WebSocket client connections would use a WS client library
    // For now, we'll use HTTP POST as a simpler alternative
    // TODO: Replace with proper WebSocket client (e.g., libwebsockets or uWS client)
    
    spdlog::info("InterInstanceComms: Started with {} peer URLs", peer_urls_.size());
}

void InterInstanceComms::stop() {
    if (!running_) return;
    
    running_ = false;
    notification_cv_.notify_all();
    
    if (batch_thread_.joinable()) {
        batch_thread_.join();
    }
    
    // Stop outbound connections
    for (auto& peer : outbound_peers_) {
        peer->should_stop = true;
        if (peer->connect_thread.joinable()) {
            peer->connect_thread.join();
        }
    }
    
    spdlog::info("InterInstanceComms: Stopped");
}

// ============================================================
// OUTBOUND: Send notifications
// ============================================================

void InterInstanceComms::notify_message_available(
    const std::string& queue_name,
    const std::string& partition_name
) {
    if (!enabled_ || !running_) return;
    
    PeerNotification n;
    n.type = NotificationType::MESSAGE_AVAILABLE;
    n.queue_name = queue_name;
    n.partition_name = partition_name;
    n.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    {
        std::lock_guard<std::mutex> lock(notification_mutex_);
        
        // Deduplicate: don't queue if same queue/partition already pending
        bool duplicate = false;
        // Simple O(n) check - could optimize with set if queue gets large
        std::queue<PeerNotification> temp;
        while (!pending_notifications_.empty()) {
            auto& existing = pending_notifications_.front();
            if (existing.type == n.type && 
                existing.queue_name == n.queue_name &&
                existing.partition_name == n.partition_name) {
                duplicate = true;
            }
            temp.push(std::move(pending_notifications_.front()));
            pending_notifications_.pop();
        }
        pending_notifications_ = std::move(temp);
        
        if (!duplicate) {
            pending_notifications_.push(std::move(n));
        }
    }
    
    notification_cv_.notify_one();
}

void InterInstanceComms::notify_partition_free(
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group
) {
    if (!enabled_ || !running_) return;
    
    PeerNotification n;
    n.type = NotificationType::PARTITION_FREE;
    n.queue_name = queue_name;
    n.partition_name = partition_name;
    n.consumer_group = consumer_group;
    n.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    {
        std::lock_guard<std::mutex> lock(notification_mutex_);
        pending_notifications_.push(std::move(n));
    }
    
    notification_cv_.notify_one();
}

void InterInstanceComms::batch_and_send_loop() {
    spdlog::info("InterInstanceComms: Batch thread started (batch_ms={})", batch_ms_);
    
    while (running_) {
        std::vector<PeerNotification> batch;
        
        {
            std::unique_lock<std::mutex> lock(notification_mutex_);
            
            // Wait for notifications or timeout
            if (batch_ms_ > 0) {
                notification_cv_.wait_for(lock, std::chrono::milliseconds(batch_ms_),
                    [this] { return !pending_notifications_.empty() || !running_; });
            } else {
                notification_cv_.wait(lock,
                    [this] { return !pending_notifications_.empty() || !running_; });
            }
            
            if (!running_) break;
            
            // Drain queue into batch
            while (!pending_notifications_.empty()) {
                batch.push_back(std::move(pending_notifications_.front()));
                pending_notifications_.pop();
            }
        }
        
        if (!batch.empty()) {
            broadcast_to_peers(batch);
        }
    }
    
    spdlog::info("InterInstanceComms: Batch thread stopped");
}

void InterInstanceComms::broadcast_to_peers(const std::vector<PeerNotification>& notifications) {
    // Broadcast to inbound peers (they connected to us)
    {
        std::lock_guard<std::mutex> lock(inbound_mutex_);
        
        for (void* ws_ptr : inbound_peer_sockets_) {
            try {
                auto* ws = static_cast<uWS::WebSocket<false, true, PeerConnectionData>*>(ws_ptr);
                
                for (const auto& n : notifications) {
                    std::string msg = n.to_json().dump();
                    ws->send(msg, uWS::OpCode::TEXT);
                    notifications_sent_++;
                }
            } catch (const std::exception& e) {
                spdlog::warn("InterInstanceComms: Failed to send to inbound peer: {}", e.what());
            }
        }
    }
    
    // For outbound peers, we need WebSocket client implementation
    // This is a placeholder - in production, use a proper WS client library
    // Alternative: Use HTTP POST to /internal/api/notify endpoint
    
    spdlog::debug("InterInstanceComms: Broadcast {} notifications to {} inbound peers",
                 notifications.size(), inbound_peer_sockets_.size());
}

// ============================================================
// INBOUND: Handle notifications from peers
// ============================================================

void InterInstanceComms::handle_peer_notification(const PeerNotification& notification) {
    notifications_received_++;
    
    switch (notification.type) {
        case NotificationType::MESSAGE_AVAILABLE:
            // New message available - reset backoff for matching groups
            spdlog::debug("InterInstanceComms: Received MESSAGE_AVAILABLE for {}:{}",
                         notification.queue_name, notification.partition_name);
            
            poll_registry_->reset_backoff_for_queue_partition(
                notification.queue_name,
                notification.partition_name
            );
            
            // Also reset stream poll registry if applicable
            if (stream_registry_) {
                // Similar method would be needed on stream registry
            }
            break;
            
        case NotificationType::PARTITION_FREE:
            // Partition freed - reset backoff for specific consumer group
            spdlog::debug("InterInstanceComms: Received PARTITION_FREE for {}:{}:{}",
                         notification.queue_name, notification.partition_name,
                         notification.consumer_group);
            
            {
                std::string group_key = notification.queue_name + ":" +
                                       notification.partition_name + ":" +
                                       notification.consumer_group;
                poll_registry_->reset_backoff_for_group(group_key);
            }
            break;
            
        case NotificationType::HEARTBEAT:
            // Just a keep-alive, nothing to do
            break;
    }
}

template<bool SSL>
void InterInstanceComms::register_inbound_peer(
    uWS::WebSocket<SSL, true, PeerConnectionData>* ws,
    const std::string& peer_url
) {
    std::lock_guard<std::mutex> lock(inbound_mutex_);
    inbound_peer_sockets_.insert(static_cast<void*>(ws));
    spdlog::info("InterInstanceComms: Registered inbound peer from {}", peer_url);
}

template<bool SSL>
void InterInstanceComms::unregister_inbound_peer(
    uWS::WebSocket<SSL, true, PeerConnectionData>* ws
) {
    std::lock_guard<std::mutex> lock(inbound_mutex_);
    inbound_peer_sockets_.erase(static_cast<void*>(ws));
    spdlog::info("InterInstanceComms: Unregistered inbound peer");
}

// Explicit template instantiations
template void InterInstanceComms::register_inbound_peer<false>(
    uWS::WebSocket<false, true, PeerConnectionData>*, const std::string&);
template void InterInstanceComms::unregister_inbound_peer<false>(
    uWS::WebSocket<false, true, PeerConnectionData>*);

// ============================================================
// Stats
// ============================================================

size_t InterInstanceComms::connected_outbound_peers() const {
    size_t count = 0;
    for (const auto& peer : outbound_peers_) {
        if (peer->connected) count++;
    }
    return count;
}

size_t InterInstanceComms::connected_inbound_peers() const {
    std::lock_guard<std::mutex> lock(inbound_mutex_);
    return inbound_peer_sockets_.size();
}

nlohmann::json InterInstanceComms::get_stats() const {
    return {
        {"enabled", enabled_},
        {"configured_peers", peer_urls_.size()},
        {"connected_outbound", connected_outbound_peers()},
        {"connected_inbound", connected_inbound_peers()},
        {"notifications_sent", notifications_sent_.load()},
        {"notifications_received", notifications_received_.load()},
        {"notifications_dropped", notifications_dropped_.load()}
    };
}

} // namespace queen
```

---

## 6. WebSocket Endpoint

### File: `src/routes/internal.cpp` (NEW FILE)

```cpp
#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/inter_instance_comms.hpp"
#include <spdlog/spdlog.h>

namespace queen {
namespace routes {

void setup_internal_routes(uWS::App* app, const RouteContext& ctx) {
    
    // WebSocket endpoint for peer-to-peer notifications
    app->ws<PeerConnectionData>("/internal/ws/peer", {
        .compression = uWS::DISABLED,
        .maxPayloadLength = 16 * 1024,  // 16KB max message
        .idleTimeout = 120,  // 2 minute timeout (heartbeats should prevent)
        .maxBackpressure = 1 * 1024 * 1024,  // 1MB backpressure limit
        
        .upgrade = [](auto* res, auto* req, auto* context) {
            // Extract peer identifier from query or header
            std::string peer_url = std::string(req->getQuery("peer_url"));
            
            spdlog::info("WS_NOTIFY: Peer upgrade request from {}", 
                        peer_url.empty() ? "unknown" : peer_url);
            
            // Upgrade the connection
            res->template upgrade<PeerConnectionData>(
                PeerConnectionData{peer_url, true},
                req->getHeader("sec-websocket-key"),
                req->getHeader("sec-websocket-protocol"),
                req->getHeader("sec-websocket-extensions"),
                context
            );
        },
        
        .open = [](auto* ws) {
            auto* data = ws->getUserData();
            spdlog::info("WS_NOTIFY: Peer connected from {}", data->peer_url);
            
            if (global_inter_instance_comms) {
                global_inter_instance_comms->register_inbound_peer(ws, data->peer_url);
            }
        },
        
        .message = [](auto* ws, std::string_view message, uWS::OpCode opCode) {
            if (opCode != uWS::OpCode::TEXT) return;
            
            try {
                auto j = nlohmann::json::parse(message);
                auto notification = PeerNotification::from_json(j);
                
                if (global_inter_instance_comms) {
                    global_inter_instance_comms->handle_peer_notification(notification);
                }
            } catch (const std::exception& e) {
                spdlog::warn("WS_NOTIFY: Failed to parse peer message: {}", e.what());
            }
        },
        
        .drain = [](auto* ws) {
            // Backpressure relief - could log if needed
        },
        
        .ping = [](auto* ws, std::string_view message) {
            // Automatic pong is sent by uWS
        },
        
        .pong = [](auto* ws, std::string_view message) {
            // Peer is alive
        },
        
        .close = [](auto* ws, int code, std::string_view message) {
            auto* data = ws->getUserData();
            spdlog::info("WS_NOTIFY: Peer disconnected from {} (code={})", 
                        data->peer_url, code);
            
            if (global_inter_instance_comms) {
                global_inter_instance_comms->unregister_inbound_peer(ws);
            }
        }
    });
    
    // HTTP fallback endpoint for peers that can't maintain WebSocket
    // Useful for: load balancer health checks, debugging, simple integrations
    app->post("/internal/api/notify", [ctx](auto* res, auto* req) {
        read_json_body(res,
            [res](const nlohmann::json& body) {
                try {
                    auto notification = PeerNotification::from_json(body);
                    
                    if (global_inter_instance_comms) {
                        global_inter_instance_comms->handle_peer_notification(notification);
                    }
                    
                    send_json_response(res, {{"status", "ok"}}, 200);
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 400);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // Stats endpoint
    app->get("/internal/api/ws-notify/stats", [ctx](auto* res, auto* req) {
        (void)req;
        
        nlohmann::json stats;
        if (global_inter_instance_comms) {
            stats = global_inter_instance_comms->get_stats();
        } else {
            stats = {{"enabled", false}};
        }
        
        send_json_response(res, stats, 200);
    });
}

} // namespace routes
} // namespace queen
```

### Update Route Registry

**File: `include/queen/routes/route_registry.hpp`**

Add declaration:

```cpp
namespace queen {
namespace routes {

// ... existing declarations ...

void setup_internal_routes(uWS::App* app, const RouteContext& ctx);  // ADD

} // namespace routes
} // namespace queen
```

---

## 7. Integration into Push Route

### File: `src/routes/push.cpp`

Add notification after successful push:

```cpp
#include "queen/inter_instance_comms.hpp"  // ADD at top

// Inside setup_push_routes, after successful push:

void setup_push_routes(uWS::App* app, const RouteContext& ctx) {
    app->post("/api/v1/push", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    // ... existing validation and push logic ...
                    
                    auto results = ctx.async_queue_manager->push_messages(items);
                    
                    // ============================================
                    // ADD: Notify peers of new messages
                    // ============================================
                    if (global_inter_instance_comms && 
                        global_inter_instance_comms->is_enabled()) {
                        
                        // Collect unique queue/partition combinations
                        std::set<std::pair<std::string, std::string>> notified;
                        
                        for (size_t i = 0; i < results.size(); i++) {
                            if (results[i].status == "created") {
                                auto key = std::make_pair(items[i].queue, items[i].partition);
                                if (notified.find(key) == notified.end()) {
                                    global_inter_instance_comms->notify_message_available(
                                        items[i].queue,
                                        items[i].partition
                                    );
                                    notified.insert(key);
                                }
                            }
                        }
                    }
                    // ============================================
                    
                    // ... existing response logic ...
                    
                } catch (const std::exception& e) {
                    // ... existing error handling ...
                }
            },
            // ... error callback ...
        );
    });
}
```

---

## 8. Integration into Ack Route

### File: `src/routes/ack.cpp`

Add notification after successful ack:

```cpp
#include "queen/inter_instance_comms.hpp"  // ADD at top

// Inside setup_ack_routes, for single ack:

app->post("/api/v1/ack", [ctx](auto* res, auto* req) {
    (void)req;
    read_json_body(res,
        [res, ctx](const nlohmann::json& body) {
            try {
                // ... existing parsing logic ...
                
                auto ack_result = ctx.async_queue_manager->acknowledge_message(
                    transaction_id, status, error, consumer_group, lease_id, partition_id
                );
                
                // ============================================
                // ADD: Notify peers that partition is free
                // ============================================
                if (ack_result.success && 
                    global_inter_instance_comms && 
                    global_inter_instance_comms->is_enabled() &&
                    partition_id.has_value()) {
                    
                    // We need queue_name - get it from the message or pass it through
                    // Option 1: Query DB for queue name (adds latency)
                    // Option 2: Require queue_name in ack request (breaking change)
                    // Option 3: Return queue_name from acknowledge_message (preferred)
                    
                    // For now, we'll need to enhance acknowledge_message to return
                    // queue/partition info, or look it up. Simplified version:
                    // 
                    // NOTE: This requires modifying AsyncQueueManager::acknowledge_message
                    // to return the queue_name and partition_name in AckResult
                    
                    if (ack_result.queue_name.has_value() && 
                        ack_result.partition_name.has_value()) {
                        global_inter_instance_comms->notify_partition_free(
                            *ack_result.queue_name,
                            *ack_result.partition_name,
                            consumer_group
                        );
                    }
                }
                // ============================================
                
                // ... existing response logic ...
                
            } catch (const std::exception& e) {
                // ... existing error handling ...
            }
        },
        // ... error callback ...
    );
});

// Similarly for batch ack: /api/v1/ack/batch
```

### Required Change to AckResult

**File: `include/queen/async_queue_manager.hpp`**

Enhance `AckResult` to include queue/partition info:

```cpp
struct AckResult {
    bool success;
    std::string message;
    std::optional<std::string> error;
    
    // ADD: For WS_NOTIFY support
    std::optional<std::string> queue_name;
    std::optional<std::string> partition_name;
};
```

**File: `src/managers/async_queue_manager.cpp`**

In `acknowledge_message()`, populate queue_name and partition_name from the query result:

```cpp
// In acknowledge_message, after successful ack query:
result.queue_name = /* queue_name from message lookup */;
result.partition_name = /* partition_name from message lookup */;
```

---

## 9. Server Initialization

### File: `src/acceptor_server.cpp`

Add InterInstanceComms initialization:

```cpp
#include "queen/inter_instance_comms.hpp"  // ADD at top

// In global resources section (near other globals):
// Note: global_inter_instance_comms is declared in inter_instance_comms.hpp

// Inside worker_thread, in the call_once block after creating poll registries:

std::call_once(global_pool_init_flag, [&config, num_workers]() {
    // ... existing initialization ...
    
    // Create global poll intention registry (shared by all workers)
    queen::global_poll_intention_registry = std::make_shared<queen::PollIntentionRegistry>();
    
    // ADD: Set base poll interval for backoff tracking
    queen::global_poll_intention_registry->set_base_poll_interval(config.queue.poll_db_interval);
    
    // ... other registry creation ...
    
    // ============================================
    // ADD: Initialize InterInstanceComms
    // ============================================
    if (config.inter_instance.use_ws_notify) {
        spdlog::info("Initializing Inter-Instance WebSocket Notifications...");
        spdlog::info("  - Peers: {}", config.inter_instance.peers);
        spdlog::info("  - Reconnect interval: {}ms", config.inter_instance.reconnect_ms);
        spdlog::info("  - Batch interval: {}ms", config.inter_instance.batch_ms);
        
        queen::global_inter_instance_comms = std::make_shared<queen::InterInstanceComms>(
            config.inter_instance,
            queen::global_poll_intention_registry,
            queen::global_stream_poll_registry,
            config.server.host,
            config.server.port
        );
        queen::global_inter_instance_comms->start();
        
        spdlog::info("Inter-Instance Comms initialized with {} peers", 
                     config.inter_instance.parse_peer_urls().size());
    }
    // ============================================
    
    // ... rest of initialization ...
});

// In setup_worker_routes, add internal routes:

static void setup_worker_routes(...) {
    // ... existing route setup ...
    
    spdlog::debug("[Worker {}] Setting up internal routes...", worker_id);
    queen::routes::setup_internal_routes(app, ctx);  // ADD
}
```

---

## 10. Health & Metrics

### File: `src/routes/health.cpp`

Add WS_NOTIFY stats to health endpoint:

```cpp
#include "queen/inter_instance_comms.hpp"  // ADD

// In health endpoint response:
nlohmann::json response = {
    {"status", "healthy"},
    // ... existing fields ...
};

// ADD: WS_NOTIFY status
if (global_inter_instance_comms) {
    response["ws_notify"] = global_inter_instance_comms->get_stats();
} else {
    response["ws_notify"] = {{"enabled", false}};
}
```

### File: `src/routes/metrics.cpp`

Add WS_NOTIFY metrics:

```cpp
// In metrics response:
if (global_inter_instance_comms && global_inter_instance_comms->is_enabled()) {
    auto stats = global_inter_instance_comms->get_stats();
    
    // Prometheus-style metrics
    ss << "# HELP queen_ws_notify_peers_connected Number of connected peer servers\n";
    ss << "# TYPE queen_ws_notify_peers_connected gauge\n";
    ss << "queen_ws_notify_peers_connected{direction=\"inbound\"} " 
       << stats["connected_inbound"].get<int>() << "\n";
    ss << "queen_ws_notify_peers_connected{direction=\"outbound\"} " 
       << stats["connected_outbound"].get<int>() << "\n";
    
    ss << "# HELP queen_ws_notify_messages_total Total notifications sent/received\n";
    ss << "# TYPE queen_ws_notify_messages_total counter\n";
    ss << "queen_ws_notify_messages_total{direction=\"sent\"} " 
       << stats["notifications_sent"].get<uint64_t>() << "\n";
    ss << "queen_ws_notify_messages_total{direction=\"received\"} " 
       << stats["notifications_received"].get<uint64_t>() << "\n";
}
```

---

## 11. Testing

### Manual Testing

1. **Start two Queen instances:**

```bash
# Terminal 1: Server A
export QUEEN_PEERS="http://localhost:6633"
export PORT=6632
./bin/queen-server

# Terminal 2: Server B
export QUEEN_PEERS="http://localhost:6632"
export PORT=6633
./bin/queen-server
```

2. **Start a consumer on Server B with long-poll:**

```bash
curl "http://localhost:6633/api/v1/pop/queue/test?wait=true&timeout=30000"
```

3. **Push a message to Server A:**

```bash
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{"items":[{"queue":"test","payload":{"msg":"hello"}}]}'
```

4. **Expected**: The consumer on Server B should receive the message almost immediately (within ~50ms), not after waiting for the full backoff interval.

### Integration Test Script

Create `tests/ws_notify_test.sh`:

```bash
#!/bin/bash

# Start Server A
QUEEN_PEERS="http://localhost:6633" PORT=6632 \
  ./bin/queen-server &
PID_A=$!

# Start Server B
QUEEN_PEERS="http://localhost:6632" PORT=6633 \
  ./bin/queen-server &
PID_B=$!

sleep 3  # Wait for servers to start

# Start consumer on B (background, with timeout)
START=$(date +%s%N)
curl -s "http://localhost:6633/api/v1/pop/queue/wstest?wait=true&timeout=10000" &
CONSUMER_PID=$!

sleep 0.5  # Small delay to ensure consumer is waiting

# Push message to A
curl -s -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{"items":[{"queue":"wstest","payload":{"test":"ws_notify"}}]}'

# Wait for consumer
wait $CONSUMER_PID
END=$(date +%s%N)

# Calculate time in ms
ELAPSED=$(( ($END - $START) / 1000000 ))
echo "Consumer received message in ${ELAPSED}ms"

# Cleanup
kill $PID_A $PID_B 2>/dev/null

# Check if fast enough (should be < 500ms with WS_NOTIFY)
if [ $ELAPSED -lt 500 ]; then
  echo "✓ WS_NOTIFY working correctly"
  exit 0
else
  echo "✗ WS_NOTIFY may not be working (took > 500ms)"
  exit 1
fi
```

---

## Summary

### Files to Create

| File | Description |
|------|-------------|
| `include/queen/inter_instance_comms.hpp` | Header for InterInstanceComms class |
| `src/services/inter_instance_comms.cpp` | Implementation of peer notifications |
| `src/routes/internal.cpp` | WebSocket endpoint `/internal/ws/peer` |

### Files to Modify

| File | Changes |
|------|---------|
| `include/queen/config.hpp` | Add `InterInstanceConfig` struct |
| `include/queen/poll_intention_registry.hpp` | Add backoff state management |
| `include/queen/async_queue_manager.hpp` | Add queue/partition to `AckResult` |
| `src/managers/async_queue_manager.cpp` | Return queue/partition in ack result |
| `src/routes/push.cpp` | Add notification after successful push |
| `src/routes/ack.cpp` | Add notification after successful ack |
| `src/acceptor_server.cpp` | Initialize InterInstanceComms |
| `include/queen/routes/route_registry.hpp` | Declare `setup_internal_routes` |
| `src/routes/health.cpp` | Add WS_NOTIFY stats |
| `src/routes/metrics.cpp` | Add WS_NOTIFY metrics |

### Environment Variables Summary

```bash
QUEEN_PEERS="url1,url2,..."     # Comma-separated peer URLs (enables peer notification)
QUEEN_PEERS="url1,url2,..."     # Comma-separated peer URLs
WS_NOTIFY_RECONNECT_MS=5000     # Reconnect interval
WS_NOTIFY_BATCH_MS=5            # Batch window (0=immediate)
```

---

## Future Improvements

1. **WebSocket Client**: Implement proper outbound WebSocket client connections (currently only inbound works). Consider using `libwebsockets` or building with uWS client mode.

2. **Authentication**: Add shared secret authentication (`WS_NOTIFY_SECRET`) for peer connections.

3. **Compression**: Add optional WebSocket compression for high-volume deployments.

4. **Selective Routing**: Only notify peers that have active intentions for the relevant queue/partition (requires intention sharing).

5. **Metrics Dashboard**: Add WebSocket stats to the webapp dashboard.

