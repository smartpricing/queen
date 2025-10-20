#include "queen/analytics_manager.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <set>

namespace queen {

// Safe conversion helpers - handle empty/null strings
static int safe_stoi(const std::string& str, int default_value = 0) {
    if (str.empty()) return default_value;
    try {
        return std::stoi(str);
    } catch (...) {
        return default_value;
    }
}

static double safe_stod(const std::string& str, double default_value = 0.0) {
    if (str.empty()) return default_value;
    try {
        return std::stod(str);
    } catch (...) {
        return default_value;
    }
}

AnalyticsManager::AnalyticsManager(std::shared_ptr<DatabasePool> db_pool)
    : db_pool_(db_pool) {
}

std::string AnalyticsManager::format_duration(double seconds) {
    if (seconds <= 0) return "0s";
    if (seconds < 60) return std::to_string((int)seconds) + "s";
    if (seconds < 3600) {
        int mins = (int)(seconds / 60);
        int secs = (int)seconds % 60;
        return std::to_string(mins) + "m " + std::to_string(secs) + "s";
    }
    if (seconds < 86400) {
        int hours = (int)(seconds / 3600);
        int mins = (int)((int)seconds % 3600) / 60;
        return std::to_string(hours) + "h " + std::to_string(mins) + "m";
    }
    int days = (int)(seconds / 86400);
    int hours = (int)((int)seconds % 86400) / 3600;
    return std::to_string(days) + "d " + std::to_string(hours) + "h";
}

void AnalyticsManager::get_time_range(const std::string& from_str, const std::string& to_str,
                                      std::string& from_iso, std::string& to_iso, int default_hours) {
    auto now = std::chrono::system_clock::now();
    auto default_ago = now - std::chrono::hours(default_hours);
    
    if (!to_str.empty()) {
        to_iso = to_str;
    } else {
        auto time_t_to = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t_to), "%Y-%m-%dT%H:%M:%S.000Z");
        to_iso = ss.str();
    }
    
    if (!from_str.empty()) {
        from_iso = from_str;
    } else {
        auto time_t_from = std::chrono::system_clock::to_time_t(default_ago);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t_from), "%Y-%m-%dT%H:%M:%S.000Z");
        from_iso = ss.str();
    }
}

std::string AnalyticsManager::build_filter_clause(const std::string& queue, const std::string& ns, const std::string& task) {
    std::string where = "";
    if (!queue.empty()) {
        where += " AND q.name = '" + queue + "'";
    }
    if (!ns.empty()) {
        where += " AND q.namespace = '" + ns + "'";
    }
    if (!task.empty()) {
        where += " AND q.task = '" + task + "'";
    }
    return where;
}

nlohmann::json AnalyticsManager::get_metrics(int total_messages) {
    try {
        nlohmann::json response = {
            {"uptime", 0},
            {"requests", {
                {"total", 0},
                {"rate", 0}
            }},
            {"messages", {
                {"total", total_messages},
                {"rate", 0}
            }},
            {"database", {
                {"poolSize", db_pool_->size()},
                {"idleConnections", db_pool_->available()},
                {"waitingRequests", 0}
            }},
            {"memory", {
                {"rss", 0},
                {"heapTotal", 0},
                {"heapUsed", 0},
                {"external", 0},
                {"arrayBuffers", 0}
            }},
            {"cpu", {
                {"user", 0},
                {"system", 0}
            }}
        };
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_metrics: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_queues() {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string query = R"(
            SELECT DISTINCT
                q.id as queue_id,
                q.name as queue_name,
                q.namespace,
                q.task,
                q.created_at as queue_created_at,
                COUNT(DISTINCT p.id) as partition_count,
                COUNT(DISTINCT m.id) as message_count,
                COALESCE(SUM(DISTINCT pc.pending_estimate), 0) as pending_count,
                COUNT(DISTINCT CASE WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN p.id END) as processing_count
            FROM queen.queues q
            LEFT JOIN queen.partitions p ON p.queue_id = q.id
            LEFT JOIN queen.messages m ON m.partition_id = p.id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
            WHERE 1=1
            GROUP BY q.id, q.name, q.namespace, q.task, q.created_at
            ORDER BY q.created_at DESC
        )";
        
        auto result = QueryResult(conn->exec(query));
        
        nlohmann::json queues = nlohmann::json::array();
        for (int i = 0; i < result.num_rows(); i++) {
            std::string ns_val = result.get_value(i, "namespace");
            std::string task_val = result.get_value(i, "task");
            queues.push_back({
                {"id", result.get_value(i, "queue_id")},
                {"name", result.get_value(i, "queue_name")},
                {"namespace", ns_val.empty() ? nullptr : nlohmann::json(ns_val)},
                {"task", task_val.empty() ? nullptr : nlohmann::json(task_val)},
                {"createdAt", result.get_value(i, "queue_created_at")},
                {"partitions", safe_stoi(result.get_value(i, "partition_count"))},
                {"messages", {
                    {"total", safe_stoi(result.get_value(i, "message_count"))},
                    {"pending", safe_stoi(result.get_value(i, "pending_count"))},
                    {"processing", safe_stoi(result.get_value(i, "processing_count"))}
                }}
            });
        }
        
        return {{"queues", queues}};
    } catch (const std::exception& e) {
        spdlog::error("Error in get_queues: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_queue(const std::string& queue_name) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Get queue info
        std::string queue_query = R"(
            SELECT 
                q.id,
                q.name,
                q.namespace,
                q.task,
                q.created_at
            FROM queen.queues q
            WHERE q.name = $1
        )";
        
        auto queue_result = QueryResult(conn->exec_params(queue_query, {queue_name}));
        
        if (queue_result.num_rows() == 0) {
            throw std::runtime_error("Queue not found");
        }
        
        std::string queue_id = queue_result.get_value(0, "id");
        
        // Get partitions with stats
        std::string partitions_query = R"(
            SELECT 
                p.id,
                p.name,
                p.created_at,
                COUNT(DISTINCT m.id) as message_count,
                COALESCE(pc.pending_estimate, 0) as pending,
                CASE WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 1 ELSE 0 END as processing,
                COALESCE(pc.total_messages_consumed, 0) as completed,
                0 as failed,
                (SELECT COUNT(*) FROM queen.dead_letter_queue dlq 
                 WHERE dlq.partition_id = p.id 
                 AND (dlq.consumer_group IS NULL OR dlq.consumer_group = '__QUEUE_MODE__')) as dead_letter,
                MIN(m.created_at) as oldest_message,
                MAX(m.created_at) as newest_message
            FROM queen.partitions p
            LEFT JOIN queen.messages m ON m.partition_id = p.id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
            WHERE p.queue_id = $1
            GROUP BY p.id, p.name, p.created_at, pc.pending_estimate, pc.lease_expires_at, pc.total_messages_consumed
            ORDER BY p.name
        )";
        
        auto partitions_result = QueryResult(conn->exec_params(partitions_query, {queue_id}));
        
        nlohmann::json partitions = nlohmann::json::array();
        int total_total = 0, total_pending = 0, total_processing = 0;
        int total_completed = 0, total_failed = 0, total_dead_letter = 0;
        
        for (int i = 0; i < partitions_result.num_rows(); i++) {
            int total = safe_stoi(partitions_result.get_value(i, "message_count"));
            int pending = safe_stoi(partitions_result.get_value(i, "pending"));
            int processing = safe_stoi(partitions_result.get_value(i, "processing"));
            int completed = safe_stoi(partitions_result.get_value(i, "completed"));
            int failed = safe_stoi(partitions_result.get_value(i, "failed"));
            int dead_letter = safe_stoi(partitions_result.get_value(i, "dead_letter"));
            
            total_total += total;
            total_pending += pending;
            total_processing += processing;
            total_completed += completed;
            total_failed += failed;
            total_dead_letter += dead_letter;
            
            std::string oldest_msg = partitions_result.get_value(i, "oldest_message");
            std::string newest_msg = partitions_result.get_value(i, "newest_message");
            
            partitions.push_back({
                {"id", partitions_result.get_value(i, "id")},
                {"name", partitions_result.get_value(i, "name")},
                {"createdAt", partitions_result.get_value(i, "created_at")},
                {"stats", {
                    {"total", total},
                    {"pending", pending},
                    {"processing", processing},
                    {"completed", completed},
                    {"failed", failed},
                    {"deadLetter", dead_letter}
                }},
                {"oldestMessage", oldest_msg.empty() ? nullptr : nlohmann::json(oldest_msg)},
                {"newestMessage", newest_msg.empty() ? nullptr : nlohmann::json(newest_msg)}
            });
        }
        
        nlohmann::json totals = {
            {"total", total_total},
            {"pending", total_pending},
            {"processing", total_processing},
            {"completed", total_completed},
            {"failed", total_failed},
            {"deadLetter", total_dead_letter}
        };
        
        nlohmann::json response = {
            {"id", queue_result.get_value(0, "id")},
            {"name", queue_result.get_value(0, "name")},
            {"namespace", queue_result.get_value(0, "namespace")},
            {"task", queue_result.get_value(0, "task")},
            {"createdAt", queue_result.get_value(0, "created_at")},
            {"partitions", partitions},
            {"totals", totals}
        };
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_queue: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_namespaces() {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string query = R"(
            SELECT DISTINCT
                q.namespace,
                COUNT(DISTINCT q.id) as queue_count,
                COUNT(DISTINCT p.id) as partition_count,
                COUNT(DISTINCT m.id) as message_count,
                COALESCE(SUM(pc.pending_estimate), 0) as pending_count
            FROM queen.queues q
            LEFT JOIN queen.partitions p ON p.queue_id = q.id
            LEFT JOIN queen.messages m ON m.partition_id = p.id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
            WHERE q.namespace IS NOT NULL
            GROUP BY q.namespace
            ORDER BY q.namespace
        )";
        
        auto result = QueryResult(conn->exec(query));
        
        nlohmann::json namespaces = nlohmann::json::array();
        for (int i = 0; i < result.num_rows(); i++) {
            namespaces.push_back({
                {"namespace", result.get_value(i, "namespace")},
                {"queues", safe_stoi(result.get_value(i, "queue_count"))},
                {"partitions", safe_stoi(result.get_value(i, "partition_count"))},
                {"messages", {
                    {"total", safe_stoi(result.get_value(i, "message_count"))},
                    {"pending", safe_stoi(result.get_value(i, "pending_count"))}
                }}
            });
        }
        
        return {{"namespaces", namespaces}};
    } catch (const std::exception& e) {
        spdlog::error("Error in get_namespaces: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_tasks() {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string query = R"(
            SELECT DISTINCT
                q.task,
                COUNT(DISTINCT q.id) as queue_count,
                COUNT(DISTINCT p.id) as partition_count,
                COUNT(DISTINCT m.id) as message_count,
                COALESCE(SUM(pc.pending_estimate), 0) as pending_count
            FROM queen.queues q
            LEFT JOIN queen.partitions p ON p.queue_id = q.id
            LEFT JOIN queen.messages m ON m.partition_id = p.id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
            WHERE q.task IS NOT NULL
            GROUP BY q.task
            ORDER BY q.task
        )";
        
        auto result = QueryResult(conn->exec(query));
        
        nlohmann::json tasks = nlohmann::json::array();
        for (int i = 0; i < result.num_rows(); i++) {
            tasks.push_back({
                {"task", result.get_value(i, "task")},
                {"queues", safe_stoi(result.get_value(i, "queue_count"))},
                {"partitions", safe_stoi(result.get_value(i, "partition_count"))},
                {"messages", {
                    {"total", safe_stoi(result.get_value(i, "message_count"))},
                    {"pending", safe_stoi(result.get_value(i, "pending_count"))}
                }}
            });
        }
        
        return {{"tasks", tasks}};
    } catch (const std::exception& e) {
        spdlog::error("Error in get_tasks: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_system_overview() {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string query = R"(
            SELECT
                (SELECT COUNT(*) FROM queen.queues) as total_queues,
                (SELECT COUNT(*) FROM queen.partitions) as total_partitions,
                (SELECT COUNT(*) FROM queen.messages) as total_messages,
                (SELECT COALESCE(SUM(pending_estimate), 0)::integer FROM queen.partition_consumers 
                 WHERE consumer_group = '__QUEUE_MODE__') as pending_messages,
                (SELECT COUNT(*) FROM queen.partition_consumers 
                 WHERE consumer_group = '__QUEUE_MODE__' 
                 AND lease_expires_at IS NOT NULL 
                 AND lease_expires_at > NOW()) as processing_messages,
                (SELECT COALESCE(SUM(total_messages_consumed), 0)::integer FROM queen.partition_consumers 
                 WHERE consumer_group = '__QUEUE_MODE__') as completed_messages,
                (SELECT COUNT(*) FROM queen.dead_letter_queue WHERE consumer_group IS NULL OR consumer_group = '__QUEUE_MODE__') as dead_letter_messages,
                (SELECT COUNT(DISTINCT namespace) FROM queen.queues WHERE namespace IS NOT NULL) as namespaces,
                (SELECT COUNT(DISTINCT task) FROM queen.queues WHERE task IS NOT NULL) as tasks
        )";
        
        auto result = QueryResult(conn->exec(query));
        
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%S.000Z");
        
        nlohmann::json response = {
            {"queues", safe_stoi(result.get_value(0, "total_queues"))},
            {"partitions", safe_stoi(result.get_value(0, "total_partitions"))},
            {"namespaces", safe_stoi(result.get_value(0, "namespaces"))},
            {"tasks", safe_stoi(result.get_value(0, "tasks"))},
            {"messages", {
                {"total", safe_stoi(result.get_value(0, "total_messages"))},
                {"pending", safe_stoi(result.get_value(0, "pending_messages"))},
                {"processing", safe_stoi(result.get_value(0, "processing_messages"))},
                {"completed", safe_stoi(result.get_value(0, "completed_messages"))},
                {"failed", 0},
                {"deadLetter", safe_stoi(result.get_value(0, "dead_letter_messages"))}
            }},
            {"timestamp", ss.str()}
        };
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_system_overview: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::list_messages(const MessageFilters& filters) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string query = R"(
            SELECT 
                m.id,
                m.transaction_id,
                m.payload,
                m.created_at,
                m.trace_id,
                q.name as queue_name,
                p.name as partition_name,
                q.namespace,
                q.task,
                q.priority as queue_priority,
                pc.lease_expires_at,
                CASE
                    WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
                    WHEN pc.last_consumed_created_at IS NOT NULL AND (
                        m.created_at < pc.last_consumed_created_at OR 
                        (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id)
                    ) THEN 'completed'
                    WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 'processing'
                    ELSE 'pending'
                END as message_status
            FROM queen.messages m
            JOIN queen.partitions p ON p.id = m.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
            LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
            WHERE 1=1
        )";
        
        std::vector<std::string> params;
        if (!filters.queue.empty()) {
            query += " AND q.name = $" + std::to_string(params.size() + 1);
            params.push_back(filters.queue);
        }
        if (!filters.partition.empty()) {
            query += " AND p.name = $" + std::to_string(params.size() + 1);
            params.push_back(filters.partition);
        }
        if (!filters.namespace_name.empty()) {
            query += " AND q.namespace = $" + std::to_string(params.size() + 1);
            params.push_back(filters.namespace_name);
        }
        if (!filters.task.empty()) {
            query += " AND q.task = $" + std::to_string(params.size() + 1);
            params.push_back(filters.task);
        }
        if (!filters.status.empty()) {
            // Add status filter with the same CASE logic
            query += R"( AND CASE
                WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
                WHEN pc.last_consumed_created_at IS NOT NULL AND (
                    m.created_at < pc.last_consumed_created_at OR 
                    (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id)
                ) THEN 'completed'
                WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 'processing'
                ELSE 'pending'
            END = $)" + std::to_string(params.size() + 1);
            params.push_back(filters.status);
        }
        
        query += " ORDER BY m.created_at DESC, m.id DESC";
        query += " LIMIT $" + std::to_string(params.size() + 1);
        params.push_back(std::to_string(filters.limit));
        query += " OFFSET $" + std::to_string(params.size() + 1);
        params.push_back(std::to_string(filters.offset));
        
        auto result = QueryResult(conn->exec_params(query, params));
        
        nlohmann::json messages = nlohmann::json::array();
        for (int i = 0; i < result.num_rows(); i++) {
            std::string trace_val = result.get_value(i, "trace_id");
            std::string lease_val = result.get_value(i, "lease_expires_at");
            std::string queue_path = result.get_value(i, "queue_name") + "/" + result.get_value(i, "partition_name");
            
            nlohmann::json msg = {
                {"id", result.get_value(i, "id")},
                {"transactionId", result.get_value(i, "transaction_id")},
                {"queuePath", queue_path},
                {"queue", result.get_value(i, "queue_name")},
                {"partition", result.get_value(i, "partition_name")},
                {"namespace", result.get_value(i, "namespace")},
                {"task", result.get_value(i, "task")},
                {"status", result.get_value(i, "message_status")},
                {"traceId", trace_val.empty() ? nullptr : nlohmann::json(trace_val)},
                {"queuePriority", safe_stoi(result.get_value(i, "queue_priority"))},
                {"createdAt", result.get_value(i, "created_at")},
                {"leaseExpiresAt", lease_val.empty() ? nullptr : nlohmann::json(lease_val)}
            };
            
            // Parse payload as JSON
            try {
                msg["payload"] = nlohmann::json::parse(result.get_value(i, "payload"));
            } catch (...) {
                msg["payload"] = result.get_value(i, "payload");
            }
            
            messages.push_back(msg);
        }
        
        return {{"messages", messages}};
    } catch (const std::exception& e) {
        spdlog::error("Error in list_messages: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_message(const std::string& transaction_id) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string query = R"(
            SELECT 
                m.id,
                m.transaction_id,
                m.partition_id,
                m.payload,
                m.created_at,
                m.trace_id,
                q.name as queue_name,
                p.name as partition_name,
                q.namespace,
                q.task,
                q.lease_time,
                q.retry_limit,
                q.retry_delay,
                q.ttl,
                q.priority as queue_priority,
                pc.lease_expires_at,
                pc.last_consumed_created_at,
                pc.last_consumed_id,
                dlq.error_message,
                dlq.retry_count,
                CASE
                    WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
                    WHEN pc.last_consumed_created_at IS NOT NULL AND (
                        m.created_at < pc.last_consumed_created_at OR 
                        (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id)
                    ) THEN 'completed'
                    WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 'processing'
                    ELSE 'pending'
                END as message_status
            FROM queen.messages m
            JOIN queen.partitions p ON p.id = m.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
            LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
            WHERE m.transaction_id = $1
        )";
        
        auto result = QueryResult(conn->exec_params(query, {transaction_id}));
        
        if (result.num_rows() == 0) {
            throw std::runtime_error("Message not found");
        }
        
        std::string trace_val = result.get_value(0, "trace_id");
        std::string lease_val = result.get_value(0, "lease_expires_at");
        std::string error_val = result.get_value(0, "error_message");
        std::string retry_val = result.get_value(0, "retry_count");
        std::string queue_path = result.get_value(0, "queue_name") + "/" + result.get_value(0, "partition_name");
        
        nlohmann::json response = {
            {"id", result.get_value(0, "id")},
            {"transactionId", result.get_value(0, "transaction_id")},
            {"queuePath", queue_path},
            {"queue", result.get_value(0, "queue_name")},
            {"partition", result.get_value(0, "partition_name")},
            {"namespace", result.get_value(0, "namespace")},
            {"task", result.get_value(0, "task")},
            {"status", result.get_value(0, "message_status")},
            {"traceId", trace_val.empty() ? nullptr : nlohmann::json(trace_val)},
            {"createdAt", result.get_value(0, "created_at")},
            {"errorMessage", error_val.empty() ? nullptr : nlohmann::json(error_val)},
            {"retryCount", retry_val.empty() ? nullptr : nlohmann::json(safe_stoi(retry_val))},
            {"leaseExpiresAt", lease_val.empty() ? nullptr : nlohmann::json(lease_val)},
            {"queueConfig", {
                {"leaseTime", safe_stoi(result.get_value(0, "lease_time"))},
                {"retryLimit", safe_stoi(result.get_value(0, "retry_limit"))},
                {"retryDelay", safe_stoi(result.get_value(0, "retry_delay"))},
                {"ttl", safe_stoi(result.get_value(0, "ttl"))},
                {"priority", safe_stoi(result.get_value(0, "queue_priority"))}
            }}
        };
        
        // Parse payload as JSON
        try {
            response["payload"] = nlohmann::json::parse(result.get_value(0, "payload"));
        } catch (...) {
            response["payload"] = result.get_value(0, "payload");
        }
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_message: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_status(const StatusFilters& filters) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string from_iso, to_iso;
        get_time_range(filters.from, filters.to, from_iso, to_iso, 1);
        
        std::string filter_clause = build_filter_clause(filters.queue, filters.namespace_name, filters.task);
        
        // Query 1: Throughput per minute
        std::string throughput_query = R"(
            WITH time_series AS (
                SELECT generate_series(
                    DATE_TRUNC('minute', $1::timestamptz),
                    DATE_TRUNC('minute', $2::timestamptz),
                    '1 minute'::interval
                ) AS minute
            ),
            ingested_per_minute AS (
                SELECT 
                    DATE_TRUNC('minute', m.created_at) as minute,
                    COUNT(DISTINCT m.id) as messages_ingested
                FROM queen.messages m
                JOIN queen.partitions p ON p.id = m.partition_id
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE m.created_at >= $1::timestamptz
                    AND m.created_at <= $2::timestamptz )" + filter_clause + R"(
                GROUP BY DATE_TRUNC('minute', m.created_at)
            ),
            processed_per_minute AS (
                SELECT 
                    DATE_TRUNC('minute', mc.acked_at) as minute,
                    SUM(mc.messages_completed) as messages_processed
                FROM queen.messages_consumed mc
                JOIN queen.partitions p ON p.id = mc.partition_id
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE mc.acked_at >= $1::timestamptz
                    AND mc.acked_at <= $2::timestamptz )" + filter_clause + R"(
                GROUP BY DATE_TRUNC('minute', mc.acked_at)
            )
            SELECT 
                ts.minute,
                COALESCE(ipm.messages_ingested, 0) as messages_ingested,
                COALESCE(ppm.messages_processed, 0) as messages_processed
            FROM time_series ts
            LEFT JOIN ingested_per_minute ipm ON ipm.minute = ts.minute
            LEFT JOIN processed_per_minute ppm ON ppm.minute = ts.minute
            ORDER BY ts.minute DESC
        )";
        
        auto throughput_result = QueryResult(conn->exec_params(throughput_query, {from_iso, to_iso}));
        
        nlohmann::json throughput = nlohmann::json::array();
        for (int i = 0; i < throughput_result.num_rows(); i++) {
            int ingested = safe_stoi(throughput_result.get_value(i, "messages_ingested"));
            int processed = safe_stoi(throughput_result.get_value(i, "messages_processed"));
            throughput.push_back({
                {"timestamp", throughput_result.get_value(i, "minute")},
                {"ingested", ingested},
                {"processed", processed},
                {"ingestedPerSecond", std::round((ingested / 60.0) * 100) / 100},
                {"processedPerSecond", std::round((processed / 60.0) * 100) / 100}
            });
        }
        
        // Query 2: Active queues in time range
        std::string queues_query = R"(
            SELECT 
                q.id, q.name, q.namespace, q.task,
                COUNT(DISTINCT p.id) as partition_count,
                COALESCE(SUM(pc.total_messages_consumed), 0) as total_consumed
            FROM queen.queues q
            LEFT JOIN queen.partitions p ON p.queue_id = q.id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                AND pc.consumer_group = '__QUEUE_MODE__'
            LEFT JOIN queen.messages m ON m.partition_id = p.id
                AND m.created_at >= $1 AND m.created_at <= $2
            WHERE 1=1 )" + filter_clause + R"(
            GROUP BY q.id, q.name, q.namespace, q.task
            HAVING COUNT(DISTINCT m.id) > 0
            ORDER BY q.name
        )";
        
        auto queues_result = QueryResult(conn->exec_params(queues_query, {from_iso, to_iso}));
        
        nlohmann::json queues = nlohmann::json::array();
        for (int i = 0; i < queues_result.num_rows(); i++) {
            std::string ns_val = queues_result.get_value(i, "namespace");
            std::string task_val = queues_result.get_value(i, "task");
            queues.push_back({
                {"id", queues_result.get_value(i, "id")},
                {"name", queues_result.get_value(i, "name")},
                {"namespace", ns_val.empty() ? nullptr : nlohmann::json(ns_val)},
                {"task", task_val.empty() ? nullptr : nlohmann::json(task_val)},
                {"partitions", safe_stoi(queues_result.get_value(i, "partition_count"))},
                {"totalConsumed", safe_stoi(queues_result.get_value(i, "total_consumed"))}
            });
        }
        
        // Query 3: Message counts
        std::string counts_query = R"(
            SELECT 
                COUNT(DISTINCT m.id) as total_messages,
                (SELECT COALESCE(SUM(pending_estimate), 0)::integer 
                 FROM queen.partition_consumers 
                 WHERE consumer_group = '__QUEUE_MODE__') as pending,
                (SELECT COUNT(*) 
                 FROM queen.partition_consumers 
                 WHERE consumer_group = '__QUEUE_MODE__' 
                 AND lease_expires_at IS NOT NULL 
                 AND lease_expires_at > NOW()) as processing,
                (SELECT COALESCE(SUM(total_messages_consumed), 0)::integer 
                 FROM queen.partition_consumers 
                 WHERE consumer_group = '__QUEUE_MODE__') as completed,
                (SELECT COUNT(*) 
                 FROM queen.dead_letter_queue 
                 WHERE consumer_group IS NULL OR consumer_group = '__QUEUE_MODE__') as dead_letter
            FROM queen.messages m
            LEFT JOIN queen.partitions p ON p.id = m.partition_id
            LEFT JOIN queen.queues q ON q.id = p.queue_id
            WHERE m.created_at >= $1 AND m.created_at <= $2 )" + filter_clause;
        
        auto counts_result = QueryResult(conn->exec_params(counts_query, {from_iso, to_iso}));
        
        nlohmann::json messages = {
            {"total", safe_stoi(counts_result.get_value(0, "total_messages"))},
            {"pending", safe_stoi(counts_result.get_value(0, "pending"))},
            {"processing", safe_stoi(counts_result.get_value(0, "processing"))},
            {"completed", safe_stoi(counts_result.get_value(0, "completed"))},
            {"failed", 0},
            {"deadLetter", safe_stoi(counts_result.get_value(0, "dead_letter"))}
        };
        
        // Query 4: Active leases
        std::string leases_query = R"(
            SELECT 
                COUNT(*) as active_leases,
                COUNT(DISTINCT partition_id) as partitions_with_leases,
                SUM(batch_size) as total_batch_size,
                SUM(acked_count) as total_acked
            FROM queen.partition_consumers
            WHERE lease_expires_at IS NOT NULL AND lease_expires_at > NOW()
        )";
        
        auto leases_result = QueryResult(conn->exec(leases_query));
        
        nlohmann::json leases = {
            {"active", safe_stoi(leases_result.get_value(0, "active_leases"))},
            {"partitionsWithLeases", safe_stoi(leases_result.get_value(0, "partitions_with_leases"))},
            {"totalBatchSize", safe_stoi(leases_result.get_value(0, "total_batch_size"))},
            {"totalAcked", safe_stoi(leases_result.get_value(0, "total_acked"))}
        };
        
        // Query 5: DLQ stats
        std::string dlq_query = R"(
            SELECT 
                COUNT(*) as total_dlq_messages,
                COUNT(DISTINCT partition_id) as affected_partitions,
                error_message,
                COUNT(*) as error_count
            FROM queen.dead_letter_queue dlq
            LEFT JOIN queen.partitions p ON p.id = dlq.partition_id
            LEFT JOIN queen.queues q ON q.id = p.queue_id
            WHERE dlq.failed_at >= $1 AND dlq.failed_at <= $2 )" + filter_clause + R"(
            GROUP BY error_message
            ORDER BY error_count DESC
            LIMIT 5
        )";
        
        auto dlq_result = QueryResult(conn->exec_params(dlq_query, {from_iso, to_iso}));
        
        int dlq_total = 0;
        std::set<std::string> dlq_partitions;
        nlohmann::json top_errors = nlohmann::json::array();
        
        for (int i = 0; i < dlq_result.num_rows(); i++) {
            int error_count = safe_stoi(dlq_result.get_value(i, "error_count"));
            dlq_total += error_count;
            std::string partition_id = dlq_result.get_value(i, "partition_id");
            if (!partition_id.empty()) {
                dlq_partitions.insert(partition_id);
            }
            
            std::string error_msg = dlq_result.get_value(i, "error_message");
            if (!error_msg.empty()) {
                top_errors.push_back({
                    {"error", error_msg},
                    {"count", error_count}
                });
            }
        }
        
        nlohmann::json response = {
            {"timeRange", {
                {"from", from_iso},
                {"to", to_iso}
            }},
            {"throughput", throughput},
            {"queues", queues},
            {"messages", messages},
            {"leases", leases},
            {"deadLetterQueue", {
                {"totalMessages", dlq_total}, 
                {"affectedPartitions", (int)dlq_partitions.size()}, 
                {"topErrors", top_errors}
            }}
        };
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_status: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_status_queues(const StatusFilters& filters, int limit, int offset) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string from_iso, to_iso;
        get_time_range(filters.from, filters.to, from_iso, to_iso, 1);
        
        std::string filter_clause = build_filter_clause("", filters.namespace_name, filters.task);
        
        std::string query = R"(
            SELECT 
                q.id, q.name, q.namespace, q.task, q.priority, q.created_at,
                COUNT(DISTINCT p.id) as partition_count,
                (SELECT COUNT(DISTINCT m2.id)
                 FROM queen.messages m2
                 JOIN queen.partitions p2 ON p2.id = m2.partition_id
                 WHERE p2.queue_id = q.id) as total_messages,
                COUNT(DISTINCT CASE WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN p.id END) as processing,
                COALESCE(SUM(DISTINCT pc.total_messages_consumed), 0) as completed
            FROM queen.queues q
            LEFT JOIN queen.partitions p ON p.queue_id = q.id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                AND pc.consumer_group = '__QUEUE_MODE__'
            WHERE 1=1 )" + filter_clause + R"(
            GROUP BY q.id, q.name, q.namespace, q.task, q.priority, q.created_at
            ORDER BY q.priority DESC, q.name
            LIMIT $1 OFFSET $2
        )";
        
        auto result = QueryResult(conn->exec_params(query, {std::to_string(limit), std::to_string(offset)}));
        
        nlohmann::json queues = nlohmann::json::array();
        for (int i = 0; i < result.num_rows(); i++) {
            int total = safe_stoi(result.get_value(i, "total_messages"));
            int completed = safe_stoi(result.get_value(i, "completed"));
            int processing = safe_stoi(result.get_value(i, "processing"));
            int pending = std::max(0, total - completed - processing);
            
            queues.push_back({
                {"id", result.get_value(i, "id")},
                {"name", result.get_value(i, "name")},
                {"namespace", result.get_value(i, "namespace")},
                {"task", result.get_value(i, "task")},
                {"priority", safe_stoi(result.get_value(i, "priority"))},
                {"createdAt", result.get_value(i, "created_at")},
                {"partitions", safe_stoi(result.get_value(i, "partition_count"))},
                {"messages", {
                    {"total", total},
                    {"pending", pending},
                    {"processing", processing},
                    {"completed", completed},
                    {"failed", 0},
                    {"deadLetter", 0}
                }}
            });
        }
        
        nlohmann::json response = {
            {"queues", queues},
            {"pagination", {
                {"limit", limit},
                {"offset", offset},
                {"total", queues.size()}
            }},
            {"timeRange", {
                {"from", from_iso}, {"to", to_iso}
            }}
        };
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_status_queues: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_queue_detail(const std::string& queue_name) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string query = R"(
            SELECT 
                q.id, q.name, q.namespace, q.task, q.priority,
                q.lease_time, q.retry_limit, q.ttl, q.max_queue_size, q.created_at,
                p.id as partition_id, p.name as partition_name,
                p.created_at as partition_created_at,
                COUNT(DISTINCT m.id) as partition_total_messages,
                CASE WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 1 ELSE 0 END as partition_processing,
                COALESCE(pc.total_messages_consumed, 0) as partition_completed,
                pc.total_messages_consumed, pc.total_batches_consumed,
                pc.last_consumed_at
            FROM queen.queues q
            LEFT JOIN queen.partitions p ON p.queue_id = q.id
            LEFT JOIN queen.messages m ON m.partition_id = p.id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
                AND pc.consumer_group = '__QUEUE_MODE__'
            WHERE q.name = $1
            GROUP BY q.id, q.name, q.namespace, q.task, q.priority, q.lease_time, 
                     q.retry_limit, q.ttl, q.max_queue_size, q.created_at,
                     p.id, p.name, p.created_at,
                     pc.total_messages_consumed, pc.total_batches_consumed, pc.last_consumed_at,
                     pc.lease_expires_at
            ORDER BY p.name
        )";
        
        auto result = QueryResult(conn->exec_params(query, {queue_name}));
        
        if (result.num_rows() == 0) {
            throw std::runtime_error("Queue not found");
        }
        
        // Build queue info from first row
        nlohmann::json queue_info = {
            {"id", result.get_value(0, "id")},
            {"name", result.get_value(0, "name")},
            {"namespace", result.get_value(0, "namespace")},
            {"task", result.get_value(0, "task")},
            {"priority", safe_stoi(result.get_value(0, "priority"))},
            {"config", {
                {"leaseTime", safe_stoi(result.get_value(0, "lease_time"))},
                {"retryLimit", safe_stoi(result.get_value(0, "retry_limit"))},
                {"ttl", safe_stoi(result.get_value(0, "ttl"))},
                {"maxQueueSize", safe_stoi(result.get_value(0, "max_queue_size"))}
            }},
            {"createdAt", result.get_value(0, "created_at")}
        };
        
        // Aggregate totals
        int total_msgs = 0, total_pending = 0, total_processing = 0, total_completed = 0;
        int total_consumed = 0, total_batches = 0;
        
        nlohmann::json partitions = nlohmann::json::array();
        for (int i = 0; i < result.num_rows(); i++) {
            std::string partition_id = result.get_value(i, "partition_id");
            if (partition_id.empty()) continue;
            
            int total = safe_stoi(result.get_value(i, "partition_total_messages"));
            int completed = safe_stoi(result.get_value(i, "partition_completed"));
            int processing = safe_stoi(result.get_value(i, "partition_processing"));
            int pending = std::max(0, total - completed - processing);
            
            total_msgs += total;
            total_pending += pending;
            total_processing += processing;
            total_completed += completed;
            total_consumed += safe_stoi(result.get_value(i, "total_messages_consumed"));
            total_batches += safe_stoi(result.get_value(i, "total_batches_consumed"));
            
            nlohmann::json partition = {
                {"id", partition_id},
                {"name", result.get_value(i, "partition_name")},
                {"createdAt", result.get_value(i, "partition_created_at")},
                {"messages", {
                    {"total", total},
                    {"pending", pending},
                    {"processing", processing},
                    {"completed", completed},
                    {"failed", 0}
                }},
                {"cursor", {
                    {"totalConsumed", safe_stoi(result.get_value(i, "total_messages_consumed"))},
                    {"batchesConsumed", safe_stoi(result.get_value(i, "total_batches_consumed"))},
                    {"lastConsumedAt", result.get_value(i, "last_consumed_at")}
                }}
            };
            
            partitions.push_back(partition);
        }
        
        nlohmann::json totals = {
            {"messages", {
                {"total", total_msgs},
                {"pending", total_pending},
                {"processing", total_processing},
                {"completed", total_completed},
                {"failed", 0}
            }},
            {"partitions", partitions.size()},
            {"consumed", total_consumed},
            {"batches", total_batches}
        };
        
        nlohmann::json response = {
            {"queue", queue_info},
            {"totals", totals},
            {"partitions", partitions},
            {"timeRange", {{"from", ""}, {"to", ""}}}
        };
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_queue_detail: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_queue_messages(const std::string& queue_name, int limit, int offset) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string from_iso, to_iso;
        get_time_range("", "", from_iso, to_iso, 1);
        
        std::string query = R"(
            SELECT 
                m.id, m.transaction_id, m.trace_id, m.created_at,
                m.is_encrypted, m.payload, p.name as partition,
                CASE 
                    WHEN EXISTS (SELECT 1 FROM queen.dead_letter_queue dlq WHERE dlq.message_id = m.id) THEN 'failed'
                    WHEN (m.created_at < COALESCE(pc.last_consumed_created_at, '1970-01-01'::timestamptz) 
                          OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', COALESCE(pc.last_consumed_created_at, '1970-01-01'::timestamptz)) 
                              AND m.id <= COALESCE(pc.last_consumed_id, '00000000-0000-0000-0000-000000000000'::uuid))) THEN 'completed'
                    WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN 'processing'
                    ELSE 'pending'
                END as status,
                EXTRACT(EPOCH FROM (NOW() - m.created_at)) as age_seconds
            FROM queen.messages m
            JOIN queen.partitions p ON p.id = m.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
                AND pc.consumer_group = '__QUEUE_MODE__'
            WHERE q.name = $1
                AND m.created_at >= $2 AND m.created_at <= $3
            ORDER BY m.created_at DESC
            LIMIT $4 OFFSET $5
        )";
        
        auto result = QueryResult(conn->exec_params(query, {
            queue_name, from_iso, to_iso, 
            std::to_string(limit), std::to_string(offset)
        }));
        
        nlohmann::json messages = nlohmann::json::array();
        for (int i = 0; i < result.num_rows(); i++) {
            std::string trace_val = result.get_value(i, "trace_id");
            nlohmann::json msg = {
                {"id", result.get_value(i, "id")},
                {"transactionId", result.get_value(i, "transaction_id")},
                {"traceId", trace_val.empty() ? nullptr : nlohmann::json(trace_val)},
                {"partition", result.get_value(i, "partition")},
                {"createdAt", result.get_value(i, "created_at")},
                {"status", result.get_value(i, "status")},
                {"retryCount", 0},
                {"isEncrypted", result.get_value(i, "is_encrypted") == "t"}
            };
            
            if (result.get_value(i, "is_encrypted") != "t") {
                try {
                    msg["data"] = nlohmann::json::parse(result.get_value(i, "payload"));
                } catch (...) {
                    msg["data"] = result.get_value(i, "payload");
                }
            }
            
            std::string status = result.get_value(i, "status");
            if (status == "pending" || status == "processing") {
                std::string age_str = result.get_value(i, "age_seconds");
                if (!age_str.empty()) {
                    double age = safe_stod(age_str);
                    msg["age"] = {
                        {"seconds", age},
                        {"formatted", format_duration(age)}
                    };
                }
            }
            
            messages.push_back(msg);
        }
        
        nlohmann::json response = {
            {"messages", messages},
            {"pagination", {{"limit", limit}, {"offset", offset}, {"total", messages.size()}}},
            {"queue", queue_name},
            {"filters", {{"status", nullptr}, {"partition", nullptr}}},
            {"timeRange", {{"from", from_iso}, {"to", to_iso}}}
        };
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_queue_messages: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_analytics(const AnalyticsFilters& filters) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string from_iso, to_iso;
        get_time_range(filters.from, filters.to, from_iso, to_iso, 24);
        
        std::string filter_clause = build_filter_clause(filters.queue, filters.namespace_name, filters.task);
        std::string interval = filters.interval.empty() ? "hour" : filters.interval;
        std::string sql_interval = interval == "minute" ? "1 minute" : (interval == "day" ? "1 day" : "1 hour");
        
        // Query for throughput time series
        std::string throughput_query = R"(
            WITH time_series AS (
                SELECT generate_series(
                    DATE_TRUNC(')" + interval + R"(', $1::timestamptz),
                    DATE_TRUNC(')" + interval + R"(', $2::timestamptz),
                    ')" + sql_interval + R"('::interval
                ) AS bucket
            ),
            ingested_counts AS (
                SELECT 
                    DATE_TRUNC(')" + interval + R"(', m.created_at) as bucket,
                    COUNT(DISTINCT m.id) as ingested
                FROM queen.messages m
                JOIN queen.partitions p ON p.id = m.partition_id
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE m.created_at >= $1 AND m.created_at <= $2 )" + filter_clause + R"(
                GROUP BY DATE_TRUNC(')" + interval + R"(', m.created_at)
            ),
            processed_counts AS (
                SELECT 
                    DATE_TRUNC(')" + interval + R"(', mc.acked_at) as bucket,
                    SUM(mc.messages_completed) as processed
                FROM queen.messages_consumed mc
                JOIN queen.partitions p ON p.id = mc.partition_id
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE mc.acked_at >= $1::timestamptz AND mc.acked_at <= $2::timestamptz )" + filter_clause + R"(
                GROUP BY DATE_TRUNC(')" + interval + R"(', mc.acked_at)
            )
            SELECT 
                ts.bucket as timestamp,
                COALESCE(ic.ingested, 0) as ingested,
                COALESCE(pc.processed, 0) as processed,
                0 as failed
            FROM time_series ts
            LEFT JOIN ingested_counts ic ON ic.bucket = ts.bucket
            LEFT JOIN processed_counts pc ON pc.bucket = ts.bucket
            ORDER BY ts.bucket DESC
            LIMIT 100
        )";
        
        auto throughput_result = QueryResult(conn->exec_params(throughput_query, {from_iso, to_iso}));
        
        int seconds_per_bucket = interval == "minute" ? 60 : (interval == "day" ? 86400 : 3600);
        
        nlohmann::json throughput_series = nlohmann::json::array();
        int total_ingested = 0, total_processed = 0, total_failed = 0;
        
        for (int i = 0; i < throughput_result.num_rows(); i++) {
            int ingested = safe_stoi(throughput_result.get_value(i, "ingested"));
            int processed = safe_stoi(throughput_result.get_value(i, "processed"));
            int failed = safe_stoi(throughput_result.get_value(i, "failed"));
            
            total_ingested += ingested;
            total_processed += processed;
            total_failed += failed;
            
            throughput_series.push_back({
                {"timestamp", throughput_result.get_value(i, "timestamp")},
                {"ingested", ingested},
                {"processed", processed},
                {"failed", failed},
                {"ingestedPerSecond", std::round((ingested / (double)seconds_per_bucket) * 100) / 100},
                {"processedPerSecond", std::round((processed / (double)seconds_per_bucket) * 100) / 100}
            });
        }
        
        // Get current queue depths
        std::string depth_query = R"(
            SELECT 
                COALESCE(SUM(pc.pending_estimate), 0) as pending,
                COALESCE(SUM(CASE WHEN pc.lease_expires_at IS NOT NULL AND pc.lease_expires_at > NOW() THEN pc.batch_size ELSE 0 END), 0) as processing
            FROM queen.partition_consumers pc
            WHERE pc.consumer_group = '__QUEUE_MODE__'
        )";
        
        auto depth_result = QueryResult(conn->exec(depth_query));
        
        nlohmann::json response = {
            {"timeRange", {
                {"from", from_iso},
                {"to", to_iso},
                {"interval", interval}
            }},
            {"throughput", {
                {"timeSeries", throughput_series},
                {"totals", {
                    {"ingested", total_ingested},
                    {"processed", total_processed},
                    {"failed", total_failed},
                    {"avgIngestedPerSecond", 0},
                    {"avgProcessedPerSecond", 0}
                }}
            }},
            {"latency", {
                {"timeSeries", nlohmann::json::array()},
                {"overall", nullptr}
            }},
            {"errorRates", {
                {"timeSeries", nlohmann::json::array()},
                {"overall", {{"failed", 0}, {"processed", total_processed}, {"rate", 0}, {"ratePercent", "0.00%"}}}
            }},
            {"queueDepths", {
                {"timeSeries", nlohmann::json::array()},
                {"current", {
                    {"pending", safe_stoi(depth_result.get_value(0, "pending"))},
                    {"processing", safe_stoi(depth_result.get_value(0, "processing"))}
                }}
            }},
            {"topQueues", nlohmann::json::array()},
            {"deadLetterQueue", {
                {"timeSeries", nlohmann::json::array()},
                {"total", 0},
                {"topErrors", nlohmann::json::array()}
            }}
        };
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_analytics: {}", e.what());
        throw;
    }
}

} // namespace queen

