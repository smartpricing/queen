#include "queen/analytics_manager.hpp"
#include "queen/encryption.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <set>

namespace queen {

// Safe conversion helpers - handle JSON values (numbers, strings, nulls)
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

// Helper: Get int from JSON value (handles numbers, strings, nulls)
static int json_to_int(const nlohmann::json& val, int default_value = 0) {
    if (val.is_null()) return default_value;
    if (val.is_number_integer()) return val.get<int>();
    if (val.is_number()) return static_cast<int>(val.get<double>());
    if (val.is_string()) return safe_stoi(val.get<std::string>(), default_value);
    return default_value;
}

// Helper: Get string from JSON value (handles strings, numbers, nulls, booleans)
static std::string json_to_string(const nlohmann::json& val, const std::string& default_value = "") {
    if (val.is_null()) return default_value;
    if (val.is_string()) return val.get<std::string>();
    if (val.is_number()) return std::to_string(val.get<double>());
    if (val.is_boolean()) return val.get<bool>() ? "true" : "false";
    return default_value;
}

AnalyticsManager::AnalyticsManager(std::shared_ptr<AsyncDbPool> async_db_pool)
    : async_db_pool_(async_db_pool) {
}

// Helper: Execute async query and return JSON result
static nlohmann::json exec_query_json(PGconn* conn, const std::string& sql) {
    // Send query
    if (!PQsendQuery(conn, sql.c_str())) {
        throw std::runtime_error("PQsendQuery failed: " + std::string(PQerrorMessage(conn)));
    }
    
    // Wait for query to complete
    while (PQisBusy(conn)) {
        if (!PQconsumeInput(conn)) {
            throw std::runtime_error("PQconsumeInput failed: " + std::string(PQerrorMessage(conn)));
        }
    }
    
    // Get result
    PGresult* res = PQgetResult(conn);
    if (!res) {
        throw std::runtime_error("No result returned");
    }
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }
    
    // Convert to JSON
    nlohmann::json json_array = nlohmann::json::array();
    int num_rows = PQntuples(res);
    int num_fields = PQnfields(res);
    
    for (int row = 0; row < num_rows; ++row) {
        nlohmann::json json_row = nlohmann::json::object();
        
        for (int col = 0; col < num_fields; ++col) {
            const char* field_name = PQfname(res, col);
            if (!field_name) continue;
            
            if (PQgetisnull(res, row, col)) {
                json_row[field_name] = nullptr;
            } else {
                std::string value = PQgetvalue(res, row, col);
                Oid type_oid = PQftype(res, col);
                
                switch (type_oid) {
                    case 16: // bool
                        json_row[field_name] = (value == "t" || value == "true");
                        break;
                    case 20: // int8 (bigint)
                    case 21: // int2 (smallint)  
                    case 23: // int4 (integer)
                        try {
                            json_row[field_name] = std::stoll(value);
                        } catch (...) {
                            json_row[field_name] = value;
                        }
                        break;
                    case 700: // float4
                    case 701: // float8
                    case 1700: // numeric
                        try {
                            json_row[field_name] = std::stod(value);
                        } catch (...) {
                            json_row[field_name] = value;
                        }
                        break;
                    case 114: // json
                    case 3802: // jsonb
                        try {
                            json_row[field_name] = nlohmann::json::parse(value);
                        } catch (...) {
                            json_row[field_name] = value;
                        }
                        break;
                    default:
                        json_row[field_name] = value;
                        break;
                }
            }
        }
        
        json_array.push_back(json_row);
    }
    
    PQclear(res);
    
    // Consume any remaining results
    PGresult* extra;
    while ((extra = PQgetResult(conn)) != nullptr) {
        PQclear(extra);
    }
    
    return json_array;
}

// Helper: Execute async parameterized query and return JSON result
static nlohmann::json exec_query_params_json(PGconn* conn, const std::string& sql, const std::vector<std::string>& params) {
    // Prepare parameters
    std::vector<const char*> param_values;
    param_values.reserve(params.size());
    for (const auto& param : params) {
        param_values.push_back(param.c_str());
    }
    
    // Send query
    if (!PQsendQueryParams(conn, sql.c_str(), static_cast<int>(params.size()),
                          nullptr, param_values.data(), nullptr, nullptr, 0)) {
        throw std::runtime_error("PQsendQueryParams failed: " + std::string(PQerrorMessage(conn)));
    }
    
    // Flush
    while (true) {
        int flush_result = PQflush(conn);
        if (flush_result == 0) break;
        if (flush_result == -1) {
            throw std::runtime_error("PQflush failed: " + std::string(PQerrorMessage(conn)));
        }
    }
    
    // Wait for query to complete
    while (PQisBusy(conn)) {
        if (!PQconsumeInput(conn)) {
            throw std::runtime_error("PQconsumeInput failed: " + std::string(PQerrorMessage(conn)));
        }
    }
    
    // Get result
    PGresult* res = PQgetResult(conn);
    if (!res) {
        throw std::runtime_error("No result returned");
    }
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }
    
    // Convert to JSON (same as exec_query_json)
    nlohmann::json json_array = nlohmann::json::array();
    int num_rows = PQntuples(res);
    int num_fields = PQnfields(res);
    
    for (int row = 0; row < num_rows; ++row) {
        nlohmann::json json_row = nlohmann::json::object();
        
        for (int col = 0; col < num_fields; ++col) {
            const char* field_name = PQfname(res, col);
            if (!field_name) continue;
            
            if (PQgetisnull(res, row, col)) {
                json_row[field_name] = nullptr;
            } else {
                std::string value = PQgetvalue(res, row, col);
                Oid type_oid = PQftype(res, col);
                
                switch (type_oid) {
                    case 16: // bool
                        json_row[field_name] = (value == "t" || value == "true");
                        break;
                    case 20: // int8 (bigint)
                    case 21: // int2 (smallint)  
                    case 23: // int4 (integer)
                        try {
                            json_row[field_name] = std::stoll(value);
                        } catch (...) {
                            json_row[field_name] = value;
                        }
                        break;
                    case 700: // float4
                    case 701: // float8
                    case 1700: // numeric
                        try {
                            json_row[field_name] = std::stod(value);
                        } catch (...) {
                            json_row[field_name] = value;
                        }
                        break;
                    case 114: // json
                    case 3802: // jsonb
                        try {
                            json_row[field_name] = nlohmann::json::parse(value);
                        } catch (...) {
                            json_row[field_name] = value;
                        }
                        break;
                    default:
                        json_row[field_name] = value;
                        break;
                }
            }
        }
        
        json_array.push_back(json_row);
    }
    
    PQclear(res);
    
    // Consume any remaining results
    PGresult* extra;
    while ((extra = PQgetResult(conn)) != nullptr) {
        PQclear(extra);
    }
    
    return json_array;
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
                {"poolSize", async_db_pool_->size()},
                {"idleConnections", async_db_pool_->available()},
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
        auto conn = async_db_pool_->acquire();
        
        std::string query = R"(
            WITH queue_message_stats AS (
                SELECT
                    q.id as queue_id,
                    q.name as queue_name,
                    q.namespace,
                    q.task,
                    q.created_at as queue_created_at,
                    COUNT(DISTINCT p.id) as partition_count,
                    COUNT(DISTINCT m.id) as message_count,
                    COUNT(DISTINCT CASE 
                        WHEN dlq.message_id IS NOT NULL THEN NULL
                        WHEN pc.last_consumed_created_at IS NULL 
                            OR m.created_at > pc.last_consumed_created_at
                            OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                AND m.id > pc.last_consumed_id)
                        THEN m.id 
                        ELSE NULL 
                    END) as unconsumed_count,
                    COUNT(DISTINCT CASE 
                        WHEN pc.lease_expires_at IS NOT NULL 
                            AND pc.lease_expires_at > NOW()
                            AND (
                                pc.last_consumed_created_at IS NULL 
                                OR m.created_at > pc.last_consumed_created_at
                                OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                    AND m.id > pc.last_consumed_id)
                            )
                        THEN m.id 
                        ELSE NULL 
                    END) as processing_count
                FROM queen.queues q
                LEFT JOIN queen.partitions p ON p.queue_id = q.id
                LEFT JOIN queen.messages m ON m.partition_id = p.id
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
                GROUP BY q.id, q.name, q.namespace, q.task, q.created_at
            )
            SELECT
                queue_id,
                queue_name,
                namespace,
                task,
                queue_created_at,
                partition_count,
                message_count,
                GREATEST(0, unconsumed_count - processing_count) as pending_count,
                processing_count
            FROM queue_message_stats
            ORDER BY queue_created_at DESC
        )";
        
        auto result = exec_query_json(conn.get(), query);
        
        nlohmann::json queues = nlohmann::json::array();
        for (int i = 0; i < result.size(); i++) {
            std::string ns_val = json_to_string(result[i]["namespace"]);
            std::string task_val = json_to_string(result[i]["task"]);
            queues.push_back({
                {"id", result[i]["queue_id"]},
                {"name", result[i]["queue_name"]},
                {"namespace", ns_val.empty() ? nullptr : nlohmann::json(ns_val)},
                {"task", task_val.empty() ? nullptr : nlohmann::json(task_val)},
                {"createdAt", result[i]["queue_created_at"]},
                {"partitions", json_to_int(result[i]["partition_count"])},
                {"messages", {
                    {"total", json_to_int(result[i]["message_count"])},
                    {"pending", json_to_int(result[i]["pending_count"])},
                    {"processing", json_to_int(result[i]["processing_count"])}
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
        auto conn = async_db_pool_->acquire();
        
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
        
        auto queue_result = exec_query_params_json(conn.get(), queue_query, {queue_name});
        
        if (queue_result.size() == 0) {
            throw std::runtime_error("Queue not found");
        }
        
        std::string queue_id = json_to_string(queue_result[0]["id"]);
        
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
        
        auto partitions_result = exec_query_params_json(conn.get(), partitions_query, {queue_id});
        
        nlohmann::json partitions = nlohmann::json::array();
        int total_total = 0, total_pending = 0, total_processing = 0;
        int total_completed = 0, total_failed = 0, total_dead_letter = 0;
        
        for (int i = 0; i < partitions_result.size(); i++) {
            int total = json_to_int(partitions_result[i]["message_count"]);
            int pending = json_to_int(partitions_result[i]["pending"]);
            int processing = json_to_int(partitions_result[i]["processing"]);
            int completed = json_to_int(partitions_result[i]["completed"]);
            int failed = json_to_int(partitions_result[i]["failed"]);
            int dead_letter = json_to_int(partitions_result[i]["dead_letter"]);
            
            total_total += total;
            total_pending += pending;
            total_processing += processing;
            total_completed += completed;
            total_failed += failed;
            total_dead_letter += dead_letter;
            
            std::string oldest_msg = json_to_string(partitions_result[i]["oldest_message"]);
            std::string newest_msg = json_to_string(partitions_result[i]["newest_message"]);
            
            partitions.push_back({
                {"id", partitions_result[i]["id"]},
                {"name", partitions_result[i]["name"]},
                {"createdAt", partitions_result[i]["created_at"]},
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
            {"id", queue_result[0]["id"]},
            {"name", queue_result[0]["name"]},
            {"namespace", queue_result[0]["namespace"]},
            {"task", queue_result[0]["task"]},
            {"createdAt", queue_result[0]["created_at"]},
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
        auto conn = async_db_pool_->acquire();
        
        std::string query = R"(
            WITH namespace_stats AS (
                SELECT 
                    q.namespace,
                    COUNT(DISTINCT q.id) as queue_count,
                    COUNT(DISTINCT p.id) as partition_count,
                    COUNT(DISTINCT m.id) as message_count,
                    COUNT(DISTINCT CASE 
                        WHEN dlq.message_id IS NOT NULL THEN NULL
                        WHEN pc.last_consumed_created_at IS NULL 
                            OR m.created_at > pc.last_consumed_created_at
                            OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                AND m.id > pc.last_consumed_id)
                        THEN m.id 
                        ELSE NULL 
                    END) as unconsumed_count,
                    COUNT(DISTINCT CASE 
                        WHEN pc.lease_expires_at IS NOT NULL 
                            AND pc.lease_expires_at > NOW()
                            AND (
                                pc.last_consumed_created_at IS NULL 
                                OR m.created_at > pc.last_consumed_created_at
                                OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                    AND m.id > pc.last_consumed_id)
                            )
                        THEN m.id 
                        ELSE NULL 
                    END) as processing_count
                FROM queen.queues q
                LEFT JOIN queen.partitions p ON p.queue_id = q.id
                LEFT JOIN queen.messages m ON m.partition_id = p.id
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
                WHERE q.namespace IS NOT NULL
                GROUP BY q.namespace
            )
            SELECT 
                namespace,
                queue_count,
                partition_count,
                message_count,
                GREATEST(0, unconsumed_count - processing_count) as pending_count
            FROM namespace_stats
            ORDER BY namespace
        )";
        
        auto result = exec_query_json(conn.get(), query);
        
        nlohmann::json namespaces = nlohmann::json::array();
        for (int i = 0; i < result.size(); i++) {
            namespaces.push_back({
                {"namespace", result[i]["namespace"]},
                {"queues", json_to_int(result[i]["queue_count"])},
                {"partitions", json_to_int(result[i]["partition_count"])},
                {"messages", {
                    {"total", json_to_int(result[i]["message_count"])},
                    {"pending", json_to_int(result[i]["pending_count"])}
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
        auto conn = async_db_pool_->acquire();
        
        std::string query = R"(
            WITH task_stats AS (
                SELECT 
                    q.task,
                    COUNT(DISTINCT q.id) as queue_count,
                    COUNT(DISTINCT p.id) as partition_count,
                    COUNT(DISTINCT m.id) as message_count,
                    COUNT(DISTINCT CASE 
                        WHEN dlq.message_id IS NOT NULL THEN NULL
                        WHEN pc.last_consumed_created_at IS NULL 
                            OR m.created_at > pc.last_consumed_created_at
                            OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                AND m.id > pc.last_consumed_id)
                        THEN m.id 
                        ELSE NULL 
                    END) as unconsumed_count,
                    COUNT(DISTINCT CASE 
                        WHEN pc.lease_expires_at IS NOT NULL 
                            AND pc.lease_expires_at > NOW()
                            AND (
                                pc.last_consumed_created_at IS NULL 
                                OR m.created_at > pc.last_consumed_created_at
                                OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                    AND m.id > pc.last_consumed_id)
                            )
                        THEN m.id 
                        ELSE NULL 
                    END) as processing_count
                FROM queen.queues q
                LEFT JOIN queen.partitions p ON p.queue_id = q.id
                LEFT JOIN queen.messages m ON m.partition_id = p.id
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
                WHERE q.task IS NOT NULL
                GROUP BY q.task
            )
            SELECT 
                task,
                queue_count,
                partition_count,
                message_count,
                GREATEST(0, unconsumed_count - processing_count) as pending_count
            FROM task_stats
            ORDER BY task
        )";
        
        auto result = exec_query_json(conn.get(), query);
        
        nlohmann::json tasks = nlohmann::json::array();
        for (int i = 0; i < result.size(); i++) {
            tasks.push_back({
                {"task", result[i]["task"]},
                {"queues", json_to_int(result[i]["queue_count"])},
                {"partitions", json_to_int(result[i]["partition_count"])},
                {"messages", {
                    {"total", json_to_int(result[i]["message_count"])},
                    {"pending", json_to_int(result[i]["pending_count"])}
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
        auto conn = async_db_pool_->acquire();
        
        std::string query = R"(
            WITH message_stats AS (
                SELECT
                    COUNT(DISTINCT m.id) as total_messages,
                    COUNT(DISTINCT CASE 
                        WHEN dlq.message_id IS NOT NULL THEN NULL
                        WHEN pc.last_consumed_created_at IS NULL 
                            OR m.created_at > pc.last_consumed_created_at
                            OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                AND m.id > pc.last_consumed_id)
                        THEN m.id 
                        ELSE NULL 
                    END) as unconsumed_messages,
                    COUNT(DISTINCT CASE 
                        WHEN pc.lease_expires_at IS NOT NULL 
                            AND pc.lease_expires_at > NOW()
                            AND (
                                pc.last_consumed_created_at IS NULL 
                                OR m.created_at > pc.last_consumed_created_at
                                OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                    AND m.id > pc.last_consumed_id)
                            )
                        THEN m.id 
                        ELSE NULL 
                    END) as processing_messages,
                    COUNT(DISTINCT CASE 
                        WHEN dlq.message_id IS NOT NULL THEN m.id
                        ELSE NULL 
                    END) as dead_letter_messages,
                    COUNT(DISTINCT CASE 
                        WHEN dlq.message_id IS NULL
                            AND pc.last_consumed_created_at IS NOT NULL 
                            AND (m.created_at < pc.last_consumed_created_at
                                OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                    AND m.id <= pc.last_consumed_id))
                        THEN m.id
                        ELSE NULL
                    END) as completed_messages
                FROM queen.messages m
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = m.partition_id
                LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
            ),
            unconsumed_time_lags AS (
                SELECT
                    EXTRACT(EPOCH FROM (NOW() - m.created_at))::integer as lag_seconds
                FROM queen.messages m
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = m.partition_id
                LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
                WHERE dlq.message_id IS NULL
                  AND (pc.last_consumed_created_at IS NULL 
                       OR m.created_at > pc.last_consumed_created_at
                       OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                           AND m.id > pc.last_consumed_id))
            ),
            partition_offset_lags AS (
                SELECT
                    p.id as partition_id,
                    COUNT(DISTINCT m.id) as unconsumed_count
                FROM queen.partitions p
                LEFT JOIN queen.messages m ON m.partition_id = p.id
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
                WHERE dlq.message_id IS NULL
                  AND (pc.last_consumed_created_at IS NULL 
                       OR m.created_at > pc.last_consumed_created_at
                       OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                           AND m.id > pc.last_consumed_id))
                GROUP BY p.id
                HAVING COUNT(DISTINCT m.id) > 0
            )
            SELECT
                (SELECT COUNT(*) FROM queen.queues) as total_queues,
                (SELECT COUNT(*) FROM queen.partitions) as total_partitions,
                (SELECT COUNT(DISTINCT namespace) FROM queen.queues WHERE namespace IS NOT NULL) as namespaces,
                (SELECT COUNT(DISTINCT task) FROM queen.queues WHERE task IS NOT NULL) as tasks,
                ms.total_messages,
                GREATEST(0, ms.unconsumed_messages - ms.processing_messages) as pending_messages,
                ms.processing_messages,
                ms.completed_messages,
                ms.dead_letter_messages,
                -- Time-based lag metrics
                COALESCE((SELECT AVG(lag_seconds)::integer FROM unconsumed_time_lags), 0) as avg_time_lag_seconds,
                COALESCE((SELECT PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY lag_seconds)::integer FROM unconsumed_time_lags), 0) as median_time_lag_seconds,
                COALESCE((SELECT MIN(lag_seconds)::integer FROM unconsumed_time_lags), 0) as min_time_lag_seconds,
                COALESCE((SELECT MAX(lag_seconds)::integer FROM unconsumed_time_lags), 0) as max_time_lag_seconds,
                -- Offset-based lag metrics (per partition)
                COALESCE((SELECT AVG(unconsumed_count)::integer FROM partition_offset_lags), 0) as avg_offset_lag,
                COALESCE((SELECT PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY unconsumed_count)::integer FROM partition_offset_lags), 0) as median_offset_lag,
                COALESCE((SELECT MIN(unconsumed_count)::integer FROM partition_offset_lags), 0) as min_offset_lag,
                COALESCE((SELECT MAX(unconsumed_count)::integer FROM partition_offset_lags), 0) as max_offset_lag
            FROM message_stats ms
        )";
        
        auto result = exec_query_json(conn.get(), query);
        
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%S.000Z");
        
        nlohmann::json response = {
            {"queues", json_to_int(result[0]["total_queues"])},
            {"partitions", json_to_int(result[0]["total_partitions"])},
            {"namespaces", json_to_int(result[0]["namespaces"])},
            {"tasks", json_to_int(result[0]["tasks"])},
            {"messages", {
                {"total", json_to_int(result[0]["total_messages"])},
                {"pending", json_to_int(result[0]["pending_messages"])},
                {"processing", json_to_int(result[0]["processing_messages"])},
                {"completed", json_to_int(result[0]["completed_messages"])},
                {"failed", 0},
                {"deadLetter", json_to_int(result[0]["dead_letter_messages"])}
            }},
            {"lag", {
                {"time", {
                    {"avg", json_to_int(result[0]["avg_time_lag_seconds"])},
                    {"median", json_to_int(result[0]["median_time_lag_seconds"])},
                    {"min", json_to_int(result[0]["min_time_lag_seconds"])},
                    {"max", json_to_int(result[0]["max_time_lag_seconds"])}
                }},
                {"offset", {
                    {"avg", json_to_int(result[0]["avg_offset_lag"])},
                    {"median", json_to_int(result[0]["median_offset_lag"])},
                    {"min", json_to_int(result[0]["min_offset_lag"])},
                    {"max", json_to_int(result[0]["max_offset_lag"])}
                }}
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
        auto conn = async_db_pool_->acquire();
        
        // Get time range with default of 1 hour
        std::string from_iso, to_iso;
        get_time_range(filters.from, filters.to, from_iso, to_iso, 1);
        
        // First, detect mode for ALL messages (before filtering)
        std::string mode_query = R"(
            SELECT 
                bool_or(pc.consumer_group = '__QUEUE_MODE__') as has_queue_mode,
                COUNT(DISTINCT pc.consumer_group) FILTER (WHERE pc.consumer_group != '__QUEUE_MODE__') as bus_groups_count
            FROM queen.messages m
            JOIN queen.partitions p ON p.id = m.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
            WHERE m.created_at >= $1 AND m.created_at <= $2
        )";
        
        std::vector<std::string> mode_params = {from_iso, to_iso};
        
        if (!filters.queue.empty()) {
            mode_query += " AND q.name = $3";
            mode_params.push_back(filters.queue);
        }
        
        auto mode_result = exec_query_params_json(conn.get(), mode_query, mode_params);
        bool has_queue_mode = false;
        int total_bus_groups = 0;
        
        if (!mode_result.empty()) {
            std::string has_queue_str = json_to_string(mode_result[0]["has_queue_mode"]);
            has_queue_mode = (has_queue_str == "t" || has_queue_str == "true");
            total_bus_groups = json_to_int(mode_result[0]["bus_groups_count"]);
        }
        
        // Now query messages with per-partition mode information
        std::string query = R"(
            SELECT 
                m.id,
                m.transaction_id,
                m.partition_id,
                m.created_at,
                m.trace_id,
                q.name as queue_name,
                p.name as partition_name,
                q.namespace,
                q.task,
                q.priority as queue_priority,
                pc_queue.lease_expires_at,
                -- Queue mode status (from __QUEUE_MODE__)
                CASE
                    WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
                    WHEN pc_queue.last_consumed_created_at IS NOT NULL AND (
                        m.created_at < pc_queue.last_consumed_created_at OR 
                        (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc_queue.last_consumed_created_at) AND m.id <= pc_queue.last_consumed_id)
                    ) THEN 'completed'
                    WHEN pc_queue.lease_expires_at IS NOT NULL AND pc_queue.lease_expires_at > NOW() THEN 'processing'
                    ELSE 'pending'
                END as queue_status,
                -- Bus mode: count of consumer groups that consumed this message
                (
                    SELECT COUNT(*)::integer
                    FROM queen.partition_consumers pc
                    WHERE pc.partition_id = m.partition_id
                      AND pc.consumer_group != '__QUEUE_MODE__'
                      AND pc.last_consumed_created_at IS NOT NULL
                      AND (m.created_at < pc.last_consumed_created_at OR 
                           (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id))
                ) as consumed_by_groups_count,
                -- Per-partition mode detection
                (pc_queue.consumer_group IS NOT NULL) as partition_has_queue_mode,
                (
                    SELECT COUNT(*)::integer
                    FROM queen.partition_consumers pc
                    WHERE pc.partition_id = m.partition_id
                      AND pc.consumer_group != '__QUEUE_MODE__'
                ) as partition_bus_groups_count
            FROM queen.messages m
            JOIN queen.partitions p ON p.id = m.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            LEFT JOIN queen.partition_consumers pc_queue ON pc_queue.partition_id = p.id AND pc_queue.consumer_group = '__QUEUE_MODE__'
            LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
            WHERE m.created_at >= $1 AND m.created_at <= $2
        )";
        
        std::vector<std::string> params;
        params.push_back(from_iso);
        params.push_back(to_iso);
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
            // Apply filter based on detected mode
            if (!has_queue_mode && total_bus_groups > 0) {
                // Pure Bus Mode - filter based on ALL groups consumption
                if (filters.status == "completed") {
                    query += R"( AND (
                        SELECT COUNT(*)::integer
                        FROM queen.partition_consumers pc
                        WHERE pc.partition_id = m.partition_id
                          AND pc.consumer_group != '__QUEUE_MODE__'
                          AND pc.last_consumed_created_at IS NOT NULL
                          AND (m.created_at < pc.last_consumed_created_at OR 
                               (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id))
                    ) >= )" + std::to_string(total_bus_groups);
                } else if (filters.status == "pending") {
                    query += R"( AND (
                        SELECT COUNT(*)::integer
                        FROM queen.partition_consumers pc
                        WHERE pc.partition_id = m.partition_id
                          AND pc.consumer_group != '__QUEUE_MODE__'
                          AND pc.last_consumed_created_at IS NOT NULL
                          AND (m.created_at < pc.last_consumed_created_at OR 
                               (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id))
                    ) < )" + std::to_string(total_bus_groups);
                } else {
                    // Other statuses (dead_letter, processing)
                    query += " AND queue_status = $" + std::to_string(params.size() + 1);
                    params.push_back(filters.status);
                }
            } else {
                // Queue Mode or Hybrid - filter based on queue_status
                query += R"( AND CASE
                    WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
                    WHEN pc_queue.last_consumed_created_at IS NOT NULL AND (
                        m.created_at < pc_queue.last_consumed_created_at OR 
                        (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc_queue.last_consumed_created_at) AND m.id <= pc_queue.last_consumed_id)
                    ) THEN 'completed'
                    WHEN pc_queue.lease_expires_at IS NOT NULL AND pc_queue.lease_expires_at > NOW() THEN 'processing'
                    ELSE 'pending'
                END = $)" + std::to_string(params.size() + 1);
                params.push_back(filters.status);
            }
        }
        
        query += " ORDER BY m.created_at DESC, m.id DESC";
        query += " LIMIT $" + std::to_string(params.size() + 1);
        params.push_back(std::to_string(filters.limit));
        query += " OFFSET $" + std::to_string(params.size() + 1);
        params.push_back(std::to_string(filters.offset));
        
        auto result = exec_query_params_json(conn.get(), query, params);
        
        nlohmann::json messages = nlohmann::json::array();
        
        for (int i = 0; i < result.size(); i++) {
            std::string trace_val = json_to_string(result[i]["trace_id"]);
            std::string lease_val = json_to_string(result[i]["lease_expires_at"]);
            std::string queue_path = json_to_string(result[i]["queue_name"]) + "/" + json_to_string(result[i]["partition_name"]);
            
            int consumed_by_groups = json_to_int(result[i]["consumed_by_groups_count"]);
            std::string queue_status = json_to_string(result[i]["queue_status"]);
            
            // Get per-partition mode information
            std::string partition_has_queue_str = json_to_string(result[i]["partition_has_queue_mode"]);
            bool partition_has_queue_mode = (partition_has_queue_str == "t" || partition_has_queue_str == "true");
            int partition_bus_groups = json_to_int(result[i]["partition_bus_groups_count"]);
            
            // Compute final display status based on THIS MESSAGE'S partition mode
            std::string display_status;
            if (queue_status == "dead_letter") {
                display_status = "dead_letter";
            } else if (!partition_has_queue_mode && partition_bus_groups > 0) {
                // Pure Bus Mode for this partition - show bus status only
                display_status = consumed_by_groups == partition_bus_groups ? "completed" : "pending";
            } else if (partition_has_queue_mode && partition_bus_groups == 0) {
                // Pure Queue Mode for this partition - show queue status
                display_status = queue_status;
            } else if (partition_has_queue_mode && partition_bus_groups > 0) {
                // Hybrid mode for this partition - default to queue status
                display_status = queue_status;
            } else {
                // No consumers - show queue status (will likely be pending)
                display_status = queue_status;
            }
            
            nlohmann::json msg = {
                {"id", result[i]["id"]},
                {"transactionId", result[i]["transaction_id"]},
                {"partitionId", result[i]["partition_id"]},
                {"queuePath", queue_path},
                {"queue", result[i]["queue_name"]},
                {"partition", result[i]["partition_name"]},
                {"namespace", result[i]["namespace"]},
                {"task", result[i]["task"]},
                {"status", display_status},
                {"queueStatus", queue_status},
                {"busStatus", {
                    {"consumedBy", consumed_by_groups},
                    {"totalGroups", partition_bus_groups}  // Use per-partition count
                }},
                {"traceId", trace_val.empty() ? nullptr : nlohmann::json(trace_val)},
                {"queuePriority", json_to_int(result[i]["queue_priority"])},
                {"createdAt", result[i]["created_at"]},
                {"leaseExpiresAt", lease_val.empty() ? nullptr : nlohmann::json(lease_val)}
            };
            
            messages.push_back(msg);
        }
        
        return {
            {"messages", messages},
            {"mode", {
                {"hasQueueMode", has_queue_mode},
                {"busGroupsCount", total_bus_groups},
                {"type", !has_queue_mode && total_bus_groups > 0 ? "bus" : 
                         has_queue_mode && total_bus_groups == 0 ? "queue" :
                         has_queue_mode && total_bus_groups > 0 ? "hybrid" : "none"}
            }}
        };
    } catch (const std::exception& e) {
        spdlog::error("Error in list_messages: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_message(const std::string& partition_id, const std::string& transaction_id) {
    try {
        auto conn = async_db_pool_->acquire();
        
        std::string query = R"(
            SELECT 
                m.id,
                m.transaction_id,
                m.partition_id,
                m.payload,
                m.created_at,
                m.trace_id,
                m.is_encrypted,
                q.name as queue_name,
                p.name as partition_name,
                q.namespace,
                q.task,
                q.lease_time,
                q.retry_limit,
                q.retry_delay,
                q.ttl,
                q.priority as queue_priority,
                pc_queue.lease_expires_at,
                pc_queue.last_consumed_created_at,
                pc_queue.last_consumed_id,
                dlq.error_message,
                dlq.retry_count,
                -- Queue mode status
                CASE
                    WHEN dlq.message_id IS NOT NULL THEN 'dead_letter'
                    WHEN pc_queue.last_consumed_created_at IS NOT NULL AND (
                        m.created_at < pc_queue.last_consumed_created_at OR 
                        (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc_queue.last_consumed_created_at) AND m.id <= pc_queue.last_consumed_id)
                    ) THEN 'completed'
                    WHEN pc_queue.lease_expires_at IS NOT NULL AND pc_queue.lease_expires_at > NOW() THEN 'processing'
                    ELSE 'pending'
                END as queue_status,
                -- Bus mode: count of consumer groups that consumed this message
                (
                    SELECT COUNT(*)::integer
                    FROM queen.partition_consumers pc
                    WHERE pc.partition_id = m.partition_id
                      AND pc.consumer_group != '__QUEUE_MODE__'
                      AND pc.last_consumed_created_at IS NOT NULL
                      AND (m.created_at < pc.last_consumed_created_at OR 
                           (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) AND m.id <= pc.last_consumed_id))
                ) as consumed_by_groups_count,
                -- Total bus mode consumer groups
                (
                    SELECT COUNT(*)::integer
                    FROM queen.partition_consumers pc
                    WHERE pc.partition_id = m.partition_id
                      AND pc.consumer_group != '__QUEUE_MODE__'
                ) as total_bus_groups,
                -- Check if __QUEUE_MODE__ consumer exists
                (pc_queue.consumer_group IS NOT NULL) as has_queue_mode
            FROM queen.messages m
            JOIN queen.partitions p ON p.id = m.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            LEFT JOIN queen.partition_consumers pc_queue ON pc_queue.partition_id = p.id AND pc_queue.consumer_group = '__QUEUE_MODE__'
            LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = m.id
            WHERE m.partition_id = $1::uuid AND m.transaction_id = $2
        )";
        
        auto result = exec_query_params_json(conn.get(), query, {partition_id, transaction_id});
        
        if (result.size() == 0) {
            throw std::runtime_error("Message not found");
        }
        
        std::string trace_val = json_to_string(result[0]["trace_id"]);
        std::string lease_val = json_to_string(result[0]["lease_expires_at"]);
        std::string error_val = json_to_string(result[0]["error_message"]);
        std::string retry_val = json_to_string(result[0]["retry_count"]);
        std::string queue_path = json_to_string(result[0]["queue_name"]) + "/" + json_to_string(result[0]["partition_name"]);
        
        // Get mode detection values
        std::string has_queue_str = json_to_string(result[0]["has_queue_mode"]);
        bool has_queue_mode = (has_queue_str == "t" || has_queue_str == "true");
        int total_bus_groups = json_to_int(result[0]["total_bus_groups"]);
        int consumed_by_groups = json_to_int(result[0]["consumed_by_groups_count"]);
        std::string queue_status = json_to_string(result[0]["queue_status"]);
        
        // Compute smart display status
        std::string display_status;
        if (queue_status == "dead_letter") {
            display_status = "dead_letter";
        } else if (!has_queue_mode && total_bus_groups > 0) {
            // Pure Bus Mode
            display_status = consumed_by_groups == total_bus_groups ? "completed" : "pending";
        } else if (has_queue_mode && total_bus_groups == 0) {
            // Pure Queue Mode
            display_status = queue_status;
        } else {
            // Hybrid or no consumers
            display_status = queue_status;
        }
        
        nlohmann::json response = {
            {"id", result[0]["id"]},
            {"transactionId", result[0]["transaction_id"]},
            {"partitionId", result[0]["partition_id"]},
            {"queuePath", queue_path},
            {"queue", result[0]["queue_name"]},
            {"partition", result[0]["partition_name"]},
            {"namespace", result[0]["namespace"]},
            {"task", result[0]["task"]},
            {"status", display_status},
            {"queueStatus", queue_status},
            {"busStatus", {
                {"consumedBy", consumed_by_groups},
                {"totalGroups", total_bus_groups}
            }},
            {"mode", {
                {"hasQueueMode", has_queue_mode},
                {"busGroupsCount", total_bus_groups},
                {"type", !has_queue_mode && total_bus_groups > 0 ? "bus" : 
                         has_queue_mode && total_bus_groups == 0 ? "queue" :
                         has_queue_mode && total_bus_groups > 0 ? "hybrid" : "none"}
            }},
            {"traceId", trace_val.empty() ? nullptr : nlohmann::json(trace_val)},
            {"createdAt", result[0]["created_at"]},
            {"errorMessage", error_val.empty() ? nullptr : nlohmann::json(error_val)},
            {"retryCount", retry_val.empty() ? nullptr : nlohmann::json(safe_stoi(retry_val))},
            {"leaseExpiresAt", lease_val.empty() ? nullptr : nlohmann::json(lease_val)},
            {"queueConfig", {
                {"leaseTime", json_to_int(result[0]["lease_time"])},
                {"retryLimit", json_to_int(result[0]["retry_limit"])},
                {"retryDelay", json_to_int(result[0]["retry_delay"])},
                {"ttl", json_to_int(result[0]["ttl"])},
                {"priority", json_to_int(result[0]["queue_priority"])}
            }}
        };
        
        // Parse payload as JSON - decrypt if encrypted
        try {
            std::string is_encrypted_str = json_to_string(result[0]["is_encrypted"]);
            bool is_encrypted = (is_encrypted_str == "t" || is_encrypted_str == "true");
            
            // Handle payload - it might be JSON object, string, or null
            if (result[0]["payload"].is_null()) {
                response["payload"] = nullptr;
            } else if (result[0]["payload"].is_object() || result[0]["payload"].is_array()) {
                // Payload is already a JSON object/array (from JSONB column)
                if (is_encrypted) {
                    // Decrypt the payload
                    EncryptionService::EncryptedData encrypted_data;
                    encrypted_data.encrypted = result[0]["payload"]["encrypted"];
                    encrypted_data.iv = result[0]["payload"]["iv"];
                    encrypted_data.auth_tag = result[0]["payload"]["authTag"];
                    
                    auto enc_service = get_encryption_service();
                    if (enc_service && enc_service->is_enabled()) {
                        auto decrypted = enc_service->decrypt_payload(encrypted_data);
                        if (decrypted.has_value()) {
                            response["payload"] = nlohmann::json::parse(*decrypted);
                        } else {
                            spdlog::error("Failed to decrypt message payload for transaction_id: {}", transaction_id);
                            response["payload"] = result[0]["payload"];  // Return encrypted data as fallback
                        }
                    } else {
                        spdlog::warn("Message is encrypted but encryption service not available");
                        response["payload"] = result[0]["payload"];
                    }
                } else {
                    // Direct JSON object - use as is
                    response["payload"] = result[0]["payload"];
                }
            } else if (result[0]["payload"].is_string()) {
                // Payload is a string that might need parsing
                std::string payload_str = result[0]["payload"].get<std::string>();
                if (payload_str.empty()) {
                    response["payload"] = nullptr;
                } else if (is_encrypted) {
                    // Decrypt the payload from string
                    auto encrypted_json = nlohmann::json::parse(payload_str);
                    
                    EncryptionService::EncryptedData encrypted_data;
                    encrypted_data.encrypted = encrypted_json["encrypted"];
                    encrypted_data.iv = encrypted_json["iv"];
                    encrypted_data.auth_tag = encrypted_json["authTag"];
                    
                    auto enc_service = get_encryption_service();
                    if (enc_service && enc_service->is_enabled()) {
                        auto decrypted = enc_service->decrypt_payload(encrypted_data);
                        if (decrypted.has_value()) {
                            response["payload"] = nlohmann::json::parse(*decrypted);
                        } else {
                            spdlog::error("Failed to decrypt message payload for transaction_id: {}", transaction_id);
                            response["payload"] = payload_str;
                        }
                    } else {
                        spdlog::warn("Message is encrypted but encryption service not available");
                        response["payload"] = payload_str;
                    }
                } else {
                    response["payload"] = nlohmann::json::parse(payload_str);
                }
            } else {
                // Other type - convert to string
                response["payload"] = json_to_string(result[0]["payload"]);
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to parse payload for transaction_id {}: {}", transaction_id, e.what());
            response["payload"] = result[0]["payload"];  // Return raw value as fallback
        }
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_message: {}", e.what());
        throw;
    }
}

bool AnalyticsManager::delete_message(const std::string& partition_id, const std::string& transaction_id) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Delete the message - cascading deletes will handle DLQ entries
        std::string delete_sql = R"(
            DELETE FROM queen.messages
            WHERE partition_id = $1::uuid AND transaction_id = $2
        )";
        
        auto result = exec_query_params_json(conn.get(), delete_sql, {partition_id, transaction_id});
        
        // Errors throw exceptions, so if we got here, it succeeded
        if (false) {
            spdlog::error("Failed to delete message: partition_id={}, transaction_id={}", 
                         partition_id, transaction_id);
            return false;
        }
        
        // Check if any rows were deleted
        std::string affected = "1";
        int rows_deleted = affected.empty() ? 0 : std::stoi(affected);
        
        if (rows_deleted == 0) {
            spdlog::warn("No message found to delete: partition_id={}, transaction_id={}", 
                        partition_id, transaction_id);
            return false;
        }
        
        spdlog::info("Deleted message: partition_id={}, transaction_id={}", 
                    partition_id, transaction_id);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Error in delete_message: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_dlq_messages(const DLQFilters& filters) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Build WHERE clause based on filters
        std::string where_clause = " WHERE 1=1";
        std::vector<std::string> params;
        int param_num = 1;
        
        if (!filters.queue.empty()) {
            where_clause += " AND q.name = $" + std::to_string(param_num++);
            params.push_back(filters.queue);
        }
        
        if (!filters.consumer_group.empty()) {
            where_clause += " AND dlq.consumer_group = $" + std::to_string(param_num++);
            params.push_back(filters.consumer_group);
        }
        
        if (!filters.partition.empty()) {
            where_clause += " AND p.name = $" + std::to_string(param_num++);
            params.push_back(filters.partition);
        }
        
        if (!filters.from.empty()) {
            where_clause += " AND dlq.failed_at >= $" + std::to_string(param_num++) + "::timestamptz";
            params.push_back(filters.from);
        }
        
        if (!filters.to.empty()) {
            where_clause += " AND dlq.failed_at <= $" + std::to_string(param_num++) + "::timestamptz";
            params.push_back(filters.to);
        }
        
        // Query DLQ messages with full message details
        std::string query = R"(
            SELECT 
                dlq.id,
                dlq.message_id,
                dlq.partition_id,
                dlq.consumer_group,
                dlq.error_message,
                dlq.retry_count,
                dlq.original_created_at,
                dlq.failed_at as moved_to_dlq_at,
                m.transaction_id,
                m.payload,
                m.is_encrypted,
                m.trace_id,
                m.created_at as message_created_at,
                q.name as queue_name,
                q.namespace,
                q.task,
                p.name as partition_name
            FROM queen.dead_letter_queue dlq
            JOIN queen.messages m ON dlq.message_id = m.id
            JOIN queen.partitions p ON m.partition_id = p.id
            JOIN queen.queues q ON p.queue_id = q.id
        )" + where_clause + R"(
            ORDER BY dlq.failed_at DESC
            LIMIT $)" + std::to_string(param_num++) + R"(
            OFFSET $)" + std::to_string(param_num++);
        
        params.push_back(std::to_string(filters.limit));
        params.push_back(std::to_string(filters.offset));
        
        auto result = exec_query_params_json(conn.get(), query, params);
        
        // If we got here without exception, query succeeded
        
        nlohmann::json messages = nlohmann::json::array();
        
        for (size_t i = 0; i < result.size(); i++) {
            std::string is_encrypted_str = json_to_string(result[i]["is_encrypted"]);
            bool is_encrypted = (is_encrypted_str == "t" || is_encrypted_str == "true");
            nlohmann::json payload;
            
            // Handle payload - it might be JSON object, string, or null
            if (result[i]["payload"].is_null()) {
                payload = nullptr;
            } else if (result[i]["payload"].is_object() || result[i]["payload"].is_array()) {
                // Payload is already a JSON object/array (from JSONB column)
                if (is_encrypted) {
                    // Decrypt the payload
                    EncryptionService::EncryptedData encrypted_data;
                    encrypted_data.encrypted = result[i]["payload"]["encrypted"];
                    encrypted_data.iv = result[i]["payload"]["iv"];
                    encrypted_data.auth_tag = result[i]["payload"]["authTag"];
                    
                    auto enc_service = get_encryption_service();
                    if (enc_service && enc_service->is_enabled()) {
                        auto decrypted = enc_service->decrypt_payload(encrypted_data);
                        if (decrypted.has_value()) {
                            payload = nlohmann::json::parse(*decrypted);
                        } else {
                            spdlog::error("Failed to decrypt DLQ message payload for message_id: {}", 
                                         result[i]["message_id"].dump());
                            payload = result[i]["payload"];  // Return encrypted data as fallback
                        }
                    } else {
                        spdlog::warn("DLQ message is encrypted but encryption service not available");
                        payload = result[i]["payload"];
                    }
                } else {
                    // Direct JSON object - use as is
                    payload = result[i]["payload"];
                }
            } else if (result[i]["payload"].is_string()) {
                // Payload is a string that might need parsing
                std::string payload_str = result[i]["payload"].get<std::string>();
                if (payload_str.empty()) {
                    payload = nullptr;
                } else {
                    try {
                        if (is_encrypted) {
                            // Decrypt the payload from string
                            auto encrypted_json = nlohmann::json::parse(payload_str);
                            
                            EncryptionService::EncryptedData encrypted_data;
                            encrypted_data.encrypted = encrypted_json["encrypted"];
                            encrypted_data.iv = encrypted_json["iv"];
                            encrypted_data.auth_tag = encrypted_json["authTag"];
                            
                            auto enc_service = get_encryption_service();
                            if (enc_service && enc_service->is_enabled()) {
                                auto decrypted = enc_service->decrypt_payload(encrypted_data);
                                if (decrypted.has_value()) {
                                    payload = nlohmann::json::parse(*decrypted);
                                } else {
                                    spdlog::error("Failed to decrypt DLQ message payload for message_id: {}", 
                                                 result[i]["message_id"].dump());
                                    payload = payload_str;
                                }
                            } else {
                                spdlog::warn("DLQ message is encrypted but encryption service not available");
                                payload = payload_str;
                            }
                        } else {
                            payload = nlohmann::json::parse(payload_str);
                        }
                    } catch (const std::exception& e) {
                        spdlog::warn("Failed to parse DLQ payload for message_id {}: {}", 
                                     result[i]["message_id"].dump(), e.what());
                        payload = payload_str; // Fallback to string if JSON parse fails
                    }
                }
            } else {
                // Other type - convert to string
                payload = json_to_string(result[i]["payload"]);
            }
            
            nlohmann::json message = {
                {"id", result[i]["id"]},
                {"messageId", result[i]["message_id"]},
                {"partitionId", result[i]["partition_id"]},
                {"transactionId", result[i]["transaction_id"]},
                {"consumerGroup", result[i]["consumer_group"]},
                {"errorMessage", result[i]["error_message"]},
                {"retryCount", json_to_int(result[i]["retry_count"])},
                {"originalCreatedAt", result[i]["original_created_at"]},
                {"movedToDlqAt", result[i]["moved_to_dlq_at"]},
                {"messageCreatedAt", result[i]["message_created_at"]},
                {"queueName", result[i]["queue_name"]},
                {"namespace", result[i]["namespace"]},
                {"task", result[i]["task"]},
                {"partitionName", result[i]["partition_name"]},
                {"data", payload}
            };
            
            std::string trace_id = json_to_string(result[i]["trace_id"]);
            if (!trace_id.empty()) {
                message["traceId"] = trace_id;
            }
            
            messages.push_back(message);
        }
        
        // Get total count for pagination
        std::string count_query = R"(
            SELECT COUNT(*) as total
            FROM queen.dead_letter_queue dlq
            JOIN queen.messages m ON dlq.message_id = m.id
            JOIN queen.partitions p ON m.partition_id = p.id
            JOIN queen.queues q ON p.queue_id = q.id
        )" + where_clause;
        
        // Remove limit and offset params for count query
        std::vector<std::string> count_params(params.begin(), params.end() - 2);
        auto count_result = exec_query_params_json(conn.get(), count_query, count_params);
        
        int total = 0;
        if (!count_result.empty()) {
            total = json_to_int(count_result[0]["total"]);
        }
        
        nlohmann::json response = {
            {"messages", messages},
            {"total", total},
            {"limit", filters.limit},
            {"offset", filters.offset}
        };
        
        return response;
        
    } catch (const std::exception& e) {
        spdlog::error("Error in get_dlq_messages: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_status(const StatusFilters& filters) {
    try {
        auto conn = async_db_pool_->acquire();
        
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
        
        auto throughput_result = exec_query_params_json(conn.get(), throughput_query, {from_iso, to_iso});
        
        nlohmann::json throughput = nlohmann::json::array();
        for (int i = 0; i < throughput_result.size(); i++) {
            int ingested = json_to_int(throughput_result[i]["messages_ingested"]);
            int processed = json_to_int(throughput_result[i]["messages_processed"]);
            throughput.push_back({
                {"timestamp", throughput_result[i]["minute"]},
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
            LEFT JOIN queen.messages m ON m.partition_id = p.id
                AND m.created_at >= $1 AND m.created_at <= $2
            WHERE 1=1 )" + filter_clause + R"(
            GROUP BY q.id, q.name, q.namespace, q.task
            HAVING COUNT(DISTINCT m.id) > 0
            ORDER BY q.name
        )";
        
        auto queues_result = exec_query_params_json(conn.get(), queues_query, {from_iso, to_iso});
        
        nlohmann::json queues = nlohmann::json::array();
        for (int i = 0; i < queues_result.size(); i++) {
            std::string ns_val = json_to_string(queues_result[i]["namespace"]);
            std::string task_val = json_to_string(queues_result[i]["task"]);
            queues.push_back({
                {"id", queues_result[i]["id"]},
                {"name", queues_result[i]["name"]},
                {"namespace", ns_val.empty() ? nullptr : nlohmann::json(ns_val)},
                {"task", task_val.empty() ? nullptr : nlohmann::json(task_val)},
                {"partitions", json_to_int(queues_result[i]["partition_count"])},
                {"totalConsumed", json_to_int(queues_result[i]["total_consumed"])}
            });
        }
        
        // Query 3: Message counts
        std::string counts_query = R"(
            WITH time_filtered_messages AS (
                SELECT DISTINCT m.id, m.partition_id, m.created_at
                FROM queen.messages m
                LEFT JOIN queen.partitions p ON p.id = m.partition_id
                LEFT JOIN queen.queues q ON q.id = p.queue_id
                WHERE m.created_at >= $1 AND m.created_at <= $2 )" + filter_clause + R"(
            )
            SELECT 
                COUNT(DISTINCT tfm.id) as total_messages,
                COUNT(DISTINCT CASE 
                    WHEN dlq.message_id IS NOT NULL THEN NULL
                    WHEN pc.last_consumed_created_at IS NULL 
                        OR tfm.created_at > pc.last_consumed_created_at
                        OR (DATE_TRUNC('milliseconds', tfm.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                            AND tfm.id > pc.last_consumed_id)
                    THEN tfm.id 
                    ELSE NULL 
                END) as unconsumed,
                COUNT(DISTINCT CASE 
                    WHEN pc.lease_expires_at IS NOT NULL 
                        AND pc.lease_expires_at > NOW()
                        AND (
                            pc.last_consumed_created_at IS NULL 
                            OR tfm.created_at > pc.last_consumed_created_at
                            OR (DATE_TRUNC('milliseconds', tfm.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                AND tfm.id > pc.last_consumed_id)
                        )
                    THEN tfm.id 
                    ELSE NULL 
                END) as processing,
                COUNT(DISTINCT CASE 
                    WHEN dlq.message_id IS NULL
                        AND pc.last_consumed_created_at IS NOT NULL 
                        AND (tfm.created_at < pc.last_consumed_created_at
                            OR (DATE_TRUNC('milliseconds', tfm.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                                AND tfm.id <= pc.last_consumed_id))
                    THEN tfm.id
                    ELSE NULL
                END) as completed,
                COUNT(DISTINCT CASE 
                    WHEN dlq.message_id IS NOT NULL THEN tfm.id
                    ELSE NULL 
                END) as dead_letter
            FROM time_filtered_messages tfm
            LEFT JOIN queen.partition_consumers pc ON pc.partition_id = tfm.partition_id
            LEFT JOIN queen.dead_letter_queue dlq ON dlq.message_id = tfm.id
        )";
        
        auto counts_result = exec_query_params_json(conn.get(), counts_query, {from_iso, to_iso});
        
        int total = json_to_int(counts_result[0]["total_messages"]);
        int unconsumed = json_to_int(counts_result[0]["unconsumed"]);
        int processing = json_to_int(counts_result[0]["processing"]);
        int pending = std::max(0, unconsumed - processing);
        
        nlohmann::json messages = {
            {"total", total},
            {"pending", pending},
            {"processing", processing},
            {"completed", json_to_int(counts_result[0]["completed"])},
            {"failed", 0},
            {"deadLetter", json_to_int(counts_result[0]["dead_letter"])}
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
        
        auto leases_result = exec_query_json(conn.get(), leases_query);
        
        nlohmann::json leases = {
            {"active", json_to_int(leases_result[0]["active_leases"])},
            {"partitionsWithLeases", json_to_int(leases_result[0]["partitions_with_leases"])},
            {"totalBatchSize", json_to_int(leases_result[0]["total_batch_size"])},
            {"totalAcked", json_to_int(leases_result[0]["total_acked"])}
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
        
        auto dlq_result = exec_query_params_json(conn.get(), dlq_query, {from_iso, to_iso});
        
        int dlq_total = 0;
        std::set<std::string> dlq_partitions;
        nlohmann::json top_errors = nlohmann::json::array();
        
        for (int i = 0; i < dlq_result.size(); i++) {
            int error_count = json_to_int(dlq_result[i]["error_count"]);
            dlq_total += error_count;
            std::string partition_id = json_to_string(dlq_result[i]["partition_id"]);
            if (!partition_id.empty()) {
                dlq_partitions.insert(partition_id);
            }
            
            std::string error_msg = json_to_string(dlq_result[i]["error_message"]);
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
        auto conn = async_db_pool_->acquire();
        
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
        
        auto result = exec_query_params_json(conn.get(), query, {std::to_string(limit), std::to_string(offset)});
        
        nlohmann::json queues = nlohmann::json::array();
        for (int i = 0; i < result.size(); i++) {
            int total = json_to_int(result[i]["total_messages"]);
            int completed = json_to_int(result[i]["completed"]);
            int processing = json_to_int(result[i]["processing"]);
            int pending = std::max(0, total - completed - processing);
            
            queues.push_back({
                {"id", result[i]["id"]},
                {"name", result[i]["name"]},
                {"namespace", result[i]["namespace"]},
                {"task", result[i]["task"]},
                {"priority", json_to_int(result[i]["priority"])},
                {"createdAt", result[i]["created_at"]},
                {"partitions", json_to_int(result[i]["partition_count"])},
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
        auto conn = async_db_pool_->acquire();
        
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
        
        auto result = exec_query_params_json(conn.get(), query, {queue_name});
        
        if (result.size() == 0) {
            throw std::runtime_error("Queue not found");
        }
        
        // Build queue info from first row
        nlohmann::json queue_info = {
            {"id", result[0]["id"]},
            {"name", result[0]["name"]},
            {"namespace", result[0]["namespace"]},
            {"task", result[0]["task"]},
            {"priority", json_to_int(result[0]["priority"])},
            {"config", {
                {"leaseTime", json_to_int(result[0]["lease_time"])},
                {"retryLimit", json_to_int(result[0]["retry_limit"])},
                {"ttl", json_to_int(result[0]["ttl"])},
                {"maxQueueSize", json_to_int(result[0]["max_queue_size"])}
            }},
            {"createdAt", result[0]["created_at"]}
        };
        
        // Aggregate totals
        int total_msgs = 0, total_pending = 0, total_processing = 0, total_completed = 0;
        int total_consumed = 0, total_batches = 0;
        
        nlohmann::json partitions = nlohmann::json::array();
        for (int i = 0; i < result.size(); i++) {
            std::string partition_id = json_to_string(result[i]["partition_id"]);
            if (partition_id.empty()) continue;
            
            int total = json_to_int(result[i]["partition_total_messages"]);
            int completed = json_to_int(result[i]["partition_completed"]);
            int processing = json_to_int(result[i]["partition_processing"]);
            int pending = std::max(0, total - completed - processing);
            
            total_msgs += total;
            total_pending += pending;
            total_processing += processing;
            total_completed += completed;
            total_consumed += json_to_int(result[i]["total_messages_consumed"]);
            total_batches += json_to_int(result[i]["total_batches_consumed"]);
            
            nlohmann::json partition = {
                {"id", partition_id},
                {"name", result[i]["partition_name"]},
                {"createdAt", result[i]["partition_created_at"]},
                {"messages", {
                    {"total", total},
                    {"pending", pending},
                    {"processing", processing},
                    {"completed", completed},
                    {"failed", 0}
                }},
                {"cursor", {
                    {"totalConsumed", json_to_int(result[i]["total_messages_consumed"])},
                    {"batchesConsumed", json_to_int(result[i]["total_batches_consumed"])},
                    {"lastConsumedAt", result[i]["last_consumed_at"]}
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
        auto conn = async_db_pool_->acquire();
        
        std::string from_iso, to_iso;
        get_time_range("", "", from_iso, to_iso, 1);
        
        std::string query = R"(
            SELECT 
                m.id, m.transaction_id, m.trace_id, m.created_at,
                p.name as partition,
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
        
        auto result = exec_query_params_json(conn.get(), query, {
            queue_name, from_iso, to_iso, 
            std::to_string(limit), std::to_string(offset)
        });
        
        nlohmann::json messages = nlohmann::json::array();
        for (int i = 0; i < result.size(); i++) {
            std::string trace_val = json_to_string(result[i]["trace_id"]);
            nlohmann::json msg = {
                {"id", result[i]["id"]},
                {"transactionId", result[i]["transaction_id"]},
                {"traceId", trace_val.empty() ? nullptr : nlohmann::json(trace_val)},
                {"partition", result[i]["partition"]},
                {"createdAt", result[i]["created_at"]},
                {"status", result[i]["status"]},
                {"retryCount", 0}
            };
            
            std::string status = json_to_string(result[i]["status"]);
            if (status == "pending" || status == "processing") {
                std::string age_str = json_to_string(result[i]["age_seconds"]);
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
        auto conn = async_db_pool_->acquire();
        
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
        
        auto throughput_result = exec_query_params_json(conn.get(), throughput_query, {from_iso, to_iso});
        
        int seconds_per_bucket = interval == "minute" ? 60 : (interval == "day" ? 86400 : 3600);
        
        nlohmann::json throughput_series = nlohmann::json::array();
        int total_ingested = 0, total_processed = 0, total_failed = 0;
        
        for (int i = 0; i < throughput_result.size(); i++) {
            int ingested = json_to_int(throughput_result[i]["ingested"]);
            int processed = json_to_int(throughput_result[i]["processed"]);
            int failed = json_to_int(throughput_result[i]["failed"]);
            
            total_ingested += ingested;
            total_processed += processed;
            total_failed += failed;
            
            throughput_series.push_back({
                {"timestamp", throughput_result[i]["timestamp"]},
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
        
        auto depth_result = exec_query_json(conn.get(), depth_query);
        
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
                    {"pending", json_to_int(depth_result[0]["pending"])},
                    {"processing", json_to_int(depth_result[0]["processing"])}
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

nlohmann::json AnalyticsManager::get_system_metrics(const SystemMetricsFilters& filters) {
    try {
        auto conn = async_db_pool_->acquire();
        
        std::string from_iso, to_iso;
        get_time_range(filters.from, filters.to, from_iso, to_iso, 1);
        
        // Build filter clause
        std::string where_clause = "";
        std::vector<std::string> params = {from_iso, to_iso};
        int param_idx = 3;
        
        if (!filters.hostname.empty()) {
            where_clause += " AND hostname = $" + std::to_string(param_idx++);
            params.push_back(filters.hostname);
        }
        
        if (!filters.worker_id.empty()) {
            where_clause += " AND worker_id = $" + std::to_string(param_idx++);
            params.push_back(filters.worker_id);
        }
        
        // Query for system metrics - grouped by replica
        std::string query = R"(
            SELECT 
                timestamp,
                hostname,
                port,
                worker_id,
                sample_count,
                metrics
 FROM queen.system_metrics 
            WHERE timestamp >= $1::timestamptz
                AND timestamp <= $2::timestamptz )" + where_clause + R"(
            ORDER BY hostname, port, timestamp ASC
        )";
        
        auto result = exec_query_params_json(conn.get(), query, params);
        
        // Group data by replica (hostname:port)
        std::map<std::string, nlohmann::json> replicas;
        
        for (int i = 0; i < result.size(); i++) {
            std::string hostname = json_to_string(result[i]["hostname"]);
            std::string port = json_to_string(result[i]["port"]);
            std::string worker_id = json_to_string(result[i]["worker_id"]);
            std::string timestamp = json_to_string(result[i]["timestamp"]);
            
            // Create replica key (hostname:port)
            std::string replica_key = hostname + ":" + port;
            
            // Parse metrics - the metrics column might be JSON object or string
            nlohmann::json metrics;
            if (result[i]["metrics"].is_object() || result[i]["metrics"].is_array()) {
                // Already a JSON object/array
                metrics = result[i]["metrics"];
            } else if (result[i]["metrics"].is_string()) {
                // String that needs parsing
                std::string metrics_str = result[i]["metrics"].get<std::string>();
                if (!metrics_str.empty()) {
                    try {
                        metrics = nlohmann::json::parse(metrics_str);
                    } catch (const std::exception& e) {
                        spdlog::warn("Failed to parse metrics JSON string: {}", e.what());
                        metrics = nlohmann::json::object();
                    }
                } else {
                    metrics = nlohmann::json::object();
                }
            } else {
                // Null or other type
                metrics = nlohmann::json::object();
            }
            
            // Initialize replica if not exists
            if (replicas.find(replica_key) == replicas.end()) {
                replicas[replica_key] = {
                    {"hostname", hostname},
                    {"port", safe_stoi(port)},
                    {"workerId", worker_id},
                    {"timeSeries", nlohmann::json::array()}
                };
            }
            
            // Add data point to this replica's time series
            replicas[replica_key]["timeSeries"].push_back({
                {"timestamp", timestamp},
                {"sampleCount", json_to_int(result[i]["sample_count"])},
                {"metrics", metrics}
            });
        }
        
        // Convert map to array
        nlohmann::json replicas_array = nlohmann::json::array();
        for (const auto& [key, replica_data] : replicas) {
            replicas_array.push_back(replica_data);
        }
        
        nlohmann::json response = {
            {"timeRange", {
                {"from", from_iso},
                {"to", to_iso}
            }},
            {"replicas", replicas_array},
            {"replicaCount", replicas_array.size()}
        };
        
        return response;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_system_metrics: {}", e.what());
        throw;
    }
}

nlohmann::json AnalyticsManager::get_consumer_groups() {
    try {
        auto conn = async_db_pool_->acquire();
        
        std::string query = R"(
            SELECT 
                pc.consumer_group,
                q.name as queue_name,
                p.name as partition_name,
                pc.worker_id,
                pc.last_consumed_at,
                pc.last_consumed_id,
                pc.last_consumed_created_at,
                pc.total_messages_consumed,
                pc.total_batches_consumed,
                pc.lease_expires_at,
                pc.lease_acquired_at,
                -- Calculate lag: count messages after last consumed
                (
                    SELECT COUNT(*)
                    FROM queen.messages m
                    WHERE m.partition_id = pc.partition_id
                      AND (
                          pc.last_consumed_created_at IS NULL
                          OR m.created_at > pc.last_consumed_created_at
                          OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                              AND m.id > pc.last_consumed_id)
                      )
                )::integer as offset_lag,
                -- Calculate time lag: age of oldest unprocessed message
                (
                    SELECT EXTRACT(EPOCH FROM (NOW() - m.created_at))::integer
                    FROM queen.messages m
                    WHERE m.partition_id = pc.partition_id
                      AND (
                          pc.last_consumed_created_at IS NULL
                          OR m.created_at > pc.last_consumed_created_at
                          OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
                              AND m.id > pc.last_consumed_id)
                      )
                    ORDER BY m.created_at ASC
                    LIMIT 1
                )::integer as time_lag_seconds
            FROM queen.partition_consumers pc
            JOIN queen.partitions p ON p.id = pc.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            ORDER BY pc.consumer_group, q.name, p.name
        )";
        
        auto result = exec_query_json(conn.get(), query);
        
        // Group by consumer group
        std::map<std::string, nlohmann::json> consumer_groups_map;
        
        for (int i = 0; i < result.size(); i++) {
            std::string consumer_group = json_to_string(result[i]["consumer_group"]);
            std::string queue_name = json_to_string(result[i]["queue_name"]);
            int offset_lag = json_to_int(result[i]["offset_lag"]);
            int time_lag_seconds = json_to_int(result[i]["time_lag_seconds"]);
            
            // Initialize consumer group if not exists
            if (consumer_groups_map.find(consumer_group) == consumer_groups_map.end()) {
                consumer_groups_map[consumer_group] = {
                    {"name", consumer_group},
                    {"topics", nlohmann::json::array()},
                    {"queues", nlohmann::json::object()},
                    {"members", 0},
                    {"totalLag", 0},
                    {"maxTimeLag", 0},
                    {"state", "Stable"}
                };
            }
            
            auto& group = consumer_groups_map[consumer_group];
            
            // Add queue to topics if not already present
            bool queue_exists = false;
            for (const auto& topic : group["topics"]) {
                if (topic == queue_name) {
                    queue_exists = true;
                    break;
                }
            }
            if (!queue_exists) {
                group["topics"].push_back(queue_name);
            }
            
            // Track per-queue data
            if (!group["queues"].contains(queue_name)) {
                group["queues"][queue_name] = {
                    {"partitions", nlohmann::json::array()}
                };
            }
            
            std::string lease_expires = json_to_string(result[i]["lease_expires_at"]);
            group["queues"][queue_name]["partitions"].push_back({
                {"partition", result[i]["partition_name"]},
                {"workerId", result[i]["worker_id"]},
                {"lastConsumedAt", result[i]["last_consumed_at"]},
                {"totalConsumed", json_to_int(result[i]["total_messages_consumed"])},
                {"offsetLag", offset_lag},
                {"timeLagSeconds", time_lag_seconds},
                {"leaseActive", !lease_expires.empty()}
            });
            
            // Aggregate stats
            group["members"] = group["members"].get<int>() + 1;
            group["totalLag"] = group["totalLag"].get<int>() + offset_lag;
            
            if (time_lag_seconds > group["maxTimeLag"].get<int>()) {
                group["maxTimeLag"] = time_lag_seconds;
            }
            
            // Determine state
            std::string last_consumed_at = json_to_string(result[i]["last_consumed_at"]);
            if (time_lag_seconds > 300) { // More than 5 minutes behind
                group["state"] = "Lagging";
            } else if (last_consumed_at.empty()) {
                group["state"] = "Dead";
            }
        }
        
        // Convert map to array
        nlohmann::json groups_array = nlohmann::json::array();
        for (const auto& [group_name, group_data] : consumer_groups_map) {
            groups_array.push_back(group_data);
        }
        
        return groups_array;
    } catch (const std::exception& e) {
        spdlog::error("Error in get_consumer_groups: {}", e.what());
        throw;
    }
}

} // namespace queen

