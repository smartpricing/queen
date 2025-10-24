#pragma once

#include <queue>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <json.hpp>
// Forward declaration to avoid including full uWebSockets headers
namespace uWS {
    template <bool SSL>
    struct HttpResponse;
}
#include <spdlog/spdlog.h>

namespace queen {

/**
 * Thread-safe response queue for passing results from DB threadpool to uWebSockets event loop
 */
class ResponseQueue {
public:
    struct ResponseItem {
        std::string request_id;
        nlohmann::json data;
        bool is_error;
        int status_code;
        std::chrono::steady_clock::time_point timestamp;
        
        ResponseItem() = default;
        ResponseItem(const std::string& id, const nlohmann::json& d, bool err = false, int code = 200)
            : request_id(id), data(d), is_error(err), status_code(code), 
              timestamp(std::chrono::steady_clock::now()) {}
    };

private:
    std::queue<ResponseItem> queue_;
    mutable std::mutex mutex_;
    
public:
    void push(const std::string& request_id, const nlohmann::json& data, 
              bool is_error = false, int status_code = 200) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check response size before queuing (optional size check)
        try {
            // Quick size estimation without full serialization
            std::string sample = data.dump(0, ' ', false, nlohmann::json::error_handler_t::ignore);
            if (sample.length() > 100 * 1024 * 1024) { // 100MB threshold for queue rejection
                spdlog::error("Response too large to queue: ~{} bytes for request {}", sample.length(), request_id);
                // Don't queue extremely large responses
                return;
            } else if (sample.length() > 5 * 1024 * 1024) { // 5MB warning threshold
                spdlog::warn("Large response queued: ~{} bytes for request {}", sample.length(), request_id);
            }
        } catch (const std::exception& e) {
            spdlog::debug("Could not estimate response size for {}: {}", request_id, e.what());
            // Continue anyway - the actual serialization will handle errors
        }
        
        queue_.emplace(request_id, data, is_error, status_code);
    }
    
    bool pop(ResponseItem& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
};

/**
 * Registry for tracking HTTP response objects safely
 */
class ResponseRegistry {
public:
    struct ResponseEntry {
        uWS::HttpResponse<false>* response;
        std::chrono::steady_clock::time_point created_at;
        std::atomic<bool> valid{true};
        std::mutex mutex;
        
        ResponseEntry(uWS::HttpResponse<false>* res) 
            : response(res), created_at(std::chrono::steady_clock::now()) {}
    };

private:
    std::unordered_map<std::string, std::shared_ptr<ResponseEntry>> responses_;
    mutable std::mutex registry_mutex_;
    
    std::string generate_uuid() const;
    
public:
    std::string register_response(uWS::HttpResponse<false>* res);
    
    bool send_response(const std::string& request_id, const nlohmann::json& data, 
                      bool is_error = false, int status_code = 200);
    
    void cleanup_expired(std::chrono::milliseconds max_age = std::chrono::seconds(60));
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return responses_.size();
    }
};

} // namespace queen
