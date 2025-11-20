#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/analytics_manager.hpp"

namespace queen {
namespace routes {

void setup_metrics_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /metrics - Performance metrics
    app->get("/metrics", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            auto response = ctx.analytics_manager->get_metrics();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

