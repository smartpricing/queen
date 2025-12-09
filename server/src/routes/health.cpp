#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/shared_state_manager.hpp"
#include <spdlog/spdlog.h>

namespace queen {
namespace routes {

void setup_cors_routes(uWS::App* app) {
    app->options("/*", [](auto* res, auto* req) {
        (void)req;
        setup_cors_headers(res);
        res->writeStatus("204");
        res->end();
    });
}

void setup_health_routes(uWS::App* app, const RouteContext& ctx) {
    app->get("/health", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            bool healthy = ctx.async_queue_manager->health_check();
            nlohmann::json response = {
                {"status", healthy ? "healthy" : "unhealthy"},
                {"database", healthy ? "connected" : "disconnected"},
                {"server", "C++ Queen Server (libqueen)"},
                {"worker_id", ctx.worker_id},
                {"version", "1.0.0"}
            };
            
            // Add shared state / cluster sync status
            if (global_shared_state) {
                response["shared_state"] = global_shared_state->get_stats();
            } else {
                response["shared_state"] = {{"enabled", false}};
            }
            
            // Note: Queen stats integration can be added later
            response["queen"] = {
                {"worker_id", ctx.worker_id},
                {"status", "active"}
            };
            
            send_json_response(res, response, healthy ? 200 : 503);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen
