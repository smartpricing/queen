#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/queue_types.hpp"
#include "queen/file_buffer.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

namespace queen {
namespace routes {

void setup_push_routes(uWS::App* app, const RouteContext& ctx) {
    // ASYNC PUSH - NON-BLOCKING WITH ASYNC DB POOL (NO THREAD POOL NEEDED!)
    app->post("/api/v1/push", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("items") || !body["items"].is_array()) {
                        send_error_response(res, "items array is required", 400);
                        return;
                    }
                    
                    spdlog::debug("[Worker {}] PUSH: Processing {} items (async, non-blocking)", 
                                 ctx.worker_id, body["items"].size());
                    
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
                    
                    // Execute async push operation (non-blocking, polls socket internally)
                    // This runs directly in the uWebSockets thread!
                    auto push_start = std::chrono::steady_clock::now();
                    auto results = ctx.async_queue_manager->push_messages(items);
                    auto push_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - push_start).count();
                    
                    spdlog::info("[Worker {}] PUSH: Async push_messages completed in {}ms ({} items)", 
                                ctx.worker_id, push_ms, items.size());
                    
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

