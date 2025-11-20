# Route Refactoring Implementation Plan

## Overview

This document provides a complete implementation plan for extracting route handlers from `acceptor_server.cpp` into separate, focused files. The current file is 2500 lines with significant duplication and complexity.

**Goal:** Split route handlers into dedicated files while maintaining all functionality, improving maintainability, and reducing code duplication.

---

## File Structure

Create the following new directory and files:

```
server/
├── include/
│   └── queen/
│       ├── routes/
│       │   ├── route_context.hpp          [NEW]
│       │   ├── route_helpers.hpp          [NEW]
│       │   └── route_registry.hpp         [NEW]
├── src/
│   ├── routes/
│   │   ├── route_helpers.cpp              [NEW]
│   │   ├── health.cpp                     [NEW]
│   │   ├── maintenance.cpp                [NEW]
│   │   ├── configure.cpp                  [NEW]
│   │   ├── push.cpp                       [NEW]
│   │   ├── pop.cpp                        [NEW]
│   │   ├── ack.cpp                        [NEW]
│   │   ├── transactions.cpp               [NEW]
│   │   ├── leases.cpp                     [NEW]
│   │   ├── metrics.cpp                    [NEW]
│   │   ├── resources.cpp                  [NEW]
│   │   ├── messages.cpp                   [NEW]
│   │   ├── dlq.cpp                        [NEW]
│   │   ├── traces.cpp                     [NEW]
│   │   ├── streams.cpp                    [NEW]
│   │   ├── status.cpp                     [NEW]
│   │   ├── consumer_groups.cpp            [NEW]
│   │   └── static_files.cpp               [NEW]
│   └── acceptor_server.cpp                [MODIFIED]
```

---

## 1. Create Route Context Header

**File:** `server/include/queen/routes/route_context.hpp`

```cpp
#pragma once

#include <memory>
#include <string>

namespace queen {

// Forward declarations
class AsyncQueueManager;
class AnalyticsManager;
class FileBufferManager;
struct Config;

namespace routes {

/**
 * Context object containing all dependencies needed by route handlers.
 * This is passed to each route setup function and captured by lambdas.
 */
struct RouteContext {
    // Core queue operations manager
    std::shared_ptr<AsyncQueueManager> async_queue_manager;
    
    // Analytics and dashboard queries
    std::shared_ptr<AnalyticsManager> analytics_manager;
    
    // File buffer for maintenance mode and failover
    std::shared_ptr<FileBufferManager> file_buffer;
    
    // Configuration reference
    const Config& config;
    
    // Worker identifier for logging and routing
    int worker_id;
    
    // Database thread pool (currently unused but kept for compatibility)
    std::shared_ptr<astp::ThreadPool> db_thread_pool;
    
    RouteContext(
        std::shared_ptr<AsyncQueueManager> qm,
        std::shared_ptr<AnalyticsManager> am,
        std::shared_ptr<FileBufferManager> fb,
        const Config& cfg,
        int wid,
        std::shared_ptr<astp::ThreadPool> dbtp
    ) : async_queue_manager(std::move(qm)),
        analytics_manager(std::move(am)),
        file_buffer(std::move(fb)),
        config(cfg),
        worker_id(wid),
        db_thread_pool(std::move(dbtp))
    {}
};

} // namespace routes
} // namespace queen
```

---

## 2. Create Route Helpers Header

**File:** `server/include/queen/routes/route_helpers.hpp`

```cpp
#pragma once

#include <App.h>
#include <json.hpp>
#include <string>
#include <functional>
#include <map>

namespace queen {
namespace routes {

// ============================================================================
// CORS and Response Helpers
// ============================================================================

/**
 * Setup CORS headers for cross-origin requests
 */
void setup_cors_headers(uWS::HttpResponse<false>* res);

/**
 * Send JSON response with proper headers and status code
 */
void send_json_response(uWS::HttpResponse<false>* res, const nlohmann::json& json, int status_code = 200);

/**
 * Send error response as JSON
 */
void send_error_response(uWS::HttpResponse<false>* res, const std::string& error, int status_code = 500);

// ============================================================================
// Request Body Helpers
// ============================================================================

/**
 * Read and parse JSON body from HTTP request
 * Handles chunked data and parsing errors
 */
void read_json_body(
    uWS::HttpResponse<false>* res,
    std::function<void(const nlohmann::json&)> callback,
    std::function<void(const std::string&)> error_callback
);

// ============================================================================
// Query Parameter Helpers
// ============================================================================

/**
 * URL decode a string (handles %XX encoding and + for spaces)
 */
std::string url_decode(const std::string& str);

/**
 * Get query parameter as string with default value
 */
std::string get_query_param(uWS::HttpRequest* req, const std::string& key, const std::string& default_value = "");

/**
 * Get query parameter as integer with default value
 */
int get_query_param_int(uWS::HttpRequest* req, const std::string& key, int default_value);

/**
 * Get query parameter as boolean with default value
 */
bool get_query_param_bool(uWS::HttpRequest* req, const std::string& key, bool default_value);

// ============================================================================
// Static File Serving Helpers
// ============================================================================

/**
 * Get MIME type based on file extension
 */
std::string get_mime_type(const std::string& file_path);

/**
 * Serve static file from disk with security checks and caching headers
 * Returns true if file was served (successfully or with error), false if file not found
 */
bool serve_static_file(
    uWS::HttpResponse<false>* res,
    const std::string& file_path,
    const std::string& webapp_root
);

// ============================================================================
// Common Business Logic Helpers
// ============================================================================

/**
 * Parse and determine subscription mode and timestamp for consumer groups
 * Returns {mode_value, timestamp_sql}
 */
std::pair<std::string, std::string> parse_subscription_mode(
    const std::string& sub_mode,
    const std::string& sub_from,
    const std::string& default_sub_mode
);

/**
 * Format timestamp for JSON response (ISO 8601 with milliseconds)
 */
std::string format_timestamp_iso8601(const std::chrono::system_clock::time_point& tp);

} // namespace routes
} // namespace queen
```

---

## 3. Create Route Registry Header

**File:** `server/include/queen/routes/route_registry.hpp`

```cpp
#pragma once

#include <App.h>

namespace queen {
namespace routes {

struct RouteContext;

// ============================================================================
// Route Setup Functions
// ============================================================================
// Each function sets up a category of routes on the provided uWS::App

/**
 * Setup CORS preflight OPTIONS handler
 */
void setup_cors_routes(uWS::App* app);

/**
 * Setup health check endpoint
 * Routes: GET /health
 */
void setup_health_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup maintenance mode endpoints
 * Routes: GET/POST /api/v1/system/maintenance
 */
void setup_maintenance_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup queue configuration endpoints
 * Routes: POST /api/v1/configure
 */
void setup_configure_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup message push endpoints
 * Routes: POST /api/v1/push
 */
void setup_push_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup message pop endpoints
 * Routes: 
 *   GET /api/v1/pop
 *   GET /api/v1/pop/queue/:queue
 *   GET /api/v1/pop/queue/:queue/partition/:partition
 */
void setup_pop_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup message acknowledgment endpoints
 * Routes:
 *   POST /api/v1/ack
 *   POST /api/v1/ack/batch
 */
void setup_ack_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup transaction endpoints
 * Routes: POST /api/v1/transaction
 */
void setup_transaction_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup lease management endpoints
 * Routes: POST /api/v1/lease/:leaseId/extend
 */
void setup_lease_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup metrics endpoints
 * Routes: GET /metrics
 */
void setup_metrics_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup resource listing endpoints
 * Routes:
 *   GET /api/v1/resources/queues
 *   GET /api/v1/resources/queues/:queue
 *   DELETE /api/v1/resources/queues/:queue
 *   GET /api/v1/resources/namespaces
 *   GET /api/v1/resources/tasks
 *   GET /api/v1/resources/overview
 */
void setup_resource_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup message listing and detail endpoints
 * Routes:
 *   GET /api/v1/messages
 *   GET /api/v1/messages/:partitionId/:transactionId
 *   DELETE /api/v1/messages/:partitionId/:transactionId
 */
void setup_message_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup dead letter queue endpoints
 * Routes: GET /api/v1/dlq
 */
void setup_dlq_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup message tracing endpoints
 * Routes:
 *   POST /api/v1/traces
 *   GET /api/v1/traces/:partitionId/:transactionId
 *   GET /api/v1/traces/by-name/:traceName
 *   GET /api/v1/traces/names
 */
void setup_trace_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup streaming endpoints
 * Routes:
 *   POST /api/v1/stream/define
 *   POST /api/v1/stream/poll
 *   POST /api/v1/stream/ack
 *   POST /api/v1/stream/renew-lease
 *   POST /api/v1/stream/seek
 *   GET /api/v1/resources/streams
 *   GET /api/v1/resources/streams/stats
 *   GET /api/v1/resources/streams/:streamName
 *   GET /api/v1/resources/streams/:streamName/consumers
 *   DELETE /api/v1/resources/streams/:streamName
 */
void setup_stream_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup status and analytics endpoints
 * Routes:
 *   GET /api/v1/status
 *   GET /api/v1/status/queues
 *   GET /api/v1/status/queues/:queue
 *   GET /api/v1/status/queues/:queue/messages
 *   GET /api/v1/status/analytics
 *   GET /api/v1/analytics/system-metrics
 *   GET /api/v1/status/buffers
 */
void setup_status_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup consumer group endpoints
 * Routes:
 *   GET /api/v1/consumer-groups
 *   GET /api/v1/consumer-groups/lagging
 *   GET /api/v1/consumer-groups/:group
 *   DELETE /api/v1/consumer-groups/:group
 *   POST /api/v1/consumer-groups/:group/subscription
 */
void setup_consumer_group_routes(uWS::App* app, const RouteContext& ctx);

/**
 * Setup static file serving for webapp
 * Routes:
 *   GET /
 *   GET /assets/*
 *   GET /* (SPA fallback)
 */
void setup_static_file_routes(uWS::App* app, const RouteContext& ctx);

} // namespace routes
} // namespace queen
```

---

## 4. Implement Route Helpers

**File:** `server/src/routes/route_helpers.cpp`

**Content to extract from acceptor_server.cpp lines 100-320:**

```cpp
#include "queen/routes/route_helpers.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <regex>
#include <algorithm>

namespace queen {
namespace routes {

// Extract lines 100-105 (setup_cors_headers)
void setup_cors_headers(uWS::HttpResponse<false>* res) {
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res->writeHeader("Access-Control-Max-Age", "86400");
}

// Extract lines 107-112 (send_json_response)
void send_json_response(uWS::HttpResponse<false>* res, const nlohmann::json& json, int status_code) {
    setup_cors_headers(res);
    res->writeHeader("Content-Type", "application/json");
    res->writeStatus(std::to_string(status_code));
    res->end(json.dump());
}

// Extract lines 114-117 (send_error_response)
void send_error_response(uWS::HttpResponse<false>* res, const std::string& error, int status_code) {
    nlohmann::json error_json = {{"error", error}};
    send_json_response(res, error_json, status_code);
}

// Extract lines 120-147 (read_json_body)
void read_json_body(uWS::HttpResponse<false>* res,
                    std::function<void(const nlohmann::json&)> callback,
                    std::function<void(const std::string&)> error_callback) {
    auto buffer = std::make_shared<std::string>();
    auto completed = std::make_shared<bool>(false);
    
    res->onData([callback, error_callback, buffer, completed](std::string_view chunk, bool is_last) {
        buffer->append(chunk);
        
        if (is_last && !*completed) {
            *completed = true;
            try {
                if (buffer->empty()) {
                    error_callback("Empty request body");
                } else {
                    nlohmann::json json = nlohmann::json::parse(*buffer);
                    callback(json);
                }
            } catch (const std::exception& e) {
                error_callback("Invalid JSON: " + std::string(e.what()));
            }
        }
    });
    
    res->onAborted([completed]() {
        *completed = true;
    });
}

// Extract lines 150-170 (url_decode)
std::string url_decode(const std::string& str) {
    std::string result;
    result.reserve(str.length());
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream is(str.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

// Extract lines 172-180 (get_query_param)
std::string get_query_param(uWS::HttpRequest* req, const std::string& key, const std::string& default_value) {
    std::string query = std::string(req->getQuery());
    size_t pos = query.find(key + "=");
    if (pos == std::string::npos) return default_value;
    pos += key.length() + 1;
    size_t end = query.find('&', pos);
    if (end == std::string::npos) end = query.length();
    return url_decode(query.substr(pos, end - pos));
}

// Extract lines 182-190 (get_query_param_int)
int get_query_param_int(uWS::HttpRequest* req, const std::string& key, int default_value) {
    std::string value = get_query_param(req, key);
    if (value.empty()) return default_value;
    try {
        return std::stoi(value);
    } catch (...) {
        return default_value;
    }
}

// Extract lines 192-196 (get_query_param_bool)
bool get_query_param_bool(uWS::HttpRequest* req, const std::string& key, bool default_value) {
    std::string value = get_query_param(req, key);
    if (value.empty()) return default_value;
    return value == "true" || value == "1";
}

// Extract lines 206-234 (get_mime_type)
std::string get_mime_type(const std::string& file_path) {
    std::filesystem::path p(file_path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    static const std::map<std::string, std::string> mime_types = {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".mjs", "application/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".eot", "application/vnd.ms-fontobject"},
        {".webp", "image/webp"},
        {".wasm", "application/wasm"}
    };
    
    auto it = mime_types.find(ext);
    return it != mime_types.end() ? it->second : "application/octet-stream";
}

// Extract lines 237-320 (serve_static_file)
bool serve_static_file(uWS::HttpResponse<false>* res, 
                       const std::string& file_path,
                       const std::string& webapp_root) {
    try {
        // Convert to absolute paths first
        std::filesystem::path abs_file_path = std::filesystem::absolute(file_path);
        std::filesystem::path abs_webapp_root = std::filesystem::absolute(webapp_root);
        
        // Check if file exists first (canonical throws if file doesn't exist)
        if (!std::filesystem::exists(abs_file_path)) {
            return false; // File not found
        }
        
        if (!std::filesystem::is_regular_file(abs_file_path)) {
            return false; // Not a file
        }
        
        // Now safe to get canonical path for security check
        std::filesystem::path normalized_path = std::filesystem::canonical(abs_file_path);
        std::filesystem::path root_path = std::filesystem::canonical(abs_webapp_root);
        
        // Security check - ensure normalized path is within webapp root
        auto normalized_str = normalized_path.string();
        auto root_str = root_path.string();
        if (normalized_str.substr(0, root_str.length()) != root_str) {
            spdlog::warn("Directory traversal attempt blocked: {}", file_path);
            res->writeStatus("403");
            res->end("Forbidden");
            return true;
        }
        
        // Read file content
        std::ifstream file(normalized_path, std::ios::binary);
        if (!file) {
            return false;
        }
        
        // Get file size
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Read into string
        std::string content(file_size, '\0');
        file.read(&content[0], file_size);
        file.close();
        
        // Determine MIME type
        std::string mime_type = get_mime_type(file_path);
        
        // Determine cache strategy
        std::string cache_control;
        std::string file_name = normalized_path.filename().string();
        
        // Fingerprinted assets (Vite-style: name-hash.js)
        std::regex fingerprint_regex(R"(.*\.[a-f0-9]{8,}\.(js|css)$)");
        if (std::regex_match(file_name, fingerprint_regex)) {
            cache_control = "public, max-age=31536000, immutable";
        }
        // index.html - never cache
        else if (file_name == "index.html") {
            cache_control = "no-cache, no-store, must-revalidate";
        }
        // Other files - cache for 1 hour
        else {
            cache_control = "public, max-age=3600";
        }
        
        // Send response
        setup_cors_headers(res);
        res->writeHeader("Content-Type", mime_type);
        res->writeHeader("Cache-Control", cache_control);
        res->writeStatus("200");
        res->end(content);  // uWebSockets automatically sets Content-Length
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Error serving static file {}: {}", file_path, e.what());
        res->writeStatus("500");
        res->end("Internal Server Error");
        return true;
    }
}

// NEW: Helper for parsing subscription mode (deduplicates logic from lines 646-665, 901-923, 1140-1164)
std::pair<std::string, std::string> parse_subscription_mode(
    const std::string& sub_mode,
    const std::string& sub_from,
    const std::string& default_sub_mode) {
    
    std::string effective_sub_mode = sub_mode.empty() ? default_sub_mode : sub_mode;
    std::string subscription_mode_value;
    std::string subscription_timestamp_sql;
    
    // Check subscriptionFrom parameter first (explicit user preference takes precedence)
    if (!sub_from.empty() && sub_from != "now") {
        subscription_mode_value = "timestamp";
        subscription_timestamp_sql = "'" + sub_from + "'::timestamptz";
    } else if (effective_sub_mode == "new" || effective_sub_mode == "new-only" || sub_from == "now") {
        subscription_mode_value = "new";
        subscription_timestamp_sql = "NOW()";
    } else {
        subscription_mode_value = "all";
        subscription_timestamp_sql = "'1970-01-01 00:00:00+00'";
    }
    
    return {subscription_mode_value, subscription_timestamp_sql};
}

// NEW: Helper for formatting timestamps (deduplicates logic from lines 724-730, 981-987, 1220-1226)
std::string format_timestamp_iso8601(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    
    return ss.str();
}

} // namespace routes
} // namespace queen
```

---

## 5. Implement Individual Route Files

### 5.1 Health Routes

**File:** `server/src/routes/health.cpp`

Extract from acceptor_server.cpp lines 332-355

```cpp
#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include <spdlog/spdlog.h>

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
            send_json_response(res, response, healthy ? 200 : 503);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen
```

### 5.2 Maintenance Routes

**File:** `server/src/routes/maintenance.cpp`

Extract from acceptor_server.cpp lines 358-411

```cpp
#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include <spdlog/spdlog.h>

namespace queen {
namespace routes {

void setup_maintenance_routes(uWS::App* app, const RouteContext& ctx) {
    // GET maintenance mode status (always fetch fresh from DB)
    app->get("/api/v1/system/maintenance", [ctx](auto* res, auto* req) {
        (void)req;
        try {
            // Force fresh check from database (bypass cache for status endpoint)
            bool current_mode = ctx.async_queue_manager->get_maintenance_mode_fresh();
            auto buffer_stats = ctx.async_queue_manager->get_buffer_stats();
            
            nlohmann::json response = {
                {"maintenanceMode", current_mode},
                {"bufferedMessages", ctx.async_queue_manager->get_buffer_pending_count()},
                {"bufferHealthy", ctx.async_queue_manager->is_buffer_healthy()},
                {"bufferStats", buffer_stats}
            };
            send_json_response(res, response);
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // POST toggle maintenance mode
    app->post("/api/v1/system/maintenance", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("enabled") || !body["enabled"].is_boolean()) {
                        send_error_response(res, "enabled (boolean) is required", 400);
                        return;
                    }
                    
                    bool enable = body["enabled"];
                    ctx.async_queue_manager->set_maintenance_mode(enable);
                    
                    nlohmann::json response = {
                        {"maintenanceMode", enable},
                        {"bufferedMessages", ctx.async_queue_manager->get_buffer_pending_count()},
                        {"bufferHealthy", ctx.async_queue_manager->is_buffer_healthy()},
                        {"message", enable ? 
                            "Maintenance mode ENABLED. All PUSHes routing to file buffer." :
                            "Maintenance mode DISABLED. Background processor will drain buffer to DB."
                        }
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
```

### 5.3 Configure Routes

**File:** `server/src/routes/configure.cpp`

Extract from acceptor_server.cpp lines 414-511

```cpp
#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include <spdlog/spdlog.h>

namespace queen {
namespace routes {

void setup_configure_routes(uWS::App* app, const RouteContext& ctx) {
    app->post("/api/v1/configure", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    spdlog::debug("Configure request body: {}", body.dump());
                    
                    if (!body.contains("queue") || !body["queue"].is_string()) {
                        send_error_response(res, "queue is required", 400);
                        return;
                    }
                    
                    std::string queue_name = body["queue"];
                    spdlog::debug("Queue name: {}", queue_name);
                    
                    std::string namespace_name = "";
                    std::string task_name = "";
                    
                    // Safe null handling
                    try {
                        if (body.contains("namespace")) {
                            if (body["namespace"].is_string()) {
                                namespace_name = body["namespace"].get<std::string>();
                            }
                        }
                        if (body.contains("task")) {
                            if (body["task"].is_string()) {
                                task_name = body["task"].get<std::string>();
                            }
                        }
                    } catch (const std::exception& e) {
                        spdlog::error("Error parsing namespace/task: {}", e.what());
                    }
                    
                    spdlog::debug("Namespace: '{}', Task: '{}'", namespace_name, task_name);
                    
                    QueueOptions options;
                    if (body.contains("options") && body["options"].is_object()) {
                        auto opts = body["options"];
                        options.lease_time = opts.value("leaseTime", 300);
                        options.max_size = opts.value("maxSize", 0);  // Default: unlimited (0)
                        options.ttl = opts.value("ttl", 3600);
                        options.retry_limit = opts.value("retryLimit", 3);
                        options.retry_delay = opts.value("retryDelay", 1000);
                        options.dead_letter_queue = opts.value("deadLetterQueue", false);
                        options.dlq_after_max_retries = opts.value("dlqAfterMaxRetries", false);
                        options.priority = opts.value("priority", 0);
                        options.delayed_processing = opts.value("delayedProcessing", 0);
                        options.window_buffer = opts.value("windowBuffer", 0);
                        options.retention_seconds = opts.value("retentionSeconds", 0);
                        options.completed_retention_seconds = opts.value("completedRetentionSeconds", 0);
                        options.retention_enabled = opts.value("retentionEnabled", false);
                        options.encryption_enabled = opts.value("encryptionEnabled", false);
                        options.max_wait_time_seconds = opts.value("maxWaitTimeSeconds", 0);
                    }
                    
                    spdlog::info("[Worker {}] Configuring queue: {}", ctx.worker_id, queue_name);
                    bool success = ctx.async_queue_manager->configure_queue(queue_name, options, namespace_name, task_name);
                    
                    spdlog::info("[Worker {}] Configure queue result: success={}", ctx.worker_id, success);
                    
                    if (success) {
                        nlohmann::json response = {
                            {"configured", true},
                            {"namespace", namespace_name},
                            {"task", task_name},
                            {"queue", queue_name},
                            {"options", {
                                {"completedRetentionSeconds", options.completed_retention_seconds},
                                {"deadLetterQueue", options.dead_letter_queue},
                                {"delayedProcessing", options.delayed_processing},
                                {"dlqAfterMaxRetries", options.dlq_after_max_retries},
                                {"leaseTime", options.lease_time},
                                {"maxSize", options.max_size},
                                {"priority", options.priority},
                                {"retentionEnabled", options.retention_enabled},
                                {"retentionSeconds", options.retention_seconds},
                                {"retryDelay", options.retry_delay},
                                {"retryLimit", options.retry_limit},
                                {"ttl", options.ttl},
                                {"windowBuffer", options.window_buffer},
                                {"encryptionEnabled", options.encryption_enabled},
                                {"maxWaitTimeSeconds", options.max_wait_time_seconds}
                            }}
                        };
                        send_json_response(res, response, 200);
                    } else {
                        send_error_response(res, "Failed to configure queue", 500);
                    }
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
```

### 5.4 Push Routes

**File:** `server/src/routes/push.cpp`

Extract from acceptor_server.cpp lines 515-610

```cpp
#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/file_buffer.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

namespace queen {
namespace routes {

void setup_push_routes(uWS::App* app, const RouteContext& ctx) {
    // ASYNC PUSH - NON-BLOCKING WITH ASYNC DB POOL (NO THREAD POOL NEEDED!)
    app->post("/api/v1/push", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("items") || !body["items"].is_array()) {
                        send_error_response(res, "items array is required", 400);
                        return;
                    }
                    
                    spdlog::debug("[Worker {}] PUSH: Processing {} items (async, non-blocking)", 
                                 ctx.worker_id, body["items"].size());
                    
                    // Parse and validate items
                    std::vector<PushItem> items;
                    for (const auto& item_json : body["items"]) {
                        // Validate partition type if present
                        if (item_json.contains("partition") && !item_json["partition"].is_string()) {
                            send_error_response(res, "Partition must be a string", 400);
                            return;
                        }
                        
                        // Validate queue type
                        if (!item_json.contains("queue") || !item_json["queue"].is_string()) {
                            send_error_response(res, "Queue must be a string", 400);
                            return;
                        }
                        
                        // Validate transactionId type if present
                        if (item_json.contains("transactionId") && !item_json["transactionId"].is_string()) {
                            send_error_response(res, "TransactionId must be a string", 400);
                            return;
                        }
                        
                        // Validate traceId type if present
                        if (item_json.contains("traceId") && !item_json["traceId"].is_string()) {
                            send_error_response(res, "TraceId must be a string", 400);
                            return;
                        }
                        
                        PushItem item;
                        item.queue = item_json["queue"];
                        item.partition = item_json.value("partition", "Default");
                        item.payload = item_json.value("payload", nlohmann::json{});
                        if (item_json.contains("transactionId")) {
                            item.transaction_id = item_json["transactionId"];
                        }
                        if (item_json.contains("traceId")) {
                            item.trace_id = item_json["traceId"];
                        }
                        items.push_back(std::move(item));
                    }
                    
                    // Execute async push operation (non-blocking, polls socket internally)
                    // This runs directly in the uWebSockets thread!
                    auto push_start = std::chrono::steady_clock::now();
                    auto results = ctx.async_queue_manager->push_messages(items);
                    auto push_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - push_start).count();
                    
                    spdlog::info("[Worker {}] PUSH: Async push_messages completed in {}ms ({} items)", 
                                ctx.worker_id, push_ms, items.size());
                    
                    // Convert results to JSON and respond immediately
                    nlohmann::json json_results = nlohmann::json::array();
                    for (const auto& result : results) {
                        nlohmann::json json_result;
                        json_result["transaction_id"] = result.transaction_id;
                        json_result["status"] = result.status;
                        
                        if (result.error) {
                            json_result["error"] = *result.error;
                        }
                        if (result.message_id) {
                            json_result["message_id"] = *result.message_id;
                        }
                        if (result.trace_id) {
                            json_result["trace_id"] = *result.trace_id;
                        }
                        
                        json_results.push_back(json_result);
                    }
                    
                    // Send response immediately (we're in the uWebSockets thread)
                    send_json_response(res, json_results, 201);
                    
                } catch (const std::exception& e) {
                    spdlog::error("[Worker {}] PUSH: Error: {}", ctx.worker_id, e.what());
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
```

### 5.5 Pop Routes

**File:** `server/src/routes/pop.cpp`

Extract from acceptor_server.cpp lines 613-761, 871-1018, 1116-1256

This is the LARGEST file with 3 variants. Need to add helper to deduplicate consumer group recording.

```cpp
#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/poll_intention_registry.hpp"
#include "queen/response_queue.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <sstream>
#include <iomanip>

// External globals (declared in acceptor_server.cpp)
namespace queen {
extern std::shared_ptr<ResponseRegistry> global_response_registry;
extern std::shared_ptr<PollIntentionRegistry> global_poll_intention_registry;
}

namespace queen {
namespace routes {

// Helper: Record consumer group subscription with deduplication
static void record_consumer_group_subscription_if_needed(
    const RouteContext& ctx,
    const std::string& consumer_group,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& namespace_name,
    const std::string& task_name,
    const std::string& sub_mode,
    const std::string& sub_from) {
    
    if (consumer_group == "__QUEUE_MODE__") {
        return;  // Skip for queue mode
    }
    
    auto [mode_value, timestamp_sql] = parse_subscription_mode(
        sub_mode, sub_from, ctx.config.queue.default_subscription_mode
    );
    
    ctx.async_queue_manager->record_consumer_group_subscription(
        consumer_group, queue_name, partition_name, namespace_name, task_name,
        mode_value, timestamp_sql
    );
}

// Helper: Build message JSON response (deduplicate lines 723-746, 980-1003, 1219-1242)
static nlohmann::json build_message_json(const Message& msg, const std::string& consumer_group, const std::optional<std::string>& lease_id) {
    std::string created_at_str = format_timestamp_iso8601(msg.created_at);
    
    nlohmann::json msg_json = {
        {"id", msg.id},
        {"transactionId", msg.transaction_id},
        {"partitionId", msg.partition_id},
        {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
        {"queue", msg.queue_name},
        {"partition", msg.partition_name},
        {"data", msg.payload},
        {"retryCount", msg.retry_count},
        {"priority", msg.priority},
        {"createdAt", created_at_str},
        {"consumerGroup", consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(consumer_group)},
        {"leaseId", lease_id.has_value() ? nlohmann::json(*lease_id) : nlohmann::json(nullptr)}
    };
    
    return msg_json;
}

void setup_pop_routes(uWS::App* app, const RouteContext& ctx) {
    // SPECIFIC POP from queue/partition - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop/queue/:queue/partition/:partition", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string partition_name = std::string(req->getParameter(1));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", ctx.config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", ctx.config.queue.default_batch_size);
            
            auto pool_stats = ctx.async_queue_manager->get_pool_stats();
            spdlog::info("[Worker {}] SPOP: [{}/{}@{}] batch={}, wait={} | Pool: {}/{} conn ({} in use)", 
                        ctx.worker_id, queue_name, partition_name, consumer_group, batch, wait,
                        pool_stats.available, pool_stats.total, pool_stats.in_use);
            
            PopOptions options;
            options.wait = false;  // Always false - registry handles waiting
            options.timeout = timeout_ms;
            options.batch = batch;
            options.auto_ack = get_query_param_bool(req, "autoAck", false);
            
            // Parse subscription mode
            std::string sub_mode = get_query_param(req, "subscriptionMode", "");
            if (!sub_mode.empty()) {
                options.subscription_mode = sub_mode;
            }
            std::string sub_from = get_query_param(req, "subscriptionFrom", "");
            if (!sub_from.empty()) {
                options.subscription_from = sub_from;
            }
            
            // Record consumer group subscription metadata (for NEW mode support)
            record_consumer_group_subscription_if_needed(
                ctx, consumer_group, queue_name, partition_name, "", "", sub_mode, sub_from
            );
            
            if (wait) {
                // Register response with abort callback to clean up intention on disconnect
                std::string request_id = global_response_registry->register_response(res, ctx.worker_id,
                    [](const std::string& req_id) {
                        // Remove intention from registry when connection aborts
                        global_poll_intention_registry->remove_intention(req_id);
                        spdlog::info("SPOP: Connection aborted, removed poll intention {}", req_id);
                    });
            
                // Use Poll Intention Registry for long-polling
                queen::PollIntention intention{
                    .request_id = request_id,
                    .worker_id = ctx.worker_id,
                    .queue_name = queue_name,
                    .partition_name = partition_name,  // Specific partition
                    .namespace_name = std::nullopt,
                    .task_name = std::nullopt,
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now(),
                    .subscription_mode = options.subscription_mode,
                    .subscription_from = options.subscription_from
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                spdlog::info("[Worker {}] SPOP: Registered poll intention {} for queue {}/{} (wait=true)", 
                            ctx.worker_id, request_id, queue_name, partition_name);
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use AsyncQueueManager directly in uWS event loop
            spdlog::info("[Worker {}] SPOP: Executing immediate pop for {}/{} (wait=false)", ctx.worker_id, queue_name, partition_name);
                
            try {
                auto start_time = std::chrono::steady_clock::now();
                auto result = ctx.async_queue_manager->pop_messages_from_partition(queue_name, partition_name, consumer_group, options);
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                
                if (result.messages.empty()) {
                    // No messages - send 204 No Content
                    nlohmann::json empty_response;
                    send_json_response(res, empty_response, 204);
                    spdlog::info("[Worker {}] SPOP: No messages for [{}/{}], {}ms", 
                               ctx.worker_id, queue_name, partition_name, duration_ms);
                    return;
                }
                
                // Build response JSON
                nlohmann::json response = {{"messages", nlohmann::json::array()}};
                
                for (const auto& msg : result.messages) {
                    response["messages"].push_back(build_message_json(msg, consumer_group, result.lease_id));
                }
                
                send_json_response(res, response, 200);
                spdlog::info("[Worker {}] SPOP: Sent response for [{}/{}] ({} messages, {}ms)", 
                           ctx.worker_id, queue_name, partition_name, result.messages.size(), duration_ms);
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
                spdlog::error("[Worker {}] SPOP: Error for [{}/{}]: {}", ctx.worker_id, queue_name, partition_name, e.what());
            }
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // POP from queue (any partition) - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop/queue/:queue", [ctx](auto* res, auto* req) {
        try {
            std::string queue_name = std::string(req->getParameter(0));
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", ctx.config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", ctx.config.queue.default_batch_size);
            
            auto pool_stats = ctx.async_queue_manager->get_pool_stats();
            spdlog::info("[Worker {}] QPOP: [{}/*@{}] batch={}, wait={} | Pool: {}/{} conn ({} in use)", 
                        ctx.worker_id, queue_name, consumer_group, batch, wait,
                        pool_stats.available, pool_stats.total, pool_stats.in_use);
            
            PopOptions options;
            options.wait = false;  // Always false - registry handles waiting
            options.timeout = timeout_ms;
            options.batch = batch;
            options.auto_ack = get_query_param_bool(req, "autoAck", false);
            
            // Parse subscription mode
            std::string sub_mode = get_query_param(req, "subscriptionMode", "");
            if (!sub_mode.empty()) {
                options.subscription_mode = sub_mode;
            }
            std::string sub_from = get_query_param(req, "subscriptionFrom", "");
            if (!sub_from.empty()) {
                options.subscription_from = sub_from;
            }
            
            // Record consumer group subscription metadata (for NEW mode support)
            record_consumer_group_subscription_if_needed(
                ctx, consumer_group, queue_name, "", "", "", sub_mode, sub_from
            );
            
            if (wait) {
                // Register response with abort callback to clean up intention on disconnect
                std::string request_id = global_response_registry->register_response(res, ctx.worker_id,
                    [](const std::string& req_id) {
                        // Remove intention from registry when connection aborts
                        global_poll_intention_registry->remove_intention(req_id);
                        spdlog::info("QPOP: Connection aborted, removed poll intention {}", req_id);
                    });
            
                // Use Poll Intention Registry for long-polling
                queen::PollIntention intention{
                    .request_id = request_id,
                    .worker_id = ctx.worker_id,
                    .queue_name = queue_name,
                    .partition_name = std::nullopt,  // Any partition
                    .namespace_name = std::nullopt,
                    .task_name = std::nullopt,
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now(),
                    .subscription_mode = options.subscription_mode,
                    .subscription_from = options.subscription_from
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                spdlog::info("[Worker {}] QPOP: Registered poll intention {} for queue {} (wait=true)", 
                            ctx.worker_id, request_id, queue_name);
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use AsyncQueueManager directly in uWS event loop
            spdlog::info("[Worker {}] QPOP: Executing immediate pop for {}/* (wait=false)", ctx.worker_id, queue_name);
                
            try {
                auto start_time = std::chrono::steady_clock::now();
                auto result = ctx.async_queue_manager->pop_messages_from_queue(queue_name, consumer_group, options);
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                
                if (result.messages.empty()) {
                    // No messages - send 204 No Content
                    nlohmann::json empty_response;
                    send_json_response(res, empty_response, 204);
                    spdlog::info("[Worker {}] QPOP: No messages for [{}/*], {}ms", 
                               ctx.worker_id, queue_name, duration_ms);
                    return;
                }
                
                // Build response JSON
                nlohmann::json response = {{"messages", nlohmann::json::array()}};
                
                for (const auto& msg : result.messages) {
                    response["messages"].push_back(build_message_json(msg, consumer_group, result.lease_id));
                }
                
                send_json_response(res, response, 200);
                spdlog::info("[Worker {}] QPOP: Sent response for [{}/*] ({} messages, {}ms)", 
                           ctx.worker_id, queue_name, result.messages.size(), duration_ms);
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
                spdlog::error("[Worker {}] QPOP: Error for [{}/*]: {}", ctx.worker_id, queue_name, e.what());
            }
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
    
    // ASYNC POP with namespace/task filtering (no specific queue) - NEW RESPONSE QUEUE ARCHITECTURE WITH POLL INTENTION REGISTRY
    app->get("/api/v1/pop", [ctx](auto* res, auto* req) {
        try {
            std::string consumer_group = get_query_param(req, "consumerGroup", "__QUEUE_MODE__");
            std::string namespace_param = get_query_param(req, "namespace", "");
            std::string task_param = get_query_param(req, "task", "");
            
            std::optional<std::string> namespace_name = namespace_param.empty() ? std::nullopt : std::optional<std::string>(namespace_param);
            std::optional<std::string> task_name = task_param.empty() ? std::nullopt : std::optional<std::string>(task_param);
            
            bool wait = get_query_param_bool(req, "wait", false);
            int timeout_ms = get_query_param_int(req, "timeout", ctx.config.queue.default_timeout);
            int batch = get_query_param_int(req, "batch", ctx.config.queue.default_batch_size);
            
            PopOptions options;
            options.wait = false;  // Always false - registry handles waiting
            options.timeout = timeout_ms;
            options.batch = batch;
            options.auto_ack = get_query_param_bool(req, "autoAck", false);
            
            std::string sub_mode = get_query_param(req, "subscriptionMode", "");
            if (!sub_mode.empty()) options.subscription_mode = sub_mode;
            std::string sub_from = get_query_param(req, "subscriptionFrom", "");
            if (!sub_from.empty()) options.subscription_from = sub_from;
            
            // Record consumer group subscription metadata (for NEW mode support)
            record_consumer_group_subscription_if_needed(
                ctx, consumer_group, "", "", namespace_param, task_param, sub_mode, sub_from
            );
            
            if (wait) {
                // Register response with abort callback to clean up intention on disconnect
                std::string request_id = global_response_registry->register_response(res, ctx.worker_id,
                    [](const std::string& req_id) {
                        // Remove intention from registry when connection aborts
                        global_poll_intention_registry->remove_intention(req_id);
                        spdlog::info("POP: Connection aborted, removed poll intention {}", req_id);
                    });
            
                // Use Poll Intention Registry for long-polling
                queen::PollIntention intention{
                    .request_id = request_id,
                    .worker_id = ctx.worker_id,
                    .queue_name = std::nullopt,
                    .partition_name = std::nullopt,
                    .namespace_name = namespace_name,  // Namespace filtering
                    .task_name = task_name,            // Task filtering
                    .consumer_group = consumer_group,
                    .batch_size = batch,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                    .created_at = std::chrono::steady_clock::now(),
                    .subscription_mode = options.subscription_mode,
                    .subscription_from = options.subscription_from
                };
                
                global_poll_intention_registry->register_intention(intention);
                
                spdlog::info("[Worker {}] POP: Registered poll intention {} for namespace={} task={} (wait=true)", 
                            ctx.worker_id, request_id, 
                            namespace_name.value_or("*"), 
                            task_name.value_or("*"));
                
                // Return immediately - poll workers will handle it
                return;
            }
            
            // Non-waiting mode: use AsyncQueueManager directly in uWS event loop
            spdlog::info("[Worker {}] POP: Executing immediate pop for namespace={} task={} (wait=false)", 
                        ctx.worker_id, namespace_name.value_or("*"), task_name.value_or("*"));
                
            try {
                auto result = ctx.async_queue_manager->pop_messages_filtered(namespace_name, task_name, consumer_group, options);
                
                if (result.messages.empty()) {
                    // No messages - send 204 No Content
                    nlohmann::json empty_response;
                    send_json_response(res, empty_response, 204);
                    spdlog::debug("[Worker {}] POP: No messages", ctx.worker_id);
                    return;
                }
                
                // Build response JSON
                nlohmann::json response = {{"messages", nlohmann::json::array()}};
                for (const auto& msg : result.messages) {
                    response["messages"].push_back(build_message_json(msg, consumer_group, result.lease_id));
                }
                
                send_json_response(res, response, 200);
                spdlog::info("[Worker {}] POP: Sent response ({} messages)", ctx.worker_id, result.messages.size());
                
            } catch (const std::exception& e) {
                send_error_response(res, e.what(), 500);
                spdlog::error("[Worker {}] POP: Error: {}", ctx.worker_id, e.what());
            }
            
        } catch (const std::exception& e) {
            send_error_response(res, e.what(), 500);
        }
    });
}

} // namespace routes
} // namespace queen
```

