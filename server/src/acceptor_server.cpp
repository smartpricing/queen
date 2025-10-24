#include "queen/database.hpp"
#include "queen/queue_manager.hpp"
#include "queen/analytics_manager.hpp"
#include "queen/config.hpp"
#include "queen/encryption.hpp"
#include "queen/file_buffer.hpp"
#include "threadpool.hpp"
#include <App.h>
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <optional>
#include <memory>

namespace queen {

// Safe Response Context to prevent segfaults on client disconnect
class SafeResponseContext {
private:
    uWS::HttpResponse<false>* response_ptr_;
    std::atomic<bool> valid_;
    std::mutex response_mutex_;
    
public:
    SafeResponseContext(uWS::HttpResponse<false>* res) 
        : response_ptr_(res), valid_(true) {
        
        // Set up abort detection - clears pointer when client disconnects
        res->onAborted([this]() {
            std::lock_guard<std::mutex> lock(response_mutex_);
            valid_.store(false);
            response_ptr_ = nullptr; // CRITICAL: Clear pointer to prevent access
            spdlog::warn("SafeResponseContext: Client disconnected, response invalidated");
        });
    }
    
    bool is_valid() const { 
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(response_mutex_));
        return valid_.load() && response_ptr_ != nullptr; 
    }
    
    void safe_send_json(const nlohmann::json& data, int status_code = 200) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        if (valid_.load() && response_ptr_) {
            response_ptr_->writeHeader("Content-Type", "application/json");
            response_ptr_->writeStatus(std::to_string(status_code));
            response_ptr_->end(data.dump());
            response_ptr_ = nullptr; // Clear after use to prevent double-send
            valid_.store(false);
        }
    }
    
    // ATOMIC: Check validity and execute Loop::defer only if safe
    template<typename Callback>
    void safe_defer(uWS::Loop* loop, Callback&& callback) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        if (valid_.load() && response_ptr_) {
            // Only call Loop::defer if response is still valid
            loop->defer(std::forward<Callback>(callback));
        } else {
            spdlog::warn("SafeResponseContext: Client disconnected, skipping Loop::defer");
        }
    }
    
    void safe_send_error(const std::string& error, int status_code = 500) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        if (valid_.load() && response_ptr_) {
            nlohmann::json error_json = {{"error", error}};
            response_ptr_->writeHeader("Content-Type", "application/json");
            response_ptr_->writeStatus(std::to_string(status_code));
            response_ptr_->end(error_json.dump());
            response_ptr_ = nullptr; // Clear after use
            valid_.store(false);
        }
    }
    
    void safe_send_empty(int status_code = 204) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        if (valid_.load() && response_ptr_) {
            // Add CORS headers manually since setup_cors_headers is defined later
            response_ptr_->writeHeader("Access-Control-Allow-Origin", "*");
            response_ptr_->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            response_ptr_->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
            response_ptr_->writeStatus(std::to_string(status_code));
            response_ptr_->end();
            response_ptr_ = nullptr; // Clear after use
            valid_.store(false);
        }
    }
};

} // namespace queen

#include <json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <regex>
#include <set>

namespace queen {

// Global shutdown flag
std::atomic<bool> g_shutdown{false};

// Helper functions
static void setup_cors_headers(uWS::HttpResponse<false>* res) {
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res->writeHeader("Access-Control-Max-Age", "86400");
}

static void send_json_response(uWS::HttpResponse<false>* res, const nlohmann::json& json, int status_code = 200) {
    setup_cors_headers(res);
    res->writeHeader("Content-Type", "application/json");
    res->writeStatus(std::to_string(status_code));
    res->end(json.dump());
}

static void send_error_response(uWS::HttpResponse<false>* res, const std::string& error, int status_code = 500) {
    nlohmann::json error_json = {{"error", error}};
    send_json_response(res, error_json, status_code);
}

// Read JSON body helper
static void read_json_body(uWS::HttpResponse<false>* res,
                          std::function<void(const nlohmann::json&)> callback,
                          std::function<void(const std::string&)> error_callback) {
    auto buffer = std::make_shared<std::string>();
    auto completed = std::make_shared<bool>(false);
    
    res->onData([callback, error_callback, buffer, completed](std::string_view chunk, bool is_last) {
        buffer->append(chunk);
        
        if (is_last && !*completed) {
            *completed = true;
            try {
                if (buffer->empty()) {
                    error_callback("Empty request body");
                } else {
                    nlohmann::json json = nlohmann::json::parse(*buffer);
                    callback(json);
                }
            } catch (const std::exception& e) {
                error_callback("Invalid JSON: " + std::string(e.what()));
            }
        }
    });
    
    res->onAborted([completed]() {
        *completed = true;
    });
}

// Query parameter helpers
static std::string get_query_param(uWS::HttpRequest* req, const std::string& key, const std::string& default_value = "") {
    std::string query = std::string(req->getQuery());
    size_t pos = query.find(key + "=");
    if (pos == std::string::npos) return default_value;
    pos += key.length() + 1;
    size_t end = query.find('&', pos);
    if (end == std::string::npos) end = query.length();
    return query.substr(pos, end - pos);
}

static int get_query_param_int(uWS::HttpRequest* req, const std::string& key, int default_value) {
    std::string value = get_query_param(req, key);
    if (value.empty()) return default_value;
    try {
        return std::stoi(value);
    } catch (...) {
        return default_value;
    }
}

static bool get_query_param_bool(uWS::HttpRequest* req, const std::string& key, bool default_value) {
    std::string value = get_query_param(req, key);
    if (value.empty()) return default_value;
    return value == "true" || value == "1";
}

// Async POP polling helper structure
struct AsyncPopState {
    uWS::HttpResponse<false>* res;
    std::shared_ptr<QueueManager> queue_manager;
    std::string queue_name;
    std::string partition_name;
    std::string consumer_group;
    int batch;
    int worker_id;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::steady_clock::time_point start_time;
    std::shared_ptr<bool> aborted;
    uWS::Loop* loop;
    us_timer_t* timer;
    int retry_count;
    int current_interval_ms;  // Current backoff interval
};

// Timer callback for async POP retries
static void pop_retry_timer(us_timer_t* timer) {
    // Correctly read the pointer (we stored AsyncPopState*, not AsyncPopState**)
    AsyncPopState* state = *(AsyncPopState**)us_timer_ext(timer);
    
    // Check if already cleaned up (safety)
    if (state == nullptr) {
        us_timer_close(timer);
        return;
    }
    
    if (*state->aborted) {
        spdlog::debug("[Worker {}] POP aborted, canceling retries", state->worker_id);
        *(AsyncPopState**)us_timer_ext(timer) = nullptr;  // Mark as cleaned up
        us_timer_close(timer);
        delete state;
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now >= state->deadline) {
        // Timeout reached, send empty response
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - state->start_time).count();
        auto pool_stats = state->queue_manager->get_pool_stats();
        spdlog::info("[Worker {}] <<< POP END (timeout): {}/{}, got 0 msgs, took {}ms, retries={} | Pool: {}/{} conn ({} in use)", 
                    state->worker_id, state->queue_name, state->partition_name, duration_ms, state->retry_count,
                    pool_stats.available, pool_stats.total, pool_stats.in_use);
        
        if (!*state->aborted) {
            setup_cors_headers(state->res);
            state->res->writeStatus("204");
            state->res->end();
        }
        
        *(AsyncPopState**)us_timer_ext(timer) = nullptr;  // Mark as cleaned up
        us_timer_close(timer);
        delete state;
        return;
    }
    
    // Try to pop again (non-blocking single attempt)
    PopOptions options;
    options.wait = false;  // Non-blocking!
    options.timeout = 0;
    options.batch = state->batch;
    
    state->retry_count++;
    
