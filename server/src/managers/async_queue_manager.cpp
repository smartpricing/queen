#include "queen/async_queue_manager.hpp"
#include "queen/file_buffer.hpp"
#include "queen/encryption.hpp"
#include "queen/shared_state_manager.hpp"
#include <spdlog/spdlog.h>

// External global for shared state
namespace queen {
extern std::shared_ptr<SharedStateManager> global_shared_state;
}
#include <random>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <numeric>
#include <map>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace queen {

// --- Constructor ---

AsyncQueueManager::AsyncQueueManager(std::shared_ptr<AsyncDbPool> async_db_pool, 
                                   const QueueConfig& config, 
                                   const std::string& schema_name)
    : async_db_pool_(async_db_pool), config_(config), schema_name_(schema_name) {
    if (!async_db_pool_) {
        throw std::invalid_argument("Async database pool cannot be null");
    }
}

// --- UUID Generation (same as QueueManager) ---

std::string AsyncQueueManager::generate_uuid() {
    static std::mutex uuid_mutex;
    static uint64_t last_ms = 0;
    static uint16_t sequence = 0;
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    
    std::lock_guard<std::mutex> lock(uuid_mutex);
    
    auto now = std::chrono::system_clock::now();
    uint64_t current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    if (current_ms <= last_ms) {
        sequence++;
    } else {
        last_ms = current_ms;
        sequence = 0; 
    }

    std::array<uint8_t, 16> bytes;

    // 48-bit unix_ts_ms (big-endian)
    bytes[0] = (last_ms >> 40) & 0xFF;
    bytes[1] = (last_ms >> 32) & 0xFF;
    bytes[2] = (last_ms >> 24) & 0xFF;
    bytes[3] = (last_ms >> 16) & 0xFF;
    bytes[4] = (last_ms >> 8) & 0xFF;
    bytes[5] = last_ms & 0xFF;

    // 4-bit version (0111) and 12-bit sequence
    uint16_t sequence_and_version = sequence & 0x0FFF;
    bytes[6] = 0x70 | (sequence_and_version >> 8);
    bytes[7] = sequence_and_version & 0xFF;

    // 2-bit variant (10) and 62-bits of random data
    uint64_t rand_data = gen();
    bytes[8] = 0x80 | ((rand_data >> 56) & 0x3F);
    bytes[9] = (rand_data >> 48) & 0xFF;
    bytes[10] = (rand_data >> 40) & 0xFF;
    bytes[11] = (rand_data >> 32) & 0xFF;
    bytes[12] = (rand_data >> 24) & 0xFF;
    bytes[13] = (rand_data >> 16) & 0xFF;
    bytes[14] = (rand_data >> 8) & 0xFF;
    bytes[15] = rand_data & 0xFF;

    // Format to string
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) ss << '-';
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    
    return ss.str();
}

std::string AsyncQueueManager::generate_transaction_id() {
    return generate_uuid();
}

// --- Consumer Group Subscription Metadata ---

void AsyncQueueManager::delete_consumer_group(
    const std::string& consumer_group,
    bool delete_metadata
) {
    auto conn = async_db_pool_->acquire();
    if (!conn) {
        throw std::runtime_error("Failed to acquire DB connection");
    }
    
    try {
        // Delete partition consumers
        std::string delete_pc_sql = R"(
            DELETE FROM queen.partition_consumers
            WHERE consumer_group = $1
        )";
        sendQueryParamsAsync(conn.get(), delete_pc_sql, {consumer_group});
        getCommandResult(conn.get());
        
        // Delete metadata if requested
        if (delete_metadata) {
            std::string delete_metadata_sql = R"(
                DELETE FROM queen.consumer_groups_metadata
                WHERE consumer_group = $1
            )";
            sendQueryParamsAsync(conn.get(), delete_metadata_sql, {consumer_group});
            getCommandResult(conn.get());
            spdlog::info("Deleted consumer group '{}' and its metadata", consumer_group);
        } else {
            spdlog::info("Deleted consumer group '{}' (metadata preserved)", consumer_group);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to delete consumer group '{}': {}", consumer_group, e.what());
        throw;
    }
}

void AsyncQueueManager::update_consumer_group_subscription(
    const std::string& consumer_group,
    const std::string& new_timestamp
) {
    auto conn = async_db_pool_->acquire();
    if (!conn) {
        throw std::runtime_error("Failed to acquire DB connection");
    }
    
    try {
        std::string update_sql = R"(
            UPDATE queen.consumer_groups_metadata
            SET subscription_timestamp = $2::timestamptz
            WHERE consumer_group = $1
        )";
        
        sendQueryParamsAsync(conn.get(), update_sql, {consumer_group, new_timestamp});
        getCommandResult(conn.get());
        
        spdlog::info("Updated subscription timestamp for consumer group '{}' to {}", 
                    consumer_group, new_timestamp);
    } catch (const std::exception& e) {
        spdlog::error("Failed to update subscription timestamp for '{}': {}", 
                     consumer_group, e.what());
        throw;
    }
}

void AsyncQueueManager::record_consumer_group_subscription(
    const std::string& consumer_group,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& namespace_name,
    const std::string& task_name,
    const std::string& subscription_mode,
    const std::string& subscription_timestamp_sql
) {
    auto conn = async_db_pool_->acquire();
    if (!conn) {
        spdlog::warn("Failed to acquire DB connection for recording CG subscription");
        return; // Non-fatal - continue with pop
    }
    
    std::string upsert_sql = R"(
        INSERT INTO queen.consumer_groups_metadata (
            consumer_group, queue_name, partition_name, namespace, task,
            subscription_mode, subscription_timestamp, created_at
        ) VALUES (
            $1, $2, $3, $4, $5, $6, 
            )" + subscription_timestamp_sql + R"(, 
            NOW()
        ) ON CONFLICT (consumer_group, queue_name, partition_name, namespace, task)
        DO NOTHING
    )";
    
    std::vector<std::string> params = {
        consumer_group,
        queue_name,
        partition_name,
        namespace_name,
        task_name,
        subscription_mode
    };
    
    try {
        sendQueryParamsAsync(conn.get(), upsert_sql, params);
        getCommandResult(conn.get());
        spdlog::debug("Recorded CG subscription: group={}, mode={}, queue={}, partition={}", 
                     consumer_group, subscription_mode, queue_name, partition_name);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to record CG subscription (non-fatal): {}", e.what());
        // Non-fatal - continue with pop even if metadata recording fails
    }
}

// --- Schema Initialization ---

