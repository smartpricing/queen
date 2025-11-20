#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/analytics_manager.hpp"

namespace queen {
namespace routes {

void setup_message_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /api/v1/messages - List messages with filters
    app->get("/api/v1/messages", [ctx](auto* res, auto* req) {
        try {
            AnalyticsManager::MessageFilters filters;
            filters.queue = get_query_param(req, "queue");
            filters.partition = get_query_param(req, "partition");
            filters.namespace_name = get_query_param(req, "ns");
            filters.task = get_query_param(req, "task");
            filters.status = get_query_param(req, "status");
            filters.from = get_query_param(req, "from");
            filters.to = get_query_param(req, "to");
            filters.limit = get_query_param_int(req, "limit", 200);
            filters.offset = get_query_param_int(req, "offset", 0);
            
            auto messages = ctx.analytics_manager->list_messages(filters);
            send_json_response(res, messages);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/messages/:partitionId/:transactionId - Get single message detail
    app->get("/api/v1/messages/:partitionId/:transactionId", [ctx](auto* res, auto* req) {
        try {
            std::string partition_id = std::string(req->getParameter(0));
            std::string transaction_id = std::string(req->getParameter(1));
            auto message = ctx.analytics_manager->get_message(partition_id, transaction_id);
            send_json_response(res, message);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not found") != std::string::npos) {
                send_error_response(res, e.what(), 404);
            } else {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // DELETE /api/v1/messages/:partitionId/:transactionId - Delete a message
    app->del("/api/v1/messages/:partitionId/:transactionId", [ctx](auto* res, auto* req) {
        try {
            std::string partition_id = std::string(req->getParameter(0));
            std::string transaction_id = std::string(req->getParameter(1));
            
            bool deleted = ctx.analytics_manager->delete_message(partition_id, transaction_id);
            
            if (deleted) {
                nlohmann::json response = {
                    {"success", true},
                    {"message", "Message deleted successfully"},
                    {"partitionId", partition_id},
                    {"transactionId", transaction_id}
                };
                send_json_response(res, response);
            } else {
                send_error_response(res, "Message not found", 404);
            }
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

