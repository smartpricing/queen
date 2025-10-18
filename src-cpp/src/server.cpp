#include "queen/server.hpp"
#include "queen/encryption.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>

namespace queen {

QueenServer::QueenServer(const Config& config) : config_(config) {
    // Multi-App pattern - each worker thread will create its own App
}

QueenServer::~QueenServer() {
    spdlog::info("Server destructor - waiting for worker threads");
    stop();
    
    // Wait for all worker threads to finish
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    spdlog::info("All worker threads stopped");
}

bool QueenServer::initialize() {
    // Initialize encryption service globally (shared across threads)
    bool encryption_enabled = init_encryption();
    spdlog::info("Encryption: {}", encryption_enabled ? "enabled" : "disabled");
    
    spdlog::info("Queen C++ server initialized - ready to spawn worker threads");
    return true;
}

// Worker thread main function - each thread is completely isolated
void QueenServer::worker_thread_main(Config config, int thread_id) {
    try {
        spdlog::info("Worker thread {} starting...", thread_id);
        
        // Create thread-local database pool (small size per thread)
        int pool_size_per_thread = 5;  // Each thread gets 5 connections
        auto db_pool = std::make_shared<DatabasePool>(
            config.database.connection_string(),
            pool_size_per_thread
        );
        
        // Create thread-local queue manager
        auto queue_manager = std::make_shared<QueueManager>(db_pool, config.queue);
        
        // Initialize schema (only first thread needs to, but idempotent)
        if (thread_id == 0) {
            queue_manager->initialize_schema();
        }
        
        // Create thread-local App instance
        uWS::App app;
        
        // Setup all routes on this App
        setup_app_routes(app, queue_manager, config);
        
        // Listen on the same port (SO_REUSEPORT allows this)
        app.listen(config.server.host, config.server.port, [thread_id, config](auto* listen_socket) {
            if (listen_socket) {
                spdlog::info("✅ Worker {} listening on {}:{}", 
                           thread_id, config.server.host, config.server.port);
            } else {
                spdlog::error("❌ Worker {} failed to listen on {}:{}", 
                            thread_id, config.server.host, config.server.port);
            }
        });
        
        // Run event loop (blocks forever in this thread)
        spdlog::info("Worker {} entering event loop", thread_id);
        app.run();
        
        spdlog::info("Worker {} event loop exited", thread_id);
        
    } catch (const std::exception& e) {
        spdlog::error("Worker thread {} error: {}", thread_id, e.what());
    }
}

void QueenServer::setup_cors_headers(uWS::HttpResponse<false>* res) {
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res->writeHeader("Access-Control-Max-Age", "86400");
}

void QueenServer::send_json_response(uWS::HttpResponse<false>* res, const nlohmann::json& json, int status_code) {
    setup_cors_headers(res);
    res->writeHeader("Content-Type", "application/json");
    res->writeStatus(std::to_string(status_code));
    res->end(json.dump());
}

void QueenServer::send_error_response(uWS::HttpResponse<false>* res, const std::string& error, int status_code) {
    nlohmann::json error_json = {{"error", error}};
    send_json_response(res, error_json, status_code);
}

std::string QueenServer::get_query_param(uWS::HttpRequest* req, const std::string& key, const std::string& default_value) {
    std::string query = std::string(req->getQuery());
    auto params = HttpUtils::parse_query_string(query);
    
    if (params.contains(key) && params[key].is_string()) {
        return params[key].get<std::string>();
    }
    return default_value;
}

int QueenServer::get_query_param_int(uWS::HttpRequest* req, const std::string& key, int default_value) {
    std::string value = get_query_param(req, key);
    if (value.empty()) return default_value;
    
    try {
        return std::stoi(value);
    } catch (...) {
        return default_value;
    }
}

bool QueenServer::get_query_param_bool(uWS::HttpRequest* req, const std::string& key, bool default_value) {
    std::string value = get_query_param(req, key);
    if (value.empty()) return default_value;
    
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    return value == "true" || value == "1";
}

void QueenServer::read_json_body(uWS::HttpResponse<false>* res, 
                                std::function<void(const nlohmann::json&)> callback,
                                std::function<void(const std::string&)> error_callback) {
    
    std::string* buffer = new std::string();
    
    res->onData([res, callback, error_callback, buffer](std::string_view chunk, bool is_last) {
        buffer->append(chunk);
        
        if (is_last) {
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
            delete buffer;
        }
    });
    
    res->onAborted([buffer]() {
        delete buffer;
    });
}

void QueenServer::handle_configure(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    read_json_body(res, 
        [this, res](const nlohmann::json& body) {
            try {
                if (!body.contains("queue") || !body["queue"].is_string()) {
                    send_error_response(res, "queue is required", 400);
                    return;
                }
                
                std::string queue_name = body["queue"];
                std::string namespace_name = "";
                std::string task_name = "";
                
                if (body.contains("namespace") && !body["namespace"].is_null() && body["namespace"].is_string()) {
                    namespace_name = body["namespace"];
                }
                if (body.contains("task") && !body["task"].is_null() && body["task"].is_string()) {
                    task_name = body["task"];
                }
                
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
                
                bool success = queue_manager_->configure_queue(queue_name, options, namespace_name, task_name);
                
                if (success) {
                    nlohmann::json response = {
                        {"queue", queue_name},
                        {"namespace", namespace_name},
                        {"task", task_name},
                        {"configured", true},
                        {"options", {
                            {"leaseTime", options.lease_time},
                            {"maxSize", options.max_size},
                            {"ttl", options.ttl},
                            {"retryLimit", options.retry_limit},
                            {"retryDelay", options.retry_delay},
                            {"deadLetterQueue", options.dead_letter_queue},
                            {"dlqAfterMaxRetries", options.dlq_after_max_retries},
                            {"priority", options.priority},
                            {"delayedProcessing", options.delayed_processing},
                            {"windowBuffer", options.window_buffer},
                            {"retentionSeconds", options.retention_seconds},
                            {"completedRetentionSeconds", options.completed_retention_seconds},
                            {"retentionEnabled", options.retention_enabled}
                        }}
                    };
                    send_json_response(res, response, 201);
                } else {
                    send_error_response(res, "Failed to configure queue", 500);
                }
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
            }
        },
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        }
    );
}

