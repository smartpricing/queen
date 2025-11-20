#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/poll_intention_registry.hpp"
#include "queen/response_queue.hpp"
#include "queen/queue_types.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

// External globals (declared in acceptor_server.cpp)
namespace queen {
extern std::shared_ptr<ResponseRegistry> global_response_registry;
extern std::shared_ptr<PollIntentionRegistry> global_poll_intention_registry;
}

namespace queen {
namespace routes {

// Helper: Record consumer group subscription with deduplication
static void record_consumer_group_subscription_if_needed(
    const RouteContext& ctx,
    const std::string& consumer_group,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& namespace_name,
    const std::string& task_name,
    const std::string& sub_mode,
    const std::string& sub_from) {
    
    if (consumer_group == "__QUEUE_MODE__") {
        return;  // Skip for queue mode
    }
    
    auto [mode_value, timestamp_sql] = parse_subscription_mode(
        sub_mode, sub_from, ctx.config.queue.default_subscription_mode
    );
    
    ctx.async_queue_manager->record_consumer_group_subscription(
        consumer_group, queue_name, partition_name, namespace_name, task_name,
        mode_value, timestamp_sql
    );
}

// Helper: Build message JSON response (deduplicate lines 723-746, 980-1003, 1219-1242)
static nlohmann::json build_message_json(const Message& msg, const std::string& consumer_group, const std::optional<std::string>& lease_id) {
    std::string created_at_str = format_timestamp_iso8601(msg.created_at);
    
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
        {"createdAt", created_at_str},
        {"consumerGroup", consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(consumer_group)},
        {"leaseId", lease_id.has_value() ? nlohmann::json(*lease_id) : nlohmann::json(nullptr)}
    };
    
    return msg_json;
}

void setup_pop_routes(uWS::App* app, const RouteContext& ctx) {
    // SPECIFIC POP from queue/partition - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop/queue/:queue/partition/:partition", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string partition_name = std::string(req->getParameter(1));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", ctx.config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", ctx.config.queue.default_batch_size);
            
            auto pool_stats = ctx.async_queue_manager->get_pool_stats();
            spdlog::info("[Worker {}] SPOP: [{}/{}@{}] batch={}, wait={} | Pool: {}/{} conn ({} in use)", 
                        ctx.worker_id, queue_name, partition_name, consumer_group, batch, wait,
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
            
            // Record consumer group subscription metadata (for NEW mode support)
            record_consumer_group_subscription_if_needed(
                ctx, consumer_group, queue_name, partition_name, "", "", sub_mode, sub_from
            );
            
            if (wait) {
                // Register response with abort callback to clean up intention on disconnect
                std::string request_id = global_response_registry->register_response(res, ctx.worker_id,
                    [](const std::string& req_id) {
                        // Remove intention from registry when connection aborts
                        global_poll_intention_registry->remove_intention(req_id);
                        spdlog::info("SPOP: Connection aborted, removed poll intention {}", req_id);
                    });
            
                // Use Poll Intention Registry for long-polling
                queen::PollIntention intention{
                    .request_id = request_id,
                    .worker_id = ctx.worker_id,
                    .queue_name = queue_name,
                    .partition_name = partition_name,  // Specific partition
                    .namespace_name = std::nullopt,
                    .task_name = std::nullopt,
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now(),
                    .subscription_mode = options.subscription_mode,
                    .subscription_from = options.subscription_from
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                spdlog::info("[Worker {}] SPOP: Registered poll intention {} for queue {}/{} (wait=true)", 
                            ctx.worker_id, request_id, queue_name, partition_name);
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use AsyncQueueManager directly in uWS event loop
            spdlog::info("[Worker {}] SPOP: Executing immediate pop for {}/{} (wait=false)", ctx.worker_id, queue_name, partition_name);
                
            try {
                auto start_time = std::chrono::steady_clock::now();
                auto result = ctx.async_queue_manager->pop_messages_from_partition(queue_name, partition_name, consumer_group, options);
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                
                if (result.messages.empty()) {
                    // No messages - send 204 No Content
                    nlohmann::json empty_response;
                    send_json_response(res, empty_response, 204);
                    spdlog::info("[Worker {}] SPOP: No messages for [{}/{}], {}ms", 
                               ctx.worker_id, queue_name, partition_name, duration_ms);
                    return;
                }
                
                // Build response JSON
                nlohmann::json response = {{"messages", nlohmann::json::array()}};
                
                for (const auto& msg : result.messages) {
                    response["messages"].push_back(build_message_json(msg, consumer_group, result.lease_id));
                }
                
                send_json_response(res, response, 200);
                spdlog::info("[Worker {}] SPOP: Sent response for [{}/{}] ({} messages, {}ms)", 
                           ctx.worker_id, queue_name, partition_name, result.messages.size(), duration_ms);
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
                spdlog::error("[Worker {}] SPOP: Error for [{}/{}]: {}", ctx.worker_id, queue_name, partition_name, e.what());
            }
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // POP from queue (any partition) - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop/queue/:queue", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", ctx.config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", ctx.config.queue.default_batch_size);
            
            auto pool_stats = ctx.async_queue_manager->get_pool_stats();
            spdlog::info("[Worker {}] QPOP: [{}/*@{}] batch={}, wait={} | Pool: {}/{} conn ({} in use)", 
                        ctx.worker_id, queue_name, consumer_group, batch, wait,
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
            
            // Record consumer group subscription metadata (for NEW mode support)
            record_consumer_group_subscription_if_needed(
                ctx, consumer_group, queue_name, "", "", "", sub_mode, sub_from
            );
            
            if (wait) {
                // Register response with abort callback to clean up intention on disconnect
                std::string request_id = global_response_registry->register_response(res, ctx.worker_id,
                    [](const std::string& req_id) {
                        // Remove intention from registry when connection aborts
                        global_poll_intention_registry->remove_intention(req_id);
                        spdlog::info("QPOP: Connection aborted, removed poll intention {}", req_id);
                    });
            
                // Use Poll Intention Registry for long-polling
                queen::PollIntention intention{
                    .request_id = request_id,
                    .worker_id = ctx.worker_id,
                    .queue_name = queue_name,
                    .partition_name = std::nullopt,  // Any partition
                    .namespace_name = std::nullopt,
                    .task_name = std::nullopt,
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now(),
                    .subscription_mode = options.subscription_mode,
                    .subscription_from = options.subscription_from
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                spdlog::info("[Worker {}] QPOP: Registered poll intention {} for queue {} (wait=true)", 
                            ctx.worker_id, request_id, queue_name);
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use AsyncQueueManager directly in uWS event loop
            spdlog::info("[Worker {}] QPOP: Executing immediate pop for {}/* (wait=false)", ctx.worker_id, queue_name);
                
            try {
                auto start_time = std::chrono::steady_clock::now();
                auto result = ctx.async_queue_manager->pop_messages_from_queue(queue_name, consumer_group, options);
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                
                if (result.messages.empty()) {
                    // No messages - send 204 No Content
                    nlohmann::json empty_response;
                    send_json_response(res, empty_response, 204);
                    spdlog::info("[Worker {}] QPOP: No messages for [{}/*], {}ms", 
                               ctx.worker_id, queue_name, duration_ms);
                    return;
                }
                
                // Build response JSON
                nlohmann::json response = {{"messages", nlohmann::json::array()}};
                
                for (const auto& msg : result.messages) {
                    response["messages"].push_back(build_message_json(msg, consumer_group, result.lease_id));
                }
                
                send_json_response(res, response, 200);
                spdlog::info("[Worker {}] QPOP: Sent response for [{}/*] ({} messages, {}ms)", 
                           ctx.worker_id, queue_name, result.messages.size(), duration_ms);
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
                spdlog::error("[Worker {}] QPOP: Error for [{}/*]: {}", ctx.worker_id, queue_name, e.what());
            }
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // ASYNC POP with namespace/task filtering (no specific queue) - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop", [ctx](auto* res, auto* req) {
        try {
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            std::string namespace_param = get_query_param(req, "namespace", "");
            std::string task_param = get_query_param(req, "task", "");
            
            std::optional<std::string> namespace_name = namespace_param.empty() ? std::nullopt : std::optional<std::string>(namespace_param);
            std::optional<std::string> task_name = task_param.empty() ? std::nullopt : std::optional<std::string>(task_param);
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", ctx.config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", ctx.config.queue.default_batch_size);
            
            PopOptions options;
            options.wait = false;  // Always false - registry handles waiting
            options.timeout = timeout_ms;
            options.batch = batch;
            options.auto_ack = get_query_param_bool(req, "autoAck", false);
            
            std::string sub_mode = get_query_param(req, "subscriptionMode", "");
            if (!sub_mode.empty()) options.subscription_mode = sub_mode;
            std::string sub_from = get_query_param(req, "subscriptionFrom", "");
            if (!sub_from.empty()) options.subscription_from = sub_from;
            
            // Record consumer group subscription metadata (for NEW mode support)
            record_consumer_group_subscription_if_needed(
                ctx, consumer_group, "", "", namespace_param, task_param, sub_mode, sub_from
            );
            
            if (wait) {
                // Register response with abort callback to clean up intention on disconnect
                std::string request_id = global_response_registry->register_response(res, ctx.worker_id,
                    [](const std::string& req_id) {
                        // Remove intention from registry when connection aborts
                        global_poll_intention_registry->remove_intention(req_id);
                        spdlog::info("POP: Connection aborted, removed poll intention {}", req_id);
                    });
            
                // Use Poll Intention Registry for long-polling
                queen::PollIntention intention{
                    .request_id = request_id,
                    .worker_id = ctx.worker_id,
                    .queue_name = std::nullopt,
                    .partition_name = std::nullopt,
                    .namespace_name = namespace_name,  // Namespace filtering
                    .task_name = task_name,            // Task filtering
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now(),
                    .subscription_mode = options.subscription_mode,
                    .subscription_from = options.subscription_from
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                spdlog::info("[Worker {}] POP: Registered poll intention {} for namespace={} task={} (wait=true)", 
                            ctx.worker_id, request_id, 
                            namespace_name.value_or("*"), 
                            task_name.value_or("*"));
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use AsyncQueueManager directly in uWS event loop
            spdlog::info("[Worker {}] POP: Executing immediate pop for namespace={} task={} (wait=false)", 
                        ctx.worker_id, namespace_name.value_or("*"), task_name.value_or("*"));
                
            try {
                auto result = ctx.async_queue_manager->pop_messages_filtered(namespace_name, task_name, consumer_group, options);
                
                if (result.messages.empty()) {
                    // No messages - send 204 No Content
                    nlohmann::json empty_response;
                    send_json_response(res, empty_response, 204);
                    spdlog::debug("[Worker {}] POP: No messages", ctx.worker_id);
                    return;
                }
                
                // Build response JSON
                nlohmann::json response = {{"messages", nlohmann::json::array()}};
                for (const auto& msg : result.messages) {
                    response["messages"].push_back(build_message_json(msg, consumer_group, result.lease_id));
                }
                
                send_json_response(res, response, 200);
                spdlog::info("[Worker {}] POP: Sent response ({} messages)", ctx.worker_id, result.messages.size());
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
                spdlog::error("[Worker {}] POP: Error: {}", ctx.worker_id, e.what());
            }
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

