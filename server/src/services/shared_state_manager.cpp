#include "queen/shared_state_manager.hpp"
#include "queen/async_database.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

namespace queen {

// Global instance
std::shared_ptr<SharedStateManager> global_shared_state;

SharedStateManager::SharedStateManager(const InterInstanceConfig& config,
                                       const std::string& server_id,
                                       std::shared_ptr<AsyncDbPool> db_pool)
    : config_(config)
    , server_id_(server_id)
    , db_pool_(db_pool)
    , partition_ids_(config.shared_state.partition_cache_max,
                     config.shared_state.partition_cache_ttl_ms,
                     config.shared_state.cache_shards)
    , server_health_(config.shared_state.dead_threshold_ms)
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
        
        // Set up lease hint server health checker
        lease_hints_.set_server_health_checker([this](const std::string& sid) {
            return server_health_.is_alive(sid);
        });
        
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
    if (!enabled_ || running_) return;
    
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
    
    cleanup_thread_ = std::thread(&SharedStateManager::cleanup_loop, this);
    
    spdlog::info("SharedStateManager: Started");
}

void SharedStateManager::stop() {
    if (!running_) return;
    
    spdlog::info("SharedStateManager: Stopping...");
    running_ = false;
    
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (refresh_thread_.joinable()) refresh_thread_.join();
    if (cleanup_thread_.joinable()) cleanup_thread_.join();
    
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
    
    // Also invalidate all partitions for this queue
    partition_ids_.invalidate_queue(queue);
    
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
// Partition ID (Tier 3)
// ============================================================

std::optional<std::string> SharedStateManager::get_partition_id(const std::string& queue,
                                                                 const std::string& partition) {
    return partition_ids_.get(queue, partition);
}

void SharedStateManager::cache_partition_id(const std::string& queue,
                                           const std::string& partition,
                                           const std::string& partition_id,
                                           const std::string& queue_id) {
    partition_ids_.put(queue, partition, partition_id, queue_id);
}

void SharedStateManager::invalidate_partition(const std::string& queue,
                                              const std::string& partition,
                                              bool broadcast) {
    partition_ids_.invalidate(queue, partition);
    
    if (broadcast && running_ && transport_) {
        nlohmann::json payload = {
            {"queue", queue},
            {"partition", partition}
        };
        transport_->broadcast(UDPSyncMessageType::PARTITION_DELETED, payload);
    }
}

// ============================================================
// Lease Hints (Tier 4)
// ============================================================

bool SharedStateManager::is_likely_leased_elsewhere(const std::string& partition_id,
                                                    const std::string& consumer_group) {
    return lease_hints_.is_likely_leased_elsewhere(partition_id, consumer_group, server_id_);
}

void SharedStateManager::hint_lease_acquired(const std::string& partition_id,
                                            const std::string& consumer_group,
                                            int lease_time_seconds) {
    int64_t expires_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() + (lease_time_seconds * 1000);
    
    lease_hints_.hint_acquired(partition_id, consumer_group, server_id_, expires_at_ms);
    
    if (running_ && transport_) {
        nlohmann::json payload = {
            {"partition_id", partition_id},
            {"consumer_group", consumer_group},
            {"server_id", server_id_},
            {"expires_at_ms", expires_at_ms}
        };
        transport_->broadcast(UDPSyncMessageType::LEASE_HINT_ACQUIRED, payload);
    }
}

void SharedStateManager::hint_lease_released(const std::string& partition_id,
                                            const std::string& consumer_group) {
    lease_hints_.hint_released(partition_id, consumer_group);
    
    if (running_ && transport_) {
        nlohmann::json payload = {
            {"partition_id", partition_id},
            {"consumer_group", consumer_group}
        };
        transport_->broadcast(UDPSyncMessageType::LEASE_HINT_RELEASED, payload);
    }
}

// ============================================================
// Server Health (Tier 5)
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
    if (!running_ || !transport_) return;
    
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

void SharedStateManager::notify_partition_free(const std::string& queue,
                                              const std::string& partition,
                                              const std::string& consumer_group) {
    if (!running_ || !transport_) return;
    
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
    
    transport_->on_message(Type::PARTITION_DELETED, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_partition_deleted(sender, p);
    });
    
    transport_->on_message(Type::LEASE_HINT_ACQUIRED, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_lease_hint_acquired(sender, p);
    });
    
    transport_->on_message(Type::LEASE_HINT_RELEASED, [this](Type, const std::string& sender, const nlohmann::json& p) {
        handle_lease_hint_released(sender, p);
    });
}

