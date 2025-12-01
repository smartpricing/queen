#include "queen/shared_state_manager.hpp"
#include "queen/async_database.hpp"
#include "queen/sidecar_db_pool.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

namespace queen {

// Global instance
std::shared_ptr<SharedStateManager> global_shared_state;

SharedStateManager::SharedStateManager(const InterInstanceConfig& config,
                                       const std::string& server_id,
                                       std::shared_ptr<AsyncDbPool> db_pool,
                                       int backoff_cleanup_threshold_seconds,
                                       int pop_wait_initial_interval_ms,
                                       int pop_wait_backoff_threshold,
                                       double pop_wait_backoff_multiplier,
                                       int pop_wait_max_interval_ms)
    : config_(config)
    , server_id_(server_id)
    , db_pool_(db_pool)
    , server_health_(config.shared_state.dead_threshold_ms)
    , backoff_threshold_(pop_wait_backoff_threshold)
    , backoff_multiplier_(pop_wait_backoff_multiplier)
    , max_interval_ms_(pop_wait_max_interval_ms)
    , base_interval_ms_(pop_wait_initial_interval_ms)
    , backoff_cleanup_threshold_seconds_(backoff_cleanup_threshold_seconds)
{
    // Enable if UDP peers are configured
    enabled_ = config.shared_state.enabled && config.has_udp_peers();
    
    if (enabled_) {
        // Create transport
        transport_ = std::make_shared<UDPSyncTransport>(
            config.shared_state,
            server_id,
            config.udp_port
        );
        
        // Add peers
        for (const auto& peer : config.parse_udp_peers()) {
            transport_->add_peer(peer.host, peer.port);
        }
        
        // Set up dead server callback
        server_health_.on_server_dead([this](const std::string& sid) {
            on_server_dead(sid);
        });
        
        spdlog::info("SharedStateManager: Enabled with {} peers, server_id={}",
                    config.parse_udp_peers().size(), server_id);
    } else {
        spdlog::info("SharedStateManager: Disabled (no UDP peers or sync disabled)");
    }
}

SharedStateManager::~SharedStateManager() {
    stop();
}

void SharedStateManager::start() {
    // Always load queue configs on startup (needed for encryption even when sync disabled)
    if (db_pool_) {
        spdlog::info("SharedStateManager: Loading queue configs from database...");
        refresh_queue_configs_from_db();
    }
    
    if (!enabled_ || running_) {
        if (!enabled_) {
            spdlog::info("SharedStateManager: Sync disabled, but queue config cache is available");
        }
        return;
    }
    
    // Resolve peer hostnames
    transport_->resolve_peers();
    
    // Set up message handlers
    setup_message_handlers();
    
    // Start transport
    if (!transport_->start()) {
        spdlog::error("SharedStateManager: Failed to start transport");
        return;
    }
    
    running_ = true;
    
    // Start background threads
    heartbeat_thread_ = std::thread(&SharedStateManager::heartbeat_loop, this);
    
    if (db_pool_) {
        refresh_thread_ = std::thread(&SharedStateManager::refresh_loop, this);
    }
    
    dns_refresh_thread_ = std::thread(&SharedStateManager::dns_refresh_loop, this);
    
    spdlog::info("SharedStateManager: Started");
}

void SharedStateManager::stop() {
    if (!running_) return;
    
    spdlog::info("SharedStateManager: Stopping...");
    running_ = false;
    
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (refresh_thread_.joinable()) refresh_thread_.join();
    if (dns_refresh_thread_.joinable()) dns_refresh_thread_.join();
    
    if (transport_) {
        transport_->stop();
    }
    
    spdlog::info("SharedStateManager: Stopped");
}

// ============================================================
// Queue Config (Tier 1)
// ============================================================

std::optional<caches::CachedQueueConfig> SharedStateManager::get_queue_config(const std::string& queue) {
    return queue_configs_.get(queue);
}

void SharedStateManager::set_queue_config(const std::string& queue, const caches::CachedQueueConfig& config) {
    queue_configs_.set(queue, config);
    
    if (running_ && transport_) {
        nlohmann::json payload = {
            {"queue", queue},
            {"config", {
                {"id", config.id},
                {"lease_time", config.lease_time},
                {"retry_limit", config.retry_limit},
                {"retry_delay", config.retry_delay},
                {"delayed_processing", config.delayed_processing},
                {"window_buffer", config.window_buffer},
                {"encryption_enabled", config.encryption_enabled},
                {"dlq_enabled", config.dlq_enabled},
                {"dlq_after_max_retries", config.dlq_after_max_retries},
                {"max_size", config.max_size},
                {"ttl", config.ttl},
                {"priority", config.priority},
                {"namespace_name", config.namespace_name},
                {"task", config.task},
                {"retention_seconds", config.retention_seconds},
                {"completed_retention_seconds", config.completed_retention_seconds},
                {"retention_enabled", config.retention_enabled},
                {"max_wait_time_seconds", config.max_wait_time_seconds}
            }},
            {"version", config.version}
        };
        
        transport_->broadcast(UDPSyncMessageType::QUEUE_CONFIG_SET, payload);
    }
}

void SharedStateManager::delete_queue_config(const std::string& queue) {
    queue_configs_.remove(queue);
    
    if (running_ && transport_) {
        nlohmann::json payload = {{"queue", queue}};
        transport_->broadcast(UDPSyncMessageType::QUEUE_CONFIG_DELETE, payload);
    }
}

// ============================================================
// Consumer Presence (Tier 2)
// ============================================================

std::set<std::string> SharedStateManager::get_servers_for_queue(const std::string& queue) {
    return consumer_presence_.get_servers_for_queue(queue);
}

void SharedStateManager::register_consumer(const std::string& queue) {
    consumer_presence_.register_consumer(queue, server_id_);
    
    if (running_ && transport_) {
        nlohmann::json payload = {
            {"queue", queue},
            {"server_id", server_id_}
        };
        transport_->broadcast(UDPSyncMessageType::CONSUMER_REGISTERED, payload);
    }
}

void SharedStateManager::deregister_consumer(const std::string& queue) {
    consumer_presence_.deregister_consumer(queue, server_id_);
    
    if (running_ && transport_) {
        nlohmann::json payload = {
            {"queue", queue},
            {"server_id", server_id_}
        };
        transport_->broadcast(UDPSyncMessageType::CONSUMER_DEREGISTERED, payload);
    }
}

// ============================================================
// Server Health (Tier 3)
// ============================================================

bool SharedStateManager::is_server_alive(const std::string& server_id) {
    if (server_id == server_id_) return true;  // We're always alive to ourselves
    return server_health_.is_alive(server_id);
}

std::vector<std::string> SharedStateManager::get_dead_servers() {
    return server_health_.get_dead_servers();
}

std::vector<std::string> SharedStateManager::get_alive_servers() {
    return server_health_.get_alive_servers();
}

// ============================================================
// Notifications
// ============================================================

void SharedStateManager::notify_message_available(const std::string& queue, const std::string& partition) {
    // ALWAYS: Notify local sidecars (works in single-node mode)
    notify_local_sidecars(queue);
    reset_backoff_for_queue(queue);
    
    // CLUSTER: If UDP enabled, broadcast to other servers
    if (running_ && transport_) {
        nlohmann::json payload = {
            {"queue", queue},
            {"partition", partition},
            {"ts", now_ms()}
        };
        
        auto servers = get_servers_for_queue(queue);
        
        if (servers.empty()) {
            // No presence info - broadcast to all
            transport_->broadcast(UDPSyncMessageType::MESSAGE_AVAILABLE, payload);
        } else {
            // Remove ourselves
            servers.erase(server_id_);
            if (!servers.empty()) {
                transport_->send_to(servers, UDPSyncMessageType::MESSAGE_AVAILABLE, payload);
            }
        }
    }
}

void SharedStateManager::notify_partition_free(const std::string& queue,
                                              const std::string& partition,
                                              const std::string& consumer_group) {
    // ALWAYS: Notify local sidecars (works in single-node mode)
    notify_local_sidecars(queue);
    reset_backoff_for_queue(queue);
    
    // CLUSTER: If UDP enabled, broadcast to other servers
    if (running_ && transport_) {
        nlohmann::json payload = {
            {"queue", queue},
            {"partition", partition},
            {"consumer_group", consumer_group},
            {"ts", now_ms()}
        };
        
        auto servers = get_servers_for_queue(queue);
        
        if (servers.empty()) {
            transport_->broadcast(UDPSyncMessageType::PARTITION_FREE, payload);
        } else {
            servers.erase(server_id_);
            if (!servers.empty()) {
                transport_->send_to(servers, UDPSyncMessageType::PARTITION_FREE, payload);
            }
        }
    }
}

// ============================================================
// Tier 4: Local Sidecar Registry
// ============================================================

void SharedStateManager::register_sidecar(SidecarDbPool* sidecar) {
    std::unique_lock lock(sidecar_mutex_);
    local_sidecars_.push_back(sidecar);
    spdlog::debug("SharedState: Registered sidecar (total: {})", local_sidecars_.size());
}

void SharedStateManager::unregister_sidecar(SidecarDbPool* sidecar) {
    std::unique_lock lock(sidecar_mutex_);
    local_sidecars_.erase(
        std::remove(local_sidecars_.begin(), local_sidecars_.end(), sidecar),
        local_sidecars_.end()
    );
    spdlog::debug("SharedState: Unregistered sidecar (total: {})", local_sidecars_.size());
}

void SharedStateManager::notify_local_sidecars(const std::string& queue_name) {
    std::shared_lock lock(sidecar_mutex_);
    for (auto* sidecar : local_sidecars_) {
        if (sidecar) {
            sidecar->notify_queue_activity(queue_name);
        }
    }
    if (!local_sidecars_.empty()) {
        spdlog::debug("SharedState: Notified {} local sidecars for queue {}", 
                     local_sidecars_.size(), queue_name);
    }
}

// ============================================================
// Tier 5: Group Backoff Coordination
// ============================================================

bool SharedStateManager::should_check_group(const std::string& group_key) {
    std::lock_guard lock(backoff_mutex_);
    auto it = group_backoff_.find(group_key);
    if (it == group_backoff_.end()) {
        return true;  // New group, check immediately
    }
    
    auto now = std::chrono::steady_clock::now();
    return (now - it->second.last_checked) >= it->second.current_interval;
}

bool SharedStateManager::try_acquire_group(const std::string& group_key) {
    std::lock_guard lock(backoff_mutex_);
    auto& state = group_backoff_[group_key];  // Creates if not exists
    
    // Update last_accessed for cleanup tracking
    state.last_accessed = std::chrono::steady_clock::now();
    
    if (state.in_flight) {
        return false;  // Already being queried
    }
    state.in_flight = true;
    return true;
}

void SharedStateManager::release_group(const std::string& group_key, bool had_messages) {
    std::lock_guard lock(backoff_mutex_);
    auto it = group_backoff_.find(group_key);
    if (it == group_backoff_.end()) {
        return;
    }
    
    auto& state = it->second;
    auto now = std::chrono::steady_clock::now();
    state.last_checked = now;
    state.last_accessed = now;  // Update for cleanup tracking
    
    if (had_messages) {
        // Reset backoff
        state.consecutive_empty = 0;
        state.current_interval = std::chrono::milliseconds(base_interval_ms_);
    } else {
        // Increase backoff
        state.consecutive_empty++;
        if (state.consecutive_empty >= backoff_threshold_) {
            int new_interval = static_cast<int>(state.current_interval.count() * backoff_multiplier_);
            state.current_interval = std::chrono::milliseconds(std::min(new_interval, max_interval_ms_));
        }
    }
    
    state.in_flight = false;
}

std::chrono::milliseconds SharedStateManager::get_group_interval(const std::string& group_key) {
    std::lock_guard lock(backoff_mutex_);
    auto it = group_backoff_.find(group_key);
    if (it == group_backoff_.end()) {
        return std::chrono::milliseconds(base_interval_ms_);
    }
    return it->second.current_interval;
}

void SharedStateManager::reset_backoff_for_queue(const std::string& queue_name) {
    std::lock_guard lock(backoff_mutex_);
    std::string prefix = queue_name + "/";
    for (auto& [key, state] : group_backoff_) {
        // Check if key starts with "queue_name/"
        if (key.size() >= prefix.size() && key.compare(0, prefix.size(), prefix) == 0) {
            state.consecutive_empty = 0;
            state.current_interval = std::chrono::milliseconds(base_interval_ms_);
            // Note: Don't touch in_flight - let current query complete
        }
    }
}

// ============================================================
// Message Handlers
// ============================================================

void SharedStateManager::setup_message_handlers() {
    using Type = UDPSyncMessageType;
    
    transport_->on_message(Type::MESSAGE_AVAILABLE, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_message_available(sender, p);
    });
    
    transport_->on_message(Type::PARTITION_FREE, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_partition_free(sender, p);
    });
    
    transport_->on_message(Type::HEARTBEAT, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_heartbeat(sender, p);
    });
    
    transport_->on_message(Type::QUEUE_CONFIG_SET, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_queue_config_set(sender, p);
    });
    
    transport_->on_message(Type::QUEUE_CONFIG_DELETE, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_queue_config_delete(sender, p);
    });
    
    transport_->on_message(Type::CONSUMER_REGISTERED, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_consumer_registered(sender, p);
    });
    
    transport_->on_message(Type::CONSUMER_DEREGISTERED, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_consumer_deregistered(sender, p);
    });
}

void SharedStateManager::handle_message_available(const std::string& sender, const nlohmann::json& payload) {
    std::string queue = payload.value("queue", "");
    spdlog::debug("SharedState: MESSAGE_AVAILABLE from {} for {}:{}",
                 sender, queue, payload.value("partition", ""));
    
    // Forward to local sidecars (received from another server)
    if (!queue.empty()) {
        notify_local_sidecars(queue);
        reset_backoff_for_queue(queue);
    }
}

void SharedStateManager::handle_partition_free(const std::string& sender, const nlohmann::json& payload) {
    std::string queue = payload.value("queue", "");
    spdlog::debug("SharedState: PARTITION_FREE from {} for {}:{}:{}",
                 sender, queue, payload.value("partition", ""),
                 payload.value("consumer_group", ""));
    
    // Forward to local sidecars (received from another server)
    if (!queue.empty()) {
        notify_local_sidecars(queue);
        reset_backoff_for_queue(queue);
    }
}

