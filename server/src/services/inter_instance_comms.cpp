#include "queen/inter_instance_comms.hpp"
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <chrono>
#include <regex>

namespace queen {

// Global instance
std::shared_ptr<InterInstanceComms> global_inter_instance_comms;

PeerEndpoint InterInstanceComms::parse_peer_url(const std::string& url) {
    PeerEndpoint ep;
    ep.original_url = url;
    ep.port = 80;  // default
    
    std::string remaining = url;
    
    // Strip protocol if present
    if (remaining.substr(0, 7) == "http://") {
        remaining = remaining.substr(7);
    } else if (remaining.substr(0, 8) == "https://") {
        remaining = remaining.substr(8);
        ep.port = 443;
    }
    
    // Strip trailing slash
    if (!remaining.empty() && remaining.back() == '/') {
        remaining.pop_back();
    }
    
    // Split host:port
    size_t colon_pos = remaining.find(':');
    if (colon_pos != std::string::npos) {
        ep.host = remaining.substr(0, colon_pos);
        try {
            ep.port = std::stoi(remaining.substr(colon_pos + 1));
        } catch (...) {
            // Keep default port
        }
    } else {
        ep.host = remaining;
    }
    
    return ep;
}

InterInstanceComms::InterInstanceComms(
    const InterInstanceConfig& config,
    std::shared_ptr<PollIntentionRegistry> poll_registry,
    const std::string& own_host,
    int own_port,
    const std::string& own_hostname
) : batch_ms_(config.batch_ms),
    own_host_(own_host),
    own_port_(own_port),
    own_hostname_(own_hostname),
    poll_registry_(poll_registry)
{
    // Parse peer URLs, excluding self
    for (const auto& url : config.parse_peer_urls()) {
        if (!is_self_url(url)) {
            peer_endpoints_.push_back(parse_peer_url(url));
        } else {
            spdlog::info("InterInstanceComms: Excluding self URL: {}", url);
        }
    }
    
    // Enabled if we have any peers after filtering
    enabled_ = !peer_endpoints_.empty();
    
    spdlog::info("InterInstanceComms: enabled={}, configured_peers={}, own={}:{}, hostname={}", 
                 enabled_, peer_endpoints_.size(), own_host_, own_port_, own_hostname_);
}

InterInstanceComms::~InterInstanceComms() {
    stop();
}

bool InterInstanceComms::is_self_url(const std::string& url) const {
    // Build various forms of self URL for comparison
    std::string port_str = std::to_string(own_port_);
    
    // Check common self-reference patterns
    if (url.find("localhost:" + port_str) != std::string::npos) return true;
    if (url.find("127.0.0.1:" + port_str) != std::string::npos) return true;
    if (url.find("0.0.0.0:" + port_str) != std::string::npos) return true;
    
    // Check if URL contains own host and port
    if (!own_host_.empty() && own_host_ != "0.0.0.0") {
        if (url.find(own_host_ + ":" + port_str) != std::string::npos) return true;
    }
    
    // Check if URL contains own hostname (for Kubernetes/DNS-based discovery)
    // e.g., queen-mq-1.queen-mq-headless.smartchat.svc.cluster.local:6632
    if (!own_hostname_.empty()) {
        // Check if URL starts with hostname (after protocol)
        // Handles: http://queen-mq-1.xxx:port or queen-mq-1.xxx:port
        std::string hostname_pattern = own_hostname_ + ".";
        if (url.find("://" + hostname_pattern) != std::string::npos ||
            url.find(hostname_pattern) == 0) {
            return true;
        }
        // Also check exact hostname match (no dot after)
        hostname_pattern = own_hostname_ + ":";
        if (url.find("://" + hostname_pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

void InterInstanceComms::start() {
    if (!enabled_ || peer_endpoints_.empty()) {
        spdlog::info("InterInstanceComms: Not starting (no peers configured)");
        return;
    }
    
    running_ = true;
    
    // Start batch sending thread
    batch_thread_ = std::thread(&InterInstanceComms::batch_and_send_loop, this);
    
    spdlog::info("InterInstanceComms: Started with {} peer(s), batch_ms={}", 
                 peer_endpoints_.size(), batch_ms_);
    
    for (const auto& ep : peer_endpoints_) {
        spdlog::info("  - Peer: {}:{}", ep.host, ep.port);
    }
}

void InterInstanceComms::stop() {
    if (!running_) return;
    
    spdlog::info("InterInstanceComms: Stopping...");
    
    running_ = false;
    notification_cv_.notify_all();
    
    if (batch_thread_.joinable()) {
        batch_thread_.join();
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
        auto dedup_key = std::make_pair(queue_name, partition_name);
        if (pending_dedup_.find(dedup_key) != pending_dedup_.end()) {
            // Already have a pending notification for this queue/partition
            return;
        }
        
        pending_notifications_.push(std::move(n));
        pending_dedup_.insert(dedup_key);
        notifications_batched_++;
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
        notifications_batched_++;
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
            
            // Clear deduplication set
            pending_dedup_.clear();
        }
        
        if (!batch.empty()) {
            send_to_peers(batch);
        }
    }
    
    spdlog::info("InterInstanceComms: Batch thread stopped");
}

void InterInstanceComms::send_to_peers(const std::vector<PeerNotification>& batch) {
    // Send each notification to each peer via HTTP POST
    for (const auto& n : batch) {
        std::string body = n.to_json().dump();
        
        for (const auto& ep : peer_endpoints_) {
            try {
                // Use URL format for httplib client
                std::string url = "http://" + ep.host + ":" + std::to_string(ep.port);
                spdlog::debug("InterInstanceComms: Connecting to {}", url);
                
                httplib::Client cli(url);
                cli.set_connection_timeout(2);  // 2 seconds
                cli.set_read_timeout(2);
                cli.set_write_timeout(2);
                
                spdlog::debug("InterInstanceComms: Sending POST to {}/internal/api/notify", url);
                auto res = cli.Post("/internal/api/notify", body, "application/json");
                
                if (res && res->status == 200) {
                    notifications_sent_++;
                    spdlog::info("InterInstanceComms: Notified peer {}:{} - {}", 
                                 ep.host, ep.port,
                                 (n.type == NotificationType::MESSAGE_AVAILABLE ? 
                                  "MESSAGE_AVAILABLE" : "PARTITION_FREE"));
                } else {
                    http_errors_++;
                    if (res) {
                        spdlog::warn("InterInstanceComms: HTTP {} from {}:{}", 
                                    res->status, ep.host, ep.port);
                    } else {
                        auto err = res.error();
                        spdlog::warn("InterInstanceComms: Failed to connect to {}:{} - error: {}", 
                                    ep.host, ep.port, httplib::to_string(err));
                    }
                }
            } catch (const std::exception& e) {
                http_errors_++;
                spdlog::warn("InterInstanceComms: Error notifying {}:{} - {}", 
                            ep.host, ep.port, e.what());
            }
        }
    }
    
    spdlog::debug("InterInstanceComms: Sent {} notifications to {} peers",
                 batch.size(), peer_endpoints_.size());
}

// ============================================================
// INBOUND: Handle notifications from peers
// ============================================================

void InterInstanceComms::handle_peer_notification(const PeerNotification& notification) {
    notifications_received_++;
    
    if (!poll_registry_) {
        spdlog::warn("InterInstanceComms: No poll registry configured");
        return;
    }
    
    switch (notification.type) {
        case NotificationType::MESSAGE_AVAILABLE:
            // New message available - reset backoff for matching groups
            spdlog::info("Peer notification received: MESSAGE_AVAILABLE for {}:{}",
                         notification.queue_name, notification.partition_name);
            
            poll_registry_->reset_backoff_for_queue_partition(
                notification.queue_name,
                notification.partition_name
            );
            break;
            
        case NotificationType::PARTITION_FREE:
            // Partition freed - reset backoff for specific consumer group
            spdlog::info("Peer notification received: PARTITION_FREE for {}:{}:{}",
                         notification.queue_name, notification.partition_name,
                         notification.consumer_group);
            
            {
                std::string group_key = notification.queue_name + ":" +
                                       notification.partition_name + ":" +
                                       notification.consumer_group;
                poll_registry_->reset_backoff_for_group(group_key);
                
                // Also reset for wildcard partition (queue:*:consumer_group)
                std::string wildcard_key = notification.queue_name + ":*:" +
                                          notification.consumer_group;
                poll_registry_->reset_backoff_for_group(wildcard_key);
            }
            break;
            
        case NotificationType::HEARTBEAT:
            // Just a keep-alive, nothing to do
            spdlog::trace("InterInstanceComms: Received HEARTBEAT");
            break;
    }
}

// ============================================================
// Stats
// ============================================================

nlohmann::json InterInstanceComms::get_stats() const {
    nlohmann::json peers = nlohmann::json::array();
    for (const auto& ep : peer_endpoints_) {
        peers.push_back(ep.host + ":" + std::to_string(ep.port));
    }
    
    return {
        {"enabled", enabled_},
        {"configured_peers", peer_endpoints_.size()},
        {"peer_urls", peers},
        {"notifications_sent", notifications_sent_.load()},
        {"notifications_received", notifications_received_.load()},
        {"notifications_batched", notifications_batched_.load()},
        {"http_errors", http_errors_.load()},
        {"batch_ms", batch_ms_}
    };
}

} // namespace queen
