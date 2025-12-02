#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/sidecar_db_pool.hpp"
#include "queen/response_queue.hpp"
#include <spdlog/spdlog.h>

// External globals
namespace queen {
extern std::vector<std::shared_ptr<ResponseRegistry>> worker_response_registries;
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
                    
                    std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
                        res, ctx.worker_id, nullptr
                    );
                    
                    // Build JSON array with single renewal
                    nlohmann::json items_json = nlohmann::json::array();
                    items_json.push_back({
                        {"index", 0},
                            {"leaseId", lease_id},
                        {"extendSeconds", seconds}
                    });
                    
                    SidecarRequest sidecar_req;
                    sidecar_req.op_type = SidecarOpType::RENEW_LEASE;
                    sidecar_req.request_id = request_id;
                    sidecar_req.sql = "SELECT queen.renew_lease_v2($1::jsonb)";
                    sidecar_req.params = {items_json.dump()};
                    sidecar_req.item_count = 1;
                    
                    ctx.sidecar->submit(std::move(sidecar_req));
                    spdlog::debug("[Worker {}] RENEW_LEASE: Submitted to sidecar (request_id={})", 
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
