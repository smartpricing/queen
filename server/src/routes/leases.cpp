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

void setup_lease_routes(uWS::App* app, const RouteContext& ctx) {
    // Lease extension
    app->post("/api/v1/lease/:leaseId/extend", [ctx](auto* res, auto* req) {
        read_json_body(res,
            [res, ctx, req](const nlohmann::json& body) {
                try {
                    std::string lease_id = std::string(req->getParameter(0));
                    int seconds = body.value("seconds", 60);
                    
                    spdlog::debug("[Worker {}] Extending lease: {}, seconds: {}", ctx.worker_id, lease_id, seconds);
                    
                    // Use sidecar for async lease renewal if enabled
                    if (ctx.config.queue.renew_lease_use_sidecar && global_sidecar_pool_ptr) {
                        std::string request_id = global_response_registry->register_response(
                            res, ctx.worker_id, nullptr
                        );
                        
                        // Build JSON array with single renewal
                        nlohmann::json items_json = nlohmann::json::array();
                        items_json.push_back({
                            {"index", 0},
                            {"leaseId", lease_id},
                            {"extendSeconds", seconds}
                        });
                        
                        SidecarRequest req;
                        req.op_type = SidecarOpType::RENEW_LEASE;
                        req.request_id = request_id;
                        req.sql = "SELECT queen.renew_lease_v2($1::jsonb)";
                        req.params = {items_json.dump()};
                        req.worker_id = ctx.worker_id;
                        req.item_count = 1;
                        
                        global_sidecar_pool_ptr->submit(std::move(req));
                        spdlog::debug("[Worker {}] RENEW_LEASE: Submitted to sidecar (request_id={})", 
                                     ctx.worker_id, request_id);
                        return;
                    }
                    
                    // Fallback: use AsyncQueueManager directly
                    bool success = ctx.async_queue_manager->extend_message_lease(lease_id, seconds);
                    
                    if (success) {
                        nlohmann::json response = {
                            {"leaseId", lease_id},
                            {"extended", true},
                            {"seconds", seconds}
                        };
                        send_json_response(res, response);
                    } else {
                        send_error_response(res, "Lease not found or expired", 404);
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

