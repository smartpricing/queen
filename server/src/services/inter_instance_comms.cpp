#include "queen/inter_instance_comms.hpp"
#include "queen/shared_state_manager.hpp"
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <chrono>
#include <regex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>

namespace queen {

// Global instance
std::shared_ptr<InterInstanceComms> global_inter_instance_comms;

// ============================================================
// Static Helpers
// ============================================================

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

// ============================================================
// Constructor / Destructor
// ============================================================

InterInstanceComms::InterInstanceComms(
    const InterInstanceConfig& config,
    std::shared_ptr<PollIntentionRegistry> poll_registry,
    const std::string& own_host,
    int own_port,
    const std::string& own_hostname
) : batch_ms_(config.batch_ms),
    udp_port_(config.udp_port),
    own_host_(own_host),
    own_port_(own_port),
    own_hostname_(own_hostname),
    poll_registry_(poll_registry)
{
    // ========================================
    // Parse HTTP peers (existing behavior)
    // ========================================
    for (const auto& url : config.parse_peer_urls()) {
        if (!is_self_http_url(url)) {
            http_endpoints_.push_back(parse_peer_url(url));
        } else {
            spdlog::info("InterInstanceComms: Excluding self HTTP URL: {}", url);
        }
    }
    http_enabled_ = !http_endpoints_.empty();
    
    // ========================================
    // Parse UDP peers (new)
    // ========================================
    // If UDPSYNC is enabled, skip setting up UDP here - UDPSyncTransport handles it
    if (config.shared_state.enabled && !config.udp_peers.empty()) {
        spdlog::info("InterInstanceComms: UDPSYNC enabled, skipping UDP listener (UDPSyncTransport handles it)");
        udp_enabled_ = false;
    } else {
        std::vector<UdpPeerEntry> udp_peers_to_resolve;
        for (const auto& peer : config.parse_udp_peers()) {
            if (!is_self_udp_peer(peer.host, peer.port)) {
                udp_peers_to_resolve.push_back(peer);
            } else {
                spdlog::info("InterInstanceComms: Excluding self UDP peer: {}:{}", peer.host, peer.port);
            }
        }
        
        if (!udp_peers_to_resolve.empty()) {
            resolve_udp_peers(udp_peers_to_resolve);
            udp_enabled_ = !udp_endpoints_.empty();
        }
    }
    
    spdlog::info("InterInstanceComms: http_enabled={}, http_peers={}, udp_enabled={}, udp_peers={}, own={}:{}, hostname={}", 
                 http_enabled_, http_endpoints_.size(),
                 udp_enabled_, udp_endpoints_.size(),
                 own_host_, own_port_, own_hostname_);
}

InterInstanceComms::~InterInstanceComms() {
    stop();
}

// ============================================================
// Self-Detection
// ============================================================

bool InterInstanceComms::is_self_http_url(const std::string& url) const {
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
    if (!own_hostname_.empty()) {
        std::string hostname_pattern = own_hostname_ + ".";
        if (url.find("://" + hostname_pattern) != std::string::npos ||
            url.find(hostname_pattern) == 0) {
            return true;
        }
        hostname_pattern = own_hostname_ + ":";
        if (url.find("://" + hostname_pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

bool InterInstanceComms::is_self_udp_peer(const std::string& hostname, int port) const {
    // For localhost/loopback addresses, only consider it "self" if port matches our UDP port
    // This allows local testing with multiple instances on different ports
    if (hostname == "localhost" || hostname == "127.0.0.1" || hostname == "0.0.0.0") {
        return port == udp_port_;  // Only self if same port
    }
    
    // Check if hostname matches our own host AND port matches
    if (!own_host_.empty() && own_host_ != "0.0.0.0" && hostname == own_host_) {
        return port == udp_port_;
    }
    
    // Check if hostname starts with our system hostname (K8s StatefulSet pattern)
    // e.g., own_hostname_="queen-mq-1" matches "queen-mq-1.queen-mq-headless..."
    // In K8s, all instances use the same UDP port, so hostname match is sufficient
    if (!own_hostname_.empty()) {
        if (hostname == own_hostname_ ||
            hostname.find(own_hostname_ + ".") == 0) {
            return true;
        }
    }
    
    return false;
}

// ============================================================
// Lifecycle
// ============================================================

void InterInstanceComms::start() {
    if (!http_enabled_ && !udp_enabled_) {
        spdlog::info("InterInstanceComms: Not starting (no peers configured)");
        return;
    }
    
    running_ = true;
    
    // Start HTTP batch thread if HTTP is enabled
    if (http_enabled_) {
        http_batch_thread_ = std::thread(&InterInstanceComms::http_batch_and_send_loop, this);
        spdlog::info("InterInstanceComms: HTTP enabled with {} peer(s), batch_ms={}", 
                     http_endpoints_.size(), batch_ms_);
        for (const auto& ep : http_endpoints_) {
            spdlog::info("  - HTTP Peer: {}:{}", ep.host, ep.port);
        }
    }
    
    // Initialize UDP sockets and start receiver if UDP is enabled
    if (udp_enabled_) {
        if (init_udp_sockets()) {
            udp_recv_thread_ = std::thread(&InterInstanceComms::udp_recv_loop, this);
            spdlog::info("InterInstanceComms: UDP enabled with {} peer(s), port={}", 
                         udp_endpoints_.size(), udp_port_);
            for (const auto& ep : udp_endpoints_) {
                if (ep.resolved) {
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &ep.addr.sin_addr, ip_str, sizeof(ip_str));
                    spdlog::info("  - UDP Peer: {}:{} -> {}:{}", ep.hostname, ep.port, ip_str, ep.port);
                } else {
                    spdlog::warn("  - UDP Peer: {}:{} (unresolved)", ep.hostname, ep.port);
                }
            }
        } else {
            spdlog::error("InterInstanceComms: Failed to initialize UDP sockets");
            udp_enabled_ = false;
        }
    }
}

void InterInstanceComms::stop() {
    if (!running_) return;
    
    spdlog::info("InterInstanceComms: Stopping...");
    
    running_ = false;
    
    // Stop HTTP thread
    http_notification_cv_.notify_all();
    if (http_batch_thread_.joinable()) {
        http_batch_thread_.join();
    }
    
    // Stop UDP threads and cleanup sockets
    cleanup_udp_sockets();
    if (udp_recv_thread_.joinable()) {
        udp_recv_thread_.join();
    }
    
    spdlog::info("InterInstanceComms: Stopped");
}

// ============================================================
// UDP Socket Management
// ============================================================

bool InterInstanceComms::init_udp_sockets() {
    // Create send socket (non-blocking)
    udp_send_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_send_sock_ < 0) {
        spdlog::error("InterInstanceComms: Failed to create UDP send socket: {}", strerror(errno));
        return false;
    }
    
    // Set send socket to non-blocking
    int flags = fcntl(udp_send_sock_, F_GETFL, 0);
    fcntl(udp_send_sock_, F_SETFL, flags | O_NONBLOCK);
    
    // Create receive socket
    udp_recv_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_recv_sock_ < 0) {
        spdlog::error("InterInstanceComms: Failed to create UDP recv socket: {}", strerror(errno));
        close(udp_send_sock_);
        udp_send_sock_ = -1;
        return false;
    }
    
    // Allow address reuse
    int opt = 1;
    setsockopt(udp_recv_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #ifdef SO_REUSEPORT
    setsockopt(udp_recv_sock_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    #endif
    
    // Bind receive socket to UDP port
    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(udp_port_);
    
    if (bind(udp_recv_sock_, (sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        spdlog::error("InterInstanceComms: Failed to bind UDP recv socket to port {}: {}", 
                     udp_port_, strerror(errno));
        close(udp_send_sock_);
        close(udp_recv_sock_);
        udp_send_sock_ = -1;
        udp_recv_sock_ = -1;
        return false;
    }
    
    spdlog::info("InterInstanceComms: UDP sockets initialized, listening on port {}", udp_port_);
    return true;
}

void InterInstanceComms::cleanup_udp_sockets() {
    if (udp_send_sock_ >= 0) {
        close(udp_send_sock_);
        udp_send_sock_ = -1;
    }
    if (udp_recv_sock_ >= 0) {
        // Shutdown to unblock recvfrom
        shutdown(udp_recv_sock_, SHUT_RDWR);
        close(udp_recv_sock_);
        udp_recv_sock_ = -1;
    }
}

void InterInstanceComms::resolve_udp_peers(const std::vector<UdpPeerEntry>& peers) {
    for (const auto& peer : peers) {
        UdpPeerEndpoint ep;
        ep.hostname = peer.host;
        ep.port = peer.port;
        ep.resolved = false;
        
        // Resolve hostname to IP
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        
        int err = getaddrinfo(peer.host.c_str(), nullptr, &hints, &res);
        if (err == 0 && res != nullptr) {
            // Copy the resolved address
            memcpy(&ep.addr, res->ai_addr, sizeof(sockaddr_in));
            ep.addr.sin_port = htons(peer.port);  // Use per-peer port
            ep.resolved = true;
            freeaddrinfo(res);
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ep.addr.sin_addr, ip_str, sizeof(ip_str));
            spdlog::info("InterInstanceComms: Resolved UDP peer {}:{} -> {}:{}", 
                        peer.host, peer.port, ip_str, peer.port);
        } else {
            spdlog::warn("InterInstanceComms: Failed to resolve UDP peer {}:{} - {}", 
                        peer.host, peer.port, gai_strerror(err));
        }
        
        udp_endpoints_.push_back(std::move(ep));
    }
}

// ============================================================
// OUTBOUND: Notification dispatch
// ============================================================

void InterInstanceComms::notify_message_available(
    const std::string& queue_name,
    const std::string& partition_name
) {
    if (!running_) return;
    if (!http_enabled_ && !udp_enabled_) return;
    
    PeerNotification n;
    n.type = NotificationType::MESSAGE_AVAILABLE;
    n.queue_name = queue_name;
    n.partition_name = partition_name;
    n.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // UDP: Use SharedStateManager for targeted notifications if available
    if (udp_enabled_) {
        if (shared_state_ && shared_state_->is_running()) {
            // Use SharedStateManager for targeted notifications
            shared_state_->notify_message_available(queue_name, partition_name);
        } else {
            // Fallback to broadcasting to all peers
        udp_send_notification(n);
        }
    }
    
    // HTTP: Queue for batched sending
    if (http_enabled_) {
        queue_http_notification(n);
    }
}

void InterInstanceComms::notify_partition_free(
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group
) {
    if (!running_) return;
    if (!http_enabled_ && !udp_enabled_) return;
    
    PeerNotification n;
    n.type = NotificationType::PARTITION_FREE;
    n.queue_name = queue_name;
    n.partition_name = partition_name;
    n.consumer_group = consumer_group;
    n.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // UDP: Use SharedStateManager for targeted notifications if available
    if (udp_enabled_) {
        if (shared_state_ && shared_state_->is_running()) {
            // Use SharedStateManager for targeted notifications
            shared_state_->notify_partition_free(queue_name, partition_name, consumer_group);
        } else {
            // Fallback to broadcasting to all peers
        udp_send_notification(n);
        }
    }
    
    // HTTP: Queue for batched sending
    if (http_enabled_) {
        queue_http_notification(n);
    }
}

// ============================================================
// HTTP: Batched sending
// ============================================================

void InterInstanceComms::queue_http_notification(const PeerNotification& n) {
    std::lock_guard<std::mutex> lock(http_notification_mutex_);
    
    // Deduplicate MESSAGE_AVAILABLE by queue/partition
    if (n.type == NotificationType::MESSAGE_AVAILABLE) {
        auto dedup_key = std::make_pair(n.queue_name, n.partition_name);
        if (pending_http_dedup_.find(dedup_key) != pending_http_dedup_.end()) {
            return;  // Already pending
        }
        pending_http_dedup_.insert(dedup_key);
    }
    
    pending_http_notifications_.push(n);
    http_notifications_batched_++;
    http_notification_cv_.notify_one();
}

void InterInstanceComms::http_batch_and_send_loop() {
    spdlog::info("InterInstanceComms: HTTP batch thread started (batch_ms={})", batch_ms_);
    
    while (running_) {
        std::vector<PeerNotification> batch;
        
        {
            std::unique_lock<std::mutex> lock(http_notification_mutex_);
            
            // Wait for notifications or timeout
            if (batch_ms_ > 0) {
                http_notification_cv_.wait_for(lock, std::chrono::milliseconds(batch_ms_),
                    [this] { return !pending_http_notifications_.empty() || !running_; });
            } else {
                http_notification_cv_.wait(lock,
                    [this] { return !pending_http_notifications_.empty() || !running_; });
            }
            
            if (!running_) break;
            
            // Drain queue into batch
            while (!pending_http_notifications_.empty()) {
                batch.push_back(std::move(pending_http_notifications_.front()));
                pending_http_notifications_.pop();
            }
            
            // Clear deduplication set
            pending_http_dedup_.clear();
        }
        
        if (!batch.empty()) {
            http_send_to_peers(batch);
        }
    }
    
    spdlog::info("InterInstanceComms: HTTP batch thread stopped");
}

void InterInstanceComms::http_send_to_peers(const std::vector<PeerNotification>& batch) {
    for (const auto& n : batch) {
        std::string body = n.to_json().dump();
        
        for (const auto& ep : http_endpoints_) {
            try {
                std::string url = "http://" + ep.host + ":" + std::to_string(ep.port);
                
                httplib::Client cli(url);
                cli.set_connection_timeout(2);
                cli.set_read_timeout(2);
                cli.set_write_timeout(2);
                
                auto res = cli.Post("/internal/api/notify", body, "application/json");
                
                if (res && res->status == 200) {
                    http_notifications_sent_++;
                    spdlog::debug("InterInstanceComms: HTTP notified {}:{} - {}", 
                                 ep.host, ep.port,
                                 (n.type == NotificationType::MESSAGE_AVAILABLE ? 
                                  "MESSAGE_AVAILABLE" : "PARTITION_FREE"));
                } else {
                    http_errors_++;
                    if (res) {
                        spdlog::warn("InterInstanceComms: HTTP {} from {}:{}", 
                                    res->status, ep.host, ep.port);
                    } else {
                        spdlog::warn("InterInstanceComms: HTTP failed to {}:{} - {}", 
                                    ep.host, ep.port, httplib::to_string(res.error()));
                    }
                }
            } catch (const std::exception& e) {
                http_errors_++;
                spdlog::warn("InterInstanceComms: HTTP error to {}:{} - {}", 
                            ep.host, ep.port, e.what());
            }
        }
    }
}

// ============================================================
// UDP: Fire-and-forget sending
// ============================================================

void InterInstanceComms::udp_send_notification(const PeerNotification& n) {
    if (udp_send_sock_ < 0) return;
    
    std::string json = n.to_json().dump();
    
    for (const auto& ep : udp_endpoints_) {
        if (!ep.resolved) continue;
        
        ssize_t sent = sendto(udp_send_sock_, json.data(), json.size(), 0,
                              (sockaddr*)&ep.addr, sizeof(ep.addr));
        
        if (sent > 0) {
            udp_notifications_sent_++;
            spdlog::debug("InterInstanceComms: UDP sent to {} ({} bytes)", ep.hostname, sent);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            udp_send_errors_++;
            spdlog::warn("InterInstanceComms: UDP send to {} failed: {}", 
                        ep.hostname, strerror(errno));
        }
        // Note: EAGAIN/EWOULDBLOCK are silently ignored (fire-and-forget)
    }
}

// ============================================================
// UDP: Receive loop
// ============================================================

void InterInstanceComms::udp_recv_loop() {
    spdlog::info("InterInstanceComms: UDP receive thread started on port {}", udp_port_);
    
    char buffer[4096];  // Plenty for our small JSON notifications
    
    while (running_) {
        // Use poll to allow clean shutdown
        struct pollfd pfd;
        pfd.fd = udp_recv_sock_;
        pfd.events = POLLIN;
        
        int ret = poll(&pfd, 1, 100);  // 100ms timeout
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            spdlog::warn("InterInstanceComms: UDP poll error: {}", strerror(errno));
            break;
        }
        
        if (ret == 0) continue;  // Timeout, check running_ and loop
        
        if (!(pfd.revents & POLLIN)) continue;
        
        sockaddr_in sender_addr{};
        socklen_t sender_len = sizeof(sender_addr);
        
        ssize_t received = recvfrom(udp_recv_sock_, buffer, sizeof(buffer) - 1, 0,
                                    (sockaddr*)&sender_addr, &sender_len);
        
        if (received <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            if (running_) {
                spdlog::warn("InterInstanceComms: UDP recvfrom error: {}", strerror(errno));
            }
            break;
        }
        
        buffer[received] = '\0';
        
        // Parse and handle notification
        try {
            auto json = nlohmann::json::parse(buffer, buffer + received);
            PeerNotification notification = PeerNotification::from_json(json);
            
            udp_notifications_received_++;
            
            char sender_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));
            spdlog::debug("InterInstanceComms: UDP received from {}:{} - {} bytes",
                         sender_ip, ntohs(sender_addr.sin_port), received);
            
            handle_peer_notification(notification);
            
        } catch (const std::exception& e) {
            spdlog::warn("InterInstanceComms: Failed to parse UDP notification: {}", e.what());
        }
    }
    
    spdlog::info("InterInstanceComms: UDP receive thread stopped");
}

// ============================================================
// INBOUND: Handle notifications from peers (HTTP or UDP)
// ============================================================

void InterInstanceComms::handle_peer_notification(const PeerNotification& notification) {
    notifications_received_++;
    
    if (!poll_registry_) {
        spdlog::warn("InterInstanceComms: No poll registry configured");
        return;
    }
    
    switch (notification.type) {
        case NotificationType::MESSAGE_AVAILABLE:
            spdlog::debug("Peer notification: MESSAGE_AVAILABLE for {}:{}",
                         notification.queue_name, notification.partition_name);
            
            poll_registry_->reset_backoff_for_queue_partition(
                notification.queue_name,
                notification.partition_name
            );
            break;
            
        case NotificationType::PARTITION_FREE:
            spdlog::debug("Peer notification: PARTITION_FREE for {}:{}:{}",
                         notification.queue_name, notification.partition_name,
                         notification.consumer_group);
            
            {
                std::string group_key = notification.queue_name + ":" +
                                       notification.partition_name + ":" +
                                       notification.consumer_group;
                poll_registry_->reset_backoff_for_group(group_key);
                
                std::string wildcard_key = notification.queue_name + ":*:" +
                                          notification.consumer_group;
                poll_registry_->reset_backoff_for_group(wildcard_key);
            }
            break;
            
        case NotificationType::HEARTBEAT:
            spdlog::trace("InterInstanceComms: Received HEARTBEAT");
            break;
    }
}

// ============================================================
// Stats
// ============================================================

nlohmann::json InterInstanceComms::get_stats() const {
    // HTTP peers list
    nlohmann::json http_peers = nlohmann::json::array();
    for (const auto& ep : http_endpoints_) {
        http_peers.push_back(ep.host + ":" + std::to_string(ep.port));
    }
    
    // UDP peers list
    nlohmann::json udp_peers = nlohmann::json::array();
    for (const auto& ep : udp_endpoints_) {
        nlohmann::json peer;
        peer["hostname"] = ep.hostname;
        peer["port"] = ep.port;
        peer["resolved"] = ep.resolved;
        if (ep.resolved) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ep.addr.sin_addr, ip_str, sizeof(ip_str));
            peer["resolved_ip"] = std::string(ip_str);
        }
        udp_peers.push_back(peer);
    }
    
    return {
        {"enabled", http_enabled_ || udp_enabled_},
        {"http", {
            {"enabled", http_enabled_},
            {"peer_count", http_endpoints_.size()},
            {"peers", http_peers},
            {"batch_ms", batch_ms_},
            {"notifications_sent", http_notifications_sent_.load()},
            {"notifications_batched", http_notifications_batched_.load()},
            {"errors", http_errors_.load()}
        }},
        {"udp", {
            {"enabled", udp_enabled_},
            {"peer_count", udp_endpoints_.size()},
            {"peers", udp_peers},
            {"port", udp_port_},
            {"notifications_sent", udp_notifications_sent_.load()},
            {"notifications_received", udp_notifications_received_.load()},
            {"send_errors", udp_send_errors_.load()}
        }},
        {"notifications_received_total", notifications_received_.load()}
    };
}

} // namespace queen
