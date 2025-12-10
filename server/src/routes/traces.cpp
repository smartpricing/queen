#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/response_queue.hpp"
#include "queen.hpp"  // libqueen
#include <spdlog/spdlog.h>

// External globals (declared in acceptor_server.cpp)
namespace queen {
extern std::vector<std::shared_ptr<ResponseRegistry>> worker_response_registries;
}

namespace queen {
namespace routes {

// Helper: Submit a stored procedure call via libqueen and handle the response
static void submit_sp_call(
    const RouteContext& ctx,
    uWS::HttpResponse<false>* res,
    const std::string& sql,
    const std::vector<std::string>& params = {},
    int success_status = 200
) {
    std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
        res, ctx.worker_id, nullptr
    );
    
    queen::JobRequest job_req;
    job_req.op_type = queen::JobType::CUSTOM;
    job_req.request_id = request_id;
    job_req.sql = sql;
    job_req.params = params;
    
    auto worker_loop = ctx.worker_loop;
    auto worker_id = ctx.worker_id;
    
    ctx.queen->submit(std::move(job_req), [worker_loop, worker_id, request_id, success_status](std::string result) {
        worker_loop->defer([result = std::move(result), worker_id, request_id, success_status]() {
            nlohmann::json json_response;
            int status_code = success_status;
            bool is_error = false;
            
            try {
                json_response = nlohmann::json::parse(result);
                
                // Check for error in response
                if (json_response.contains("error") && !json_response["error"].is_null()) {
                    is_error = true;
                    status_code = 500;
                }
                // Check for success: false
                if (json_response.contains("success") && json_response["success"] == false) {
                    is_error = true;
                    status_code = 500;
                }
            } catch (const std::exception& e) {
                json_response = {{"error", e.what()}};
                status_code = 500;
                is_error = true;
            }
            
            worker_response_registries[worker_id]->send_response(
                request_id, json_response, is_error, status_code);
        });
    });
}

void setup_trace_routes(uWS::App* app, const RouteContext& ctx) {
    // POST /api/v1/traces - Record trace event (async via stored procedure)
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
                    
                    // Build JSONB input for stored procedure
                    nlohmann::json sp_input = {
                        {"transactionId", body["transactionId"]},
                        {"partitionId", body["partitionId"]},
                        {"consumerGroup", body.value("consumerGroup", "__QUEUE_MODE__")},
                        {"eventType", body.value("eventType", "info")},
                        {"data", body["data"]},
                        {"workerId", std::to_string(ctx.worker_id)}
                    };
                    
                    // Parse traceNames (can be null, string, or array)
                    if (body.contains("traceNames") && !body["traceNames"].is_null()) {
                        if (body["traceNames"].is_array()) {
                            sp_input["traceNames"] = body["traceNames"];
                        } else if (body["traceNames"].is_string()) {
                            sp_input["traceNames"] = nlohmann::json::array({body["traceNames"]});
                        }
                    }
                    
                    submit_sp_call(ctx, res, 
                        "SELECT queen.record_trace_v1($1::jsonb)",
                        {sp_input.dump()},
                        201);
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
    
    // GET /api/v1/traces/:partitionId/:transactionId - Get traces for a message (async via stored procedure)
    app->get("/api/v1/traces/:partitionId/:transactionId", [ctx](auto* res, auto* req) {
        try {
            std::string partition_id = std::string(req->getParameter(0));
            std::string transaction_id = std::string(req->getParameter(1));
            
            submit_sp_call(ctx, res, 
                "SELECT queen.get_message_traces_v1($1::uuid, $2)",
                {partition_id, transaction_id});
        } catch (const std::exception& e) {
            spdlog::error("Failed to get traces: {}", e.what());
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/traces/by-name/:traceName - Get traces by any name (async via stored procedure)
    app->get("/api/v1/traces/by-name/:traceName", [ctx](auto* res, auto* req) {
        try {
            std::string trace_name = std::string(req->getParameter(0));
            int limit = get_query_param_int(req, "limit", 100);
            int offset = get_query_param_int(req, "offset", 0);
            
            submit_sp_call(ctx, res, 
                "SELECT queen.get_traces_by_name_v1($1, $2::integer, $3::integer)",
                {trace_name, std::to_string(limit), std::to_string(offset)});
        } catch (const std::exception& e) {
            spdlog::error("Failed to get traces by name: {}", e.what());
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/traces/names - Get available trace names (async via stored procedure)
    app->get("/api/v1/traces/names", [ctx](auto* res, auto* req) {
        try {
            int limit = get_query_param_int(req, "limit", 50);
            int offset = get_query_param_int(req, "offset", 0);
            
            submit_sp_call(ctx, res, 
                "SELECT queen.get_available_trace_names_v1($1::integer, $2::integer)",
                {std::to_string(limit), std::to_string(offset)});
        } catch (const std::exception& e) {
            spdlog::error("Failed to get available trace names: {}", e.what());
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen
