/**
 *
 *           Message Queue Client - C++ Implementation            
 *                 Single Header Library                          
 *                                                                
 * 
 * Features:
 * - Fluent API matching Node.js client
 * - HTTP client with retry and failover
 * - Load balancing (round-robin & session)
 * - Client-side buffering with time/count triggers
 * - Consumer groups and partitions
 * - Atomic transactions
 * - Lease renewal
 * - Dead Letter Queue (DLQ) queries
 * - Concurrent consumers using astp::ThreadPool
 * - Graceful shutdown
 * 
 * Dependencies:
 * - C++17 or later
 * - nlohmann/json
 * - cpp-httplib (header-only HTTP client)
 * - astp::ThreadPool
 * 
 * Usage:
 *   #include "queen_client.hpp"
 *   
 *   queen::QueenClient client("http://localhost:6632");
 *   client.queue("tasks").create();
 *   client.queue("tasks").push({{"data", {{"job", "test"}}}}});
 *   client.queue("tasks").consume([](const json& msg) {
 *       std::cout << "Processing: " << msg << std::endl;
 *   });
 *   client.close();
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <iostream>
#include <optional>
#include <regex>
#include <future>
#include <csignal>

// JSON library (nlohmann)
#include <json.hpp>

// HTTP library
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

// ThreadPool
#include "../server/include/threadpool.hpp"

namespace queen {

using json = nlohmann::json;

// ============================================================================
// Forward Declarations
// ============================================================================

class QueenClient;
class QueueBuilder;
class TransactionBuilder;
class HttpClient;
class LoadBalancer;
class BufferManager;
class MessageBuffer;
class ConsumerManager;

// ============================================================================
// Configuration Structures
// ============================================================================

struct ClientConfig {
    std::vector<std::string> urls;
    int timeout_millis = 30000;
    int retry_attempts = 3;
    int retry_delay_millis = 1000;
    std::string load_balancing_strategy = "round-robin"; // "round-robin" or "session"
    bool enable_failover = true;
};

struct QueueConfig {
    int lease_time = 300;                    // seconds
    int retry_limit = 3;
    int priority = 0;
    int delayed_processing = 0;              // seconds
    int window_buffer = 0;                   // seconds
    int max_size = 0;
    int retention_seconds = 0;
    int completed_retention_seconds = 0;
    bool encryption_enabled = false;
    
    json to_json() const {
        return {
            {"leaseTime", lease_time},
            {"retryLimit", retry_limit},
            {"priority", priority},
            {"delayedProcessing", delayed_processing},
            {"windowBuffer", window_buffer},
            {"maxSize", max_size},
            {"retentionSeconds", retention_seconds},
            {"completedRetentionSeconds", completed_retention_seconds},
            {"encryptionEnabled", encryption_enabled}
        };
    }
};

struct ConsumeOptions {
    std::string queue;
    std::string partition;
    std::string namespace_name;
    std::string task;
    std::string group;
    int concurrency = 1;
    int batch = 1;
    int limit = 0;                           // 0 = unlimited
    int idle_millis = 0;                     // 0 = no idle timeout
    bool auto_ack = true;
    bool wait = true;
    int timeout_millis = 30000;
    bool renew_lease = false;
    int renew_lease_interval_millis = 60000;
    std::string subscription_mode;
    std::string subscription_from;
    bool each = false;
    std::atomic<bool>* stop_signal = nullptr;
};

struct BufferOptions {
    int message_count = 100;
    int time_millis = 1000;
};

// ============================================================================
// Utility Functions
// ============================================================================

namespace util {

/**
 * Generate UUIDv7 (time-ordered UUID with millisecond precision)
 * Based on queen::QueueManager::generate_uuid()
 */
inline std::string generate_uuid_v7() {
    static std::mutex uuid_mutex;
    static uint64_t last_ms = 0;
    static uint16_t sequence = 0;
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    
    std::lock_guard<std::mutex> lock(uuid_mutex);
    
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    uint64_t current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // If the clock moved backwards or stayed the same, increment the sequence
    if (current_ms <= last_ms) {
        sequence++;
    } else {
        last_ms = current_ms;
        sequence = 0;
    }
    
    std::array<uint8_t, 16> bytes;
    
    // 48-bit unix_ts_ms (big-endian)
    bytes[0] = (last_ms >> 40) & 0xFF;
    bytes[1] = (last_ms >> 32) & 0xFF;
    bytes[2] = (last_ms >> 24) & 0xFF;
    bytes[3] = (last_ms >> 16) & 0xFF;
    bytes[4] = (last_ms >> 8) & 0xFF;
    bytes[5] = last_ms & 0xFF;
    
    // 4-bit version (0111) and 12-bit sequence
    uint16_t sequence_and_version = sequence & 0x0FFF;
    bytes[6] = 0x70 | (sequence_and_version >> 8);
    bytes[7] = sequence_and_version & 0xFF;
    
    // 2-bit variant (10) and 62-bits of random data
    uint64_t rand_data = gen();
    bytes[8] = 0x80 | ((rand_data >> 56) & 0x3F);
    bytes[9] = (rand_data >> 48) & 0xFF;
    bytes[10] = (rand_data >> 40) & 0xFF;
    bytes[11] = (rand_data >> 32) & 0xFF;
    bytes[12] = (rand_data >> 24) & 0xFF;
    bytes[13] = (rand_data >> 16) & 0xFF;
    bytes[14] = (rand_data >> 8) & 0xFF;
    bytes[15] = rand_data & 0xFF;
    
    // Format to string: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) ss << '-';
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    
    return ss.str();
}

/**
 * Validate UUID format
 */
inline bool is_valid_uuid(const std::string& str) {
    static std::regex uuid_regex(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$",
        std::regex::icase
    );
    return std::regex_match(str, uuid_regex);
}

/**
 * URL encode string
 */
inline std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << int((unsigned char) c);
        }
    }
    
    return escaped.str();
}

/**
 * Parse URL into components
 */
