#pragma once

#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>

namespace queen {

/**
 * Represents a client's intention to long-poll for messages
 */
struct PollIntention {
    std::string request_id;
    
    // Queue-based polling
    std::optional<std::string> queue_name;
    std::optional<std::string> partition_name;
    
    // Namespace/task-based polling
    std::optional<std::string> namespace_name;
    std::optional<std::string> task_name;
    
    std::string consumer_group;
    int batch_size;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point created_at;
    
    /**
     * Create a grouping key for batching intentions together
     * Intentions with the same key can be served by a single DB query
     */
    std::string grouping_key() const {
        if (queue_name.has_value()) {
            return queue_name.value() + ":" + 
                   partition_name.value_or("*") + ":" + 
                   consumer_group;
        } else {
            return namespace_name.value_or("*") + ":" + 
                   task_name.value_or("*") + ":" + 
                   consumer_group;
        }
    }
    
    /**
     * Check if this intention is for queue-based polling
     */
    bool is_queue_based() const {
        return queue_name.has_value();
    }
    
    /**
     * Check if this intention is for namespace/task-based polling
     */
    bool is_namespace_based() const {
        return namespace_name.has_value() || task_name.has_value();
    }
};

/**
 * Thread-safe registry for tracking long-polling intentions
 * 
 * This registry stores client intentions to long-poll for messages.
 * Poll workers periodically scan the registry, group intentions by
 * queue/partition/consumer_group, and execute batched queries.
 */
class PollIntentionRegistry {
private:
    std::unordered_map<std::string, PollIntention> intentions_;
    std::unordered_set<std::string> in_flight_groups_;  // Track groups being processed
    mutable std::mutex mutex_;
    std::atomic<bool> running_{true};
    
public:
    PollIntentionRegistry();
    ~PollIntentionRegistry() = default;
    
    // Disable copy and move
    PollIntentionRegistry(const PollIntentionRegistry&) = delete;
    PollIntentionRegistry& operator=(const PollIntentionRegistry&) = delete;
    
    /**
     * Register a new polling intention
     */
    void register_intention(const PollIntention& intention);
    
    /**
     * Remove an intention (called when fulfilled or timed out)
     */
    void remove_intention(const std::string& request_id);
    
    /**
     * Get all active intentions (thread-safe copy)
     */
    std::vector<PollIntention> get_active_intentions() const;
    
    /**
     * Get count of active intentions
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return intentions_.size();
    }
    
    /**
     * Check if registry is empty
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return intentions_.empty();
    }
    
    /**
     * Signal shutdown to poll workers
     */
    void shutdown() {
        running_ = false;
    }
    
    /**
     * Check if registry is still running
     */
    bool is_running() const {
        return running_;
    }
    
    /**
     * Cleanup expired intentions and return their request IDs
     */
    std::vector<std::string> cleanup_expired();
    
    /**
     * Try to mark a group as in-flight for processing
     * Returns true if successfully marked, false if already in-flight
     */
    bool mark_group_in_flight(const std::string& group_key);
    
    /**
     * Remove a group from in-flight tracking
     */
    void unmark_group_in_flight(const std::string& group_key);
    
    /**
     * Check if a group is currently in-flight
     */
    bool is_group_in_flight(const std::string& group_key) const;
};

/**
 * Helper: Group intentions by their grouping key
 * Returns a map where the key is the grouping key and value is the list of intentions
 */
std::map<std::string, std::vector<PollIntention>> 
group_intentions(const std::vector<PollIntention>& intentions);

/**
 * Helper: Filter intentions for a specific worker (load balancing)
 * Uses hash-based distribution to split work evenly across workers
 */
std::vector<PollIntention> 
filter_by_worker(const std::vector<PollIntention>& intentions, 
                 int worker_id, int total_workers);

} // namespace queen

