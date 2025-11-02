#include "queen/stream_poll_intention_registry.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace queen {

StreamPollIntentionRegistry::StreamPollIntentionRegistry() {
    spdlog::debug("StreamPollIntentionRegistry initialized");
}

void StreamPollIntentionRegistry::register_intention(const StreamPollIntention& intention) {
    std::lock_guard<std::mutex> lock(mutex_);
    intentions_[intention.request_id] = intention;
    spdlog::debug("Registered stream poll intention: request_id={}, stream={}, group={}", 
                  intention.request_id, intention.stream_name, intention.consumer_group);
}

void StreamPollIntentionRegistry::remove_intention(const std::string& request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = intentions_.find(request_id);
    if (it != intentions_.end()) {
        spdlog::debug("Removed stream poll intention: request_id={}", request_id);
        intentions_.erase(it);
    }
}

std::vector<StreamPollIntention> StreamPollIntentionRegistry::get_active_intentions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<StreamPollIntention> result;
    result.reserve(intentions_.size());
    
    for (const auto& [_, intention] : intentions_) {
        result.push_back(intention);
    }
    
    return result;
}

std::vector<std::string> StreamPollIntentionRegistry::cleanup_expired() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> expired_ids;
    
    for (auto it = intentions_.begin(); it != intentions_.end();) {
        if (it->second.deadline < now) {
            expired_ids.push_back(it->first);
            spdlog::debug("Stream poll intention expired: request_id={}", it->first);
            it = intentions_.erase(it);
        } else {
            ++it;
        }
    }
    
    return expired_ids;
}

bool StreamPollIntentionRegistry::mark_group_in_flight(const std::string& group_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [_, inserted] = in_flight_groups_.insert(group_key);
    return inserted;
}

void StreamPollIntentionRegistry::unmark_group_in_flight(const std::string& group_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    in_flight_groups_.erase(group_key);
}

bool StreamPollIntentionRegistry::is_group_in_flight(const std::string& group_key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return in_flight_groups_.find(group_key) != in_flight_groups_.end();
}

std::map<std::string, std::vector<StreamPollIntention>> 
group_stream_intentions(const std::vector<StreamPollIntention>& intentions) {
    std::map<std::string, std::vector<StreamPollIntention>> grouped;
    
    for (const auto& intention : intentions) {
        grouped[intention.grouping_key()].push_back(intention);
    }
    
    return grouped;
}

} // namespace queen

