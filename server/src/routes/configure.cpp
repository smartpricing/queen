#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/queue_types.hpp"
#include "queen/response_queue.hpp"
#include "queen/shared_state_manager.hpp"
#include "queen.hpp"  // libqueen
#include <spdlog/spdlog.h>

// External globals (declared in acceptor_server.cpp)
namespace queen {
extern std::vector<std::shared_ptr<ResponseRegistry>> worker_response_registries;
extern std::shared_ptr<SharedStateManager> global_shared_state;
}

namespace queen {
namespace routes {

void setup_configure_routes(uWS::App* app, const RouteContext& ctx) {
    app->post("/api/v1/configure", [ctx](auto* res, auto* req) {
        // Check authentication - READ_WRITE required for queue configuration
        REQUIRE_AUTH(res, req, ctx, auth::AccessLevel::READ_WRITE);
        
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    spdlog::debug("Configure request body: {}", body.dump());
                    
                    if (!body.contains("queue") || !body["queue"].is_string()) {
                        send_error_response(res, "queue is required", 400);
                        return;
                    }
                    
                    std::string queue_name = body["queue"];
                    spdlog::debug("Queue name: {}", queue_name);
                    
                    // Build options JSON for stored procedure
                    nlohmann::json options_json;
                    
                    // Safe null handling for namespace/task
                    if (body.contains("namespace") && body["namespace"].is_string()) {
                        options_json["namespace"] = body["namespace"].get<std::string>();
                    }
                    if (body.contains("task") && body["task"].is_string()) {
                        options_json["task"] = body["task"].get<std::string>();
                    }
                    
                    // Extract options
                    if (body.contains("options") && body["options"].is_object()) {
                        auto opts = body["options"];
                        if (opts.contains("leaseTime")) options_json["leaseTime"] = opts["leaseTime"];
                        if (opts.contains("maxSize")) options_json["maxSize"] = opts["maxSize"];
                        if (opts.contains("ttl")) options_json["ttl"] = opts["ttl"];
                        if (opts.contains("retryLimit")) options_json["retryLimit"] = opts["retryLimit"];
                        if (opts.contains("retryDelay")) options_json["retryDelay"] = opts["retryDelay"];
                        if (opts.contains("deadLetterQueue")) options_json["deadLetterQueue"] = opts["deadLetterQueue"];
                        if (opts.contains("dlqAfterMaxRetries")) options_json["dlqAfterMaxRetries"] = opts["dlqAfterMaxRetries"];
                        if (opts.contains("priority")) options_json["priority"] = opts["priority"];
                        if (opts.contains("delayedProcessing")) options_json["delayedProcessing"] = opts["delayedProcessing"];
                        if (opts.contains("windowBuffer")) options_json["windowBuffer"] = opts["windowBuffer"];
                        if (opts.contains("retentionSeconds")) options_json["retentionSeconds"] = opts["retentionSeconds"];
                        if (opts.contains("completedRetentionSeconds")) options_json["completedRetentionSeconds"] = opts["completedRetentionSeconds"];
                        if (opts.contains("retentionEnabled")) options_json["retentionEnabled"] = opts["retentionEnabled"];
                        if (opts.contains("encryptionEnabled")) options_json["encryptionEnabled"] = opts["encryptionEnabled"];
                        if (opts.contains("maxWaitTimeSeconds")) options_json["maxWaitTimeSeconds"] = opts["maxWaitTimeSeconds"];
                    }
                    
                    spdlog::info("[Worker {}] Configuring queue: {} via stored procedure", ctx.worker_id, queue_name);
                    
                    // Register response for async delivery
                    std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
                        res, ctx.worker_id, nullptr
                    );
                    
                    // Build stored procedure call
                    queen::JobRequest job_req;
                    job_req.op_type = queen::JobType::CUSTOM;
                    job_req.request_id = request_id;
                    job_req.sql = "SELECT queen.configure_queue_v1($1, $2::jsonb)";
                    job_req.params = {queue_name, options_json.dump()};
                    
                    // Capture values for callback
                    auto worker_loop = ctx.worker_loop;
                    auto worker_id = ctx.worker_id;
                    std::string captured_queue_name = queue_name;
                    nlohmann::json captured_options = options_json;
                    
                    ctx.queen->submit(std::move(job_req), [worker_loop, worker_id, request_id, captured_queue_name, captured_options](std::string result) {
                        worker_loop->defer([result = std::move(result), worker_id, request_id, captured_queue_name, captured_options]() {
                            nlohmann::json json_response;
                            int status_code = 200;
                            bool is_error = false;
                            
                            try {
                                json_response = nlohmann::json::parse(result);
                                
                                // Check for error in response
                                if (json_response.contains("error") && !json_response["error"].is_null()) {
                                    is_error = true;
                                    status_code = 500;
                                } else if (json_response.contains("configured") && json_response["configured"].get<bool>()) {
                                    // Update shared state cache on success
                                    if (global_shared_state) {
                                        caches::CachedQueueConfig cached_config;
                                        cached_config.name = captured_queue_name;
                                        cached_config.namespace_name = captured_options.value("namespace", "");
                                        cached_config.task = captured_options.value("task", "");
                                        cached_config.priority = captured_options.value("priority", 0);
                                        cached_config.lease_time = captured_options.value("leaseTime", 300);
                                        cached_config.retry_limit = captured_options.value("retryLimit", 3);
                                        cached_config.retry_delay = captured_options.value("retryDelay", 1000);
                                        cached_config.max_size = captured_options.value("maxSize", 0);
                                        cached_config.ttl = captured_options.value("ttl", 3600);
                                        cached_config.dlq_enabled = captured_options.value("deadLetterQueue", false);
                                        cached_config.dlq_after_max_retries = captured_options.value("dlqAfterMaxRetries", false);
                                        cached_config.delayed_processing = captured_options.value("delayedProcessing", 0);
                                        cached_config.window_buffer = captured_options.value("windowBuffer", 0);
                                        cached_config.retention_seconds = captured_options.value("retentionSeconds", 0);
                                        cached_config.completed_retention_seconds = captured_options.value("completedRetentionSeconds", 0);
                                        cached_config.encryption_enabled = captured_options.value("encryptionEnabled", false);
                                        global_shared_state->set_queue_config(captured_queue_name, cached_config);
                                        spdlog::debug("Queue '{}' config cached", captured_queue_name);
                                    }
                                }
                            } catch (const std::exception& e) {
                                json_response = {{"error", e.what()}};
                                status_code = 500;
                                is_error = true;
                            }
                            
                            worker_response_registries[worker_id]->send_response(
                                request_id, json_response, is_error, status_code);
                        });
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
}

} // namespace routes
} // namespace queen