namespace {

// Helper: Read file contents
std::string read_sql_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open SQL file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Helper: Execute SQL (blocking, handles multi-statement)
bool exec_sql(PGconn* conn, const std::string& sql, const std::string& context = "") {
    if (!PQsendQuery(conn, sql.c_str())) {
        spdlog::error("[{}] PQsendQuery failed: {}", context, PQerrorMessage(conn));
        return false;
    }
    
    while (PQisBusy(conn)) {
        if (!PQconsumeInput(conn)) {
            spdlog::error("[{}] PQconsumeInput failed: {}", context, PQerrorMessage(conn));
            return false;
        }
    }
    
    bool success = true;
    PGresult* res;
    while ((res = PQgetResult(conn)) != nullptr) {
        auto status = PQresultStatus(res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            spdlog::error("[{}] SQL failed: {}", context, PQresultErrorMessage(res));
            success = false;
        }
        PQclear(res);
    }
    return success;
}

// Helper: Query single integer value
int query_int(PGconn* conn, const std::string& sql, int default_val = -1) {
    PGresult* res = PQexec(conn, sql.c_str());
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return default_val;
    }
    int val = default_val;
    if (PQntuples(res) > 0 && PQnfields(res) > 0) {
        val = std::atoi(PQgetvalue(res, 0, 0));
    }
    PQclear(res);
    return val;
}

// Helper: Get sorted SQL files from directory
std::vector<std::string> get_sql_files(const std::string& dir) {
    std::vector<std::string> files;
    if (!std::filesystem::exists(dir)) return files;
    
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".sql") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // anonymous namespace

bool AsyncQueueManager::initialize_schema() {
    try {
        auto conn = async_db_pool_->acquire();
        if (!conn) {
            spdlog::error("Failed to acquire connection for schema initialization");
            return false;
        }
        
        spdlog::info("Initializing schema: {}", schema_name_);
        
        // Get base paths (relative to executable or absolute)
        std::string schema_dir = "schema";
        std::string migrations_dir = "migrations";
        
        // Check common locations
        for (const auto& base : {".", "..", "server", "../server"}) {
            if (std::filesystem::exists(std::string(base) + "/schema/schema.sql")) {
                schema_dir = std::string(base) + "/schema";
                migrations_dir = std::string(base) + "/migrations";
                break;
            }
        }
        
        // Create queen schema first
        if (!exec_sql(conn.get(), "CREATE SCHEMA IF NOT EXISTS queen", "create schema")) {
            spdlog::error("Failed to create queen schema");
            return false;
        }

        // If no schema version, this is a fresh install - run base schema
        spdlog::info("Running base schema initialization...");
            
        std::string schema_file = schema_dir + "/schema.sql";
        if (!std::filesystem::exists(schema_file)) {
            spdlog::error("Base schema file not found: {}", schema_file);
            // Fall back to legacy embedded schema
            throw std::runtime_error("Base schema file not found: " + schema_file);
        }
            
        // Load and execute base schema
        std::string schema_sql = read_sql_file(schema_file);
        if (!exec_sql(conn.get(), schema_sql, "base schema")) {
            spdlog::error("Failed to apply base schema");
            return false;
        }
        spdlog::info("Base schema applied successfully");

        
        // Always load/update stored procedures (they use CREATE OR REPLACE, so idempotent)
        {
            std::string procedures_dir = schema_dir + "/procedures";
            auto procedure_files = get_sql_files(procedures_dir);
            if (!procedure_files.empty()) {
                spdlog::info("Loading {} stored procedure(s)...", procedure_files.size());
                for (const auto& proc_file : procedure_files) {
                    spdlog::debug("Loading stored procedure: {}", proc_file);
                    std::string proc_sql = read_sql_file(proc_file);
                    if (!exec_sql(conn.get(), proc_sql, proc_file)) {
                        spdlog::error("Failed to load procedure: {}", proc_file);
                        return false;
                    }
                }
            }
        }

        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize schema: {}", e.what());
        return false;
    }
}

// --- Maintenance Mode ---

bool AsyncQueueManager::check_maintenance_mode_with_cache() {
    // Check if cache is still valid (reduces DB churn from 10 queries/sec to 1 query/sec)
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    
    uint64_t last_check = last_maintenance_check_ms_.load();
    if (last_check > 0 && (now_ms - last_check) < MAINTENANCE_CACHE_TTL_MS) {
        // Cache is still valid, return cached value
        return maintenance_mode_cached_.load();
    }
    
    // Cache expired or first check - query database
    try {
        auto conn = async_db_pool_->acquire();
        
        std::string sql = R"(
            SELECT value->>'enabled' as enabled
            FROM queen.system_state
            WHERE key = 'maintenance_mode'
        )";
        
        sendAndWait(conn.get(), sql.c_str());
        auto result = getTuplesResult(conn.get());
        
        bool enabled = false;
        if (PQntuples(result.get()) > 0) {
            const char* val = PQgetvalue(result.get(), 0, 0);
            enabled = (val && std::string(val) == "true");
        }
        
        // Update cache
        maintenance_mode_cached_.store(enabled);
        last_maintenance_check_ms_.store(now_ms);
        
        return enabled;
    } catch (const std::exception& e) {
        spdlog::error("Failed to check maintenance mode from database: {}", e.what());
        // On DB error, return cached value as fallback
        return maintenance_mode_cached_.load();
    }
}

// --- Queue/Partition Management ---

bool AsyncQueueManager::ensure_queue_exists(PGconn* conn,
                                           const std::string& queue_name, 
                                           const std::string& namespace_name,
                                           const std::string& task_name) {
    try {
        std::string sql = R"(
            INSERT INTO queen.queues (name, namespace, task, priority, lease_time, retry_limit, retry_delay, max_queue_size, ttl)
            VALUES ($1, $2, $3, 0, 300, 3, 1000, 0, 3600)
            ON CONFLICT (name) DO NOTHING
        )";
        
        std::vector<std::string> params = {
            queue_name,
            namespace_name.empty() ? "" : namespace_name,
            task_name.empty() ? "" : task_name
        };
        
        sendQueryParamsAsync(conn, sql, params);
        getCommandResult(conn);
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to ensure queue exists: {}", e.what());
        return false;
    }
}

bool AsyncQueueManager::ensure_partition_exists(PGconn* conn,
                                               const std::string& queue_name, 
                                               const std::string& partition_name) {
    try {
        spdlog::debug("Ensuring partition exists: queue='{}', partition='{}'", queue_name, partition_name);
        
        std::string sql = R"(
            INSERT INTO queen.partitions (queue_id, name)
            SELECT id, $2 FROM queen.queues WHERE name = $1
            ON CONFLICT (queue_id, name) DO NOTHING
            RETURNING id, name
        )";
        
        std::vector<std::string> params = {queue_name, partition_name};
        sendQueryParamsAsync(conn, sql, params);
        
        auto result = getTuplesResult(conn);
        
        if (PQntuples(result.get()) > 0) {
            spdlog::debug("Partition created: id='{}', name='{}'", 
                         PQgetvalue(result.get(), 0, 0), 
                         PQgetvalue(result.get(), 0, 1));
        } else {
            spdlog::debug("Partition already exists (ON CONFLICT triggered)");
        }
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to ensure partition exists: {}", e.what());
        return false;
    }
}

// --- Helper for FileBufferManager ---

void AsyncQueueManager::push_single_message(
    const std::string& queue_name,
    const std::string& partition_name,
    const nlohmann::json& payload,
    const std::string& namespace_name,
    const std::string& task,
    const std::string& transaction_id,
    const std::string& trace_id
) {
    auto conn = async_db_pool_->acquire();
    
    // Generate transaction ID if not provided
    std::string tx_id = transaction_id.empty() ? generate_uuid() : transaction_id;
    
    // Ensure queue exists
    ensure_queue_exists(conn.get(), queue_name, namespace_name, task);
    
    // Ensure partition exists
    ensure_partition_exists(conn.get(), queue_name, partition_name);
    
    // Get next sequence number
    std::string seq_sql = R"(
        SELECT COALESCE(MAX(sequence), 0) + 1 as next_seq
        FROM queen.messages
        WHERE queue_name = $1 AND partition_name = $2
    )";
    
    sendQueryParamsAsync(conn.get(), seq_sql, {queue_name, partition_name});
    auto seq_result = getTuplesResult(conn.get());
    
    int64_t sequence = 1;
    if (PQntuples(seq_result.get()) > 0) {
        const char* seq_str = PQgetvalue(seq_result.get(), 0, 0);
        sequence = seq_str ? std::stoll(seq_str) : 1;
    }
    
    // Serialize payload
    std::string payload_str = payload.dump();
    
    // Insert message
    std::string insert_sql = R"(
        INSERT INTO queen.messages 
        (queue_name, partition_name, payload, transaction_id, trace_id, 
         namespace, task, status, sequence, created_at)
        VALUES ($1, $2, $3, $4, $5, $6, $7, 'pending', $8, NOW())
    )";
    
    sendQueryParamsAsync(conn.get(), insert_sql, {
        queue_name,
        partition_name,
        payload_str,
        tx_id,
        trace_id,
        namespace_name,
        task,
        std::to_string(sequence)
    });
    
    getCommandResult(conn.get());
}

// --- Push Single Message ---

PushResult AsyncQueueManager::push_single_message(const PushItem& item) {
    PushResult result;
    result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
    
    // MAINTENANCE MODE: Route to file buffer
    if (check_maintenance_mode_with_cache() && file_buffer_manager_) {
        try {
            nlohmann::json event = {
                {"queue", item.queue},
                {"partition", item.partition},
                {"payload", item.payload},
                {"transactionId", result.transaction_id},
                {"failover", true}
            };
            
            if (item.trace_id.has_value() && !item.trace_id->empty()) {
                event["traceId"] = *item.trace_id;
            }
            
            if (file_buffer_manager_->write_event(event)) {
                result.status = "buffered";
                result.message_id = std::nullopt;
                result.trace_id = item.trace_id;
                return result;
            } else {
                result.status = "failed";
                result.error = "File buffer write failed";
                return result;
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to write to file buffer during maintenance mode: {}", e.what());
            result.status = "failed";
            result.error = std::string("Buffer write error: ") + e.what();
            return result;
        }
    }
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Check if queue exists
        sendQueryParamsAsync(conn.get(), "SELECT id FROM queen.queues WHERE name = $1", {item.queue});
        auto queue_check = getTuplesResult(conn.get());
        
        if (PQntuples(queue_check.get()) == 0) {
            result.status = "failed";
            result.error = "Queue '" + item.queue + "' does not exist. Please create it using the configure endpoint first.";
            return result;
        }
        
        // Ensure partition exists
        if (!ensure_partition_exists(conn.get(), item.queue, item.partition)) {
            result.status = "failed";
            result.error = "Failed to create partition";
            return result;
        }
        
        // Generate UUIDv7 for message ID
        std::string message_id = generate_uuid();
        
        std::string sql;
        std::vector<std::string> params;
        
        if (item.trace_id.has_value() && !item.trace_id->empty()) {
            sql = R"(
                INSERT INTO queen.messages (
                    id, transaction_id, partition_id, payload, trace_id, created_at
                )
                SELECT $1, $2, p.id, $3, $4, NOW()
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $5 AND p.name = $6
                RETURNING id, trace_id
            )";
            
            params = {
                message_id,
                result.transaction_id,
                item.payload.dump(),
                item.trace_id.value(),
                item.queue,
                item.partition
            };
        } else {
            sql = R"(
                INSERT INTO queen.messages (
                    id, transaction_id, partition_id, payload, created_at
                )
                SELECT $1, $2, p.id, $3, NOW()
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $4 AND p.name = $5
                RETURNING id, trace_id
            )";
            
            params = {
                message_id,
                result.transaction_id,
                item.payload.dump(),
                item.queue,
                item.partition
            };
        }
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto query_result = getTuplesResult(conn.get());
        
        if (PQntuples(query_result.get()) > 0) {
            result.status = "queued";
            result.message_id = PQgetvalue(query_result.get(), 0, 0);
            const char* trace_val = PQgetvalue(query_result.get(), 0, 1);
            if (trace_val && std::string(trace_val).length() > 0) {
                result.trace_id = trace_val;
            }
        } else {
            result.status = "failed";
            result.error = "Insert returned no result";
        }
        
    } catch (const std::exception& e) {
        // Database push failed - try file buffer failover
        spdlog::warn("Database push failed: {}. Attempting file buffer failover...", e.what());
        
        if (file_buffer_manager_) {
            // Mark database as unhealthy for faster failover on subsequent requests
            file_buffer_manager_->mark_db_unhealthy();
            
            try {
                nlohmann::json event = {
                    {"queue", item.queue},
                    {"partition", item.partition},
                    {"payload", item.payload},
                    {"transactionId", result.transaction_id},
                    {"failover", true}  // Failover buffer (FIFO, will be replayed when DB recovers)
                };
                
                if (item.trace_id.has_value() && !item.trace_id->empty()) {
                    event["traceId"] = *item.trace_id;
                }
                
                if (file_buffer_manager_->write_event(event)) {
                    spdlog::info("Message successfully failed over to file buffer");
                    result.status = "buffered";
                    result.message_id = std::nullopt;
                    result.trace_id = item.trace_id;
                    result.error = std::nullopt;  // Clear error since failover succeeded
                    return result;
                } else {
                    spdlog::error("File buffer write failed during failover");
                    result.status = "failed";
                    result.error = std::string("Database error: ") + e.what() + "; File buffer also failed";
                    return result;
                }
            } catch (const std::exception& fb_error) {
                spdlog::error("File buffer failover exception: {}", fb_error.what());
                result.status = "failed";
                result.error = std::string("Database error: ") + e.what() + "; Failover error: " + fb_error.what();
                return result;
            }
        } else {
            // No file buffer configured - return database error
            spdlog::error("Database push failed and no file buffer configured: {}", e.what());
            result.status = "failed";
            result.error = e.what();
            return result;
        }
    }
    
    return result;
}
// --- Push Messages Internal (Bypasses Maintenance Mode) ---