void SharedStateManager::handle_heartbeat(const std::string& sender, const nlohmann::json& payload) {
    std::string session_id = payload.value("session_id", "");
    uint64_t sequence = payload.value("sequence", 0ULL);
    
    server_health_.record_heartbeat(sender, session_id, sequence);
}

void SharedStateManager::handle_queue_config_set(const std::string& sender, const nlohmann::json& payload) {
    std::string queue = payload.value("queue", "");
    if (queue.empty()) return;
    
    auto config_json = payload.value("config", nlohmann::json{});
    
    caches::CachedQueueConfig config;
    config.name = queue;
    config.id = config_json.value("id", "");
    config.lease_time = config_json.value("lease_time", 300);
    config.retry_limit = config_json.value("retry_limit", 3);
    config.retry_delay = config_json.value("retry_delay", 1000);
    config.delayed_processing = config_json.value("delayed_processing", 0);
    config.window_buffer = config_json.value("window_buffer", 0);
    config.encryption_enabled = config_json.value("encryption_enabled", false);
    config.dlq_enabled = config_json.value("dlq_enabled", false);
    config.dlq_after_max_retries = config_json.value("dlq_after_max_retries", false);
    config.max_size = config_json.value("max_size", 10000);
    config.ttl = config_json.value("ttl", 3600);
    config.priority = config_json.value("priority", 0);
    config.namespace_name = config_json.value("namespace_name", "");
    config.task = config_json.value("task", "");
    config.version = payload.value("version", 0ULL);
    config.retention_seconds = config_json.value("retention_seconds", 0);
    config.completed_retention_seconds = config_json.value("completed_retention_seconds", 0);
    config.retention_enabled = config_json.value("retention_enabled", false);
    config.max_wait_time_seconds = config_json.value("max_wait_time_seconds", 0);
    
    queue_configs_.set(queue, config);
    
    spdlog::debug("SharedState: QUEUE_CONFIG_SET from {} for {}", sender, queue);
}

void SharedStateManager::handle_queue_config_delete(const std::string& sender, const nlohmann::json& payload) {
    std::string queue = payload.value("queue", "");
    if (queue.empty()) return;
    
    queue_configs_.remove(queue);
    
    spdlog::debug("SharedState: QUEUE_CONFIG_DELETE from {} for {}", sender, queue);
}

void SharedStateManager::handle_consumer_registered(const std::string& sender, const nlohmann::json& payload) {
    std::string queue = payload.value("queue", "");
    std::string sid = payload.value("server_id", sender);
    
    if (!queue.empty()) {
        consumer_presence_.register_consumer(queue, sid);
        spdlog::debug("SharedState: CONSUMER_REGISTERED {} for {}", sid, queue);
    }
}

void SharedStateManager::handle_consumer_deregistered(const std::string& sender, const nlohmann::json& payload) {
    std::string queue = payload.value("queue", "");
    std::string sid = payload.value("server_id", sender);
    
    if (!queue.empty()) {
        consumer_presence_.deregister_consumer(queue, sid);
        spdlog::debug("SharedState: CONSUMER_DEREGISTERED {} for {}", sid, queue);
    }
}

// ============================================================
// Background Tasks
// ============================================================

void SharedStateManager::heartbeat_loop() {
    spdlog::info("SharedStateManager: Heartbeat thread started (interval={}ms, backoff_cleanup_threshold={}s)",
                 config_.shared_state.heartbeat_interval_ms, backoff_cleanup_threshold_seconds_);
    
    // Run cleanup every ~60 heartbeats (about once per minute at 1000ms interval)
    constexpr int CLEANUP_INTERVAL_HEARTBEATS = 60;
    int heartbeat_count = 0;
    
    while (running_) {
        heartbeat_count++;
        
        // Send heartbeat
        nlohmann::json payload = {
            {"session_id", transport_->session_id()},
            {"sequence", transport_->messages_sent()},
            {"ts", now_ms()}
        };
        
        transport_->broadcast(UDPSyncMessageType::HEARTBEAT, payload);
        
        // Check for dead servers
        server_health_.check_dead_servers();
        
        // Periodic cleanup of stale backoff entries
        if (heartbeat_count % CLEANUP_INTERVAL_HEARTBEATS == 0) {
            cleanup_stale_backoff_entries();
        }
        
        // Sleep
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.shared_state.heartbeat_interval_ms)
        );
    }
    
    spdlog::info("SharedStateManager: Heartbeat thread stopped");
}