void SharedStateManager::handle_message_available(const std::string& sender, const nlohmann::json& payload) {
    // This will be forwarded to PollIntentionRegistry by InterInstanceComms
    // For now, just log
    spdlog::debug("SharedState: MESSAGE_AVAILABLE from {} for {}:{}",
                 sender, payload.value("queue", ""), payload.value("partition", ""));
}

void SharedStateManager::handle_partition_free(const std::string& sender, const nlohmann::json& payload) {
    spdlog::debug("SharedState: PARTITION_FREE from {} for {}:{}:{}",
                 sender, payload.value("queue", ""), payload.value("partition", ""),
                 payload.value("consumer_group", ""));
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
    partition_ids_.invalidate_queue(queue);
    
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

void SharedStateManager::handle_partition_deleted(const std::string& sender, const nlohmann::json& payload) {
    std::string queue = payload.value("queue", "");
    std::string partition = payload.value("partition", "");
    
    if (!queue.empty() && !partition.empty()) {
        partition_ids_.invalidate(queue, partition);
        spdlog::debug("SharedState: PARTITION_DELETED from {} for {}:{}", sender, queue, partition);
    }
}

void SharedStateManager::handle_lease_hint_acquired(const std::string& sender, const nlohmann::json& payload) {
    std::string partition_id = payload.value("partition_id", "");
    std::string consumer_group = payload.value("consumer_group", "");
    std::string sid = payload.value("server_id", sender);
    int64_t expires_at_ms = payload.value("expires_at_ms", 0LL);
    
    if (!partition_id.empty() && !consumer_group.empty()) {
        lease_hints_.hint_acquired(partition_id, consumer_group, sid, expires_at_ms);
        spdlog::trace("SharedState: LEASE_HINT_ACQUIRED {} by {} (expires {})",
                     partition_id, sid, expires_at_ms);
    }
}

void SharedStateManager::handle_lease_hint_released(const std::string& sender, const nlohmann::json& payload) {
    std::string partition_id = payload.value("partition_id", "");
    std::string consumer_group = payload.value("consumer_group", "");
    
    if (!partition_id.empty() && !consumer_group.empty()) {
        lease_hints_.hint_released(partition_id, consumer_group);
        spdlog::trace("SharedState: LEASE_HINT_RELEASED {} by {}", partition_id, sender);
    }
}

// ============================================================
// Background Tasks
// ============================================================

void SharedStateManager::heartbeat_loop() {
    spdlog::info("SharedStateManager: Heartbeat thread started (interval={}ms)",
                 config_.shared_state.heartbeat_interval_ms);
    
    while (running_) {
        // Send heartbeat
        nlohmann::json payload = {
            {"session_id", transport_->session_id()},
            {"sequence", transport_->messages_sent()},
            {"ts", now_ms()}
        };
        
        transport_->broadcast(UDPSyncMessageType::HEARTBEAT, payload);
        
        // Check for dead servers
        server_health_.check_dead_servers();
        
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

void SharedStateManager::cleanup_loop() {
    spdlog::info("SharedStateManager: Cleanup thread started");
    
    while (running_) {
        // Cleanup expired partition cache entries
        size_t partition_removed = partition_ids_.cleanup_expired();
        
        // Cleanup expired lease hints
        size_t lease_removed = lease_hints_.cleanup_expired();
        
        if (partition_removed > 0 || lease_removed > 0) {
            spdlog::debug("SharedStateManager: Cleanup removed {} partitions, {} lease hints",
                         partition_removed, lease_removed);
        }
        
        // Run every 30 seconds
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    
    spdlog::info("SharedStateManager: Cleanup thread stopped");
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
    
    // Clear lease hints for dead server
    lease_hints_.clear_server_hints(server_id);
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
    
    stats["partition_id_cache"] = {
        {"size", partition_ids_.size()},
        {"max_size", partition_ids_.max_size()},
        {"hits", partition_ids_.hits()},
        {"misses", partition_ids_.misses()},
        {"evictions", partition_ids_.evictions()},
        {"hit_rate", partition_ids_.hit_rate()}
    };
    
    stats["lease_hints"] = {
        {"size", lease_hints_.size()},
        {"hints_used", lease_hints_.hints_used()},
        {"hints_wrong", lease_hints_.hints_wrong()},
        {"accuracy", lease_hints_.accuracy()}
    };
    
    stats["server_health"] = {
        {"servers_tracked", server_health_.server_count()},
        {"servers_alive", server_health_.get_alive_servers().size()},
        {"servers_dead", server_health_.get_dead_servers().size()},
        {"heartbeats_received", server_health_.heartbeats_received()},
        {"restarts_detected", server_health_.restarts_detected()}
    };
    
    return stats;
}

} // namespace queen

