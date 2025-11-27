#pragma once

#include "queen/caches/lru_cache.hpp"
#include <string>
#include <optional>
#include <functional>

namespace queen {
namespace caches {

/**
 * Composite key for partition lookup
 * Avoids string allocation on hot path
 */
struct PartitionKey {
    std::string queue;
    std::string partition;
    
    bool operator==(const PartitionKey& other) const {
        return queue == other.queue && partition == other.partition;
    }
};

/**
 * Hash function for PartitionKey
 */
struct PartitionKeyHash {
    size_t operator()(const PartitionKey& k) const {
        size_t h1 = std::hash<std::string>{}(k.queue);
        size_t h2 = std::hash<std::string>{}(k.partition);
        // Combine hashes using FNV-like mixing
        return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL);
    }
};

/**
 * Cached partition info
 */
struct CachedPartition {
    std::string partition_id;  // UUID from database
    std::string queue_id;      // Queue UUID (for FK lookups)
};

} // namespace caches
} // namespace queen

// Hash specialization BEFORE any usage
namespace std {
template<>
struct hash<queen::caches::PartitionKey> {
    size_t operator()(const queen::caches::PartitionKey& k) const {
        return queen::caches::PartitionKeyHash{}(k);
    }
};
}

namespace queen {
namespace caches {

/**
 * Partition ID Cache
 * 
 * Caches the mapping: (queue_name, partition_name) → partition_id
 * 
 * Properties:
 * - LRU eviction when size limit is reached
 * - TTL-based expiration (default: 5 minutes)
 * - Sharded for reduced lock contention
 * - Local only (not synced between servers)
 * - Invalidated on PARTITION_DELETED message
 */
class PartitionIdCache {
public:
    /**
     * Create partition ID cache
     * @param max_size Maximum number of entries
     * @param ttl_ms TTL in milliseconds
     * @param num_shards Number of shards for lock distribution
     */
    PartitionIdCache(size_t max_size = 10000, int ttl_ms = 300000, size_t num_shards = 16)
        : max_size_(max_size), num_shards_(num_shards) {
        size_t per_shard = (max_size + num_shards - 1) / num_shards;
        for (size_t i = 0; i < num_shards; i++) {
            shards_.push_back(std::make_unique<LRUCache<PartitionKey, CachedPartition>>(per_shard, ttl_ms));
        }
    }
    
    /**
     * Get partition ID from cache
     * @param queue Queue name
     * @param partition Partition name
     * @return Partition ID if found and not expired
     */
    std::optional<std::string> get(const std::string& queue, const std::string& partition) {
        PartitionKey key{queue, partition};
        auto result = shard_for(key).get(key);
        if (result) {
            return result->partition_id;
        }
        return std::nullopt;
    }
    
    /**
     * Get full cached partition info
     */
    std::optional<CachedPartition> get_full(const std::string& queue, const std::string& partition) {
        PartitionKey key{queue, partition};
        return shard_for(key).get(key);
    }
    
    /**
     * Cache a partition ID
     * @param queue Queue name
     * @param partition Partition name
     * @param partition_id The partition UUID
     * @param queue_id Optional queue UUID
     */
    void put(const std::string& queue, const std::string& partition, 
             const std::string& partition_id, const std::string& queue_id = "") {
        PartitionKey key{queue, partition};
        CachedPartition value{partition_id, queue_id};
        shard_for(key).put(key, value);
    }
    
    /**
     * Invalidate a partition from cache
     * @param queue Queue name
     * @param partition Partition name
     * @return true if removed
     */
    bool invalidate(const std::string& queue, const std::string& partition) {
        PartitionKey key{queue, partition};
        return shard_for(key).remove(key);
    }
    
    /**
     * Invalidate all partitions for a queue
     * @param queue Queue name
     * @return Number of entries removed
     */
    size_t invalidate_queue(const std::string& queue) {
        size_t removed = 0;
        for (auto& shard : shards_) {
            removed += shard->remove_if([&queue](const PartitionKey& k) {
                return k.queue == queue;
            });
        }
        return removed;
    }
    
    /**
     * Clear all entries
     */
    void clear() {
        for (auto& shard : shards_) {
            shard->clear();
        }
    }
    
    /**
     * Cleanup expired entries
     * @return Number of entries removed
     */
    size_t cleanup_expired() {
        size_t removed = 0;
        for (auto& shard : shards_) {
            removed += shard->cleanup_expired();
        }
        return removed;
    }
    
    // Stats
    size_t size() const {
        size_t total = 0;
        for (const auto& shard : shards_) {
            total += shard->size();
        }
        return total;
    }
    
    size_t max_size() const { return max_size_; }
    
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
    
private:
    size_t max_size_;
    size_t num_shards_;
    std::vector<std::unique_ptr<LRUCache<PartitionKey, CachedPartition>>> shards_;
    
    LRUCache<PartitionKey, CachedPartition>& shard_for(const PartitionKey& key) {
        size_t idx = PartitionKeyHash{}(key) % num_shards_;
        return *shards_[idx];
    }
};

} // namespace caches
} // namespace queen
