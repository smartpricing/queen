#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/inter_instance_comms.hpp"
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
                {"server", "C++ Queen Server (Acceptor/Worker)"},
                {"worker_id", ctx.worker_id},
                {"version", "1.0.0"}
            };
            
            // Add peer notification status
            if (global_inter_instance_comms) {
                response["peer_notify"] = global_inter_instance_comms->get_stats();
            } else {
                response["peer_notify"] = {{"enabled", false}};
            }
            
            send_json_response(res, response, healthy ? 200 : 503);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