    try {
        auto result = state->queue_manager->pop_messages(
            state->queue_name, 
            state->partition_name.empty() ? std::nullopt : std::optional<std::string>(state->partition_name),
            state->consumer_group, 
            options
        );
        
        if (!result.messages.empty()) {
            // Found messages! Send response
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - state->start_time).count();
            auto pool_stats = state->queue_manager->get_pool_stats();
            spdlog::info("[Worker {}] <<< POP END (found): {}/{}, got {} msgs, took {}ms, retries={} | Pool: {}/{} conn ({} in use)", 
                        state->worker_id, state->queue_name, state->partition_name, 
                        result.messages.size(), duration_ms, state->retry_count,
                        pool_stats.available, pool_stats.total, pool_stats.in_use);
            
            if (!*state->aborted) {
                nlohmann::json response = {{"messages", nlohmann::json::array()}};
                
                for (const auto& msg : result.messages) {
                    auto time_t = std::chrono::system_clock::to_time_t(msg.created_at);
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        msg.created_at.time_since_epoch()) % 1000;
                    
                    std::stringstream created_at_ss;
                    created_at_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
                    created_at_ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
                    
                    nlohmann::json msg_json = {
                        {"id", msg.id},
                        {"transactionId", msg.transaction_id},
                        {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                        {"queue", msg.queue_name},
                        {"partition", msg.partition_name},
                        {"data", msg.payload},
                        {"retryCount", msg.retry_count},
                        {"priority", msg.priority},
                        {"createdAt", created_at_ss.str()},
                        {"consumerGroup", nlohmann::json(nullptr)},
                        {"leaseId", result.lease_id.has_value() ? nlohmann::json(*result.lease_id) : nlohmann::json(nullptr)}
                    };
                    response["messages"].push_back(msg_json);
                }
                
                send_json_response(state->res, response);
            }
            
            *(AsyncPopState**)us_timer_ext(timer) = nullptr;  // Mark as cleaned up
            us_timer_close(timer);
            delete state;
            return;
        }
    } catch (const std::exception& e) {
        spdlog::error("[Worker {}] POP retry error: {}", state->worker_id, e.what());
    }
    
    // No messages yet, apply exponential backoff
    // Double the interval, max 2000ms
    state->current_interval_ms = std::min(state->current_interval_ms * 2, 2000);
    
    // Update timer with new interval
    us_timer_set(timer, pop_retry_timer, state->current_interval_ms, state->current_interval_ms);
}

// ============================================================================
// Static File Serving Helpers
// ============================================================================

// Get MIME type based on file extension
static std::string get_mime_type(const std::string& file_path) {
    std::filesystem::path p(file_path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    static const std::map<std::string, std::string> mime_types = {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".mjs", "application/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".eot", "application/vnd.ms-fontobject"},
        {".webp", "image/webp"},
        {".wasm", "application/wasm"}
    };
    
    auto it = mime_types.find(ext);
    return it != mime_types.end() ? it->second : "application/octet-stream";
}

// Serve static file from disk (on-demand)
static bool serve_static_file(uWS::HttpResponse<false>* res, 
                              const std::string& file_path,
                              const std::string& webapp_root) {
    try {
        // Convert to absolute paths first
        std::filesystem::path abs_file_path = std::filesystem::absolute(file_path);
        std::filesystem::path abs_webapp_root = std::filesystem::absolute(webapp_root);
        
        // Check if file exists first (canonical throws if file doesn't exist)
        if (!std::filesystem::exists(abs_file_path)) {
            return false; // File not found
        }
        
        if (!std::filesystem::is_regular_file(abs_file_path)) {
            return false; // Not a file
        }
        
        // Now safe to get canonical path for security check
        std::filesystem::path normalized_path = std::filesystem::canonical(abs_file_path);
        std::filesystem::path root_path = std::filesystem::canonical(abs_webapp_root);
        
        // Security check - ensure normalized path is within webapp root
        auto normalized_str = normalized_path.string();
        auto root_str = root_path.string();
        if (normalized_str.substr(0, root_str.length()) != root_str) {
            spdlog::warn("Directory traversal attempt blocked: {}", file_path);
            res->writeStatus("403");
            res->end("Forbidden");
            return true;
        }
        
        // Read file content
        std::ifstream file(normalized_path, std::ios::binary);
        if (!file) {
            return false;
        }
        
        // Get file size
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Read into string
        std::string content(file_size, '\0');
        file.read(&content[0], file_size);
        file.close();
        
        // Determine MIME type
        std::string mime_type = get_mime_type(file_path);
        
        // Determine cache strategy
        std::string cache_control;
        std::string file_name = normalized_path.filename().string();
        
        // Fingerprinted assets (Vite-style: name-hash.js)
        std::regex fingerprint_regex(R"(.*\.[a-f0-9]{8,}\.(js|css)$)");
        if (std::regex_match(file_name, fingerprint_regex)) {
            cache_control = "public, max-age=31536000, immutable";
        }
        // index.html - never cache
        else if (file_name == "index.html") {
            cache_control = "no-cache, no-store, must-revalidate";
        }
        // Other files - cache for 1 hour
        else {
            cache_control = "public, max-age=3600";
        }
        
        // Send response
        setup_cors_headers(res);
        res->writeHeader("Content-Type", mime_type);
        res->writeHeader("Content-Length", std::to_string(file_size));
        res->writeHeader("Cache-Control", cache_control);
        res->writeStatus("200");
        res->end(content);
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Error serving static file {}: {}", file_path, e.what());
        res->writeStatus("500");
        res->end("Internal Server Error");
        return true;
    }
}

// Setup routes for a worker app
static void setup_worker_routes(uWS::App* app, 
                                std::shared_ptr<QueueManager> queue_manager,
                                std::shared_ptr<AnalyticsManager> analytics_manager,
                                std::shared_ptr<FileBufferManager> file_buffer,
                                const Config& config,
                                int worker_id,
                                std::shared_ptr<astp::ThreadPool> db_thread_pool) {
    
    // CORS preflight
    app->options("/*", [](auto* res, auto* req) {
        setup_cors_headers(res);
        res->writeStatus("204");
        res->end();
    });
    
    // Health check
    app->get("/health", [queue_manager, worker_id](auto* res, auto* req) {
        try {
            bool healthy = queue_manager->health_check();
            nlohmann::json response = {
                {"status", healthy ? "healthy" : "unhealthy"},
                {"database", healthy ? "connected" : "disconnected"},
                {"server", "C++ Queen Server (Acceptor/Worker)"},
                {"worker_id", worker_id},
                {"version", "1.0.0"}
            };
            send_json_response(res, response, healthy ? 200 : 503);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // Configure queue
    app->post("/api/v1/configure", [queue_manager, worker_id](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager, worker_id](const nlohmann::json& body) {
                try {
                    spdlog::debug("Configure request body: {}", body.dump());
                    
                    if (!body.contains("queue") || !body["queue"].is_string()) {
                        send_error_response(res, "queue is required", 400);
                        return;
                    }
                    
                    std::string queue_name = body["queue"];
                    spdlog::debug("Queue name: {}", queue_name);
                    
                    std::string namespace_name = "";
                    std::string task_name = "";
                    
                    // Safe null handling
                    try {
                        if (body.contains("namespace")) {
                            if (body["namespace"].is_string()) {
                                namespace_name = body["namespace"].get<std::string>();
                            }
                        }
                        if (body.contains("task")) {
                            if (body["task"].is_string()) {
                                task_name = body["task"].get<std::string>();
                            }
                        }
                    } catch (const std::exception& e) {
                        spdlog::error("Error parsing namespace/task: {}", e.what());
                    }
                    
                    spdlog::debug("Namespace: '{}', Task: '{}'", namespace_name, task_name);
                    
                    QueueManager::QueueOptions options;
                    if (body.contains("options") && body["options"].is_object()) {
                        auto opts = body["options"];
                        options.lease_time = opts.value("leaseTime", 300);
                        options.max_size = opts.value("maxSize", 10000);
                        options.ttl = opts.value("ttl", 3600);
                        options.retry_limit = opts.value("retryLimit", 3);
                        options.retry_delay = opts.value("retryDelay", 1000);
                        options.dead_letter_queue = opts.value("deadLetterQueue", false);
                        options.dlq_after_max_retries = opts.value("dlqAfterMaxRetries", false);
                        options.priority = opts.value("priority", 0);
                        options.delayed_processing = opts.value("delayedProcessing", 0);
                        options.window_buffer = opts.value("windowBuffer", 0);
                        options.retention_seconds = opts.value("retentionSeconds", 0);
                        options.completed_retention_seconds = opts.value("completedRetentionSeconds", 0);
                        options.retention_enabled = opts.value("retentionEnabled", false);
                        options.encryption_enabled = opts.value("encryptionEnabled", false);
                        options.max_wait_time_seconds = opts.value("maxWaitTimeSeconds", 0);
                    }
                    
                    spdlog::info("[Worker {}] Configuring queue: {}", worker_id, queue_name);
                    bool success = queue_manager->configure_queue(queue_name, options, namespace_name, task_name);
                    
                    if (success) {
                        nlohmann::json response = {
                            {"configured", true},
                            {"namespace", namespace_name},
                            {"task", task_name},
                            {"queue", queue_name},
                            {"options", {
                                {"completedRetentionSeconds", options.completed_retention_seconds},
                                {"deadLetterQueue", options.dead_letter_queue},
                                {"delayedProcessing", options.delayed_processing},
                                {"dlqAfterMaxRetries", options.dlq_after_max_retries},
                                {"leaseTime", options.lease_time},
                                {"maxSize", options.max_size},
                                {"priority", options.priority},
                                {"retentionEnabled", options.retention_enabled},
                                {"retentionSeconds", options.retention_seconds},
                                {"retryDelay", options.retry_delay},
                                {"retryLimit", options.retry_limit},
                                {"ttl", options.ttl},
                                {"windowBuffer", options.window_buffer},
                                {"encryptionEnabled", options.encryption_enabled},
                                {"maxWaitTimeSeconds", options.max_wait_time_seconds}
                            }}
                        };
                        send_json_response(res, response, 200);
                    } else {
                        send_error_response(res, "Failed to configure queue", 500);
                    }
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // ASYNC: Push Route using per-worker ThreadPool + Loop::defer
    
    app->post("/api/v1/push", [queue_manager, file_buffer, worker_id, db_thread_pool](auto* res, auto* req) {
        // CRITICAL: Add abort detection to prevent segfaults
        auto aborted = std::make_shared<std::atomic<bool>>(false);
        res->onAborted([aborted]() {
            aborted->store(true);
            spdlog::warn("PUSH request aborted by client");
        });
        
        read_json_body(res,
            [res, queue_manager, file_buffer, worker_id, db_thread_pool, aborted](const nlohmann::json& body) {
                try {
                    if (!body.contains("items") || !body["items"].is_array()) {
                        send_error_response(res, "items array is required", 400);
                        return;
                    }
                    
                    // Get the current uWebSockets loop
                    uWS::Loop* current_loop = uWS::Loop::get();
                    
                    spdlog::info("[Worker {}] ASYNC: Submitting work to ThreadPool", worker_id);
                    
                    // Push work to ThreadPool with abort detection
                    db_thread_pool->push([res, queue_manager, file_buffer, worker_id, current_loop, body, aborted]() {
                        spdlog::info("[Worker {}] ASYNC: ThreadPool worker executing database operation", worker_id);
                        
                        try {
                            // Parse items (same as original logic)
                            std::vector<PushItem> items;
                            for (const auto& item_json : body["items"]) {
                                PushItem item;
                                item.queue = item_json["queue"];
                                item.partition = item_json.value("partition", "Default");
                                item.payload = item_json.value("payload", nlohmann::json{});
                                if (item_json.contains("transactionId")) {
                                    item.transaction_id = item_json["transactionId"];
                                }
                                if (item_json.contains("traceId")) {
                                    item.trace_id = item_json["traceId"];
                                }
                                items.push_back(std::move(item));
                            }
                            
                            // Execute database operation (blocks in ThreadPool worker, not uWebSockets thread)
                            spdlog::info("[Worker {}] ASYNC: Calling push_messages in ThreadPool", worker_id);
                            auto results = queue_manager->push_messages(items);
                            spdlog::info("[Worker {}] ASYNC: push_messages completed, using Loop::defer", worker_id);
                            
                            // Use Loop::defer to safely send response back to uWebSockets thread
                            current_loop->defer([res, results, worker_id, aborted]() {
                                // CRITICAL: Check if response was aborted before accessing it
                                if (aborted->load()) {
                                    spdlog::warn("[Worker {}] ASYNC: Response aborted, skipping response", worker_id);
                                    return;
                                }
                                
                                spdlog::info("[Worker {}] ASYNC: Sending response via Loop::defer", worker_id);
                                
                                // Convert PushResult vector to JSON manually
                                nlohmann::json json_results = nlohmann::json::array();
                                for (const auto& result : results) {
                                    nlohmann::json json_result;
                                    json_result["transaction_id"] = result.transaction_id;
                                    json_result["status"] = result.status;
                                    if (result.message_id.has_value()) {
                                        json_result["message_id"] = result.message_id.value();
                                    }
                                    if (result.trace_id.has_value()) {
                                        json_result["trace_id"] = result.trace_id.value();
                                    }
                                    if (result.error.has_value()) {
                                        json_result["error"] = result.error.value();
                                    }
                                    json_results.push_back(json_result);
                                }
                                
                                send_json_response(res, json_results, 201);
                            });
                            
                        } catch (const std::exception& e) {
                            // Error handling - also use Loop::defer with abort check
                            current_loop->defer([res, error = std::string(e.what()), worker_id, aborted]() {
                                // CRITICAL: Check if response was aborted before accessing it
                                if (aborted->load()) {
                                    spdlog::warn("[Worker {}] ASYNC: Response aborted during error handling, skipping", worker_id);
                                    return;
                                }
                                
                                spdlog::error("[Worker {}] ASYNC: Error via Loop::defer: {}", worker_id, error);
                                send_error_response(res, error, 500);
                            });
                        }
                    });
                    
                    spdlog::info("[Worker {}] ASYNC: Work submitted to ThreadPool, uWebSockets thread continues!", worker_id);
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // POP from queue/partition (ASYNC - non-blocking!)
    app->get("/api/v1/pop/queue/:queue/partition/:partition", [queue_manager, config, worker_id](auto* res, auto* req) {
        auto aborted = std::make_shared<bool>(false);
        res->onAborted([aborted]() { *aborted = true; });
        
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string partition_name = std::string(req->getParameter(1));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
            
            auto pool_stats = queue_manager->get_pool_stats();
            spdlog::info("[Worker {}] SPOP: [{}/{}] batch={}, wait={} | Pool: {}/{} conn ({} in use)", 
                        worker_id, queue_name, partition_name, batch, wait,
                        pool_stats.available, pool_stats.total, pool_stats.in_use);
            
            auto start_time = std::chrono::steady_clock::now();
            
            // Try once immediately (non-blocking)
            PopOptions options;
            options.wait = false;
            options.timeout = 0;
            options.batch = batch;
            options.auto_ack = get_query_param_bool(req, "autoAck", false);
            
            // Parse subscription mode
            std::string sub_mode = get_query_param(req, "subscriptionMode", "");
            if (!sub_mode.empty()) {
                options.subscription_mode = sub_mode;
            }
            std::string sub_from = get_query_param(req, "subscriptionFrom", "");
            if (!sub_from.empty()) {
                options.subscription_from = sub_from;
            }
            
            auto result = queue_manager->pop_messages(queue_name, partition_name, consumer_group, options);
            
            if (!result.messages.empty()) {
                // Found messages immediately!
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                auto pool_stats_end = queue_manager->get_pool_stats();
                spdlog::info("[Worker {}] EPOP: [{}/{}] {} msgs, {}ms | Pool: {}/{} conn ({} in use)", 
                            worker_id, queue_name, partition_name, result.messages.size(), duration_ms,
                            pool_stats_end.available, pool_stats_end.total, pool_stats_end.in_use);
                
                if (*aborted) return;
                
                nlohmann::json response = {{"messages", nlohmann::json::array()}};
                
                for (const auto& msg : result.messages) {
                    auto time_t = std::chrono::system_clock::to_time_t(msg.created_at);
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        msg.created_at.time_since_epoch()) % 1000;
                    
                    std::stringstream created_at_ss;
                    created_at_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
                    created_at_ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
                    
                    nlohmann::json msg_json = {
                        {"id", msg.id},
                        {"transactionId", msg.transaction_id},
                        {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                        {"queue", msg.queue_name},
                        {"partition", msg.partition_name},
                        {"data", msg.payload},
                        {"retryCount", msg.retry_count},
                        {"priority", msg.priority},
                        {"createdAt", created_at_ss.str()},
                        {"consumerGroup", nlohmann::json(nullptr)},
                        {"leaseId", result.lease_id.has_value() ? nlohmann::json(*result.lease_id) : nlohmann::json(nullptr)}
                    };
                    response["messages"].push_back(msg_json);
                }
                
                send_json_response(res, response);
                return;
            }
            
            // No messages found
            if (!wait) {
                // Not waiting, return empty immediately
                auto pool_stats_end = queue_manager->get_pool_stats();
                spdlog::info("[Worker {}] EPOP: [{}/{}] 0 msgs | Pool: {}/{} conn ({} in use)", 
                            worker_id, queue_name, partition_name,
                            pool_stats_end.available, pool_stats_end.total, pool_stats_end.in_use);
                
                if (!*aborted) {
                    setup_cors_headers(res);
                    res->writeStatus("204");
                    res->end();
                }
                return;
            }
            
            // Start async polling (non-blocking!)
            auto* state = new AsyncPopState{
                res,
                queue_manager,
                queue_name,
                partition_name,
                consumer_group,
                batch,
                worker_id,
                start_time + std::chrono::milliseconds(timeout_ms),  // deadline
                start_time,
                aborted,
                uWS::Loop::get(),
                nullptr,
                0,   // retry_count
                100  // current_interval_ms - start with 100ms
            };
            
            // Create timer with exponential backoff (100ms -> 200ms -> 400ms -> ... -> 2000ms max)
            state->timer = us_create_timer((struct us_loop_t*)uWS::Loop::get(), 0, sizeof(AsyncPopState*));
            *(AsyncPopState**)us_timer_ext(state->timer) = state;
            
            // Set initial timer to fire at 100ms
            us_timer_set(state->timer, pop_retry_timer, 100, 100);
            
        } catch (const std::exception& e) {
            if (!*aborted) {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // ASYNC ACK batch
    app->post("/api/v1/ack/batch", [queue_manager, worker_id, db_thread_pool](auto* res, auto* req) {
        // CRITICAL: Create safe response context to prevent segfaults
        auto safe_response = std::make_shared<SafeResponseContext>(res);
        
        read_json_body(res,
            [safe_response, queue_manager, worker_id, db_thread_pool](const nlohmann::json& body) {
                try {
                    if (!body.contains("acknowledgments") || !body["acknowledgments"].is_array()) {
                        safe_response->safe_send_error("acknowledgments array is required", 400);
                        return;
                    }
                    
                    auto acknowledgments = body["acknowledgments"];
                    std::string consumer_group = "__QUEUE_MODE__";
                    
                    if (body.contains("consumerGroup") && !body["consumerGroup"].is_null() && body["consumerGroup"].is_string()) {
                        consumer_group = body["consumerGroup"];
                    }
                    
                    std::vector<QueueManager::AckItem> ack_items;
                    for (const auto& ack_json : acknowledgments) {
                        QueueManager::AckItem ack;
                        
                        if (!ack_json.contains("transactionId") || ack_json["transactionId"].is_null() || !ack_json["transactionId"].is_string()) {
                            safe_response->safe_send_error("Each acknowledgment must have a valid transactionId string", 400);
                            return;
                        }
                        ack.transaction_id = ack_json["transactionId"];
                        
                        ack.status = "completed";
                        if (ack_json.contains("status") && !ack_json["status"].is_null() && ack_json["status"].is_string()) {
                            ack.status = ack_json["status"];
                        }
                        
                        if (ack_json.contains("error") && !ack_json["error"].is_null() && ack_json["error"].is_string()) {
                            ack.error = ack_json["error"];
                        }
                        
                        ack_items.push_back(ack);
                    }
                    
                    // Get uWebSockets loop for response handling
                    uWS::Loop* current_loop = uWS::Loop::get();
                    
                    spdlog::info("[Worker {}] ASYNC ACK BATCH: Submitting work to ThreadPool", worker_id);
                    
                    // Execute batch ACK operation in ThreadPool with safe response
                    db_thread_pool->push([safe_response, queue_manager, worker_id, current_loop, ack_items, consumer_group]() {
                        spdlog::info("[Worker {}] ASYNC ACK BATCH: ThreadPool worker executing batch ACK operation", worker_id);
                        
                        try {
                            auto ack_start = std::chrono::steady_clock::now();
                            auto results = queue_manager->acknowledge_messages(ack_items, consumer_group);
                            auto ack_end = std::chrono::steady_clock::now();
                            auto ack_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ack_end - ack_start).count();
                            
                            spdlog::info("[Worker {}] ASYNC ACK BATCH: {} items, {}ms", 
                                        worker_id, results.size(), ack_duration_ms);
                            
                            // Use safe_defer to atomically check validity and call Loop::defer
                            safe_response->safe_defer(current_loop, [safe_response, results, worker_id]() {
                                spdlog::info("[Worker {}] ASYNC ACK BATCH: Sending response via safe Loop::defer", worker_id);
                                
                                nlohmann::json response = {
                                    {"processed", results.size()},
                                    {"results", nlohmann::json::array()}
                                };
                                
                                for (const auto& ack_result : results) {
                                    nlohmann::json result_item = {
                                        {"transactionId", ack_result.transaction_id},
                                        {"status", ack_result.status}
                                    };
                                    response["results"].push_back(result_item);
                                }
                                
                                safe_response->safe_send_json(response, 200);
                            });
                            
                        } catch (const std::exception& e) {
                            // Error handling via safe_defer
                            safe_response->safe_defer(current_loop, [safe_response, error = std::string(e.what()), worker_id]() {
                                spdlog::error("[Worker {}] ASYNC ACK BATCH: Error via safe Loop::defer: {}", worker_id, error);
                                safe_response->safe_send_error(error, 500);
                            });
                        }
                    });
                    
                    spdlog::info("[Worker {}] ASYNC ACK BATCH: Work submitted to ThreadPool, uWebSockets thread continues!", worker_id);
                } catch (const std::exception& e) {
                    safe_response->safe_send_error(e.what(), 500);
                }
            },
            [safe_response](const std::string& error) {
                safe_response->safe_send_error(error, 400);
            }
        );
    });
    
    // Delete queue
    app->del("/api/v1/resources/queues/:queue", [queue_manager, worker_id](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            
            spdlog::info("[Worker {}] DELETE queue: {}", worker_id, queue_name);
            bool deleted = queue_manager->delete_queue(queue_name);
            
            nlohmann::json response = {
                {"deleted", true},
                {"queue", queue_name},
                {"existed", deleted}
            };
            
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // POP from queue (any partition)
    app->get("/api/v1/pop/queue/:queue", [queue_manager, config, worker_id](auto* res, auto* req) {
        auto aborted = std::make_shared<bool>(false);
        res->onAborted([aborted]() { *aborted = true; });
        
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
            
            auto pool_stats = queue_manager->get_pool_stats();
            spdlog::info("[Worker {}] SPOP: [{}/*] batch={}, wait={} | Pool: {}/{} conn ({} in use)", 
                        worker_id, queue_name, batch, wait,
                        pool_stats.available, pool_stats.total, pool_stats.in_use);
            
            auto start_time = std::chrono::steady_clock::now();
            
            // Try once immediately (non-blocking)
            PopOptions options;
            options.wait = false;
            options.timeout = 0;
            options.batch = batch;
            options.auto_ack = get_query_param_bool(req, "autoAck", false);
            
            // Parse subscription mode
            std::string sub_mode = get_query_param(req, "subscriptionMode", "");
            if (!sub_mode.empty()) {
                options.subscription_mode = sub_mode;
            }
            std::string sub_from = get_query_param(req, "subscriptionFrom", "");
            if (!sub_from.empty()) {
                options.subscription_from = sub_from;
            }
            
            auto result = queue_manager->pop_messages(queue_name, std::nullopt, consumer_group, options);
            
            if (!result.messages.empty()) {
                // Found messages immediately!
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                auto pool_stats_end = queue_manager->get_pool_stats();
                spdlog::info("[Worker {}] EPOP: [{}/*] {} msgs, {}ms | Pool: {}/{} conn ({} in use)", 
                            worker_id, queue_name, result.messages.size(), duration_ms,
                            pool_stats_end.available, pool_stats_end.total, pool_stats_end.in_use);
                
                if (*aborted) return;
                
                nlohmann::json response = {{"messages", nlohmann::json::array()}};
                
                for (const auto& msg : result.messages) {
                    auto time_t = std::chrono::system_clock::to_time_t(msg.created_at);
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        msg.created_at.time_since_epoch()) % 1000;
                    
                    std::stringstream created_at_ss;
                    created_at_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
                    created_at_ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
                    
                    nlohmann::json msg_json = {
                        {"id", msg.id},
                        {"transactionId", msg.transaction_id},
                        {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                        {"queue", msg.queue_name},
                        {"partition", msg.partition_name},
                        {"data", msg.payload},
                        {"retryCount", msg.retry_count},
                        {"priority", msg.priority},
                        {"createdAt", created_at_ss.str()},
                        {"consumerGroup", nlohmann::json(nullptr)},
                        {"leaseId", result.lease_id.has_value() ? nlohmann::json(*result.lease_id) : nlohmann::json(nullptr)}
                    };
                    response["messages"].push_back(msg_json);
                }
                
                send_json_response(res, response);
                return;
            }
            
            // No messages found
            if (!wait) {
                // Not waiting, return empty immediately
                auto pool_stats_end = queue_manager->get_pool_stats();
                spdlog::info("[Worker {}] EPOP: [{}/*] 0 msgs | Pool: {}/{} conn ({} in use)", 
                            worker_id, queue_name,
                            pool_stats_end.available, pool_stats_end.total, pool_stats_end.in_use);
                
                if (!*aborted) {
                    setup_cors_headers(res);
                    res->writeStatus("204");
                    res->end();
                }
                return;
            }
            
            // Start async polling (non-blocking!)
            auto* state = new AsyncPopState{
                res,
                queue_manager,
                queue_name,
                "",  // Empty partition name means "any partition"
                consumer_group,
                batch,
                worker_id,
                start_time + std::chrono::milliseconds(timeout_ms),  // deadline
                start_time,
                aborted,
                uWS::Loop::get(),
                nullptr,
                0,   // retry_count
                100  // current_interval_ms - start with 100ms
            };
            
            // Create timer with exponential backoff (100ms -> 200ms -> 400ms -> ... -> 2000ms max)
            state->timer = us_create_timer((struct us_loop_t*)uWS::Loop::get(), 0, sizeof(AsyncPopState*));
            *(AsyncPopState**)us_timer_ext(state->timer) = state;
            
            // Set initial timer to fire at 100ms
            us_timer_set(state->timer, pop_retry_timer, 100, 100);
            
        } catch (const std::exception& e) {
            if (!*aborted) {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // ASYNC Single ACK
    app->post("/api/v1/ack", [queue_manager, worker_id, db_thread_pool](auto* res, auto* req) {
        // CRITICAL: Add abort detection to prevent segfaults
        auto aborted = std::make_shared<std::atomic<bool>>(false);
        res->onAborted([aborted]() {
            aborted->store(true);
            spdlog::warn("ACK request aborted by client");
        });
        
        read_json_body(res,
            [res, queue_manager, worker_id, db_thread_pool, aborted](const nlohmann::json& body) {
                try {
                    std::string transaction_id = "";
                    if (body.contains("transactionId") && !body["transactionId"].is_null() && body["transactionId"].is_string()) {
                        transaction_id = body["transactionId"];
                    } else {
                        send_error_response(res, "transactionId is required", 400);
                        return;
                    }
                    
                    std::string status = "completed";
                    if (body.contains("status") && !body["status"].is_null() && body["status"].is_string()) {
                        status = body["status"];
                    }
                    
                    std::string consumer_group = "__QUEUE_MODE__";
                    if (body.contains("consumerGroup") && !body["consumerGroup"].is_null() && body["consumerGroup"].is_string()) {
                        consumer_group = body["consumerGroup"];
                    }
                    
                    std::optional<std::string> error;
                    if (body.contains("error") && !body["error"].is_null() && body["error"].is_string()) {
                        error = body["error"];
                    }
                    
                    std::optional<std::string> lease_id;
                    if (body.contains("leaseId") && !body["leaseId"].is_null() && body["leaseId"].is_string()) {
                        lease_id = body["leaseId"];
                    }
                    
                    // Get uWebSockets loop for response handling
                    uWS::Loop* current_loop = uWS::Loop::get();
                    
                    spdlog::info("[Worker {}] ASYNC ACK: Submitting work to ThreadPool", worker_id);
                    
                    // Execute ACK operation in ThreadPool with abort detection
                    db_thread_pool->push([res, queue_manager, worker_id, current_loop, transaction_id, status, error, consumer_group, lease_id, aborted]() {
                        spdlog::info("[Worker {}] ASYNC ACK: ThreadPool worker executing ACK operation", worker_id);
                        
                        try {
                            auto ack_start = std::chrono::steady_clock::now();
                            auto ack_result = queue_manager->acknowledge_message(transaction_id, status, error, consumer_group, lease_id);
                            auto ack_end = std::chrono::steady_clock::now();
                            auto ack_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ack_end - ack_start).count();
                            
                            spdlog::debug("[Worker {}] ASYNC ACK: 1 item, {}ms", worker_id, ack_duration_ms);
                            
                            // Use Loop::defer to safely send response back to uWebSockets thread
                            current_loop->defer([res, ack_result, transaction_id, consumer_group, worker_id, aborted]() {
                                // CRITICAL: Check if response was aborted before accessing it
                                if (aborted->load()) {
                                    spdlog::warn("[Worker {}] ASYNC ACK: Response aborted, skipping response", worker_id);
                                    return;
                                }
                                
                                spdlog::info("[Worker {}] ASYNC ACK: Sending response via Loop::defer", worker_id);
                                
                                if (ack_result.success) {
                                    auto now = std::chrono::system_clock::now();
                                    auto time_t = std::chrono::system_clock::to_time_t(now);
                                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        now.time_since_epoch()) % 1000;
                                    
                                    std::stringstream ss;
                                    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
                                    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
                                    
                                    nlohmann::json response = {
                                        {"transactionId", transaction_id},
                                        {"status", ack_result.status},
                                        {"consumerGroup", consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(consumer_group)},
                                        {"acknowledgedAt", ss.str()}
                                    };
                                    send_json_response(res, response);
                                } else {
                                    send_error_response(res, "Failed to acknowledge message: " + ack_result.status, 500);
                                }
                            });
                            
                        } catch (const std::exception& e) {
                            // Error handling via Loop::defer with abort check
                            current_loop->defer([res, error = std::string(e.what()), worker_id, aborted]() {
                                // CRITICAL: Check if response was aborted before accessing it
                                if (aborted->load()) {
                                    spdlog::warn("[Worker {}] ASYNC ACK: Response aborted during error handling, skipping", worker_id);
                                    return;
                                }
                                
                                spdlog::error("[Worker {}] ASYNC ACK: Error via Loop::defer: {}", worker_id, error);
                                send_error_response(res, error, 500);
                            });
                        }
                    });
                    
                    spdlog::info("[Worker {}] ASYNC ACK: Work submitted to ThreadPool, uWebSockets thread continues!", worker_id);
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // ASYNC POP with namespace/task filtering (no specific queue)
    app->get("/api/v1/pop", [queue_manager, config, worker_id, db_thread_pool](auto* res, auto* req) {
        // CRITICAL: Create safe response context to prevent segfaults
        auto safe_response = std::make_shared<SafeResponseContext>(res);
        
        try {
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            std::string namespace_param = get_query_param(req, "namespace", "");
            std::string task_param = get_query_param(req, "task", "");
            
            std::optional<std::string> namespace_name = namespace_param.empty() ? std::nullopt : std::optional<std::string>(namespace_param);
            std::optional<std::string> task_name = task_param.empty() ? std::nullopt : std::optional<std::string>(task_param);
            
            PopOptions options;
            options.wait = get_query_param_bool(req, "wait", false);
            options.timeout = get_query_param_int(req, "timeout", config.queue.default_timeout);
            options.batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
            options.auto_ack = get_query_param_bool(req, "autoAck", false);
            
            std::string sub_mode = get_query_param(req, "subscriptionMode", "");
            if (!sub_mode.empty()) options.subscription_mode = sub_mode;
            std::string sub_from = get_query_param(req, "subscriptionFrom", "");
            if (!sub_from.empty()) options.subscription_from = sub_from;
            
            // Get uWebSockets loop for response handling
            uWS::Loop* current_loop = uWS::Loop::get();
            
            spdlog::info("[Worker {}] ASYNC POP: Submitting work to ThreadPool", worker_id);
            
            // Execute POP operation in ThreadPool with safe response
            db_thread_pool->push([safe_response, queue_manager, worker_id, current_loop, namespace_name, task_name, consumer_group, options]() {
                spdlog::info("[Worker {}] ASYNC POP: ThreadPool worker executing pop operation", worker_id);
                
                try {
                    auto result = queue_manager->pop_with_namespace_task(namespace_name, task_name, consumer_group, options);
                    
                    // Use safe_defer to atomically check validity and call Loop::defer
                    safe_response->safe_defer(current_loop, [safe_response, result, worker_id]() {
                        spdlog::info("[Worker {}] ASYNC POP: Sending response via safe Loop::defer", worker_id);
                        
                        if (result.messages.empty()) {
                            safe_response->safe_send_empty(204);
                            return;
                        }
                        
                        nlohmann::json response = {{"messages", nlohmann::json::array()}};
                        for (const auto& msg : result.messages) {
                            auto time_t = std::chrono::system_clock::to_time_t(msg.created_at);
                            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                msg.created_at.time_since_epoch()) % 1000;
                            
                            std::stringstream created_at_ss;
                            created_at_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
                            created_at_ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
                            
                            nlohmann::json msg_json = {
                                {"id", msg.id},
                                {"transactionId", msg.transaction_id},
                                {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                                {"queue", msg.queue_name},
                                {"partition", msg.partition_name},
                                {"data", msg.payload},
                                {"retryCount", msg.retry_count},
                                {"priority", msg.priority},
                                {"createdAt", created_at_ss.str()},
                                {"consumerGroup", nlohmann::json(nullptr)},
                                {"leaseId", result.lease_id.has_value() ? nlohmann::json(*result.lease_id) : nlohmann::json(nullptr)}
                            };
                            response["messages"].push_back(msg_json);
                        }
                        
                        safe_response->safe_send_json(response, 200);
                    });
                    
                } catch (const std::exception& e) {
                    // Error handling via safe_defer
                    safe_response->safe_defer(current_loop, [safe_response, error = std::string(e.what()), worker_id]() {
                        spdlog::error("[Worker {}] ASYNC POP: Error via safe Loop::defer: {}", worker_id, error);
                        safe_response->safe_send_error(error, 500);
                    });
                }
            });
            
            spdlog::info("[Worker {}] ASYNC POP: Work submitted to ThreadPool, uWebSockets thread continues!", worker_id);
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // ASYNC Transaction API (atomic operations)
    app->post("/api/v1/transaction", [queue_manager, worker_id, db_thread_pool](auto* res, auto* req) {
        // CRITICAL: Add abort detection to prevent segfaults
        auto aborted = std::make_shared<std::atomic<bool>>(false);
        res->onAborted([aborted]() {
            aborted->store(true);
            spdlog::warn("TRANSACTION request aborted by client");
        });
        
        read_json_body(res,
            [res, queue_manager, worker_id, db_thread_pool, aborted](const nlohmann::json& body) {
                try {
                    if (!body.contains("operations") || !body["operations"].is_array()) {
                        send_error_response(res, "operations array required", 400);
                        return;
                    }
                    
                    auto operations = body["operations"];
                    if (operations.empty()) {
                        send_error_response(res, "operations array cannot be empty", 400);
                        return;
                    }
                    
                    // Get uWebSockets loop for response handling
                    uWS::Loop* current_loop = uWS::Loop::get();
                    
                    spdlog::info("[Worker {}] ASYNC TRANSACTION: Submitting work to ThreadPool", worker_id);
                    
                    // Execute entire transaction in ThreadPool with abort detection
                    db_thread_pool->push([res, queue_manager, worker_id, current_loop, operations, aborted]() {
                        spdlog::info("[Worker {}] ASYNC TRANSACTION: ThreadPool worker executing transaction operations", worker_id);
                        
                        try {
                            // Execute operations sequentially
                            nlohmann::json results = nlohmann::json::array();
                    
                            for (const auto& op : operations) {
                                if (!op.contains("type")) {
                                    // Handle missing type via Loop::defer with abort check
                                    current_loop->defer([res, worker_id, aborted]() {
                                        if (aborted->load()) {
                                            spdlog::warn("[Worker {}] ASYNC TRANSACTION: Response aborted during validation, skipping", worker_id);
                                            return;
                                        }
                                        spdlog::error("[Worker {}] ASYNC TRANSACTION: Missing operation type", worker_id);
                                        send_error_response(res, "operation type required", 400);
                                    });
                                    return;
                                }
                        
                        std::string op_type = op["type"];
                        
                        if (op_type == "push") {
                            // Push operation
                            auto items_json = op["items"];
                            std::vector<PushItem> items;
                            
                            for (const auto& item_json : items_json) {
                                PushItem item;
                                item.queue = item_json["queue"];
                                item.partition = item_json.value("partition", "Default");
                                item.payload = item_json.value("payload", nlohmann::json{});
                                items.push_back(item);
                            }
                            
                            auto push_results = queue_manager->push_messages(items);
                            nlohmann::json push_result_json;
                            push_result_json["type"] = "push";
                            push_result_json["count"] = push_results.size();
                            results.push_back(push_result_json);
                            
                        } else if (op_type == "ack") {
                            // ACK operation
                            std::string transaction_id = op["transactionId"];
                            std::string status = op.value("status", "completed");
                            std::string consumer_group = op.value("consumerGroup", "__QUEUE_MODE__");
                            
                            std::optional<std::string> error;
                            if (op.contains("error") && !op["error"].is_null()) {
                                error = op["error"];
                            }
                            
                            auto ack_result = queue_manager->acknowledge_message(transaction_id, status, error, consumer_group, std::nullopt);
                            nlohmann::json ack_result_json;
                            ack_result_json["type"] = "ack";
                            ack_result_json["success"] = ack_result.success;
                            ack_result_json["transactionId"] = ack_result.transaction_id;
                            ack_result_json["status"] = ack_result.status;
                            results.push_back(ack_result_json);
                            
                        } else {
                            // Handle unknown operation type via Loop::defer with abort check
                            current_loop->defer([res, op_type, worker_id, aborted]() {
                                if (aborted->load()) {
                                    spdlog::warn("[Worker {}] ASYNC TRANSACTION: Response aborted during validation, skipping", worker_id);
                                    return;
                                }
                                spdlog::error("[Worker {}] ASYNC TRANSACTION: Unknown operation type: {}", worker_id, op_type);
                                send_error_response(res, "Unknown operation type: " + op_type, 400);
                            });
                            return;
                        }
                    }
                    
                    // Use Loop::defer to safely send response back to uWebSockets thread
                    current_loop->defer([res, results, queue_manager, worker_id, aborted]() {
                        // CRITICAL: Check if response was aborted before accessing it
                        if (aborted->load()) {
                            spdlog::warn("[Worker {}] ASYNC TRANSACTION: Response aborted, skipping response", worker_id);
                            return;
                        }
                        
                        spdlog::info("[Worker {}] ASYNC TRANSACTION: Sending response via Loop::defer", worker_id);
                        
                        nlohmann::json response = {
                            {"success", true},
                            {"results", results},
                            {"transactionId", queue_manager->generate_uuid()}
                        };
                        
                        send_json_response(res, response);
                    });
                    
                } catch (const std::exception& e) {
                    // Error handling via Loop::defer with abort check
                    current_loop->defer([res, error = std::string(e.what()), worker_id, aborted]() {
                        // CRITICAL: Check if response was aborted before accessing it
                        if (aborted->load()) {
                            spdlog::warn("[Worker {}] ASYNC TRANSACTION: Response aborted during error handling, skipping", worker_id);
                            return;
                        }
                        
                        spdlog::error("[Worker {}] ASYNC TRANSACTION: Error via Loop::defer: {}", worker_id, error);
                        send_error_response(res, error, 500);
                    });
                }
            });
            
            spdlog::info("[Worker {}] ASYNC TRANSACTION: Work submitted to ThreadPool, uWebSockets thread continues!", worker_id);
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // Lease extension
    app->post("/api/v1/lease/:leaseId/extend", [queue_manager, worker_id](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager, worker_id, req](const nlohmann::json& body) {
                try {
                    std::string lease_id = std::string(req->getParameter(0));
                    int seconds = body.value("seconds", 60);
                    
                    spdlog::debug("[Worker {}] Extending lease: {}, seconds: {}", worker_id, lease_id, seconds);
                    
                    bool success = queue_manager->extend_message_lease(lease_id, seconds);
                    
                    if (success) {
                        nlohmann::json response = {
                            {"leaseId", lease_id},
                            {"extended", true},
                            {"seconds", seconds}
                        };
                        send_json_response(res, response);
                    } else {
                        send_error_response(res, "Lease not found or expired", 404);
                    }
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // ============================================================================
    // Metrics & Resources Routes
    // ============================================================================
    
    // GET /metrics - Performance metrics
    app->get("/metrics", [analytics_manager](auto* res, auto* req) {
        try {
            auto response = analytics_manager->get_metrics();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/queues - List all queues
    app->get("/api/v1/resources/queues", [analytics_manager](auto* res, auto* req) {
        try {
            auto response = analytics_manager->get_queues();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/queues/:queue - Get single queue detail
    app->get("/api/v1/resources/queues/:queue", [analytics_manager](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            auto response = analytics_manager->get_queue(queue_name);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not found") != std::string::npos) {
                send_error_response(res, e.what(), 404);
            } else {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // GET /api/v1/resources/namespaces - List all namespaces
    app->get("/api/v1/resources/namespaces", [analytics_manager](auto* res, auto* req) {
        try {
            auto response = analytics_manager->get_namespaces();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/tasks - List all tasks
    app->get("/api/v1/resources/tasks", [analytics_manager](auto* res, auto* req) {
        try {
            auto response = analytics_manager->get_tasks();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/overview - System overview
    app->get("/api/v1/resources/overview", [analytics_manager](auto* res, auto* req) {
        try {
            auto response = analytics_manager->get_system_overview();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/messages - List messages with filters
    app->get("/api/v1/messages", [analytics_manager](auto* res, auto* req) {
        try {
            AnalyticsManager::MessageFilters filters;
            filters.queue = get_query_param(req, "queue");
            filters.partition = get_query_param(req, "partition");
            filters.namespace_name = get_query_param(req, "ns");
            filters.task = get_query_param(req, "task");
            filters.status = get_query_param(req, "status");
            filters.limit = get_query_param_int(req, "limit", 50);
            filters.offset = get_query_param_int(req, "offset", 0);
            
            auto messages = analytics_manager->list_messages(filters);
            send_json_response(res, messages);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/messages/:transactionId - Get single message detail
    app->get("/api/v1/messages/:transactionId", [analytics_manager](auto* res, auto* req) {
        try {
            std::string transaction_id = std::string(req->getParameter(0));
            auto message = analytics_manager->get_message(transaction_id);
            send_json_response(res, message);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not found") != std::string::npos) {
                send_error_response(res, e.what(), 404);
            } else {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // ============================================================================
    // Status/Dashboard Routes
    // ============================================================================
    
    // GET /api/v1/status - Dashboard overview
    app->get("/api/v1/status", [analytics_manager](auto* res, auto* req) {
        try {
            AnalyticsManager::StatusFilters filters;
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.queue = get_query_param(req, "queue");
            filters.namespace_name = get_query_param(req, "namespace");
            filters.task = get_query_param(req, "task");
            
            auto response = analytics_manager->get_status(filters);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/queues - Queues list
    app->get("/api/v1/status/queues", [analytics_manager](auto* res, auto* req) {
        try {
            AnalyticsManager::StatusFilters filters;
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.namespace_name = get_query_param(req, "namespace");
            filters.task = get_query_param(req, "task");
            
            int limit = get_query_param_int(req, "limit", 100);
            int offset = get_query_param_int(req, "offset", 0);
            
            auto response = analytics_manager->get_status_queues(filters, limit, offset);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/queues/:queue - Queue detail
    app->get("/api/v1/status/queues/:queue", [analytics_manager](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            auto response = analytics_manager->get_queue_detail(queue_name);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not found") != std::string::npos) {
                send_error_response(res, e.what(), 404);
            } else {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // GET /api/v1/status/queues/:queue/messages - Queue messages
    app->get("/api/v1/status/queues/:queue/messages", [analytics_manager](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            int limit = get_query_param_int(req, "limit", 50);
            int offset = get_query_param_int(req, "offset", 0);
            
            auto response = analytics_manager->get_queue_messages(queue_name, limit, offset);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/analytics - Analytics
    app->get("/api/v1/status/analytics", [analytics_manager](auto* res, auto* req) {
        try {
            AnalyticsManager::AnalyticsFilters filters;
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.interval = get_query_param(req, "interval", "hour");
            filters.queue = get_query_param(req, "queue");
            filters.namespace_name = get_query_param(req, "namespace");
            filters.task = get_query_param(req, "task");
            
            auto response = analytics_manager->get_analytics(filters);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/buffers - File buffer stats
    app->get("/api/v1/status/buffers", [file_buffer, worker_id](auto* res, auto* req) {
        try {
            if (file_buffer) {
                nlohmann::json response = {
                    {"pending", file_buffer->get_pending_count()},
                    {"failed", file_buffer->get_failed_count()},
                    {"dbHealthy", file_buffer->is_db_healthy()},
                    {"worker", worker_id}
                };
                send_json_response(res, response);
            } else {
                // Non-zero worker - no file buffer
                nlohmann::json response = {
                    {"pending", 0},
                    {"failed", 0},
                    {"dbHealthy", true},
                    {"worker", worker_id},
                    {"note", "File buffer only available on Worker 0"}
                };
                send_json_response(res, response);
            }
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // ============================================================================
    // Static File Serving (Frontend Dashboard)
    // ============================================================================
    
    // Define webapp path - check multiple locations
    std::string webapp_root;
    
    // 1. Check environment variable first
    const char* webapp_env = std::getenv("WEBAPP_ROOT");
    if (webapp_env) {
        webapp_root = webapp_env;
    }
    // 2. Check Docker/production location
    else if (std::filesystem::exists("./webapp/dist")) {
        webapp_root = "./webapp/dist";
    }
    // 3. Check development location (running from server/bin/)
    else if (std::filesystem::exists("../webapp/dist")) {
        webapp_root = "../webapp/dist";
    }
    // 4. Check alternative development location
    else if (std::filesystem::exists("../../webapp/dist")) {
        webapp_root = "../../webapp/dist";
    }
    
    // Check if webapp directory exists
    if (!webapp_root.empty() && std::filesystem::exists(webapp_root)) {
        spdlog::info("[Worker {}] Static file serving enabled from: {}", worker_id, 
                    std::filesystem::absolute(webapp_root).string());
        
        // Route 1: GET /assets/* - Serve static assets (JS, CSS, images, fonts)
        app->get("/assets/*", [webapp_root](auto* res, auto* req) {
            auto aborted = std::make_shared<bool>(false);
            res->onAborted([aborted]() { *aborted = true; });
            
            if (*aborted) return;
            
            std::string url = std::string(req->getUrl());
            std::string file_path = webapp_root + url;
            
            bool served = serve_static_file(res, file_path, webapp_root);
            if (!served && !*aborted) {
                res->writeStatus("404");
                res->end("Not Found");
            }
        });
        
        // Route 2: GET / - Serve root index.html
        app->get("/", [webapp_root](auto* res, auto* req) {
            auto aborted = std::make_shared<bool>(false);
            res->onAborted([aborted]() { *aborted = true; });
            
            if (*aborted) return;
            
            std::string index_path = webapp_root + "/index.html";
            serve_static_file(res, index_path, webapp_root);
        });
        
        // Route 3: GET /* - SPA fallback (for Vue Router client-side routing)
        // MUST be registered LAST to not override API routes
        app->get("/*", [webapp_root](auto* res, auto* req) {
            auto aborted = std::make_shared<bool>(false);
            res->onAborted([aborted]() { *aborted = true; });
            
            if (*aborted) return;
            
            std::string url = std::string(req->getUrl());
            
            // Don't serve index.html for API routes or special endpoints
            if (url.find("/api/") == 0 || 
                url.find("/health") == 0 || 
                url.find("/metrics") == 0 ||
                url.find("/ws/") == 0) {
                res->writeStatus("404");
                res->end("Not Found");
                return;
            }
            
            // Try to serve as a direct file first (e.g., favicon.ico)
            std::string file_path = webapp_root + url;
            bool served = serve_static_file(res, file_path, webapp_root);
            
            // If file doesn't exist, serve index.html for SPA routing
            if (!served && !*aborted) {
                std::string index_path = webapp_root + "/index.html";
                serve_static_file(res, index_path, webapp_root);
            }
        });
        
    } else {
        spdlog::warn("[Worker {}] Webapp directory not found - Static file serving disabled", worker_id);
        spdlog::warn("[Worker {}] Searched: ./webapp/dist, ../webapp/dist, ../../webapp/dist", worker_id);
        spdlog::warn("[Worker {}] Set WEBAPP_ROOT environment variable to override", worker_id);
    }
}

// Global shared resources for all workers
static std::shared_ptr<astp::ThreadPool> global_db_thread_pool;
static std::shared_ptr<DatabasePool> global_db_pool;
static std::once_flag global_pool_init_flag;

// Worker thread function
static void worker_thread(const Config& config, int worker_id, int num_workers,
                         std::mutex& init_mutex,
                         std::vector<uWS::App*>& worker_apps,
                         std::shared_ptr<FileBufferManager>& shared_file_buffer,
                         std::mutex& file_buffer_mutex,
                         std::condition_variable& file_buffer_ready) {
    spdlog::info("[Worker {}] Starting...", worker_id);
    
    try {
        // Initialize global shared resources (only once)
        std::call_once(global_pool_init_flag, [&config]() {
            // Calculate global pool sizes with 5% safety buffer
            int total_connections = static_cast<int>(config.database.pool_size * 0.95); // 95% of total
            int total_threads = total_connections; // 1:1 ratio
            
            spdlog::info("Initializing GLOBAL shared resources:");
            spdlog::info("  - Total DB connections: {} (95% of {})", total_connections, config.database.pool_size);
            spdlog::info("  - Total ThreadPool threads: {}", total_threads);
            
            // Create global connection pool
            global_db_pool = std::make_shared<DatabasePool>(
                config.database.connection_string(),
                total_connections,
                config.database.pool_acquisition_timeout,
                config.database.statement_timeout,
                config.database.lock_timeout,
                config.database.idle_timeout
            );
            
            // Create global ThreadPool
            global_db_thread_pool = std::make_shared<astp::ThreadPool>(total_threads);
            
            spdlog::info("Global shared resources initialized successfully");
        });
        
        // Use global shared resources
        auto db_pool = global_db_pool;
        auto db_thread_pool = global_db_thread_pool;
        
        // Thread-local queue manager (uses shared pool)
        auto queue_manager = std::make_shared<QueueManager>(db_pool, config.queue);
        
        // Thread-local analytics manager (uses shared pool)
        auto analytics_manager = std::make_shared<AnalyticsManager>(db_pool);
        
        spdlog::info("[Worker {}] Using GLOBAL shared ThreadPool and DatabasePool", worker_id);
        
        // Test database connection and log pool stats
        // FAILOVER: Don't fail at startup if DB is down - use file buffer instead
        bool db_available = queue_manager->health_check();
        
        auto pool_stats = queue_manager->get_pool_stats();
        
        if (db_available) {
            spdlog::info("[Worker {}] Database connection: OK | Pool: {}/{} conn available", 
                         worker_id, pool_stats.available, pool_stats.total);
            
            // Only first worker initializes schema (only if DB is available)
            if (worker_id == 0) {
                spdlog::info("[Worker 0] Initializing database schema...");
                queue_manager->initialize_schema();
            }
        } else {
            spdlog::warn("[Worker {}] Database connection: UNAVAILABLE (Pool: 0/{}) - Will use file buffer for failover", 
                         worker_id, pool_stats.total);
            spdlog::warn("[Worker {}] Server will operate with file buffer until PostgreSQL becomes available", worker_id);
        }
        
        // Create or wait for SHARED FileBufferManager
        // Worker 0 creates it, other workers wait for it to be ready
        std::shared_ptr<FileBufferManager> file_buffer;
        
        if (worker_id == 0) {
            spdlog::info("[Worker 0] Creating SHARED file buffer manager (dir={})...", 
                         config.file_buffer.buffer_dir);
            
            auto new_file_buffer = std::make_shared<FileBufferManager>(
                queue_manager,
                config.file_buffer.buffer_dir,
                config.file_buffer.flush_interval_ms,
                config.file_buffer.max_batch_size,
                config.file_buffer.max_events_per_file,
                true  // Do startup recovery
            );
            
            // Wait for recovery to complete
            while (!new_file_buffer->is_ready()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            spdlog::info("[Worker 0] File buffer ready - Pending: {}, Failed: {}, DB: {}", 
                         new_file_buffer->get_pending_count(),
                         new_file_buffer->get_failed_count(),
                         new_file_buffer->is_db_healthy() ? "healthy" : "down");
            
            // Share with other workers
            spdlog::info("[Worker 0] Sharing file buffer with other workers...");
            {
                std::lock_guard<std::mutex> lock(file_buffer_mutex);
                shared_file_buffer = new_file_buffer;
                file_buffer = new_file_buffer;
            }
            file_buffer_ready.notify_all();
            spdlog::info("[Worker 0] File buffer shared successfully");
            
        } else {
            // Wait for Worker 0 to create the shared file buffer
            spdlog::info("[Worker {}] Waiting for shared file buffer from Worker 0...", worker_id);
            {
                std::unique_lock<std::mutex> lock(file_buffer_mutex);
                file_buffer_ready.wait(lock, [&shared_file_buffer]() { 
                    return shared_file_buffer != nullptr; 
                });
                file_buffer = shared_file_buffer;
            }
            spdlog::info("[Worker {}] Using shared file buffer from Worker 0", worker_id);
        }
        
        // Create worker App
        spdlog::info("[Worker {}] Creating uWS::App...", worker_id);
        auto worker_app = new uWS::App();
        
        // Setup routes
        spdlog::info("[Worker {}] Setting up routes...", worker_id);
        setup_worker_routes(worker_app, queue_manager, analytics_manager, file_buffer, config, worker_id, db_thread_pool);
        spdlog::info("[Worker {}] Routes configured", worker_id);
        
        // Register this worker app with the acceptor (thread-safe)
        {
            std::lock_guard<std::mutex> lock(init_mutex);
            worker_apps.push_back(worker_app);
            spdlog::info("[Worker {}] Registered with acceptor", worker_id);
        }
        
        // IMPORTANT: Workers do NOT listen on the main port in acceptor/worker pattern!
        // Only the acceptor listens on the main port (6632).
        // Workers listen on a dummy localhost-only port just to keep event loops alive.
        // They will receive actual sockets via adoptSocket() from the acceptor.
        int dummy_port = 50000 + worker_id;  // Each worker gets unique dummy port
        worker_app->listen("127.0.0.1", dummy_port, [worker_id, dummy_port](auto* listen_socket) {
            if (listen_socket) {
                spdlog::debug("[Worker {}] Listening on dummy port 127.0.0.1:{} (keeps event loop alive)", 
                             worker_id, dummy_port);
            }
        });
        
        spdlog::info("[Worker {}] Event loop ready to receive adopted sockets from acceptor", worker_id);
        
        // Run worker event loop (blocks forever)
        // Will receive sockets adopted from the acceptor
        worker_app->run();
        
        spdlog::info("[Worker {}] Event loop exited", worker_id);
        
    } catch (const std::exception& e) {
        spdlog::error("[Worker {}] FATAL: {}", worker_id, e.what());
    }
}

// Main server start function using acceptor/worker pattern
bool start_acceptor_server(const Config& config) {
    // Initialize encryption globally
    bool encryption_enabled = init_encryption();
    spdlog::info("Encryption: {}", encryption_enabled ? "enabled" : "disabled");
    
    // Determine number of workers
    int hardware_threads = static_cast<int>(std::thread::hardware_concurrency());
    int num_workers = config.server.num_workers;
    
    // Only cap if hardware_threads is valid AND user requested more than 2x the hardware
    // This allows users to override for containers/VMs where hardware_concurrency() may be wrong
    if (hardware_threads > 0 && num_workers > hardware_threads * 2) {
        spdlog::warn("NUM_WORKERS ({}) is more than 2x hardware concurrency ({}), this may cause performance issues", 
                     num_workers, hardware_threads);
    }
    
    spdlog::info("Starting acceptor/worker pattern with {} workers (hardware cores: {})", 
                 num_workers, hardware_threads > 0 ? hardware_threads : 0);
    
    // Shared data for worker registration
    std::mutex init_mutex;
    std::vector<uWS::App*> worker_apps;
    std::vector<std::thread> worker_threads;
    
    // Create SHARED FileBufferManager (will be initialized by Worker 0)
    std::shared_ptr<FileBufferManager> shared_file_buffer;
    std::mutex file_buffer_mutex;
    std::condition_variable file_buffer_ready;
    
    // Create worker threads
    for (int i = 0; i < num_workers; i++) {
        worker_threads.emplace_back(worker_thread, config, i, num_workers,
                                   std::ref(init_mutex), std::ref(worker_apps),
                                   std::ref(shared_file_buffer), std::ref(file_buffer_mutex),
                                   std::ref(file_buffer_ready));
    }
    
    // Wait for all workers to register
    spdlog::info("Waiting for workers to initialize...");
    auto start_wait = std::chrono::steady_clock::now();
    
    while (true) {
        {
            std::lock_guard<std::mutex> lock(init_mutex);
            if (worker_apps.size() == static_cast<size_t>(num_workers)) {
                auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_wait
                ).count();
                spdlog::info("All {} workers initialized in {}ms", num_workers, wait_time);
                break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Timeout after 3600 seconds (matches MAX_STARTUP_RECOVERY_SECONDS in file_buffer.cpp)
        // TODO: Make this configurable and improve recovery to be non-blocking
        // See README.md "Known Issues & Roadmap" section for details
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_wait
        ).count();
        if (elapsed > 3600) {
            spdlog::error("Timeout waiting for workers to initialize (3600s)");
            spdlog::error("This may indicate file buffer recovery is taking too long");
            spdlog::error("Check pending event count and database connectivity");
            break;
        }
    }
    
    // Create acceptor app
    spdlog::info("Creating acceptor app...");
    auto acceptor = new uWS::App();
    
    // Register all worker apps with acceptor
    {
        std::lock_guard<std::mutex> lock(init_mutex);
        for (auto* worker : worker_apps) {
            acceptor->addChildApp(worker);
        }
        spdlog::info("Registered {} worker apps with acceptor", worker_apps.size());
    }
    
    // Acceptor listens on port and distributes in round-robin
    acceptor->listen(config.server.host, config.server.port, [config, num_workers](auto* listen_socket) {
        if (listen_socket) {
            spdlog::info("Acceptor listening on {}:{}", config.server.host, config.server.port);
            spdlog::info("Round-robin load balancing across {} workers", num_workers);
        } else {
            spdlog::error("Failed to listen on {}:{}", config.server.host, config.server.port);
        }
    });
    
    // Run acceptor event loop
    spdlog::info("Starting acceptor event loop...");
    acceptor->run();
    
    // Wait for shutdown
    spdlog::info("Shutdown - waiting for workers...");
    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    spdlog::info("Clean shutdown");
    return true;
}

} // namespace queen