### 5.6 Ack Routes

**File:** `server/src/routes/ack.cpp`

Extract from acceptor_server.cpp lines 764-848, 1020-1113

```cpp
#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace queen {
namespace routes {

void setup_ack_routes(uWS::App* app, const RouteContext& ctx) {
    // ASYNC ACK batch
    app->post("/api/v1/ack/batch", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("acknowledgments") || !body["acknowledgments"].is_array()) {
                        send_error_response(res, "acknowledgments array is required", 400);
                        return;
                    }
                    
                    auto acknowledgments = body["acknowledgments"];
                    std::string consumer_group = "__QUEUE_MODE__";
                    
                    if (body.contains("consumerGroup") && !body["consumerGroup"].is_null() && body["consumerGroup"].is_string()) {
                        consumer_group = body["consumerGroup"];
                    }
                    
                    // Validate each acknowledgment has required fields
                    std::vector<nlohmann::json> ack_items;
                    for (const auto& ack_json : acknowledgments) {
                        if (!ack_json.contains("transactionId") || ack_json["transactionId"].is_null() || !ack_json["transactionId"].is_string()) {
                            send_error_response(res, "Each acknowledgment must have a valid transactionId string", 400);
                            return;
                        }
                        
                        // CRITICAL: partition_id is now MANDATORY
                        if (!ack_json.contains("partitionId") || ack_json["partitionId"].is_null() || !ack_json["partitionId"].is_string()) {
                            send_error_response(res, "Each acknowledgment must have a valid partitionId string to ensure message uniqueness", 400);
                            return;
                        }
                        
                        // Build ack item with consumer group
                        nlohmann::json ack_item = ack_json;
                        ack_item["consumerGroup"] = consumer_group;
                        ack_items.push_back(ack_item);
                    }
                    
                    spdlog::info("[Worker {}] ACK BATCH: Executing immediate batch ACK ({} items)", ctx.worker_id, ack_items.size());
                    
                    // Execute batch ACK operation directly in uWS event loop
                    try {
                        auto ack_start = std::chrono::steady_clock::now();
                        auto batch_result = ctx.async_queue_manager->acknowledge_messages_batch(ack_items);
                        auto ack_end = std::chrono::steady_clock::now();
                        auto ack_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ack_end - ack_start).count();
                        
                        spdlog::info("[Worker {}] ACK BATCH: {} items, {}ms (success: {}, failed: {})", 
                                    ctx.worker_id, batch_result.results.size(), ack_duration_ms,
                                    batch_result.successful_acks, batch_result.failed_acks);
                        
                        nlohmann::json response = {
                            {"successful", batch_result.successful_acks},
                            {"failed", batch_result.failed_acks},
                            {"results", nlohmann::json::array()}
                        };
                        
                        for (const auto& ack_result : batch_result.results) {
                            nlohmann::json result_item = {
                                {"success", ack_result.success},
                                {"message", ack_result.message}
                            };
                            if (ack_result.error.has_value()) {
                                result_item["error"] = *ack_result.error;
                            }
                            response["results"].push_back(result_item);
                        }
                        
                        send_json_response(res, response, 200);
                        spdlog::info("[Worker {}] ACK BATCH: Sent response ({} items)", ctx.worker_id, batch_result.results.size());
                        
                    } catch (const std::exception& e) {
                        send_error_response(res, e.what(), 500);
                        spdlog::error("[Worker {}] ACK BATCH: Error: {}", ctx.worker_id, e.what());
                    }
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // ASYNC Single ACK
    app->post("/api/v1/ack", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    std::string transaction_id = "";
                    if (body.contains("transactionId") && !body["transactionId"].is_null() && body["transactionId"].is_string()) {
                        transaction_id = body["transactionId"];
                    } else {
                        send_error_response(res, "transactionId is required", 400);
                        return;
                    }
                    
                    std::string status = "completed";
                    if (body.contains("status") && !body["status"].is_null() && body["status"].is_string()) {
                        status = body["status"];
                    }
                    
                    std::string consumer_group = "__QUEUE_MODE__";
                    if (body.contains("consumerGroup") && !body["consumerGroup"].is_null() && body["consumerGroup"].is_string()) {
                        consumer_group = body["consumerGroup"];
                    }
                    
                    std::optional<std::string> error;
                    if (body.contains("error") && !body["error"].is_null() && body["error"].is_string()) {
                        error = body["error"];
                    }
                    
                    std::optional<std::string> lease_id;
                    if (body.contains("leaseId") && !body["leaseId"].is_null() && body["leaseId"].is_string()) {
                        lease_id = body["leaseId"];
                    }
                    
                    // CRITICAL: partition_id is now MANDATORY to prevent acking wrong message
                    // when transactionId is not unique across partitions
                    std::optional<std::string> partition_id;
                    if (body.contains("partitionId") && !body["partitionId"].is_null() && body["partitionId"].is_string()) {
                        partition_id = body["partitionId"];
                    } else {
                        send_error_response(res, "partitionId is required to ensure message uniqueness", 400);
                        return;
                    }
                    
                    spdlog::info("[Worker {}] ACK: Executing immediate ACK", ctx.worker_id);
                    
                    // Execute ACK operation directly in uWS event loop
                    try {
                        auto ack_start = std::chrono::steady_clock::now();
                        auto ack_result = ctx.async_queue_manager->acknowledge_message(transaction_id, status, error, consumer_group, lease_id, partition_id);
                        auto ack_end = std::chrono::steady_clock::now();
                        auto ack_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ack_end - ack_start).count();
                        
                        spdlog::debug("[Worker {}] ACK: 1 item, {}ms", ctx.worker_id, ack_duration_ms);
                        
                        if (ack_result.success) {
                            auto now = std::chrono::system_clock::now();
                            std::string ack_timestamp = format_timestamp_iso8601(now);
                            
                            nlohmann::json response = {
                                {"transactionId", transaction_id},
                                {"status", ack_result.message},
                                {"consumerGroup", consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(consumer_group)},
                                {"acknowledgedAt", ack_timestamp}
                            };
                            
                            send_json_response(res, response, 200);
                            spdlog::info("[Worker {}] ACK: Sent success response", ctx.worker_id);
                        } else {
                            nlohmann::json error_response = {{"error", "Failed to acknowledge message: " + ack_result.message}};
                            send_json_response(res, error_response, 500);
                            spdlog::warn("[Worker {}] ACK: Sent error response: {}", ctx.worker_id, ack_result.message);
                        }
                        
                    } catch (const std::exception& e) {
                        send_error_response(res, e.what(), 500);
                        spdlog::error("[Worker {}] ACK: Error: {}", ctx.worker_id, e.what());
                    }
                    
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
```

