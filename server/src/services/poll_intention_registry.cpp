#include "queen/poll_intention_registry.hpp"
#include <spdlog/spdlog.h>
#include <functional>

namespace queen {

PollIntentionRegistry::PollIntentionRegistry() : running_(true) {
    spdlog::info("PollIntentionRegistry created, running={}", running_.load());
}

void PollIntentionRegistry::register_intention(const PollIntention& intention) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if we've hit the safety limit
    if (intentions_.size() >= 10000) {
        spdlog::error("PollIntentionRegistry at max capacity (10000), rejecting intention {}", 
                     intention.request_id);
        return;
    }
    
    intentions_[intention.request_id] = intention;
    
    spdlog::debug("Registered poll intention {} for key: {}", 
                 intention.request_id, intention.grouping_key());
}

void PollIntentionRegistry::remove_intention(const std::string& request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = intentions_.find(request_id);
    if (it != intentions_.end()) {
        spdlog::debug("Removed poll intention {} (registry now has {} intentions)", 
                    request_id, intentions_.size() - 1);
        intentions_.erase(it);
    } else {
        spdlog::debug("Attempted to remove non-existent intention {}", request_id);
    }
}

std::vector<PollIntention> PollIntentionRegistry::get_active_intentions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PollIntention> result;
    result.reserve(intentions_.size());
    
    for (const auto& [id, intention] : intentions_) {
        result.push_back(intention);
    }
    
    return result;
}

std::vector<std::string> PollIntentionRegistry::cleanup_expired() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> expired;
    
    for (auto it = intentions_.begin(); it != intentions_.end();) {
        if (now >= it->second.deadline) {
            spdlog::debug("Poll intention {} expired", it->first);
            expired.push_back(it->first);
            it = intentions_.erase(it);
        } else {
            ++it;
        }
    }
    
    return expired;
}

bool PollIntentionRegistry::mark_group_in_flight(const std::string& group_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (in_flight_groups_.count(group_key) > 0) {
        return false; // Already in-flight
    }
    
    in_flight_groups_.insert(group_key);
    return true; // Successfully marked
}

void PollIntentionRegistry::unmark_group_in_flight(const std::string& group_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    in_flight_groups_.erase(group_key);
}

bool PollIntentionRegistry::is_group_in_flight(const std::string& group_key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return in_flight_groups_.count(group_key) > 0;
}

// ============================================================
// Backoff State Management
// ============================================================

void PollIntentionRegistry::set_base_poll_interval(int ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    base_poll_interval_ms_ = ms;
    spdlog::info("PollIntentionRegistry: base_poll_interval set to {}ms", ms);
}

int PollIntentionRegistry::get_current_interval(const std::string& group_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = backoff_states_.find(group_key);
    if (it == backoff_states_.end()) {
        // Initialize new state
        backoff_states_.emplace(group_key, GroupBackoffState(base_poll_interval_ms_));
        return base_poll_interval_ms_;
    }
    it->second.last_accessed = std::chrono::steady_clock::now();
    return it->second.current_interval_ms;
}

void PollIntentionRegistry::update_backoff_state(const std::string& group_key, bool had_messages,
                                                  int backoff_threshold, double backoff_multiplier, 
                                                  int max_poll_interval) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = backoff_states_.find(group_key);
    if (it == backoff_states_.end()) {
        backoff_states_.emplace(group_key, GroupBackoffState(base_poll_interval_ms_));
        it = backoff_states_.find(group_key);
    }
    
    auto& state = it->second;
    state.last_accessed = std::chrono::steady_clock::now();
    
    if (had_messages) {
        // Reset backoff on success
        if (state.consecutive_empty_pops > 0) {
            spdlog::debug("Resetting backoff for group '{}' (was: {}ms, {} empty)",
                        group_key, state.current_interval_ms, state.consecutive_empty_pops);
        }
        state.consecutive_empty_pops = 0;
        state.current_interval_ms = base_poll_interval_ms_;
    } else {
        // Increase backoff on empty
        state.consecutive_empty_pops++;
        if (state.consecutive_empty_pops >= backoff_threshold) {
            int old_interval = state.current_interval_ms;
            state.current_interval_ms = std::min(
                static_cast<int>(state.current_interval_ms * backoff_multiplier),
                max_poll_interval
            );
            
            if (state.current_interval_ms > old_interval) {
                spdlog::debug("Backoff increased for group '{}': {}ms -> {}ms (empty count: {})",
                            group_key, old_interval, state.current_interval_ms,
                            state.consecutive_empty_pops);
            }
        }
    }
}

