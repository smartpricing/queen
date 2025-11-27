#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <chrono>
#include <atomic>
#include <random>
#include <functional>

namespace queen {
namespace caches {

/**
 * Lease hint entry
 */
struct LeaseHint {
    std::string server_id;      // Server that has the lease
    int64_t expires_at_ms;      // When the lease expires (epoch ms)
    int64_t created_at_ms;      // When this hint was created
};

/**
 * Composite key for lease lookup
 */
struct LeaseKey {
    std::string partition_id;
    std::string consumer_group;
    
    bool operator==(const LeaseKey& other) const {
        return partition_id == other.partition_id && consumer_group == other.consumer_group;
    }
};

struct LeaseKeyHash {
    size_t operator()(const LeaseKey& k) const {
        size_t h1 = std::hash<std::string>{}(k.partition_id);
        size_t h2 = std::hash<std::string>{}(k.consumer_group);
        return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL);
    }
};

/**
 * Lease Hint Cache
 * 
 * Caches hints about which server holds which partition lease.
 * These are ADVISORY ONLY - the database is always authoritative.
 * 
 * Usage:
 * - Before attempting to acquire a lease, check if another server likely has it
 * - If hint says "leased elsewhere", skip DB query and try another partition
 * - If hint says "available" or no hint, MUST still try DB (hint could be wrong!)
 * 
 * Properties:
 * - Dynamic TTL based on queue's lease_time (lease_time / 5)
 * - Probabilistic DB check (1% chance to ignore hint) for self-healing
 * - Invalidated when hinted server is detected as dead
 */
class LeaseHintCache {
public:
    using IsServerAliveFunc = std::function<bool(const std::string&)>;
    
    LeaseHintCache() = default;
    
    /**
     * Set function to check if a server is alive
     * (Used to ignore hints for dead servers)
     */
    void set_server_health_checker(IsServerAliveFunc checker) {
        server_alive_checker_ = std::move(checker);
    }
    
    /**
     * Check if a partition is likely leased elsewhere
     * 
     * @param partition_id The partition UUID
     * @param consumer_group The consumer group
     * @param own_server_id This server's ID (to exclude self)
     * @return true if another server likely has the lease
     */
    bool is_likely_leased_elsewhere(const std::string& partition_id,
                                    const std::string& consumer_group,
                                    const std::string& own_server_id) {
        LeaseKey key{partition_id, consumer_group};
        
        std::shared_lock lock(mutex_);
        
        auto it = hints_.find(key);
        if (it == hints_.end()) {
            return false;  // No hint, try DB
        }
        
        const auto& hint = it->second;
        
        // Check if it's our own hint
        if (hint.server_id == own_server_id) {
            return false;  // We have it, proceed
        }
        
        // Check expiration
        int64_t now = now_ms();
        if (now >= hint.expires_at_ms) {
            // Expired hint - remove and return false
            // (Can't remove under shared lock, but it's okay - just stale)
            hints_wrong_++;
            return false;
        }
        
        // Check if hinted server is dead
        if (server_alive_checker_ && !server_alive_checker_(hint.server_id)) {
            hints_wrong_++;
            return false;  // Dead server, try DB
        }
        
        // Probabilistic DB check (1% chance to ignore hint)
        // This provides self-healing in case hints get stuck
        static thread_local std::mt19937 rng(std::random_device{}());
        if (std::uniform_int_distribution<>(1, 100)(rng) == 1) {
            return false;  // Random check, try DB anyway
        }
        
        hints_used_++;
        return true;  // Likely leased elsewhere, skip this partition
    }
    
    /**
     * Record that a server acquired a lease
     * 
     * @param partition_id The partition UUID
     * @param consumer_group The consumer group
     * @param server_id The server that acquired the lease
     * @param expires_at_ms When the lease expires (epoch ms)
     */
    void hint_acquired(const std::string& partition_id,
                       const std::string& consumer_group,
                       const std::string& server_id,
                       int64_t expires_at_ms) {
        LeaseKey key{partition_id, consumer_group};
        LeaseHint hint{server_id, expires_at_ms, now_ms()};
        
        std::unique_lock lock(mutex_);
        hints_[key] = hint;
    }
    
    /**
     * Record that a lease was released
     * 
     * @param partition_id The partition UUID
     * @param consumer_group The consumer group
     */
    void hint_released(const std::string& partition_id,
                       const std::string& consumer_group) {
        LeaseKey key{partition_id, consumer_group};
        
        std::unique_lock lock(mutex_);
        hints_.erase(key);
    }
    
    /**
     * Clear all hints for a server (when server dies)
     * @param server_id The dead server's ID
     */
    void clear_server_hints(const std::string& server_id) {
        std::unique_lock lock(mutex_);
        
        for (auto it = hints_.begin(); it != hints_.end(); ) {
            if (it->second.server_id == server_id) {
                it = hints_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    /**
     * Cleanup expired hints
     * @return Number of hints removed
     */
    size_t cleanup_expired() {
        std::unique_lock lock(mutex_);
        
        int64_t now = now_ms();
        size_t removed = 0;
        
        for (auto it = hints_.begin(); it != hints_.end(); ) {
            if (it->second.expires_at_ms <= now) {
                it = hints_.erase(it);
                removed++;
            } else {
                ++it;
            }
        }
        
        return removed;
    }
    
    /**
     * Clear all hints
     */
    void clear() {
        std::unique_lock lock(mutex_);
        hints_.clear();
    }
    
    // Stats
    size_t size() const {
        std::shared_lock lock(mutex_);
        return hints_.size();
    }
    
    uint64_t hints_used() const { return hints_used_.load(); }
    uint64_t hints_wrong() const { return hints_wrong_.load(); }
    
    double accuracy() const {
        uint64_t used = hints_used_.load();
        uint64_t wrong = hints_wrong_.load();
        if (used + wrong == 0) return 1.0;
        return static_cast<double>(used) / static_cast<double>(used + wrong);
    }
    
private:
    std::unordered_map<LeaseKey, LeaseHint, LeaseKeyHash> hints_;
    mutable std::shared_mutex mutex_;
    
    IsServerAliveFunc server_alive_checker_;
    
    std::atomic<uint64_t> hints_used_{0};
    std::atomic<uint64_t> hints_wrong_{0};
    
    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
};

} // namespace caches
} // namespace queen

// Hash specialization
namespace std {
template<>
struct hash<queen::caches::LeaseKey> {
    size_t operator()(const queen::caches::LeaseKey& k) const {
        return queen::caches::LeaseKeyHash{}(k);
    }
};
}

