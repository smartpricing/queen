#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>
#include <vector>
#include <atomic>
#include <functional>

namespace queen {
namespace caches {

/**
 * Server health entry
 */
struct ServerHealth {
    std::string server_id;
    std::string session_id;      // Random ID, changes on restart
    int64_t last_heartbeat_ms;   // Epoch ms of last heartbeat
    uint64_t last_sequence;      // Last sequence number seen
    bool is_alive;               // Computed based on threshold
};

/**
 * Server Health Tracker
 * 
 * Tracks heartbeats from peer servers to detect failures.
 * 
 * Properties:
 * - Heartbeat every 1 second
 * - Dead threshold: 5 seconds (5 missed heartbeats)
 * - Session ID tracks restarts (sequence resets on restart)
 */
class ServerHealthTracker {
public:
    using OnServerDeadCallback = std::function<void(const std::string& server_id)>;
    
    /**
     * Create tracker with configurable thresholds
     * @param dead_threshold_ms Time without heartbeat before server is considered dead
     */
    explicit ServerHealthTracker(int dead_threshold_ms = 5000)
        : dead_threshold_ms_(dead_threshold_ms) {}
    
    /**
     * Set callback for when a server is detected as dead
     */
    void on_server_dead(OnServerDeadCallback callback) {
        on_dead_callback_ = std::move(callback);
    }
    
    /**
     * Record a heartbeat from a server
     * 
     * @param server_id The server ID
     * @param session_id The session ID (changes on restart)
     * @param sequence The sequence number
     */
    void record_heartbeat(const std::string& server_id,
                          const std::string& session_id,
                          uint64_t sequence) {
        std::unique_lock lock(mutex_);
        
        auto it = servers_.find(server_id);
        if (it == servers_.end()) {
            // New server
            ServerHealth health;
            health.server_id = server_id;
            health.session_id = session_id;
            health.last_heartbeat_ms = now_ms();
            health.last_sequence = sequence;
            health.is_alive = true;
            servers_[server_id] = health;
            return;
        }
        
        auto& health = it->second;
        
        // Check for server restart (session_id changed)
        if (health.session_id != session_id) {
            // Server restarted - reset sequence tracking
            health.session_id = session_id;
            health.last_sequence = sequence;
            health.last_heartbeat_ms = now_ms();
            health.is_alive = true;
            restarts_detected_++;
            return;
        }
        
        // Update existing server
        // Note: We accept any sequence (heartbeats can arrive out of order over UDP)
        health.last_heartbeat_ms = now_ms();
        if (sequence > health.last_sequence) {
            health.last_sequence = sequence;
        }
        health.is_alive = true;
        heartbeats_received_++;
    }
    
    /**
     * Check if a server is alive
     * @param server_id The server ID
     * @return true if alive (received heartbeat within threshold)
     */
    bool is_alive(const std::string& server_id) const {
        std::shared_lock lock(mutex_);
        
        auto it = servers_.find(server_id);
        if (it == servers_.end()) {
            return false;  // Unknown server
        }
        
        int64_t now = now_ms();
        return (now - it->second.last_heartbeat_ms) < dead_threshold_ms_;
    }
    
    /**
     * Get list of all known servers
     */
    std::vector<std::string> get_all_servers() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        result.reserve(servers_.size());
        for (const auto& [id, _] : servers_) {
            result.push_back(id);
        }
        return result;
    }
    
    /**
     * Get list of alive servers
     */
    std::vector<std::string> get_alive_servers() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        int64_t now = now_ms();
        for (const auto& [id, health] : servers_) {
            if ((now - health.last_heartbeat_ms) < dead_threshold_ms_) {
                result.push_back(id);
            }
        }
        return result;
    }
    
    /**
     * Get list of dead servers
     */
    std::vector<std::string> get_dead_servers() const {
        std::shared_lock lock(mutex_);
        std::vector<std::string> result;
        int64_t now = now_ms();
        for (const auto& [id, health] : servers_) {
            if ((now - health.last_heartbeat_ms) >= dead_threshold_ms_) {
                result.push_back(id);
            }
        }
        return result;
    }
    
    /**
     * Check all servers and invoke callback for newly dead ones
     * Should be called periodically (e.g., every heartbeat interval)
     * 
     * @return Number of servers detected as dead
     */
    size_t check_dead_servers() {
        std::vector<std::string> newly_dead;
        
        {
            std::unique_lock lock(mutex_);
            int64_t now = now_ms();
            
            for (auto& [id, health] : servers_) {
                bool was_alive = health.is_alive;
                bool is_now_alive = (now - health.last_heartbeat_ms) < dead_threshold_ms_;
                
                if (was_alive && !is_now_alive) {
                    health.is_alive = false;
                    newly_dead.push_back(id);
                }
            }
        }
        
        // Invoke callbacks outside lock
        if (on_dead_callback_) {
            for (const auto& server_id : newly_dead) {
                on_dead_callback_(server_id);
            }
        }
        
        return newly_dead.size();
    }
    
    /**
     * Get health info for a specific server
     */
    std::optional<ServerHealth> get_health(const std::string& server_id) const {
        std::shared_lock lock(mutex_);
        auto it = servers_.find(server_id);
        if (it != servers_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    /**
     * Clear tracking for a specific server
     */
    void remove_server(const std::string& server_id) {
        std::unique_lock lock(mutex_);
        servers_.erase(server_id);
    }
    
    /**
     * Clear all tracking
     */
    void clear() {
        std::unique_lock lock(mutex_);
        servers_.clear();
    }
    
    // Stats
    size_t server_count() const {
        std::shared_lock lock(mutex_);
        return servers_.size();
    }
    
    uint64_t heartbeats_received() const { return heartbeats_received_.load(); }
    uint64_t restarts_detected() const { return restarts_detected_.load(); }
    
private:
    int dead_threshold_ms_;
    std::unordered_map<std::string, ServerHealth> servers_;
    mutable std::shared_mutex mutex_;
    
    OnServerDeadCallback on_dead_callback_;
    
    std::atomic<uint64_t> heartbeats_received_{0};
    std::atomic<uint64_t> restarts_detected_{0};
    
    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
};

} // namespace caches
} // namespace queen


