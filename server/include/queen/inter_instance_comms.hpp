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

namespace queen {

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
 * Parsed peer URL components
 */
struct PeerEndpoint {
    std::string host;
    int port;
    std::string original_url;
};

/**
 * Inter-Instance Communication Manager
 * 
 * Manages HTTP-based communication with peer Queen servers for real-time
 * notification of message availability and partition freeing.
 * 
 * When QUEEN_PEERS is configured:
 * - On PUSH: Notifies peers via HTTP POST of new message availability
 * - On ACK: Notifies peers via HTTP POST that partition is free for consumption
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
    // INBOUND: Handle notifications from peers (via HTTP route)
    // ============================================================
    
    /**
     * Process incoming notification from a peer server
     * Resets backoff state for matching poll intentions
     */
    void handle_peer_notification(const PeerNotification& notification);
    
    // ============================================================
    // Stats & Monitoring
    // ============================================================
    
    size_t configured_peer_count() const { return peer_endpoints_.size(); }
    nlohmann::json get_stats() const;

private:
    // Configuration
    bool enabled_;
    std::vector<PeerEndpoint> peer_endpoints_;
    int batch_ms_;
    std::string own_host_;
    int own_port_;
    std::string own_hostname_;  // System hostname for K8s self-detection
    
    // Registry to notify on incoming messages
    std::shared_ptr<PollIntentionRegistry> poll_registry_;
    
    // Notification batching
    std::queue<PeerNotification> pending_notifications_;
    std::set<std::pair<std::string, std::string>> pending_dedup_;  // For deduplication
    std::mutex notification_mutex_;
    std::condition_variable notification_cv_;
    std::thread batch_thread_;
    std::atomic<bool> running_{false};
    
    // Stats
    std::atomic<uint64_t> notifications_sent_{0};
    std::atomic<uint64_t> notifications_received_{0};
    std::atomic<uint64_t> notifications_batched_{0};
    std::atomic<uint64_t> http_errors_{0};
    
    // Internal methods
    void batch_and_send_loop();
    void send_to_peers(const std::vector<PeerNotification>& batch);
    bool is_self_url(const std::string& url) const;
    static PeerEndpoint parse_peer_url(const std::string& url);
};

// Global instance (set in acceptor_server.cpp)
extern std::shared_ptr<InterInstanceComms> global_inter_instance_comms;

} // namespace queen
