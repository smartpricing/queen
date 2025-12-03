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
extern std::vector<std::shared_ptr<ResponseRegistry>> worker_response_registries;
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
    // SPECIFIC POP from queue/partition
    app->get("/api/v1/pop/queue/:queue/partition/:partition", [ctx](auto* res, auto* req) {
        auto request_start = std::chrono::steady_clock::now();
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string partition_name = std::string(req->getParameter(1));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", ctx.config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", ctx.config.queue.default_batch_size);
            
            // Pool stats available via ctx.async_queue_manager->get_pool_stats() if needed for debugging
            
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
                std::string request_id = worker_response_registries[ctx.worker_id]->register_response(res, ctx.worker_id,
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
                
                // SQL is built by sidecar using pop_messages_batch_v2
                
                // Register consumer presence for targeted notifications
                if (global_shared_state && global_shared_state->is_enabled()) {
                    global_shared_state->register_consumer(queue_name);
                }

                ctx.sidecar->submit(std::move(sidecar_req));
                
                //spdlog::info("[Worker {}] SPOP TIMING: route_setup={}us | Submitted POP_WAIT {} for queue {}/{} (timeout={}ms)", ctx.worker_id, route_time_us, request_id, queue_name, partition_name, timeout_ms);
                
                // Return immediately - sidecar will handle it
                return;
            }
            
            // Non-waiting mode: use state machine for async parallel pop
            // State machine enables true parallel processing across DB connections
            // Each specific-partition POP is processed independently
                
            // Register response for async delivery
            std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
                res, ctx.worker_id, nullptr
            );
            
            // Build sidecar request for state machine processing
            // State machine processes requests in parallel on multiple DB connections
            SidecarRequest sidecar_req;
            sidecar_req.op_type = SidecarOpType::POP_BATCH;
            sidecar_req.request_id = request_id;
            sidecar_req.queue_name = queue_name;
            sidecar_req.partition_name = partition_name;
            sidecar_req.consumer_group = consumer_group;
            sidecar_req.batch_size = options.batch;
            sidecar_req.subscription_mode = options.subscription_mode.value_or("all");
            sidecar_req.subscription_from = options.subscription_from.value_or("");
            
            auto route_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - request_start).count();
            (void)route_time_us;  // Silence unused variable warning in release builds
            
            // Use state machine for specific-partition POPs
            // This enables parallel processing across multiple DB connections
            ctx.sidecar->submit_pop_batch_sm({std::move(sidecar_req)});
            
            /*spdlog::info("[Worker {}] SPOP TIMING: route_setup={}us | Submitted POP_SM (request_id={})", 
                         ctx.worker_id, route_time_us, request_id);*/
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // POP from queue (any partition)
    app->get("/api/v1/pop/queue/:queue", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", ctx.config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", ctx.config.queue.default_batch_size);
            
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
                std::string request_id = worker_response_registries[ctx.worker_id]->register_response(res, ctx.worker_id,
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
                
                // SQL is built by sidecar using pop_messages_batch_v2
                ctx.sidecar->submit(std::move(sidecar_req));
                
                // Register consumer presence for targeted notifications
                if (global_shared_state && global_shared_state->is_enabled()) {
                    global_shared_state->register_consumer(queue_name);
                }

                // Return immediately - sidecar will handle it
                return;
            }
                
            std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
                res, ctx.worker_id, nullptr
            );
            
            // Build sidecar request for state machine processing
            // State machine handles wildcard via RESOLVING -> LEASING -> FETCHING flow
            SidecarRequest sidecar_req;
            sidecar_req.op_type = SidecarOpType::POP_BATCH;
            sidecar_req.request_id = request_id;
            sidecar_req.queue_name = queue_name;
            sidecar_req.partition_name = "";  // Wildcard - state machine will resolve
            sidecar_req.consumer_group = consumer_group;
            sidecar_req.batch_size = options.batch;
            sidecar_req.subscription_mode = options.subscription_mode.value_or("all");
            sidecar_req.subscription_from = options.subscription_from.value_or("");
            
            // Use state machine for parallel processing
            ctx.sidecar->submit_pop_batch_sm({std::move(sidecar_req)});
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

