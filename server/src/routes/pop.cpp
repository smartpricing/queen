#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/response_queue.hpp"
#include "queen/queue_types.hpp"
#include "queen/shared_state_manager.hpp"
#include "queen/sidecar_db_pool.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

// External globals (declared in acceptor_server.cpp)
namespace queen {
extern std::shared_ptr<ResponseRegistry> global_response_registry;
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

void setup_pop_routes(uWS::App* app, const RouteContext& ctx) {
    // SPECIFIC POP from queue/partition - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop/queue/:queue/partition/:partition", [ctx](auto* res, auto* req) {
        auto request_start = std::chrono::steady_clock::now();
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
                // Register response with abort callback to clean up waiting request on disconnect
                std::string request_id = global_response_registry->register_response(res, ctx.worker_id,
                    [](const std::string& req_id) {
                        // Log abort - sidecar will handle cleanup via timeout
                        spdlog::info("SPOP: Connection aborted for {}", req_id);
                    });
            
                // Submit POP_WAIT to sidecar - it will manage waiting and backoff
                SidecarRequest sidecar_req;
                sidecar_req.op_type = SidecarOpType::POP_WAIT;
                sidecar_req.request_id = request_id;
                sidecar_req.queue_name = queue_name;
                sidecar_req.partition_name = partition_name;
                sidecar_req.consumer_group = consumer_group;
                sidecar_req.batch_size = options.batch;
                sidecar_req.subscription_mode = options.subscription_mode.value_or("all");
                sidecar_req.subscription_from = options.subscription_from.value_or("");
                sidecar_req.wait_deadline = std::chrono::steady_clock::now() + 
                                            std::chrono::milliseconds(timeout_ms);
                sidecar_req.next_check = std::chrono::steady_clock::now();  // Check immediately
                
                // Build SQL for pop_messages_v2
                sidecar_req.sql = "SELECT queen.pop_messages_v2($1, $2, $3, $4, $5, $6, $7)";
                sidecar_req.params = {
                    queue_name,
                    partition_name,
                    consumer_group,
                    std::to_string(options.batch),
                    "0",  // Use queue's configured lease_time
                    sidecar_req.subscription_mode,
                    sidecar_req.subscription_from
                };
                
                auto route_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - request_start).count();
            ctx.sidecar->submit(std::move(sidecar_req));
                
                // Register consumer presence for targeted notifications
                if (global_shared_state && global_shared_state->is_enabled()) {
                    global_shared_state->register_consumer(queue_name);
                }
                
                spdlog::info("[Worker {}] SPOP TIMING: route_setup={}us | Submitted POP_WAIT {} for queue {}/{} (timeout={}ms)", 
                            ctx.worker_id, route_time_us, request_id, queue_name, partition_name, timeout_ms);
                
                // Return immediately - sidecar will handle it
                return;
            }
            
            // Non-waiting mode: use sidecar for async pop
            // Use POP_BATCH for ALL POPs - enables batching when multiple requests arrive
            spdlog::info("[Worker {}] SPOP: Executing immediate pop for {}/{} (wait=false)", ctx.worker_id, queue_name, partition_name);
                
            // Register response for async delivery
            std::string request_id = global_response_registry->register_response(
                res, ctx.worker_id, nullptr
            );
            
            // Build sidecar request using POP_BATCH
            // Different partitions can be batched together efficiently
            // Same partition requests: only one wins lease, others return empty
            SidecarRequest sidecar_req;
            sidecar_req.op_type = SidecarOpType::POP_BATCH;
            sidecar_req.request_id = request_id;
            sidecar_req.queue_name = queue_name;
            sidecar_req.partition_name = partition_name;
            sidecar_req.consumer_group = consumer_group;
            sidecar_req.batch_size = options.batch;
            
            // Build JSON array for batch processing
            nlohmann::json batch_item;
            batch_item["idx"] = 0;
            batch_item["queue"] = queue_name;
            batch_item["partition"] = partition_name;  // Specific partition
            batch_item["consumerGroup"] = consumer_group;
            batch_item["batch"] = options.batch;
            batch_item["leaseTime"] = 0;  // Use queue config
            batch_item["subMode"] = options.subscription_mode.value_or("all");
            batch_item["subFrom"] = options.subscription_from.value_or("");
            
            nlohmann::json batch_array = nlohmann::json::array();
            batch_array.push_back(batch_item);
            
            sidecar_req.params = {batch_array.dump()};
            sidecar_req.item_count = 1;
            
            auto route_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - request_start).count();
            ctx.sidecar->submit(std::move(sidecar_req));
            spdlog::info("[Worker {}] SPOP TIMING: route_setup={}us | Submitted POP_BATCH (request_id={})", 
                         ctx.worker_id, route_time_us, request_id);
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // POP from queue (any partition) - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop/queue/:queue", [ctx](auto* res, auto* req) {
        auto request_start = std::chrono::steady_clock::now();
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
                // Register response with abort callback to clean up waiting request on disconnect
                std::string request_id = global_response_registry->register_response(res, ctx.worker_id,
                    [](const std::string& req_id) {
                        // Log abort - sidecar will handle cleanup via timeout
                        spdlog::info("QPOP: Connection aborted for {}", req_id);
                    });
            
                // Submit POP_WAIT to sidecar - it will manage waiting and backoff
                SidecarRequest sidecar_req;
                sidecar_req.op_type = SidecarOpType::POP_WAIT;
                sidecar_req.request_id = request_id;
                sidecar_req.queue_name = queue_name;
                sidecar_req.partition_name = "";  // Any partition
                sidecar_req.consumer_group = consumer_group;
                sidecar_req.batch_size = options.batch;
                sidecar_req.subscription_mode = options.subscription_mode.value_or("all");
                sidecar_req.subscription_from = options.subscription_from.value_or("");
                sidecar_req.wait_deadline = std::chrono::steady_clock::now() + 
                                            std::chrono::milliseconds(timeout_ms);
                sidecar_req.next_check = std::chrono::steady_clock::now();  // Check immediately
                
                // Build SQL for pop_messages_v2 (empty partition = any partition)
                sidecar_req.sql = "SELECT queen.pop_messages_v2($1, $2, $3, $4, $5, $6, $7)";
                sidecar_req.params = {
                    queue_name,
                    "",  // Empty = any partition
                    consumer_group,
                    std::to_string(options.batch),
                    "0",  // Use queue's configured lease_time
                    sidecar_req.subscription_mode,
                    sidecar_req.subscription_from
                };
                
                auto route_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - request_start).count();
                ctx.sidecar->submit(std::move(sidecar_req));
                
                // Register consumer presence for targeted notifications
                if (global_shared_state && global_shared_state->is_enabled()) {
                    global_shared_state->register_consumer(queue_name);
                }
                
                spdlog::info("[Worker {}] QPOP TIMING: route_setup={}us | Submitted POP_WAIT {} for queue {} (timeout={}ms)", 
                            ctx.worker_id, route_time_us, request_id, queue_name, timeout_ms);
                
                // Return immediately - sidecar will handle it
                return;
            }
            
            // Non-waiting mode: use sidecar for async pop
            // Use POP_BATCH for wildcard partition (any partition) - enables true batching
            spdlog::info("[Worker {}] QPOP: Executing immediate pop for {}/* (wait=false)", ctx.worker_id, queue_name);
                
            std::string request_id = global_response_registry->register_response(
                res, ctx.worker_id, nullptr
            );
            
            // Build sidecar request using POP_BATCH for wildcard partition
            // This enables true batching via partition pre-allocation
            SidecarRequest sidecar_req;
            sidecar_req.op_type = SidecarOpType::POP_BATCH;  // Use batchable POP for wildcard
            sidecar_req.request_id = request_id;
            sidecar_req.queue_name = queue_name;
            sidecar_req.partition_name = "";  // Wildcard
            sidecar_req.consumer_group = consumer_group;
            sidecar_req.batch_size = options.batch;
            
            // Build JSON array for batch processing (single request)
            nlohmann::json batch_item;
            batch_item["idx"] = 0;
            batch_item["queue"] = queue_name;
            batch_item["partition"] = "";  // Wildcard
            batch_item["consumerGroup"] = consumer_group;
            batch_item["batch"] = options.batch;
            batch_item["leaseTime"] = 0;  // Use queue config
            batch_item["subMode"] = options.subscription_mode.value_or("all");
            batch_item["subFrom"] = options.subscription_from.value_or("");
            
            nlohmann::json batch_array = nlohmann::json::array();
            batch_array.push_back(batch_item);
            
            sidecar_req.params = {batch_array.dump()};
            sidecar_req.item_count = 1;
            
            auto route_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - request_start).count();
            ctx.sidecar->submit(std::move(sidecar_req));
            spdlog::info("[Worker {}] QPOP TIMING: route_setup={}us | Submitted POP_BATCH (request_id={})", 
                         ctx.worker_id, route_time_us, request_id);
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

