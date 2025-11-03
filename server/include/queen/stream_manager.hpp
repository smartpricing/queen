#pragma once

#include "queen/database.hpp"
#include "queen/response_queue.hpp"
#include "queen/stream_poll_intention_registry.hpp"
#include "threadpool.hpp"
#include <json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>

// Forward declaration to avoid including full uWebSockets headers
namespace uWS {
    template <bool SSL>
    struct HttpResponse;
    struct HttpRequest;
}

namespace queen {

/**
 * Represents a stream definition
 */
struct StreamDefinition {
    std::string id;
    std::string name;
    std::string namespace_name;
    bool partitioned;
    std::string window_type;
    int64_t window_duration_ms;
    int window_size_count;
    int64_t window_slide_ms;
    int window_slide_count;
    int64_t window_grace_period_ms;
    int64_t window_lease_timeout_ms;
};

/**
 * Represents a partition or global stream key with its offset
 */
struct StreamPartitionOffset {
    std::string stream_key;        // partition_id::TEXT or '__GLOBAL__'
    std::string partition_name;    // Partition name for display
    std::string last_acked_window_end;  // Timestamp string or '-infinity'
};

/**
 * Manages streaming windows over message queues
 */
class StreamManager {
public:
    StreamManager(
        std::shared_ptr<DatabasePool> db_pool,
        std::shared_ptr<astp::ThreadPool> db_thread_pool,
        const std::vector<std::shared_ptr<ResponseQueue>>& worker_response_queues,
        std::shared_ptr<StreamPollIntentionRegistry> intention_registry,
        std::shared_ptr<ResponseRegistry> response_registry
    );
    
    ~StreamManager();
    
    // HTTP Handlers (called from uWebSockets event loop)
    void handle_define(uWS::HttpResponse<false>* res, uWS::HttpRequest* req, int worker_id);
    void handle_poll(uWS::HttpResponse<false>* res, uWS::HttpRequest* req, int worker_id);
    void handle_ack(uWS::HttpResponse<false>* res, uWS::HttpRequest* req, int worker_id);
    void handle_renew(uWS::HttpResponse<false>* res, uWS::HttpRequest* req, int worker_id);
    void handle_seek(uWS::HttpResponse<false>* res, uWS::HttpRequest* req, int worker_id);
    
    // Poll worker support
    bool check_and_deliver_window_for_poll(
        const std::string& stream_name,
        const std::string& consumer_group,
        const std::vector<StreamPollIntention>& intentions,
        std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues
    );
    
private:
    std::shared_ptr<DatabasePool> db_pool_;
    std::shared_ptr<astp::ThreadPool> db_thread_pool_;
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues_;
    std::shared_ptr<StreamPollIntentionRegistry> stream_intention_registry_;
    std::shared_ptr<ResponseRegistry> response_registry_;
    
    // Database query helpers
    std::optional<StreamDefinition> get_stream(DatabaseConnection* conn, const std::string& stream_name);
    std::vector<StreamPartitionOffset> get_partitions_and_offsets(
        DatabaseConnection* conn, 
        const std::string& stream_id, 
        const std::string& consumer_group,
        bool partitioned
    );
    std::string get_watermark(DatabaseConnection* conn, const std::string& stream_id);
    bool check_lease_exists(
        DatabaseConnection* conn,
        const std::string& stream_id,
        const std::string& consumer_group,
        const std::string& stream_key,
        const std::string& window_start
    );
    std::string create_lease(
        DatabaseConnection* conn,
        const std::string& stream_id,
        const std::string& consumer_group,
        const std::string& stream_key,
        const std::string& window_start,
        const std::string& window_end,
        int64_t lease_timeout_ms
    );
    nlohmann::json get_messages(
        DatabaseConnection* conn,
        const std::string& stream_id,
        const std::string& stream_key,
        bool partitioned,
        const std::string& window_start,
        const std::string& window_end
    );
    std::optional<std::string> get_first_message_time(
        DatabaseConnection* conn,
        const std::string& stream_id,
        const std::string& stream_key,
        bool partitioned
    );
    
    // Utility methods
    std::string align_to_boundary(const std::string& timestamp, int64_t duration_ms);
    std::string format_timestamp(const std::chrono::system_clock::time_point& tp);
    std::chrono::system_clock::time_point parse_timestamp(const std::string& ts_str);
    std::string generate_window_id(
        const std::string& stream_id,
        const std::string& partition_name,
        const std::string& window_start,
        const std::string& window_end
    );
    void send_json_response(uWS::HttpResponse<false>* res, const nlohmann::json& data, int status_code = 200);
    void send_error_response(uWS::HttpResponse<false>* res, const std::string& error, int status_code = 400);
    void read_json_body(
        uWS::HttpResponse<false>* res,
        std::function<void(const nlohmann::json&)> on_success,
        std::function<void(const std::string&)> on_error
    );
};

} // namespace queen

