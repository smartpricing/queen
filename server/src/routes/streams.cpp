#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/stream_manager.hpp"

// External globals (declared in acceptor_server.cpp)
namespace queen {
extern std::shared_ptr<StreamManager> global_stream_manager;
}

namespace queen {
namespace routes {

void setup_stream_routes(uWS::App* app, const RouteContext& ctx) {
    // POST /api/v1/stream/define - Define a stream
    app->post("/api/v1/stream/define", [ctx](auto* res, auto* req) {
        global_stream_manager->handle_define(res, req, ctx.worker_id);
    });
    
    // POST /api/v1/stream/poll - Poll for a window
    app->post("/api/v1/stream/poll", [ctx](auto* res, auto* req) {
        global_stream_manager->handle_poll(res, req, ctx.worker_id);
    });
    
    // POST /api/v1/stream/ack - Acknowledge a window
    app->post("/api/v1/stream/ack", [ctx](auto* res, auto* req) {
        global_stream_manager->handle_ack(res, req, ctx.worker_id);
    });
    
    // POST /api/v1/stream/renew-lease - Renew a lease
    app->post("/api/v1/stream/renew-lease", [ctx](auto* res, auto* req) {
        global_stream_manager->handle_renew(res, req, ctx.worker_id);
    });
    
    // POST /api/v1/stream/seek - Seek to a timestamp
    app->post("/api/v1/stream/seek", [ctx](auto* res, auto* req) {
        global_stream_manager->handle_seek(res, req, ctx.worker_id);
    });
    
    // GET /api/v1/resources/streams - List all streams
    app->get("/api/v1/resources/streams", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            auto response = ctx.async_queue_manager->list_streams();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/streams/stats - Get stream statistics
    app->get("/api/v1/resources/streams/stats", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            auto response = ctx.async_queue_manager->get_stream_stats();
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // GET /api/v1/resources/streams/:streamName - Get stream details
    app->get("/api/v1/resources/streams/:streamName", [ctx](auto* res, auto* req) {
        try {
            std::string stream_name = std::string(req->getParameter(0));
            auto stream = ctx.async_queue_manager->get_stream_details(stream_name);
            send_json_response(res, stream);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not found") != std::string::npos) {
                send_error_response(res, e.what(), 404);
            } else {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // GET /api/v1/resources/streams/:streamName/consumers - Get consumer groups for a stream
    app->get("/api/v1/resources/streams/:streamName/consumers", [ctx](auto* res, auto* req) {
        try {
            std::string stream_name = std::string(req->getParameter(0));
            auto response = ctx.async_queue_manager->get_stream_consumers(stream_name);
            send_json_response(res, response);
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("not found") != std::string::npos) {
                send_error_response(res, e.what(), 404);
            } else {
                send_error_response(res, e.what(), 500);
            }
        }
    });
    
    // DELETE /api/v1/resources/streams/:streamName - Delete a stream
    app->del("/api/v1/resources/streams/:streamName", [ctx](auto* res, auto* req) {
        try {
            std::string stream_name = std::string(req->getParameter(0));
            bool success = ctx.async_queue_manager->delete_stream(stream_name);
            
            if (success) {
                nlohmann::json response = {
                    {"success", true},
                    {"message", "Stream deleted successfully"}
                };
                send_json_response(res, response);
            } else {
                send_error_response(res, "Stream not found", 404);
            }
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen

