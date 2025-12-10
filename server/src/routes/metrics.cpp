#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"

namespace queen {
namespace routes {

void setup_metrics_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /metrics - Performance metrics (pool stats)
    app->get("/metrics", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            auto pool_stats = ctx.async_queue_manager->get_pool_stats();
            nlohmann::json response = {
                {"uptime", 0},
                {"requests", {
                    {"total", 0},
                    {"rate", 0}
                }},
                {"messages", {
                    {"total", 0},
                    {"rate", 0}
                }},
                {"database", {
                    {"poolSize", pool_stats.total},
                    {"idleConnections", pool_stats.available},
                    {"waitingRequests", 0}
                }},
                {"memory", {
                    {"rss", 0},
                    {"heapTotal", 0},
                    {"heapUsed", 0},
                    {"external", 0},
                    {"arrayBuffers", 0}
                }},
                {"cpu", {
                    {"user", 0},
                    {"system", 0}
                }}
            };
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen
