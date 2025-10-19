#include "queen/database.hpp"
#include "queen/queue_manager.hpp"
#include "queen/config.hpp"
#include "queen/encryption.hpp"
#include <App.h>
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <chrono>

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
        spdlog::info("[Worker {}] <<< POP END (timeout): {}/{}, got 0 msgs, took {}ms, retries={}", 
                    state->worker_id, state->queue_name, state->partition_name, duration_ms, state->retry_count);
        
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
            spdlog::info("[Worker {}] <<< POP END (found): {}/{}, got {} msgs, took {}ms, retries={}", 
                        state->worker_id, state->queue_name, state->partition_name, 
                        result.messages.size(), duration_ms, state->retry_count);
            
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

// Setup routes for a worker app
static void setup_worker_routes(uWS::App* app, 
                                std::shared_ptr<QueueManager> queue_manager,
                                const Config& config,
                                int worker_id) {
    
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
                                {"completedRetentionSeconds", 0},
                                {"deadLetterQueue", false},
                                {"delayedProcessing", 0},
                                {"dlqAfterMaxRetries", false},
                                {"leaseTime", options.lease_time},
                                {"maxSize", options.max_size},
                                {"priority", 0},
                                {"retentionEnabled", false},
                                {"retentionSeconds", 0},
                                {"retryDelay", options.retry_delay},
                                {"retryLimit", options.retry_limit},
                                {"ttl", options.ttl},
                                {"windowBuffer", 0}
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
    
    // Push messages
    app->post("/api/v1/push", [queue_manager, worker_id](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager, worker_id](const nlohmann::json& body) {
                try {
                    if (!body.contains("items") || !body["items"].is_array()) {
                        send_error_response(res, "items array is required", 400);
                        return;
                    }
                    
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
                    
                    spdlog::info("[Worker {}] PUSH: {} items", worker_id, items.size());
                    auto results = queue_manager->push_messages(items);
                    
                    nlohmann::json response = {{"messages", nlohmann::json::array()}};
                    for (const auto& result : results) {
                        nlohmann::json msg_result = {
                            {"id", result.message_id.value_or("")},
                            {"transactionId", result.transaction_id},
                            {"traceId", result.trace_id.has_value() ? nlohmann::json(*result.trace_id) : nlohmann::json(nullptr)},
                            {"status", result.status}
                        };
                        if (result.error) {
                            msg_result["error"] = *result.error;
                        }
                        response["messages"].push_back(msg_result);
                    }
                    
                    send_json_response(res, response, 201);
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
            
            spdlog::info("[Worker {}] >>> POP START: {}/{}, batch={}, wait={}, timeout={}ms", 
                        worker_id, queue_name, partition_name, batch, wait, timeout_ms);
            
            auto start_time = std::chrono::steady_clock::now();
            
            // Try once immediately (non-blocking)
            PopOptions options;
            options.wait = false;
            options.timeout = 0;
            options.batch = batch;
            
            auto result = queue_manager->pop_messages(queue_name, partition_name, consumer_group, options);
            
            if (!result.messages.empty()) {
                // Found messages immediately!
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                spdlog::info("[Worker {}] <<< POP END (immediate): {}/{}, got {} msgs, took {}ms", 
                            worker_id, queue_name, partition_name, result.messages.size(), duration_ms);
                
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
                spdlog::info("[Worker {}] <<< POP END (no-wait): {}/{}, got 0 msgs", 
                            worker_id, queue_name, partition_name);
                
                if (!*aborted) {
                    setup_cors_headers(res);
                    res->writeStatus("204");
                    res->end();
                }
                return;
            }
            
            // Start async polling (non-blocking!)
            spdlog::debug("[Worker {}] POP starting async polling for {}/{}", 
                         worker_id, queue_name, partition_name);
            
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
    
    // ACK batch
    app->post("/api/v1/ack/batch", [queue_manager, worker_id](auto* res, auto* req) {
        read_json_body(res,
            [res, queue_manager, worker_id](const nlohmann::json& body) {
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
                        
                        ack_items.push_back(ack);
                    }
                    
                    spdlog::info("[Worker {}] >>> ACK START: {} items", worker_id, ack_items.size());
                    
                    auto ack_start = std::chrono::steady_clock::now();
                    auto results = queue_manager->acknowledge_messages(ack_items, consumer_group);
                    auto ack_end = std::chrono::steady_clock::now();
                    auto ack_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ack_end - ack_start).count();
                    
                    spdlog::info("[Worker {}] <<< ACK END: {} items processed, took {}ms", 
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
}

// Worker thread function
static void worker_thread(const Config& config, int worker_id, int num_workers,
                         std::mutex& init_mutex,
                         std::vector<uWS::App*>& worker_apps) {
    spdlog::info("[Worker {}] Starting...", worker_id);
    
    try {
        // Thread-local database pool
        int pool_per_thread = std::max(5, config.database.pool_size / num_workers);
        auto db_pool = std::make_shared<DatabasePool>(
            config.database.connection_string(),
            pool_per_thread
        );
        
        // Thread-local queue manager
        auto queue_manager = std::make_shared<QueueManager>(db_pool, config.queue);
        
        // Only first worker initializes schema
        if (worker_id == 0) {
            spdlog::info("[Worker 0] Initializing database schema...");
            queue_manager->initialize_schema();
        }
        
        // Create worker App
        auto worker_app = new uWS::App();
        
        // Setup routes
        setup_worker_routes(worker_app, queue_manager, config, worker_id);
        
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
    int num_workers = std::min(10, static_cast<int>(std::thread::hardware_concurrency()));
    spdlog::info("🚀 Starting acceptor/worker pattern with {} workers", num_workers);
    spdlog::info("✅ This pattern works on ALL platforms (macOS, Linux, Windows)");
    
    // Shared data for worker registration
    std::mutex init_mutex;
    std::vector<uWS::App*> worker_apps;
    std::vector<std::thread> worker_threads;
    
    // Create worker threads
    for (int i = 0; i < num_workers; i++) {
        worker_threads.emplace_back(worker_thread, config, i, num_workers,
                                   std::ref(init_mutex), std::ref(worker_apps));
    }
    
    // Wait for all workers to register
    spdlog::info("Waiting for workers to initialize...");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Create acceptor app
    spdlog::info("Creating acceptor app...");
    auto acceptor = new uWS::App();
    
    // Register all worker apps with acceptor
    {
        std::lock_guard<std::mutex> lock(init_mutex);
        for (auto* worker : worker_apps) {
            acceptor->addChildApp(worker);
        }
        spdlog::info("✅ Registered {} worker apps with acceptor", worker_apps.size());
    }
    
    // Acceptor listens on port and distributes in round-robin
    acceptor->listen(config.server.host, config.server.port, [config, num_workers](auto* listen_socket) {
        if (listen_socket) {
            spdlog::info("✅ Acceptor listening on {}:{}", config.server.host, config.server.port);
            spdlog::info("🎯 Round-robin load balancing across {} workers", num_workers);
        } else {
            spdlog::error("❌ Failed to listen on {}:{}", config.server.host, config.server.port);
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
    
    spdlog::info("✅ Clean shutdown");
    return true;
}

} // namespace queen

