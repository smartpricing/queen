#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/shared_state_manager.hpp"
#include <spdlog/spdlog.h>

// External reference to global shared state manager
extern std::shared_ptr<queen::SharedStateManager> global_shared_state;

namespace queen {
namespace routes {

void setup_maintenance_routes(uWS::App* app, const RouteContext& ctx) {
    // GET maintenance mode status (always fetch fresh from DB)
    app->get("/api/v1/system/maintenance", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            // Force fresh check from database (bypass cache for status endpoint)
            bool current_mode = ctx.async_queue_manager->get_maintenance_mode_fresh();
            auto buffer_stats = ctx.async_queue_manager->get_buffer_stats();
            
            nlohmann::json response = {
                {"maintenanceMode", current_mode},
                {"bufferedMessages", ctx.async_queue_manager->get_buffer_pending_count()},
                {"bufferHealthy", ctx.async_queue_manager->is_buffer_healthy()},
                {"bufferStats", buffer_stats}
            };
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // POST toggle maintenance mode
    app->post("/api/v1/system/maintenance", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("enabled") || !body["enabled"].is_boolean()) {
                        send_error_response(res, "enabled (boolean) is required", 400);
                        return;
                    }
                    
                    bool enable = body["enabled"];
                    ctx.async_queue_manager->set_maintenance_mode(enable);
                    
                    nlohmann::json response = {
                        {"maintenanceMode", enable},
                        {"bufferedMessages", ctx.async_queue_manager->get_buffer_pending_count()},
                        {"bufferHealthy", ctx.async_queue_manager->is_buffer_healthy()},
                        {"message", enable ? 
                            "Maintenance mode ENABLED. All PUSHes routing to file buffer." :
                            "Maintenance mode DISABLED. Background processor will drain buffer to DB."
                        }
                    };
                    
                    send_json_response(res, response);
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // GET shared state / UDPSYNC cache stats
    app->get("/api/v1/system/shared-state", [](auto* res, auto* req) {
        (void)req;
        
        nlohmann::json stats;
        if (global_shared_state) {
            stats = global_shared_state->get_stats();
        } else {
            stats = {{"enabled", false}, {"reason", "not_initialized"}};
        }
        
        send_json_response(res, stats, 200);
    });
}

} // namespace routes
} // namespace queen