inline std::tuple<std::string, std::string, int> parse_url(const std::string& url) {
    std::regex url_regex(R"(^(https?)://([^:/]+)(?::(\d+))?$)");
    std::smatch match;
    
    if (!std::regex_match(url, match, url_regex)) {
        throw std::invalid_argument("Invalid URL format: " + url);
    }
    
    std::string scheme = match[1].str();
    std::string host = match[2].str();
    int port = match[3].str().empty() ? (scheme == "https" ? 443 : 80) : std::stoi(match[3].str());
    
    return {scheme, host, port};
}

/**
 * Get current timestamp in ISO 8601 format
 */
inline std::string get_iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    
    return ss.str();
}

/**
 * Logging utility (controlled by QUEEN_CLIENT_LOG env var)
 */
inline bool is_log_enabled() {
    static bool enabled = []() {
        const char* env = std::getenv("QUEEN_CLIENT_LOG");
        return env && std::string(env) == "true";
    }();
    return enabled;
}

inline void log(const std::string& operation, const std::string& details) {
    if (is_log_enabled()) {
        std::cout << "[" << get_iso_timestamp() << "] [INFO] [" << operation << "] " 
                  << details << std::endl;
    }
}

inline void log_error(const std::string& operation, const std::string& details) {
    if (is_log_enabled()) {
        std::cerr << "[" << get_iso_timestamp() << "] [ERROR] [" << operation << "] " 
                  << details << std::endl;
    }
}

} // namespace util

// ============================================================================
// LoadBalancer - Distributes requests across multiple servers
// ============================================================================

class LoadBalancer {
private:
    std::vector<std::string> urls_;
    std::string strategy_;
    std::atomic<size_t> current_index_{0};
    std::mutex session_mutex_;
    std::unordered_map<std::string, size_t> session_map_;
    std::string session_id_;
    
public:
    LoadBalancer(const std::vector<std::string>& urls, const std::string& strategy = "round-robin")
        : urls_(urls), strategy_(strategy) {
        if (urls_.empty()) {
            throw std::invalid_argument("URLs vector cannot be empty");
        }
        
        // Remove trailing slashes
        for (auto& url : urls_) {
            if (!url.empty() && url.back() == '/') {
                url.pop_back();
            }
        }
        
        // Generate session ID
        session_id_ = "session_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }
    
    std::string get_next_url(const std::string& session_key = "") {
        const std::string& key = session_key.empty() ? session_id_ : session_key;
        
        if (strategy_ == "session") {
            // Session affinity: stick to the same server per session
            std::lock_guard<std::mutex> lock(session_mutex_);
            
            if (session_map_.find(key) == session_map_.end()) {
                size_t assigned_index = current_index_.fetch_add(1) % urls_.size();
                session_map_[key] = assigned_index;
            }
            
            return urls_[session_map_[key]];
        }
        
        // Round robin: cycle through URLs
        size_t index = current_index_.fetch_add(1) % urls_.size();
        return urls_[index];
    }
    
    std::vector<std::string> get_all_urls() const {
        return urls_;
    }
    
    std::string get_strategy() const {
        return strategy_;
    }
    
    void reset() {
        current_index_ = 0;
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_map_.clear();
    }
};

// ============================================================================
// HttpClient - HTTP client with retry, failover support
// ============================================================================

class HttpClient {
private:
    std::string base_url_;
    std::shared_ptr<LoadBalancer> load_balancer_;
    int timeout_millis_;
    int retry_attempts_;
    int retry_delay_millis_;
    bool enable_failover_;
    
    struct HttpResponse {
        int status_code;
        std::string body;
        bool success;
        std::string error_message;
    };
    
    HttpResponse execute_request(const std::string& url, const std::string& method,
                                 const std::string& path, const json& body = nullptr,
                                 int request_timeout_millis = 0) {
        try {
            auto [scheme, host, port] = util::parse_url(url);
            
            int timeout_sec = (request_timeout_millis > 0 ? request_timeout_millis : timeout_millis_) / 1000;
            
            httplib::Client client(host, port);
            client.set_connection_timeout(timeout_sec);
            client.set_read_timeout(timeout_sec);
            client.set_write_timeout(timeout_sec);
            
            httplib::Headers headers = {
                {"Content-Type", "application/json"}
            };
            
            httplib::Result res;
            
            if (method == "GET") {
                res = client.Get(path.c_str(), headers);
            } else if (method == "POST") {
                std::string body_str = body.is_null() ? "" : body.dump();
                res = client.Post(path.c_str(), headers, body_str, "application/json");
            } else if (method == "PUT") {
                std::string body_str = body.is_null() ? "" : body.dump();
                res = client.Put(path.c_str(), headers, body_str, "application/json");
            } else if (method == "DELETE") {
                res = client.Delete(path.c_str(), headers);
            } else {
                return {0, "", false, "Unsupported HTTP method: " + method};
            }
            
            if (!res) {
                return {0, "", false, "HTTP request failed: " + httplib::to_string(res.error())};
            }
            
            // Handle 204 No Content
            if (res->status == 204) {
                return {204, "", true, ""};
            }
            
            // Handle errors
            if (res->status >= 400) {
                std::string error_msg = "HTTP " + std::to_string(res->status);
                try {
                    if (!res->body.empty()) {
                        json error_body = json::parse(res->body);
                        if (error_body.contains("error")) {
                            error_msg = error_body["error"].get<std::string>();
                        }
                    }
                } catch (...) {
                    // Ignore JSON parse errors
                }
                return {res->status, res->body, false, error_msg};
            }
            
            return {res->status, res->body, true, ""};
            
        } catch (const std::exception& e) {
            return {0, "", false, std::string("Exception: ") + e.what()};
        }
    }
    
    HttpResponse request_with_retry(const std::string& method, const std::string& path,
                                    const json& body = nullptr, int request_timeout_millis = 0) {
        HttpResponse last_response;
        
        for (int attempt = 0; attempt < retry_attempts_; ++attempt) {
            std::string url = get_url();
            last_response = execute_request(url, method, path, body, request_timeout_millis);
            
            if (last_response.success) {
                return last_response;
            }
            
            // Don't retry on client errors (4xx)
            if (last_response.status_code >= 400 && last_response.status_code < 500) {
                return last_response;
            }
            
            // Wait before retry (except on last attempt)
            if (attempt < retry_attempts_ - 1) {
                int delay = retry_delay_millis_ * (1 << attempt); // Exponential backoff
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        }
        
        return last_response;
    }
    
    HttpResponse request_with_failover(const std::string& method, const std::string& path,
                                       const json& body = nullptr, int request_timeout_millis = 0) {
        if (!load_balancer_ || !enable_failover_) {
            return request_with_retry(method, path, body, request_timeout_millis);
        }
        
        auto urls = load_balancer_->get_all_urls();
        std::unordered_set<std::string> attempted_urls;
        HttpResponse last_response;
        
        for (size_t i = 0; i < urls.size(); ++i) {
            std::string url = load_balancer_->get_next_url();
            
            if (attempted_urls.count(url)) {
                continue;
            }
            
            attempted_urls.insert(url);
            last_response = execute_request(url, method, path, body, request_timeout_millis);
            
            if (last_response.success) {
                return last_response;
            }
            
            // Don't retry on client errors (4xx)
            if (last_response.status_code >= 400 && last_response.status_code < 500) {
                return last_response;
            }
        }
        
        return last_response;
    }
    
    std::string get_url() const {
        if (load_balancer_) {
            return load_balancer_->get_next_url();
        }
        return base_url_;
    }
    
public:
    HttpClient(const std::string& base_url, int timeout_millis = 30000,
               int retry_attempts = 3, int retry_delay_millis = 1000)
        : base_url_(base_url), timeout_millis_(timeout_millis),
          retry_attempts_(retry_attempts), retry_delay_millis_(retry_delay_millis),
          enable_failover_(false) {
        // Remove trailing slash
        if (!base_url_.empty() && base_url_.back() == '/') {
            base_url_.pop_back();
        }
    }
    
    HttpClient(std::shared_ptr<LoadBalancer> load_balancer, int timeout_millis = 30000,
               int retry_attempts = 3, int retry_delay_millis = 1000, bool enable_failover = true)
        : load_balancer_(load_balancer), timeout_millis_(timeout_millis),
          retry_attempts_(retry_attempts), retry_delay_millis_(retry_delay_millis),
          enable_failover_(enable_failover) {
    }
    
    json get(const std::string& path, int request_timeout_millis = 0) {
        auto response = request_with_failover("GET", path, nullptr, request_timeout_millis);
        
        if (!response.success) {
            throw std::runtime_error(response.error_message);
        }
        
        if (response.body.empty() || response.status_code == 204) {
            return nullptr;
        }
        
        return json::parse(response.body);
    }
    
    json post(const std::string& path, const json& body = nullptr, int request_timeout_millis = 0) {
        auto response = request_with_failover("POST", path, body, request_timeout_millis);
        
        if (!response.success) {
            throw std::runtime_error(response.error_message);
        }
        
        if (response.body.empty() || response.status_code == 204) {
            return nullptr;
        }
        
        return json::parse(response.body);
    }
    
    json put(const std::string& path, const json& body = nullptr, int request_timeout_millis = 0) {
        auto response = request_with_failover("PUT", path, body, request_timeout_millis);
        
        if (!response.success) {
            throw std::runtime_error(response.error_message);
        }
        
        if (response.body.empty() || response.status_code == 204) {
            return nullptr;
        }
        
        return json::parse(response.body);
    }
    
    json del(const std::string& path, int request_timeout_millis = 0) {
        auto response = request_with_failover("DELETE", path, nullptr, request_timeout_millis);
        
        if (!response.success) {
            throw std::runtime_error(response.error_message);
        }
        
        if (response.body.empty() || response.status_code == 204) {
            return nullptr;
        }
        
        return json::parse(response.body);
    }
    
    std::shared_ptr<LoadBalancer> get_load_balancer() const {
        return load_balancer_;
    }
};

// ============================================================================
// MessageBuffer - Buffer for a single queue
// ============================================================================

class MessageBuffer {
private:
    std::string queue_address_;
    std::vector<json> messages_;
    BufferOptions options_;
    std::function<void(const std::string&)> flush_callback_;
    std::mutex mutex_;
    std::unique_ptr<std::thread> timer_thread_;
    std::atomic<bool> timer_active_{false};
    std::atomic<bool> flushing_{false};
    std::chrono::steady_clock::time_point first_message_time_;
    
    void start_timer() {
        if (timer_active_) return;
        
        timer_active_ = true;
        first_message_time_ = std::chrono::steady_clock::now();
        
        timer_thread_ = std::make_unique<std::thread>([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(options_.time_millis));
            if (timer_active_ && !flushing_) {
                flush_callback_(queue_address_);
            }
        });
        timer_thread_->detach();
    }
    
public:
    MessageBuffer(const std::string& queue_address, const BufferOptions& options,
                 std::function<void(const std::string&)> flush_callback)
        : queue_address_(queue_address), options_(options), flush_callback_(flush_callback) {
    }
    
    ~MessageBuffer() {
        cancel_timer();
    }
    
    void add(const json& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (messages_.empty()) {
            start_timer();
        }
        
        messages_.push_back(message);
        
        if (messages_.size() >= static_cast<size_t>(options_.message_count)) {
            // Trigger flush (will be called outside lock)
            timer_active_ = false;
            flush_callback_(queue_address_);
        }
    }
    
    std::vector<json> extract_messages(int batch_size = -1) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (batch_size < 0 || batch_size >= static_cast<int>(messages_.size())) {
            std::vector<json> result = std::move(messages_);
            messages_.clear();
            timer_active_ = false;
            flushing_ = false;
            return result;
        }
        
        std::vector<json> result(messages_.begin(), messages_.begin() + batch_size);
        messages_.erase(messages_.begin(), messages_.begin() + batch_size);
        
        if (messages_.empty()) {
            timer_active_ = false;
            flushing_ = false;
        }
        
        return result;
    }
    
    void set_flushing(bool value) {
        flushing_ = value;
    }
    
    void cancel_timer() {
        timer_active_ = false;
    }
    
    size_t message_count() const {
        return messages_.size();
    }
    
    const BufferOptions& get_options() const {
        return options_;
    }
    
    int first_message_age_millis() const {
        if (messages_.empty()) return 0;
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - first_message_time_).count();
    }
};

// ============================================================================
// BufferManager - Manages buffers for all queues
// ============================================================================

class BufferManager {
private:
    std::shared_ptr<HttpClient> http_client_;
    std::unordered_map<std::string, std::unique_ptr<MessageBuffer>> buffers_;
    mutable std::mutex mutex_;  // mutable so it can be locked in const methods
    std::atomic<int> flush_count_{0};
    
    void flush_buffer_internal(const std::string& queue_address) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        auto it = buffers_.find(queue_address);
        if (it == buffers_.end() || it->second->message_count() == 0) {
            return;
        }
        
        auto& buffer = it->second;
        buffer->set_flushing(true);
        
        lock.unlock();
        
        try {
            auto messages = buffer->extract_messages();
            
            if (!messages.empty()) {
                json request = {{"items", messages}};
                http_client_->post("/api/v1/push", request);
                flush_count_++;
            }
            
            lock.lock();
            if (buffer->message_count() == 0) {
                buffers_.erase(queue_address);
            }
            
        } catch (const std::exception& e) {
            util::log_error("BufferManager.flush", std::string("Error: ") + e.what());
            buffer->set_flushing(false);
            throw;
        }
    }
    
public:
    BufferManager(std::shared_ptr<HttpClient> http_client)
        : http_client_(http_client) {
    }
    
    void add_message(const std::string& queue_address, const json& formatted_message,
                    const BufferOptions& options) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (buffers_.find(queue_address) == buffers_.end()) {
            auto flush_callback = [this](const std::string& addr) {
                flush_buffer_internal(addr);
            };
            
            buffers_[queue_address] = std::make_unique<MessageBuffer>(
                queue_address, options, flush_callback
            );
        }
        
        buffers_[queue_address]->add(formatted_message);
    }
    
    void flush_buffer(const std::string& queue_address) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        auto it = buffers_.find(queue_address);
        if (it == buffers_.end()) {
            return;
        }
        
        auto& buffer = it->second;
        buffer->cancel_timer();
        
        lock.unlock();
        
        while (buffer->message_count() > 0) {
            flush_buffer_internal(queue_address);
        }
    }
    
    void flush_all_buffers() {
        std::vector<std::string> queue_addresses;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& pair : buffers_) {
                queue_addresses.push_back(pair.first);
            }
        }
        
        for (const auto& addr : queue_addresses) {
            flush_buffer(addr);
        }
    }
    
    json get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int total_buffered = 0;
        int oldest_age = 0;
        
        for (const auto& pair : buffers_) {
            total_buffered += pair.second->message_count();
            oldest_age = std::max(oldest_age, pair.second->first_message_age_millis());
        }
        
        return {
            {"activeBuffers", buffers_.size()},
            {"totalBufferedMessages", total_buffered},
            {"oldestBufferAge", oldest_age},
            {"flushesPerformed", flush_count_.load()}
        };
    }
    
    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffers_.clear();
    }
};

// ============================================================================
// TransactionBuilder - Atomic transactions
// ============================================================================

class TransactionBuilder {
private:
    std::shared_ptr<HttpClient> http_client_;
    json operations_ = json::array();
    std::vector<std::string> required_leases_;
    
    class QueuePushBuilder {
    private:
        TransactionBuilder* parent_;
        std::string queue_name_;
        
    public:
        QueuePushBuilder(TransactionBuilder* parent, const std::string& queue_name)
            : parent_(parent), queue_name_(queue_name) {
        }
        
        TransactionBuilder& push(const std::vector<json>& items) {
            json push_items = json::array();
            
            for (const auto& item : items) {
                json payload;
                if (item.contains("data")) {
                    payload = item["data"];
                } else if (item.contains("payload")) {
                    payload = item["payload"];
                } else {
                    payload = item;
                }
                
                push_items.push_back({
                    {"queue", queue_name_},
                    {"payload", payload}
                });
            }
            
            parent_->operations_.push_back({
                {"type", "push"},
                {"items", push_items}
            });
            
            return *parent_;
        }
    };
    
public:
    TransactionBuilder(std::shared_ptr<HttpClient> http_client)
        : http_client_(http_client) {
    }
    
    TransactionBuilder& ack(const json& message, const std::string& status = "completed") {
        std::vector<json> messages;
        if (message.is_array()) {
            messages = message.get<std::vector<json>>();
        } else {
            messages.push_back(message);
        }
        
        for (const auto& msg : messages) {
            std::string transaction_id;
            if (msg.is_string()) {
                transaction_id = msg.get<std::string>();
            } else if (msg.contains("transactionId")) {
                transaction_id = msg["transactionId"].get<std::string>();
            } else if (msg.contains("id")) {
                transaction_id = msg["id"].get<std::string>();
            } else {
                throw std::runtime_error("Message must have transactionId or id property");
            }
            
            if (!msg.is_object() || !msg.contains("partitionId")) {
                throw std::runtime_error("Message must have partitionId property to ensure message uniqueness");
            }
            
            std::string partition_id = msg["partitionId"].get<std::string>();
            
            json ack_op = {
                {"type", "ack"},
                {"transactionId", transaction_id},
                {"partitionId", partition_id},
                {"status", status}
            };
            
            if (msg.contains("leaseId") && !msg["leaseId"].is_null()) {
                std::string lease_id = msg["leaseId"].get<std::string>();
                required_leases_.push_back(lease_id);
            }
            
            operations_.push_back(ack_op);
        }
        
        return *this;
    }
    
    QueuePushBuilder queue(const std::string& queue_name) {
        return QueuePushBuilder(this, queue_name);
    }
    
    json commit() {
        if (operations_.empty()) {
            throw std::runtime_error("Transaction has no operations to commit");
        }
        
        // Remove duplicate leases
        std::sort(required_leases_.begin(), required_leases_.end());
        required_leases_.erase(std::unique(required_leases_.begin(), required_leases_.end()),
                              required_leases_.end());
        
        json request = {
            {"operations", operations_},
            {"requiredLeases", required_leases_}
        };
        
        json result = http_client_->post("/api/v1/transaction", request);
        
        if (!result.contains("success") || !result["success"].get<bool>()) {
            std::string error = result.contains("error") ? 
                result["error"].get<std::string>() : "Transaction failed";
            throw std::runtime_error(error);
        }
        
        return result;
    }
};

// ============================================================================
// DLQBuilder - Dead Letter Queue query builder
// ============================================================================

class DLQBuilder {
private:
    std::shared_ptr<HttpClient> http_client_;
    std::string queue_name_;
    std::string consumer_group_;
    std::string partition_;
    int limit_ = 100;
    int offset_ = 0;
    std::string from_;
    std::string to_;
    
public:
    DLQBuilder(std::shared_ptr<HttpClient> http_client, const std::string& queue_name,
              const std::string& consumer_group = "", const std::string& partition = "")
        : http_client_(http_client), queue_name_(queue_name),
          consumer_group_(consumer_group), partition_(partition) {
    }
    
    DLQBuilder& limit(int count) {
        limit_ = std::max(1, count);
        return *this;
    }
    
    DLQBuilder& offset(int count) {
        offset_ = std::max(0, count);
        return *this;
    }
    
    DLQBuilder& from(const std::string& timestamp) {
        from_ = timestamp;
        return *this;
    }
    
    DLQBuilder& to(const std::string& timestamp) {
        to_ = timestamp;
        return *this;
    }
    
    json get() {
        std::stringstream params;
        params << "?queue=" << util::url_encode(queue_name_);
        params << "&limit=" << limit_;
        params << "&offset=" << offset_;
        
        if (!consumer_group_.empty()) {
            params << "&consumerGroup=" << util::url_encode(consumer_group_);
        }
        
        if (!partition_.empty()) {
            params << "&partition=" << util::url_encode(partition_);
        }
        
        if (!from_.empty()) {
            params << "&from=" << util::url_encode(from_);
        }
        
        if (!to_.empty()) {
            params << "&to=" << util::url_encode(to_);
        }
        
        try {
            json result = http_client_->get("/api/v1/dlq" + params.str());
            if (result.is_null()) {
                return {{"messages", json::array()}, {"total", 0}};
            }
            return result;
        } catch (const std::exception& e) {
            util::log_error("DLQBuilder.get", std::string("Error: ") + e.what());
            return {{"messages", json::array()}, {"total", 0}};
        }
    }
};

// ============================================================================
// ConsumerManager - Manages concurrent workers using ThreadPool
// ============================================================================

class ConsumerManager {
private:
    std::shared_ptr<HttpClient> http_client_;
    QueenClient* queen_;
    astp::ThreadPool thread_pool_;
    
    void enhance_message_with_trace(json& message, const std::string& consumer_group) {
        // Note: In C++, we can't add methods to json objects like in JavaScript
        // Instead, we provide a separate trace function that takes the message
        // The user would call: queen.trace(message, trace_config)
    }
    
public:
    ConsumerManager(std::shared_ptr<HttpClient> http_client, QueenClient* queen,
                   int pool_size = std::thread::hardware_concurrency())
        : http_client_(http_client), queen_(queen), thread_pool_(pool_size) {
    }
    
    ~ConsumerManager() {
        thread_pool_.wait();
    }
    
    void start(std::function<void(const json&)> handler, const ConsumeOptions& options);
    // Implementation will be defined after QueenClient declaration
};

// ============================================================================
// QueueBuilder - Fluent API for queue operations
// ============================================================================

class QueueBuilder {
private:
    QueenClient* queen_;
    std::shared_ptr<HttpClient> http_client_;
    std::shared_ptr<BufferManager> buffer_manager_;
    std::string queue_name_;
    std::string partition_ = "Default";
    std::string namespace_;
    std::string task_;
    std::string group_;
    QueueConfig config_;
    
    // Consume options
    int concurrency_ = 1;
    int batch_ = 1;
    int limit_ = 0;
    int idle_millis_ = 0;
    bool auto_ack_ = true;
    bool wait_ = true;
    int timeout_millis_ = 30000;
    bool renew_lease_ = false;
    int renew_lease_interval_millis_ = 60000;
    std::string subscription_mode_;
    std::string subscription_from_;
    bool each_ = false;
    
    // Buffer options
    std::optional<BufferOptions> buffer_options_;
    
public:
    QueueBuilder(QueenClient* queen, std::shared_ptr<HttpClient> http_client,
                std::shared_ptr<BufferManager> buffer_manager, const std::string& queue_name = "")
        : queen_(queen), http_client_(http_client), buffer_manager_(buffer_manager),
          queue_name_(queue_name) {
    }
    
    // Queue configuration methods
    QueueBuilder& namespace_name(const std::string& name) {
        namespace_ = name;
        return *this;
    }
    
    QueueBuilder& task(const std::string& name) {
        task_ = name;
        return *this;
    }
    
    QueueBuilder& config(const QueueConfig& cfg) {
        config_ = cfg;
        return *this;
    }
    
    json create() {
        json payload = {
            {"queue", queue_name_},
            {"namespace", namespace_},
            {"task", task_},
            {"options", config_.to_json()}
        };
        
        return http_client_->post("/api/v1/configure", payload);
    }
    
    json del() {
        if (queue_name_.empty()) {
            throw std::runtime_error("Queue name is required for delete operation");
        }
        
        std::string path = "/api/v1/resources/queues/" + util::url_encode(queue_name_);
        return http_client_->del(path);
    }
    
    // Push methods
    QueueBuilder& partition(const std::string& name) {
        partition_ = name;
        return *this;
    }
    
    QueueBuilder& buffer(const BufferOptions& options) {
        buffer_options_ = options;
        return *this;
    }
    
    json push(const std::vector<json>& payload) {
        if (queue_name_.empty()) {
            throw std::runtime_error("Queue name is required for push operation");
        }
        
        json formatted_items = json::array();
        
        for (const auto& item : payload) {
            json payload_value;
            if (item.contains("data")) {
                payload_value = item["data"];
            } else if (item.contains("payload")) {
                payload_value = item["payload"];
            } else {
                payload_value = item;
            }
            
            json formatted = {
                {"queue", queue_name_},
                {"partition", partition_},
                {"payload", payload_value},
                {"transactionId", item.contains("transactionId") ? 
                    item["transactionId"].get<std::string>() : util::generate_uuid_v7()}
            };
            
            if (item.contains("traceId") && item["traceId"].is_string()) {
                std::string trace_id = item["traceId"].get<std::string>();
                if (util::is_valid_uuid(trace_id)) {
                    formatted["traceId"] = trace_id;
                }
            }
            
            formatted_items.push_back(formatted);
        }
        
        // Client-side buffering
        if (buffer_options_.has_value()) {
            for (const auto& item : formatted_items) {
                std::string queue_address = queue_name_ + "/" + partition_;
                buffer_manager_->add_message(queue_address, item, buffer_options_.value());
            }
            
            return {
                {"buffered", true},
                {"count", formatted_items.size()}
            };
        }
        
        // Immediate push
        json request = {{"items", formatted_items}};
        return http_client_->post("/api/v1/push", request);
    }
    
    // Consume configuration methods
    QueueBuilder& group(const std::string& name) {
        group_ = name;
        return *this;
    }
    
    QueueBuilder& concurrency(int count) {
        concurrency_ = std::max(1, count);
        return *this;
    }
    
    QueueBuilder& batch(int size) {
        batch_ = std::max(1, size);
        return *this;
    }
    
    QueueBuilder& limit(int count) {
        limit_ = count;
        return *this;
    }
    
    QueueBuilder& idle_millis(int millis) {
        idle_millis_ = millis;
        return *this;
    }
    
    QueueBuilder& auto_ack(bool enabled) {
        auto_ack_ = enabled;
        return *this;
    }
    
    QueueBuilder& renew_lease(bool enabled, int interval_millis = 60000) {
        renew_lease_ = enabled;
        if (interval_millis > 0) {
            renew_lease_interval_millis_ = interval_millis;
        }
        return *this;
    }
    
    QueueBuilder& subscription_mode(const std::string& mode) {
        subscription_mode_ = mode;
        return *this;
    }
    
    QueueBuilder& subscription_from(const std::string& from) {
        subscription_from_ = from;
        return *this;
    }
    
    QueueBuilder& each() {
        each_ = true;
        return *this;
    }
    
    // Consume method
    void consume(std::function<void(const json&)> handler, std::atomic<bool>* stop_signal = nullptr);
    // Implementation will be defined after QueenClient declaration
    
    // Pop methods
    QueueBuilder& wait(bool enabled) {
        wait_ = enabled;
        return *this;
    }
    
    json pop() {
        std::stringstream path;
        
        if (!queue_name_.empty()) {
            if (partition_ != "Default") {
                path << "/api/v1/pop/queue/" << util::url_encode(queue_name_)
                     << "/partition/" << util::url_encode(partition_);
            } else {
                path << "/api/v1/pop/queue/" << util::url_encode(queue_name_);
            }
        } else if (!namespace_.empty() || !task_.empty()) {
            path << "/api/v1/pop";
        } else {
            throw std::runtime_error("Must specify queue, namespace, or task for pop operation");
        }
        
        std::stringstream params;
        params << "?batch=" << batch_;
        params << "&wait=" << (wait_ ? "true" : "false");
        params << "&timeout=" << timeout_millis_;
        
        if (!group_.empty()) {
            params << "&consumerGroup=" << util::url_encode(group_);
        }
        if (!namespace_.empty()) {
            params << "&namespace=" << util::url_encode(namespace_);
        }
        if (!task_.empty()) {
            params << "&task=" << util::url_encode(task_);
        }
        if (!subscription_mode_.empty()) {
            params << "&subscriptionMode=" << util::url_encode(subscription_mode_);
        }
        if (!subscription_from_.empty()) {
            params << "&subscriptionFrom=" << util::url_encode(subscription_from_);
        }
        
        try {
            int client_timeout = wait_ ? timeout_millis_ + 5000 : timeout_millis_;
            json result = http_client_->get(path.str() + params.str(), client_timeout);
            
            if (result.is_null() || !result.contains("messages")) {
                return json::array();
            }
            
            return result["messages"];
        } catch (const std::exception& e) {
            util::log_error("QueueBuilder.pop", std::string("Error: ") + e.what());
            return json::array();
        }
    }
    
    // Buffer management
    void flush_buffer() {
        if (queue_name_.empty()) {
            throw std::runtime_error("Queue name is required for buffer flush");
        }
        std::string queue_address = queue_name_ + "/" + partition_;
        buffer_manager_->flush_buffer(queue_address);
    }
    
    // DLQ methods
    DLQBuilder dlq(const std::string& consumer_group = "") {
        if (queue_name_.empty()) {
            throw std::runtime_error("Queue name is required for DLQ operations");
        }
        return DLQBuilder(http_client_, queue_name_, consumer_group, 
                         partition_ != "Default" ? partition_ : "");
    }
};

// ============================================================================
// QueenClient - Main client class
// ============================================================================

class QueenClient {
private:
    ClientConfig config_;
    std::shared_ptr<HttpClient> http_client_;
    std::shared_ptr<BufferManager> buffer_manager_;
    std::atomic<bool> shutdown_requested_{false};
    
    void setup_graceful_shutdown() {
        // Register signal handlers
        std::signal(SIGINT, [](int) {
            std::cout << "\nReceived SIGINT, shutting down gracefully..." << std::endl;
            // Note: In production, you'd need a global instance pointer or better signal handling
            exit(0);
        });
        
        std::signal(SIGTERM, [](int) {
            std::cout << "\nReceived SIGTERM, shutting down gracefully..." << std::endl;
        });
    }
    
public:
    QueenClient(const std::string& url) {
        config_.urls = {url};
        config_.timeout_millis = 30000;
        config_.retry_attempts = 3;
        config_.retry_delay_millis = 1000;
        config_.load_balancing_strategy = "round-robin";
        config_.enable_failover = true;
        
        http_client_ = std::make_shared<HttpClient>(url, config_.timeout_millis,
            config_.retry_attempts, config_.retry_delay_millis);
        
        buffer_manager_ = std::make_shared<BufferManager>(http_client_);
        
        setup_graceful_shutdown();
    }
    
    QueenClient(const std::vector<std::string>& urls, const ClientConfig& config = ClientConfig())
        : config_(config) {
        config_.urls = urls;
        
        if (urls.size() == 1) {
            http_client_ = std::make_shared<HttpClient>(urls[0], config_.timeout_millis,
                config_.retry_attempts, config_.retry_delay_millis);
        } else {
            auto load_balancer = std::make_shared<LoadBalancer>(urls, config_.load_balancing_strategy);
            http_client_ = std::make_shared<HttpClient>(load_balancer, config_.timeout_millis,
                config_.retry_attempts, config_.retry_delay_millis, config_.enable_failover);
        }
        
        buffer_manager_ = std::make_shared<BufferManager>(http_client_);
        
        setup_graceful_shutdown();
    }
    
    QueueBuilder queue(const std::string& name = "") {
        return QueueBuilder(this, http_client_, buffer_manager_, name);
    }
    
    TransactionBuilder transaction() {
        return TransactionBuilder(http_client_);
    }
    
    json ack(const json& message, bool status = true, const json& context = json::object()) {
        bool is_batch = message.is_array();
        
        if (is_batch) {
            auto messages = message.get<std::vector<json>>();
            if (messages.empty()) {
                return {{"processed", 0}, {"results", json::array()}};
            }
            
            json acknowledgments = json::array();
            std::string status_str = status ? "completed" : "failed";
            
            for (const auto& msg : messages) {
                std::string transaction_id;
                if (msg.is_string()) {
                    transaction_id = msg.get<std::string>();
                } else if (msg.contains("transactionId")) {
                    transaction_id = msg["transactionId"].get<std::string>();
                } else if (msg.contains("id")) {
                    transaction_id = msg["id"].get<std::string>();
                } else {
                    throw std::runtime_error("Message must have transactionId or id property");
                }
                
                if (!msg.is_object() || !msg.contains("partitionId")) {
                    throw std::runtime_error("Message must have partitionId property");
                }
                
                std::string partition_id = msg["partitionId"].get<std::string>();
                
                json ack = {
                    {"transactionId", transaction_id},
                    {"partitionId", partition_id},
                    {"status", status_str},
                    {"error", context.contains("error") ? context["error"] : nullptr}
                };
                
                if (msg.contains("leaseId") && !msg["leaseId"].is_null()) {
                    ack["leaseId"] = msg["leaseId"];
                }
                
                acknowledgments.push_back(ack);
            }
            
            json request = {
                {"acknowledgments", acknowledgments},
                {"consumerGroup", context.contains("group") ? context["group"] : nullptr}
            };
            
            try {
                json result = http_client_->post("/api/v1/ack/batch", request);
                return {{"success", true}, {"result", result}};
            } catch (const std::exception& e) {
                return {{"success", false}, {"error", e.what()}};
            }
        }
        
        // Single message
        std::string transaction_id;
        std::string partition_id;
        
        if (message.is_string()) {
            transaction_id = message.get<std::string>();
        } else if (message.contains("transactionId")) {
            transaction_id = message["transactionId"].get<std::string>();
        } else if (message.contains("id")) {
            transaction_id = message["id"].get<std::string>();
        } else {
            return {{"success", false}, {"error", "Message must have transactionId or id property"}};
        }
        
        if (!message.is_object() || !message.contains("partitionId")) {
            return {{"success", false}, {"error", "Message must have partitionId property"}};
        }
        
        partition_id = message["partitionId"].get<std::string>();
        
        std::string status_str = status ? "completed" : "failed";
        
        json body = {
            {"transactionId", transaction_id},
            {"partitionId", partition_id},
            {"status", status_str},
            {"error", context.contains("error") ? context["error"] : nullptr},
            {"consumerGroup", context.contains("group") ? context["group"] : nullptr}
        };
        
        if (message.contains("leaseId") && !message["leaseId"].is_null()) {
            body["leaseId"] = message["leaseId"];
        }
        
        try {
            json result = http_client_->post("/api/v1/ack", body);
            return {{"success", true}, {"result", result}};
        } catch (const std::exception& e) {
            return {{"success", false}, {"error", e.what()}};
        }
    }
    
    json renew(const json& message_or_lease_id) {
        std::vector<std::string> lease_ids;
        
        if (message_or_lease_id.is_string()) {
            lease_ids.push_back(message_or_lease_id.get<std::string>());
        } else if (message_or_lease_id.is_array()) {
            for (const auto& item : message_or_lease_id) {
                if (item.is_string()) {
                    lease_ids.push_back(item.get<std::string>());
                } else if (item.contains("leaseId")) {
                    lease_ids.push_back(item["leaseId"].get<std::string>());
                }
            }
        } else if (message_or_lease_id.is_object() && message_or_lease_id.contains("leaseId")) {
            lease_ids.push_back(message_or_lease_id["leaseId"].get<std::string>());
        }
        
        if (lease_ids.empty()) {
            return {{"success", false}, {"error", "No valid lease IDs found for renewal"}};
        }
        
        json results = json::array();
        for (const auto& lease_id : lease_ids) {
            try {
                json result = http_client_->post("/api/v1/lease/" + lease_id + "/extend", json::object());
                results.push_back({
                    {"leaseId", lease_id},
                    {"success", true},
                    {"newExpiresAt", result.contains("newExpiresAt") ? result["newExpiresAt"] : 
                                     result.contains("lease_expires_at") ? result["lease_expires_at"] : nullptr}
                });
            } catch (const std::exception& e) {
                results.push_back({
                    {"leaseId", lease_id},
                    {"success", false},
                    {"error", e.what()}
                });
            }
        }
        
        return message_or_lease_id.is_array() ? results : results[0];
    }
    
    void flush_all_buffers() {
        buffer_manager_->flush_all_buffers();
    }
    
    json get_buffer_stats() const {
        return buffer_manager_->get_stats();
    }
    
    void close() {
        std::cout << "Closing Queen client..." << std::endl;
        shutdown_requested_ = true;
        
        try {
            buffer_manager_->flush_all_buffers();
            std::cout << "All buffers flushed" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error flushing buffers: " << e.what() << std::endl;
        }
        
        buffer_manager_->cleanup();
        std::cout << "Queen client closed" << std::endl;
    }
    
    bool is_shutdown_requested() const {
        return shutdown_requested_;
    }
    
    std::shared_ptr<HttpClient> get_http_client() const {
        return http_client_;
    }
};

// ============================================================================
// ConsumerManager Implementation (after QueenClient is defined)
// ============================================================================

inline void ConsumerManager::start(std::function<void(const json&)> handler,
                                   const ConsumeOptions& options) {
    // Build the path
    std::string path;
    if (!options.queue.empty()) {
        if (!options.partition.empty()) {
            path = "/api/v1/pop/queue/" + util::url_encode(options.queue) +
                   "/partition/" + util::url_encode(options.partition);
        } else {
            path = "/api/v1/pop/queue/" + util::url_encode(options.queue);
        }
    } else if (!options.namespace_name.empty() || !options.task.empty()) {
        path = "/api/v1/pop";
    } else {
        throw std::runtime_error("Must specify queue, namespace, or task");
    }
    
    // Build params
    std::stringstream params;
    params << "?batch=" << options.batch;
    params << "&wait=" << (options.wait ? "true" : "false");
    params << "&timeout=" << options.timeout_millis;
    
    if (!options.group.empty()) {
        params << "&consumerGroup=" << util::url_encode(options.group);
    }
    if (!options.namespace_name.empty()) {
        params << "&namespace=" << util::url_encode(options.namespace_name);
    }
    if (!options.task.empty()) {
        params << "&task=" << util::url_encode(options.task);
    }
    if (!options.subscription_mode.empty()) {
        params << "&subscriptionMode=" << util::url_encode(options.subscription_mode);
    }
    if (!options.subscription_from.empty()) {
        params << "&subscriptionFrom=" << util::url_encode(options.subscription_from);
    }
    
    std::string full_url = path + params.str();
    
    // Worker function
    auto worker = [this, handler, full_url, options](int worker_id) {
        int processed_count = 0;
        auto last_message_time = std::chrono::steady_clock::now();
        
        while (true) {
            // Check stop signal
            if (options.stop_signal && options.stop_signal->load()) {
                break;
            }
            if (queen_->is_shutdown_requested()) {
                break;
            }
            
            // Check limit
            if (options.limit > 0 && processed_count >= options.limit) {
                break;
            }
            
            // Check idle timeout
            if (options.idle_millis > 0) {
                auto now = std::chrono::steady_clock::now();
                auto idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_message_time).count();
                if (idle_time >= options.idle_millis) {
                    break;
                }
            }
            
            try {
                int client_timeout = options.wait ? options.timeout_millis + 5000 : options.timeout_millis;
                json result = http_client_->get(full_url, client_timeout);
                
                if (result.is_null() || !result.contains("messages") || 
                    result["messages"].empty()) {
                    if (options.wait) {
                        continue; // Long polling timeout, retry
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                }
                
                json messages = result["messages"];
                if (messages.empty()) {
                    continue;
                }
                
                last_message_time = std::chrono::steady_clock::now();
                
                // TODO: Set up lease renewal if enabled
                
                // Process messages
                if (options.each) {
                    for (const auto& msg : messages) {
                        if (options.stop_signal && options.stop_signal->load()) break;
                        
                        try {
                            handler(msg);
                            if (options.auto_ack) {
                                json context = options.group.empty() ? 
                                    json::object() : json{{"group", options.group}};
                                queen_->ack(msg, true, context);
                            }
                        } catch (const std::exception& e) {
                            if (options.auto_ack) {
                                json context = options.group.empty() ? 
                                    json::object() : json{{"group", options.group}};
                                queen_->ack(msg, false, context);
                            }
                        }
                        
                        processed_count++;
                        if (options.limit > 0 && processed_count >= options.limit) break;
                    }
                } else {
                    // Process as batch
                    try {
                        handler(messages);
                        if (options.auto_ack) {
                            json context = options.group.empty() ? 
                                json::object() : json{{"group", options.group}};
                            queen_->ack(messages, true, context);
                        }
                    } catch (const std::exception& e) {
                        if (options.auto_ack) {
                            json context = options.group.empty() ? 
                                json::object() : json{{"group", options.group}};
                            queen_->ack(messages, false, context);
                        }
                    }
                    processed_count += messages.size();
                }
                
            } catch (const std::exception& e) {
                // Check if timeout error (expected for long polling)
                std::string error_msg = e.what();
                if (error_msg.find("timeout") != std::string::npos && options.wait) {
                    continue; // Retry on timeout
                }
                
                // Network error - wait before retry
                if (error_msg.find("Connection refused") != std::string::npos ||
                    error_msg.find("connect") != std::string::npos) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                
                // Other errors - rethrow
                util::log_error("ConsumerManager.worker", std::string("Worker ") + 
                              std::to_string(worker_id) + " error: " + e.what());
                throw;
            }
        }
    };
    
    // Start workers in thread pool
    std::vector<std::future<void>> futures;
    for (int i = 0; i < options.concurrency; ++i) {
        futures.push_back(thread_pool_.future_from_push([worker, i]() {
            worker(i);
        }));
    }
    
    // Wait for all workers to complete
    for (auto& future : futures) {
        future.wait();
    }
}

// ============================================================================
// QueueBuilder::consume Implementation (after ConsumerManager is defined)
// ============================================================================

inline void QueueBuilder::consume(std::function<void(const json&)> handler,
                                  std::atomic<bool>* stop_signal) {
    ConsumeOptions options;
    options.queue = queue_name_;
    options.partition = partition_ != "Default" ? partition_ : "";
    options.namespace_name = namespace_;
    options.task = task_;
    options.group = group_;
    options.concurrency = concurrency_;
    options.batch = batch_;
    options.limit = limit_;
    options.idle_millis = idle_millis_;
    options.auto_ack = auto_ack_;
    options.wait = wait_;
    options.timeout_millis = timeout_millis_;
    options.renew_lease = renew_lease_;
    options.renew_lease_interval_millis = renew_lease_interval_millis_;
    options.subscription_mode = subscription_mode_;
    options.subscription_from = subscription_from_;
    options.each = each_;
    options.stop_signal = stop_signal;
    
    ConsumerManager consumer_manager(http_client_, queen_);
    consumer_manager.start(handler, options);
}

} // namespace queen

