#pragma once

#include "queen/config.hpp"
#include "queen/udp_sync_transport.hpp"
#include "queen/udp_sync_message.hpp"
#include "queen/caches/queue_config_cache.hpp"
#include "queen/caches/consumer_presence_cache.hpp"
#include "queen/caches/partition_id_cache.hpp"
#include "queen/caches/lease_hint_cache.hpp"
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
class PollIntentionRegistry;
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
 * 1. QueueConfigCache - Full sync, queue configurations
 * 2. ConsumerPresenceCache - Which servers have consumers for which queues
 * 3. PartitionIdCache - Local LRU cache for partition UUIDs
 * 4. LeaseHintCache - Hints about lease ownership
 * 5. ServerHealthTracker - Heartbeat-based health monitoring
 */
class SharedStateManager {
public:
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
    // Partition ID (Tier 3) - Local LRU cache
    // ============================================================
    
    /**
     * Get partition ID from cache
     * @param queue Queue name
     * @param partition Partition name
     * @return Partition UUID if cached
     */
    std::optional<std::string> get_partition_id(const std::string& queue, 
                                                 const std::string& partition);
    
    /**
     * Cache a partition ID (local only, not broadcast)
     * @param queue Queue name
     * @param partition Partition name
     * @param partition_id Partition UUID
     * @param queue_id Optional queue UUID
     */
    void cache_partition_id(const std::string& queue, 
                           const std::string& partition,
                           const std::string& partition_id,
                           const std::string& queue_id = "");
    
    /**
     * Invalidate a partition from cache (e.g., after DB error or deletion)
     * @param queue Queue name
     * @param partition Partition name
     * @param broadcast If true, broadcast PARTITION_DELETED to peers
     */
    void invalidate_partition(const std::string& queue, 
                             const std::string& partition,
                             bool broadcast = false);
    
    // ============================================================
    // Lease Hints (Tier 4) - Advisory hints
    // ============================================================
    
    /**
     * Check if partition is likely leased elsewhere
     * @param partition_id Partition UUID
     * @param consumer_group Consumer group
     * @return true if hint says another server has it
     */
    bool is_likely_leased_elsewhere(const std::string& partition_id,
                                    const std::string& consumer_group);
    
    /**
     * Record lease acquisition and broadcast hint
     * @param partition_id Partition UUID
     * @param consumer_group Consumer group
     * @param lease_time_seconds Lease duration
     */
    void hint_lease_acquired(const std::string& partition_id,
                            const std::string& consumer_group,
                            int lease_time_seconds);
    
    /**
     * Record lease release and broadcast hint
     * @param partition_id Partition UUID
     * @param consumer_group Consumer group
     */
    void hint_lease_released(const std::string& partition_id,
                            const std::string& consumer_group);
    
    // ============================================================
    // Server Health (Tier 5) - Heartbeat monitoring
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
    // Tier 6: Local Sidecar Registry (for POP_WAIT notifications)
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
    // Tier 7: Group Backoff Coordination (local only, not synced via UDP)
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
    void release_group(const std::string& group_key, bool had_messages);
    
    /**
     * Get current backoff interval for a group
     */
    std::chrono::milliseconds get_group_interval(const std::string& group_key);
    
    /**
     * Reset backoff for all groups of a queue (called on push notification)
     */
    void reset_backoff_for_queue(const std::string& queue_name);
    
    // ============================================================
    // Notifications (for InterInstanceComms compatibility)
    // Also notifies local sidecars for POP_WAIT
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
    caches::PartitionIdCache partition_ids_;
    caches::LeaseHintCache lease_hints_;
    caches::ServerHealthTracker server_health_;
    
    // Background threads
    std::thread heartbeat_thread_;
    std::thread refresh_thread_;
    std::thread cleanup_thread_;
    std::thread dns_refresh_thread_;
    
    // ============================================================
    // Tier 6: Local Sidecar Registry
    // ============================================================
    std::vector<SidecarDbPool*> local_sidecars_;
    mutable std::shared_mutex sidecar_mutex_;
    
    // ============================================================
    // Tier 7: Group Backoff State (local only)
    // ============================================================
    struct GroupBackoffState {
        std::chrono::steady_clock::time_point last_checked;
        int consecutive_empty = 0;
        std::chrono::milliseconds current_interval{100};
        bool in_flight = false;  // Protected by backoff_mutex_, not atomic
    };
    std::unordered_map<std::string, GroupBackoffState> group_backoff_;
    mutable std::mutex backoff_mutex_;  // Regular mutex, not shared_mutex (simpler, no atomic in struct)
    
    // Backoff configuration (from config or defaults)
    int backoff_threshold_ = 3;
    double backoff_multiplier_ = 2.0;
    int max_interval_ms_ = 1000;
    int base_interval_ms_ = 100;
    
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
    void handle_partition_deleted(const std::string& sender, const nlohmann::json& payload);
    void handle_lease_hint_acquired(const std::string& sender, const nlohmann::json& payload);
    void handle_lease_hint_released(const std::string& sender, const nlohmann::json& payload);
    
    // ============================================================
    // Background Tasks
    // ============================================================
    
    void heartbeat_loop();
    void refresh_loop();
    void cleanup_loop();
    void dns_refresh_loop();
    
    void refresh_queue_configs_from_db();
    
    // ============================================================
    // Callbacks
    // ============================================================
    
    void on_server_dead(const std::string& server_id);
};

// Global instance
extern std::shared_ptr<SharedStateManager> global_shared_state;

} // namespace queen

