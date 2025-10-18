#pragma once

#include "queen/queue_manager.hpp"
#include "queen/config.hpp"
#include <App.h>
#include <json.hpp>
#include <memory>
#include <string>
#include <functional>
#include <thread>
#include <vector>

namespace queen {

class QueenServer {
private:
    Config config_;
    std::atomic<bool> is_shutting_down_{false};
    std::vector<std::thread> worker_threads_;
    
    // Worker thread function - each runs isolated App with own resources
    static void worker_thread_main(Config config, int thread_id);
    
    // Request handling helpers
    void setup_cors_headers(uWS::HttpResponse<false>* res);
    void send_json_response(uWS::HttpResponse<false>* res, const nlohmann::json& json, int status_code = 200);
    void send_error_response(uWS::HttpResponse<false>* res, const std::string& error, int status_code = 500);
    
    // Helper to setup all routes on an App instance
    // This is called by each worker thread for its own App
    static void setup_app_routes(uWS::App& app, 
                                 std::shared_ptr<QueueManager> queue_manager,
                                 const Config& config);
    
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
