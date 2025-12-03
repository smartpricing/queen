#pragma once

#include "queen/config.hpp"
#include "queen/udp_sync_transport.hpp"
#include "queen/udp_sync_message.hpp"
#include "queen/caches/queue_config_cache.hpp"
#include "queen/caches/consumer_presence_cache.hpp"
#include "queen/caches/server_health_tracker.hpp"
#include <json.hpp>
#include <memory>
#include <string>
#include <set>
#include <optional>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <chrono>
#include <unordered_map>

namespace queen {

// Forward declarations
class AsyncDbPool;
class SidecarDbPool;

/**
 * SharedStateManager
 * 
 * Central manager for distributed cache synchronization between Queen MQ instances.
 * Coordinates all cache tiers and handles UDP message dispatching.
 * 
 * Design principle: Cache is ALWAYS advisory. PostgreSQL is ALWAYS authoritative.
 * Stale or missing cache data must NEVER break correctness - only performance.
 * 
 * Cache Tiers:
 * 1. QueueConfigCache - Full sync, queue configurations (used for encryption checks)
 * 2. ConsumerPresenceCache - Which servers have consumers for which queues
 * 3. ServerHealthTracker - Heartbeat-based health monitoring
 * 
 * Note: PartitionIdCache and LeaseHintCache were removed as stored procedures
 * now handle partition ID resolution and lease checks internally.
 */
class SharedStateManager {
public:
    /**
     * Create shared state manager
     * 
     * @param config Inter-instance configuration (includes SharedStateConfig)
     * @param server_id This server's identifier (e.g., "queen-mq-0")
     * @param db_pool Database pool for background refresh
     * @param backoff_cleanup_threshold_seconds Stale backoff entries older than this are cleaned up (default: 3600s = 1 hour)
     * @param pop_wait_initial_interval_ms Initial poll interval for POP_WAIT (default: 100ms)
     * @param pop_wait_backoff_threshold Consecutive empty checks before backoff (default: 3)
     * @param pop_wait_backoff_multiplier Exponential backoff multiplier (default: 2.0)
     * @param pop_wait_max_interval_ms Max poll interval after backoff (default: 1000ms)
     */
    SharedStateManager(const InterInstanceConfig& config,
                       const std::string& server_id,
                       std::shared_ptr<AsyncDbPool> db_pool = nullptr,
                       int backoff_cleanup_threshold_seconds = 3600,
                       int pop_wait_initial_interval_ms = 100,
                       int pop_wait_backoff_threshold = 3,
                       double pop_wait_backoff_multiplier = 2.0,
                       int pop_wait_max_interval_ms = 1000);
    
    ~SharedStateManager();
    
    // Lifecycle
    void start();
    void stop();
    bool is_enabled() const { return enabled_; }
    bool is_running() const { return running_; }
    
    // ============================================================
    // Queue Config (Tier 1) - Full sync
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
    // Consumer Presence (Tier 2) - Queue-level tracking
    // ============================================================
    
    /**
     * Get servers that have consumers for a queue
     * @param queue Queue name
     * @return Set of server IDs (empty = fallback to broadcast all)
     */
    std::set<std::string> get_servers_for_queue(const std::string& queue);
    
    /**
     * Register this server as having consumers for a queue
     * @param queue Queue name
     */
    void register_consumer(const std::string& queue);
    
    /**
     * Deregister this server from having consumers for a queue
     * @param queue Queue name
     */
    void deregister_consumer(const std::string& queue);
    
    // ============================================================
    // Server Health (Tier 3) - Heartbeat monitoring
    // ============================================================
    
    /**
     * Check if a server is alive
     * @param server_id Server ID
     * @return true if alive (received heartbeat recently)
     */
    bool is_server_alive(const std::string& server_id);
    
    /**
     * Get list of dead servers
     */
    std::vector<std::string> get_dead_servers();
    
    /**
     * Get list of alive servers
     */
    std::vector<std::string> get_alive_servers();
    
    // ============================================================
    // Tier 4: Local Sidecar Registry (for POP_WAIT notifications)
    // ============================================================
    
    /**
     * Register a local sidecar for notifications
     * Called when each worker creates its sidecar
     */
    void register_sidecar(SidecarDbPool* sidecar);
    
    /**
     * Unregister a local sidecar
     * Called when worker shuts down
     */
    void unregister_sidecar(SidecarDbPool* sidecar);
    
    // ============================================================
    // Tier 5: Group Backoff Coordination (local only, not synced via UDP)
    // Used by sidecars to coordinate POP_WAIT polling
    // ============================================================
    
    /**
     * Check if a group should be checked now (respects backoff interval)
     * @param group_key Format: "queue/partition/consumer_group"
     */
    bool should_check_group(const std::string& group_key);
    
    /**
     * Try to acquire exclusive access for querying a group
     * Prevents multiple sidecars from querying the same group simultaneously
     * @return true if acquired, false if another sidecar is already querying
     */
    bool try_acquire_group(const std::string& group_key);
    
