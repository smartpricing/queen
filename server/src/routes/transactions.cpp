#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/sidecar_db_pool.hpp"
#include "queen/response_queue.hpp"
#include "queen/encryption.hpp"
#include "queen/shared_state_manager.hpp"
#include <spdlog/spdlog.h>

// External globals
namespace queen {
extern std::vector<std::shared_ptr<ResponseRegistry>> worker_response_registries;
extern std::shared_ptr<SharedStateManager> global_shared_state;
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
                    
                    std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
                        res, ctx.worker_id, nullptr
                    );
                    
                    // Flatten and normalize operations for stored procedure
                    // Client sends: {type:"push", items:[{queue, payload}]}
                    // SP expects:   {type:"push", queue, payload, messageId, is_encrypted}
                    
                    // Get encryption service once
                    EncryptionService* enc_service = get_encryption_service();
                    
                    nlohmann::json ops_json = nlohmann::json::array();
                        for (const auto& op : operations) {
                        std::string op_type = op.value("type", "");
                        
                        if (op_type == "push" && op.contains("items")) {
                            // Flatten items array into individual push operations
                            for (const auto& item : op["items"]) {
                                std::string queue_name = item.value("queue", "");
                                nlohmann::json payload = item.value("payload", nlohmann::json::object());
                                
                                // Check if queue has encryption enabled
                                bool queue_encryption_enabled = false;
                                if (global_shared_state) {
                                    auto cached_config = global_shared_state->get_queue_config(queue_name);
                                    if (cached_config) {
                                        queue_encryption_enabled = cached_config->encryption_enabled;
                                    }
                                }
                                
                                // Encrypt payload if needed
                                nlohmann::json payload_to_store = payload;
                                bool is_encrypted = false;
                                
                                if (queue_encryption_enabled && enc_service && enc_service->is_enabled()) {
                                    std::string payload_str = payload.dump();
                                    auto encrypted = enc_service->encrypt_payload(payload_str);
                                    if (encrypted.has_value()) {
                                        payload_to_store = {
                                            {"encrypted", encrypted->encrypted},
                                            {"iv", encrypted->iv},
                                            {"authTag", encrypted->auth_tag}
                                        };
                                        is_encrypted = true;
                                        spdlog::info("[Worker {}] TRANSACTION: Encrypted payload for queue '{}'", 
                                                    ctx.worker_id, queue_name);
                                    }
                                }
                                
                                nlohmann::json flat_op = {
                                    {"type", "push"},
                                    {"queue", queue_name},
                                    {"partition", item.value("partition", "Default")},
                                    {"payload", payload_to_store},
                                    {"is_encrypted", is_encrypted},
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
                    
                    SidecarRequest sidecar_req;
                    sidecar_req.op_type = SidecarOpType::TRANSACTION;
                    sidecar_req.request_id = request_id;
                    sidecar_req.sql = "SELECT queen.execute_transaction_v2($1::jsonb)";
                    sidecar_req.params = {ops_json.dump()};
                    sidecar_req.item_count = ops_json.size();
                    
                    ctx.sidecar->submit(std::move(sidecar_req));
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
