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

