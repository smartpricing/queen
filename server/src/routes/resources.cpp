#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/response_queue.hpp"
#include "queen/shared_state_manager.hpp"
#include "queen.hpp"  // libqueen
#include <spdlog/spdlog.h>

// External globals (declared in acceptor_server.cpp)
namespace queen {
extern std::vector<std::shared_ptr<ResponseRegistry>> worker_response_registries;
extern std::shared_ptr<SharedStateManager> global_shared_state;
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

void setup_resource_routes(uWS::App* app, const RouteContext& ctx) {
    // GET /api/v1/resources/queues - List all queues (async via stored procedure)
    // Uses V2 optimized procedure with pre-computed stats
    app->get("/api/v1/resources/queues", [ctx](auto* res, auto* req) {
        // Check authentication - READ_ONLY required for resources
        REQUIRE_AUTH(res, req, ctx, auth::AccessLevel::READ_ONLY);
        
        try {
            submit_sp_call(ctx, res, "SELECT queen.get_queues_v2()");
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/queues/:queue - Get single queue detail (async via stored procedure)
    // Uses V2 optimized procedure with pre-computed stats
    app->get("/api/v1/resources/queues/:queue", [ctx](auto* res, auto* req) {
        // Check authentication - READ_ONLY required for resources
        REQUIRE_AUTH(res, req, ctx, auth::AccessLevel::READ_ONLY);
        
        try {
            std::string queue_name = std::string(req->getParameter(0));
            submit_sp_call(ctx, res, "SELECT queen.get_queue_v2($1)", {queue_name});
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // DELETE /api/v1/resources/queues/:queue - Delete queue (async via stored procedure)
    app->del("/api/v1/resources/queues/:queue", [ctx](auto* res, auto* req) {
        // Check authentication - ADMIN required for queue deletion
        REQUIRE_AUTH(res, req, ctx, auth::AccessLevel::ADMIN);
        
        try {
            std::string queue_name = std::string(req->getParameter(0));
            
            spdlog::info("[Worker {}] DELETE queue: {}", ctx.worker_id, queue_name);
            
            std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
                res, ctx.worker_id, nullptr
            );
            
            queen::JobRequest job_req;
            job_req.op_type = queen::JobType::CUSTOM;
            job_req.request_id = request_id;
            job_req.sql = "SELECT queen.delete_queue_v1($1)";
            job_req.params = {queue_name};
            
            auto worker_loop = ctx.worker_loop;
            auto worker_id = ctx.worker_id;
            std::string captured_queue_name = queue_name;
            
            ctx.queen->submit(std::move(job_req), [worker_loop, worker_id, request_id, captured_queue_name](std::string result) {
                worker_loop->defer([result = std::move(result), worker_id, request_id, captured_queue_name]() {
                    nlohmann::json json_response;
                    int status_code = 200;
                    bool is_error = false;
                    
                    try {
                        json_response = nlohmann::json::parse(result);
                        
                        // Broadcast queue deletion to peers
                        if (global_shared_state && global_shared_state->is_enabled()) {
                            global_shared_state->delete_queue_config(captured_queue_name);
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
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/namespaces - List all namespaces (async via stored procedure)
    // Uses V2 optimized procedure with pre-computed stats
    app->get("/api/v1/resources/namespaces", [ctx](auto* res, auto* req) {
        // Check authentication - READ_ONLY required for resources
        REQUIRE_AUTH(res, req, ctx, auth::AccessLevel::READ_ONLY);
        
        try {
            submit_sp_call(ctx, res, "SELECT queen.get_namespaces_v2()");
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/tasks - List all tasks (async via stored procedure)
    // Uses V2 optimized procedure with pre-computed stats
    app->get("/api/v1/resources/tasks", [ctx](auto* res, auto* req) {
        // Check authentication - READ_ONLY required for resources
        REQUIRE_AUTH(res, req, ctx, auth::AccessLevel::READ_ONLY);
        
        try {
            submit_sp_call(ctx, res, "SELECT queen.get_tasks_v2()");
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/overview - System overview (async via stored procedure)
    // Uses V3 procedure with worker_metrics for real-time throughput and lag
    app->get("/api/v1/resources/overview", [ctx](auto* res, auto* req) {
        // Check authentication - READ_ONLY required for resources
        REQUIRE_AUTH(res, req, ctx, auth::AccessLevel::READ_ONLY);
        
        try {
            submit_sp_call(ctx, res, "SELECT queen.get_system_overview_v3()");
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen
