#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/analytics_manager.hpp"

namespace queen {
namespace routes {

void setup_dlq_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /api/v1/dlq - Get dead letter queue messages
    app->get("/api/v1/dlq", [ctx](auto* res, auto* req) {
        try {
            AnalyticsManager::DLQFilters filters;
            filters.queue = get_query_param(req, "queue");
            filters.consumer_group = get_query_param(req, "consumerGroup");
            filters.partition = get_query_param(req, "partition");
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.limit = get_query_param_int(req, "limit", 100);
            filters.offset = get_query_param_int(req, "offset", 0);
            
            auto response = ctx.analytics_manager->get_dlq_messages(filters);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

