#include "queen/server.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>

namespace queen {

QueenServer::QueenServer(const Config& config) : config_(config) {
    app_ = std::make_unique<uWS::App>();
}

QueenServer::~QueenServer() {
    stop();
}

bool QueenServer::initialize() {
    try {
        // Initialize database pool
        auto db_config = config_.database;
        db_pool_ = std::make_shared<DatabasePool>(
            db_config.connection_string(), 
            db_config.pool_size
        );
        
        // Initialize queue manager
        queue_manager_ = std::make_shared<QueueManager>(db_pool_, config_.queue);
        
        // Initialize database schema
        if (!queue_manager_->initialize_schema()) {
            spdlog::error("Failed to initialize database schema");
            return false;
        }
        
        spdlog::info("Queen C++ server initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize server: {}", e.what());
        return false;
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
        
        auto result = queue_manager_->pop_messages(queue_name, partition_name, consumer_group, options);
        
        if (result.messages.empty()) {
            setup_cors_headers(res);
            res->writeStatus("204");
            res->end();
        } else {
            nlohmann::json response = {
                {"messages", nlohmann::json::array()}
            };
            
            for (const auto& msg : result.messages) {
                // Format timestamp for createdAt
                auto time_t = std::chrono::system_clock::to_time_t(msg.created_at);
                std::stringstream created_at_ss;
                created_at_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S.000Z");
                
                nlohmann::json msg_json = {
                    {"id", msg.id},
                    {"transactionId", msg.transaction_id},
                    {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                    {"queue", msg.queue_name},
                    {"partition", msg.partition_name},
                    {"data", msg.payload},
                    {"retryCount", 0},
                    {"priority", 0},
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
                // Format timestamp for createdAt
                auto time_t = std::chrono::system_clock::to_time_t(msg.created_at);
                std::stringstream created_at_ss;
                created_at_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S.000Z");
                
                nlohmann::json msg_json = {
                    {"id", msg.id},
                    {"transactionId", msg.transaction_id},
                    {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                    {"queue", msg.queue_name},
                    {"partition", msg.partition_name},
                    {"data", msg.payload},
                    {"retryCount", 0},
                    {"priority", 0},
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
                // Format timestamp for createdAt
                auto time_t = std::chrono::system_clock::to_time_t(msg.created_at);
                std::stringstream created_at_ss;
                created_at_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S.000Z");
                
                nlohmann::json msg_json = {
                    {"id", msg.id},
                    {"transactionId", msg.transaction_id},
                    {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                    {"queue", msg.queue_name},
                    {"partition", msg.partition_name},
                    {"data", msg.payload},
                    {"retryCount", 0},
                    {"priority", 0},
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
                if (!body.contains("transactionId") || !body["transactionId"].is_string()) {
                    send_error_response(res, "transactionId is required", 400);
                    return;
                }
                
                std::string transaction_id = body["transactionId"];
                std::string status = body.value("status", "completed");
                std::string consumer_group = "__QUEUE_MODE__";
                
                if (body.contains("consumerGroup") && !body["consumerGroup"].is_null() && body["consumerGroup"].is_string()) {
                    consumer_group = body["consumerGroup"];
                }
                
                std::optional<std::string> error;
                if (body.contains("error") && !body["error"].is_null() && body["error"].is_string()) {
                    error = body["error"];
                }
                
                bool success = queue_manager_->acknowledge_message(transaction_id, status, error, consumer_group);
                
                if (success) {
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
                        {"status", status},
                        {"consumerGroup", consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(consumer_group)},
                        {"acknowledgedAt", ss.str()}
                    };
                    send_json_response(res, response);
                } else {
                    send_error_response(res, "Failed to acknowledge message", 500);
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

void QueenServer::setup_routes() {
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
    // uWebSockets doesn't have a clean shutdown method in the simple API
    // In a production implementation, you'd need to handle this more gracefully
    spdlog::info("Server shutdown requested");
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
