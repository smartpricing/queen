#pragma once

#include "queen/database.hpp"
#include <json.hpp>
#include <memory>
#include <string>
#include <vector>

namespace queen {

// Analytics and Status Manager for dashboard/frontend queries
class AnalyticsManager {
private:
    std::shared_ptr<DatabasePool> db_pool_;
    
public:
    explicit AnalyticsManager(std::shared_ptr<DatabasePool> db_pool);
    
    // Metrics
    nlohmann::json get_metrics(int total_messages = 0);
    
    // Resources
    nlohmann::json get_queues();
    nlohmann::json get_queue(const std::string& queue_name);
    nlohmann::json get_namespaces();
    nlohmann::json get_tasks();
    nlohmann::json get_system_overview();
    
    // Messages
    struct MessageFilters {
        std::string queue;
        std::string partition;
        std::string namespace_name;
        std::string task;
        std::string status;
        int limit = 50;
        int offset = 0;
    };
    nlohmann::json list_messages(const MessageFilters& filters);
    nlohmann::json get_message(const std::string& transaction_id);
    
    // Status & Dashboard
    struct StatusFilters {
        std::string from;
        std::string to;
        std::string queue;
        std::string namespace_name;
        std::string task;
    };
    nlohmann::json get_status(const StatusFilters& filters);
    nlohmann::json get_status_queues(const StatusFilters& filters, int limit = 100, int offset = 0);
    nlohmann::json get_queue_detail(const std::string& queue_name);
    nlohmann::json get_queue_messages(const std::string& queue_name, int limit = 50, int offset = 0);
    
    // Analytics
    struct AnalyticsFilters {
        std::string from;
        std::string to;
        std::string interval; // "minute", "hour", "day"
        std::string queue;
        std::string namespace_name;
        std::string task;
    };
    nlohmann::json get_analytics(const AnalyticsFilters& filters);
    
    // System Metrics
    struct SystemMetricsFilters {
        std::string from;
        std::string to;
        std::string hostname;
        std::string worker_id;
    };
    nlohmann::json get_system_metrics(const SystemMetricsFilters& filters);
    
private:
    // Helper methods
    std::string format_duration(double seconds);
    void get_time_range(const std::string& from_str, const std::string& to_str,
                       std::string& from_iso, std::string& to_iso, int default_hours = 1);
    std::string build_filter_clause(const std::string& queue, const std::string& ns, const std::string& task);
};

} // namespace queen