void QueenServer::handle_push(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    read_json_body(res,
        [this, res](const nlohmann::json& body) {
            try {
                spdlog::info("=== PUSH REQUEST RECEIVED ===");
                spdlog::info("Request body: {}", body.dump(2));
                
                if (!body.contains("items") || !body["items"].is_array()) {
                    send_error_response(res, "items array is required", 400);
                    return;
                }
                
                std::vector<PushItem> items;
                for (const auto& item_json : body["items"]) {
                    if (!item_json.contains("queue") || !item_json["queue"].is_string()) {
                        send_error_response(res, "Each item must have a queue", 400);
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
                    
                    spdlog::info("Parsed PushItem: queue='{}', partition='{}', payload='{}'", 
                                item.queue, item.partition, item.payload.dump());
                    
                    items.push_back(std::move(item));
                }
                
                auto results = queue_manager_->push_messages(items);
                
                // Check if any message failed due to queue not existing - this should be a top-level error
                for (const auto& result : results) {
                    if (result.status == "failed" && result.error && 
                        result.error->find("does not exist") != std::string::npos) {
                        send_error_response(res, *result.error, 400);
                        return;
                    }
                }
                
                nlohmann::json response = {
                    {"messages", nlohmann::json::array()}
                };
                
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
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        }
    );
}

void QueenServer::handle_pop_queue_partition(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    spdlog::info("=== POP REQUEST RECEIVED (partition) ===");
    
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() {
        spdlog::warn("POP request aborted by client");
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
        options.timeout = get_query_param_int(req, "timeout", config_.queue.default_timeout);
        options.batch = get_query_param_int(req, "batch", config_.queue.default_batch_size);
        
        spdlog::info("POP params: queue={}, partition={}, batch={}, wait={}", 
                    queue_name, partition_name, options.batch, options.wait);
        
        // No artificial batch limits - handle any size with streaming
        
        std::string sub_mode = get_query_param(req, "subscriptionMode");
        std::string sub_from = get_query_param(req, "subscriptionFrom");
        if (!sub_mode.empty()) options.subscription_mode = sub_mode;
        if (!sub_from.empty()) options.subscription_from = sub_from;
        
        spdlog::info("Pushing POP task to thread pool (queue size: {})", thread_pool_->queue_size());
        
        // ⚡ Offload to thread pool - worker does ONLY database work
        thread_pool_->push([this, res, queue_name, partition_name, consumer_group, options, aborted]() {
            if (*aborted || is_shutting_down_.load()) return;
            
            spdlog::info("Worker thread: Starting POP for queue={}, partition={}, batch={}", 
                        queue_name, partition_name, options.batch);
            
            try {
                // Worker thread: ONLY do blocking database work
                auto result = queue_manager_->pop_messages(queue_name, partition_name, consumer_group, options);
                
                spdlog::info("Worker thread: POP completed, got {} messages", result.messages.size());
                
                if (*aborted || is_shutting_down_.load()) {
                    spdlog::debug("Request aborted after POP, skipping defer");
                    return;
                }
                
                // ✅ CRITICAL: Use Loop::defer to get back to event loop thread!
                // This is the ONLY thread-safe way to use res
                uWS::Loop::get()->defer([this, res, result, aborted]() {
                    // NOW in event loop thread - safe to use res!
                    if (*aborted) {
                        spdlog::debug("Request aborted in defer callback");
                        return;
                    }
                    
                    try {
                        if (result.messages.empty()) {
                            res->cork([this, res]() {
                                setup_cors_headers(res);
                                res->writeStatus("204");
                                res->end();
                            });
                        } else {
                            // Build response JSON
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
                            
                            // Send response in event loop thread (safe!)
                            res->cork([this, res, response]() {
                                send_json_response(res, response);
                            });
                        }
                    } catch (const std::exception& e) {
                        spdlog::error("Error in defer callback: {}", e.what());
                        res->cork([this, res, e]() {
                            send_error_response(res, e.what(), 500);
                        });
                    }
                });
                
            } catch (const std::exception& e) {
                spdlog::error("Error in worker thread: {}", e.what());
                
                if (*aborted) return;
                
                // Defer error response back to event loop
                uWS::Loop::get()->defer([this, res, e, aborted]() {
                    if (*aborted) return;
                    
                    res->cork([this, res, e]() {
                        send_error_response(res, e.what(), 500);
                    });
                });
            }
        });
        
    } catch (const std::exception& e) {
        if (*aborted) return;
        send_error_response(res, e.what(), 500);
    }
}

void QueenServer::handle_pop_queue(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    try {
        std::string queue_name = std::string(req->getParameter(0));
        
        std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
        if (consumer_group.empty()) {
            consumer_group = get_query_param(req, "consumer_group", "__QUEUE_MODE__");
        }
        
        PopOptions options;
        options.wait = get_query_param_bool(req, "wait", false);
        options.timeout = get_query_param_int(req, "timeout", config_.queue.default_timeout);
        options.batch = get_query_param_int(req, "batch", config_.queue.default_batch_size);
        
        std::string sub_mode = get_query_param(req, "subscriptionMode");
        std::string sub_from = get_query_param(req, "subscriptionFrom");
        if (!sub_mode.empty()) options.subscription_mode = sub_mode;
        if (!sub_from.empty()) options.subscription_from = sub_from;
        
        auto result = queue_manager_->pop_messages(queue_name, std::nullopt, consumer_group, options);
        
        if (result.messages.empty()) {
            setup_cors_headers(res);
            res->writeStatus("204");
            res->end();
        } else {
            nlohmann::json response = {
                {"messages", nlohmann::json::array()}
            };
            
            for (const auto& msg : result.messages) {
                // Format timestamp for createdAt with milliseconds
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
        send_error_response(res, e.what(), 500);
    }
}

void QueenServer::handle_pop_filtered(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    try {
        std::string namespace_param = get_query_param(req, "namespace");
        std::string task_param = get_query_param(req, "task");
        std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
        if (consumer_group.empty()) {
            consumer_group = get_query_param(req, "consumer_group", "__QUEUE_MODE__");
        }
        
        PopOptions options;
        options.wait = get_query_param_bool(req, "wait", false);
        options.timeout = get_query_param_int(req, "timeout", config_.queue.default_timeout);
        options.batch = get_query_param_int(req, "batch", config_.queue.default_batch_size);
        
        std::string sub_mode = get_query_param(req, "subscriptionMode");
        std::string sub_from = get_query_param(req, "subscriptionFrom");
        if (!sub_mode.empty()) options.subscription_mode = sub_mode;
        if (!sub_from.empty()) options.subscription_from = sub_from;
        
        std::optional<std::string> namespace_opt = namespace_param.empty() ? std::nullopt : std::optional<std::string>(namespace_param);
        std::optional<std::string> task_opt = task_param.empty() ? std::nullopt : std::optional<std::string>(task_param);
        
        auto result = queue_manager_->pop_with_namespace_task(namespace_opt, task_opt, consumer_group, options);
        
        if (result.messages.empty()) {
            setup_cors_headers(res);
            res->writeStatus("204");
            res->end();
        } else {
            nlohmann::json response = {
                {"messages", nlohmann::json::array()}
            };
            
            for (const auto& msg : result.messages) {
                // Format timestamp for createdAt with milliseconds
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
        send_error_response(res, e.what(), 500);
    }
}

void QueenServer::handle_ack(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    read_json_body(res,
        [this, res](const nlohmann::json& body) {
            try {
                std::string transaction_id = "";
                if (body.contains("transactionId") && !body["transactionId"].is_null() && body["transactionId"].is_string()) {
                    transaction_id = body["transactionId"];
                } else {
                    send_error_response(res, "transactionId is required and must be a string", 400);
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
                
                auto ack_result = queue_manager_->acknowledge_message(transaction_id, status, error, consumer_group, lease_id);
                
                if (ack_result.success) {
                    // Get current timestamp in ISO format
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
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        }
    );
}

void QueenServer::handle_transaction(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    read_json_body(res,
        [this, res](const nlohmann::json& body) {
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
                
                // For now, execute operations sequentially (not in a single DB transaction)
                // This is a simplified implementation that works for most use cases
                nlohmann::json results = nlohmann::json::array();
                
                for (const auto& op : operations) {
                    if (!op.contains("type")) {
                        send_error_response(res, "operation type required", 400);
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
                        
                        auto push_results = queue_manager_->push_messages(items);
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
                        
                        auto ack_result = queue_manager_->acknowledge_message(transaction_id, status, error, consumer_group, std::nullopt);
                        nlohmann::json ack_result_json;
                        ack_result_json["type"] = "ack";
                        ack_result_json["success"] = ack_result.success;
                        ack_result_json["transactionId"] = ack_result.transaction_id;
                        ack_result_json["status"] = ack_result.status;
                        results.push_back(ack_result_json);
                        
                    } else {
                        send_error_response(res, "Unknown operation type: " + op_type, 400);
                        return;
                    }
                }
                
                nlohmann::json response = {
                    {"success", true},
                    {"results", results},
                    {"transactionId", queue_manager_->generate_uuid()}
                };
                
                send_json_response(res, response);
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 400);
            }
        },
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        }
    );
}

void QueenServer::handle_ack_batch(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    // Track if connection was aborted
    auto aborted = std::make_shared<bool>(false);
    res->onAborted([aborted]() {
        *aborted = true;
    });
    
    read_json_body(res,
        [this, res, aborted](const nlohmann::json& body) {
            if (*aborted) return;
            
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
                
                // Process each acknowledgment
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
                
                // ⚡ Offload to ThreadPool - worker does ONLY database work
                thread_pool_->push([this, res, ack_items, consumer_group, aborted]() {
                    if (is_shutting_down_.load()) return;
                    
                    spdlog::info("Worker thread: Starting batch ACK for {} items", ack_items.size());
                    
                    try {
                        // Worker thread: ONLY blocking database work
                        auto results = queue_manager_->acknowledge_messages(ack_items, consumer_group);
                        
                        spdlog::info("Worker thread: Batch ACK completed");
                        
                        if (*aborted) {
                            spdlog::debug("Request aborted, skipping defer");
                            return;
                        }
                        
                        // ✅ CRITICAL: Use Loop::defer to get back to event loop thread!
                        uWS::Loop::get()->defer([this, res, results, aborted]() {
                            // NOW in event loop thread - safe to use res!
                            if (*aborted) return;
                            
                            try {
                                // Build response
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
                                
                                // Send in event loop thread with cork
                                res->cork([this, res, response]() {
                                    send_json_response(res, response);
                                });
                                
                            } catch (const std::exception& e) {
                                spdlog::error("Error building ACK response: {}", e.what());
                                res->cork([this, res, e]() {
                                    send_error_response(res, e.what(), 500);
                                });
                            }
                        });
                        
                    } catch (const std::exception& e) {
                        spdlog::error("Error in ACK worker thread: {}", e.what());
                        
                        if (*aborted) return;
                        
                        // Defer error response back to event loop
                        uWS::Loop::get()->defer([this, res, e, aborted]() {
                            if (*aborted) return;
                            
                            res->cork([this, res, e]() {
                                send_error_response(res, e.what(), 500);
                            });
                        });
                    }
                });  // Lambda returns void ✓
                
            } catch (const std::exception& e) {
                if (*aborted) return;
                send_error_response(res, e.what(), 500);
            }
        },
        [this, res, aborted](const std::string& error) {
            if (*aborted) return;
            send_error_response(res, error, 400);
        }
    );
}

void QueenServer::handle_lease_extend(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    read_json_body(res,
        [this, res, req](const nlohmann::json& body) {
            try {
                std::string lease_id = std::string(req->getParameter(0));
                int seconds = body.value("seconds", 60);
                
                bool success = queue_manager_->extend_message_lease(lease_id, seconds);
                
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
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        }
    );
}

void QueenServer::handle_delete_queue(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    try {
        std::string queue_name = std::string(req->getParameter(0));
        
        bool deleted = queue_manager_->delete_queue(queue_name);
        
        nlohmann::json response = {
            {"deleted", true},
            {"queue", queue_name},
            {"existed", deleted}
        };
        
        send_json_response(res, response);
        
    } catch (const std::exception& e) {
        send_error_response(res, e.what(), 500);
    }
}

void QueenServer::handle_health(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    try {
        bool healthy = queue_manager_->health_check();
        
        nlohmann::json response = {
            {"status", healthy ? "healthy" : "unhealthy"},
            {"database", healthy ? "connected" : "disconnected"},
            {"server", "C++ Queen Server"},
            {"version", "1.0.0"}
        };
        
        send_json_response(res, response, healthy ? 200 : 503);
        
    } catch (const std::exception& e) {
        send_error_response(res, e.what(), 500);
    }
}

// Static helper for CORS headers (used by all threads)
static void setup_cors_headers(uWS::HttpResponse<false>* res) {
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res->writeHeader("Access-Control-Max-Age", "86400");
}

// Static helper to send JSON responses
static void send_json_response(uWS::HttpResponse<false>* res, const nlohmann::json& json, int status_code = 200) {
    setup_cors_headers(res);
    res->writeHeader("Content-Type", "application/json");
    std::string status_str = std::to_string(status_code);
    res->writeStatus(status_str);
    res->end(json.dump());
}

// Static helper to send error responses
static void send_error_response(uWS::HttpResponse<false>* res, const std::string& error, int status_code = 500) {
    nlohmann::json error_json = {{"error", error}};
    send_json_response(res, error_json, status_code);
}

// Setup all routes on an App instance (called by each worker thread)
void QueenServer::setup_app_routes(uWS::App& app, 
                                   std::shared_ptr<QueueManager> queue_manager,
                                   const Config& config) {
    // CORS preflight
    app_->options("/*", [this](auto* res, auto* req) {
        setup_cors_headers(res);
        res->writeStatus("204");
        res->end();
    });
    
    // API routes
    app_->post("/api/v1/configure", [this](auto* res, auto* req) {
        handle_configure(res, req);
    });
    
    app_->post("/api/v1/push", [this](auto* res, auto* req) {
        handle_push(res, req);
    });
    
    app_->get("/api/v1/pop/queue/:queue/partition/:partition", [this](auto* res, auto* req) {
        handle_pop_queue_partition(res, req);
    });
    
    app_->get("/api/v1/pop/queue/:queue", [this](auto* res, auto* req) {
        handle_pop_queue(res, req);
    });
    
    app_->get("/api/v1/pop", [this](auto* res, auto* req) {
        handle_pop_filtered(res, req);
    });
    
    app_->post("/api/v1/ack", [this](auto* res, auto* req) {
        handle_ack(res, req);
    });
    
    app_->post("/api/v1/ack/batch", [this](auto* res, auto* req) {
        handle_ack_batch(res, req);
    });
    
    app_->post("/api/v1/transaction", [this](auto* res, auto* req) {
        handle_transaction(res, req);
    });
    
    app_->post("/api/v1/lease/:leaseId/extend", [this](auto* res, auto* req) {
        handle_lease_extend(res, req);
    });
    
    app_->del("/api/v1/resources/queues/:queue", [this](auto* res, auto* req) {
        handle_delete_queue(res, req);
    });
    
    app_->get("/health", [this](auto* res, auto* req) {
        handle_health(res, req);
    });
    
    spdlog::info("Routes configured successfully");
}

bool QueenServer::start() {
    try {
        setup_routes();
        
        app_->listen(config_.server.host, config_.server.port, [this](auto* listen_socket) {
            if (listen_socket) {
                spdlog::info("✅ Queen C++ Server running at http://{}:{}", 
                           config_.server.host, config_.server.port);
                spdlog::info("🎯 Ready to process messages with high performance!");
            } else {
                spdlog::error("❌ Failed to start server on {}:{}", 
                            config_.server.host, config_.server.port);
            }
        });
        
        app_->run();
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to start server: {}", e.what());
        return false;
    }
}

void QueenServer::stop() {
    if (is_shutting_down_.exchange(true)) {
        spdlog::warn("Shutdown already in progress - forcing exit");
        std::_Exit(0);  // Force immediate exit, skip destructors
    }
    
    spdlog::info("Server shutdown requested - stopping thread pool");
    
    if (thread_pool_) {
        try {
            spdlog::info("Waiting for {} worker threads to finish...", thread_pool_->pool_size());
            thread_pool_->stop();
            spdlog::info("✅ Thread pool stopped gracefully");
        } catch (const std::exception& e) {
            spdlog::error("Error stopping thread pool: {}", e.what());
        }
    }
    
    spdlog::info("✅ Shutdown complete - exiting cleanly");
    
    // Clean exit - skip static destructors that may cause issues
    std::_Exit(0);
}

// HttpUtils implementation
std::string HttpUtils::url_decode(const std::string& encoded) {
    std::string decoded;
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            int value;
            std::istringstream is(encoded.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                decoded += static_cast<char>(value);
                i += 2;
            } else {
                decoded += encoded[i];
            }
        } else if (encoded[i] == '+') {
            decoded += ' ';
        } else {
            decoded += encoded[i];
        }
    }
    return decoded;
}

nlohmann::json HttpUtils::parse_query_string(const std::string& query) {
    nlohmann::json params = nlohmann::json::object();
    
    if (query.empty()) return params;
    
    std::istringstream iss(query);
    std::string pair;
    
    while (std::getline(iss, pair, '&')) {
        auto eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = url_decode(pair.substr(0, eq_pos));
            std::string value = url_decode(pair.substr(eq_pos + 1));
            params[key] = value;
        } else {
            params[url_decode(pair)] = "";
        }
    }
    
    return params;
}

} // namespace queen