std::vector<PushResult> AsyncQueueManager::push_messages_internal(const std::vector<PushItem>& items) {
    if (items.empty()) {
        return {};
    }
    
    // NOTE: This method NEVER checks maintenance mode
    // It's used ONLY for internal operations like file buffer drain
    // DO NOT call from user-facing code paths!
    
    spdlog::debug("push_messages_internal: Processing {} items (bypassing maintenance mode)", items.size());
    
    // Group items by queue and partition while PRESERVING ORDER
    std::vector<std::pair<std::string, std::vector<size_t>>> partition_groups_ordered;
    std::map<std::string, size_t> key_to_index;
    
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        std::string key = item.queue + ":" + item.partition;
        
        if (key_to_index.find(key) == key_to_index.end()) {
            key_to_index[key] = partition_groups_ordered.size();
            partition_groups_ordered.push_back({key, {i}});
        } else {
            size_t group_idx = key_to_index[key];
            partition_groups_ordered[group_idx].second.push_back(i);
        }
    }
    
    std::vector<PushResult> all_results(items.size());
    
    // Batch capacity check - check ALL queues in ONE query
    try {
        // Build map of queue -> total batch size across all partitions
        std::map<std::string, int> queue_batch_sizes;
        for (const auto& [partition_key, item_indices] : partition_groups_ordered) {
            size_t colon_pos = partition_key.find(':');
            std::string queue_name = partition_key.substr(0, colon_pos);
            queue_batch_sizes[queue_name] += item_indices.size();
        }
        
        // Check capacity for all queues with max_queue_size set
        if (!queue_batch_sizes.empty()) {
            std::vector<std::string> queues_to_check;
            for (const auto& [queue_name, _] : queue_batch_sizes) {
                queues_to_check.push_back(queue_name);
            }
            
            // Build PostgreSQL array
            auto build_pg_array = [](const std::vector<std::string>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    result += "\"";
                    for (char c : vec[i]) {
                        if (c == '\\' || c == '"') result += '\\';
                        result += c;
                    }
                    result += "\"";
                }
                result += "}";
                return result;
            };
            
            std::string queues_array = build_pg_array(queues_to_check);
            
            auto capacity_conn = async_db_pool_->acquire();
            std::string capacity_check_sql = R"(
                SELECT 
                  q.name,
                  q.max_queue_size,
                  (
                    SELECT COUNT(m.id)::integer
                    FROM queen.messages m
                    JOIN queen.partitions p ON p.id = m.partition_id
                    LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                      AND pc.consumer_group = '__QUEUE_MODE__'
                    WHERE p.queue_id = q.id
                           AND (pc.last_consumed_created_at IS NULL 
                                OR m.created_at > pc.last_consumed_created_at
                           OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
                  ) as current_depth
                FROM queen.queues q
                WHERE q.name = ANY($1::varchar[])
                  AND q.max_queue_size > 0
            )";
            
            sendQueryParamsAsync(capacity_conn.get(), capacity_check_sql, {queues_array});
            auto capacity_result = getTuplesResult(capacity_conn.get());
            
            // Check each queue's capacity
            for (int i = 0; i < PQntuples(capacity_result.get()); ++i) {
                std::string queue_name = PQgetvalue(capacity_result.get(), i, 0);
                const char* max_size_str = PQgetvalue(capacity_result.get(), i, 1);
                const char* current_depth_str = PQgetvalue(capacity_result.get(), i, 2);
                
                int max_size = (max_size_str && strlen(max_size_str) > 0) ? std::stoi(max_size_str) : 0;
                int current_depth = (current_depth_str && strlen(current_depth_str) > 0) ? std::stoi(current_depth_str) : 0;
                int batch_size = queue_batch_sizes[queue_name];
                
                if (current_depth + batch_size > max_size) {
                    // Queue would exceed capacity - fail all messages for this queue
                    std::string error_msg = "Queue '" + queue_name + "' would exceed max capacity (" +
                                          std::to_string(max_size) + "). Current: " + 
                                          std::to_string(current_depth) + ", Batch: " + 
                                          std::to_string(batch_size);
                    
                    spdlog::warn(error_msg);
                    
                    // Find all items for this queue and mark them as failed
                    for (size_t item_idx = 0; item_idx < items.size(); ++item_idx) {
                        if (items[item_idx].queue == queue_name) {
                            PushResult failed_result;
                            failed_result.transaction_id = items[item_idx].transaction_id.value_or(generate_transaction_id());
                            failed_result.status = "failed";
                            failed_result.error = error_msg;
                            all_results[item_idx] = failed_result;
                        }
                    }
                    
                    // Remove this queue from partition_groups_ordered
                    partition_groups_ordered.erase(
                        std::remove_if(partition_groups_ordered.begin(), partition_groups_ordered.end(),
                            [&queue_name](const auto& pair) {
                                return pair.first.substr(0, pair.first.find(':')) == queue_name;
                            }),
                        partition_groups_ordered.end()
                    );
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Batch capacity check failed: {}", e.what());
        // Continue with push - individual checks will catch issues
    }
    
    // Process each partition group using stored procedure
    for (const auto& [partition_key, item_indices] : partition_groups_ordered) {
    try {
        auto conn = async_db_pool_->acquire();
        
            // Build JSON array for stored procedure
            nlohmann::json messages_array = nlohmann::json::array();
            
            for (size_t idx : item_indices) {
                const auto& item = items[idx];
                std::string msg_id = generate_uuid();
                std::string txn_id = item.transaction_id.value_or(generate_transaction_id());
                
                // Check encryption
        bool encryption_enabled = false;
                std::string payload_str = item.payload.dump();
        
        if (global_shared_state && global_shared_state->is_enabled()) {
                    auto cached_config = global_shared_state->get_queue_config(item.queue);
            if (cached_config) {
                encryption_enabled = cached_config->encryption_enabled;
                    }
            }
                
                bool is_encrypted = false;
                if (encryption_enabled) {
                    EncryptionService* enc_service = get_encryption_service();
                if (enc_service && enc_service->is_enabled()) {
                        auto encrypted = enc_service->encrypt_payload(payload_str);
                    if (encrypted.has_value()) {
                        nlohmann::json encrypted_json = {
                            {"encrypted", encrypted->encrypted},
                            {"iv", encrypted->iv},
                            {"authTag", encrypted->auth_tag}
                        };
                            payload_str = encrypted_json.dump();
                        is_encrypted = true;
                        }
                    }
                }
                
                nlohmann::json msg_obj;
                msg_obj["messageId"] = msg_id;
                msg_obj["transactionId"] = txn_id;
                msg_obj["queue"] = item.queue;
                msg_obj["partition"] = item.partition;
                msg_obj["payload"] = payload_str;
                msg_obj["is_encrypted"] = is_encrypted;
                if (item.trace_id.has_value() && !item.trace_id->empty()) {
                    msg_obj["traceId"] = *item.trace_id;
                }
                
                messages_array.push_back(msg_obj);
                
                // Pre-populate result
                PushResult result;
                result.transaction_id = txn_id;
                result.message_id = msg_id;
                result.status = "queued";
                all_results[idx] = result;
                }
                
            // Call stored procedure
            std::string sql = "SELECT queen.push_messages_v2($1::jsonb)";
            sendQueryParamsAsync(conn.get(), sql, {messages_array.dump()});
            auto result = getTuplesResult(conn.get());
            
            if (PQntuples(result.get()) > 0) {
                const char* result_str = PQgetvalue(result.get(), 0, 0);
                if (result_str && strlen(result_str) > 0) {
                    try {
                        nlohmann::json sp_result = nlohmann::json::parse(result_str);
                        
                        if (sp_result.contains("error")) {
                            std::string error_msg = sp_result["error"].get<std::string>();
                            for (size_t idx : item_indices) {
                                all_results[idx].status = "failed";
                                all_results[idx].error = error_msg;
                    }
                }
                    } catch (...) {
                        // SP returned success without detailed JSON
                    }
                }
            }
            
        } catch (const std::exception& e) {
            spdlog::error("push_messages_internal failed for partition group: {}", e.what());
            for (size_t idx : item_indices) {
                all_results[idx].status = "failed";
                all_results[idx].error = e.what();
            }
        }
    }
    
    return all_results;
        }
        
// --- Helper: Check if error is connection-related (not retryable via cache invalidation) ---
static bool is_connection_error(const std::string& error_msg) {
    // Connection errors should NOT trigger cache invalidation retry
    // These indicate the DB is down, not that cache is stale
    return error_msg.find("connection") != std::string::npos ||
           error_msg.find("Connection") != std::string::npos ||
           error_msg.find("server closed") != std::string::npos ||
           error_msg.find("could not connect") != std::string::npos ||
           error_msg.find("timeout") != std::string::npos ||
           error_msg.find("SSL") != std::string::npos;
}

// --- Pool Statistics ---

AsyncQueueManager::PoolStats AsyncQueueManager::get_pool_stats() const {
    return {
        async_db_pool_->size(),
        async_db_pool_->available(),
        async_db_pool_->size() - async_db_pool_->available()
    };
}

// --- Health Check ---

bool AsyncQueueManager::health_check() {
    try {
        auto conn = async_db_pool_->acquire();
        sendAndWait(conn.get(), "SELECT 1");
        auto result = getTuplesResult(conn.get());
        return PQntuples(result.get()) > 0;
    } catch (const std::exception& e) {
        spdlog::error("Health check failed: {}", e.what());
        return false;
    }
}

// --- Pop using Stored Procedure (Single Round Trip) ---
// HIGH-PERFORMANCE version used by poll workers for wait=true long-polling

PopResult AsyncQueueManager::pop_messages_sp(
    const std::string& queue_name,
    const std::string& partition_name,  // Empty for any partition
    const std::string& consumer_group,
    const PopOptions& options
) {
    PopResult result;
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Call the stored procedure
        std::string sql = "SELECT queen.pop_messages_v2($1, $2, $3, $4, $5, $6, $7)";
        std::vector<std::string> params = {
            queue_name,
            partition_name,  // Empty string will be treated as NULL by SP
            consumer_group,
            std::to_string(options.batch),
            "0",  // lease_time_seconds = 0 means use queue config
            options.subscription_mode.value_or("all"),
            options.subscription_from.value_or("")
        };
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto query_result = getTuplesResult(conn.get());
        
        if (PQntuples(query_result.get()) == 0) {
            return result;
        }
        
        // Parse JSON result
        const char* json_str = PQgetvalue(query_result.get(), 0, 0);
        if (!json_str || strlen(json_str) == 0) {
            return result;
        }
        
        auto json_result = nlohmann::json::parse(json_str);
        
        // Extract lease ID
        if (json_result.contains("leaseId") && !json_result["leaseId"].is_null()) {
            result.lease_id = json_result["leaseId"].get<std::string>();
        }
        
        // Extract messages
        if (!json_result.contains("messages") || !json_result["messages"].is_array()) {
            return result;
        }
        
        auto& messages_json = json_result["messages"];
        if (messages_json.empty()) {
            return result;
        }
        
        // Get encryption service if needed
        EncryptionService* enc_service = get_encryption_service();
        
        // Parse each message
        for (const auto& msg_json : messages_json) {
            Message msg;
            
            msg.id = msg_json.value("id", "");
            msg.transaction_id = msg_json.value("transactionId", "");
            msg.partition_id = msg_json.value("partitionId", "");
            msg.queue_name = msg_json.value("queue", "");
            msg.partition_name = msg_json.value("partition", "");
            msg.retry_count = msg_json.value("retryCount", 0);
            msg.priority = msg_json.value("priority", 0);
            
            // Parse trace_id
            if (msg_json.contains("traceId") && !msg_json["traceId"].is_null()) {
                msg.trace_id = msg_json["traceId"].get<std::string>();
            }
            
            // Parse created_at timestamp
            if (msg_json.contains("createdAt") && !msg_json["createdAt"].is_null()) {
                std::string ts_str = msg_json["createdAt"].get<std::string>();
                // Parse ISO8601 timestamp
                std::tm tm = {};
                int ms = 0;
                if (sscanf(ts_str.c_str(), "%d-%d-%dT%d:%d:%d.%dZ",
                          &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                          &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) >= 6) {
                    tm.tm_year -= 1900;
                    tm.tm_mon -= 1;
                    time_t time = timegm(&tm);
                    msg.created_at = std::chrono::system_clock::from_time_t(time) + 
                                    std::chrono::milliseconds(ms);
            }
            }
            
            // Handle payload/data - check if encrypted
            nlohmann::json payload_data;
            if (msg_json.contains("data")) {
                payload_data = msg_json["data"];
            }
            
            // Check if encrypted and decrypt
            if (payload_data.is_object() && 
                payload_data.contains("encrypted") && 
                payload_data.contains("iv") && 
                payload_data.contains("authTag") &&
                enc_service && enc_service->is_enabled()) {
                try {
                    EncryptionService::EncryptedData encrypted_data{
                        payload_data["encrypted"].get<std::string>(),
                        payload_data["iv"].get<std::string>(),
                        payload_data["authTag"].get<std::string>()
                    };
                    auto decrypted = enc_service->decrypt_payload(encrypted_data);
                    if (decrypted.has_value()) {
                        msg.payload = nlohmann::json::parse(decrypted.value());
                    } else {
                        msg.payload = payload_data;
                    }
                } catch (...) {
                    msg.payload = payload_data;
                }
            } else {
                msg.payload = payload_data;
            }
            
            result.messages.push_back(msg);
        }
        
        // Auto-ack if enabled - use stored procedure directly
        if (options.auto_ack && !result.messages.empty() && result.lease_id.has_value()) {
            spdlog::debug("Auto-ack enabled - acknowledging {} messages via stored procedure", result.messages.size());
            
            try {
                // Build JSON array for ack stored procedure
                nlohmann::json ack_array = nlohmann::json::array();
                for (const auto& msg : result.messages) {
                    nlohmann::json ack_obj;
                    ack_obj["transaction_id"] = msg.transaction_id;
                    ack_obj["partition_id"] = msg.partition_id;
                    ack_obj["consumer_group"] = consumer_group;
                    ack_obj["status"] = "completed";
                    ack_array.push_back(ack_obj);
                }
                
                std::string ack_sql = "SELECT queen.ack_messages_v2($1::jsonb)";
                sendQueryParamsAsync(conn.get(), ack_sql, {ack_array.dump()});
                getTuplesResult(conn.get());
        
                spdlog::debug("Auto-ack completed for {} messages", result.messages.size());
    } catch (const std::exception& e) {
                spdlog::error("Auto-ack failed: {}", e.what());
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("pop_messages_sp failed: {}", e.what());
    }
    
    return result;
}


// ===================================
// MAINTENANCE MODE
// ===================================

void AsyncQueueManager::set_maintenance_mode(bool enabled) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Persist to database for multi-instance support
        std::string sql = R"(
            INSERT INTO queen.system_state (key, value, updated_at)
            VALUES ('maintenance_mode', $1::jsonb, NOW())
            ON CONFLICT (key) DO UPDATE
            SET value = EXCLUDED.value,
                updated_at = NOW()
        )";
        
        nlohmann::json value = {{"enabled", enabled}};
        sendQueryParamsAsync(conn.get(), sql, {value.dump()});
        getCommandResult(conn.get());
        
        // Update cache immediately on this instance
        maintenance_mode_cached_.store(enabled);
        last_maintenance_check_ms_.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        // Handle file buffer state changes
        if (file_buffer_manager_) {
            if (enabled) {
                // ENABLE: Pause background drain to prevent infinite loop
                file_buffer_manager_->pause_background_drain();
                spdlog::info("Maintenance mode ENABLED - PUSHes routing to file buffer, drain paused");
            } else {
                // DISABLE: Force finalize .tmp files and resume drain
                spdlog::info("Maintenance mode DISABLED - finalizing buffered messages and resuming drain");
                
                // Step 1: Force finalize all .tmp files to make messages visible
                file_buffer_manager_->force_finalize_all();
                
                // Step 2: Resume background drain (will wake processor)
                file_buffer_manager_->resume_background_drain();
                
                size_t pending = file_buffer_manager_->get_pending_count();
                spdlog::info("File buffer will drain {} pending messages to database", pending);
            }
        } else {
            spdlog::info("Maintenance mode {} (persisted to database for all instances)", 
                        enabled ? "ENABLED - routing PUSHes to file buffer" : "DISABLED - resuming normal operations");
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to set maintenance mode: {}", e.what());
        throw;
    }
}

bool AsyncQueueManager::get_maintenance_mode_fresh() {
    // Force cache invalidation by setting last_check to 0
    last_maintenance_check_ms_.store(0);
    return check_maintenance_mode_with_cache();
}

size_t AsyncQueueManager::get_buffer_pending_count() const {
    if (!file_buffer_manager_) {
        return 0;
    }
    return file_buffer_manager_->get_pending_count();
}

bool AsyncQueueManager::is_buffer_healthy() const {
    if (!file_buffer_manager_) {
        return true;  // No buffer = healthy by default
    }
    return file_buffer_manager_->is_db_healthy();
}

nlohmann::json AsyncQueueManager::get_buffer_stats() const {
    if (!file_buffer_manager_) {
        return {
            {"pendingCount", 0},
            {"failedCount", 0},
            {"dbHealthy", true},
            {"failedFiles", {
                {"count", 0},
                {"totalBytes", 0},
                {"totalMB", 0.0},
                {"failoverCount", 0},
                {"qos0Count", 0}
            }}
        };
    }
    
    auto failed_stats = file_buffer_manager_->get_failed_files_stats();
    double total_mb = failed_stats.total_bytes / (1024.0 * 1024.0);
    
    return {
        {"pendingCount", file_buffer_manager_->get_pending_count()},
        {"failedCount", file_buffer_manager_->get_failed_count()},
        {"dbHealthy", file_buffer_manager_->is_db_healthy()},
        {"failedFiles", {
            {"count", failed_stats.file_count},
            {"totalBytes", failed_stats.total_bytes},
            {"totalMB", total_mb},
            {"failoverCount", failed_stats.failover_count},
            {"qos0Count", failed_stats.qos0_count}
        }}
    };
}

// ===================================
// QUEUE CONFIGURATION
// ===================================

bool AsyncQueueManager::configure_queue(const std::string& queue_name, 
                                       const QueueOptions& options,
                                       const std::string& namespace_name,
                                       const std::string& task_name) {
    try {
        auto conn = async_db_pool_->acquire();
        
        std::string sql = R"(
            INSERT INTO queen.queues (
                name, namespace, task, priority, lease_time, retry_limit, retry_delay,
                max_queue_size, ttl, dead_letter_queue, dlq_after_max_retries, delayed_processing,
                window_buffer, retention_seconds, completed_retention_seconds, 
                retention_enabled, encryption_enabled, max_wait_time_seconds
            ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18)
            ON CONFLICT (name) DO UPDATE SET
                namespace = EXCLUDED.namespace,
                task = EXCLUDED.task,
                priority = EXCLUDED.priority,
                lease_time = EXCLUDED.lease_time,
                retry_limit = EXCLUDED.retry_limit,
                retry_delay = EXCLUDED.retry_delay,
                max_queue_size = EXCLUDED.max_queue_size,
                ttl = EXCLUDED.ttl,
                dead_letter_queue = EXCLUDED.dead_letter_queue,
                dlq_after_max_retries = EXCLUDED.dlq_after_max_retries,
                delayed_processing = EXCLUDED.delayed_processing,
                window_buffer = EXCLUDED.window_buffer,
                retention_seconds = EXCLUDED.retention_seconds,
                completed_retention_seconds = EXCLUDED.completed_retention_seconds,
                retention_enabled = EXCLUDED.retention_enabled,
                encryption_enabled = EXCLUDED.encryption_enabled,
                max_wait_time_seconds = EXCLUDED.max_wait_time_seconds
        )";
        
        std::vector<std::string> params = {
            queue_name,
            namespace_name.empty() ? "" : namespace_name,
            task_name.empty() ? "" : task_name,
            std::to_string(options.priority),
            std::to_string(options.lease_time),
            std::to_string(options.retry_limit),
            std::to_string(options.retry_delay),
            std::to_string(options.max_size),
            std::to_string(options.ttl),
            options.dead_letter_queue ? "true" : "false",
            options.dlq_after_max_retries ? "true" : "false",
            std::to_string(options.delayed_processing),
            std::to_string(options.window_buffer),
            std::to_string(options.retention_seconds),
            std::to_string(options.completed_retention_seconds),
            options.retention_enabled ? "true" : "false",
            options.encryption_enabled ? "true" : "false",
            std::to_string(options.max_wait_time_seconds)
        };
        
        sendQueryParamsAsync(conn.get(), sql, params);
        getCommandResult(conn.get());
        
        spdlog::debug("Queue '{}' configured successfully in database", queue_name);
        
        // Ensure default partition exists
        ensure_partition_exists(conn.get(), queue_name, "Default");
        
        
        // Update queue config cache (needed for encryption, also broadcasts to peers if sync enabled)
        if (global_shared_state) {
            caches::CachedQueueConfig cached_config;
            cached_config.name = queue_name;
            cached_config.namespace_name = namespace_name;
            cached_config.task = task_name;
            cached_config.priority = options.priority;
            cached_config.lease_time = options.lease_time;
            cached_config.retry_limit = options.retry_limit;
            cached_config.retry_delay = options.retry_delay;
            cached_config.max_size = options.max_size;
            cached_config.ttl = options.ttl;
            cached_config.dlq_enabled = options.dead_letter_queue;
            cached_config.dlq_after_max_retries = options.dlq_after_max_retries;
            cached_config.delayed_processing = options.delayed_processing;
            cached_config.window_buffer = options.window_buffer;
            cached_config.retention_seconds = options.retention_seconds;
            cached_config.completed_retention_seconds = options.completed_retention_seconds;
            cached_config.encryption_enabled = options.encryption_enabled;
            global_shared_state->set_queue_config(queue_name, cached_config);
            spdlog::info("Queue '{}' config cached (encryption_enabled={})", queue_name, options.encryption_enabled);
        }
        
        spdlog::info("Queue '{}' fully configured with Default partition", queue_name);
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to configure queue '{}': {}", queue_name, e.what());
        return false;
    }
}

