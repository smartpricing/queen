#pragma once

#include "queen/udp_sync_message.hpp"
#include "queen/config.hpp"
#include <json.hpp>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <netinet/in.h>

namespace queen {

// Forward declaration
class SharedStateManager;

/**
 * Resolved UDP peer endpoint
 */
struct SyncPeerEndpoint {
    std::string hostname;
    std::string server_id;   // Extracted server ID (e.g., "queen-mq-0")
    int port;
    sockaddr_in addr;
    bool resolved = false;
};

/**
 * Sender tracking for sequence validation
 */
struct SenderState {
    std::string session_id;
    uint64_t max_seen_seq = 0;
};

/**
 * UDPSyncTransport
 * 
 * Low-level UDP transport for distributed state synchronization.
 * Handles sending/receiving UDP messages, HMAC validation, and sequence tracking.
 * 
 * Features:
 * - HMAC-SHA256 signature verification (optional, but recommended)
 * - Sequence tracking for replay protection
 * - Session ID tracking for restart detection
 * - Configurable receive buffer size
 */
class UDPSyncTransport {
public:
    using MessageHandler = std::function<void(UDPSyncMessageType, const std::string& sender_id, 
                                              const nlohmann::json& payload)>;
    
    /**
     * Create transport
     * 
     * @param config UDP and shared state configuration
     * @param server_id This server's identifier
     * @param udp_port Port to listen on
     */
    UDPSyncTransport(const SharedStateConfig& config, 
                     const std::string& server_id,
                     int udp_port);
    
    ~UDPSyncTransport();
    
    // Lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    /**
     * Add a peer to send messages to
     * @param hostname Hostname or IP
     * @param port UDP port
     */
    void add_peer(const std::string& hostname, int port);
    
    /**
     * Resolve peer hostnames to IP addresses
     * Called after adding peers, can be called again to re-resolve
     */
    void resolve_peers();
    
    /**
     * Send a message to all peers
     * @param type Message type
     * @param payload JSON payload
     */
    void broadcast(UDPSyncMessageType type, const nlohmann::json& payload);
    
    /**
     * Send a message to specific servers
     * @param server_ids List of server IDs to send to
     * @param type Message type
     * @param payload JSON payload
     */
    void send_to(const std::set<std::string>& server_ids, 
                 UDPSyncMessageType type, 
                 const nlohmann::json& payload);
    
    /**
     * Send a message to a specific server
     * @param server_id Server ID
     * @param type Message type
     * @param payload JSON payload
     */
    void send_to_server(const std::string& server_id,
                        UDPSyncMessageType type,
                        const nlohmann::json& payload);
    
    /**
     * Register handler for message type
     * @param type Message type to handle
     * @param handler Callback function
     */
    void on_message(UDPSyncMessageType type, MessageHandler handler);
    
    /**
     * Get list of peer server IDs
     */
    std::vector<std::string> get_peer_ids() const;
    
    /**
     * Get this server's ID
     */
    const std::string& server_id() const { return server_id_; }
    
    /**
     * Get this server's session ID
     */
    const std::string& session_id() const { return session_id_; }
    
    // Stats
    nlohmann::json get_stats() const;
    uint64_t messages_sent() const { return messages_sent_.load(); }
    uint64_t messages_received() const { return messages_received_.load(); }
    uint64_t messages_dropped() const { return messages_dropped_.load(); }
    size_t peer_count() const { return peers_.size(); }
    
private:
    // Configuration
    SharedStateConfig config_;
    std::string server_id_;
    std::string session_id_;
    int udp_port_;
    
    // Message builder
    std::unique_ptr<UDPSyncMessageBuilder> message_builder_;
    
    // Sockets
    int send_sock_ = -1;
    int recv_sock_ = -1;
    
    // Peers
    std::vector<SyncPeerEndpoint> peers_;
    std::unordered_map<std::string, size_t> peer_by_server_id_;  // server_id → index in peers_
    mutable std::mutex peers_mutex_;
    
    // Sender tracking (for sequence validation)
    std::unordered_map<std::string, SenderState> sender_states_;
    std::mutex sender_mutex_;
    
    // Learned server identities (from heartbeats)
    // Maps declared server_id → peer index
    // This is populated when we receive messages and can match sender address to a peer
    std::unordered_map<std::string, size_t> learned_server_ids_;
    mutable std::mutex learned_ids_mutex_;
    
    // Message handlers
    std::unordered_map<UDPSyncMessageType, std::vector<MessageHandler>> handlers_;
    std::mutex handlers_mutex_;
    
    // Threads
    std::thread recv_thread_;
    std::atomic<bool> running_{false};
    
    // Stats
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> messages_dropped_{0};
    std::atomic<uint64_t> signature_failures_{0};
    std::atomic<uint64_t> sequence_rejections_{0};
    
    // Internal methods
    bool init_sockets();
    void cleanup_sockets();
    void recv_loop();
    bool validate_signature(const UDPSyncMessageParser& parser);
    bool validate_sequence(const std::string& sender_id, 
                          const std::string& session_id, 
                          uint64_t sequence);
    void dispatch_message(const UDPSyncMessageParser& parser);
    void send_raw(const SyncPeerEndpoint& peer, const std::vector<uint8_t>& data);
    
    // Learn peer identity from received message
    // Matches sender_addr to a peer and records their declared server_id
    void learn_peer_identity(const std::string& sender_id, const sockaddr_in& sender_addr);
    
    // Find peer index by network address (returns -1 if not found)
    int find_peer_by_addr(const sockaddr_in& addr) const;
    
    // Extract server ID from hostname (e.g., "queen-mq-0.service" → "queen-mq-0")
    static std::string extract_server_id(const std::string& hostname);
};

} // namespace queen

