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
 * Represents a client's intention to long-poll for streaming windows
 */
struct StreamPollIntention {
    std::string request_id;
    int worker_id;  // Track which worker owns this request
    std::string stream_name;
    std::string consumer_group;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point created_at;
    
    /**
     * Create a grouping key for batching intentions together
     * Intentions with the same key can be served by a single DB query
     */
    std::string grouping_key() const {
        return stream_name + ":" + consumer_group;
    }
};

/**
 * Thread-safe registry for tracking streaming long-polling intentions
 * 
 * This registry stores client intentions to long-poll for streaming windows.
 * Poll workers periodically scan the registry, group intentions by
 * stream_name/consumer_group, and execute batched queries.
 */
class StreamPollIntentionRegistry {
private:
    std::unordered_map<std::string, StreamPollIntention> intentions_;
    std::unordered_set<std::string> in_flight_groups_;  // Track groups being processed
    mutable std::mutex mutex_;
    std::atomic<bool> running_{true};
    
public:
    StreamPollIntentionRegistry();
    ~StreamPollIntentionRegistry() = default;
    
    // Disable copy and move
    StreamPollIntentionRegistry(const StreamPollIntentionRegistry&) = delete;
    StreamPollIntentionRegistry& operator=(const StreamPollIntentionRegistry&) = delete;
    
    /**
     * Register a new polling intention
     */
    void register_intention(const StreamPollIntention& intention);
    
    /**
     * Remove an intention (called when fulfilled or timed out)
     */
    void remove_intention(const std::string& request_id);
    
    /**
     * Get all active intentions (thread-safe copy)
     */
    std::vector<StreamPollIntention> get_active_intentions() const;
    
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
std::map<std::string, std::vector<StreamPollIntention>> 
group_stream_intentions(const std::vector<StreamPollIntention>& intentions);

} // namespace queen