void PollIntentionRegistry::reset_backoff_for_group(const std::string& group_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = backoff_states_.find(group_key);
    if (it != backoff_states_.end()) {
        int old_interval = it->second.current_interval_ms;
        it->second.consecutive_empty_pops = 0;
        it->second.current_interval_ms = base_poll_interval_ms_;
        it->second.last_accessed = std::chrono::steady_clock::now();
        
        if (old_interval > base_poll_interval_ms_) {
            spdlog::info("Poll worker notified: Reset backoff for '{}' ({}ms -> {}ms)", 
                         group_key, old_interval, base_poll_interval_ms_);
        }
    }
}

void PollIntentionRegistry::reset_backoff_for_queue_partition(const std::string& queue_name,
                                                               const std::string& partition_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Build prefixes to match
    std::string exact_prefix = queue_name + ":" + partition_name + ":";
    std::string wildcard_prefix = queue_name + ":*:";
    
    int reset_count = 0;
    for (auto& [key, state] : backoff_states_) {
        // Match exact partition or wildcard partition patterns
        if (key.rfind(exact_prefix, 0) == 0 || key.rfind(wildcard_prefix, 0) == 0) {
            if (state.current_interval_ms > base_poll_interval_ms_) {
                state.consecutive_empty_pops = 0;
                state.current_interval_ms = base_poll_interval_ms_;
                state.last_accessed = std::chrono::steady_clock::now();
                reset_count++;
            }
        }
    }
    
    if (reset_count > 0) {
        spdlog::info("Poll worker notified: Reset backoff for {} group(s) matching {}:{}", 
                     reset_count, queue_name, partition_name);
    }
}

void PollIntentionRegistry::cleanup_backoff_states(int max_age_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    size_t initial_size = backoff_states_.size();
    
    for (auto it = backoff_states_.begin(); it != backoff_states_.end();) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.last_accessed).count();
        if (age > max_age_seconds) {
            it = backoff_states_.erase(it);
        } else {
            ++it;
        }
    }
    
    size_t cleaned = initial_size - backoff_states_.size();
    if (cleaned > 0) {
        spdlog::debug("Cleaned {} stale backoff states (remaining: {})", 
                     cleaned, backoff_states_.size());
    }
}

std::vector<std::string> PollIntentionRegistry::get_active_group_keys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> keys;
    keys.reserve(backoff_states_.size());
    for (const auto& [key, _] : backoff_states_) {
        keys.push_back(key);
    }
    return keys;
}


// Helper function implementations

std::map<std::string, std::vector<PollIntention>> 
group_intentions(const std::vector<PollIntention>& intentions) {
    std::map<std::string, std::vector<PollIntention>> grouped;
    
    for (const auto& intention : intentions) {
        std::string key = intention.grouping_key();
        grouped[key].push_back(intention);
    }
    
    return grouped;
}

std::vector<PollIntention> 
filter_by_worker(const std::vector<PollIntention>& intentions, 
                 int worker_id, int total_workers) {
    std::vector<PollIntention> result;
    
    for (const auto& intention : intentions) {
        // Hash-based distribution for load balancing
        size_t hash = std::hash<std::string>{}(intention.grouping_key());
        if (static_cast<int>(hash % total_workers) == worker_id) {
            result.push_back(intention);
        }
    }
    
    return result;
}

} // namespace queen