void SharedStateManager::refresh_loop() {
    if (!db_pool_) return;
    
    spdlog::info("SharedStateManager: Refresh thread started (interval={}ms)",
                 config_.shared_state.queue_config_refresh_ms);
    
    // Initial delay to let server start up
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    while (running_) {
        try {
            refresh_queue_configs_from_db();
        } catch (const std::exception& e) {
            spdlog::warn("SharedStateManager: Failed to refresh queue configs: {}", e.what());
        }
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.shared_state.queue_config_refresh_ms)
        );
    }
    
    spdlog::info("SharedStateManager: Refresh thread stopped");
}

void SharedStateManager::dns_refresh_loop() {
    // DNS refresh interval: 30 seconds
    // This ensures that if a pod restarts and gets a new IP, other pods
    // will discover the new IP within 30 seconds
    constexpr int DNS_REFRESH_INTERVAL_SECONDS = 30;
    
    spdlog::info("SharedStateManager: DNS refresh thread started (interval={}s)",
                 DNS_REFRESH_INTERVAL_SECONDS);
    
    while (running_) {
        // Sleep first - initial resolution is done in start()
        std::this_thread::sleep_for(std::chrono::seconds(DNS_REFRESH_INTERVAL_SECONDS));
        
        if (!running_) break;
        
        try {
            int changes = transport_->resolve_peers();
            
            if (changes > 0) {
                spdlog::warn("SharedStateManager: DNS refresh detected {} IP change(s) - "
                            "peer addresses updated", changes);
            }
        } catch (const std::exception& e) {
            spdlog::error("SharedStateManager: DNS refresh failed: {}", e.what());
        }
    }
    
    spdlog::info("SharedStateManager: DNS refresh thread stopped");
}

