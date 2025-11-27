#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/inter_instance_comms.hpp"
#include "queen/shared_state_manager.hpp"
#include <spdlog/spdlog.h>

namespace queen {
namespace routes {

void setup_internal_routes(uWS::App* app, const RouteContext& ctx) {
    (void)ctx;  // Context not needed for these endpoints
    
    // ============================================================
    // HTTP endpoint for peer-to-peer notifications
    // Peers POST notifications here to trigger local poll worker wake-up
    // ============================================================
    app->post("/internal/api/notify", [](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res](const nlohmann::json& body) {
                try {
                    auto notification = PeerNotification::from_json(body);
                    
                    if (global_inter_instance_comms) {
                        global_inter_instance_comms->handle_peer_notification(notification);
                    }
                    
                    send_json_response(res, {{"status", "ok"}}, 200);
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 400);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // ============================================================
    // Stats endpoint for monitoring inter-instance communication
    // ============================================================
    app->get("/internal/api/inter-instance/stats", [](auto* res, auto* req) {
        (void)req;
        
        nlohmann::json stats;
        if (global_inter_instance_comms) {
            stats = global_inter_instance_comms->get_stats();
        } else {
            stats = {{"enabled", false}, {"reason", "not_initialized"}};
        }
        
        send_json_response(res, stats, 200);
    });
    
    // ============================================================
    // Stats endpoint for SharedStateManager (UDPSYNC distributed cache)
    // ============================================================
    app->get("/internal/api/shared-state/stats", [](auto* res, auto* req) {
        (void)req;
        
        nlohmann::json stats;
        if (global_shared_state) {
            stats = global_shared_state->get_stats();
        } else {
            stats = {{"enabled", false}, {"reason", "not_initialized"}};
        }
        
        send_json_response(res, stats, 200);
    });
    
    spdlog::debug("Internal routes configured (inter-instance HTTP endpoints, shared state stats)");
}

} // namespace routes
} // namespace queen
