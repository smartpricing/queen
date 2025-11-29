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

namespace routes {

void setup_push_routes(uWS::App* app, const RouteContext& ctx) {
    // PUSH endpoint - uses per-worker sidecar for true async non-blocking operation
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
                    
                    // Handle empty push immediately - no need for DB call
                    if (items.empty()) {
                        send_json_response(res, nlohmann::json::array(), 201);
                        return;
                    }
                    
                        // Register response for async delivery
                        std::string request_id = global_response_registry->register_response(
                            res, ctx.worker_id, nullptr
                        );
                        
                        // Build JSON array for stored procedure
                    // Generate UUIDv7 message IDs on C++ side
                        nlohmann::json items_json = nlohmann::json::array();
                        for (const auto& item : items) {
                            nlohmann::json item_json = {
                                {"queue", item.queue},
                                {"partition", item.partition},
                                {"payload", item.payload},
                                {"messageId", ctx.async_queue_manager->generate_uuid()}  // UUIDv7
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
                    SidecarRequest sidecar_req;
                    sidecar_req.op_type = SidecarOpType::PUSH;
                    sidecar_req.request_id = request_id;
                    sidecar_req.sql = "SELECT queen.push_messages_v2($1::jsonb, $2::boolean, $3::boolean)";
                    sidecar_req.params = {items_json.dump(), "true", "true"};
                    sidecar_req.item_count = items.size();
                        
                    // Submit to per-worker sidecar - RETURNS IMMEDIATELY!
                    ctx.sidecar->submit(std::move(sidecar_req));
                        
                        spdlog::debug("[Worker {}] PUSH: Submitted {} items to sidecar (request_id={})", 
                                     ctx.worker_id, item_count, request_id);
                        
                    // Don't send response here - sidecar will deliver it via loop->defer()
                    
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