bool AsyncQueueManager::delete_queue(const std::string& queue_name) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Begin transaction and increase statement timeout for large queue deletions
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            // Set timeout to 10 minutes for this transaction
            sendAndWait(conn.get(), "SET LOCAL statement_timeout = '600000'");
            getCommandResult(conn.get());
            
            // First, delete consumer state for all partitions in this queue
            std::string delete_consumers_sql = R"(
                DELETE FROM queen.partition_consumers
                WHERE partition_id IN (
                    SELECT p.id FROM queen.partitions p
                    JOIN queen.queues q ON p.queue_id = q.id
                    WHERE q.name = $1
                )
            )";
            sendQueryParamsAsync(conn.get(), delete_consumers_sql, {queue_name});
            getCommandResult(conn.get());
            
            // Delete the queue (CASCADE will handle partitions and messages)
            std::string delete_queue_sql = "DELETE FROM queen.queues WHERE name = $1 RETURNING id";
            sendQueryParamsAsync(conn.get(), delete_queue_sql, {queue_name});
            auto result = getTuplesResult(conn.get());
            
            if (PQntuples(result.get()) == 0) {
                sendAndWait(conn.get(), "ROLLBACK");
                getCommandResult(conn.get());
                spdlog::warn("Queue not found: {}", queue_name);
                return false;
            }
            
            // Commit transaction
            sendAndWait(conn.get(), "COMMIT");
            getCommandResult(conn.get());
            
            // Broadcast queue deletion to peers (Phase 2: Queue Config Cache)
            if (global_shared_state && global_shared_state->is_enabled()) {
                global_shared_state->delete_queue_config(queue_name);
                spdlog::debug("Queue '{}' deletion broadcast to peers", queue_name);
            }
            
            spdlog::info("Queue deleted successfully: {}", queue_name);
            return true;
            
        } catch (const std::exception& e) {
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to delete queue: {}", e.what());
        return false;
    }
}
// ===================================
// MESSAGE TRACING
// ===================================

bool AsyncQueueManager::record_trace(
    const std::string& transaction_id,
    const std::string& partition_id,
    const std::string& consumer_group,
    const std::vector<std::string>& trace_names,
    const std::string& event_type,
    const nlohmann::json& data,
    const std::string& worker_id
) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Start transaction for atomicity
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            // Get message_id from transaction_id + partition_id
            std::string query = R"(
                SELECT id FROM queen.messages 
                WHERE transaction_id = $1 AND partition_id = $2
                LIMIT 1
            )";
            
            sendQueryParamsAsync(conn.get(), query, {transaction_id, partition_id});
            auto result = getTuplesResult(conn.get());
            
            if (PQntuples(result.get()) == 0) {
                spdlog::warn("Message not found for trace: {}/{}", partition_id, transaction_id);
                sendAndWait(conn.get(), "ROLLBACK");
                getCommandResult(conn.get());
                return false;
            }
            
            std::string message_id = PQgetvalue(result.get(), 0, 0);
            
            // Insert main trace record
            std::string insert_query = R"(
                INSERT INTO queen.message_traces 
                (message_id, partition_id, transaction_id, consumer_group, event_type, data, worker_id)
                VALUES ($1, $2, $3, $4, $5, $6, $7)
                RETURNING id
            )";
            
            sendQueryParamsAsync(conn.get(), insert_query, {
                message_id,
                partition_id,
                transaction_id,
                consumer_group,
                event_type,
                data.dump(),
                worker_id
            });
            auto insert_result = getTuplesResult(conn.get());
            
            if (PQntuples(insert_result.get()) == 0) {
                spdlog::error("Failed to insert trace");
                sendAndWait(conn.get(), "ROLLBACK");
                getCommandResult(conn.get());
                return false;
            }
            
            std::string trace_id = PQgetvalue(insert_result.get(), 0, 0);
            
            // Insert trace names (for filtering/searching)
            if (!trace_names.empty()) {
                for (const auto& name : trace_names) {
                    std::string name_insert = R"(
                        INSERT INTO queen.message_trace_names (trace_id, trace_name)
                        VALUES ($1, $2)
                        ON CONFLICT (trace_id, trace_name) DO NOTHING
                    )";
                    sendQueryParamsAsync(conn.get(), name_insert, {trace_id, name});
                    getCommandResult(conn.get());
                }
            }
            
            // Commit transaction
            sendAndWait(conn.get(), "COMMIT");
            getCommandResult(conn.get());
            
            return true;
            
        } catch (const std::exception& e) {
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to record trace: {}", e.what());
        return false;
    }
}

nlohmann::json AsyncQueueManager::get_message_traces(
    const std::string& partition_id,
    const std::string& transaction_id
) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Get traces with their associated names
        std::string query = R"(
            SELECT 
                mt.id,
                mt.event_type,
                mt.data,
                mt.consumer_group,
                mt.worker_id,
                mt.created_at,
                COALESCE(
                    json_agg(mtn.trace_name ORDER BY mtn.trace_name) 
                    FILTER (WHERE mtn.trace_name IS NOT NULL),
                    '[]'::json
                ) as trace_names
            FROM queen.message_traces mt
            LEFT JOIN queen.message_trace_names mtn ON mt.id = mtn.trace_id
            WHERE mt.partition_id = $1 AND mt.transaction_id = $2
            GROUP BY mt.id, mt.event_type, mt.data, mt.consumer_group, mt.worker_id, mt.created_at
            ORDER BY mt.created_at ASC
        )";
        
        sendQueryParamsAsync(conn.get(), query, {partition_id, transaction_id});
        auto result = getTuplesResult(conn.get());
        
        nlohmann::json traces = nlohmann::json::array();
        int num_rows = PQntuples(result.get());
        int num_cols = PQnfields(result.get());
        
        for (int i = 0; i < num_rows; i++) {
            nlohmann::json row;
            for (int j = 0; j < num_cols; j++) {
                std::string field_name = PQfname(result.get(), j);
                if (PQgetisnull(result.get(), i, j)) {
                    row[field_name] = nullptr;
                } else {
                    std::string value = PQgetvalue(result.get(), i, j);
                    // Try to parse JSON fields
                    if (field_name == "data" || field_name == "trace_names") {
                        try {
                            row[field_name] = nlohmann::json::parse(value);
                        } catch (...) {
                            row[field_name] = value;
                        }
                    } else {
                        row[field_name] = value;
                    }
                }
            }
            traces.push_back(row);
        }
        
        return {
            {"traces", traces},
            {"count", num_rows}
        };
    } catch (const std::exception& e) {
        spdlog::error("Failed to get traces: {}", e.what());
        throw;
    }
}

nlohmann::json AsyncQueueManager::get_traces_by_name(
    const std::string& trace_name,
    int limit,
    int offset
) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Query traces by any matching name
        std::string query = R"(
            SELECT 
                mt.id,
                mt.transaction_id,
                mt.partition_id,
                mt.event_type,
                mt.data,
                mt.consumer_group,
                mt.worker_id,
                mt.created_at,
                m.payload as message_payload,
                q.name as queue_name,
                p.name as partition_name,
                COALESCE(
                    json_agg(mtn2.trace_name ORDER BY mtn2.trace_name) 
                    FILTER (WHERE mtn2.trace_name IS NOT NULL),
                    '[]'::json
                ) as trace_names
            FROM queen.message_trace_names mtn
            JOIN queen.message_traces mt ON mtn.trace_id = mt.id
            LEFT JOIN queen.message_trace_names mtn2 ON mt.id = mtn2.trace_id
            LEFT JOIN queen.messages m ON mt.message_id = m.id
            LEFT JOIN queen.partitions p ON mt.partition_id = p.id
            LEFT JOIN queen.queues q ON p.queue_id = q.id
            WHERE mtn.trace_name = $1
            GROUP BY mt.id, mt.transaction_id, mt.partition_id, mt.event_type, 
                     mt.data, mt.consumer_group, mt.worker_id, mt.created_at,
                     m.payload, q.name, p.name
            ORDER BY mt.created_at ASC
            LIMIT $2 OFFSET $3
        )";
        
        sendQueryParamsAsync(conn.get(), query, {
            trace_name,
            std::to_string(limit),
            std::to_string(offset)
        });
        auto result = getTuplesResult(conn.get());
        
        nlohmann::json traces = nlohmann::json::array();
        int num_rows = PQntuples(result.get());
        int num_cols = PQnfields(result.get());
        
        for (int i = 0; i < num_rows; i++) {
            nlohmann::json row;
            for (int j = 0; j < num_cols; j++) {
                std::string field_name = PQfname(result.get(), j);
                if (PQgetisnull(result.get(), i, j)) {
                    row[field_name] = nullptr;
                } else {
                    std::string value = PQgetvalue(result.get(), i, j);
                    // Try to parse JSON fields
                    if (field_name == "data" || field_name == "trace_names" || field_name == "message_payload") {
                        try {
                            row[field_name] = nlohmann::json::parse(value);
                        } catch (...) {
                            row[field_name] = value;
                        }
                    } else {
                        row[field_name] = value;
                    }
                }
            }
            traces.push_back(row);
        }
        
        // Get total count
        std::string count_query = R"(
            SELECT COUNT(DISTINCT mt.id)
            FROM queen.message_trace_names mtn
            JOIN queen.message_traces mt ON mtn.trace_id = mt.id
            WHERE mtn.trace_name = $1
        )";
        
        sendQueryParamsAsync(conn.get(), count_query, {trace_name});
        auto count_result = getTuplesResult(conn.get());
        int total = (PQntuples(count_result.get()) > 0) ? std::stoi(PQgetvalue(count_result.get(), 0, 0)) : 0;
        
        return {
            {"traces", traces},
            {"count", num_rows},
            {"total", total},
            {"limit", limit},
            {"offset", offset}
        };
    } catch (const std::exception& e) {
        spdlog::error("Failed to get traces by name: {}", e.what());
        throw;
    }
}

nlohmann::json AsyncQueueManager::get_available_trace_names(
    int limit,
    int offset
) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Query for distinct trace names with counts
        std::string query = R"(
            SELECT 
                trace_name,
                COUNT(DISTINCT trace_id) as trace_count,
                COUNT(DISTINCT mt.transaction_id) as message_count,
                MAX(mt.created_at) as last_seen
            FROM queen.message_trace_names mtn
            JOIN queen.message_traces mt ON mtn.trace_id = mt.id
            GROUP BY trace_name
            ORDER BY last_seen DESC
            LIMIT $1 OFFSET $2
        )";
        
        sendQueryParamsAsync(conn.get(), query, {
            std::to_string(limit),
            std::to_string(offset)
        });
        auto result = getTuplesResult(conn.get());
        
        nlohmann::json trace_names = nlohmann::json::array();
        int num_rows = PQntuples(result.get());
        int num_cols = PQnfields(result.get());
        
        for (int i = 0; i < num_rows; i++) {
            nlohmann::json row;
            for (int j = 0; j < num_cols; j++) {
                std::string field_name = PQfname(result.get(), j);
                if (PQgetisnull(result.get(), i, j)) {
                    row[field_name] = nullptr;
                } else {
                    std::string value = PQgetvalue(result.get(), i, j);
                    // Convert numeric fields
                    if (field_name == "trace_count" || field_name == "message_count") {
                        row[field_name] = std::stoi(value);
                    } else {
                        row[field_name] = value;
                    }
                }
            }
            trace_names.push_back(row);
        }
        
        // Get total count
        std::string count_query = R"(
            SELECT COUNT(DISTINCT trace_name) as total
            FROM queen.message_trace_names
        )";
        
        sendQueryParamsAsync(conn.get(), count_query, {});
        auto count_result = getTuplesResult(conn.get());
        int total = (PQntuples(count_result.get()) > 0) ? std::stoi(PQgetvalue(count_result.get(), 0, 0)) : 0;
        
        return {
            {"trace_names", trace_names},
            {"count", num_rows},
            {"total", total},
            {"limit", limit},
            {"offset", offset}
        };
    } catch (const std::exception& e) {
        spdlog::error("Failed to get available trace names: {}", e.what());
        throw;
    }
}

} // namespace queen

