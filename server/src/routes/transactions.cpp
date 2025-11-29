#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/sidecar_db_pool.hpp"
#include "queen/response_queue.hpp"
#include <spdlog/spdlog.h>

// External globals
namespace queen {
extern std::shared_ptr<ResponseRegistry> global_response_registry;
extern SidecarDbPool* global_sidecar_pool_ptr;
}

namespace queen {
namespace routes {

void setup_transaction_routes(uWS::App* app, const RouteContext& ctx) {
    // ASYNC Transaction API (atomic operations)
    app->post("/api/v1/transaction", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("operations") || !body["operations"].is_array()) {
                        send_error_response(res, "operations array required", 400);
                        return;
                    }
                    
                    auto operations = body["operations"];
                    if (operations.empty()) {
                        send_error_response(res, "operations array cannot be empty", 400);
                        return;
                    }
                    
                    spdlog::info("[Worker {}] TRANSACTION: Executing transaction ({} operations)", ctx.worker_id, operations.size());
                    
                    std::string request_id = global_response_registry->register_response(
                        res, ctx.worker_id, nullptr
                    );
                    
                    // Flatten and normalize operations for stored procedure
                    // Client sends: {type:"push", items:[{queue, payload}]}
                    // SP expects:   {type:"push", queue, payload, messageId}
                    nlohmann::json ops_json = nlohmann::json::array();
                        for (const auto& op : operations) {
                        std::string op_type = op.value("type", "");
                        
                        if (op_type == "push" && op.contains("items")) {
                            // Flatten items array into individual push operations
                            for (const auto& item : op["items"]) {
                                nlohmann::json flat_op = {
                                    {"type", "push"},
                                    {"queue", item.value("queue", "")},
                                    {"partition", item.value("partition", "Default")},
                                    {"payload", item.value("payload", nlohmann::json::object())},
                                    {"messageId", ctx.async_queue_manager->generate_uuid()}
                        };
                                if (item.contains("transactionId")) {
                                    flat_op["transactionId"] = item["transactionId"];
                        }
                                if (item.contains("traceId")) {
                                    flat_op["traceId"] = item["traceId"];
                                }
                                ops_json.push_back(flat_op);
                            }
                        } else if (op_type == "ack") {
                            // ACK operations are already in correct format
                            ops_json.push_back(op);
                        } else {
                            // Copy other operations as-is
                            ops_json.push_back(op);
                        }
                    }
                    
                    SidecarRequest req;
                    req.op_type = SidecarOpType::TRANSACTION;
                    req.request_id = request_id;
                    req.sql = "SELECT queen.execute_transaction_v2($1::jsonb)";
                    req.params = {ops_json.dump()};
                    req.worker_id = ctx.worker_id;
                    req.item_count = ops_json.size();
                    
                    global_sidecar_pool_ptr->submit(std::move(req));
                    spdlog::debug("[Worker {}] TRANSACTION: Submitted {} ops to sidecar (request_id={})", 
                                 ctx.worker_id, ops_json.size(), request_id);
                    
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

