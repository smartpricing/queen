#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/analytics_manager.hpp"
#include "queen/async_queue_manager.hpp"
#include <spdlog/spdlog.h>

namespace queen {
namespace routes {

void setup_resource_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /api/v1/resources/queues - List all queues
    app->get("/api/v1/resources/queues", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            auto response = ctx.analytics_manager->get_queues();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/queues/:queue - Get single queue detail
    app->get("/api/v1/resources/queues/:queue", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            auto response = ctx.analytics_manager->get_queue(queue_name);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not found") != std::string::npos) {
                send_error_response(res, e.what(), 404);
            } else {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // DELETE /api/v1/resources/queues/:queue - Delete queue
    app->del("/api/v1/resources/queues/:queue", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            
            spdlog::info("[Worker {}] DELETE queue: {}", ctx.worker_id, queue_name);
            bool deleted = ctx.async_queue_manager->delete_queue(queue_name);
            
            nlohmann::json response = {
                {"deleted", true},
                {"queue", queue_name},
                {"existed", deleted}
            };
            
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/namespaces - List all namespaces
    app->get("/api/v1/resources/namespaces", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            auto response = ctx.analytics_manager->get_namespaces();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/tasks - List all tasks
    app->get("/api/v1/resources/tasks", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            auto response = ctx.analytics_manager->get_tasks();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/overview - System overview
    app->get("/api/v1/resources/overview", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            auto response = ctx.analytics_manager->get_system_overview();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

