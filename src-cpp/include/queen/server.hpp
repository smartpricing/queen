#pragma once

#include "queen/queue_manager.hpp"
#include "queen/config.hpp"
#include <App.h>
#include <json.hpp>
#include <memory>
#include <string>
#include <functional>

namespace queen {

class QueenServer {
private:
    std::shared_ptr<QueueManager> queue_manager_;
    std::shared_ptr<DatabasePool> db_pool_;
    Config config_;
    std::unique_ptr<uWS::App> app_;
    
    // Request handling helpers
    void setup_cors_headers(uWS::HttpResponse<false>* res);
    void send_json_response(uWS::HttpResponse<false>* res, const nlohmann::json& json, int status_code = 200);
    void send_error_response(uWS::HttpResponse<false>* res, const std::string& error, int status_code = 500);
    
    // Route handlers
    void handle_configure(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_push(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_pop_queue_partition(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_pop_queue(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_pop_filtered(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_ack(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    void handle_health(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
    
    // JSON body reading helper
    void read_json_body(uWS::HttpResponse<false>* res, 
                       std::function<void(const nlohmann::json&)> callback,
                       std::function<void(const std::string&)> error_callback);
    
    // Query parameter parsing
    std::string get_query_param(uWS::HttpRequest* req, const std::string& key, const std::string& default_value = "");
    int get_query_param_int(uWS::HttpRequest* req, const std::string& key, int default_value = 0);
    bool get_query_param_bool(uWS::HttpRequest* req, const std::string& key, bool default_value = false);
    
public:
    explicit QueenServer(const Config& config = Config::load());
    ~QueenServer();
    
    // Server lifecycle
    bool initialize();
    void setup_routes();
    bool start();
    void stop();
    
    // Configuration
    void set_port(int port) { config_.server.port = port; }
    void set_host(const std::string& host) { config_.server.host = host; }
    void set_dev_mode(bool dev_mode) { config_.server.dev_mode = dev_mode; }
    
    // Access to components (for testing)
    std::shared_ptr<QueueManager> get_queue_manager() const { return queue_manager_; }
    std::shared_ptr<DatabasePool> get_database_pool() const { return db_pool_; }
};

// Utility functions for HTTP handling
class HttpUtils {
public:
    static std::string url_decode(const std::string& encoded);
    static std::string get_content_type(const std::string& path);
    static bool is_valid_json(const std::string& json_str);
    static nlohmann::json parse_query_string(const std::string& query);
};

} // namespace queen