void SharedStateManager::refresh_queue_configs_from_db() {
    if (!db_pool_) {
        spdlog::debug("SharedStateManager: No DB pool, skipping queue config refresh");
        return;
    }
    
    try {
        auto conn = db_pool_->acquire();
        if (!conn) {
            spdlog::warn("SharedStateManager: Failed to acquire DB connection for queue config refresh");
            return;
        }
        
        // Query all queue configs
        std::string sql = R"(
            SELECT 
                id, name, namespace, task, priority,
                lease_time, retry_limit, retry_delay, 
                max_queue_size, ttl, dead_letter_queue, 
                dlq_after_max_retries, delayed_processing,
                window_buffer, retention_seconds, completed_retention_seconds,
                encryption_enabled
            FROM queen.queues
        )";
        
        sendAndWait(conn.get(), sql.c_str());
        auto result = getTuplesResult(conn.get());
        
        int num_rows = PQntuples(result.get());
        spdlog::debug("SharedStateManager: Loaded {} queue configs from DB", num_rows);
        
        std::vector<caches::CachedQueueConfig> configs;
        
        for (int i = 0; i < num_rows; i++) {
            caches::CachedQueueConfig cfg;
            
            cfg.id = PQgetvalue(result.get(), i, PQfnumber(result.get(), "id"));
            cfg.name = PQgetvalue(result.get(), i, PQfnumber(result.get(), "name"));
            
            const char* ns = PQgetvalue(result.get(), i, PQfnumber(result.get(), "namespace"));
            cfg.namespace_name = (ns && strlen(ns) > 0) ? ns : "";
            
            const char* task = PQgetvalue(result.get(), i, PQfnumber(result.get(), "task"));
            cfg.task = (task && strlen(task) > 0) ? task : "";
            
            const char* priority = PQgetvalue(result.get(), i, PQfnumber(result.get(), "priority"));
            cfg.priority = (priority && strlen(priority) > 0) ? std::stoi(priority) : 0;
            
            const char* lease_time = PQgetvalue(result.get(), i, PQfnumber(result.get(), "lease_time"));
            cfg.lease_time = (lease_time && strlen(lease_time) > 0) ? std::stoi(lease_time) : 300;
            
            const char* retry_limit = PQgetvalue(result.get(), i, PQfnumber(result.get(), "retry_limit"));
            cfg.retry_limit = (retry_limit && strlen(retry_limit) > 0) ? std::stoi(retry_limit) : 3;
            
            const char* retry_delay = PQgetvalue(result.get(), i, PQfnumber(result.get(), "retry_delay"));
            cfg.retry_delay = (retry_delay && strlen(retry_delay) > 0) ? std::stoi(retry_delay) : 1000;
            
            const char* max_size = PQgetvalue(result.get(), i, PQfnumber(result.get(), "max_queue_size"));
            cfg.max_size = (max_size && strlen(max_size) > 0) ? std::stoi(max_size) : 10000;
            
            const char* ttl = PQgetvalue(result.get(), i, PQfnumber(result.get(), "ttl"));
            cfg.ttl = (ttl && strlen(ttl) > 0) ? std::stoi(ttl) : 3600;
            
            const char* dlq = PQgetvalue(result.get(), i, PQfnumber(result.get(), "dead_letter_queue"));
            cfg.dlq_enabled = (dlq && (strcmp(dlq, "t") == 0 || strcmp(dlq, "true") == 0));
            
            const char* dlq_max = PQgetvalue(result.get(), i, PQfnumber(result.get(), "dlq_after_max_retries"));
            cfg.dlq_after_max_retries = (dlq_max && (strcmp(dlq_max, "t") == 0 || strcmp(dlq_max, "true") == 0));
            
            const char* delayed = PQgetvalue(result.get(), i, PQfnumber(result.get(), "delayed_processing"));
            cfg.delayed_processing = (delayed && strlen(delayed) > 0) ? std::stoi(delayed) : 0;
            
            const char* window = PQgetvalue(result.get(), i, PQfnumber(result.get(), "window_buffer"));
            cfg.window_buffer = (window && strlen(window) > 0) ? std::stoi(window) : 0;
            
            const char* retention = PQgetvalue(result.get(), i, PQfnumber(result.get(), "retention_seconds"));
            cfg.retention_seconds = (retention && strlen(retention) > 0) ? std::stoi(retention) : 0;
            
            const char* completed_retention = PQgetvalue(result.get(), i, PQfnumber(result.get(), "completed_retention_seconds"));
            cfg.completed_retention_seconds = (completed_retention && strlen(completed_retention) > 0) ? std::stoi(completed_retention) : 0;
            
            const char* encrypted = PQgetvalue(result.get(), i, PQfnumber(result.get(), "encryption_enabled"));
            cfg.encryption_enabled = (encrypted && (strcmp(encrypted, "t") == 0 || strcmp(encrypted, "true") == 0));
            
            configs.push_back(std::move(cfg));
        }
        
        // Bulk update the cache
        queue_configs_.bulk_update(configs, true);
        
        spdlog::debug("SharedStateManager: Queue config cache refreshed with {} entries", configs.size());
        
    } catch (const std::exception& e) {
        spdlog::warn("SharedStateManager: Failed to refresh queue configs from DB: {}", e.what());
    }
}

// ============================================================
// Callbacks
// ============================================================

void SharedStateManager::on_server_dead(const std::string& server_id) {
    spdlog::warn("SharedStateManager: Server {} detected as dead", server_id);
    
    // Clear consumer presence for dead server
    consumer_presence_.clear_server(server_id);
}

// ============================================================
// Cleanup
// ============================================================

size_t SharedStateManager::cleanup_stale_backoff_entries() {
    std::lock_guard lock(backoff_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    size_t initial_size = group_backoff_.size();
    
    for (auto it = group_backoff_.begin(); it != group_backoff_.end();) {
        // Skip entries that are currently in-flight
        if (it->second.in_flight) {
            ++it;
            continue;
        }
        
        auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.last_accessed
        ).count();
        
        if (age_seconds > backoff_cleanup_threshold_seconds_) {
            it = group_backoff_.erase(it);
        } else {
            ++it;
        }
    }
    
    size_t removed = initial_size - group_backoff_.size();
    
    if (removed > 0) {
        spdlog::info("SharedStateManager: Cleaned up {} stale backoff entries ({} -> {})",
                    removed, initial_size, group_backoff_.size());
    }
    
    return removed;
}

// ============================================================
// Stats
// ============================================================

nlohmann::json SharedStateManager::get_stats() const {
    nlohmann::json stats;
    
    stats["enabled"] = enabled_;
    stats["running"] = running_.load();
    stats["server_id"] = server_id_;
    
    if (transport_) {
        stats["transport"] = transport_->get_stats();
    }
    
    stats["queue_config_cache"] = {
        {"size", queue_configs_.size()},
        {"hits", queue_configs_.hits()},
        {"misses", queue_configs_.misses()},
        {"hit_rate", queue_configs_.hit_rate()}
    };
    
    stats["consumer_presence"] = {
        {"queues_tracked", consumer_presence_.queue_count()},
        {"servers_tracked", consumer_presence_.server_count()},
        {"total_registrations", consumer_presence_.total_registrations()},
        {"registrations_received", consumer_presence_.registrations()},
        {"deregistrations_received", consumer_presence_.deregistrations()}
    };
    
    stats["server_health"] = {
        {"servers_tracked", server_health_.server_count()},
        {"servers_alive", server_health_.get_alive_servers().size()},
        {"servers_dead", server_health_.get_dead_servers().size()},
        {"heartbeats_received", server_health_.heartbeats_received()},
        {"restarts_detected", server_health_.restarts_detected()}
    };
    
    // Tier 4: Local Sidecar Registry
    {
        std::shared_lock lock(sidecar_mutex_);
        stats["local_sidecars"] = local_sidecars_.size();
    }
    
    // Tier 5: Group Backoff State
    {
        std::lock_guard lock(backoff_mutex_);
        int in_flight = 0;
        int backed_off = 0;
        for (const auto& [key, state] : group_backoff_) {
            if (state.in_flight) in_flight++;
            if (state.consecutive_empty >= backoff_threshold_) backed_off++;
        }
        stats["group_backoff"] = {
            {"groups_tracked", group_backoff_.size()},
            {"groups_in_flight", in_flight},
            {"groups_backed_off", backed_off}
        };
    }
    
    // Add aggregated sidecar stats and queue backoff summary
    stats["sidecar_ops"] = get_aggregated_sidecar_stats();
    stats["queue_backoff_summary"] = get_queue_backoff_summary();
    
    return stats;
}

