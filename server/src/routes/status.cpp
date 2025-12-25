#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/file_buffer.hpp"
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

// Helper: Build JSONB filters parameter
static std::string build_filters_json(
    const std::string& from = "",
    const std::string& to = "",
    const std::string& queue = "",
    const std::string& ns = "",
    const std::string& task = "",
    const std::string& interval = "",
    const std::string& hostname = "",
    const std::string& worker_id = ""
) {
    nlohmann::json filters;
    if (!from.empty()) filters["from"] = from;
    if (!to.empty()) filters["to"] = to;
    if (!queue.empty()) filters["queue"] = queue;
    if (!ns.empty()) filters["namespace"] = ns;
    if (!task.empty()) filters["task"] = task;
    if (!interval.empty()) filters["interval"] = interval;
    if (!hostname.empty()) filters["hostname"] = hostname;
    if (!worker_id.empty()) filters["workerId"] = worker_id;
    return filters.dump();
}

void setup_status_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /api/v1/status - Dashboard overview (async via stored procedure)
    // Uses V3 procedure with worker_metrics for real-time throughput and lag
    app->get("/api/v1/status", [ctx](auto* res, auto* req) {
        try {
            std::string filters_json = build_filters_json(
                get_query_param(req, "from"),
                get_query_param(req, "to"),
                get_query_param(req, "queue"),
                get_query_param(req, "namespace"),
                get_query_param(req, "task")
            );
            submit_sp_call(ctx, res, 
                "SELECT queen.get_status_v3($1::jsonb)",
                {filters_json});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/queues - Queues list (async via stored procedure)
    // Uses V2 optimized procedure with pre-computed stats
    app->get("/api/v1/status/queues", [ctx](auto* res, auto* req) {
        try {
            std::string filters_json = build_filters_json(
                get_query_param(req, "from"),
                get_query_param(req, "to"),
                "",  // queue not used for list
                get_query_param(req, "namespace"),
                get_query_param(req, "task")
            );
            int limit = get_query_param_int(req, "limit", 100);
            int offset = get_query_param_int(req, "offset", 0);
            
            submit_sp_call(ctx, res, 
                "SELECT queen.get_status_queues_v2($1::jsonb, $2::integer, $3::integer)",
                {filters_json, std::to_string(limit), std::to_string(offset)});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/queues/:queue - Queue detail (async via stored procedure)
    // Uses V2 optimized procedure with pre-computed stats
    app->get("/api/v1/status/queues/:queue", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            submit_sp_call(ctx, res, 
                "SELECT queen.get_queue_detail_v2($1)",
                {queue_name});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/queues/:queue/messages - Queue messages (async via stored procedure)
    app->get("/api/v1/status/queues/:queue/messages", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            int limit = get_query_param_int(req, "limit", 50);
            int offset = get_query_param_int(req, "offset", 0);
            
            submit_sp_call(ctx, res, 
                "SELECT queen.get_queue_messages_v1($1, $2::integer, $3::integer)",
                {queue_name, std::to_string(limit), std::to_string(offset)});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/analytics - Analytics (async via stored procedure)
    app->get("/api/v1/status/analytics", [ctx](auto* res, auto* req) {
        try {
            std::string filters_json = build_filters_json(
                get_query_param(req, "from"),
                get_query_param(req, "to"),
                get_query_param(req, "queue"),
                get_query_param(req, "namespace"),
                get_query_param(req, "task"),
                get_query_param(req, "interval", "hour")
            );
            submit_sp_call(ctx, res, 
                "SELECT queen.get_analytics_v1($1::jsonb)",
                {filters_json});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/analytics/system-metrics - System metrics (async via stored procedure)
    app->get("/api/v1/analytics/system-metrics", [ctx](auto* res, auto* req) {
        try {
            std::string filters_json = build_filters_json(
                get_query_param(req, "from"),
                get_query_param(req, "to"),
                "", "", "", "",
                get_query_param(req, "hostname"),
                get_query_param(req, "workerId")
            );
            submit_sp_call(ctx, res, 
                "SELECT queen.get_system_metrics_v1($1::jsonb)",
                {filters_json});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/analytics/worker-metrics - Worker metrics time series (from libqueen)
    app->get("/api/v1/analytics/worker-metrics", [ctx](auto* res, auto* req) {
        try {
            std::string filters_json = build_filters_json(
                get_query_param(req, "from"),
                get_query_param(req, "to"),
                "", "", "", "",
                get_query_param(req, "hostname"),
                get_query_param(req, "workerId")
            );
            submit_sp_call(ctx, res, 
                "SELECT queen.get_worker_metrics_timeseries_v1($1::jsonb)",
                {filters_json});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/analytics/postgres-stats - PostgreSQL internal stats (async via stored procedure)
    app->get("/api/v1/analytics/postgres-stats", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            submit_sp_call(ctx, res, 
                "SELECT queen.get_postgres_stats_v1()");
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/status/buffers - File buffer stats (local, not async)
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
