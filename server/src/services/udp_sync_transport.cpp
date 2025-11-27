#include "queen/udp_sync_transport.hpp"
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>

namespace queen {

UDPSyncTransport::UDPSyncTransport(const SharedStateConfig& config,
                                   const std::string& server_id,
                                   int udp_port)
    : config_(config)
    , server_id_(server_id)
    , session_id_(generate_session_id())
    , udp_port_(udp_port)
{
    message_builder_ = std::make_unique<UDPSyncMessageBuilder>(server_id_, session_id_);
    spdlog::info("UDPSyncTransport: Created with server_id={}, session_id={}, port={}",
                 server_id_, session_id_, udp_port_);
}

UDPSyncTransport::~UDPSyncTransport() {
    stop();
}

bool UDPSyncTransport::init_sockets() {
    // Create a single socket for both sending and receiving
    // This ensures outgoing packets have our listening port as the source
    // which allows peers to identify us correctly
    recv_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_sock_ < 0) {
        spdlog::error("UDPSyncTransport: Failed to create socket: {}", strerror(errno));
        return false;
    }
    
    // Use the same socket for sending
    send_sock_ = recv_sock_;
    
    // Set large receive buffer
    int recv_buf_size = config_.recv_buffer_mb * 1024 * 1024;
    if (setsockopt(recv_sock_, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size)) < 0) {
        spdlog::warn("UDPSyncTransport: Failed to set SO_RCVBUF to {}MB: {}", 
                    config_.recv_buffer_mb, strerror(errno));
    }
    
    // Verify actual size
    int actual_size;
    socklen_t len = sizeof(actual_size);
    getsockopt(recv_sock_, SOL_SOCKET, SO_RCVBUF, &actual_size, &len);
    spdlog::info("UDPSyncTransport: Receive buffer size = {} bytes", actual_size);
    
    // Allow address reuse
    int opt = 1;
    setsockopt(recv_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #ifdef SO_REUSEPORT
    setsockopt(recv_sock_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    #endif
    
    // Bind socket to our port (for both receiving AND as source port for sends)
    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(udp_port_);
    
    if (bind(recv_sock_, (sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        spdlog::error("UDPSyncTransport: Failed to bind to port {}: {}", 
                     udp_port_, strerror(errno));
        close(recv_sock_);
        send_sock_ = -1;
        recv_sock_ = -1;
        return false;
    }
    
    // Set non-blocking for sends (after bind)
    int flags = fcntl(recv_sock_, F_GETFL, 0);
    fcntl(recv_sock_, F_SETFL, flags | O_NONBLOCK);
    
    spdlog::info("UDPSyncTransport: Socket initialized on port {} (single socket for send/recv)", udp_port_);
    return true;
}

void UDPSyncTransport::cleanup_sockets() {
    // send_sock_ and recv_sock_ are the same socket now
    if (recv_sock_ >= 0) {
        shutdown(recv_sock_, SHUT_RDWR);
        close(recv_sock_);
        recv_sock_ = -1;
        send_sock_ = -1;
    }
}

bool UDPSyncTransport::start() {
    if (running_) return true;
    
    if (!init_sockets()) {
        return false;
    }
    
    running_ = true;
    recv_thread_ = std::thread(&UDPSyncTransport::recv_loop, this);
    
    spdlog::info("UDPSyncTransport: Started");
    return true;
}

void UDPSyncTransport::stop() {
    if (!running_) return;
    
    spdlog::info("UDPSyncTransport: Stopping...");
    running_ = false;
    
    cleanup_sockets();
    
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    
    spdlog::info("UDPSyncTransport: Stopped");
}

void UDPSyncTransport::add_peer(const std::string& hostname, int port) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    SyncPeerEndpoint peer;
    peer.hostname = hostname;
    peer.port = port;
    // server_id format: "hostname:port" to match SharedStateManager format
    peer.server_id = extract_server_id(hostname) + ":" + std::to_string(port);
    peer.resolved = false;
    
    peers_.push_back(std::move(peer));
    
    spdlog::debug("UDPSyncTransport: Added peer {}:{} (server_id={})", 
                 hostname, port, peers_.back().server_id);
}

void UDPSyncTransport::resolve_peers() {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    peer_by_server_id_.clear();
    
    for (size_t i = 0; i < peers_.size(); i++) {
        auto& peer = peers_[i];
        
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        
        int err = getaddrinfo(peer.hostname.c_str(), nullptr, &hints, &res);
        if (err == 0 && res != nullptr) {
            memcpy(&peer.addr, res->ai_addr, sizeof(sockaddr_in));
            peer.addr.sin_port = htons(peer.port);
            peer.resolved = true;
            freeaddrinfo(res);
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer.addr.sin_addr, ip_str, sizeof(ip_str));
            spdlog::info("UDPSyncTransport: Resolved {}:{} -> {}:{} (server_id={})",
                        peer.hostname, peer.port, ip_str, peer.port, peer.server_id);
        } else {
            spdlog::warn("UDPSyncTransport: Failed to resolve {}:{} - {}",
                        peer.hostname, peer.port, gai_strerror(err));
        }
        
        peer_by_server_id_[peer.server_id] = i;
    }
}

void UDPSyncTransport::broadcast(UDPSyncMessageType type, const nlohmann::json& payload) {
    std::vector<uint8_t> data = message_builder_->build(type, payload, config_.sync_secret);
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    for (const auto& peer : peers_) {
        if (peer.resolved) {
            send_raw(peer, data);
        }
    }
}

void UDPSyncTransport::send_to(const std::set<std::string>& server_ids,
                               UDPSyncMessageType type,
                               const nlohmann::json& payload) {
    if (server_ids.empty()) return;
    
    std::vector<uint8_t> data = message_builder_->build(type, payload, config_.sync_secret);
    
    // Lock both mutexes in consistent order to avoid deadlock
    std::lock_guard<std::mutex> lock1(learned_ids_mutex_);
    std::lock_guard<std::mutex> lock2(peers_mutex_);
    
    for (const auto& server_id : server_ids) {
        // First try learned identities (populated from heartbeats)
        auto learned_it = learned_server_ids_.find(server_id);
        if (learned_it != learned_server_ids_.end()) {
            size_t idx = learned_it->second;
            if (idx < peers_.size() && peers_[idx].resolved) {
                send_raw(peers_[idx], data);
                continue;
            }
        }
        
        // Fall back to pre-computed mapping (for backward compatibility)
        auto it = peer_by_server_id_.find(server_id);
        if (it != peer_by_server_id_.end() && peers_[it->second].resolved) {
            send_raw(peers_[it->second], data);
        }
    }
}

void UDPSyncTransport::send_to_server(const std::string& server_id,
                                      UDPSyncMessageType type,
                                      const nlohmann::json& payload) {
    // Lock both mutexes in consistent order
    std::lock_guard<std::mutex> lock1(learned_ids_mutex_);
    std::lock_guard<std::mutex> lock2(peers_mutex_);
    
    size_t peer_idx = SIZE_MAX;
    
    // First try learned identities (populated from heartbeats)
    auto learned_it = learned_server_ids_.find(server_id);
    if (learned_it != learned_server_ids_.end()) {
        peer_idx = learned_it->second;
    } else {
        // Fall back to pre-computed mapping
        auto it = peer_by_server_id_.find(server_id);
        if (it != peer_by_server_id_.end()) {
            peer_idx = it->second;
        }
    }
    
    if (peer_idx == SIZE_MAX || peer_idx >= peers_.size()) {
        spdlog::debug("UDPSyncTransport: Unknown server_id {}", server_id);
        return;
    }
    
    const auto& peer = peers_[peer_idx];
    if (!peer.resolved) {
        spdlog::debug("UDPSyncTransport: Peer {} not resolved", server_id);
        return;
    }
    
    std::vector<uint8_t> data = message_builder_->build(type, payload, config_.sync_secret);
    send_raw(peer, data);
}

void UDPSyncTransport::send_raw(const SyncPeerEndpoint& peer, const std::vector<uint8_t>& data) {
    ssize_t sent = sendto(send_sock_, data.data(), data.size(), 0,
                          (sockaddr*)&peer.addr, sizeof(peer.addr));
    
    if (sent > 0) {
        messages_sent_++;
        spdlog::trace("UDPSyncTransport: Sent {} bytes to {}", sent, peer.hostname);
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        spdlog::warn("UDPSyncTransport: Send to {} failed: {}", peer.hostname, strerror(errno));
    }
}

void UDPSyncTransport::on_message(UDPSyncMessageType type, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_[type].push_back(std::move(handler));
}

std::vector<std::string> UDPSyncTransport::get_peer_ids() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    std::vector<std::string> result;
    result.reserve(peers_.size());
    for (const auto& peer : peers_) {
        result.push_back(peer.server_id);
    }
    return result;
}

void UDPSyncTransport::recv_loop() {
    spdlog::info("UDPSyncTransport: Receive thread started on port {}", udp_port_);
    
    std::vector<uint8_t> buffer(UDP_SYNC_MAX_MESSAGE);
    
    while (running_) {
        // Use poll to allow clean shutdown
        struct pollfd pfd;
        pfd.fd = recv_sock_;
        pfd.events = POLLIN;
        
        int ret = poll(&pfd, 1, 100);  // 100ms timeout
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            spdlog::warn("UDPSyncTransport: Poll error: {}", strerror(errno));
            break;
        }
        
        if (ret == 0) continue;  // Timeout
        
        if (!(pfd.revents & POLLIN)) continue;
        
        sockaddr_in sender_addr{};
        socklen_t sender_len = sizeof(sender_addr);
        
        ssize_t received = recvfrom(recv_sock_, buffer.data(), buffer.size(), 0,
                                    (sockaddr*)&sender_addr, &sender_len);
        
        if (received <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            if (running_) {
                spdlog::warn("UDPSyncTransport: recvfrom error: {}", strerror(errno));
            }
            break;
        }
        
        // Parse message
        UDPSyncMessageParser parser(buffer.data(), static_cast<size_t>(received));
        
        if (!parser.is_valid()) {
            spdlog::debug("UDPSyncTransport: Invalid message (too short)");
            messages_dropped_++;
            continue;
        }
        
        // Check version
        if (parser.version() != UDP_SYNC_VERSION) {
            spdlog::debug("UDPSyncTransport: Unknown version {}", parser.version());
            messages_dropped_++;
            continue;
        }
        
        // Validate signature
        if (!validate_signature(parser)) {
            continue;
        }
        
        // Validate sequence
        if (!validate_sequence(parser.sender_id(), parser.session_id(), parser.sequence())) {
            continue;
        }
        
        // Learn peer identity from this message
        // This maps the declared server_id to the network address that sent it
        learn_peer_identity(parser.sender_id(), sender_addr);
        
        // Dispatch to handlers
        messages_received_++;
        dispatch_message(parser);
    }
    
    spdlog::info("UDPSyncTransport: Receive thread stopped");
}

bool UDPSyncTransport::validate_signature(const UDPSyncMessageParser& parser) {
    if (config_.sync_secret.empty()) {
        return true;  // No secret = no validation (insecure mode)
    }
    
    if (!parser.verify_signature(config_.sync_secret)) {
        spdlog::debug("UDPSyncTransport: Invalid signature from {}", parser.sender_id());
        signature_failures_++;
        messages_dropped_++;
        return false;
    }
    
    return true;
}

bool UDPSyncTransport::validate_sequence(const std::string& sender_id,
                                         const std::string& session_id,
                                         uint64_t sequence) {
    std::lock_guard<std::mutex> lock(sender_mutex_);
    
    auto& state = sender_states_[sender_id];
    
    // Check session ID (detect restarts)
    if (state.session_id.empty()) {
        // First message from this sender
        state.session_id = session_id;
        state.max_seen_seq = sequence;
        return true;
    }
    
    if (state.session_id != session_id) {
        // Sender restarted - reset sequence tracking
        spdlog::info("UDPSyncTransport: Server {} restarted (new session)", sender_id);
        state.session_id = session_id;
        state.max_seen_seq = sequence;
        return true;
    }
    
    // Same session - check sequence
    if (sequence <= state.max_seen_seq) {
        // Old or duplicate packet - drop
        spdlog::trace("UDPSyncTransport: Dropping old packet from {} (seq {} <= {})",
                     sender_id, sequence, state.max_seen_seq);
        sequence_rejections_++;
        messages_dropped_++;
        return false;
    }
    
    state.max_seen_seq = sequence;
    return true;
}

void UDPSyncTransport::dispatch_message(const UDPSyncMessageParser& parser) {
    UDPSyncMessageType type = parser.type();
    std::string sender_id = parser.sender_id();
    nlohmann::json payload = parser.payload();
    
    spdlog::trace("UDPSyncTransport: Received {} from {} (seq {})",
                 message_type_to_string(type), sender_id, parser.sequence());
    
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    
    auto it = handlers_.find(type);
    if (it != handlers_.end()) {
        for (const auto& handler : it->second) {
            try {
                handler(type, sender_id, payload);
            } catch (const std::exception& e) {
                spdlog::error("UDPSyncTransport: Handler exception for {}: {}",
                             message_type_to_string(type), e.what());
            }
        }
    }
}

std::string UDPSyncTransport::extract_server_id(const std::string& hostname) {
    // For K8s StatefulSet: "queen-mq-0.queen-mq-headless.namespace.svc" → "queen-mq-0"
    // For simple hostname: "queen-mq-0" → "queen-mq-0"
    // For IP: "192.168.1.1" → "192.168.1.1" (keep as-is)
    
    // Check if it's an IP address (starts with digit) - keep full IP
    if (!hostname.empty() && std::isdigit(static_cast<unsigned char>(hostname[0]))) {
        return hostname;
    }
    
    size_t dot = hostname.find('.');
    if (dot != std::string::npos) {
        return hostname.substr(0, dot);
    }
    return hostname;
}

int UDPSyncTransport::find_peer_by_addr(const sockaddr_in& addr) const {
    // Match by IP address and port
    for (size_t i = 0; i < peers_.size(); i++) {
        if (peers_[i].resolved &&
            peers_[i].addr.sin_addr.s_addr == addr.sin_addr.s_addr &&
            peers_[i].addr.sin_port == addr.sin_port) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void UDPSyncTransport::learn_peer_identity(const std::string& sender_id, const sockaddr_in& sender_addr) {
    // Don't learn our own identity
    if (sender_id == server_id_) {
        return;
    }
    
    // Find which peer sent this message
    int peer_idx;
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        peer_idx = find_peer_by_addr(sender_addr);
    }
    
    if (peer_idx < 0) {
        // Message from unknown peer - might be misconfigured or new peer
        spdlog::trace("UDPSyncTransport: Message from unknown address, sender_id={}", sender_id);
        return;
    }
    
    // Update the learned identity mapping
    {
        std::lock_guard<std::mutex> lock(learned_ids_mutex_);
        auto it = learned_server_ids_.find(sender_id);
        if (it == learned_server_ids_.end()) {
            // New identity learned
            learned_server_ids_[sender_id] = static_cast<size_t>(peer_idx);
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sender_addr.sin_addr, ip_str, sizeof(ip_str));
            spdlog::info("UDPSyncTransport: Learned identity: {} -> peer[{}] ({}:{})",
                        sender_id, peer_idx, ip_str, ntohs(sender_addr.sin_port));
        } else if (it->second != static_cast<size_t>(peer_idx)) {
            // Identity moved to different peer (unusual, but handle it)
            spdlog::warn("UDPSyncTransport: Identity {} moved from peer[{}] to peer[{}]",
                        sender_id, it->second, peer_idx);
            learned_server_ids_[sender_id] = static_cast<size_t>(peer_idx);
        }
    }
}

nlohmann::json UDPSyncTransport::get_stats() const {
    // Lock both mutexes in consistent order
    std::lock_guard<std::mutex> lock1(learned_ids_mutex_);
    std::lock_guard<std::mutex> lock2(peers_mutex_);
    
    nlohmann::json peers_json = nlohmann::json::array();
    for (size_t i = 0; i < peers_.size(); i++) {
        const auto& peer = peers_[i];
        nlohmann::json p;
        p["hostname"] = peer.hostname;
        p["config_server_id"] = peer.server_id;  // Pre-computed (may not match)
        p["port"] = peer.port;
        p["resolved"] = peer.resolved;
        if (peer.resolved) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer.addr.sin_addr, ip_str, sizeof(ip_str));
            p["resolved_ip"] = std::string(ip_str);
        }
        
        // Find learned identity for this peer
        for (const auto& [learned_id, idx] : learned_server_ids_) {
            if (idx == i) {
                p["learned_server_id"] = learned_id;
                break;
            }
        }
        
        peers_json.push_back(p);
    }
    
    // Build learned identities list
    nlohmann::json learned_json = nlohmann::json::object();
    for (const auto& [server_id, peer_idx] : learned_server_ids_) {
        learned_json[server_id] = peer_idx;
    }
    
    return {
        {"server_id", server_id_},
        {"session_id", session_id_},
        {"port", udp_port_},
        {"peer_count", peers_.size()},
        {"learned_identities_count", learned_server_ids_.size()},
        {"peers", peers_json},
        {"learned_identities", learned_json},
        {"messages_sent", messages_sent_.load()},
        {"messages_received", messages_received_.load()},
        {"messages_dropped", messages_dropped_.load()},
        {"signature_failures", signature_failures_.load()},
        {"sequence_rejections", sequence_rejections_.load()}
    };
}

} // namespace queen

