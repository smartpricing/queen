#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/analytics_manager.hpp"
#include "queen/file_buffer.hpp"

namespace queen {
namespace routes {

void setup_status_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /api/v1/status - Dashboard overview
    app->get("/api/v1/status", [ctx](auto* res, auto* req) {
        try {
            AnalyticsManager::StatusFilters filters;
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.queue = get_query_param(req, "queue");
            filters.namespace_name = get_query_param(req, "namespace");
            filters.task = get_query_param(req, "task");
            
            auto response = ctx.analytics_manager->get_status(filters);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/queues - Queues list
    app->get("/api/v1/status/queues", [ctx](auto* res, auto* req) {
        try {
            AnalyticsManager::StatusFilters filters;
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.namespace_name = get_query_param(req, "namespace");
            filters.task = get_query_param(req, "task");
            
            int limit = get_query_param_int(req, "limit", 100);
            int offset = get_query_param_int(req, "offset", 0);
            
            auto response = ctx.analytics_manager->get_status_queues(filters, limit, offset);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/queues/:queue - Queue detail
    app->get("/api/v1/status/queues/:queue", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            auto response = ctx.analytics_manager->get_queue_detail(queue_name);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not found") != std::string::npos) {
                send_error_response(res, e.what(), 404);
            } else {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // GET /api/v1/status/queues/:queue/messages - Queue messages
    app->get("/api/v1/status/queues/:queue/messages", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            int limit = get_query_param_int(req, "limit", 50);
            int offset = get_query_param_int(req, "offset", 0);
            
            auto response = ctx.analytics_manager->get_queue_messages(queue_name, limit, offset);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/analytics - Analytics
    app->get("/api/v1/status/analytics", [ctx](auto* res, auto* req) {
        try {
            AnalyticsManager::AnalyticsFilters filters;
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.interval = get_query_param(req, "interval", "hour");
            filters.queue = get_query_param(req, "queue");
            filters.namespace_name = get_query_param(req, "namespace");
            filters.task = get_query_param(req, "task");
            
            auto response = ctx.analytics_manager->get_analytics(filters);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/analytics/system-metrics - System metrics time series
    app->get("/api/v1/analytics/system-metrics", [ctx](auto* res, auto* req) {
        try {
            AnalyticsManager::SystemMetricsFilters filters;
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.hostname = get_query_param(req, "hostname");
            filters.worker_id = get_query_param(req, "workerId");
            
            auto response = ctx.analytics_manager->get_system_metrics(filters);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/buffers - File buffer stats
    app->get("/api/v1/status/buffers", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            if (ctx.file_buffer) {
                nlohmann::json response = {
                    {"pending", ctx.file_buffer->get_pending_count()},
                    {"failed", ctx.file_buffer->get_failed_count()},
                    {"dbHealthy", ctx.file_buffer->is_db_healthy()},
                    {"worker", ctx.worker_id}
                };
                send_json_response(res, response);
            } else {
                // Non-zero worker - no file buffer
                nlohmann::json response = {
                    {"pending", 0},
                    {"failed", 0},
                    {"dbHealthy", true},
                    {"worker", ctx.worker_id},
                    {"note", "File buffer only available on Worker 0"}
                };
                send_json_response(res, response);
            }
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

