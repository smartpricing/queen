#pragma once

#include <App.h>
#include <json.hpp>
#include <string>
#include <functional>
#include <map>
#include <chrono>

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

