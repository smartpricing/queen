#pragma once

#include "queen/config.hpp"
#include "queen/udp_sync_transport.hpp"
#include "queen/udp_sync_message.hpp"
#include "queen/caches/queue_config_cache.hpp"
#include "queen/caches/server_health_tracker.hpp"
#include <json.hpp>
#include <memory>
#include <string>
#include <optional>
#include <thread>
#include <atomic>
#include <functional>

namespace queen {

// Forward declarations
class AsyncDbPool;

/**
 * SharedStateManager
 * 
 * Central manager for distributed cache synchronization between Queen MQ instances.
 * 
 * Design principle: Cache is ALWAYS advisory. PostgreSQL is ALWAYS authoritative.
 * Stale or missing cache data must NEVER break correctness - only performance.
 * 
 * Features:
 * 1. QueueConfigCache - Queue configurations (used for encryption checks)
 * 2. Maintenance Mode - Global flag to pause push operations
 * 3. Heartbeats - Server health monitoring
 * 4. MESSAGE_AVAILABLE notifications - Trigger pop backoff reset via Queen library
 */
class SharedStateManager {
public:
    // Callback type for notifying Queen library about new messages
    using MessageAvailableCallback = std::function<void(const std::string& queue, const std::string& partition)>;

    /**
     * Create shared state manager
     * 
     * @param config Inter-instance configuration (includes SharedStateConfig)
     * @param server_id This server's identifier (e.g., "queen-mq-0")
     * @param db_pool Database pool for background refresh
     */
    SharedStateManager(const InterInstanceConfig& config,
                       const std::string& server_id,
                       std::shared_ptr<AsyncDbPool> db_pool = nullptr);
    
    ~SharedStateManager();
    
    // Lifecycle
    void start();
    void stop();
    bool is_enabled() const { return enabled_; }
    bool is_running() const { return running_; }
    
    // ============================================================
    // Queue Config Cache
    // ============================================================
    
    /**
     * Get queue config from cache
     * @param queue Queue name
     * @return Config if cached, nullopt if miss (caller should query DB)
     */
    std::optional<caches::CachedQueueConfig> get_queue_config(const std::string& queue);
    
    /**
     * Set queue config and broadcast to peers
     * @param queue Queue name
     * @param config Configuration
     */
    void set_queue_config(const std::string& queue, const caches::CachedQueueConfig& config);
    
    /**
     * Delete queue config and broadcast to peers
     * @param queue Queue name
     */
    void delete_queue_config(const std::string& queue);
    
    // ============================================================
    // Maintenance Mode (PUSH - buffers writes to file)
    // ============================================================
    
    /**
     * Get push maintenance mode status (cached, never hits DB)
     * @return true if push maintenance mode is enabled
     */
    bool get_maintenance_mode() const { return maintenance_mode_.load(); }
    
    /**
     * Set push maintenance mode and persist to DB
     * Also broadcasts to other instances via UDP
     * @param enabled New maintenance mode status
     */
    void set_maintenance_mode(bool enabled);
    
    // ============================================================
    // Pop Maintenance Mode (POP - returns empty to consumers)
    // ============================================================
    
    /**
     * Get pop maintenance mode status (cached, never hits DB)
     * @return true if pop maintenance mode is enabled
     */
    bool get_pop_maintenance_mode() const { return pop_maintenance_mode_.load(); }
    
    /**
     * Set pop maintenance mode and persist to DB
     * Also broadcasts to other instances via UDP
     * When enabled, all POP operations return empty arrays
     * @param enabled New pop maintenance mode status
     */
    void set_pop_maintenance_mode(bool enabled);
    
    // ============================================================
    // Message Available Notification
    // ============================================================
    
    /**
     * Register callback to be invoked when MESSAGE_AVAILABLE is received
     * This is called from the UDP receive thread, so the callback should be thread-safe
     * (e.g., Queen::update_pop_backoff_tracker which uses uv_async)
     */
    void on_message_available(MessageAvailableCallback callback);
    
    /**
     * Notify that a message is available (called after PUSH)
     * Broadcasts to other servers via UDP
     */
    void notify_message_available(const std::string& queue, const std::string& partition);
    
    // ============================================================
    // Server Health (read-only, for monitoring)
    // ============================================================
    
    std::vector<std::string> get_alive_servers();
    std::vector<std::string> get_dead_servers();
    
    // ============================================================
    // Stats & Monitoring
    // ============================================================
    
    nlohmann::json get_stats() const;
    
    /**
     * Get the underlying transport (for advanced use)
     */
    std::shared_ptr<UDPSyncTransport> get_transport() const { return transport_; }
    
    /**
     * Get this server's ID
     */
    const std::string& server_id() const { return server_id_; }
    
private:
    // Configuration
    InterInstanceConfig config_;
    std::string server_id_;
    bool enabled_ = false;
    std::atomic<bool> running_{false};
    
    // Database pool for background refresh
    std::shared_ptr<AsyncDbPool> db_pool_;
    
    // Transport
    std::shared_ptr<UDPSyncTransport> transport_;
    
    // Caches
    caches::QueueConfigCache queue_configs_;
    caches::ServerHealthTracker server_health_;
    
    // Maintenance mode (global atomic cache)
    std::atomic<bool> maintenance_mode_{false};
    
    // Pop maintenance mode (global atomic cache)
    std::atomic<bool> pop_maintenance_mode_{false};
    
    // Message available callback (called from UDP thread)
    MessageAvailableCallback message_available_callback_;
    std::mutex callback_mutex_;
    
    // Background threads
    std::thread heartbeat_thread_;
    std::thread refresh_thread_;
    std::thread dns_refresh_thread_;
    
    // ============================================================
    // Message Handlers
    // ============================================================
    
    void setup_message_handlers();
    
    void handle_message_available(const std::string& sender, const nlohmann::json& payload);
    void handle_heartbeat(const std::string& sender, const nlohmann::json& payload);
    void handle_queue_config_set(const std::string& sender, const nlohmann::json& payload);
    void handle_queue_config_delete(const std::string& sender, const nlohmann::json& payload);
    void handle_maintenance_mode_set(const std::string& sender, const nlohmann::json& payload);
    void handle_pop_maintenance_mode_set(const std::string& sender, const nlohmann::json& payload);
    
    // ============================================================
    // Background Tasks
    // ============================================================
    
    void heartbeat_loop();
    void refresh_loop();
    void dns_refresh_loop();
    
    void refresh_queue_configs_from_db();
    void refresh_maintenance_mode_from_db();
    void refresh_pop_maintenance_mode_from_db();
};

// Global instance
extern std::shared_ptr<SharedStateManager> global_shared_state;

} // namespace queen
