#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include <spdlog/spdlog.h>

namespace queen {
namespace routes {

void setup_trace_routes(uWS::App* app, const RouteContext& ctx) {
    // POST /api/v1/traces - Record trace event
    app->post("/api/v1/traces", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    // Validate required fields
                    if (!body.contains("transactionId") || !body.contains("partitionId")) {
                        send_error_response(res, "transactionId and partitionId are required", 400);
                        return;
                    }
                    
                    if (!body.contains("data")) {
                        send_error_response(res, "data is required", 400);
                        return;
                    }
                    
                    // Parse traceNames (can be null, string, or array)
                    std::vector<std::string> trace_names;
                    if (body.contains("traceNames") && !body["traceNames"].is_null()) {
                        if (body["traceNames"].is_array()) {
                            for (const auto& name : body["traceNames"]) {
                                if (name.is_string() && !name.get<std::string>().empty()) {
                                    trace_names.push_back(name.get<std::string>());
                                }
                            }
                        } else if (body["traceNames"].is_string()) {
                            trace_names.push_back(body["traceNames"].get<std::string>());
                        }
                    }
                    
                    // Call async_queue_manager method to store trace
                    bool success = ctx.async_queue_manager->record_trace(
                        body["transactionId"],
                        body["partitionId"],
                        body.value("consumerGroup", "__QUEUE_MODE__"),
                        trace_names,
                        body.value("eventType", "info"),
                        body["data"],
                        std::to_string(ctx.worker_id)
                    );
                    
                    if (success) {
                        nlohmann::json response = {{"success", true}};
                        send_json_response(res, response, 201);
                    } else {
                        send_error_response(res, "Failed to record trace", 500);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Failed to handle trace request: {}", e.what());
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // GET /api/v1/traces/:partitionId/:transactionId - Get traces for a message
    app->get("/api/v1/traces/:partitionId/:transactionId", [ctx](auto* res, auto* req) {
        try {
            std::string partition_id = std::string(req->getParameter(0));
            std::string transaction_id = std::string(req->getParameter(1));
            
            auto traces = ctx.async_queue_manager->get_message_traces(partition_id, transaction_id);
            send_json_response(res, traces);
        } catch (const std::exception& e) {
            spdlog::error("Failed to get traces: {}", e.what());
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/traces/by-name/:traceName - Get traces by any name
    app->get("/api/v1/traces/by-name/:traceName", [ctx](auto* res, auto* req) {
        try {
            std::string trace_name = std::string(req->getParameter(0));
            
            int limit = get_query_param_int(req, "limit", 100);
            int offset = get_query_param_int(req, "offset", 0);
            
            auto traces = ctx.async_queue_manager->get_traces_by_name(trace_name, limit, offset);
            send_json_response(res, traces);
        } catch (const std::exception& e) {
            spdlog::error("Failed to get traces by name: {}", e.what());
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/traces/names - Get available trace names
    app->get("/api/v1/traces/names", [ctx](auto* res, auto* req) {
        try {
            int limit = get_query_param_int(req, "limit", 50);
            int offset = get_query_param_int(req, "offset", 0);
            
            auto trace_names = ctx.async_queue_manager->get_available_trace_names(limit, offset);
            send_json_response(res, trace_names);
        } catch (const std::exception& e) {
            spdlog::error("Failed to get available trace names: {}", e.what());
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

