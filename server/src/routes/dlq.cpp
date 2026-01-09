#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
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
    const std::vector<std::string>& params = {}
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
    
    ctx.queen->submit(std::move(job_req), [worker_loop, worker_id, request_id](std::string result) {
        worker_loop->defer([result = std::move(result), worker_id, request_id]() {
            nlohmann::json json_response;
            int status_code = 200;
            bool is_error = false;
            
            try {
                json_response = nlohmann::json::parse(result);
                
                if (json_response.contains("error") && !json_response["error"].is_null()) {
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

void setup_dlq_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /api/v1/dlq - Get dead letter queue messages (async via stored procedure)
    app->get("/api/v1/dlq", [ctx](auto* res, auto* req) {
        // Check authentication - READ_ONLY required for DLQ viewing
        REQUIRE_AUTH(res, req, ctx, auth::AccessLevel::READ_ONLY);
        
        try {
            nlohmann::json filters;
            std::string queue = get_query_param(req, "queue");
            std::string consumer_group = get_query_param(req, "consumerGroup");
            if (!queue.empty()) filters["queue"] = queue;
            if (!consumer_group.empty()) filters["consumerGroup"] = consumer_group;
            filters["limit"] = get_query_param_int(req, "limit", 100);
            filters["offset"] = get_query_param_int(req, "offset", 0);
            
            submit_sp_call(ctx, res, 
                "SELECT queen.get_dlq_messages_v1($1::jsonb)",
                {filters.dump()});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen
