#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/shared_state_manager.hpp"
#include "queen/sidecar_db_pool.hpp"
#include "queen/response_queue.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

// External globals
namespace queen {
extern std::shared_ptr<SharedStateManager> global_shared_state;
extern std::vector<std::shared_ptr<ResponseRegistry>> worker_response_registries;
}

namespace queen {
namespace routes {

void setup_ack_routes(uWS::App* app, const RouteContext& ctx) {
    // ASYNC ACK batch
    app->post("/api/v1/ack/batch", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("acknowledgments") || !body["acknowledgments"].is_array()) {
                        send_error_response(res, "acknowledgments array is required", 400);
                        return;
                    }
                    
                    auto acknowledgments = body["acknowledgments"];
                    std::string consumer_group = "__QUEUE_MODE__";
                    
                    if (body.contains("consumerGroup") && !body["consumerGroup"].is_null() && body["consumerGroup"].is_string()) {
                        consumer_group = body["consumerGroup"];
                    }
                    
                    // Validate each acknowledgment has required fields
                    std::vector<nlohmann::json> ack_items;
                    for (const auto& ack_json : acknowledgments) {
                        if (!ack_json.contains("transactionId") || ack_json["transactionId"].is_null() || !ack_json["transactionId"].is_string()) {
                            send_error_response(res, "Each acknowledgment must have a valid transactionId string", 400);
                            return;
                        }
                        
                        // CRITICAL: partition_id is now MANDATORY
                        if (!ack_json.contains("partitionId") || ack_json["partitionId"].is_null() || !ack_json["partitionId"].is_string()) {
                            send_error_response(res, "Each acknowledgment must have a valid partitionId string to ensure message uniqueness", 400);
                            return;
                        }
                        
                        // Build ack item with consumer group
                        nlohmann::json ack_item = ack_json;
                        ack_item["consumerGroup"] = consumer_group;
                        ack_items.push_back(ack_item);
                    }
                    
                    //spdlog::info("[Worker {}] ACK BATCH: Executing batch ACK ({} items)", ctx.worker_id, ack_items.size());
                    
                    std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
                        res, ctx.worker_id, nullptr
                    );
                        
                    // Build JSON array with index for result routing
                    nlohmann::json ack_json = nlohmann::json::array();
                    int idx = 0;
                    for (const auto& ack : ack_items) {
                        nlohmann::json ack_item = ack;
                        ack_item["index"] = idx++;
                        ack_json.push_back(ack_item);
                    }
                    
                    SidecarRequest sidecar_req;
                    sidecar_req.op_type = SidecarOpType::ACK_BATCH;
                    sidecar_req.request_id = request_id;
                    sidecar_req.sql = "SELECT queen.ack_messages_v2($1::jsonb)";
                    sidecar_req.params = {ack_json.dump()};
                    sidecar_req.item_count = ack_items.size();
                    
                    ctx.sidecar->submit(std::move(sidecar_req));
                    spdlog::debug("[Worker {}] ACK BATCH: Submitted {} items to sidecar (request_id={})", 
                                 ctx.worker_id, ack_items.size(), request_id);
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // ASYNC Single ACK
    app->post("/api/v1/ack", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    std::string transaction_id = "";
                    if (body.contains("transactionId") && !body["transactionId"].is_null() && body["transactionId"].is_string()) {
                        transaction_id = body["transactionId"];
                    } else {
                        send_error_response(res, "transactionId is required", 400);
                        return;
                    }
                    
                    std::string status = "completed";
                    if (body.contains("status") && !body["status"].is_null() && body["status"].is_string()) {
                        status = body["status"];
                    }
                    
                    std::string consumer_group = "__QUEUE_MODE__";
                    if (body.contains("consumerGroup") && !body["consumerGroup"].is_null() && body["consumerGroup"].is_string()) {
                        consumer_group = body["consumerGroup"];
                    }
                    
                    std::optional<std::string> error;
                    if (body.contains("error") && !body["error"].is_null() && body["error"].is_string()) {
                        error = body["error"];
                    }
                    
                    std::optional<std::string> lease_id;
                    if (body.contains("leaseId") && !body["leaseId"].is_null() && body["leaseId"].is_string()) {
                        lease_id = body["leaseId"];
                    }
                    
                    // CRITICAL: partition_id is now MANDATORY to prevent acking wrong message
                    // when transactionId is not unique across partitions
                    std::optional<std::string> partition_id;
                    if (body.contains("partitionId") && !body["partitionId"].is_null() && body["partitionId"].is_string()) {
                        partition_id = body["partitionId"];
                    } else {
                        send_error_response(res, "partitionId is required to ensure message uniqueness", 400);
                        return;
                    }
                    
                    std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
                        res, ctx.worker_id, nullptr
                    );
                    
                    // Build JSON array with single ACK
                    nlohmann::json ack_json = nlohmann::json::array();
                    ack_json.push_back({
                        {"index", 0},
                                {"transactionId", transaction_id},
                        {"partitionId", partition_id.value()},
                        {"leaseId", lease_id.value_or("")},
                        {"status", status},
                        {"consumerGroup", consumer_group},
                        {"error", error.value_or("")}
                    });
                    
                    SidecarRequest sidecar_req;
                    sidecar_req.op_type = SidecarOpType::ACK_BATCH;  // Use batch procedure even for single ACK
                    sidecar_req.request_id = request_id;
                    sidecar_req.sql = "SELECT queen.ack_messages_v2($1::jsonb)";
                    sidecar_req.params = {ack_json.dump()};
                    sidecar_req.item_count = 1;
                    
                    ctx.sidecar->submit(std::move(sidecar_req));
                    spdlog::debug("[Worker {}] ACK: Submitted to sidecar (request_id={})", 
                                 ctx.worker_id, request_id);
                    
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

