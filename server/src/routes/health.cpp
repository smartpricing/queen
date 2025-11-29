#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/inter_instance_comms.hpp"
#include "queen/sidecar_db_pool.hpp"
#include <spdlog/spdlog.h>

// External globals
namespace queen {
extern SidecarDbPool* global_sidecar_pool_ptr;
}

namespace queen {
namespace routes {

void setup_cors_routes(uWS::App* app) {
    app->options("/*", [](auto* res, auto* req) {
        (void)req;
        setup_cors_headers(res);
        res->writeStatus("204");
        res->end();
    });
}

void setup_health_routes(uWS::App* app, const RouteContext& ctx) {
    app->get("/health", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            bool healthy = ctx.async_queue_manager->health_check();
            nlohmann::json response = {
                {"status", healthy ? "healthy" : "unhealthy"},
                {"database", healthy ? "connected" : "disconnected"},
                {"server", "C++ Queen Server (Acceptor/Worker)"},
                {"worker_id", ctx.worker_id},
                {"version", "1.0.0"}
            };
            
            // Add peer notification status
            if (global_inter_instance_comms) {
                response["peer_notify"] = global_inter_instance_comms->get_stats();
            } else {
                response["peer_notify"] = {{"enabled", false}};
            }
            
            // Add sidecar stats if enabled
            if (global_sidecar_pool_ptr) {
                auto sidecar_stats = global_sidecar_pool_ptr->get_stats();
                
                // Build per-operation stats
                nlohmann::json op_stats_json = nlohmann::json::object();
                for (const auto& [op_type, op_stats] : sidecar_stats.op_stats) {
                    std::string op_name;
                    switch (op_type) {
                        case SidecarOpType::PUSH: op_name = "push"; break;
                        case SidecarOpType::POP: op_name = "pop"; break;
                        case SidecarOpType::ACK: op_name = "ack"; break;
                        case SidecarOpType::ACK_BATCH: op_name = "ack_batch"; break;
                        case SidecarOpType::TRANSACTION: op_name = "transaction"; break;
                        case SidecarOpType::RENEW_LEASE: op_name = "renew_lease"; break;
                    }
                    op_stats_json[op_name] = {
                        {"count", op_stats.count},
                        {"avg_ms", op_stats.count > 0 ? static_cast<double>(op_stats.total_time_us) / op_stats.count / 1000.0 : 0},
                        {"items", op_stats.items_processed}
                    };
                }
                
                response["sidecar"] = {
                    {"connections", {
                        {"total", sidecar_stats.total_connections},
                        {"busy", sidecar_stats.busy_connections}
                    }},
                    {"queue", {
                        {"pending", sidecar_stats.pending_requests},
                        {"completed", sidecar_stats.completed_responses}
                    }},
                    {"total_queries", sidecar_stats.total_queries},
                    {"operations", op_stats_json}
                };
            }
            
            send_json_response(res, response, healthy ? 200 : 503);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