Due to length constraints, I'll continue with the remaining route files in summary form:

### 5.7-5.17 Remaining Route Files

Create the following files following the same pattern:

- **transactions.cpp** (lines 1258-1315): Extract transaction endpoint
- **leases.cpp** (lines 1318-1348): Extract lease extension endpoint  
- **metrics.cpp** (lines 1355-1363): Extract metrics endpoint
- **resources.cpp** (lines 1366-1422): Extract resource listing endpoints (queues, namespaces, tasks, overview)
- **messages.cpp** (lines 1425-1483): Extract message CRUD endpoints
- **dlq.cpp** (lines 1486-1502): Extract DLQ endpoint
- **traces.cpp** (lines 1509-1609): Extract trace recording/retrieval endpoints
- **streams.cpp** (lines 1616-1710): Extract all streaming endpoints
- **status.cpp** (lines 1717-1813, 1903-1929): Extract status/analytics endpoints including buffers
- **consumer_groups.cpp** (lines 1816-1901): Extract consumer group management endpoints
- **static_files.cpp** (lines 1935-2025): Extract static file serving logic with webapp_root computation

Each file should follow this structure:
1. Include necessary headers
2. Use `namespace queen::routes`
3. Capture `ctx` by value in lambdas
4. Use helper functions from `route_helpers.hpp`
5. Access globals like `global_stream_manager` directly

---

## 6. Modify acceptor_server.cpp

**File:** `server/src/acceptor_server.cpp`

### 6.1 Add Includes (after line 30)

```cpp
// Route setup functions
#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
```

### 6.2 Replace setup_worker_routes Function

**Delete:** Lines 322-2026 (entire `setup_worker_routes` function and helper functions)

**Replace with (at line 322):**

```cpp
// Setup routes for a worker app using route registry
static void setup_worker_routes(uWS::App* app, 
                                std::shared_ptr<queen::AsyncQueueManager> async_queue_manager,
                                std::shared_ptr<AnalyticsManager> analytics_manager,
                                std::shared_ptr<FileBufferManager> file_buffer,
                                const Config& config,
                                int worker_id,
                                std::shared_ptr<astp::ThreadPool> db_thread_pool) {
    
    // Create route context with all dependencies
    queen::routes::RouteContext ctx(
        async_queue_manager,
        analytics_manager,
        file_buffer,
        config,
        worker_id,
        db_thread_pool
    );
    
    // Setup all routes in organized categories
    spdlog::debug("[Worker {}] Setting up CORS routes...", worker_id);
    queen::routes::setup_cors_routes(app);
    
    spdlog::debug("[Worker {}] Setting up health routes...", worker_id);
    queen::routes::setup_health_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up maintenance routes...", worker_id);
    queen::routes::setup_maintenance_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up configure routes...", worker_id);
    queen::routes::setup_configure_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up push routes...", worker_id);
    queen::routes::setup_push_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up pop routes...", worker_id);
    queen::routes::setup_pop_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up ack routes...", worker_id);
    queen::routes::setup_ack_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up transaction routes...", worker_id);
    queen::routes::setup_transaction_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up lease routes...", worker_id);
    queen::routes::setup_lease_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up metrics routes...", worker_id);
    queen::routes::setup_metrics_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up resource routes...", worker_id);
    queen::routes::setup_resource_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up message routes...", worker_id);
    queen::routes::setup_message_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up DLQ routes...", worker_id);
    queen::routes::setup_dlq_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up trace routes...", worker_id);
    queen::routes::setup_trace_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up stream routes...", worker_id);
    queen::routes::setup_stream_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up status routes...", worker_id);
    queen::routes::setup_status_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up consumer group routes...", worker_id);
    queen::routes::setup_consumer_group_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up static file routes...", worker_id);
    queen::routes::setup_static_file_routes(app, ctx);
}
```

### 6.3 Add External Global Declarations

After the existing global declarations (around line 48), add:

```cpp
// Export globals for use in route files
namespace queen {
extern std::shared_ptr<ResponseRegistry> global_response_registry;
extern std::shared_ptr<PollIntentionRegistry> global_poll_intention_registry;
extern std::shared_ptr<StreamPollIntentionRegistry> global_stream_poll_registry;
extern std::shared_ptr<StreamManager> global_stream_manager;
}
```

---

## 7. Update CMakeLists.txt

**File:** `server/CMakeLists.txt`

Add new source files to the server target. Find the section that lists source files (likely around `add_executable` or `target_sources`) and add:

```cmake
# Route implementation files
src/routes/route_helpers.cpp
src/routes/health.cpp
src/routes/maintenance.cpp
src/routes/configure.cpp
src/routes/push.cpp
src/routes/pop.cpp
src/routes/ack.cpp
src/routes/transactions.cpp
src/routes/leases.cpp
src/routes/metrics.cpp
src/routes/resources.cpp
src/routes/messages.cpp
src/routes/dlq.cpp
src/routes/traces.cpp
src/routes/streams.cpp
src/routes/status.cpp
src/routes/consumer_groups.cpp
src/routes/static_files.cpp
```

---

## 8. Implementation Order

To minimize risk and enable incremental testing, implement in this specific order:

1. **Create header files first:**
   - `route_context.hpp`
   - `route_helpers.hpp`
   - `route_registry.hpp`

2. **Implement route_helpers.cpp:**
   - Extract all helper functions
   - Compile and verify no errors

3. **Create stub route files (empty implementations):**
   - Create all 17 route .cpp files with empty `setup_*` functions
   - Add to CMakeLists.txt
   - Compile to verify build system works

4. **Modify acceptor_server.cpp:**
   - Add includes
   - Replace `setup_worker_routes` with registry calls
   - Add external declarations
   - **DO NOT delete old code yet - comment it out**

5. **Implement route files in this order (test after each):**
   - health.cpp (simplest, good test case)
   - maintenance.cpp
   - metrics.cpp
   - configure.cpp
   - push.cpp
   - ack.cpp
   - transactions.cpp
   - leases.cpp
   - pop.cpp (complex, test thoroughly)
   - resources.cpp
   - messages.cpp
   - dlq.cpp
   - traces.cpp
   - streams.cpp
   - status.cpp
   - consumer_groups.cpp
   - static_files.cpp

6. **Delete commented old code from acceptor_server.cpp**

---

## 9. Testing Strategy

After each route file implementation:

1. **Compile:** `make clean && make`
2. **Start server:** Verify no crashes on startup
3. **Test specific endpoints:** Use curl or test suite for that route category
4. **Check logs:** Verify worker_id logging works correctly
5. **Load test:** Ensure no performance regression

Example test commands:

```bash
# Health check
curl http://localhost:6632/health

# Push message
curl -X POST http://localhost:6632/api/v1/push \
  -H "Content-Type: application/json" \
  -d '{"items":[{"queue":"test","payload":{"hello":"world"}}]}'

# Pop message
curl "http://localhost:6632/api/v1/pop/queue/test?wait=true&timeout=5000"
```

---

## 10. Expected Line Count Reduction

| File | Before | After | Reduction |
|------|--------|-------|-----------|
| acceptor_server.cpp | 2500 | ~500 | -2000 lines |
| route_helpers.cpp | 0 | ~350 | +350 lines |
| Individual routes | 0 | ~2200 | +2200 lines |
| Headers | 0 | ~200 | +200 lines |
| **Total** | **2500** | **3250** | **+750 lines** |

The total line count increases due to:
- Function declarations in headers
- Namespace wrappers
- Better code organization and spacing
- **Deduplication of repeated logic** (net reduction in duplicate code)

However, **maintainability improves significantly**:
- Each route file is 50-300 lines (manageable)
- Clear separation of concerns
- Easy to locate and modify specific endpoints
- Better test isolation

---

## 11. Potential Issues and Mitigations

### Issue 1: Global Variable Access
**Problem:** Routes need access to `global_response_registry`, `global_poll_intention_registry`, etc.

**Solution:** Declare as `extern` in headers, already defined in acceptor_server.cpp

### Issue 2: Lambda Capture Size
**Problem:** Capturing entire `RouteContext` struct (64 bytes) per lambda

**Solution:** This is acceptable - modern compilers optimize well, and clarity is more important than micro-optimization

### Issue 3: Build Time
**Problem:** More files = longer initial compile

**Solution:** Incremental builds will be faster (only modified routes recompile)

### Issue 4: Header Dependencies
**Problem:** Circular dependencies between headers

**Solution:** Use forward declarations in route_context.hpp, include full headers only in .cpp files

### Issue 5: Duplicate Consumer Group Logic
**Problem:** Consumer group subscription recording duplicated 3 times in pop.cpp

**Solution:** Already addressed with `record_consumer_group_subscription_if_needed()` helper

---

## 12. Post-Refactor Benefits

1. **Maintainability:** Each route file is focused and < 300 lines
2. **Testability:** Can unit test individual route setup functions
3. **Collaboration:** Multiple developers can work on different routes without conflicts
4. **Debugging:** Stack traces show specific route file, not just "acceptor_server.cpp:1234"
5. **Documentation:** Each route file can have focused comments/documentation
6. **Reusability:** Helper functions eliminate ~150 lines of duplication

---

## 13. Validation Checklist

After completing refactor:

- [ ] All endpoints respond correctly
- [ ] No compilation errors or warnings
- [ ] Server starts without crashes
- [ ] All existing tests pass
- [ ] Memory usage unchanged (check with valgrind/heaptrack)
- [ ] Performance unchanged (benchmark critical paths)
- [ ] Logs show correct worker_id
- [ ] Long polling works (pop with wait=true)
- [ ] File buffer maintenance mode works
- [ ] Static file serving works
- [ ] CORS headers present on all responses
- [ ] Error responses formatted correctly
- [ ] Query parameters parsed correctly
- [ ] JSON body parsing handles errors gracefully

---

## 14. Files to Create (Complete List)

**Headers (3):**
1. `server/include/queen/routes/route_context.hpp`
2. `server/include/queen/routes/route_helpers.hpp`
3. `server/include/queen/routes/route_registry.hpp`

**Implementation (18):**
1. `server/src/routes/route_helpers.cpp`
2. `server/src/routes/health.cpp`
3. `server/src/routes/maintenance.cpp`
4. `server/src/routes/configure.cpp`
5. `server/src/routes/push.cpp`
6. `server/src/routes/pop.cpp`
7. `server/src/routes/ack.cpp`
8. `server/src/routes/transactions.cpp`
9. `server/src/routes/leases.cpp`
10. `server/src/routes/metrics.cpp`
11. `server/src/routes/resources.cpp`
12. `server/src/routes/messages.cpp`
13. `server/src/routes/dlq.cpp`
14. `server/src/routes/traces.cpp`
15. `server/src/routes/streams.cpp`
16. `server/src/routes/status.cpp`
17. `server/src/routes/consumer_groups.cpp`
18. `server/src/routes/static_files.cpp`

**Modified (2):**
1. `server/src/acceptor_server.cpp` (lines 322-2026 replaced)
2. `server/CMakeLists.txt` (add new source files)

**Total: 23 files to create/modify**

---

## Conclusion

This refactoring is highly feasible and will significantly improve code organization. The implementation order ensures safety through incremental testing, and the use of a context struct maintains clean interfaces while preserving all functionality.

The key to success is **following the implementation order strictly**.
