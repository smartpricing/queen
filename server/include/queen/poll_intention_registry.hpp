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
#include <memory>

namespace queen {

/**
 * Represents a client's intention to long-poll for messages
 */
struct PollIntention {
    std::string request_id;
    int worker_id;  // Track which worker owns this request
    
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
    
    // Subscription options (for consumer groups)
    std::optional<std::string> subscription_mode;
    std::optional<std::string> subscription_from;
    
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
 * Backoff state for a poll group (queue:partition:consumer_group)
 * Used by poll workers to implement adaptive exponential backoff
 */
struct GroupBackoffState {
    int consecutive_empty_pops = 0;
    int current_interval_ms;
    std::chrono::steady_clock::time_point last_accessed;
    
    GroupBackoffState(int base_interval = 100) 
        : consecutive_empty_pops(0), current_interval_ms(base_interval),
          last_accessed(std::chrono::steady_clock::now()) {}
};

/**
 * Thread-safe registry for tracking long-polling intentions
 * 
 * This registry stores client intentions to long-poll for messages.
 * Poll workers periodically scan the registry, group intentions by
 * queue/partition/consumer_group, and execute batched queries.
 * 
 * Also manages backoff state per group for adaptive polling intervals.
 */
class PollIntentionRegistry {
private:
    std::unordered_map<std::string, PollIntention> intentions_;
    std::unordered_set<std::string> in_flight_groups_;  // Track groups being processed
    std::unordered_map<std::string, GroupBackoffState> backoff_states_;  // Backoff per group
    int base_poll_interval_ms_ = 100;  // Configurable base interval
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
    
    // ============================================================
    // Backoff State Management (for peer notification and adaptive polling)
    // ============================================================
    
    /**
     * Set base poll interval (called during init)
     */
    void set_base_poll_interval(int ms);
    
    /**
     * Get current interval for a group (used by poll workers)
     */
    int get_current_interval(const std::string& group_key);
    
    /**
     * Update backoff state after pop result
     * @param group_key The group key (queue:partition:consumer_group)
     * @param had_messages Whether the pop returned any messages
     * @param backoff_threshold Number of consecutive empty pops before backoff
     * @param backoff_multiplier Exponential backoff multiplier
     * @param max_poll_interval Maximum poll interval after backoff
     */
    void update_backoff_state(const std::string& group_key, bool had_messages,
                              int backoff_threshold, double backoff_multiplier, 
                              int max_poll_interval);
    
    /**
     * Reset backoff for specific group (called by InterInstanceComms)
     * @param group_key The exact group key to reset
     */
    void reset_backoff_for_group(const std::string& group_key);
    
    /**
     * Reset backoff for all groups matching queue/partition pattern
     * Used when we receive a notification but don't know exact consumer group
     * @param queue_name The queue name
     * @param partition_name The partition name
     */
    void reset_backoff_for_queue_partition(const std::string& queue_name,
                                           const std::string& partition_name);
    
    /**
     * Cleanup old backoff states (called periodically by poll workers)
     * @param max_age_seconds Maximum age in seconds before cleanup
     */
    void cleanup_backoff_states(int max_age_seconds);
    
    /**
     * Get all active group keys (for debugging/metrics)
     */
    std::vector<std::string> get_active_group_keys() const;
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

// Global instance (set in acceptor_server.cpp)
extern std::shared_ptr<PollIntentionRegistry> global_poll_intention_registry;

} // namespace queen

