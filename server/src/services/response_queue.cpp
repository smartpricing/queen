#include "queen/response_queue.hpp"
#include <App.h>
#include <random>
#include <sstream>
#include <iomanip>

namespace queen {

std::string ResponseRegistry::generate_uuid() const {
    // Simple UUID generation for request IDs
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<> dis(0, 15);
    static thread_local std::uniform_int_distribution<> dis2(8, 11);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    return ss.str();
}

std::string ResponseRegistry::register_response(uWS::HttpResponse<false>* res) {
    std::string request_id = generate_uuid();
    auto entry = std::make_shared<ResponseEntry>(res);
    
    // Set up abort handler to mark response as invalid
    res->onAborted([entry]() {
        std::lock_guard<std::mutex> lock(entry->mutex);
        entry->valid = false;
        entry->response = nullptr;
        spdlog::debug("Response aborted, marked as invalid");
    });
    
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        responses_[request_id] = entry;
    }
    
    spdlog::debug("Registered response with ID: {}", request_id);
    return request_id;
}

bool ResponseRegistry::send_response(const std::string& request_id, const nlohmann::json& data, 
                                   bool is_error, int status_code) {
    std::shared_ptr<ResponseEntry> entry;
    
    // Find and remove from registry
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        auto it = responses_.find(request_id);
        if (it == responses_.end()) {
            spdlog::debug("Response ID {} not found in registry", request_id);
            return false;
        }
        entry = it->second;
        responses_.erase(it);
    }
    
    // Send response safely
    {
        std::lock_guard<std::mutex> lock(entry->mutex);
        if (entry->valid && entry->response) {
            try {
                // Set CORS headers
                entry->response->writeHeader("Access-Control-Allow-Origin", "*");
                entry->response->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
                entry->response->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
                entry->response->writeHeader("Content-Type", "application/json");
                
                // Set status and send data
                entry->response->writeStatus(std::to_string(status_code));
                entry->response->end(data.dump());
                
                entry->response = nullptr;
                entry->valid = false;
                
                spdlog::debug("Successfully sent response for ID: {}", request_id);
                return true;
            } catch (const std::exception& e) {
                spdlog::error("Error sending response for ID {}: {}", request_id, e.what());
                return false;
            }
        }
    }
    
    spdlog::debug("Response {} was already aborted or invalid", request_id);
    return false;
}

void ResponseRegistry::cleanup_expired(std::chrono::milliseconds max_age) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    for (auto it = responses_.begin(); it != responses_.end();) {
        if (now - it->second->created_at > max_age) {
            spdlog::debug("Cleaning up expired response: {}", it->first);
            it = responses_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace queen
