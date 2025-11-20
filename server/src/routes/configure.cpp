#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/queue_types.hpp"
#include <spdlog/spdlog.h>

namespace queen {
namespace routes {

void setup_configure_routes(uWS::App* app, const RouteContext& ctx) {
    app->post("/api/v1/configure", [ctx](auto* res, auto* req) {
        (void)req;
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
                    
                    QueueOptions options;
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
                    
                    spdlog::info("[Worker {}] Configuring queue: {}", ctx.worker_id, queue_name);
                    bool success = ctx.async_queue_manager->configure_queue(queue_name, options, namespace_name, task_name);
                    
                    spdlog::info("[Worker {}] Configure queue result: success={}", ctx.worker_id, success);
                    
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
}

} // namespace routes
} // namespace queen

