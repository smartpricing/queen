#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/poll_intention_registry.hpp"
#include "queen/response_queue.hpp"
#include "queen/queue_types.hpp"
#include "queen/shared_state_manager.hpp"
#include "queen/sidecar_db_pool.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

// External globals (declared in acceptor_server.cpp)
namespace queen {
extern std::shared_ptr<ResponseRegistry> global_response_registry;
extern std::shared_ptr<PollIntentionRegistry> global_poll_intention_registry;
extern std::shared_ptr<SharedStateManager> global_shared_state;
}

namespace queen {
namespace routes {

// Helper: Record consumer group subscription with deduplication
static void record_consumer_group_subscription_if_needed(
    const RouteContext& ctx,
    const std::string& consumer_group,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& sub_mode,
    const std::string& sub_from) {
    
    if (consumer_group == "__QUEUE_MODE__") {
        return;  // Skip for queue mode
    }
    
    auto [mode_value, timestamp_sql] = parse_subscription_mode(
        sub_mode, sub_from, ctx.config.queue.default_subscription_mode
    );
    
    ctx.async_queue_manager->record_consumer_group_subscription(
        consumer_group, queue_name, partition_name, "", "",
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
                ctx, consumer_group, queue_name, partition_name, sub_mode, sub_from
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
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now(),
                    .subscription_mode = options.subscription_mode,
                    .subscription_from = options.subscription_from
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                // Register consumer presence for targeted notifications
                if (global_shared_state && global_shared_state->is_enabled()) {
                    global_shared_state->register_consumer(queue_name);
                }
                
                spdlog::info("[Worker {}] SPOP: Registered poll intention {} for queue {}/{} (wait=true)", 
                            ctx.worker_id, request_id, queue_name, partition_name);
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use sidecar for async pop
            spdlog::info("[Worker {}] SPOP: Executing immediate pop for {}/{} (wait=false)", ctx.worker_id, queue_name, partition_name);
                
            // Register response for async delivery
            std::string request_id = global_response_registry->register_response(
                res, ctx.worker_id, nullptr
            );
            
            // Build sidecar request with direct pop_messages_v2 call
            // leaseTime=0 means "use queue's configured lease_time from database"
            SidecarRequest sidecar_req;
            sidecar_req.op_type = SidecarOpType::POP;
            sidecar_req.request_id = request_id;
            sidecar_req.sql = "SELECT queen.pop_messages_v2($1, $2, $3, $4, $5, $6, $7)";
            sidecar_req.params = {
                queue_name,                                      // p_queue_name
                partition_name,                                  // p_partition_name (specific partition)
                consumer_group,                                  // p_consumer_group
                std::to_string(options.batch),                   // p_batch_size
                "0",                                             // p_lease_time_seconds (0 = use queue config)
                options.subscription_mode.value_or("all"),       // p_subscription_mode
                options.subscription_from.value_or("")           // p_subscription_from
            };
            sidecar_req.item_count = 1;
            
            ctx.sidecar->submit(std::move(sidecar_req));
            spdlog::debug("[Worker {}] SPOP: Submitted to sidecar (request_id={})", 
                         ctx.worker_id, request_id);
            
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
                ctx, consumer_group, queue_name, "", sub_mode, sub_from
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
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now(),
                    .subscription_mode = options.subscription_mode,
                    .subscription_from = options.subscription_from
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                // Register consumer presence for targeted notifications
                if (global_shared_state && global_shared_state->is_enabled()) {
                    global_shared_state->register_consumer(queue_name);
                }
                
                spdlog::info("[Worker {}] QPOP: Registered poll intention {} for queue {} (wait=true)", 
                            ctx.worker_id, request_id, queue_name);
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use sidecar for async pop
            spdlog::info("[Worker {}] QPOP: Executing immediate pop for {}/* (wait=false)", ctx.worker_id, queue_name);
                
            std::string request_id = global_response_registry->register_response(
                res, ctx.worker_id, nullptr
            );
            
            // Build sidecar request with direct pop_messages_v2 call
            // Pass empty string for partition to pop from any partition
            // leaseTime=0 means "use queue's configured lease_time from database"
            SidecarRequest sidecar_req;
            sidecar_req.op_type = SidecarOpType::POP;
            sidecar_req.request_id = request_id;
            sidecar_req.sql = "SELECT queen.pop_messages_v2($1, $2, $3, $4, $5, $6, $7)";
            sidecar_req.params = {
                queue_name,                                      // p_queue_name
                "",                                              // p_partition_name (empty = any partition, will be NULL)
                consumer_group,                                  // p_consumer_group
                std::to_string(options.batch),                   // p_batch_size
                "0",                                             // p_lease_time_seconds (0 = use queue config)
                options.subscription_mode.value_or("all"),       // p_subscription_mode
                options.subscription_from.value_or("")           // p_subscription_from
            };
            sidecar_req.item_count = 1;
            
            ctx.sidecar->submit(std::move(sidecar_req));
            spdlog::debug("[Worker {}] QPOP: Submitted to sidecar (request_id={})", 
                         ctx.worker_id, request_id);
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

