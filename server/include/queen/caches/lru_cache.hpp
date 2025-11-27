#pragma once

#include <list>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <chrono>
#include <atomic>
#include <functional>

namespace queen {
namespace caches {

/**
 * Thread-safe LRU cache with TTL support
 * 
 * Features:
 * - LRU eviction when size limit is reached
 * - TTL-based expiration for entries
 * - Thread-safe with shared_mutex (multiple readers, single writer)
 * - Stats tracking (hits, misses, evictions)
 * 
 * @tparam K Key type (must be hashable)
 * @tparam V Value type
 */
template<typename K, typename V>
class LRUCache {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    
    struct CacheEntry {
        K key;
        V value;
        TimePoint inserted_at;
        TimePoint expires_at;
    };
    
    using ListIterator = typename std::list<CacheEntry>::iterator;
    
    /**
     * Create an LRU cache
     * @param max_size Maximum number of entries
     * @param ttl_ms TTL in milliseconds (0 = no TTL)
     */
    explicit LRUCache(size_t max_size, int ttl_ms = 0)
        : max_size_(max_size), ttl_ms_(ttl_ms) {}
    
    /**
     * Get a value from the cache
     * @param key The key to look up
     * @return The value if found and not expired, nullopt otherwise
     */
    std::optional<V> get(const K& key) {
        std::unique_lock lock(mutex_);
        
        auto it = lookup_.find(key);
        if (it == lookup_.end()) {
            misses_++;
            return std::nullopt;
        }
        
        // Check TTL
        if (ttl_ms_ > 0 && Clock::now() >= it->second->expires_at) {
            // Entry expired - remove it
            items_.erase(it->second);
            lookup_.erase(it);
            misses_++;
            return std::nullopt;
        }
        
        // Move to front (most recently used)
        items_.splice(items_.begin(), items_, it->second);
        hits_++;
        
        return it->second->value;
    }
    
    /**
     * Put a value into the cache
     * @param key The key
     * @param value The value
     */
    void put(const K& key, const V& value) {
        std::unique_lock lock(mutex_);
        
        auto now = Clock::now();
        TimePoint expires_at = ttl_ms_ > 0 
            ? now + std::chrono::milliseconds(ttl_ms_)
            : TimePoint::max();
        
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            // Update existing entry
            it->second->value = value;
            it->second->inserted_at = now;
            it->second->expires_at = expires_at;
            // Move to front
            items_.splice(items_.begin(), items_, it->second);
            return;
        }
        
        // Evict if at capacity
        while (items_.size() >= max_size_ && !items_.empty()) {
            auto& back = items_.back();
            lookup_.erase(back.key);
            items_.pop_back();
            evictions_++;
        }
        
        // Insert new entry at front
        items_.push_front(CacheEntry{key, value, now, expires_at});
        lookup_[key] = items_.begin();
    }
    
    /**
     * Remove a value from the cache
     * @param key The key to remove
     * @return true if removed, false if not found
     */
    bool remove(const K& key) {
        std::unique_lock lock(mutex_);
        
        auto it = lookup_.find(key);
        if (it == lookup_.end()) {
            return false;
        }
        
        items_.erase(it->second);
        lookup_.erase(it);
        return true;
    }
    
    /**
     * Remove all entries with keys matching a predicate
     * @param predicate Function that returns true for keys to remove
     * @return Number of entries removed
     */
    template<typename Pred>
    size_t remove_if(Pred predicate) {
        std::unique_lock lock(mutex_);
        
        size_t removed = 0;
        for (auto it = items_.begin(); it != items_.end(); ) {
            if (predicate(it->key)) {
                lookup_.erase(it->key);
                it = items_.erase(it);
                removed++;
            } else {
                ++it;
            }
        }
        return removed;
    }
    
    /**
     * Clear all entries
     */
    void clear() {
        std::unique_lock lock(mutex_);
        items_.clear();
        lookup_.clear();
    }
    
    /**
     * Get current size
     */
    size_t size() const {
        std::shared_lock lock(mutex_);
        return items_.size();
    }
    
    /**
     * Get maximum size
     */
    size_t max_size() const { return max_size_; }
    
    /**
     * Get hit count
     */
    uint64_t hits() const { return hits_.load(); }
    
    /**
     * Get miss count
     */
    uint64_t misses() const { return misses_.load(); }
    
    /**
     * Get eviction count
     */
    uint64_t evictions() const { return evictions_.load(); }
    
    /**
     * Get hit rate (0.0 to 1.0)
     */
    double hit_rate() const {
        uint64_t h = hits_.load();
        uint64_t m = misses_.load();
        if (h + m == 0) return 0.0;
        return static_cast<double>(h) / static_cast<double>(h + m);
    }
    
    /**
     * Cleanup expired entries
     * @return Number of entries removed
     */
    size_t cleanup_expired() {
        if (ttl_ms_ <= 0) return 0;
        
        std::unique_lock lock(mutex_);
        auto now = Clock::now();
        size_t removed = 0;
        
        // Check from back (oldest entries)
        while (!items_.empty() && items_.back().expires_at <= now) {
            lookup_.erase(items_.back().key);
            items_.pop_back();
            removed++;
        }
        
        return removed;
    }
    
    /**
     * Get all keys (for debugging/stats)
     */
    std::vector<K> keys() const {
        std::shared_lock lock(mutex_);
        std::vector<K> result;
        result.reserve(items_.size());
        for (const auto& item : items_) {
            result.push_back(item.key);
        }
        return result;
    }
    
private:
    size_t max_size_;
    int ttl_ms_;
    
    std::list<CacheEntry> items_;
    std::unordered_map<K, ListIterator> lookup_;
    mutable std::shared_mutex mutex_;
    
    std::atomic<uint64_t> hits_{0};
    std::atomic<uint64_t> misses_{0};
    std::atomic<uint64_t> evictions_{0};
};

/**
 * Sharded LRU cache for reduced lock contention
 * 
 * Distributes entries across multiple shards based on key hash.
 * Each shard has its own lock, reducing contention under high load.
 */
template<typename K, typename V, size_t NumShards = 16>
class ShardedLRUCache {
public:
    /**
     * Create a sharded LRU cache
     * @param total_max_size Total max entries (distributed across shards)
     * @param ttl_ms TTL in milliseconds (0 = no TTL)
     */
    explicit ShardedLRUCache(size_t total_max_size, int ttl_ms = 0) {
        size_t per_shard = (total_max_size + NumShards - 1) / NumShards;
        for (size_t i = 0; i < NumShards; i++) {
            shards_[i] = std::make_unique<LRUCache<K, V>>(per_shard, ttl_ms);
        }
    }
    
    std::optional<V> get(const K& key) {
        return shard_for(key).get(key);
    }
    
    void put(const K& key, const V& value) {
        shard_for(key).put(key, value);
    }
    
    bool remove(const K& key) {
        return shard_for(key).remove(key);
    }
    
    void clear() {
        for (auto& shard : shards_) {
            shard->clear();
        }
    }
    
    size_t size() const {
        size_t total = 0;
        for (const auto& shard : shards_) {
            total += shard->size();
        }
        return total;
    }
    
    uint64_t hits() const {
        uint64_t total = 0;
        for (const auto& shard : shards_) {
            total += shard->hits();
        }
        return total;
    }
    
    uint64_t misses() const {
        uint64_t total = 0;
        for (const auto& shard : shards_) {
            total += shard->misses();
        }
        return total;
    }
    
    uint64_t evictions() const {
        uint64_t total = 0;
        for (const auto& shard : shards_) {
            total += shard->evictions();
        }
        return total;
    }
    
    double hit_rate() const {
        uint64_t h = hits();
        uint64_t m = misses();
        if (h + m == 0) return 0.0;
        return static_cast<double>(h) / static_cast<double>(h + m);
    }
    
    size_t cleanup_expired() {
        size_t total = 0;
        for (auto& shard : shards_) {
            total += shard->cleanup_expired();
        }
        return total;
    }
    
private:
    std::array<std::unique_ptr<LRUCache<K, V>>, NumShards> shards_;
    
    LRUCache<K, V>& shard_for(const K& key) {
        size_t idx = std::hash<K>{}(key) % NumShards;
        return *shards_[idx];
    }
    
    const LRUCache<K, V>& shard_for(const K& key) const {
        size_t idx = std::hash<K>{}(key) % NumShards;
        return *shards_[idx];
    }
};

} // namespace caches
} // namespace queen


