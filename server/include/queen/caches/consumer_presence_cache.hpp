#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <shared_mutex>
#include <atomic>
#include <vector>

namespace queen {
namespace caches {

/**
 * Consumer Presence Cache
 * 
 * Tracks which servers have consumers for which queues.
 * Used for targeted notification delivery instead of broadcasting to all servers.
 * 
 * Properties:
 * - Queue → Set<ServerID> mapping
 * - Thread-safe with shared_mutex
 * - Fallback: If no presence info, return empty (caller should broadcast to all)
 * 
 * Data flow:
 * - Local: When PollIntentionRegistry gets first intention for a queue, register
 * - Local: When PollIntentionRegistry loses all intentions for a queue, deregister
 * - Remote: Receive CONSUMER_REGISTERED/DEREGISTERED from peers
 */
class ConsumerPresenceCache {
public:
    ConsumerPresenceCache() = default;
    
    /**
     * Get all servers that have consumers for a queue
     * @param queue_name The queue name
     * @return Set of server IDs (empty if no info = fallback to broadcast)
     */
    std::set<std::string> get_servers_for_queue(const std::string& queue_name) const {
        std::shared_lock lock(mutex_);
        
        auto it = presence_.find(queue_name);
        if (it != presence_.end()) {
            return it->second;
        }
        return {};
    }
    
    /**
     * Check if any server has consumers for a queue
     * @param queue_name The queue name
     * @return true if at least one server is known to have consumers
     */
    bool has_consumers(const std::string& queue_name) const {
        std::shared_lock lock(mutex_);
        auto it = presence_.find(queue_name);
        return it != presence_.end() && !it->second.empty();
    }
    
    /**
     * Register a server as having consumers for a queue
     * @param queue_name The queue name
     * @param server_id The server ID
     */
    void register_consumer(const std::string& queue_name, const std::string& server_id) {
        std::unique_lock lock(mutex_);
        presence_[queue_name].insert(server_id);
        registrations_++;
    }
    
    /**
     * Deregister a server as having consumers for a queue
     * @param queue_name The queue name
     * @param server_id The server ID
     */
    void deregister_consumer(const std::string& queue_name, const std::string& server_id) {
        std::unique_lock lock(mutex_);
        
        auto it = presence_.find(queue_name);
        if (it != presence_.end()) {
            it->second.erase(server_id);
            // Clean up empty sets
            if (it->second.empty()) {
                presence_.erase(it);
            }
        }
        deregistrations_++;
    }
    
    /**
     * Clear all presence info for a server (called when server dies)
     * @param server_id The server ID
     */
    void clear_server(const std::string& server_id) {
        std::unique_lock lock(mutex_);
        
        for (auto it = presence_.begin(); it != presence_.end(); ) {
            it->second.erase(server_id);
            if (it->second.empty()) {
                it = presence_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    /**
     * Get all queues a server has presence for
     * @param server_id The server ID
     * @return Vector of queue names
     */
    std::vector<std::string> get_queues_for_server(const std::string& server_id) const {
        std::shared_lock lock(mutex_);
        
        std::vector<std::string> result;
        for (const auto& [queue, servers] : presence_) {
            if (servers.count(server_id) > 0) {
                result.push_back(queue);
            }
        }
        return result;
    }
    
    /**
     * Get count of queues tracked
     */
    size_t queue_count() const {
        std::shared_lock lock(mutex_);
        return presence_.size();
    }
    
    /**
     * Get total number of server registrations (queue × server pairs)
     */
    size_t total_registrations() const {
        std::shared_lock lock(mutex_);
        size_t total = 0;
        for (const auto& [_, servers] : presence_) {
            total += servers.size();
        }
        return total;
    }
    
    /**
     * Get unique server count
     */
    size_t server_count() const {
        std::shared_lock lock(mutex_);
        std::unordered_set<std::string> unique;
        for (const auto& [_, servers] : presence_) {
            unique.insert(servers.begin(), servers.end());
        }
        return unique.size();
    }
    
    /**
     * Clear all entries
     */
    void clear() {
        std::unique_lock lock(mutex_);
        presence_.clear();
    }
    
    // Stats
    uint64_t registrations() const { return registrations_.load(); }
    uint64_t deregistrations() const { return deregistrations_.load(); }
    
private:
    // queue_name → set<server_id>
    std::unordered_map<std::string, std::set<std::string>> presence_;
    mutable std::shared_mutex mutex_;
    
    std::atomic<uint64_t> registrations_{0};
    std::atomic<uint64_t> deregistrations_{0};
};

} // namespace caches
} // namespace queen


