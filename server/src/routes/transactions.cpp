#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include <spdlog/spdlog.h>

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
                    
                    spdlog::info("[Worker {}] TRANSACTION: Executing immediate transaction ({} operations)", ctx.worker_id, operations.size());
                    
                    // Execute entire transaction directly in uWS event loop
                    try {
                        // Convert operations to vector for execute_transaction
                        std::vector<nlohmann::json> ops_vec;
                        for (const auto& op : operations) {
                            ops_vec.push_back(op);
                        }
                        
                        auto txn_result = ctx.async_queue_manager->execute_transaction(ops_vec);
                        
                        nlohmann::json response = {
                            {"transactionId", txn_result.transaction_id},
                            {"success", txn_result.success},
                            {"results", txn_result.results}
                        };
                        
                        if (txn_result.error.has_value()) {
                            response["error"] = *txn_result.error;
                        }
                        
                        int status_code = txn_result.success ? 200 : 400;
                        send_json_response(res, response, status_code);
                        spdlog::info("[Worker {}] TRANSACTION: Sent response ({} operations, success={})", 
                                   ctx.worker_id, txn_result.results.size(), txn_result.success);
                        
                    } catch (const std::exception& e) {
                        send_error_response(res, e.what(), 500);
                        spdlog::error("[Worker {}] TRANSACTION: Error: {}", ctx.worker_id, e.what());
                    }
                    
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

