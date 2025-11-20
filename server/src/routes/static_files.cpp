#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>

namespace queen {
namespace routes {

void setup_static_file_routes(uWS::App* app, const RouteContext& ctx) {
    // Define webapp path - check multiple locations
    std::string webapp_root;
    
    // 1. Check environment variable first
    const char* webapp_env = std::getenv("WEBAPP_ROOT");
    if (webapp_env) {
        webapp_root = webapp_env;
    }
    // 2. Check Docker/production location
    else if (std::filesystem::exists("./webapp/dist")) {
        webapp_root = "./webapp/dist";
    }
    // 3. Check development location (running from server/bin/)
    else if (std::filesystem::exists("../webapp/dist")) {
        webapp_root = "../webapp/dist";
    }
    // 4. Check alternative development location
    else if (std::filesystem::exists("../../webapp/dist")) {
        webapp_root = "../../webapp/dist";
    }
    
    // Check if webapp directory exists
    if (!webapp_root.empty() && std::filesystem::exists(webapp_root)) {
        spdlog::info("[Worker {}] Static file serving enabled from: {}", ctx.worker_id, 
                    std::filesystem::absolute(webapp_root).string());
        
        // Route 1: GET /assets/* - Serve static assets (JS, CSS, images, fonts)
        app->get("/assets/*", [webapp_root](auto* res, auto* req) {
            auto aborted = std::make_shared<bool>(false);
            res->onAborted([aborted]() { *aborted = true; });
            
            if (*aborted) return;
            
            std::string url = std::string(req->getUrl());
            std::string file_path = webapp_root + url;
            
            bool served = serve_static_file(res, file_path, webapp_root);
            if (!served && !*aborted) {
                res->writeStatus("404");
                res->end("Not Found");
            }
        });
        
        // Route 2: GET / - Serve root index.html
        app->get("/", [webapp_root](auto* res, auto* req) {
            (void)req;
            auto aborted = std::make_shared<bool>(false);
            res->onAborted([aborted]() { *aborted = true; });
            
            if (*aborted) return;
            
            std::string index_path = webapp_root + "/index.html";
            serve_static_file(res, index_path, webapp_root);
        });
        
        // Route 3: GET /* - SPA fallback (for Vue Router client-side routing)
        // MUST be registered LAST to not override API routes
        app->get("/*", [webapp_root](auto* res, auto* req) {
            auto aborted = std::make_shared<bool>(false);
            res->onAborted([aborted]() { *aborted = true; });
            
            if (*aborted) return;
            
            std::string url = std::string(req->getUrl());
            
            // Don't serve index.html for API routes or special endpoints
            if (url.find("/api/") == 0 || 
                url.find("/health") == 0 || 
                url.find("/metrics") == 0 ||
                url.find("/ws/") == 0) {
                res->writeStatus("404");
                res->end("Not Found");
                return;
            }
            
            // Try to serve as a direct file first (e.g., favicon.ico)
            std::string file_path = webapp_root + url;
            bool served = serve_static_file(res, file_path, webapp_root);
            
            // If file doesn't exist, serve index.html for SPA routing
            if (!served && !*aborted) {
                std::string index_path = webapp_root + "/index.html";
                serve_static_file(res, index_path, webapp_root);
            }
        });
        
    } else {
        spdlog::warn("[Worker {}] Webapp directory not found - Static file serving disabled", ctx.worker_id);
        spdlog::warn("[Worker {}] Searched: ./webapp/dist, ../webapp/dist, ../../webapp/dist", ctx.worker_id);
        spdlog::warn("[Worker {}] Set WEBAPP_ROOT environment variable to override", ctx.worker_id);
    }
}

} // namespace routes
} // namespace queen

