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
#include <csignal>
#include <sstream>
#include <iomanip>

namespace queen {

// Global shutdown flag
std::atomic<bool> g_shutdown{false};

// Helper functions (static, no class needed)
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

// Read JSON body helper - fixed double-free issue with shared_ptr
static void read_json_body(uWS::HttpResponse<false>* res,
                          std::function<void(const nlohmann::json&)> callback,
                          std::function<void(const std::string&)> error_callback) {
    auto buffer = std::make_shared<std::string>();
    auto completed = std::make_shared<bool>(false);
    
    res->onData([callback, error_callback, buffer, completed](std::string_view chunk, bool is_last) {
        buffer->append(chunk);
        
        if (is_last && !*completed) {
            *completed = true;  // Mark as completed
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
        *completed = true;  // Just mark aborted, shared_ptr cleans up automatically
    });
}

// Query parameter helpers
static std::string get_query_param(uWS::HttpRequest* req, const std::string& key, const std::string& default_value = "") {
    std::string query = std::string(req->getQuery());
    
    // Simple query parsing
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

// Worker thread main - each thread is completely isolated!
static void worker_thread(const Config& config, int thread_id, int num_threads) {
    spdlog::info("[Worker {}] Thread started", thread_id);
    
    try {
        
        // Thread-local database pool
        // Divide total pool size among threads
        int pool_per_thread = std::max(3, config.database.pool_size / num_threads);
        
        spdlog::info("[Worker {}] Creating DB pool with {} connections", thread_id, pool_per_thread);
        
        auto db_pool = std::make_shared<DatabasePool>(
            config.database.connection_string(),
            pool_per_thread
        );
        
        // Thread-local queue manager
        auto queue_manager = std::make_shared<QueueManager>(db_pool, config.queue);
        
        // Only first thread initializes schema
        if (thread_id == 0) {
            spdlog::info("[Worker 0] Initializing database schema...");
            queue_manager->initialize_schema();
        }
        
        // Thread-local App instance
        uWS::App app;
        
        spdlog::info("[Worker {}] Setting up routes...", thread_id);
        
        // ========================================================================
        // ROUTES - All single-threaded, no defer needed!
        // ========================================================================
        
        // CORS preflight
        app.options("/*", [](auto* res, auto* req) {
            setup_cors_headers(res);
            res->writeStatus("204");
            res->end();
        });
        
        // Configure queue
        app.post("/api/v1/configure", [queue_manager](auto* res, auto* req) {
            read_json_body(res,
                [res, queue_manager](const nlohmann::json& body) {
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
                        
                        spdlog::debug("Calling configure_queue...");
                        
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
        app.post("/api/v1/push", [queue_manager, thread_id](auto* res, auto* req) {
            read_json_body(res,
                [res, queue_manager, thread_id](const nlohmann::json& body) {
                    try {
                        if (!body.contains("items") || !body["items"].is_array()) {
                            send_error_response(res, "items array is required", 400);
                            return;
                        }
                        
                        std::vector<PushItem> items;
                        std::string first_queue = "";
                        std::string first_partition = "";
                        
                        for (const auto& item_json : body["items"]) {
                            if (!item_json.contains("queue") || !item_json["queue"].is_string()) {
                                send_error_response(res, "Each item must have a queue", 400);
                                return;
                            }
                            
                            PushItem item;
                            item.queue = item_json["queue"];
                            item.partition = item_json.value("partition", "Default");
                            item.payload = item_json.value("payload", nlohmann::json{});
                            
                            if (first_queue.empty()) {
                                first_queue = item.queue;
                                first_partition = item.partition;
                            }
                            
                            if (item_json.contains("transactionId")) {
                                item.transaction_id = item_json["transactionId"];
                            }
                            if (item_json.contains("traceId")) {
                                item.trace_id = item_json["traceId"];
                            }
                            
                            items.push_back(std::move(item));
                        }
                        
                        spdlog::info("[Worker {}] PUSH START: {} items to {}/{}", thread_id, items.size(), first_queue, first_partition);
                        
                        // Single-threaded - blocking is OK!
                        auto results = queue_manager->push_messages(items);
                        
                        spdlog::info("[Worker {}] PUSH COMPLETE: {} items", thread_id, results.size());
                        
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
        
        // POP from queue/partition
        app.get("/api/v1/pop/queue/:queue/partition/:partition", [queue_manager, config, thread_id](auto* res, auto* req) {
            // CRITICAL: Attach abort handler FIRST!
            auto aborted = std::make_shared<bool>(false);
            res->onAborted([aborted]() {
                *aborted = true;
            });
            
            try {
                std::string queue_name = std::string(req->getParameter(0));
                std::string partition_name = std::string(req->getParameter(1));
                
                std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
                if (consumer_group.empty()) {
                    consumer_group = get_query_param(req, "consumer_group", "__QUEUE_MODE__");
                }
                
                PopOptions options;
                options.wait = get_query_param_bool(req, "wait", false);
                options.timeout = get_query_param_int(req, "timeout", config.queue.default_timeout);
                options.batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
                
                spdlog::info("[Worker {}] POP START: {}/{}, batch={}", thread_id, queue_name, partition_name, options.batch);
                
                try {
                    // Single-threaded - blocking is OK! Each thread has own event loop
                    auto result = queue_manager->pop_messages(queue_name, partition_name, consumer_group, options);
                    
                    spdlog::info("[Worker {}] POP COMPLETE: {}/{}, got {} messages", thread_id, queue_name, partition_name, result.messages.size());
                    
                    // Check if aborted before building response
                    if (*aborted) {
                        spdlog::debug("[Worker {}] POP request aborted, skipping response", thread_id);
                        return;
                    }
                
                    if (result.messages.empty()) {
                        setup_cors_headers(res);
                        res->writeStatus("204");
                        res->end();
                    } else {
                    nlohmann::json response = {{"messages", nlohmann::json::array()}};
                    
                    for (const auto& msg : result.messages) {
                        // Format timestamp
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
                    }
                } catch (const std::exception& e) {
                    if (!*aborted) {
                        spdlog::error("[Worker {}] POP exception: {}", thread_id, e.what());
                        send_error_response(res, e.what(), 500);
                    }
                }
                
            } catch (const std::exception& e) {
                if (!*aborted) {
                    spdlog::error("[Worker {}] POP outer exception: {}", thread_id, e.what());
                    send_error_response(res, e.what(), 500);
                }
            }
        });
        
        // ACK batch
        app.post("/api/v1/ack/batch", [queue_manager, thread_id](auto* res, auto* req) {
            read_json_body(res,
                [res, queue_manager, thread_id](const nlohmann::json& body) {
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
                        
                        spdlog::info("[Worker {}] ACK START: {} items", thread_id, ack_items.size());
                        
                        // Single-threaded - blocking is OK!
                        auto results = queue_manager->acknowledge_messages(ack_items, consumer_group);
                        
                        spdlog::info("[Worker {}] ACK COMPLETE: {} items processed", thread_id, results.size());
                        
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
        app.del("/api/v1/resources/queues/:queue", [queue_manager](auto* res, auto* req) {
            try {
                std::string queue_name = std::string(req->getParameter(0));
                
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
        app.get("/api/v1/pop/queue/:queue", [queue_manager, config](auto* res, auto* req) {
            try {
                std::string queue_name = std::string(req->getParameter(0));
                std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
                
                PopOptions options;
                options.wait = get_query_param_bool(req, "wait", false);
                options.timeout = get_query_param_int(req, "timeout", config.queue.default_timeout);
                options.batch = get_query_param_int(req, "batch", config.queue.default_batch_size);
                
                auto result = queue_manager->pop_messages(queue_name, std::nullopt, consumer_group, options);
                
                if (result.messages.empty()) {
                    setup_cors_headers(res);
                    res->writeStatus("204");
                    res->end();
                    return;
                }
                
                // Build response (same as partition pop)
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
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
            }
        });
        
        // Single ACK
        app.post("/api/v1/ack", [queue_manager](auto* res, auto* req) {
            read_json_body(res,
                [res, queue_manager](const nlohmann::json& body) {
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
                        
                        auto ack_result = queue_manager->acknowledge_message(transaction_id, status, error, consumer_group, lease_id);
                        
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
                        
                    } catch (const std::exception& e) {
                        send_error_response(res, e.what(), 500);
                    }
                },
                [res](const std::string& error) {
                    send_error_response(res, error, 400);
                }
            );
        });
        
        // Health check
        app.get("/health", [queue_manager](auto* res, auto* req) {
            try {
                bool healthy = queue_manager->health_check();
                
                nlohmann::json response = {
                    {"status", healthy ? "healthy" : "unhealthy"},
                    {"database", healthy ? "connected" : "disconnected"},
                    {"server", "C++ Queen Server (Multi-App)"},
                    {"version", "1.0.0"}
                };
                
                send_json_response(res, response, healthy ? 200 : 503);
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
            }
        });
        
        // Listen on the same port (SO_REUSEPORT kernel load balancing)
        app.listen(config.server.host, config.server.port, [thread_id, config](auto* listen_socket) {
            if (listen_socket) {
                spdlog::info("✅ [Worker {}] Listening on {}:{}", 
                           thread_id, config.server.host, config.server.port);
            } else {
                spdlog::error("❌ [Worker {}] Failed to listen on {}:{}", 
                            thread_id, config.server.host, config.server.port);
            }
        });
        
        // Run event loop - blocks in this thread forever
        spdlog::info("[Worker {}] Entering event loop", thread_id);
        app.run();
        
        spdlog::info("[Worker {}] Event loop exited gracefully", thread_id);
        
    } catch (const std::exception& e) {
        spdlog::error("[Worker {}] FATAL exception: {}", thread_id, e.what());
    } catch (...) {
        spdlog::error("[Worker {}] FATAL unknown exception!", thread_id);
    }
    
    spdlog::info("[Worker {}] Thread exiting", thread_id);
}

// Main server start function
bool start_multi_app_server(const Config& config) {
    // Initialize encryption globally
    bool encryption_enabled = init_encryption();
    spdlog::info("Encryption: {}", encryption_enabled ? "enabled" : "disabled");
    
    // Multi-App: Platform-specific threading
    #ifdef __APPLE__
        // macOS: SO_REUSEPORT doesn't load balance, use 1 worker
        int num_threads = 1;
        spdlog::info("macOS detected - using single worker (SO_REUSEPORT not supported)");
    #else
        // Linux: SO_REUSEPORT enables kernel load balancing
        int num_threads = std::min(10, static_cast<int>(std::thread::hardware_concurrency()));
        spdlog::info("Linux detected - using {} workers with kernel load balancing", num_threads);
    #endif
    
    int pool_per_thread = std::max(5, config.database.pool_size / num_threads);
    
    spdlog::info("🚀 Starting {} worker threads (multi-App pattern)", num_threads);
    spdlog::info("📊 Total capacity: {} threads × {} DB connections = {} total connections", 
                num_threads, pool_per_thread, num_threads * pool_per_thread);
    
    // Spawn worker threads - each completely isolated!
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker_thread, config, i, num_threads);
    }
    
    spdlog::info("✅ All {} workers started - server ready!", num_threads);
    spdlog::info("🎯 OS kernel will load-balance connections across workers");
    
    // Main thread waits for shutdown signal
    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    spdlog::info("Shutdown signal received - waiting for workers to finish");
    
    // Wait for all threads
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    spdlog::info("✅ All workers stopped - clean shutdown");
    return true;
}

} // namespace queen

