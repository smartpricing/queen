#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/analytics_manager.hpp"
#include "queen/async_queue_manager.hpp"

namespace queen {
namespace routes {

void setup_consumer_group_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /api/v1/consumer-groups - Consumer groups summary
    app->get("/api/v1/consumer-groups", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            auto response = ctx.analytics_manager->get_consumer_groups();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/consumer-groups/lagging - Get lagging partitions
    app->get("/api/v1/consumer-groups/lagging", [ctx](auto* res, auto* req) {
        try {
            int min_lag_seconds = 3600; // Default: 1 hour
            std::string lag_param = get_query_param(req, "minLagSeconds");
            if (!lag_param.empty()) {
                min_lag_seconds = std::stoi(lag_param);
            }
            auto response = ctx.analytics_manager->get_lagging_partitions(min_lag_seconds);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/consumer-groups/:group - Consumer group details (partitions)
    app->get("/api/v1/consumer-groups/:group", [ctx](auto* res, auto* req) {
        try {
            std::string consumer_group = std::string(req->getParameter(0));
            auto response = ctx.analytics_manager->get_consumer_group_details(consumer_group);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // DELETE /api/v1/consumer-groups/:group - Delete consumer group and metadata
    app->del("/api/v1/consumer-groups/:group", [ctx](auto* res, auto* req) {
        try {
            std::string consumer_group = std::string(req->getParameter(0));
            bool delete_metadata = get_query_param_bool(req, "deleteMetadata", true);
            
            ctx.async_queue_manager->delete_consumer_group(consumer_group, delete_metadata);
            
            nlohmann::json response = {
                {"success", true},
                {"consumerGroup", consumer_group},
                {"metadataDeleted", delete_metadata}
            };
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // POST /api/v1/consumer-groups/:group/subscription - Update subscription timestamp
    app->post("/api/v1/consumer-groups/:group/subscription", [ctx](auto* res, auto* req) {
        std::string consumer_group = std::string(req->getParameter(0));
        
        read_json_body(res,
            [res, ctx, consumer_group](const nlohmann::json& body) {
                try {
                    if (!body.contains("subscriptionTimestamp")) {
                        send_error_response(res, "Missing subscriptionTimestamp in request body", 400);
                        return;
                    }
                    
                    std::string new_timestamp = body["subscriptionTimestamp"].get<std::string>();
                    
                    ctx.async_queue_manager->update_consumer_group_subscription(consumer_group, new_timestamp);
                    
                    nlohmann::json response = {
                        {"success", true},
                        {"consumerGroup", consumer_group},
                        {"subscriptionTimestamp", new_timestamp}
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
}

} // namespace routes
} // namespace queen

