#pragma once

#include "queen/poll_intention_registry.hpp"
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
#include <set>
#include <chrono>
#include <netinet/in.h>  // For sockaddr_in

namespace queen {

// Forward declaration
class SharedStateManager;

// Forward declaration
class StreamPollIntentionRegistry;

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
        n.timestamp_ms = j.value("ts", static_cast<int64_t>(0));
        return n;
    }
};

/**
 * Parsed HTTP peer URL components
 */
struct PeerEndpoint {
    std::string host;
    int port;
    std::string original_url;
};

/**
 * UDP peer endpoint (resolved address)
 */
struct UdpPeerEndpoint {
    std::string hostname;      // Original hostname for logging
    int port;                  // Target port
    sockaddr_in addr;          // Resolved address (includes port)
    bool resolved = false;     // Whether DNS resolution succeeded
};

/**
 * Inter-Instance Communication Manager
 * 
 * Manages communication with peer Queen servers for real-time notification
 * of message availability and partition freeing.
 * 
 * Supports two protocols:
 * - HTTP (QUEEN_PEERS): Batched HTTP POST with guaranteed delivery
 * - UDP (QUEEN_UDP_PEERS): Fire-and-forget with lowest latency
 * 
 * When peers are configured:
 * - On PUSH: Notifies peers of new message availability
 * - On ACK: Notifies peers that partition is free for consumption
 * - Poll workers on peers reset their backoff and query immediately
 */
class InterInstanceComms : public std::enable_shared_from_this<InterInstanceComms> {
public:
    InterInstanceComms(
        const InterInstanceConfig& config,
        std::shared_ptr<PollIntentionRegistry> poll_registry,
        const std::string& own_host,
        int own_port,
        const std::string& own_hostname = ""  // System hostname for K8s self-detection
    );
    
    ~InterInstanceComms();
    
    // Lifecycle
    void start();
    void stop();
    bool is_enabled() const { return http_enabled_ || udp_enabled_; }
    bool is_http_enabled() const { return http_enabled_; }
    bool is_udp_enabled() const { return udp_enabled_; }
    
    // ============================================================
    // OUTBOUND: Send notifications to peers (called after PUSH/ACK)
    // ============================================================
    
    /**
     * Notify peers that a new message is available
     * Called after successful PUSH
     * Sends via UDP (immediate) and/or HTTP (batched) depending on config
     */
    void notify_message_available(
        const std::string& queue_name,
        const std::string& partition_name
    );
    
    /**
     * Notify peers that a partition is free (lease released)
     * Called after successful ACK
     * Sends via UDP (immediate) and/or HTTP (batched) depending on config
     */
    void notify_partition_free(
        const std::string& queue_name,
        const std::string& partition_name,
        const std::string& consumer_group
    );
    
    // ============================================================
    // INBOUND: Handle notifications from peers (via HTTP or UDP)
    // ============================================================
    
    /**
     * Process incoming notification from a peer server
     * Resets backoff state for matching poll intentions
     */
    void handle_peer_notification(const PeerNotification& notification);
    
    // ============================================================
    // Shared State Integration
    // ============================================================
    
    /**
     * Set shared state manager for targeted notifications
     * When set, UDP notifications will use consumer presence cache
     * to send only to servers that have consumers for the queue.
     */
    void set_shared_state(std::shared_ptr<SharedStateManager> shared_state) {
        shared_state_ = shared_state;
    }
    
    // ============================================================
    // Stats & Monitoring
    // ============================================================
    
    size_t http_peer_count() const { return http_endpoints_.size(); }
    size_t udp_peer_count() const { return udp_endpoints_.size(); }
    nlohmann::json get_stats() const;

private:
    // ============================================================
    // HTTP Configuration & State
    // ============================================================
    bool http_enabled_ = false;
    std::vector<PeerEndpoint> http_endpoints_;
    int batch_ms_;
    
    // HTTP Notification batching
    std::queue<PeerNotification> pending_http_notifications_;
    std::set<std::pair<std::string, std::string>> pending_http_dedup_;
    std::mutex http_notification_mutex_;
    std::condition_variable http_notification_cv_;
    std::thread http_batch_thread_;
    
    // ============================================================
    // UDP Configuration & State
    // ============================================================
    bool udp_enabled_ = false;
    std::vector<UdpPeerEndpoint> udp_endpoints_;
    int udp_port_;
    int udp_send_sock_ = -1;     // Socket for sending
    int udp_recv_sock_ = -1;     // Socket for receiving
    std::thread udp_recv_thread_;
    
    // ============================================================
    // Shared State
    // ============================================================
    std::string own_host_;
    int own_port_;
    std::string own_hostname_;
    std::shared_ptr<PollIntentionRegistry> poll_registry_;
    std::shared_ptr<SharedStateManager> shared_state_;  // For targeted notifications
    std::atomic<bool> running_{false};
    
    // ============================================================
    // Stats
    // ============================================================
    // HTTP stats
    std::atomic<uint64_t> http_notifications_sent_{0};
    std::atomic<uint64_t> http_notifications_batched_{0};
    std::atomic<uint64_t> http_errors_{0};
    
    // UDP stats
    std::atomic<uint64_t> udp_notifications_sent_{0};
    std::atomic<uint64_t> udp_notifications_received_{0};
    std::atomic<uint64_t> udp_send_errors_{0};
    
    // Shared stats
    std::atomic<uint64_t> notifications_received_{0};  // Total received (HTTP + UDP)
    
    // ============================================================
    // HTTP Internal Methods
    // ============================================================
    void http_batch_and_send_loop();
    void http_send_to_peers(const std::vector<PeerNotification>& batch);
    void queue_http_notification(const PeerNotification& n);
    bool is_self_http_url(const std::string& url) const;
    static PeerEndpoint parse_peer_url(const std::string& url);
    
    // ============================================================
    // UDP Internal Methods
    // ============================================================
    bool init_udp_sockets();
    void resolve_udp_peers(const std::vector<UdpPeerEntry>& peers);
    void udp_send_notification(const PeerNotification& n);
    void udp_recv_loop();
    bool is_self_udp_peer(const std::string& hostname, int port) const;
    void cleanup_udp_sockets();
};

// Global instance (set in acceptor_server.cpp)
extern std::shared_ptr<InterInstanceComms> global_inter_instance_comms;

} // namespace queen