nlohmann::json SharedStateManager::get_aggregated_sidecar_stats() const {
    nlohmann::json result = nlohmann::json::object();
    
    // Aggregated counters
    uint64_t push_count = 0, pop_count = 0, ack_count = 0;
    uint64_t ack_batch_count = 0, transaction_count = 0, renew_lease_count = 0;
    uint64_t push_time_us = 0, pop_time_us = 0, ack_time_us = 0;
    uint64_t push_items = 0, pop_items = 0, ack_items = 0;
    
    {
        std::shared_lock lock(sidecar_mutex_);
        
        for (auto* sidecar : local_sidecars_) {
            if (!sidecar) continue;
            
            auto stats = sidecar->get_stats();
            
            for (const auto& [op_type, op_stats] : stats.op_stats) {
                switch (op_type) {
                    case SidecarOpType::PUSH:
                        push_count += op_stats.count;
                        push_time_us += op_stats.total_time_us;
                        push_items += op_stats.items_processed;
                        break;
                    case SidecarOpType::POP:
                        pop_count += op_stats.count;
                        pop_time_us += op_stats.total_time_us;
                        pop_items += op_stats.items_processed;
                        break;
                    case SidecarOpType::ACK:
                        ack_count += op_stats.count;
                        ack_time_us += op_stats.total_time_us;
                        ack_items += op_stats.items_processed;
                        break;
                    case SidecarOpType::ACK_BATCH:
                        ack_batch_count += op_stats.count;
                        break;
                    case SidecarOpType::TRANSACTION:
                        transaction_count += op_stats.count;
                        break;
                    case SidecarOpType::RENEW_LEASE:
                        renew_lease_count += op_stats.count;
                        break;
                    case SidecarOpType::POP_BATCH:
                    case SidecarOpType::POP_WAIT:
                        // These are tracked under POP
                        pop_count += op_stats.count;
                        pop_time_us += op_stats.total_time_us;
                        pop_items += op_stats.items_processed;
                        break;
                }
            }
        }
    }
    
    result["push"] = {
        {"count", push_count},
        {"avg_latency_us", push_count > 0 ? push_time_us / push_count : 0},
        {"items", push_items}
    };
    result["pop"] = {
        {"count", pop_count},
        {"avg_latency_us", pop_count > 0 ? pop_time_us / pop_count : 0},
        {"items", pop_items}
    };
    result["ack"] = {
        {"count", ack_count},
        {"avg_latency_us", ack_count > 0 ? ack_time_us / ack_count : 0},
        {"items", ack_items}
    };
    result["ack_batch"] = {{"count", ack_batch_count}};
    result["transaction"] = {{"count", transaction_count}};
    result["renew_lease"] = {{"count", renew_lease_count}};
    
    return result;
}

nlohmann::json SharedStateManager::get_queue_backoff_summary() const {
    nlohmann::json result = nlohmann::json::array();
    
    // Aggregate by queue name
    struct QueueSummary {
        int groups_tracked = 0;
        int groups_backed_off = 0;
        int groups_in_flight = 0;
        int64_t total_interval_ms = 0;
        int max_consecutive_empty = 0;
    };
    std::unordered_map<std::string, QueueSummary> by_queue;
    
    {
        std::lock_guard lock(backoff_mutex_);
        
        for (const auto& [group_key, state] : group_backoff_) {
            // group_key format: "queue/partition/consumer_group"
            size_t first_slash = group_key.find('/');
            std::string queue = (first_slash != std::string::npos) 
                ? group_key.substr(0, first_slash) 
                : group_key;
            
            auto& summary = by_queue[queue];
            summary.groups_tracked++;
            if (state.consecutive_empty >= backoff_threshold_) {
                summary.groups_backed_off++;
            }
            if (state.in_flight) {
                summary.groups_in_flight++;
            }
            summary.total_interval_ms += state.current_interval.count();
            summary.max_consecutive_empty = std::max(summary.max_consecutive_empty, 
                                                      state.consecutive_empty);
        }
    }
    
    // Convert to JSON array (sorted by groups_tracked descending)
    std::vector<std::pair<std::string, QueueSummary>> sorted_queues(
        by_queue.begin(), by_queue.end()
    );
    std::sort(sorted_queues.begin(), sorted_queues.end(),
              [](const auto& a, const auto& b) {
                  return a.second.groups_tracked > b.second.groups_tracked;
              });
    
    // Return top 20 queues
    int count = 0;
    for (const auto& [queue, summary] : sorted_queues) {
        if (count++ >= 20) break;
        
        result.push_back({
            {"queue", queue},
            {"groups_tracked", summary.groups_tracked},
            {"groups_backed_off", summary.groups_backed_off},
            {"groups_in_flight", summary.groups_in_flight},
            {"avg_interval_ms", summary.groups_tracked > 0 
                ? summary.total_interval_ms / summary.groups_tracked : 0},
            {"max_consecutive_empty", summary.max_consecutive_empty}
        });
    }
    
    return result;
}

} // namespace queen