    /**
     * Release group lock and update backoff based on result
     * @param had_messages true if messages were found (resets backoff)
     */
    void release_group(const std::string& group_key, bool had_messages, int worker_id = -1);
    
    /**
     * Get current backoff interval for a group
     */
    std::chrono::milliseconds get_group_interval(const std::string& group_key);
    
    /**
     * Get backoff configuration values
     */
    int get_base_interval_ms() const { return base_interval_ms_; }
    int get_max_interval_ms() const { return max_interval_ms_; }
    int get_backoff_threshold() const { return backoff_threshold_; }
    
    /**
     * Reset backoff for all groups of a queue (called on push notification)
     */
    void reset_backoff_for_queue(const std::string& queue_name);
    
    // ============================================================
    // Notifications (local sidecars + cluster broadcast via UDP)
    // ============================================================
    
    /**
     * Send MESSAGE_AVAILABLE notification to servers with consumers
     * Falls back to broadcast if no presence info
     * ALSO notifies local sidecars (for POP_WAIT)
     */
    void notify_message_available(const std::string& queue, const std::string& partition);
    
    /**
     * Send PARTITION_FREE notification
     * ALSO notifies local sidecars (for POP_WAIT)
     */
    void notify_partition_free(const std::string& queue, 
                              const std::string& partition,
                              const std::string& consumer_group);
    
    // ============================================================
    // Stats & Monitoring
    // ============================================================
    
    nlohmann::json get_stats() const;
    
    /**
     * Get aggregated sidecar operation statistics
     * Aggregates across all local sidecars (all workers)
     */
    nlohmann::json get_aggregated_sidecar_stats() const;
    
    /**
     * Get per-queue backoff summary
     * Returns array of queues with their backoff state
     */
    nlohmann::json get_queue_backoff_summary() const;
    
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
    caches::ConsumerPresenceCache consumer_presence_;
    caches::ServerHealthTracker server_health_;
    
    // Background threads
    std::thread heartbeat_thread_;
    std::thread refresh_thread_;
    std::thread dns_refresh_thread_;
    
    // ============================================================
    // Tier 4: Local Sidecar Registry
    // ============================================================
    std::vector<SidecarDbPool*> local_sidecars_;
    mutable std::shared_mutex sidecar_mutex_;
    
    // ============================================================
    // Tier 5: Group Backoff State (local only)
    // ============================================================
    struct GroupBackoffState {
        std::chrono::steady_clock::time_point last_checked;
        std::chrono::steady_clock::time_point last_accessed;  // For cleanup - when was this entry last used
        int consecutive_empty = 0;
        std::chrono::milliseconds current_interval{100};
        bool in_flight = false;  // Protected by backoff_mutex_, not atomic
        
        GroupBackoffState() 
            : last_checked(std::chrono::steady_clock::now())
            , last_accessed(std::chrono::steady_clock::now()) {}
    };
    std::unordered_map<std::string, GroupBackoffState> group_backoff_;
    mutable std::mutex backoff_mutex_;  // Regular mutex, not shared_mutex (simpler, no atomic in struct)
    
    // POP_WAIT backoff configuration (from env vars or defaults)
    int backoff_threshold_ = 3;           // POP_WAIT_BACKOFF_THRESHOLD
    double backoff_multiplier_ = 2.0;     // POP_WAIT_BACKOFF_MULTIPLIER
    int max_interval_ms_ = 1000;          // POP_WAIT_MAX_INTERVAL_MS
    int base_interval_ms_ = 100;          // POP_WAIT_INITIAL_INTERVAL_MS
    int backoff_cleanup_threshold_seconds_ = 3600;  // QUEUE_BACKOFF_CLEANUP_THRESHOLD
    
    // Internal: notify all local sidecars
    void notify_local_sidecars(const std::string& queue_name);
    
    // ============================================================
    // Message Handlers
    // ============================================================
    
    void setup_message_handlers();
    
    void handle_message_available(const std::string& sender, const nlohmann::json& payload);
    void handle_partition_free(const std::string& sender, const nlohmann::json& payload);
    void handle_heartbeat(const std::string& sender, const nlohmann::json& payload);
    void handle_queue_config_set(const std::string& sender, const nlohmann::json& payload);
    void handle_queue_config_delete(const std::string& sender, const nlohmann::json& payload);
    void handle_consumer_registered(const std::string& sender, const nlohmann::json& payload);
    void handle_consumer_deregistered(const std::string& sender, const nlohmann::json& payload);
    
    // ============================================================
    // Background Tasks
    // ============================================================
    
    void heartbeat_loop();
    void refresh_loop();
    void dns_refresh_loop();
    
    void refresh_queue_configs_from_db();
    
    /**
     * Cleanup stale backoff entries that haven't been accessed recently
     * Called periodically from heartbeat_loop to prevent unbounded memory growth
     * @return Number of entries removed
     */
    size_t cleanup_stale_backoff_entries();
    
    // ============================================================
    // Callbacks
    // ============================================================
    
    void on_server_dead(const std::string& server_id);
};

// Global instance
extern std::shared_ptr<SharedStateManager> global_shared_state;

} // namespace queen
