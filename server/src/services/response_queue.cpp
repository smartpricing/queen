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

    // Lock the entry to ensure it's still valid before we start sending.
    // The lock is released when 'lock' goes out of scope.
    std::lock_guard<std::mutex> lock(entry->mutex);

    if (!entry->valid || !entry->response) {
        spdlog::debug("Response {} was already aborted or invalid before sending", request_id);
        return false;
    }

    try {
        // Set headers and status code
        entry->response->writeHeader("Access-Control-Allow-Origin", "*");
        entry->response->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        entry->response->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        entry->response->writeHeader("Content-Type", "application/json");
        entry->response->writeStatus(std::to_string(status_code));

        // Handle empty responses (like 204 No Content)
        if (data.is_null() || data.empty()) {
            entry->response->end();
            entry->response = nullptr;
            entry->valid = false;
            return true;
        }

        std::string json_str = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::ignore);
        
        // Log warning for very large responses
        if (json_str.length() > 10 * 1024 * 1024) { // 10MB threshold
            spdlog::warn("Very large response being sent: {} bytes for request {}", json_str.length(), request_id);
        }

        // For small payloads, send directly and we are done.
        if (json_str.length() <= 64 * 1024) {
            entry->response->end(json_str);
            entry->response = nullptr;
            entry->valid = false;
            spdlog::debug("Successfully sent small response for ID: {}", request_id);
            return true;
        }

        // --- CORRECTED LARGE PAYLOAD STREAMING LOGIC ---

        // 1. Create shared state for the asynchronous operation.
        struct ResponseData {
            std::string payload;
            size_t offset = 0;
            // Keep a reference to the entry to ensure its mutex and flags are accessible
            // and to prevent it from being prematurely destroyed.
            std::shared_ptr<ResponseEntry> entry_ref;
        };
        auto responseData = std::make_shared<ResponseData>();
        responseData->payload = std::move(json_str);
        responseData->entry_ref = entry;

        const size_t totalSize = responseData->payload.length();

        // 2. Define the writer logic in a single, reusable lambda.
        // This lambda will be used for both the initial write and subsequent onWritable calls.
        auto writer = [responseData, totalSize](uWS::HttpResponse<false>* res, int last_offset) mutable -> bool {
            // Check if the response was aborted
            if (!responseData->entry_ref->valid) {
                return false; // Stop streaming if aborted.
            }

            responseData->offset = last_offset;
            bool finished = false;

            res->cork([&]() {
                const size_t chunkSize = 64 * 1024;

                // Loop and send chunks until we are done or hit backpressure.
                while (responseData->offset < totalSize) {
                    size_t remaining = totalSize - responseData->offset;
                    size_t currentChunkSize = std::min(chunkSize, remaining);
                    std::string_view chunk(responseData->payload.data() + responseData->offset, currentChunkSize);

                    auto [ok, done] = res->tryEnd(chunk, totalSize);

                    if (done) {
                        finished = true;
                        // The stream is complete, invalidate the entry.
                        responseData->entry_ref->response = nullptr;
                        responseData->entry_ref->valid = false;
                        return; // Exit cork lambda
                    }

                    if (!ok) {
                        // Backpressure was applied. We must stop and wait for onWritable.
                        return; // Exit cork lambda
                    }
                    
                    // If ok, the chunk was sent. Update our offset and continue the loop.
                    responseData->offset += currentChunkSize;
                }
            });

            return !finished; // Return true to stay registered, false to unregister.
        };

        // 3. Register the writer as the onWritable handler.
        entry->response->onWritable([writer, res = entry->response](int offset) mutable {
            return writer(res, offset);
        });

        // 4. Kick off the streaming process by calling the writer immediately.
        // This handles the initial write and the loop for subsequent writes until
        // backpressure is hit for the first time.
        bool still_writing = writer(entry->response, 0);

        if (!still_writing) {
            spdlog::debug("Successfully sent large response for ID {} in a single go", request_id);
        } else {
            spdlog::debug("Started streaming large response for ID {}", request_id);
        }

        return true;

    } catch (const std::exception& e) {
        spdlog::error("Error sending response for ID {}: {}", request_id, e.what());
        // Ensure entry is invalidated on error
        entry->response = nullptr;
        entry->valid = false;
        return false;
    }
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
