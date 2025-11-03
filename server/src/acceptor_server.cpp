#include "queen/database.hpp"
#include "queen/queue_manager.hpp"
#include "queen/analytics_manager.hpp"
#include "queen/config.hpp"
#include "queen/encryption.hpp"
#include "queen/file_buffer.hpp"
#include "queen/response_queue.hpp"
#include "queen/metrics_collector.hpp"
#include "queen/retention_service.hpp"
#include "queen/eviction_service.hpp"
#include "queen/poll_intention_registry.hpp"
#include "queen/poll_worker.hpp"
#include "queen/stream_poll_worker.hpp"
#include "queen/stream_poll_intention_registry.hpp"
#include "queen/stream_manager.hpp"
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
#include <unistd.h>

// Global shared resources for all workers (declared early for use in handlers)
static std::shared_ptr<astp::ThreadPool> global_db_thread_pool;
static std::shared_ptr<astp::ThreadPool> global_system_thread_pool;
static std::shared_ptr<queen::DatabasePool> global_db_pool;
static std::vector<std::shared_ptr<queen::ResponseQueue>> worker_response_queues;  // Per-worker queues
static std::shared_ptr<queen::ResponseRegistry> global_response_registry;
static std::shared_ptr<queen::MetricsCollector> global_metrics_collector;
static std::shared_ptr<queen::RetentionService> global_retention_service;
static std::shared_ptr<queen::EvictionService> global_eviction_service;
static std::shared_ptr<queen::PollIntentionRegistry> global_poll_intention_registry;
static std::shared_ptr<queen::StreamPollIntentionRegistry> global_stream_poll_registry;
static std::shared_ptr<queen::StreamManager> global_stream_manager;
static us_timer_t* global_response_timer = nullptr;
static std::once_flag global_pool_init_flag;
static std::once_flag global_stream_registry_init_flag;
static int num_workers_global = 0;  // Track number of workers for queue array sizing

// System information for metrics
struct SystemInfo {
    std::string hostname;
    int port;
    
    static SystemInfo get_current() {
        SystemInfo info;
        
        // Get hostname
        char hostname_buf[256];
        if (gethostname(hostname_buf, sizeof(hostname_buf)) == 0) {
            info.hostname = hostname_buf;
        } else {
            info.hostname = "unknown";
        }
        
        // Port will be set from config
        info.port = 0;
        
        return info;
    }
};

static SystemInfo global_system_info;

namespace queen {

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
                        {"partitionId", msg.partition_id},
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
        res->writeHeader("Cache-Control", cache_control);
        res->writeStatus("200");
        res->end(content);  // uWebSockets automatically sets Content-Length
        
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
    
