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
                
                // Check for error in response
                if (json_response.contains("error") && !json_response["error"].is_null()) {
                    is_error = true;
                    status_code = json_response["error"].get<std::string>().find("not found") != std::string::npos 
                        ? 404 : 500;
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

// Helper: Build JSONB filters parameter for messages
static std::string build_message_filters_json(
    const std::string& queue = "",
    const std::string& partition = "",
    const std::string& ns = "",
    const std::string& task = "",
    const std::string& status = "",
    const std::string& from = "",
    const std::string& to = "",
    int limit = 200,
    int offset = 0
) {
    nlohmann::json filters;
    if (!queue.empty()) filters["queue"] = queue;
    if (!partition.empty()) filters["partition"] = partition;
    if (!ns.empty()) filters["namespace"] = ns;
    if (!task.empty()) filters["task"] = task;
    if (!status.empty()) filters["status"] = status;
    if (!from.empty()) filters["from"] = from;
    if (!to.empty()) filters["to"] = to;
    filters["limit"] = limit;
    filters["offset"] = offset;
    return filters.dump();
}

void setup_message_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /api/v1/messages - List messages with filters (async via stored procedure)
    app->get("/api/v1/messages", [ctx](auto* res, auto* req) {
        try {
            std::string filters_json = build_message_filters_json(
                get_query_param(req, "queue"),
                get_query_param(req, "partition"),
                get_query_param(req, "ns"),
                get_query_param(req, "task"),
                get_query_param(req, "status"),
                get_query_param(req, "from"),
                get_query_param(req, "to"),
                get_query_param_int(req, "limit", 200),
                get_query_param_int(req, "offset", 0)
            );
            submit_sp_call(ctx, res, 
                "SELECT queen.list_messages_v1($1::jsonb)",
                {filters_json});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/messages/:partitionId/:transactionId - Get single message detail (async via stored procedure)
    app->get("/api/v1/messages/:partitionId/:transactionId", [ctx](auto* res, auto* req) {
        try {
            std::string partition_id = std::string(req->getParameter(0));
            std::string transaction_id = std::string(req->getParameter(1));
            submit_sp_call(ctx, res, 
                "SELECT queen.get_message_v1($1::uuid, $2)",
                {partition_id, transaction_id});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // DELETE /api/v1/messages/:partitionId/:transactionId - Delete a message (async via stored procedure)
    app->del("/api/v1/messages/:partitionId/:transactionId", [ctx](auto* res, auto* req) {
        try {
            std::string partition_id = std::string(req->getParameter(0));
            std::string transaction_id = std::string(req->getParameter(1));
            
            spdlog::info("[Worker {}] DELETE message: partition={}, transaction={}", 
                ctx.worker_id, partition_id, transaction_id);
            
            submit_sp_call(ctx, res, 
                "SELECT queen.delete_message_v1($1::uuid, $2)",
                {partition_id, transaction_id});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen
