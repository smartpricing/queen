#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/queue_types.hpp"
#include "queen/file_buffer.hpp"
#include "queen/inter_instance_comms.hpp"
#include "queen/poll_intention_registry.hpp"
#include "queen/response_queue.hpp"
#include "queen/sidecar_db_pool.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <set>

namespace queen {

// External globals (declared in acceptor_server.cpp)
extern std::shared_ptr<ResponseRegistry> global_response_registry;
extern SidecarDbPool* global_sidecar_pool_ptr;

namespace routes {

void setup_push_routes(uWS::App* app, const RouteContext& ctx) {
    // PUSH endpoint with multiple modes:
    // 1. Legacy (push_use_stored_procedure=false): Multiple round trips, blocking
    // 2. Stored Procedure (push_use_stored_procedure=true, push_use_sidecar=false): Single round trip, blocking
    // 3. Sidecar (push_use_sidecar=true): Single round trip, NON-BLOCKING
    app->post("/api/v1/push", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("items") || !body["items"].is_array()) {
                        send_error_response(res, "items array is required", 400);
                        return;
                    }
                    
                    size_t item_count = body["items"].size();
                    spdlog::debug("[Worker {}] PUSH: Processing {} items", ctx.worker_id, item_count);
                    
                    // Parse and validate items
                    std::vector<PushItem> items;
                    for (const auto& item_json : body["items"]) {
                        // Validate partition type if present
                        if (item_json.contains("partition") && !item_json["partition"].is_string()) {
                            send_error_response(res, "Partition must be a string", 400);
                            return;
                        }
                        
                        // Validate queue type
                        if (!item_json.contains("queue") || !item_json["queue"].is_string()) {
                            send_error_response(res, "Queue must be a string", 400);
                            return;
                        }
                        
                        // Validate transactionId type if present
                        if (item_json.contains("transactionId") && !item_json["transactionId"].is_string()) {
                            send_error_response(res, "TransactionId must be a string", 400);
                            return;
                        }
                        
                        // Validate traceId type if present
                        if (item_json.contains("traceId") && !item_json["traceId"].is_string()) {
                            send_error_response(res, "TraceId must be a string", 400);
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
                        items.push_back(std::move(item));
                    }
                    
                    // =================================================================
                    // MODE 3: SIDECAR (TRUE ASYNC - NON-BLOCKING)
                    // =================================================================
                    if (ctx.config.queue.push_use_sidecar && global_sidecar_pool_ptr) {
                        // Register response for async delivery
                        std::string request_id = global_response_registry->register_response(
                            res, ctx.worker_id, nullptr
                        );
                        
                        // Build JSON array for stored procedure
                        nlohmann::json items_json = nlohmann::json::array();
                        for (const auto& item : items) {
                            nlohmann::json item_json = {
                                {"queue", item.queue},
                                {"partition", item.partition},
                                {"payload", item.payload}
                            };
                            if (item.transaction_id.has_value() && !item.transaction_id->empty()) {
                                item_json["transactionId"] = *item.transaction_id;
                            }
                            if (item.trace_id.has_value() && !item.trace_id->empty()) {
                                item_json["traceId"] = *item.trace_id;
                            }
                            items_json.push_back(item_json);
                        }
                        
                        // Build sidecar request
                        SidecarRequest req;
                        req.request_id = request_id;
                        req.sql = "SELECT queen.push_messages_v2($1::jsonb, $2::boolean, $3::boolean)";
                        req.params = {items_json.dump(), "true", "true"};
                        req.worker_id = ctx.worker_id;
                        req.item_count = items.size();  // For micro-batching decisions
                        
                        // Submit to sidecar - RETURNS IMMEDIATELY!
                        global_sidecar_pool_ptr->submit(std::move(req));
                        
                        spdlog::debug("[Worker {}] PUSH: Submitted {} items to sidecar (request_id={})", 
                                     ctx.worker_id, item_count, request_id);
                        
                        // Don't send response here - sidecar will deliver it via response queue
                        return;
                    }
                    
                    // =================================================================
                    // MODE 1 & 2: BLOCKING (stored procedure or legacy)
                    // =================================================================
                    auto push_start = std::chrono::steady_clock::now();
                    std::vector<PushResult> results;
                    
                    if (ctx.config.queue.push_use_stored_procedure) {
                        // MODE 2: Stored procedure (single round trip, but blocking)
                        spdlog::debug("[Worker {}] PUSH: Using stored procedure for {} items", 
                                     ctx.worker_id, items.size());
                        results = ctx.async_queue_manager->push_messages_sp(items);
                    } else {
                        // MODE 1: Legacy (multiple round trips)
                        results = ctx.async_queue_manager->push_messages(items);
                    }
                    
                    auto push_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - push_start).count();
                    
                    spdlog::info("[Worker {}] PUSH: {} completed in {}ms ({} items)", 
                                ctx.worker_id, 
                                ctx.config.queue.push_use_stored_procedure ? "push_messages_sp" : "push_messages",
                                push_ms, items.size());
                    
                    // Notify local poll registry + peers of new messages
                    {
                        std::set<std::pair<std::string, std::string>> notified;
                        for (size_t i = 0; i < results.size(); i++) {
                            // Status is "queued" for successful push, or "buffered" for file buffer
                            if ((results[i].status == "queued" || results[i].status == "buffered") && i < items.size()) {
                                auto key = std::make_pair(items[i].queue, items[i].partition);
                                if (notified.find(key) == notified.end()) {
                                    // Notify LOCAL poll registry (this server's own poll workers)
                                    if (queen::global_poll_intention_registry) {
                                        queen::global_poll_intention_registry->reset_backoff_for_queue_partition(
                                            items[i].queue,
                                            items[i].partition
                                        );
                                        spdlog::debug("[Worker {}] PUSH: Notify local poll workers for {}:{}", 
                                                    ctx.worker_id, items[i].queue, items[i].partition);
                                    }
                                    
                                    // Notify PEER servers via WebSocket
                                    if (global_inter_instance_comms && global_inter_instance_comms->is_enabled()) {
                                        global_inter_instance_comms->notify_message_available(
                                            items[i].queue,
                                            items[i].partition
                                        );
                                        spdlog::debug("[Worker {}] PUSH: Notify peers for {}:{}", 
                                                    ctx.worker_id, items[i].queue, items[i].partition);
                                    }
                                    
                                    notified.insert(key);
                                }
                            }
                        }
                    }
                    
                    // Convert results to JSON and respond immediately
                    nlohmann::json json_results = nlohmann::json::array();
                    for (const auto& result : results) {
                        nlohmann::json json_result;
                        json_result["transaction_id"] = result.transaction_id;
                        json_result["status"] = result.status;
                        
                        if (result.error) {
                            json_result["error"] = *result.error;
                        }
                        if (result.message_id) {
                            json_result["message_id"] = *result.message_id;
                        }
                        if (result.trace_id) {
                            json_result["trace_id"] = *result.trace_id;
                        }
                        
                        json_results.push_back(json_result);
                    }
                    
                    // Send response immediately (we're in the uWebSockets thread)
                    send_json_response(res, json_results, 201);
                    
                } catch (const std::exception& e) {
                    spdlog::error("[Worker {}] PUSH: Error: {}", ctx.worker_id, e.what());
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