    // GET maintenance mode status (always fetch fresh from DB)
    app->get("/api/v1/system/maintenance", [queue_manager](auto* res, auto* req) {
        try {
            // Force fresh check from database (bypass cache for status endpoint)
            bool current_mode = queue_manager->get_maintenance_mode_fresh();
            auto buffer_stats = queue_manager->get_buffer_stats();
            
            nlohmann::json response = {
                {"maintenanceMode", current_mode},
                {"bufferedMessages", queue_manager->get_buffer_pending_count()},
                {"bufferHealthy", queue_manager->is_buffer_healthy()},
                {"bufferStats", buffer_stats}
            };
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // POST toggle maintenance mode
    app->post("/api/v1/system/maintenance", [queue_manager](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager](const nlohmann::json& body) {
                try {
                    if (!body.contains("enabled") || !body["enabled"].is_boolean()) {
                        send_error_response(res, "enabled (boolean) is required", 400);
                        return;
                    }
                    
                    bool enable = body["enabled"];
                    queue_manager->set_maintenance_mode(enable);
                    
                    nlohmann::json response = {
                        {"maintenanceMode", enable},
                        {"bufferedMessages", queue_manager->get_buffer_pending_count()},
                        {"bufferHealthy", queue_manager->is_buffer_healthy()},
                        {"message", enable ? 
                            "Maintenance mode ENABLED. All PUSHes routing to file buffer." :
                            "Maintenance mode DISABLED. Background processor will drain buffer to DB."
                        }
                    };
                    
                    send_json_response(res, response);
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
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
                        options.max_size = opts.value("maxSize", 0);  // Default: unlimited (0)
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
    
    
    // ASYNC PUSH - NEW RESPONSE QUEUE ARCHITECTURE WITH FAILOVER
    app->post("/api/v1/push", [queue_manager, file_buffer, worker_id, db_thread_pool](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager, file_buffer, worker_id, db_thread_pool](const nlohmann::json& body) {
                try {
                    if (!body.contains("items") || !body["items"].is_array()) {
                        send_error_response(res, "items array is required", 400);
                        return;
                    }
                    
                    // Register response in uWebSockets thread - SAFE
                    std::string request_id = global_response_registry->register_response(res, worker_id);
                    
                    spdlog::info("[Worker {}] PUSH: Registered response {}, submitting to ThreadPool", worker_id, request_id);
                    
                    // Execute PUSH operation in ThreadPool
                    db_thread_pool->push([request_id, queue_manager, file_buffer, worker_id, body]() {
                        spdlog::info("[Worker {}] PUSH: ThreadPool executing push operation for {}", worker_id, request_id);
                        
                        // FAILOVER: Quick health check first - if DB is known to be down, skip directly to failover
                        if (file_buffer && !file_buffer->is_db_healthy()) {
                            spdlog::warn("[Worker {}] PUSH: DB known to be down, using file buffer immediately for {}", worker_id, request_id);
                            
                            // Validate items before writing to file buffer
                            for (const auto& item_json : body["items"]) {
                                // Validate partition type if present
                                if (item_json.contains("partition") && !item_json["partition"].is_string()) {
                                    spdlog::error("[Worker {}] PUSH: Invalid partition type for {}, must be string", worker_id, request_id);
                                    nlohmann::json error_response = {{"error", "Partition must be a string"}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                    return;
                                }
                                
                                // Validate queue type
                                if (!item_json.contains("queue") || !item_json["queue"].is_string()) {
                                    spdlog::error("[Worker {}] PUSH: Invalid or missing queue for {}", worker_id, request_id);
                                    nlohmann::json error_response = {{"error", "Queue must be a string"}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                    return;
                                }
                            }
                            
                            // Write all items to file buffer with failover flag
                            for (const auto& item_json : body["items"]) {
                                nlohmann::json buffered_event = {
                                    {"queue", item_json["queue"]},
                                    {"partition", item_json.value("partition", "Default")},
                                    {"payload", item_json.value("payload", nlohmann::json{})},
                                    {"namespace", item_json.value("namespace", "")},
                                    {"task", item_json.value("task", "")},
                                    {"traceId", item_json.value("traceId", "")},
                                    {"transactionId", item_json.value("transactionId", queue_manager->generate_uuid())},
                                    {"failover", true}
                                };
                                file_buffer->write_event(buffered_event);
                            }
                            
                            // Build success response with failover metadata
                            nlohmann::json json_results = nlohmann::json::array();
                            for (const auto& item_json : body["items"]) {
                                nlohmann::json json_result;
                                json_result["transaction_id"] = item_json.value("transactionId", queue_manager->generate_uuid());
                                json_result["status"] = "queued";
                                json_result["failover"] = true;
                                json_results.push_back(json_result);
                            }
                            
                            worker_response_queues[worker_id]->push(request_id, json_results, false, 201);
                            spdlog::info("[Worker {}] PUSH: Queued failover response for {} ({} items buffered)", worker_id, request_id, body["items"].size());
                            return;
                        }
                        
                        try {
                            // Parse items with validation
                            std::vector<PushItem> items;
                            for (const auto& item_json : body["items"]) {
                                // Validate partition type if present
                                if (item_json.contains("partition") && !item_json["partition"].is_string()) {
                                    spdlog::error("[Worker {}] PUSH: Invalid partition type for {}, must be string", worker_id, request_id);
                                    nlohmann::json error_response = {{"error", "Partition must be a string"}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                    return;
                                }
                                
                                // Validate queue type
                                if (!item_json.contains("queue") || !item_json["queue"].is_string()) {
                                    spdlog::error("[Worker {}] PUSH: Invalid or missing queue for {}", worker_id, request_id);
                                    nlohmann::json error_response = {{"error", "Queue must be a string"}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                    return;
                                }
                                
                                // Validate transactionId type if present
                                if (item_json.contains("transactionId") && !item_json["transactionId"].is_string()) {
                                    spdlog::error("[Worker {}] PUSH: Invalid transactionId type for {}, must be string", worker_id, request_id);
                                    nlohmann::json error_response = {{"error", "TransactionId must be a string"}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                    return;
                                }
                                
                                // Validate traceId type if present
                                if (item_json.contains("traceId") && !item_json["traceId"].is_string()) {
                                    spdlog::error("[Worker {}] PUSH: Invalid traceId type for {}, must be string", worker_id, request_id);
                                    nlohmann::json error_response = {{"error", "TraceId must be a string"}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                    return;
                                }
                                
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
                            spdlog::info("[Worker {}] PUSH: Calling push_messages in ThreadPool for {}", worker_id, request_id);
                            auto results = queue_manager->push_messages(items);
                            
                            // FAILOVER: Check if push failed
                            bool has_error = false;
                            std::string error_msg;
                            
                            for (const auto& result : results) {
                                if (result.status == "failed" && result.error) {
                                    has_error = true;
                                    error_msg = *result.error;
                                    break;
                                }
                            }
                            
                            // If ANY error occurred, do a health check to determine if DB is down
                            bool db_failure = false;
                            if (has_error) {
                                // Do a quick health check
                                bool db_is_healthy = queue_manager->health_check();
                                if (!db_is_healthy) {
                                    // DB is down - use failover
                                    spdlog::warn("[Worker {}] PUSH: Error '{}' detected and health check failed - DB is down, using failover", worker_id, error_msg);
                                    db_failure = true;
                                } else {
                                    // DB is healthy - this is a real application error
                                    spdlog::warn("[Worker {}] PUSH: Error '{}' detected but DB is healthy - returning error to client", worker_id, error_msg);
                                    nlohmann::json error_response = {{"error", error_msg}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                    return;
                                }
                            }
                            
                            // FAILOVER: If DB failure detected, use file buffer (if available)
                            if (db_failure) {
                                if (file_buffer) {
                                    spdlog::warn("[Worker {}] PUSH: DB connection failed, using file buffer for failover for {}", worker_id, request_id);
                                    file_buffer->mark_db_unhealthy();
                                    
                                    // Write all items to file buffer
                                    for (const auto& item_json : body["items"]) {
                                        nlohmann::json buffered_event = {
                                            {"queue", item_json["queue"]},
                                            {"partition", item_json.value("partition", "Default")},
                                            {"payload", item_json.value("payload", nlohmann::json{})},
                                            {"namespace", item_json.value("namespace", "")},
                                            {"task", item_json.value("task", "")},
                                            {"traceId", item_json.value("traceId", "")},
                                            {"transactionId", item_json.value("transactionId", queue_manager->generate_uuid())},
                                            {"failover", true}
                                        };
                                        file_buffer->write_event(buffered_event);
                                    }
                                    
                                    // Build success response with failover metadata
                                    nlohmann::json json_results = nlohmann::json::array();
                                    for (const auto& item_json : body["items"]) {
                                        nlohmann::json json_result;
                                        json_result["transaction_id"] = item_json.value("transactionId", queue_manager->generate_uuid());
                                        json_result["status"] = "queued";
                                        json_result["failover"] = true;
                                        json_results.push_back(json_result);
                                    }
                                    
                                    worker_response_queues[worker_id]->push(request_id, json_results, false, 201);
                                    spdlog::info("[Worker {}] PUSH: Queued failover response for {} ({} items buffered)", worker_id, request_id, body["items"].size());
                                    return;
                                } else {
                                    // No file buffer available (non-zero worker) - return error
                                    spdlog::error("[Worker {}] PUSH: DB connection failed and no file buffer available for {}", worker_id, request_id);
                                    nlohmann::json error_response = {{"error", "Database unavailable and failover not available on this worker"}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 503);
                                    return;
                                }
                            }
                            
                            // Convert PushResult vector to JSON (normal success case)
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
                            
                            // All items succeeded
                            worker_response_queues[worker_id]->push(request_id, json_results, false, 201);
                            spdlog::info("[Worker {}] PUSH: Queued success response for {} ({} items)", worker_id, request_id, results.size());
                            
                        } catch (const nlohmann::json::exception& json_error) {
                            // JSON parsing/validation error - return 400 error, do NOT trigger failover
                            spdlog::error("[Worker {}] PUSH: JSON validation error for {}: {}", 
                                       worker_id, request_id, json_error.what());
                            nlohmann::json error_response = {{"error", std::string("Invalid JSON data: ") + json_error.what()}};
                            worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                        } catch (const std::exception& db_error) {
                            // FAILOVER: PostgreSQL exception - fallback to file buffer for all items (if available)
                            if (file_buffer) {
                                spdlog::warn("[Worker {}] PUSH: PostgreSQL unavailable for {}, using file buffer for failover: {}", 
                                           worker_id, request_id, db_error.what());
                                
                                // Mark DB as unhealthy so subsequent pushes skip DB attempt
                                file_buffer->mark_db_unhealthy();
                                
                                // Write all items to file buffer
                                for (const auto& item_json : body["items"]) {
                                    nlohmann::json buffered_event = {
                                        {"queue", item_json["queue"]},
                                        {"partition", item_json.value("partition", "Default")},
                                        {"payload", item_json.value("payload", nlohmann::json{})},
                                        {"namespace", item_json.value("namespace", "")},
                                        {"task", item_json.value("task", "")},
                                        {"traceId", item_json.value("traceId", "")},
                                        {"transactionId", item_json.value("transactionId", queue_manager->generate_uuid())},
                                        {"failover", true}
                                    };
                                    file_buffer->write_event(buffered_event);
                                }
                                
                                // Build success response with failover metadata
                                nlohmann::json json_results = nlohmann::json::array();
                                for (const auto& item_json : body["items"]) {
                                    nlohmann::json json_result;
                                    json_result["transaction_id"] = item_json.value("transactionId", queue_manager->generate_uuid());
                                    json_result["status"] = "queued";
                                    json_result["failover"] = true;
                                    json_results.push_back(json_result);
                                }
                                
                                worker_response_queues[worker_id]->push(request_id, json_results, false, 201);
                                spdlog::info("[Worker {}] PUSH: Queued failover response for {} ({} items buffered)", worker_id, request_id, body["items"].size());
                            } else {
                                // No file buffer available (non-zero worker) - return error
                                spdlog::error("[Worker {}] PUSH: PostgreSQL unavailable and no file buffer available for {}: {}", 
                                            worker_id, request_id, db_error.what());
                                nlohmann::json error_response = {{"error", "Database unavailable and failover not available on this worker"}};
                                worker_response_queues[worker_id]->push(request_id, error_response, true, 503);
                            }
                        }
                    });
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // SPECIFIC POP from queue/partition - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop/queue/:queue/partition/:partition", [queue_manager, config, worker_id, db_thread_pool](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string partition_name = std::string(req->getParameter(1));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
            
            auto pool_stats = queue_manager->get_pool_stats();
            spdlog::info("[Worker {}] SPOP: [{}/{}@{}] batch={}, wait={} | Pool: {}/{} conn ({} in use)", 
                        worker_id, queue_name, partition_name, consumer_group, batch, wait,
                        pool_stats.available, pool_stats.total, pool_stats.in_use);
            
            PopOptions options;
            options.wait = false;  // Always false - registry handles waiting
            options.timeout = timeout_ms;
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
            
            // Register response in uWebSockets thread - SAFE
            std::string request_id = global_response_registry->register_response(res, worker_id);
            
            if (wait) {
                // Use Poll Intention Registry for long-polling
                queen::PollIntention intention{
                    .request_id = request_id,
                    .worker_id = worker_id,
                    .queue_name = queue_name,
                    .partition_name = partition_name,  // Specific partition
                    .namespace_name = std::nullopt,
                    .task_name = std::nullopt,
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now()
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                spdlog::info("[Worker {}] SPOP: Registered poll intention {} for queue {}/{} (wait=true)", 
                            worker_id, request_id, queue_name, partition_name);
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use ThreadPool directly (existing logic)
            spdlog::info("[Worker {}] SPOP: Registered response {}, submitting to ThreadPool (wait=false)", worker_id, request_id);
            
            db_thread_pool->push([request_id, queue_manager, worker_id, queue_name, partition_name, consumer_group, options]() {
                spdlog::info("[Worker {}] SPOP: ThreadPool executing specific pop operation for {}", worker_id, request_id);
                
                try {
                    auto start_time = std::chrono::steady_clock::now();
                    auto result = queue_manager->pop_messages(queue_name, partition_name, consumer_group, options);
                    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time).count();
                    
                    if (result.messages.empty()) {
                        // No messages - send 204 No Content
                        nlohmann::json empty_response;
                        worker_response_queues[worker_id]->push(request_id, empty_response, false, 204);
                        spdlog::info("[Worker {}] SPOP: No messages for {} [{}/{}], {}ms", 
                                   worker_id, request_id, queue_name, partition_name, duration_ms);
                        return;
                    }
                    
                    // Build response JSON
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
                            {"partitionId", msg.partition_id},
                            {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                            {"queue", msg.queue_name},
                            {"partition", msg.partition_name},
                            {"data", msg.payload},
                            {"retryCount", msg.retry_count},
                            {"priority", msg.priority},
                            {"createdAt", created_at_ss.str()},
                            {"consumerGroup", consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(consumer_group)},
                            {"leaseId", result.lease_id.has_value() ? nlohmann::json(*result.lease_id) : nlohmann::json(nullptr)}
                        };
                        response["messages"].push_back(msg_json);
                    }
                    
                    // SAFE: Push to response queue
                    worker_response_queues[worker_id]->push(request_id, response, false, 200);
                    spdlog::info("[Worker {}] SPOP: Queued response for {} [{}/{}] ({} messages, {}ms)", 
                               worker_id, request_id, queue_name, partition_name, result.messages.size(), duration_ms);
                    
                } catch (const std::exception& e) {
                    // Error handling - push error to response queue
                    nlohmann::json error_response = {{"error", e.what()}};
                    worker_response_queues[worker_id]->push(request_id, error_response, true, 500);
                    spdlog::error("[Worker {}] SPOP: Error for {} [{}/{}]: {}", worker_id, request_id, queue_name, partition_name, e.what());
                }
            });
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // ASYNC ACK batch
    // ASYNC ACK Batch - NEW RESPONSE QUEUE ARCHITECTURE
    app->post("/api/v1/ack/batch", [queue_manager, worker_id, db_thread_pool](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager, worker_id, db_thread_pool](const nlohmann::json& body) {
                try {
                    if (!body.contains("acknowledgments") || !body["acknowledgments"].is_array()) {
                        send_error_response(res, "acknowledgments array is required", 400);
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
                            send_error_response(res, "Each acknowledgment must have a valid transactionId string", 400);
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
                        
                        // CRITICAL: partition_id is now MANDATORY to prevent acking wrong message
                        // when transactionId is not unique across partitions
                        if (ack_json.contains("partitionId") && !ack_json["partitionId"].is_null() && ack_json["partitionId"].is_string()) {
                            ack.partition_id = ack_json["partitionId"];
                        } else {
                            send_error_response(res, "Each acknowledgment must have a valid partitionId string to ensure message uniqueness", 400);
                            return;
                        }
                        
                        ack_items.push_back(ack);
                    }
                    
                    // Register response in uWebSockets thread - SAFE
                    std::string request_id = global_response_registry->register_response(res, worker_id);
                    
                    spdlog::info("[Worker {}] ACK BATCH: Registered response {}, submitting to ThreadPool", worker_id, request_id);
                    
                    // Execute batch ACK operation in ThreadPool
                    db_thread_pool->push([request_id, queue_manager, worker_id, ack_items, consumer_group]() {
                        spdlog::info("[Worker {}] ACK BATCH: ThreadPool executing batch ACK operation for {}", worker_id, request_id);
                        
                        try {
                            auto ack_start = std::chrono::steady_clock::now();
                            auto results = queue_manager->acknowledge_messages(ack_items, consumer_group);
                            auto ack_end = std::chrono::steady_clock::now();
                            auto ack_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ack_end - ack_start).count();
                            
                            spdlog::info("[Worker {}] ACK BATCH: {} items, {}ms", 
                                        worker_id, results.size(), ack_duration_ms);
                            
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
                            
                            // SAFE: Push to response queue
                            worker_response_queues[worker_id]->push(request_id, response, false, 200);
                            spdlog::info("[Worker {}] ACK BATCH: Queued response for {} ({} items)", worker_id, request_id, results.size());
                            
                        } catch (const std::exception& e) {
                            // Error handling - push error to response queue
                            nlohmann::json error_response = {{"error", e.what()}};
                            worker_response_queues[worker_id]->push(request_id, error_response, true, 500);
                            spdlog::error("[Worker {}] ACK BATCH: Error for {}: {}", worker_id, request_id, e.what());
                        }
                    });
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
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
    
    // POP from queue (any partition) - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop/queue/:queue", [queue_manager, config, worker_id, db_thread_pool](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
            
            auto pool_stats = queue_manager->get_pool_stats();
            spdlog::info("[Worker {}] QPOP: [{}/*@{}] batch={}, wait={} | Pool: {}/{} conn ({} in use)", 
                        worker_id, queue_name, consumer_group, batch, wait,
                        pool_stats.available, pool_stats.total, pool_stats.in_use);
            
            PopOptions options;
            options.wait = false;  // Always false - registry handles waiting
            options.timeout = timeout_ms;
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
            
            // Register response in uWebSockets thread - SAFE
            std::string request_id = global_response_registry->register_response(res, worker_id);
            
            if (wait) {
                // Use Poll Intention Registry for long-polling
                queen::PollIntention intention{
                    .request_id = request_id,
                    .worker_id = worker_id,
                    .queue_name = queue_name,
                    .partition_name = std::nullopt,  // Any partition
                    .namespace_name = std::nullopt,
                    .task_name = std::nullopt,
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now()
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                spdlog::info("[Worker {}] QPOP: Registered poll intention {} for queue {} (wait=true)", 
                            worker_id, request_id, queue_name);
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use ThreadPool directly (existing logic)
            spdlog::info("[Worker {}] QPOP: Registered response {}, submitting to ThreadPool (wait=false)", worker_id, request_id);
            
            db_thread_pool->push([request_id, queue_manager, worker_id, queue_name, consumer_group, options]() {
                spdlog::info("[Worker {}] QPOP: ThreadPool executing queue pop operation for {}", worker_id, request_id);
                
                try {
                    auto start_time = std::chrono::steady_clock::now();
                    auto result = queue_manager->pop_messages(queue_name, std::nullopt, consumer_group, options);
                    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time).count();
                    
                    if (result.messages.empty()) {
                        // No messages - send 204 No Content
                        nlohmann::json empty_response;
                        worker_response_queues[worker_id]->push(request_id, empty_response, false, 204);
                        spdlog::info("[Worker {}] QPOP: No messages for {} [{}/*], {}ms", 
                                   worker_id, request_id, queue_name, duration_ms);
                        return;
                    }
                    
                    // Build response JSON
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
                            {"partitionId", msg.partition_id},
                            {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                            {"queue", msg.queue_name},
                            {"partition", msg.partition_name},
                            {"data", msg.payload},
                            {"retryCount", msg.retry_count},
                            {"priority", msg.priority},
                            {"createdAt", created_at_ss.str()},
                            {"consumerGroup", consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(consumer_group)},
                            {"leaseId", result.lease_id.has_value() ? nlohmann::json(*result.lease_id) : nlohmann::json(nullptr)}
                        };
                        response["messages"].push_back(msg_json);
                    }
                    
                    // SAFE: Push to response queue
                    worker_response_queues[worker_id]->push(request_id, response, false, 200);
                    spdlog::info("[Worker {}] QPOP: Queued response for {} [{}/*] ({} messages, {}ms)", 
                               worker_id, request_id, queue_name, result.messages.size(), duration_ms);
                    
                } catch (const std::exception& e) {
                    // Error handling - push error to response queue
                    nlohmann::json error_response = {{"error", e.what()}};
                    worker_response_queues[worker_id]->push(request_id, error_response, true, 500);
                    spdlog::error("[Worker {}] QPOP: Error for {} [{}/*]: {}", worker_id, request_id, queue_name, e.what());
                }
            });
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // ASYNC Single ACK - NEW RESPONSE QUEUE ARCHITECTURE
    app->post("/api/v1/ack", [queue_manager, worker_id, db_thread_pool](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager, worker_id, db_thread_pool](const nlohmann::json& body) {
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
                    
                    // CRITICAL: partition_id is now MANDATORY to prevent acking wrong message
                    // when transactionId is not unique across partitions
                    std::optional<std::string> partition_id;
                    if (body.contains("partitionId") && !body["partitionId"].is_null() && body["partitionId"].is_string()) {
                        partition_id = body["partitionId"];
                    } else {
                        send_error_response(res, "partitionId is required to ensure message uniqueness", 400);
                        return;
                    }
                    
                    // Register response in uWebSockets thread - SAFE
                    std::string request_id = global_response_registry->register_response(res, worker_id);
                    
                    spdlog::info("[Worker {}] ACK: Registered response {}, submitting to ThreadPool", worker_id, request_id);
                    
                    // Execute ACK operation in ThreadPool
                    db_thread_pool->push([request_id, queue_manager, worker_id, transaction_id, status, error, consumer_group, lease_id, partition_id]() {
                        spdlog::info("[Worker {}] ACK: ThreadPool executing ACK operation for {}", worker_id, request_id);
                        
                        try {
                            auto ack_start = std::chrono::steady_clock::now();
                            auto ack_result = queue_manager->acknowledge_message(transaction_id, status, error, consumer_group, lease_id, partition_id);
                            auto ack_end = std::chrono::steady_clock::now();
                            auto ack_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ack_end - ack_start).count();
                            
                            spdlog::debug("[Worker {}] ACK: 1 item, {}ms", worker_id, ack_duration_ms);
                            
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
                                
                                // SAFE: Push to response queue
                                worker_response_queues[worker_id]->push(request_id, response, false, 200);
                                spdlog::info("[Worker {}] ACK: Queued success response for {}", worker_id, request_id);
                            } else {
                                nlohmann::json error_response = {{"error", "Failed to acknowledge message: " + ack_result.status}};
                                worker_response_queues[worker_id]->push(request_id, error_response, true, 500);
                                spdlog::warn("[Worker {}] ACK: Queued error response for {}: {}", worker_id, request_id, ack_result.status);
                            }
                            
                        } catch (const std::exception& e) {
                            // Error handling - push error to response queue
                            nlohmann::json error_response = {{"error", e.what()}};
                            worker_response_queues[worker_id]->push(request_id, error_response, true, 500);
                            spdlog::error("[Worker {}] ACK: Error for {}: {}", worker_id, request_id, e.what());
                        }
                    });
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // ASYNC POP with namespace/task filtering (no specific queue) - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop", [queue_manager, config, worker_id, db_thread_pool](auto* res, auto* req) {
        try {
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            std::string namespace_param = get_query_param(req, "namespace", "");
            std::string task_param = get_query_param(req, "task", "");
            
            std::optional<std::string> namespace_name = namespace_param.empty() ? std::nullopt : std::optional<std::string>(namespace_param);
            std::optional<std::string> task_name = task_param.empty() ? std::nullopt : std::optional<std::string>(task_param);
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
            
            PopOptions options;
            options.wait = false;  // Always false - registry handles waiting
            options.timeout = timeout_ms;
            options.batch = batch;
            options.auto_ack = get_query_param_bool(req, "autoAck", false);
            
            std::string sub_mode = get_query_param(req, "subscriptionMode", "");
            if (!sub_mode.empty()) options.subscription_mode = sub_mode;
            std::string sub_from = get_query_param(req, "subscriptionFrom", "");
            if (!sub_from.empty()) options.subscription_from = sub_from;
            
            // Register response in uWebSockets thread - SAFE
            std::string request_id = global_response_registry->register_response(res, worker_id);
            
            if (wait) {
                // Use Poll Intention Registry for long-polling
                queen::PollIntention intention{
                    .request_id = request_id,
                    .worker_id = worker_id,
                    .queue_name = std::nullopt,
                    .partition_name = std::nullopt,
                    .namespace_name = namespace_name,  // Namespace filtering
                    .task_name = task_name,            // Task filtering
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now()
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                spdlog::info("[Worker {}] POP: Registered poll intention {} for namespace={} task={} (wait=true)", 
                            worker_id, request_id, 
                            namespace_name.value_or("*"), 
                            task_name.value_or("*"));
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use ThreadPool directly (existing logic)
            spdlog::info("[Worker {}] POP: Registered response {}, submitting to ThreadPool (wait=false)", worker_id, request_id);
            
            db_thread_pool->push([request_id, queue_manager, worker_id, namespace_name, task_name, consumer_group, options]() {
                spdlog::info("[Worker {}] POP: ThreadPool executing pop operation for {}", worker_id, request_id);
                
                try {
                    auto result = queue_manager->pop_with_namespace_task(namespace_name, task_name, consumer_group, options);
                    
                    if (result.messages.empty()) {
                        // No messages - send 204 No Content
                        nlohmann::json empty_response;
                        worker_response_queues[worker_id]->push(request_id, empty_response, false, 204);
                        spdlog::debug("[Worker {}] POP: No messages for {}", worker_id, request_id);
                        return;
                    }
                    
                    // Build response JSON
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
                            {"partitionId", msg.partition_id},
                            {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                            {"queue", msg.queue_name},
                            {"partition", msg.partition_name},
                            {"data", msg.payload},
                            {"retryCount", msg.retry_count},
                            {"priority", msg.priority},
                            {"createdAt", created_at_ss.str()},
                            {"consumerGroup", consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(consumer_group)},
                            {"leaseId", result.lease_id.has_value() ? nlohmann::json(*result.lease_id) : nlohmann::json(nullptr)}
                        };
                        response["messages"].push_back(msg_json);
                    }
                    
                    // SAFE: Push to response queue
                    worker_response_queues[worker_id]->push(request_id, response, false, 200);
                    spdlog::info("[Worker {}] POP: Queued response for {} ({} messages)", worker_id, request_id, result.messages.size());
                    
                } catch (const std::exception& e) {
                    // Error handling - push error to response queue
                    nlohmann::json error_response = {{"error", e.what()}};
                    worker_response_queues[worker_id]->push(request_id, error_response, true, 500);
                    spdlog::error("[Worker {}] POP: Error for {}: {}", worker_id, request_id, e.what());
                }
            });
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // ASYNC Transaction API (atomic operations) - NEW RESPONSE QUEUE ARCHITECTURE
    app->post("/api/v1/transaction", [queue_manager, worker_id, db_thread_pool](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager, worker_id, db_thread_pool](const nlohmann::json& body) {
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
                    
                    // Register response in uWebSockets thread - SAFE
                    std::string request_id = global_response_registry->register_response(res, worker_id);
                    
                    spdlog::info("[Worker {}] TRANSACTION: Registered response {}, submitting to ThreadPool", worker_id, request_id);
                    
                    // Execute entire transaction in ThreadPool
                    db_thread_pool->push([request_id, queue_manager, worker_id, operations]() {
                        spdlog::info("[Worker {}] TRANSACTION: ThreadPool executing transaction operations for {}", worker_id, request_id);
                        
                        try {
                            // Execute operations sequentially
                            nlohmann::json results = nlohmann::json::array();
                    
                            for (const auto& op : operations) {
                                if (!op.contains("type")) {
                                    nlohmann::json error_response = {{"error", "operation type required"}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                    spdlog::error("[Worker {}] TRANSACTION: Missing operation type for {}", worker_id, request_id);
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
                                    
                                    // CRITICAL: partition_id is now MANDATORY
                                    std::optional<std::string> partition_id;
                                    if (op.contains("partitionId") && !op["partitionId"].is_null() && op["partitionId"].is_string()) {
                                        partition_id = op["partitionId"];
                                    } else {
                                        nlohmann::json error_response = {{"error", "partitionId is required for ack operations to ensure message uniqueness"}};
                                        worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                        spdlog::error("[Worker {}] TRANSACTION: Missing partitionId in ack operation for {}", worker_id, request_id);
                                        return;
                                    }
                                    
                                    auto ack_result = queue_manager->acknowledge_message(transaction_id, status, error, consumer_group, std::nullopt, partition_id);
                                    nlohmann::json ack_result_json;
                                    ack_result_json["type"] = "ack";
                                    ack_result_json["success"] = ack_result.success;
                                    ack_result_json["transactionId"] = ack_result.transaction_id;
                                    ack_result_json["status"] = ack_result.status;
                                    results.push_back(ack_result_json);
                                    
                                } else {
                                    // Handle unknown operation type
                                    nlohmann::json error_response = {{"error", "Unknown operation type: " + op_type}};
                                    worker_response_queues[worker_id]->push(request_id, error_response, true, 400);
                                    spdlog::error("[Worker {}] TRANSACTION: Unknown operation type {} for {}", worker_id, op_type, request_id);
                                    return;
                                }
                            }
                            
                            // Build final response
                            nlohmann::json response = {
                                {"success", true},
                                {"results", results},
                                {"transactionId", queue_manager->generate_uuid()}
                            };
                            
                            // SAFE: Push to response queue
                            worker_response_queues[worker_id]->push(request_id, response, false, 200);
                            spdlog::info("[Worker {}] TRANSACTION: Queued response for {} ({} operations)", worker_id, request_id, operations.size());
                            
                        } catch (const std::exception& e) {
                            // Error handling - push error to response queue
                            nlohmann::json error_response = {{"error", e.what()}};
                            worker_response_queues[worker_id]->push(request_id, error_response, true, 500);
                            spdlog::error("[Worker {}] TRANSACTION: Error for {}: {}", worker_id, request_id, e.what());
                        }
                    });
                    
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
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.limit = get_query_param_int(req, "limit", 200);
            filters.offset = get_query_param_int(req, "offset", 0);
            
            auto messages = analytics_manager->list_messages(filters);
            send_json_response(res, messages);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/messages/:partitionId/:transactionId - Get single message detail
    app->get("/api/v1/messages/:partitionId/:transactionId", [analytics_manager](auto* res, auto* req) {
        try {
            std::string partition_id = std::string(req->getParameter(0));
            std::string transaction_id = std::string(req->getParameter(1));
            auto message = analytics_manager->get_message(partition_id, transaction_id);
            send_json_response(res, message);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not found") != std::string::npos) {
                send_error_response(res, e.what(), 404);
            } else {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // DELETE /api/v1/messages/:partitionId/:transactionId - Delete a message
    app->del("/api/v1/messages/:partitionId/:transactionId", [analytics_manager](auto* res, auto* req) {
        try {
            std::string partition_id = std::string(req->getParameter(0));
            std::string transaction_id = std::string(req->getParameter(1));
            
            bool deleted = analytics_manager->delete_message(partition_id, transaction_id);
            
            if (deleted) {
                nlohmann::json response = {
                    {"success", true},
                    {"message", "Message deleted successfully"},
                    {"partitionId", partition_id},
                    {"transactionId", transaction_id}
                };
                send_json_response(res, response);
            } else {
                send_error_response(res, "Message not found", 404);
            }
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/dlq - Get dead letter queue messages
    app->get("/api/v1/dlq", [analytics_manager](auto* res, auto* req) {
        try {
            AnalyticsManager::DLQFilters filters;
            filters.queue = get_query_param(req, "queue");
            filters.consumer_group = get_query_param(req, "consumerGroup");
            filters.partition = get_query_param(req, "partition");
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.limit = get_query_param_int(req, "limit", 100);
            filters.offset = get_query_param_int(req, "offset", 0);
            
            auto response = analytics_manager->get_dlq_messages(filters);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // ============================================================================
    // Message Tracing Routes
    // ============================================================================
    
    // POST /api/v1/traces - Record trace event
    app->post("/api/v1/traces", [queue_manager, worker_id](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager, worker_id](const nlohmann::json& body) {
                try {
                    // Validate required fields
                    if (!body.contains("transactionId") || !body.contains("partitionId")) {
                        send_error_response(res, "transactionId and partitionId are required", 400);
                        return;
                    }
                    
                    if (!body.contains("data")) {
                        send_error_response(res, "data is required", 400);
                        return;
                    }
                    
                    // Parse traceNames (can be null, string, or array)
                    std::vector<std::string> trace_names;
                    if (body.contains("traceNames") && !body["traceNames"].is_null()) {
                        if (body["traceNames"].is_array()) {
                            for (const auto& name : body["traceNames"]) {
                                if (name.is_string() && !name.get<std::string>().empty()) {
                                    trace_names.push_back(name.get<std::string>());
                                }
                            }
                        } else if (body["traceNames"].is_string()) {
                            trace_names.push_back(body["traceNames"].get<std::string>());
                        }
                    }
                    
                    // Call queue_manager method to store trace
                    bool success = queue_manager->record_trace(
                        body["transactionId"],
                        body["partitionId"],
                        body.value("consumerGroup", "__QUEUE_MODE__"),
                        trace_names,
                        body.value("eventType", "info"),
                        body["data"],
                        std::to_string(worker_id)
                    );
                    
                    if (success) {
                        nlohmann::json response = {{"success", true}};
                        send_json_response(res, response, 201);
                    } else {
                        send_error_response(res, "Failed to record trace", 500);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Failed to handle trace request: {}", e.what());
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // GET /api/v1/traces/:partitionId/:transactionId - Get traces for a message
    app->get("/api/v1/traces/:partitionId/:transactionId", [queue_manager](auto* res, auto* req) {
        try {
            std::string partition_id = std::string(req->getParameter(0));
            std::string transaction_id = std::string(req->getParameter(1));
            
            auto traces = queue_manager->get_message_traces(partition_id, transaction_id);
            send_json_response(res, traces);
        } catch (const std::exception& e) {
            spdlog::error("Failed to get traces: {}", e.what());
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/traces/by-name/:traceName - Get traces by any name
    app->get("/api/v1/traces/by-name/:traceName", [queue_manager](auto* res, auto* req) {
        try {
            std::string trace_name = std::string(req->getParameter(0));
            
            int limit = get_query_param_int(req, "limit", 100);
            int offset = get_query_param_int(req, "offset", 0);
            
            auto traces = queue_manager->get_traces_by_name(trace_name, limit, offset);
            send_json_response(res, traces);
        } catch (const std::exception& e) {
            spdlog::error("Failed to get traces by name: {}", e.what());
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/traces/names - Get available trace names
    app->get("/api/v1/traces/names", [queue_manager](auto* res, auto* req) {
        try {
            int limit = get_query_param_int(req, "limit", 50);
            int offset = get_query_param_int(req, "offset", 0);
            
            auto trace_names = queue_manager->get_available_trace_names(limit, offset);
            send_json_response(res, trace_names);
        } catch (const std::exception& e) {
            spdlog::error("Failed to get available trace names: {}", e.what());
            send_error_response(res, e.what(), 500);
        }
    });
    
    // ============================================================================
    // Streaming Routes
    // ============================================================================
    
    // POST /api/v1/stream/define - Define a stream
    app->post("/api/v1/stream/define", [worker_id](auto* res, auto* req) {
        global_stream_manager->handle_define(res, req, worker_id);
    });
    
    // POST /api/v1/stream/poll - Poll for a window
    app->post("/api/v1/stream/poll", [worker_id](auto* res, auto* req) {
        global_stream_manager->handle_poll(res, req, worker_id);
    });
    
    // POST /api/v1/stream/ack - Acknowledge a window
    app->post("/api/v1/stream/ack", [worker_id](auto* res, auto* req) {
        global_stream_manager->handle_ack(res, req, worker_id);
    });
    
    // POST /api/v1/stream/renew-lease - Renew a lease
    app->post("/api/v1/stream/renew-lease", [worker_id](auto* res, auto* req) {
        global_stream_manager->handle_renew(res, req, worker_id);
    });
    
    // POST /api/v1/stream/seek - Seek to a timestamp
    app->post("/api/v1/stream/seek", [worker_id](auto* res, auto* req) {
        global_stream_manager->handle_seek(res, req, worker_id);
    });
    
    // GET /api/v1/resources/streams - List all streams
    app->get("/api/v1/resources/streams", [](auto* res, auto* req) {
        try {
            ScopedConnection conn(global_db_pool.get());
            auto result = QueryResult(conn->exec("SELECT * FROM queen.streams ORDER BY created_at DESC"));
            
            nlohmann::json streams = nlohmann::json::array();
            for (int i = 0; i < result.num_rows(); i++) {
                nlohmann::json stream = {
                    {"id", result.get_value(i, "id")},
                    {"name", result.get_value(i, "name")},
                    {"namespace", result.get_value(i, "namespace")},
                    {"partitioned", result.get_value(i, "partitioned") == "t"},
                    {"windowType", result.get_value(i, "window_type")},
                    {"windowDurationMs", std::stoll(result.get_value(i, "window_duration_ms"))},
                    {"windowGracePeriodMs", std::stoll(result.get_value(i, "window_grace_period_ms"))},
                    {"windowLeaseTimeoutMs", std::stoll(result.get_value(i, "window_lease_timeout_ms"))},
                    {"createdAt", result.get_value(i, "created_at")},
                    {"updatedAt", result.get_value(i, "updated_at")}
                };
                
                // Get source queues for this stream
                auto sources_result = QueryResult(conn->exec_params(
                    "SELECT q.name FROM queen.stream_sources ss JOIN queen.queues q ON ss.queue_id = q.id WHERE ss.stream_id = $1::UUID",
                    {result.get_value(i, "id")}
                ));
                nlohmann::json source_queues = nlohmann::json::array();
                for (int j = 0; j < sources_result.num_rows(); j++) {
                    source_queues.push_back(sources_result.get_value(j, "name"));
                }
                stream["sourceQueues"] = source_queues;
                
                // Get active leases count
                auto leases_result = QueryResult(conn->exec_params(
                    "SELECT COUNT(*) as count FROM queen.stream_leases WHERE stream_id = $1::UUID AND lease_expires_at > NOW()",
                    {result.get_value(i, "id")}
                ));
                stream["activeLeases"] = leases_result.num_rows() > 0 ? std::stoi(leases_result.get_value(0, "count")) : 0;
                
                // Get consumer groups count
                auto consumers_result = QueryResult(conn->exec_params(
                    "SELECT COUNT(DISTINCT consumer_group) as count FROM queen.stream_consumer_offsets WHERE stream_id = $1::UUID",
                    {result.get_value(i, "id")}
                ));
                stream["consumerGroups"] = consumers_result.num_rows() > 0 ? std::stoi(consumers_result.get_value(0, "count")) : 0;
                
                streams.push_back(stream);
            }
            
            nlohmann::json response = {
                {"streams", streams}
            };
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/streams/stats - Get stream statistics
    app->get("/api/v1/resources/streams/stats", [](auto* res, auto* req) {
        try {
            ScopedConnection conn(global_db_pool.get());
            
            // Total streams
            auto streams_result = QueryResult(conn->exec("SELECT COUNT(*) as count FROM queen.streams"));
            int total_streams = streams_result.num_rows() > 0 ? std::stoi(streams_result.get_value(0, "count")) : 0;
            
            // Partitioned streams count
            auto partitioned_result = QueryResult(conn->exec("SELECT COUNT(*) as count FROM queen.streams WHERE partitioned = true"));
            int partitioned_streams = partitioned_result.num_rows() > 0 ? std::stoi(partitioned_result.get_value(0, "count")) : 0;
            
            // Active leases (windows being processed)
            auto leases_result = QueryResult(conn->exec("SELECT COUNT(*) as count FROM queen.stream_leases WHERE lease_expires_at > NOW()"));
            int active_leases = leases_result.num_rows() > 0 ? std::stoi(leases_result.get_value(0, "count")) : 0;
            
            // Total unique consumer groups across all streams
            auto consumer_groups_result = QueryResult(conn->exec("SELECT COUNT(DISTINCT consumer_group) as count FROM queen.stream_consumer_offsets"));
            int total_consumer_groups = consumer_groups_result.num_rows() > 0 ? std::stoi(consumer_groups_result.get_value(0, "count")) : 0;
            
            // Active consumers (consumer groups that consumed in last hour)
            auto active_consumers_result = QueryResult(conn->exec(
                "SELECT COUNT(DISTINCT consumer_group) as count FROM queen.stream_consumer_offsets "
                "WHERE last_consumed_at > NOW() - INTERVAL '1 hour'"
            ));
            int active_consumers = active_consumers_result.num_rows() > 0 ? std::stoi(active_consumers_result.get_value(0, "count")) : 0;
            
            // Total windows processed (sum of all consumer groups)
            auto windows_processed_result = QueryResult(conn->exec("SELECT COALESCE(SUM(total_windows_consumed), 0) as total FROM queen.stream_consumer_offsets"));
            long long total_windows_processed = windows_processed_result.num_rows() > 0 ? std::stoll(windows_processed_result.get_value(0, "total")) : 0;
            
            // Windows processed in last hour
            auto windows_hour_result = QueryResult(conn->exec(
                "SELECT COUNT(*) as count FROM queen.stream_consumer_offsets "
                "WHERE last_consumed_at > NOW() - INTERVAL '1 hour'"
            ));
            int windows_last_hour = windows_hour_result.num_rows() > 0 ? std::stoi(windows_hour_result.get_value(0, "count")) : 0;
            
            // Average lease time (in seconds)
            auto avg_lease_result = QueryResult(conn->exec(
                "SELECT COALESCE(AVG(EXTRACT(EPOCH FROM (lease_expires_at - CURRENT_TIMESTAMP))), 0) as avg_seconds "
                "FROM queen.stream_leases WHERE lease_expires_at > NOW()"
            ));
            int avg_lease_time = avg_lease_result.num_rows() > 0 ? static_cast<int>(std::stod(avg_lease_result.get_value(0, "avg_seconds"))) : 0;
            
            nlohmann::json response = {
                {"totalStreams", total_streams},
                {"partitionedStreams", partitioned_streams},
                {"activeLeases", active_leases},
                {"totalConsumerGroups", total_consumer_groups},
                {"activeConsumers", active_consumers},
                {"totalWindowsProcessed", total_windows_processed},
                {"windowsLastHour", windows_last_hour},
                {"avgLeaseTime", avg_lease_time}
            };
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/streams/:streamName - Get stream details
    app->get("/api/v1/resources/streams/:streamName", [](auto* res, auto* req) {
        try {
            std::string stream_name = std::string(req->getParameter(0));
            ScopedConnection conn(global_db_pool.get());
            
            auto result = QueryResult(conn->exec_params("SELECT * FROM queen.streams WHERE name = $1", {stream_name}));
            
            if (result.num_rows() == 0) {
                send_error_response(res, "Stream not found", 404);
                return;
            }
            
            nlohmann::json stream = {
                {"id", result.get_value(0, "id")},
                {"name", result.get_value(0, "name")},
                {"namespace", result.get_value(0, "namespace")},
                {"partitioned", result.get_value(0, "partitioned") == "t"},
                {"windowType", result.get_value(0, "window_type")},
                {"windowDurationMs", std::stoll(result.get_value(0, "window_duration_ms"))},
                {"windowGracePeriodMs", std::stoll(result.get_value(0, "window_grace_period_ms"))},
                {"windowLeaseTimeoutMs", std::stoll(result.get_value(0, "window_lease_timeout_ms"))},
                {"createdAt", result.get_value(0, "created_at")},
                {"updatedAt", result.get_value(0, "updated_at")}
            };
            
            send_json_response(res, stream);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/streams/:streamName/consumers - Get consumer groups for a stream
    app->get("/api/v1/resources/streams/:streamName/consumers", [](auto* res, auto* req) {
        try {
            std::string stream_name = std::string(req->getParameter(0));
            ScopedConnection conn(global_db_pool.get());
            
            // Get stream ID
            auto stream_result = QueryResult(conn->exec_params("SELECT id FROM queen.streams WHERE name = $1", {stream_name}));
            if (stream_result.num_rows() == 0) {
                send_error_response(res, "Stream not found", 404);
                return;
            }
            std::string stream_id = stream_result.get_value(0, "id");
            
            // Get consumer groups
            auto result = QueryResult(conn->exec_params(
                "SELECT consumer_group, stream_key, "
                "last_acked_window_end::TEXT, "
                "COALESCE(total_windows_consumed, 0) as total_windows_consumed, "
                "last_consumed_at::TEXT "
                "FROM queen.stream_consumer_offsets WHERE stream_id = $1::UUID ORDER BY consumer_group, stream_key",
                {stream_id}
            ));
            
            nlohmann::json consumers = nlohmann::json::array();
            for (int i = 0; i < result.num_rows(); i++) {
                std::string last_acked = result.get_value(i, "last_acked_window_end");
                std::string last_consumed = result.get_value(i, "last_consumed_at");
                
                nlohmann::json consumer = {
                    {"consumerGroup", result.get_value(i, "consumer_group")},
                    {"streamKey", result.get_value(i, "stream_key")},
                    {"lastAckedWindowEnd", last_acked.empty() ? nullptr : nlohmann::json(last_acked)},
                    {"totalWindowsConsumed", std::stoll(result.get_value(i, "total_windows_consumed"))},
                    {"lastConsumedAt", last_consumed.empty() ? nullptr : nlohmann::json(last_consumed)}
                };
                consumers.push_back(consumer);
            }
            
            nlohmann::json response = {
                {"consumers", consumers}
            };
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // DELETE /api/v1/resources/streams/:streamName - Delete a stream
    app->del("/api/v1/resources/streams/:streamName", [](auto* res, auto* req) {
        try {
            std::string stream_name = std::string(req->getParameter(0));
            ScopedConnection conn(global_db_pool.get());
            
            auto result = QueryResult(conn->exec_params("DELETE FROM queen.streams WHERE name = $1 RETURNING id", {stream_name}));
            
            if (result.num_rows() == 0) {
                send_error_response(res, "Stream not found", 404);
                return;
            }
            
            nlohmann::json response = {
                {"success", true},
                {"message", "Stream deleted successfully"}
            };
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
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
    
    // GET /api/v1/analytics/system-metrics - System metrics time series
    app->get("/api/v1/analytics/system-metrics", [analytics_manager](auto* res, auto* req) {
        try {
            AnalyticsManager::SystemMetricsFilters filters;
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.hostname = get_query_param(req, "hostname");
            filters.worker_id = get_query_param(req, "workerId");
            
            auto response = analytics_manager->get_system_metrics(filters);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/consumer-groups - Consumer groups
    app->get("/api/v1/consumer-groups", [analytics_manager](auto* res, auto* req) {
        try {
            auto response = analytics_manager->get_consumer_groups();
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

// Global shared resources initialized in worker_thread

// Timer callback for processing response queue
static void response_timer_callback(us_timer_t* timer) {
    auto* response_queue = *(queen::ResponseQueue**)us_timer_ext(timer);
    
    queen::ResponseQueue::ResponseItem item;
    int processed = 0;
    
    // Process up to 50 responses per timer tick to avoid blocking event loop
    while (processed < 50 && response_queue->pop(item)) {
        bool sent = global_response_registry->send_response(
            item.request_id, item.data, item.is_error, item.status_code);
        
        if (!sent) {
            spdlog::debug("Response {} was aborted or expired", item.request_id);
        }
        processed++;
    }
    
    // Cleanup expired responses every 200 timer ticks (~5 seconds at 25ms intervals)
    // Use 120s timeout to allow for long-polling requests up to 60s + buffer
    static int cleanup_counter = 0;
    if (++cleanup_counter >= 200) {
        global_response_registry->cleanup_expired(std::chrono::seconds(120));
        cleanup_counter = 0;
    }
}

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
        std::call_once(global_pool_init_flag, [&config, num_workers]() {
            // Calculate global pool sizes with 5% safety buffer
            int total_connections = static_cast<int>(config.database.pool_size * 0.95); // 95% of total
            int total_db_threads = total_connections; // 1:1 ratio
            int system_threads = 4; // Small pool for background tasks (metrics, cleanup, etc.)
            
            spdlog::info("Initializing GLOBAL shared resources:");
            spdlog::info("  - Total DB connections: {} (95% of {})", total_connections, config.database.pool_size);
            spdlog::info("  - DB ThreadPool threads: {}", total_db_threads);
            spdlog::info("  - System ThreadPool threads: {}", system_threads);
            spdlog::info("  - Number of workers: {}", num_workers);
            
            // Create global connection pool
            global_db_pool = std::make_shared<DatabasePool>(
                config.database.connection_string(),
                total_connections,
                config.database.pool_acquisition_timeout,
                config.database.statement_timeout,
                config.database.lock_timeout,
                config.database.idle_timeout
            );
            
            // Create global DB operations ThreadPool
            global_db_thread_pool = std::make_shared<astp::ThreadPool>(total_db_threads);
            
            // Create global System operations ThreadPool (for metrics, cleanup, etc.)
            global_system_thread_pool = std::make_shared<astp::ThreadPool>(system_threads);
            
            // Create per-worker response queues
            num_workers_global = num_workers;
            worker_response_queues.resize(num_workers);
            for (int i = 0; i < num_workers; i++) {
                worker_response_queues[i] = std::make_shared<queen::ResponseQueue>();
                spdlog::info("  - Created response queue for worker {}", i);
            }
            
            // Create global response registry (shared across workers)
            global_response_registry = std::make_shared<queen::ResponseRegistry>();
            
            // Create global poll intention registry (shared by all workers)
            global_poll_intention_registry = std::make_shared<queen::PollIntentionRegistry>();
            
            // Create global stream poll registry (shared by all workers)
            global_stream_poll_registry = std::make_shared<queen::StreamPollIntentionRegistry>();
            
            // Create global stream manager (shared by all workers)
            global_stream_manager = std::make_shared<queen::StreamManager>(
                global_db_pool,
                global_db_thread_pool,
                worker_response_queues,
                global_stream_poll_registry,
                global_response_registry
            );
            
            // Get system info for metrics
            global_system_info = SystemInfo::get_current();
            global_system_info.port = config.server.port;
            
            spdlog::info("System info: hostname={}, port={}", 
                         global_system_info.hostname, 
                         global_system_info.port);
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
                
                // Start metrics collector (samples every 1s, aggregates every 60s)
                spdlog::info("[Worker 0] Starting background metrics collector...");
                global_metrics_collector = std::make_shared<queen::MetricsCollector>(
                    global_db_pool,
                    global_db_thread_pool,
                    global_system_thread_pool,
                    global_system_info.hostname,
                    global_system_info.port,
                    config.server.worker_id,
                    1000,  // Sample every 1 second
                    60     // Aggregate and save every 60 seconds
                );
                global_metrics_collector->start();
                
                // Start retention service (cleanup old messages, partitions, metrics)
                spdlog::info("[Worker 0] Starting background retention service...");
                global_retention_service = std::make_shared<queen::RetentionService>(
                    global_db_pool,
                    global_db_thread_pool,
                    global_system_thread_pool,
                    config.jobs.retention_interval,
                    config.jobs.retention_batch_size,
                    config.jobs.partition_cleanup_days,
                    config.jobs.metrics_retention_days
                );
                global_retention_service->start();
                
                // Start eviction service (evict messages exceeding max_wait_time)
                spdlog::info("[Worker 0] Starting background eviction service...");
                global_eviction_service = std::make_shared<queen::EvictionService>(
                    global_db_pool,
                    global_db_thread_pool,
                    global_system_thread_pool,
                    config.jobs.eviction_interval,
                    config.jobs.eviction_batch_size
                );
                global_eviction_service->start();
            }
        } else {
            spdlog::warn("[Worker {}] Database connection: UNAVAILABLE (Pool: 0/{}) - Will use file buffer for failover", 
                         worker_id, pool_stats.total);
            spdlog::warn("[Worker {}] Server will operate with file buffer until PostgreSQL becomes available", worker_id);
        }
        
        // Initialize long-polling poll workers (Worker 0 only, regardless of DB status)
        if (worker_id == 0) {
            spdlog::info("[Worker 0] Starting long-polling poll workers...");
            queen::init_long_polling(
                global_db_thread_pool,
                global_poll_intention_registry,
                queue_manager,
                worker_response_queues,  // All worker queues (poll workers will route to correct one)
                2,  // 2 poll workers
                config.queue.poll_worker_interval,
                config.queue.poll_db_interval,
                config.queue.backoff_threshold,
                config.queue.backoff_multiplier,
                config.queue.max_poll_interval,
                config.queue.backoff_cleanup_inactive_threshold
            );
            
            // Initialize stream long-polling poll workers
            spdlog::info("[Worker 0] Starting stream long-polling poll workers...");
            queen::init_stream_long_polling(
                global_db_thread_pool,
                global_stream_poll_registry,
                global_stream_manager,
                worker_response_queues,
                2,  // 2 stream poll workers
                100,   // poll_worker_interval_ms (how often to check registry)
                1000,  // poll_stream_interval_ms (min time between stream checks per group)
                5,     // backoff_threshold (consecutive empty checks before backoff)
                2.0,   // backoff_multiplier
                5000,  // max_poll_interval_ms
                config.queue.backoff_cleanup_inactive_threshold
            );
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
        
        // Connect file buffer to queue manager for maintenance mode
        queue_manager->set_file_buffer_manager(file_buffer);
        spdlog::info("[Worker {}] File buffer connected to queue manager for maintenance mode", worker_id);
        
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
        
        // Setup response timer for this worker (each worker has its own timer and queue)
        // CRITICAL: Timer must be created AFTER listen() to ensure event loop is properly initialized
        spdlog::info("[Worker {}] Setting up response timer...", worker_id);
        us_timer_t* response_timer = us_create_timer((us_loop_t*)uWS::Loop::get(), 0, sizeof(queen::ResponseQueue*));
        *(queen::ResponseQueue**)us_timer_ext(response_timer) = worker_response_queues[worker_id].get();
        
        // Poll using configured interval for good balance between latency and CPU usage
        int timer_interval = config.queue.response_timer_interval_ms;
        us_timer_set(response_timer, response_timer_callback, timer_interval, timer_interval);
        spdlog::info("[Worker {}] Response timer configured to process own queue ({}ms interval)", worker_id, timer_interval);
        
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

