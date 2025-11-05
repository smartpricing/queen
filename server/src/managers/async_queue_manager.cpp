#include "queen/async_queue_manager.hpp"
#include "queen/file_buffer.hpp"
#include "queen/encryption.hpp"
#include <spdlog/spdlog.h>
#include <random>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <numeric>
#include <map>

namespace queen {

// --- Helper Functions for Async Parameterized Queries ---

/**
 * @brief Sends a parameterized query asynchronously
 * 
 * CRITICAL: In non-blocking mode, we must:
 * 1. Call PQsendQueryParams() to queue the query
 * 2. Call PQflush() in a loop until all data is sent (may require multiple writes)
 * 3. Then wait for the response
 */
static void sendQueryParamsAsync(PGconn* conn, const std::string& sql, const std::vector<std::string>& params) {
    std::vector<const char*> param_values;
    param_values.reserve(params.size());
    
    for (const auto& param : params) {
        param_values.push_back(param.c_str());
    }
    
    if (!PQsendQueryParams(conn, sql.c_str(), static_cast<int>(params.size()),
                          nullptr, param_values.data(), nullptr, nullptr, 0)) {
        throw std::runtime_error("PQsendQueryParams failed: " + 
                                 std::string(PQerrorMessage(conn)));
    }
    
    // CRITICAL: Flush outgoing data buffer until all data is sent
    // For large queries (e.g., 2000 items), this can take multiple iterations
    while (true) {
        int flush_result = PQflush(conn);
        if (flush_result == 0) {
            // All data successfully sent to server
            break;
        } else if (flush_result == -1) {
            throw std::runtime_error("PQflush failed: " + 
                                     std::string(PQerrorMessage(conn)));
        }
        // flush_result == 1: more data to send, socket send buffer full
        // Wait for socket to be WRITABLE (not readable!)
        waitForSocket(conn, false);  // false = wait for write
    }
    
    // Now wait for query to be processed and response to arrive
    while (true) {
        waitForSocket(conn, true);  // true = wait for read
        if (!PQconsumeInput(conn)) {
            throw std::runtime_error("PQconsumeInput failed: " + 
                                     std::string(PQerrorMessage(conn)));
        }
        if (PQisBusy(conn) == 0) {
            break;
        }
    }
}

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

// --- Maintenance Mode ---

bool AsyncQueueManager::check_maintenance_mode_with_cache() {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Check cache TTL
    uint64_t last_check = last_maintenance_check_ms_.load();
    if (now_ms - last_check < MAINTENANCE_CACHE_TTL_MS) {
        return maintenance_mode_cached_.load();
    }
    
    // Cache expired, check database
    try {
        auto conn = async_db_pool_->acquire();
        
        std::string sql = R"(
            SELECT value->>'enabled' as enabled
            FROM system_state
            WHERE key = 'maintenance_mode'
        )";
        
        sendAndWait(conn.get(), sql.c_str());
        auto result = getTuplesResult(conn.get());
        
        bool enabled = false;
        if (PQntuples(result.get()) > 0) {
            const char* val = PQgetvalue(result.get(), 0, 0);
            enabled = (val && std::string(val) == "true");
        }
        
        maintenance_mode_cached_.store(enabled);
        last_maintenance_check_ms_.store(now_ms);
        
        return enabled;
    } catch (const std::exception& e) {
        spdlog::error("Failed to check maintenance mode from database: {}", e.what());
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
            INSERT INTO queues (name, namespace, task, priority, lease_time, retry_limit, retry_delay, max_queue_size, ttl)
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
            INSERT INTO partitions (queue_id, name)
            SELECT id, $2 FROM queues WHERE name = $1
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
        FROM messages
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
        INSERT INTO messages 
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
        sendQueryParamsAsync(conn.get(), "SELECT id FROM queues WHERE name = $1", {item.queue});
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
                INSERT INTO messages (
                    id, transaction_id, partition_id, payload, trace_id, created_at
                )
                SELECT $1, $2, p.id, $3, $4, NOW()
                FROM partitions p
                JOIN queues q ON q.id = p.queue_id
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
                INSERT INTO messages (
                    id, transaction_id, partition_id, payload, created_at
                )
                SELECT $1, $2, p.id, $3, NOW()
                FROM partitions p
                JOIN queues q ON q.id = p.queue_id
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
        result.status = "failed";
        result.error = e.what();
        spdlog::error("Failed to push message: {}", e.what());
    }
    
    return result;
}

// --- Push Messages (Batch Entry Point) ---

std::vector<PushResult> AsyncQueueManager::push_messages(const std::vector<PushItem>& items) {
    if (items.empty()) {
        return {};
    }
    
    // MAINTENANCE MODE: Route all items to file buffer
    bool is_maintenance = check_maintenance_mode_with_cache();
    spdlog::debug("push_messages: maintenance_mode_check={}, has_file_buffer={}", 
                  is_maintenance, file_buffer_manager_ != nullptr);
    
    if (is_maintenance && file_buffer_manager_) {
        spdlog::info("push_messages: Routing {} messages to file buffer (maintenance mode enabled)", items.size());
        std::vector<PushResult> results;
        results.reserve(items.size());
        
        for (const auto& item : items) {
            try {
                std::string transaction_id = item.transaction_id.value_or(generate_transaction_id());
                
                nlohmann::json event = {
                    {"queue", item.queue},
                    {"partition", item.partition},
                    {"payload", item.payload},
                    {"transactionId", transaction_id},
                    {"failover", true}
                };
                
                if (item.trace_id.has_value() && !item.trace_id->empty()) {
                    event["traceId"] = *item.trace_id;
                }
                
                if (file_buffer_manager_->write_event(event)) {
                    results.push_back({
                        transaction_id,
                        "buffered",
                        std::nullopt,
                        std::nullopt,
                        item.trace_id
                    });
                } else {
                    results.push_back({
                        transaction_id,
                        "failed",
                        "File buffer write failed",
                        std::nullopt,
                        std::nullopt
                    });
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to write to file buffer during maintenance mode: {}", e.what());
                results.push_back({
                    item.transaction_id.value_or(generate_transaction_id()),
                    "failed",
                    std::string("Buffer write error: ") + e.what(),
                    std::nullopt,
                    std::nullopt
                });
            }
        }
        
        return results;
    }
    
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
                    FROM messages m
                    JOIN partitions p ON p.id = m.partition_id
                    LEFT JOIN partition_consumers pc ON pc.partition_id = p.id
                      AND pc.consumer_group = '__QUEUE_MODE__'
                    WHERE p.queue_id = q.id
                      AND (pc.last_consumed_created_at IS NULL 
                           OR m.created_at > pc.last_consumed_created_at
                           OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
                  ) as current_depth
                FROM queues q
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
    
    // Process each partition group in order
    for (const auto& [partition_key, item_indices] : partition_groups_ordered) {
        // Build items vector for this partition batch
        std::vector<PushItem> partition_items;
        partition_items.reserve(item_indices.size());
        for (size_t idx : item_indices) {
            partition_items.push_back(items[idx]);
        }
        
        auto batch_results = push_messages_batch(partition_items);
        
        // Place results back in original positions
        for (size_t i = 0; i < batch_results.size(); ++i) {
            all_results[item_indices[i]] = batch_results[i];
        }
    }
    
    return all_results;
}

// --- Size Estimation ---

size_t AsyncQueueManager::estimate_row_size(const PushItem& item, bool encryption_enabled) const {
    size_t size = 500; // Fixed overhead
    size += 20;  // id (UUID)
    size += 40;  // transaction_id
    size += 20;  // trace_id
    size += 20;  // partition_id
    size += 8;   // created_at
    size += 1;   // is_encrypted
    
    std::string payload_str = item.payload.dump();
    size += payload_str.size();
    
    if (encryption_enabled) {
        size += static_cast<size_t>(payload_str.size() * 0.35) + 200;
    }
    
    size += 50;   // JSONB overhead
    size += 250;  // Index overhead
    size += size / 10; // Fragmentation
    
    return size;
}

// --- Push Messages Batch ---

std::vector<PushResult> AsyncQueueManager::push_messages_batch(const std::vector<PushItem>& items) {
    if (items.empty()) {
        return {};
    }
    
    const std::string& queue_name = items[0].queue;
    const std::string& partition_name = items[0].partition;
    
    std::vector<PushResult> results;
    results.reserve(items.size());
    
    // Dynamic size-based batching
    if (config_.batch_push_use_size_based) {
        // Check if queue has encryption enabled
        bool encryption_enabled = false;
        try {
            auto check_conn = async_db_pool_->acquire();
            sendQueryParamsAsync(check_conn.get(), 
                                "SELECT encryption_enabled FROM queues WHERE name = $1", 
                                {queue_name});
            auto encryption_result = getTuplesResult(check_conn.get());
            
            if (PQntuples(encryption_result.get()) > 0) {
                const char* enc_str = PQgetvalue(encryption_result.get(), 0, 0);
                encryption_enabled = (enc_str && (std::string(enc_str) == "t" || std::string(enc_str) == "true"));
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to check encryption status, assuming disabled: {}", e.what());
        }
        
        const size_t TARGET_BATCH_SIZE = static_cast<size_t>(config_.batch_push_target_size_mb) * 1024 * 1024;
        const size_t MIN_BATCH_SIZE = static_cast<size_t>(config_.batch_push_min_size_mb) * 1024 * 1024;
        const size_t MAX_BATCH_SIZE = static_cast<size_t>(config_.batch_push_max_size_mb) * 1024 * 1024;
        const size_t MIN_MESSAGES = static_cast<size_t>(config_.batch_push_min_messages);
        const size_t MAX_MESSAGES = static_cast<size_t>(config_.batch_push_max_messages);
        
        size_t accumulated_size = 0;
        std::vector<PushItem> current_batch;
        
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            size_t row_size = estimate_row_size(item, encryption_enabled);
            
            bool should_flush = false;
            
            if (!current_batch.empty()) {
                if (accumulated_size + row_size > MAX_BATCH_SIZE) {
                    should_flush = true;
                    spdlog::debug("Flushing batch: would exceed MAX_BATCH_SIZE ({} MB)", 
                                config_.batch_push_max_size_mb);
                }
                else if (current_batch.size() >= MAX_MESSAGES) {
                    should_flush = true;
                    spdlog::debug("Flushing batch: hit MAX_MESSAGES ({})", MAX_MESSAGES);
                }
                else if (accumulated_size >= TARGET_BATCH_SIZE && 
                         current_batch.size() >= MIN_MESSAGES) {
                    should_flush = true;
                    spdlog::debug("Flushing batch: reached TARGET_BATCH_SIZE ({} MB) with {} messages", 
                                config_.batch_push_target_size_mb, current_batch.size());
                }
            }
            
            if (should_flush) {
                spdlog::debug("Processing batch: {} messages, ~{} MB", 
                            current_batch.size(), 
                            accumulated_size / (1024.0 * 1024.0));
                
                auto chunk_results = push_messages_chunk(current_batch, queue_name, partition_name);
                results.insert(results.end(), chunk_results.begin(), chunk_results.end());
                
                current_batch.clear();
                accumulated_size = 0;
            }
            
            current_batch.push_back(item);
            accumulated_size += row_size;
        }
        
        // Process final batch
        if (!current_batch.empty()) {
            spdlog::debug("Processing final batch: {} messages, ~{} MB", 
                        current_batch.size(), 
                        accumulated_size / (1024.0 * 1024.0));
            
            auto chunk_results = push_messages_chunk(current_batch, queue_name, partition_name);
            results.insert(results.end(), chunk_results.begin(), chunk_results.end());
        }
        
    } else {
        // Legacy count-based batching
        const size_t CHUNK_SIZE = config_.batch_push_chunk_size;
        spdlog::debug("Using legacy count-based batching: {} messages per chunk", CHUNK_SIZE);
        
        for (size_t chunk_start = 0; chunk_start < items.size(); chunk_start += CHUNK_SIZE) {
            size_t chunk_end = std::min(chunk_start + CHUNK_SIZE, items.size());
            std::vector<PushItem> chunk(items.begin() + chunk_start, items.begin() + chunk_end);
            
            auto chunk_results = push_messages_chunk(chunk, queue_name, partition_name);
            results.insert(results.end(), chunk_results.begin(), chunk_results.end());
        }
    }
    
    return results;
}

// --- Push Messages Chunk (Core Batch INSERT with Async DB) ---

std::vector<PushResult> AsyncQueueManager::push_messages_chunk(const std::vector<PushItem>& items,
                                                               const std::string& queue_name,
                                                               const std::string& partition_name) {
    auto chunk_start = std::chrono::steady_clock::now();
    std::vector<PushResult> results;
    results.reserve(items.size());
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Check if queue exists
        auto queue_check_start = std::chrono::steady_clock::now();
        sendQueryParamsAsync(conn.get(), "SELECT id FROM queues WHERE name = $1", {queue_name});
        auto queue_check = getTuplesResult(conn.get());
        auto queue_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - queue_check_start).count();
        spdlog::debug("TIMING: Queue check took {}ms", queue_check_ms);
        
        if (PQntuples(queue_check.get()) == 0) {
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                result.status = "failed";
                result.error = "Queue '" + queue_name + "' does not exist. Please create it using the configure endpoint first.";
                results.push_back(result);
            }
            return results;
        }
        
        // Ensure partition exists
        auto partition_check_start = std::chrono::steady_clock::now();
        if (!ensure_partition_exists(conn.get(), queue_name, partition_name)) {
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                result.status = "failed";
                result.error = "Failed to create partition";
                results.push_back(result);
            }
            return results;
        }
        auto partition_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - partition_check_start).count();
        spdlog::debug("TIMING: Partition check took {}ms", partition_check_ms);
        
        // Check encryption enabled
        auto encryption_check_start = std::chrono::steady_clock::now();
        sendQueryParamsAsync(conn.get(), "SELECT encryption_enabled FROM queues WHERE name = $1", {queue_name});
        auto encryption_result = getTuplesResult(conn.get());
        
        bool encryption_enabled = false;
        if (PQntuples(encryption_result.get()) > 0) {
            const char* enc_str = PQgetvalue(encryption_result.get(), 0, 0);
            encryption_enabled = (enc_str && (std::string(enc_str) == "t" || std::string(enc_str) == "true"));
        }
        
        // Get encryption service if needed
        EncryptionService* enc_service = encryption_enabled ? get_encryption_service() : nullptr;
        
        // Get partition ID
        std::string partition_id_sql = R"(
            SELECT p.id FROM partitions p
            JOIN queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
        )";
        sendQueryParamsAsync(conn.get(), partition_id_sql, {queue_name, partition_name});
        auto partition_result = getTuplesResult(conn.get());
        
        if (PQntuples(partition_result.get()) == 0) {
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                result.status = "failed";
                result.error = "Partition not found";
                results.push_back(result);
            }
            return results;
        }
        
        std::string partition_id = PQgetvalue(partition_result.get(), 0, 0);
        auto encryption_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - encryption_check_start).count();
        spdlog::debug("TIMING: Encryption check + partition ID lookup took {}ms", encryption_check_ms);
        
        // Begin transaction
        auto txn_start = std::chrono::steady_clock::now();
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            auto build_arrays_start = std::chrono::steady_clock::now();
            
            // Build arrays for batch insert
            std::vector<std::string> message_ids;
            std::vector<std::string> transaction_ids;
            std::vector<std::string> payloads;
            std::vector<std::string> trace_ids;
            std::vector<std::string> partition_ids;
            std::vector<bool> encrypted_flags;
            
            for (size_t idx = 0; idx < items.size(); ++idx) {
                const auto& item = items[idx];
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                
                std::string msg_id = generate_uuid();
                message_ids.push_back(msg_id);
                transaction_ids.push_back(result.transaction_id);
                partition_ids.push_back(partition_id);
                
                // Prepare payload - encrypt if needed
                std::string payload_to_store;
                bool is_encrypted = false;
                
                if (enc_service && enc_service->is_enabled()) {
                    auto encrypted = enc_service->encrypt_payload(item.payload.dump());
                    if (encrypted.has_value()) {
                        nlohmann::json encrypted_json = {
                            {"encrypted", encrypted->encrypted},
                            {"iv", encrypted->iv},
                            {"authTag", encrypted->auth_tag}
                        };
                        payload_to_store = encrypted_json.dump();
                        is_encrypted = true;
                    } else {
                        spdlog::warn("Encryption failed for message, storing unencrypted");
                        payload_to_store = item.payload.dump();
                    }
                } else {
                    payload_to_store = item.payload.dump();
                }
                
                payloads.push_back(payload_to_store);
                trace_ids.push_back(item.trace_id.value_or(""));
                encrypted_flags.push_back(is_encrypted);
                results.push_back(result);
            }
            auto build_arrays_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - build_arrays_start).count();
            spdlog::debug("TIMING: Building arrays took {}ms", build_arrays_ms);
            
            // Duplicate detection
            auto dup_check_start = std::chrono::steady_clock::now();
            std::map<std::string, std::string> existing_txn_map;
            
            bool has_explicit_txn_ids = false;
            for (const auto& item : items) {
                if (item.transaction_id.has_value() && !item.transaction_id->empty()) {
                    has_explicit_txn_ids = true;
                    break;
                }
            }
            
            if (has_explicit_txn_ids) {
                // Build array of transaction IDs
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
                
                std::string txn_check_array = build_pg_array(transaction_ids);
                
                std::string dup_check_sql = R"(
                    SELECT t.txn_id as transaction_id, m.id as message_id
                    FROM UNNEST($1::varchar[]) AS t(txn_id)
                    LEFT JOIN messages m ON m.transaction_id = t.txn_id 
                        AND m.partition_id = $2::uuid
                    WHERE m.id IS NOT NULL
                )";
                
                sendQueryParamsAsync(conn.get(), dup_check_sql, {txn_check_array, partition_id});
                auto dup_result = getTuplesResult(conn.get());
                
                for (int i = 0; i < PQntuples(dup_result.get()); ++i) {
                    std::string txn_id = PQgetvalue(dup_result.get(), i, 0);
                    std::string msg_id = PQgetvalue(dup_result.get(), i, 1);
                    existing_txn_map[txn_id] = msg_id;
                }
                
                if (!existing_txn_map.empty()) {
                    spdlog::debug("Found {} duplicate transaction IDs in batch of {}", 
                                existing_txn_map.size(), transaction_ids.size());
                }
            }
            auto dup_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - dup_check_start).count();
            spdlog::debug("TIMING: Duplicate check took {}ms (found {} duplicates)", dup_check_ms, existing_txn_map.size());
            
            // Filter out duplicates
            auto filter_start = std::chrono::steady_clock::now();
            std::vector<std::string> filtered_message_ids;
            std::vector<std::string> filtered_transaction_ids;
            std::vector<std::string> filtered_partition_ids;
            std::vector<std::string> filtered_payloads;
            std::vector<std::string> filtered_trace_ids;
            std::vector<bool> filtered_encrypted_flags;
            std::vector<size_t> filtered_result_indices;
            
            for (size_t i = 0; i < transaction_ids.size(); ++i) {
                auto it = existing_txn_map.find(transaction_ids[i]);
                if (it != existing_txn_map.end()) {
                    results[i].status = "duplicate";
                    results[i].message_id = it->second;
                    continue;
                }
                
                filtered_message_ids.push_back(message_ids[i]);
                filtered_transaction_ids.push_back(transaction_ids[i]);
                filtered_partition_ids.push_back(partition_ids[i]);
                filtered_payloads.push_back(payloads[i]);
                filtered_trace_ids.push_back(trace_ids[i]);
                filtered_encrypted_flags.push_back(encrypted_flags[i]);
                filtered_result_indices.push_back(i);
            }
            
            // Build PostgreSQL arrays
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
            
            auto build_pg_array_with_nulls = [](const std::vector<std::string>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    if (vec[i].empty()) {
                        result += "NULL";
                    } else {
                        result += "\"";
                        for (char c : vec[i]) {
                            if (c == '\\' || c == '"') result += '\\';
                            result += c;
                        }
                        result += "\"";
                    }
                }
                result += "}";
                return result;
            };
            
            auto build_bool_array = [](const std::vector<bool>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    result += vec[i] ? "true" : "false";
                }
                result += "}";
                return result;
            };
            
            std::string ids_array = build_pg_array(filtered_message_ids);
            std::string txn_array = build_pg_array(filtered_transaction_ids);
            std::string part_array = build_pg_array(filtered_partition_ids);
            std::string payload_array = build_pg_array(filtered_payloads);
            std::string trace_array = build_pg_array_with_nulls(filtered_trace_ids);
            std::string encrypted_array = build_bool_array(filtered_encrypted_flags);
            auto filter_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - filter_start).count();
            spdlog::debug("TIMING: Filtering and building PG arrays took {}ms ({} items to insert)", filter_ms, filtered_message_ids.size());
            
            // Batch INSERT
            if (!filtered_message_ids.empty()) {
                auto insert_start = std::chrono::steady_clock::now();
                std::string sql = R"(
                    INSERT INTO messages (id, transaction_id, partition_id, payload, trace_id, is_encrypted)
                    SELECT * FROM UNNEST(
                        $1::uuid[],
                        $2::varchar[],
                        $3::uuid[],
                        $4::jsonb[],
                        $5::uuid[],
                        $6::boolean[]
                    )
                    RETURNING id, transaction_id, trace_id
                )";
                
                std::vector<std::string> unnest_params = {
                    ids_array,
                    txn_array,
                    part_array,
                    payload_array,
                    trace_array,
                    encrypted_array
                };
                
                sendQueryParamsAsync(conn.get(), sql, unnest_params);
                auto insert_result = getTuplesResult(conn.get());
                auto insert_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - insert_start).count();
                spdlog::debug("TIMING: INSERT took {}ms ({} items)", insert_ms, filtered_message_ids.size());
                
                // Update results
                for (int i = 0; i < PQntuples(insert_result.get()) && i < static_cast<int>(filtered_result_indices.size()); ++i) {
                    size_t result_idx = filtered_result_indices[i];
                    results[result_idx].status = "queued";
                    results[result_idx].message_id = PQgetvalue(insert_result.get(), i, 0);
                    const char* trace_val = PQgetvalue(insert_result.get(), i, 2);
                    if (trace_val && std::string(trace_val).length() > 0) {
                        results[result_idx].trace_id = trace_val;
                    }
                }
            }
            
            // Commit
            auto commit_start = std::chrono::steady_clock::now();
            sendAndWait(conn.get(), "COMMIT");
            getCommandResult(conn.get());
            auto commit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - commit_start).count();
            spdlog::debug("TIMING: COMMIT took {}ms", commit_ms);
            
            auto txn_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - txn_start).count();
            spdlog::debug("TIMING: Total transaction took {}ms", txn_total_ms);
            
        } catch (const std::exception& e) {
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        for (auto& result : results) {
            if (result.status.empty()) {
                result.status = "failed";
                result.error = e.what();
            }
        }
        spdlog::error("Batch push failed: {}", e.what());
    }
    
    auto chunk_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - chunk_start).count();
    spdlog::debug("TIMING: Total chunk processing took {}ms ({} items)", chunk_total_ms, items.size());
    
    return results;
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

// ===================================
// POP OPERATIONS - ASYNC VERSIONS
// ===================================

// --- Lease Management ---

std::string AsyncQueueManager::acquire_partition_lease(
    PGconn* conn,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group,
    int lease_time_seconds,
    const PopOptions& options
) {
    try {
        // Reclaim expired leases FIRST
        std::string reclaim_sql = R"(
            UPDATE partition_consumers
            SET lease_expires_at = NULL,
                lease_acquired_at = NULL,
                message_batch = NULL,
                batch_size = 0,
                acked_count = 0,
                worker_id = NULL
            WHERE lease_expires_at IS NOT NULL
              AND lease_expires_at < NOW()
        )";
        
        sendAndWait(conn, reclaim_sql.c_str());
        getCommandResult(conn);
        spdlog::debug("Reclaimed expired leases");
        
        std::string lease_id = generate_uuid();
        
        // Check subscription preferences for initial cursor
        std::string initial_cursor_id = "00000000-0000-0000-0000-000000000000";
        std::string initial_cursor_timestamp_sql = "NULL";
        
        if (consumer_group != "__QUEUE_MODE__" && 
            (options.subscription_mode.has_value() || options.subscription_from.has_value())) {
            
            std::string sub_mode = options.subscription_mode.value_or("");
            std::string sub_from = options.subscription_from.value_or("");
            
            // For 'new', 'new-only', or 'now' - start from latest message
            if (sub_mode == "new" || sub_mode == "new-only" || sub_from == "now") {
                std::string latest_sql = R"(
                    SELECT m.id, m.created_at
                    FROM messages m
                    JOIN partitions p ON m.partition_id = p.id
                    JOIN queues q ON p.queue_id = q.id
                    WHERE q.name = $1 AND p.name = $2
                    ORDER BY m.created_at DESC, m.id DESC
                    LIMIT 1
                )";
                
                sendQueryParamsAsync(conn, latest_sql, {queue_name, partition_name});
                auto latest_result = getTuplesResult(conn);
                
                if (PQntuples(latest_result.get()) > 0) {
                    initial_cursor_id = PQgetvalue(latest_result.get(), 0, 0);
                    initial_cursor_timestamp_sql = "'" + std::string(PQgetvalue(latest_result.get(), 0, 1)) + "'";
                    spdlog::debug("Subscription mode '{}' - starting from latest message: {}", sub_mode, initial_cursor_id);
                }
            }
        }
        
        // Check if consumer group already exists
        std::string check_sql = R"(
            SELECT pc.id FROM partition_consumers pc
            JOIN partitions p ON pc.partition_id = p.id
            JOIN queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2 AND pc.consumer_group = $3
        )";
        
        sendQueryParamsAsync(conn, check_sql, {queue_name, partition_name, consumer_group});
        auto check_result = getTuplesResult(conn);
        bool consumer_exists = PQntuples(check_result.get()) > 0;
        
        spdlog::debug("Consumer group '{}' exists: {}, has subscription options: {}", 
                     consumer_group, consumer_exists, 
                     (options.subscription_mode.has_value() || options.subscription_from.has_value()));
        
        std::string sql;
        std::vector<std::string> params;
        
        if (!consumer_exists && consumer_group != "__QUEUE_MODE__" && 
            (options.subscription_mode.has_value() || options.subscription_from.has_value())) {
            // New consumer group with subscription preferences
            spdlog::debug("Creating new consumer group '{}' with cursor at: {}", consumer_group, initial_cursor_id);
            
            sql = R"(
                INSERT INTO partition_consumers (
                    partition_id, consumer_group, lease_expires_at, lease_acquired_at, worker_id,
                    last_consumed_id, last_consumed_created_at
                )
                SELECT p.id, $1, NOW() + INTERVAL '1 second' * $2, NOW(), $3, 
                       $6::uuid, )" + initial_cursor_timestamp_sql + R"(
                FROM partitions p
                JOIN queues q ON q.id = p.queue_id
                WHERE q.name = $4 AND p.name = $5
                ON CONFLICT (partition_id, consumer_group) DO NOTHING
                RETURNING worker_id
            )";
            
            params = {
                consumer_group,
                std::to_string(lease_time_seconds),
                lease_id,
                queue_name,
                partition_name,
                initial_cursor_id
            };
        } else {
            // Existing consumer or no subscription preference
            sql = R"(
                INSERT INTO partition_consumers (
                    partition_id, consumer_group, lease_expires_at, lease_acquired_at, worker_id
                )
                SELECT p.id, $1, NOW() + INTERVAL '1 second' * $2, NOW(), $3
                FROM partitions p
                JOIN queues q ON q.id = p.queue_id
                WHERE q.name = $4 AND p.name = $5
                ON CONFLICT (partition_id, consumer_group) DO UPDATE SET
                    lease_expires_at = NOW() + INTERVAL '1 second' * $2,
                    lease_acquired_at = NOW(),
                    worker_id = $3
                WHERE partition_consumers.lease_expires_at IS NULL 
                   OR partition_consumers.lease_expires_at <= NOW()
                RETURNING worker_id
            )";
            
            params = {
                consumer_group,
                std::to_string(lease_time_seconds),
                lease_id,
                queue_name,
                partition_name
            };
        }
        
        spdlog::debug("Lease acquisition query params: consumer_group={}, lease_time={}, lease_id={}, queue={}, partition={}", 
                     consumer_group, lease_time_seconds, lease_id, queue_name, partition_name);
        
        sendQueryParamsAsync(conn, sql, params);
        auto result = getTuplesResult(conn);
        
        spdlog::debug("Lease acquisition returned {} rows", PQntuples(result.get()));
        
        if (PQntuples(result.get()) > 0) {
            spdlog::debug("Lease acquired successfully: {}", lease_id);
            return lease_id;
        } else {
            spdlog::debug("Lease acquisition failed - partition may be locked by another consumer");
            return "";
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to acquire lease: {}", e.what());
        return "";
    }
}

void AsyncQueueManager::release_partition_lease(
    PGconn* conn,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group
) {
    try {
        std::string release_sql = R"(
            UPDATE partition_consumers
            SET lease_expires_at = NULL,
                lease_acquired_at = NULL,
                worker_id = NULL
            WHERE partition_id = (
                SELECT p.id FROM partitions p
                JOIN queues q ON p.queue_id = q.id
                WHERE q.name = $1 AND p.name = $2
            )
            AND consumer_group = $3
        )";
        
        sendQueryParamsAsync(conn, release_sql, {queue_name, partition_name, consumer_group});
        getCommandResult(conn);
        
    } catch (const std::exception& e) {
        spdlog::warn("Failed to release lease: {}", e.what());
    }
}

bool AsyncQueueManager::ensure_consumer_group_exists(
    PGconn* conn,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group
) {
    try {
        std::string sql = R"(
            INSERT INTO partition_consumers (partition_id, consumer_group)
            SELECT p.id, $3
            FROM partitions p
            JOIN queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
            ON CONFLICT (partition_id, consumer_group) DO NOTHING
        )";
        
        sendQueryParamsAsync(conn, sql, {queue_name, partition_name, consumer_group});
        getCommandResult(conn);
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to ensure consumer group exists: {}", e.what());
        return false;
    }
}

// --- Pop from Specific Partition ---

PopResult AsyncQueueManager::pop_messages_from_partition(
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group,
    const PopOptions& options
) {
    PopResult result;
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Check if partition is accessible considering window_buffer
        std::string window_check_sql = R"(
            SELECT p.id, q.window_buffer
            FROM partitions p
            JOIN queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
              AND (q.window_buffer = 0 OR NOT EXISTS (
                SELECT 1 FROM messages m
                WHERE m.partition_id = p.id
                  AND m.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
              ))
        )";
        
        sendQueryParamsAsync(conn.get(), window_check_sql, {queue_name, partition_name});
        auto window_result = getTuplesResult(conn.get());
        
        if (PQntuples(window_result.get()) == 0) {
            spdlog::debug("Partition {} blocked by window buffer", partition_name);
            return result;
        }
        
        // Get queue configuration for delayed_processing, max_wait_time_seconds, and lease_time
        std::string config_sql = R"(
            SELECT q.delayed_processing, q.max_wait_time_seconds, q.lease_time
            FROM queues q
            WHERE q.name = $1
        )";
        
        sendQueryParamsAsync(conn.get(), config_sql, {queue_name});
        auto config_result = getTuplesResult(conn.get());
        
        int delayed_processing = 0;
        int max_wait_time = 0;
        int lease_time = 300;
        
        if (PQntuples(config_result.get()) > 0) {
            const char* delay_str = PQgetvalue(config_result.get(), 0, 0);
            const char* wait_str = PQgetvalue(config_result.get(), 0, 1);
            const char* lease_str = PQgetvalue(config_result.get(), 0, 2);
            
            delayed_processing = (delay_str && strlen(delay_str) > 0) ? std::stoi(delay_str) : 0;
            max_wait_time = (wait_str && strlen(wait_str) > 0) ? std::stoi(wait_str) : 0;
            lease_time = (lease_str && strlen(lease_str) > 0) ? std::stoi(lease_str) : 300;
        }
        
        // Acquire lease for this partition
        std::string lease_id = acquire_partition_lease(conn.get(), queue_name, partition_name, 
                                                      consumer_group, lease_time, options);
        if (lease_id.empty()) {
            return result;
        }
        
        result.lease_id = lease_id;
        
        // Build WHERE clause
        std::string where_clause = R"(
            WHERE q.name = $1 AND p.name = $2 AND pc.consumer_group = $3
              AND pc.worker_id = $4 AND pc.lease_expires_at > NOW()
              AND (pc.last_consumed_created_at IS NULL 
                   OR m.created_at > pc.last_consumed_created_at
                   OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
        )";
        
        std::vector<std::string> params = {
            queue_name,
            partition_name,
            consumer_group,
            lease_id
        };
        
        // Add delayed processing filter
        if (delayed_processing > 0) {
            where_clause += " AND m.created_at <= NOW() - INTERVAL '1 second' * $" + std::to_string(params.size() + 1);
            params.push_back(std::to_string(delayed_processing));
            spdlog::debug("Delayed processing filter active: {} seconds", delayed_processing);
        }
        
        // Add max wait time filter
        if (max_wait_time > 0) {
            where_clause += " AND m.created_at > NOW() - INTERVAL '1 second' * $" + std::to_string(params.size() + 1);
            params.push_back(std::to_string(max_wait_time));
        }
        
        // Add batch limit
        params.push_back(std::to_string(options.batch));
        std::string limit_param = "$" + std::to_string(params.size());
        
        // Get messages
        std::string sql = R"(
            SELECT m.id, m.transaction_id, m.partition_id, m.payload, m.trace_id, m.created_at, m.is_encrypted,
                   q.name as queue_name, p.name as partition_name, q.priority as queue_priority
            FROM messages m
            JOIN partitions p ON p.id = m.partition_id
            JOIN queues q ON q.id = p.queue_id
            JOIN partition_consumers pc ON pc.partition_id = p.id
            )" + where_clause + R"(
            ORDER BY m.created_at ASC, m.id ASC
            LIMIT )" + limit_param + R"(
            FOR UPDATE OF m SKIP LOCKED
        )";
        
        spdlog::debug("Pop query params: queue={}, partition={}, consumer_group={}, lease_id={}, batch={}", 
                     queue_name, partition_name, consumer_group, lease_id, options.batch);
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto query_result = getTuplesResult(conn.get());
        
        int num_messages = PQntuples(query_result.get());
        
        spdlog::debug("Pop query returned {} rows (requested: {})", num_messages, options.batch);
        
        if (num_messages < options.batch) {
            spdlog::debug("Returned fewer messages than requested - likely fewer messages available in partition");
        }
        
        if (num_messages > 0) {
            // Update batch_size
            std::string update_batch_size = R"(
                UPDATE partition_consumers
                SET batch_size = $1,
                    acked_count = 0
                WHERE partition_id = (
                    SELECT p.id FROM partitions p
                    JOIN queues q ON p.queue_id = q.id
                    WHERE q.name = $2 AND p.name = $3
                )
                AND consumer_group = $4
                AND worker_id = $5
            )";
            
            sendQueryParamsAsync(conn.get(), update_batch_size, {
                std::to_string(num_messages),
                queue_name,
                partition_name,
                consumer_group,
                lease_id
            });
            getCommandResult(conn.get());
        }
        
        if (num_messages == 0) {
            // No messages found - release lease
            spdlog::debug("No messages found - releasing lease for partition: {}", partition_name);
            release_partition_lease(conn.get(), queue_name, partition_name, consumer_group);
            return result;
        }
        
        // Get encryption service if needed
        EncryptionService* enc_service = get_encryption_service();
        
        // Parse messages
        for (int i = 0; i < num_messages; ++i) {
            Message msg;
            msg.id = PQgetvalue(query_result.get(), i, 0);
            msg.transaction_id = PQgetvalue(query_result.get(), i, 1);
            msg.partition_id = PQgetvalue(query_result.get(), i, 2);
            
            std::string payload_str = PQgetvalue(query_result.get(), i, 3);
            const char* trace_val = PQgetvalue(query_result.get(), i, 4);
            msg.trace_id = (trace_val && strlen(trace_val) > 0) ? trace_val : "";
            
            // Parse created_at
            const char* created_str = PQgetvalue(query_result.get(), i, 5);
            if (created_str && strlen(created_str) > 0) {
                std::tm tm = {};
                std::istringstream ss(created_str);
                ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                msg.created_at = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
            
            const char* is_encrypted_val = PQgetvalue(query_result.get(), i, 6);
            bool is_encrypted = (is_encrypted_val && (std::string(is_encrypted_val) == "t" || std::string(is_encrypted_val) == "true"));
            
            msg.queue_name = PQgetvalue(query_result.get(), i, 7);
            msg.partition_name = PQgetvalue(query_result.get(), i, 8);
            
            // Decrypt if needed
            if (is_encrypted && enc_service && enc_service->is_enabled()) {
                try {
                    auto encrypted_json = nlohmann::json::parse(payload_str);
                    EncryptionService::EncryptedData encrypted_data{
                        encrypted_json["encrypted"],
                        encrypted_json["iv"],
                        encrypted_json["authTag"]
                    };
                    auto decrypted = enc_service->decrypt_payload(encrypted_data);
                    
                    if (decrypted.has_value()) {
                        msg.payload = nlohmann::json::parse(decrypted.value());
                    } else {
                        spdlog::error("Decryption failed for message {}", msg.id);
                        msg.payload = nlohmann::json::parse(payload_str);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Error decrypting message {}: {}", msg.id, e.what());
                    msg.payload = nlohmann::json::parse(payload_str);
                }
            } else {
                msg.payload = nlohmann::json::parse(payload_str);
            }
            
            result.messages.push_back(msg);
        }
        
        // Auto-ack if enabled
        if (options.auto_ack && !result.messages.empty()) {
            spdlog::debug("Auto-ack enabled - acknowledging {} messages", result.messages.size());
            for (const auto& msg : result.messages) {
                acknowledge_message(msg.transaction_id, "completed", std::nullopt, 
                                  consumer_group, lease_id, msg.partition_id);
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pop from partition: {}", e.what());
    }
    
    return result;
}

// --- Pop from Any Partition in Queue ---

PopResult AsyncQueueManager::pop_messages_from_queue(
    const std::string& queue_name,
    const std::string& consumer_group,
    const PopOptions& options
) {
    PopResult result;
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Get queue configuration for window_buffer
        std::string config_sql = "SELECT window_buffer FROM queues WHERE name = $1";
        sendQueryParamsAsync(conn.get(), config_sql, {queue_name});
        auto config_result = getTuplesResult(conn.get());
        
        int window_buffer = 0;
        if (PQntuples(config_result.get()) > 0) {
            const char* buffer_str = PQgetvalue(config_result.get(), 0, 0);
            window_buffer = (buffer_str && strlen(buffer_str) > 0) ? std::stoi(buffer_str) : 0;
        }
        
        // Build query to find available partitions
        std::string sql;
        std::vector<std::string> params = {queue_name, consumer_group};
        
        if (window_buffer > 0) {
            sql = R"(
                SELECT p.id, p.name, COUNT(m.id) as message_count
                FROM partitions p
                JOIN queues q ON p.queue_id = q.id
                LEFT JOIN messages m ON m.partition_id = p.id
                LEFT JOIN partition_consumers pc ON pc.partition_id = p.id 
                    AND pc.consumer_group = $2
                WHERE q.name = $1
                  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
                  AND m.id IS NOT NULL
                  AND NOT EXISTS (
                      SELECT 1 FROM messages m2
                      WHERE m2.partition_id = p.id
                        AND m2.created_at > NOW() - INTERVAL '1 second' * $3
                  )
                  AND (pc.last_consumed_created_at IS NULL
                       OR m.created_at > pc.last_consumed_created_at
                       OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
                GROUP BY p.id, p.name
                HAVING COUNT(m.id) > 0
                ORDER BY COUNT(m.id) DESC
                LIMIT 10
            )";
            params.push_back(std::to_string(window_buffer));
        } else {
            sql = R"(
                SELECT p.id, p.name, COUNT(m.id) as message_count
                FROM partitions p
                JOIN queues q ON p.queue_id = q.id
                LEFT JOIN messages m ON m.partition_id = p.id
                LEFT JOIN partition_consumers pc ON pc.partition_id = p.id 
                    AND pc.consumer_group = $2
                WHERE q.name = $1
                  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
                  AND m.id IS NOT NULL
                  AND (pc.last_consumed_created_at IS NULL
                       OR m.created_at > pc.last_consumed_created_at
                       OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
                GROUP BY p.id, p.name
                HAVING COUNT(m.id) > 0
                ORDER BY COUNT(m.id) DESC
                LIMIT 10
            )";
        }
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto partitions_result = getTuplesResult(conn.get());
        
        if (PQntuples(partitions_result.get()) == 0) {
            spdlog::debug("No available partitions found for queue: {}", queue_name);
            return result;
        }
        
        // Try each partition until we get messages
        for (int i = 0; i < PQntuples(partitions_result.get()); ++i) {
            std::string partition_name = PQgetvalue(partitions_result.get(), i, 1);
            const char* count_str = PQgetvalue(partitions_result.get(), i, 2);
            int message_count = (count_str && strlen(count_str) > 0) ? std::stoi(count_str) : 0;
            
            spdlog::debug("Trying partition '{}' with {} messages", partition_name, message_count);
            
            result = pop_messages_from_partition(queue_name, partition_name, consumer_group, options);
            
            if (!result.messages.empty()) {
                return result;
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pop from any partition: {}", e.what());
    }
    
    return result;
}

// --- Pop with Namespace/Task Filters ---

PopResult AsyncQueueManager::pop_messages_filtered(
    const std::optional<std::string>& namespace_name,
    const std::optional<std::string>& task_name,
    const std::string& consumer_group,
    const PopOptions& options
) {
    PopResult result;
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Find queues matching filters with window buffer check
        std::string sql = R"(
            SELECT 
                q.name as queue_name,
                p.name as partition_name,
                q.window_buffer
            FROM queues q
            JOIN partitions p ON p.queue_id = q.id
            WHERE 1=1
        )";
        
        std::vector<std::string> params;
        
        if (namespace_name.has_value()) {
            sql += " AND q.namespace = $" + std::to_string(params.size() + 1);
            params.push_back(*namespace_name);
        }
        
        if (task_name.has_value()) {
            sql += " AND q.task = $" + std::to_string(params.size() + 1);
            params.push_back(*task_name);
        }
        
        sql += R"(
              AND (q.window_buffer = 0 OR NOT EXISTS (
                SELECT 1 FROM messages m
                WHERE m.partition_id = p.id
                  AND m.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
              ))
            LIMIT 10
        )";
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto queues_result = getTuplesResult(conn.get());
        
        if (PQntuples(queues_result.get()) == 0) {
            spdlog::debug("No queues found matching namespace/task filters");
            return result;
        }
        
        // Try each matching queue/partition
        for (int i = 0; i < PQntuples(queues_result.get()); ++i) {
            std::string queue_name = PQgetvalue(queues_result.get(), i, 0);
            std::string partition_name = PQgetvalue(queues_result.get(), i, 1);
            
            result = pop_messages_from_partition(queue_name, partition_name, consumer_group, options);
            
            if (!result.messages.empty()) {
                return result;
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pop with namespace/task: {}", e.what());
    }
    
    return result;
}

// ===================================
// ACK OPERATIONS - ASYNC VERSIONS
// ===================================

AsyncQueueManager::AckResult AsyncQueueManager::acknowledge_message(
    const std::string& transaction_id,
    const std::string& status,
    const std::optional<std::string>& error,
    const std::string& consumer_group,
    const std::optional<std::string>& lease_id,
    const std::optional<std::string>& partition_id_param
) {
    AckResult result;
    result.success = false;
    result.message = "not_found";
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // CRITICAL: partition_id is MANDATORY
        if (!partition_id_param.has_value() || partition_id_param->empty()) {
            spdlog::error("partition_id is required for acknowledgment of transaction {}", transaction_id);
            result.message = "invalid_request";
            result.error = "partition_id is required";
            return result;
        }
        
        std::string partition_id = *partition_id_param;
        spdlog::debug("Using provided partition_id {} for transaction {}", partition_id, transaction_id);
        
        // Validate lease if provided
        if (lease_id.has_value()) {
            spdlog::debug("Validating lease {} for transaction {}", *lease_id, transaction_id);
            std::string validate_sql = R"(
                SELECT 1 FROM partition_consumers pc
                WHERE pc.partition_id = $1::uuid
                  AND pc.worker_id = $2
                  AND pc.lease_expires_at > NOW()
            )";
            
            sendQueryParamsAsync(conn.get(), validate_sql, {partition_id, *lease_id});
            auto valid = getTuplesResult(conn.get());
            
            if (PQntuples(valid.get()) == 0) {
                spdlog::warn("Invalid lease {} for partition {}", *lease_id, partition_id);
                result.message = "invalid_lease";
                result.error = "Lease validation failed";
                return result;
            }
            spdlog::debug("Lease {} validated successfully", *lease_id);
        }
        
        if (status == "completed") {
            // Update cursor and release lease if all messages ACKed
            std::string sql = R"(
                UPDATE partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    acked_count = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + 1
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                FROM messages m
                WHERE partition_consumers.partition_id = $1::uuid
                  AND partition_consumers.consumer_group = $2
                  AND m.partition_id = $1::uuid
                  AND m.transaction_id = $3
                RETURNING 
                    (partition_consumers.lease_expires_at IS NULL) as lease_released,
                    partition_consumers.partition_id
            )";
            
            sendQueryParamsAsync(conn.get(), sql, {partition_id, consumer_group, transaction_id});
            auto query_result = getTuplesResult(conn.get());
            
            if (PQntuples(query_result.get()) > 0) {
                const char* lease_released_val = PQgetvalue(query_result.get(), 0, 0);
                bool lease_released = (lease_released_val && (std::string(lease_released_val) == "t"));
                
                if (lease_released) {
                    spdlog::debug("Lease released after ACK for transaction: {}", transaction_id);
                    
                    // Insert into messages_consumed for analytics
                    std::string partition_id_ret = PQgetvalue(query_result.get(), 0, 1);
                    std::string insert_consumed_sql = R"(
                        INSERT INTO messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                        VALUES ($1, $2, 1, 0, NOW())
                    )";
                    
                    sendQueryParamsAsync(conn.get(), insert_consumed_sql, {partition_id_ret, consumer_group});
                    getCommandResult(conn.get());
                }
                
                result.success = true;
                result.message = "completed";
            }
            
            return result;
            
        } else if (status == "failed") {
            // Check if this failure completes the batch and if we should retry
            std::string batch_info_sql = R"(
                SELECT 
                    pc.batch_size,
                    pc.acked_count,
                    pc.batch_retry_count,
                    q.retry_limit
                FROM partition_consumers pc
                JOIN partitions p ON p.id = pc.partition_id
                JOIN queues q ON q.id = p.queue_id
                WHERE pc.partition_id = $1::uuid
                  AND pc.consumer_group = $2
                LIMIT 1
            )";
            
            sendQueryParamsAsync(conn.get(), batch_info_sql, {partition_id, consumer_group});
            auto batch_info = getTuplesResult(conn.get());
            
            int batch_size = 0;
            int acked_count = 0;
            int batch_retry_count = 0;
            int retry_limit = 3;
            
            if (PQntuples(batch_info.get()) > 0) {
                const char* bs = PQgetvalue(batch_info.get(), 0, 0);
                const char* ac = PQgetvalue(batch_info.get(), 0, 1);
                const char* brc = PQgetvalue(batch_info.get(), 0, 2);
                const char* rl = PQgetvalue(batch_info.get(), 0, 3);
                
                batch_size = (bs && strlen(bs) > 0) ? std::stoi(bs) : 0;
                acked_count = (ac && strlen(ac) > 0) ? std::stoi(ac) : 0;
                batch_retry_count = (brc && strlen(brc) > 0) ? std::stoi(brc) : 0;
                retry_limit = (rl && strlen(rl) > 0) ? std::stoi(rl) : 3;
            }
            
            // Check if this is the last message in batch being ACKed
            bool is_last_in_batch = (batch_size > 0) && (acked_count + 1 >= batch_size);
            
            if (is_last_in_batch && batch_retry_count < retry_limit) {
                // This is a batch failure and we haven't exceeded retry limit
                // Increment retry counter, release lease, DON'T advance cursor (retry!)
                spdlog::info("Single message failure completing batch - retry {}/{}", batch_retry_count + 1, retry_limit);
                
                std::string retry_sql = R"(
                    UPDATE partition_consumers
                    SET lease_expires_at = NULL,
                        lease_acquired_at = NULL,
                        message_batch = NULL,
                        batch_size = 0,
                        acked_count = 0,
                        worker_id = NULL,
                        batch_retry_count = COALESCE(batch_retry_count, 0) + 1
                    WHERE partition_id = $1::uuid
                      AND consumer_group = $2
                )";
                
                sendQueryParamsAsync(conn.get(), retry_sql, {partition_id, consumer_group});
                getCommandResult(conn.get());
                
                result.success = true;
                result.message = "failed_retry";
                return result;
            }
            
            // Either not last in batch, or retry limit exceeded - move to DLQ
            std::string dlq_sql = R"(
                INSERT INTO dead_letter_queue (
                    message_id, partition_id, consumer_group, error_message, 
                    retry_count, original_created_at
                )
                SELECT m.id, m.partition_id, $1::varchar, $2::text, 0, m.created_at
                FROM messages m
                WHERE m.partition_id = $3::uuid
                  AND m.transaction_id = $4
                  AND NOT EXISTS (
                      SELECT 1 FROM dead_letter_queue dlq
                      WHERE dlq.message_id = m.id AND dlq.consumer_group = $1::varchar
                  )
            )";
            
            sendQueryParamsAsync(conn.get(), dlq_sql, {
                consumer_group,
                error.value_or("Message processing failed"),
                partition_id,
                transaction_id
            });
            getCommandResult(conn.get());
            
            // Advance cursor for failed messages
            std::string cursor_sql = R"(
                UPDATE partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    batch_retry_count = 0,
                    acked_count = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + 1
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                FROM messages m
                WHERE partition_consumers.partition_id = $1::uuid
                  AND partition_consumers.consumer_group = $2
                  AND m.partition_id = $1::uuid
                  AND m.transaction_id = $3
                RETURNING (partition_consumers.lease_expires_at IS NULL) as lease_released
            )";
            
            sendQueryParamsAsync(conn.get(), cursor_sql, {partition_id, consumer_group, transaction_id});
            auto cursor_result = getTuplesResult(conn.get());
            
            bool lease_released = PQntuples(cursor_result.get()) > 0 &&
                                 std::string(PQgetvalue(cursor_result.get(), 0, 0)) == "t";
            
            if (lease_released) {
                // Insert analytics for failed message
                std::string insert_consumed_sql = R"(
                    INSERT INTO messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                    VALUES ($1, $2, 0, 1, NOW())
                )";
                sendQueryParamsAsync(conn.get(), insert_consumed_sql, {partition_id, consumer_group});
                getCommandResult(conn.get());
            }
            
            spdlog::warn("Message failed and moved to DLQ: {} - {}", transaction_id, error.value_or("No error message"));
            result.success = true;
            result.message = "failed_dlq";
            return result;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to acknowledge message: {}", e.what());
        result.error = e.what();
    }
    
    return result;
}

AsyncQueueManager::BatchAckResult AsyncQueueManager::acknowledge_messages_batch(
    const std::vector<nlohmann::json>& acknowledgments
) {
    BatchAckResult batch_result;
    batch_result.successful_acks = 0;
    batch_result.failed_acks = 0;
    
    if (acknowledgments.empty()) {
        return batch_result;
    }
    
    try {
        // CRITICAL: Use ONE connection for entire batch
        auto conn = async_db_pool_->acquire();
        
        spdlog::debug("Batch ACK: Processing {} acknowledgments with SINGLE connection", acknowledgments.size());
        
        // Begin transaction for atomic batch ACK
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            // Extract consumer_group from first ack (should be same for all)
            std::string consumer_group = acknowledgments[0].value("consumerGroup", "__QUEUE_MODE__");
            
            // Group by status
            std::vector<nlohmann::json> completed;
            std::vector<nlohmann::json> failed;
            
            for (const auto& ack : acknowledgments) {
                std::string status = ack.value("status", "completed");
                if (status == "completed") {
                    completed.push_back(ack);
                } else if (status == "failed") {
                    failed.push_back(ack);
                }
            }
            
            int total_messages = acknowledgments.size();
            int failed_count = failed.size();
            int success_count = completed.size();
            
            // Build array of all transaction IDs
            std::vector<std::string> all_txn_ids;
            for (const auto& ack : acknowledgments) {
                all_txn_ids.push_back(ack.value("transactionId", ""));
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
            
            std::string txn_array = build_pg_array(all_txn_ids);
            
            // Get partition_id from first ack
            if (!acknowledgments[0].contains("partitionId") || acknowledgments[0]["partitionId"].is_null()) {
                throw std::runtime_error("partition_id is required for batch acknowledgment");
            }
            
            std::string partition_id = acknowledgments[0]["partitionId"].get<std::string>();
            
            // Get batch size for the partition
            std::string batch_size_sql = R"(
                SELECT pc.batch_size
                FROM partition_consumers pc
                WHERE pc.partition_id = $1::uuid
                  AND pc.consumer_group = $2
                LIMIT 1
            )";
            
            sendQueryParamsAsync(conn.get(), batch_size_sql, {partition_id, consumer_group});
            auto batch_size_result = getTuplesResult(conn.get());
            
            if (PQntuples(batch_size_result.get()) == 0) {
                throw std::runtime_error("Cannot find batch size for partition");
            }
            
            const char* batch_size_str = PQgetvalue(batch_size_result.get(), 0, 0);
            int leased_batch_size = (batch_size_str && strlen(batch_size_str) > 0) ? std::stoi(batch_size_str) : 0;
            
            // Check if this is a total batch failure
            bool is_total_batch_failure = (failed_count == total_messages) && (total_messages == leased_batch_size);
            
            if (is_total_batch_failure) {
                // Get retry info
                std::string retry_info_sql = R"(
                    SELECT 
                        q.retry_limit,
                        pc.batch_retry_count
                    FROM messages m
                    JOIN partitions p ON p.id = m.partition_id
                    JOIN queues q ON q.id = p.queue_id
                    JOIN partition_consumers pc ON pc.partition_id = m.partition_id 
                      AND pc.consumer_group = $2
                    WHERE m.partition_id = $3::uuid
                      AND m.transaction_id = ANY($1::varchar[])
                    LIMIT 1
                )";
                
                sendQueryParamsAsync(conn.get(), retry_info_sql, {txn_array, consumer_group, partition_id});
                auto retry_info = getTuplesResult(conn.get());
                
                if (PQntuples(retry_info.get()) == 0) {
                    throw std::runtime_error("Cannot find queue retry limit");
                }
                
                const char* retry_limit_str = PQgetvalue(retry_info.get(), 0, 0);
                const char* batch_retry_str = PQgetvalue(retry_info.get(), 0, 1);
                
                int retry_limit = (retry_limit_str && strlen(retry_limit_str) > 0) ? std::stoi(retry_limit_str) : 3;
                int current_retry_count = (batch_retry_str && strlen(batch_retry_str) > 0) ? std::stoi(batch_retry_str) : 0;
                
                if (current_retry_count >= retry_limit) {
                    // Retry limit exceeded - move to DLQ and advance cursor
                    spdlog::warn("Batch retry limit exceeded ({}/{}), moving to DLQ", current_retry_count, retry_limit);
                    
                    std::vector<std::string> failed_errors;
                    for (const auto& ack : failed) {
                        std::string error_msg = "Batch retry limit exceeded";
                        if (ack.contains("error") && !ack["error"].is_null()) {
                            error_msg = ack["error"].get<std::string>();
                        }
                        failed_errors.push_back(error_msg);
                    }
                    
                    std::string error_array = build_pg_array(failed_errors);
                    
                    std::string dlq_sql = R"(
                        INSERT INTO dead_letter_queue (message_id, partition_id, consumer_group, error_message, original_created_at)
                        SELECT 
                            m.id,
                            m.partition_id,
                            $2,
                            e.error_message,
                            m.created_at
                        FROM messages m
                        CROSS JOIN LATERAL UNNEST($1::varchar[], $4::text[]) AS e(txn_id, error_message)
                        WHERE m.partition_id = $3::uuid
                          AND m.transaction_id = e.txn_id
                        ON CONFLICT DO NOTHING
                    )";
                    
                    sendQueryParamsAsync(conn.get(), dlq_sql, {txn_array, consumer_group, partition_id, error_array});
                    getCommandResult(conn.get());
                    
                    // Get last message for cursor
                    std::string last_msg_sql = R"(
                        SELECT id, created_at
                        FROM messages m
                        WHERE m.partition_id = $1::uuid
                          AND m.transaction_id = ANY($2::varchar[])
                        ORDER BY m.created_at DESC, m.id DESC
                        LIMIT 1
                    )";
                    
                    sendQueryParamsAsync(conn.get(), last_msg_sql, {partition_id, txn_array});
                    auto last_msg = getTuplesResult(conn.get());
                    
                    std::string last_id = PQgetvalue(last_msg.get(), 0, 0);
                    std::string last_created = PQgetvalue(last_msg.get(), 0, 1);
                    
                    // Advance cursor and reset retry count
                    std::string cursor_sql = R"(
                        UPDATE partition_consumers
                        SET 
                            last_consumed_created_at = $1,
                            last_consumed_id = $2,
                            total_messages_consumed = total_messages_consumed + $3,
                            total_batches_consumed = total_batches_consumed + 1,
                            last_consumed_at = NOW(),
                            batch_retry_count = 0,
                            pending_estimate = GREATEST(0, pending_estimate - $3),
                            last_stats_update = NOW(),
                            lease_expires_at = NULL,
                            lease_acquired_at = NULL,
                            message_batch = NULL,
                            batch_size = 0,
                            acked_count = 0,
                            worker_id = NULL
                        WHERE partition_id = $4 AND consumer_group = $5
                    )";
                    
                    sendQueryParamsAsync(conn.get(), cursor_sql, {last_created, last_id, std::to_string(total_messages), partition_id, consumer_group});
                    getCommandResult(conn.get());
                    
                    // Insert analytics
                    std::string insert_consumed_sql = R"(
                        INSERT INTO messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                        VALUES ($1, $2, 0, $3, NOW())
                    )";
                    sendQueryParamsAsync(conn.get(), insert_consumed_sql, {partition_id, consumer_group, std::to_string(failed_count)});
                    getCommandResult(conn.get());
                    
                    for (const auto& ack : failed) {
                        AckResult result;
                        result.success = true;
                        result.message = "failed_dlq";
                        batch_result.results.push_back(result);
                        batch_result.successful_acks++;
                    }
                    
                } else {
                    // Batch retry - increment counter, release lease, DON'T advance cursor
                    spdlog::info("Total batch failure - retry {}/{}", current_retry_count + 1, retry_limit);
                    
                    std::string retry_sql = R"(
                        UPDATE partition_consumers pc
                        SET lease_expires_at = NULL,
                            lease_acquired_at = NULL,
                            message_batch = NULL,
                            batch_size = 0,
                            acked_count = 0,
                            worker_id = NULL,
                            batch_retry_count = COALESCE(batch_retry_count, 0) + 1
                        WHERE pc.partition_id = $1 AND pc.consumer_group = $2
                    )";
                    
                    sendQueryParamsAsync(conn.get(), retry_sql, {partition_id, consumer_group});
                    getCommandResult(conn.get());
                    
                    for (const auto& ack : failed) {
                        AckResult result;
                        result.success = true;
                        result.message = "failed_retry";
                        batch_result.results.push_back(result);
                        batch_result.successful_acks++;
                    }
                }
                
                // Commit and exit early for total batch failure
                sendAndWait(conn.get(), "COMMIT");
                getCommandResult(conn.get());
                
                return batch_result;
            }
            
            // Partial success - advance cursor, DLQ individual failures
            
            // Get last message info for cursor update
            std::string batch_info_sql = R"(
                SELECT 
                    m.id,
                    m.created_at
                FROM messages m
                WHERE m.partition_id = $1::uuid
                  AND m.transaction_id = ANY($2::varchar[])
                ORDER BY m.created_at DESC, m.id DESC
                LIMIT 1
            )";
            
            sendQueryParamsAsync(conn.get(), batch_info_sql, {partition_id, txn_array});
            auto batch_info = getTuplesResult(conn.get());
            
            if (PQntuples(batch_info.get()) == 0) {
                throw std::runtime_error("No messages found for acknowledgment");
            }
            
            std::string last_id = PQgetvalue(batch_info.get(), 0, 0);
            std::string last_created = PQgetvalue(batch_info.get(), 0, 1);
            
            // Move failed messages to DLQ
            if (!failed.empty()) {
                std::vector<std::string> failed_txn_ids;
                std::vector<std::string> failed_errors;
                
                for (const auto& ack : failed) {
                    failed_txn_ids.push_back(ack.value("transactionId", ""));
                    std::string error_msg = "Unknown error";
                    if (ack.contains("error") && !ack["error"].is_null()) {
                        error_msg = ack["error"].get<std::string>();
                    }
                    failed_errors.push_back(error_msg);
                }
                
                std::string failed_array = build_pg_array(failed_txn_ids);
                std::string error_array = build_pg_array(failed_errors);
                
                std::string dlq_sql = R"(
                    INSERT INTO dead_letter_queue (message_id, partition_id, consumer_group, error_message, original_created_at)
                    SELECT 
                        m.id,
                        m.partition_id,
                        $2,
                        e.error_message,
                        m.created_at
                    FROM messages m
                    CROSS JOIN LATERAL UNNEST($1::varchar[], $4::text[]) AS e(txn_id, error_message)
                    WHERE m.partition_id = $3::uuid
                      AND m.transaction_id = e.txn_id
                    ON CONFLICT DO NOTHING
                )";
                
                sendQueryParamsAsync(conn.get(), dlq_sql, {failed_array, consumer_group, partition_id, error_array});
                getCommandResult(conn.get());
                
                spdlog::info("Moved {} failed messages to DLQ", failed_count);
            }
            
            // Advance cursor atomically
            std::string cursor_sql = R"(
                UPDATE partition_consumers
                SET 
                    last_consumed_created_at = $1,
                    last_consumed_id = $2,
                    total_messages_consumed = total_messages_consumed + $3,
                    total_batches_consumed = total_batches_consumed + 1,
                    last_consumed_at = NOW(),
                    batch_retry_count = 0,
                    pending_estimate = GREATEST(0, pending_estimate - $3),
                    last_stats_update = NOW(),
                    acked_count = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + $3
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    message_batch = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE message_batch 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                WHERE partition_id = $4 AND consumer_group = $5
                RETURNING (lease_expires_at IS NULL) as lease_released
            )";
            
            sendQueryParamsAsync(conn.get(), cursor_sql, {
                last_created, last_id, std::to_string(total_messages), partition_id, consumer_group
            });
            auto cursor_result = getTuplesResult(conn.get());
            
            bool lease_released = PQntuples(cursor_result.get()) > 0 && 
                                 std::string(PQgetvalue(cursor_result.get(), 0, 0)) == "t";
            
            spdlog::debug("Batch ACK: Success={} Failed={} Lease={}", 
                        success_count, failed_count, lease_released ? "Released" : "Active");
            
            // Insert analytics record if lease was released (batch complete)
            if (lease_released) {
                std::string insert_consumed_sql = R"(
                    INSERT INTO messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                    VALUES ($1, $2, $3, $4, NOW())
                )";
                
                sendQueryParamsAsync(conn.get(), insert_consumed_sql, {
                    partition_id, 
                    consumer_group, 
                    std::to_string(success_count), 
                    std::to_string(failed_count)
                });
                getCommandResult(conn.get());
            }
            
            // Commit transaction
            sendAndWait(conn.get(), "COMMIT");
            getCommandResult(conn.get());
            
            // Build results
            for (const auto& ack : acknowledgments) {
                AckResult result;
                std::string status = ack.value("status", "completed");
                result.success = true;
                result.message = (status == "completed") ? "completed" : "failed_dlq";
                batch_result.results.push_back(result);
                batch_result.successful_acks++;
            }
            
        } catch (const std::exception& e) {
            // Rollback on error
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Batch acknowledgment failed: {}", e.what());
        
        // Return all as failed
        for (size_t i = 0; i < acknowledgments.size(); ++i) {
            AckResult result;
            result.success = false;
            result.message = "batch_failed";
            result.error = e.what();
            batch_result.results.push_back(result);
            batch_result.failed_acks++;
        }
    }
    
    return batch_result;
}

// ===================================
// TRANSACTION HELPERS - USE PROVIDED CONNECTION
// ===================================

PushResult AsyncQueueManager::push_single_message_transactional(
    PGconn* conn,
    const PushItem& item
) {
    PushResult result;
    result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
    
    try {
        // Ensure queue exists
        if (!ensure_queue_exists(conn, item.queue, "", "")) {
            result.status = "failed";
            result.error = "Failed to ensure queue exists";
            return result;
        }
        
        // Ensure partition exists
        if (!ensure_partition_exists(conn, item.queue, item.partition)) {
            result.status = "failed";
            result.error = "Failed to create partition";
            return result;
        }
        
        // Generate message ID
        std::string message_id = generate_uuid();
        
        // Check encryption
        sendQueryParamsAsync(conn, "SELECT encryption_enabled FROM queues WHERE name = $1", {item.queue});
        auto encryption_result = getTuplesResult(conn);
        
        bool encryption_enabled = false;
        if (PQntuples(encryption_result.get()) > 0) {
            const char* enc_str = PQgetvalue(encryption_result.get(), 0, 0);
            encryption_enabled = (enc_str && (std::string(enc_str) == "t" || std::string(enc_str) == "true"));
        }
        
        // Prepare payload
        std::string payload_str = item.payload.dump();
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
        
        // Insert message
        std::string sql;
        std::vector<std::string> params;
        
        if (item.trace_id.has_value() && !item.trace_id->empty()) {
            sql = R"(
                INSERT INTO messages (
                    id, transaction_id, partition_id, payload, trace_id, is_encrypted, created_at
                )
                SELECT $1, $2, p.id, $3, $4, $5, NOW()
                FROM partitions p
                JOIN queues q ON q.id = p.queue_id
                WHERE q.name = $6 AND p.name = $7
                RETURNING id, trace_id
            )";
            
            params = {
                message_id,
                result.transaction_id,
                payload_str,
                item.trace_id.value(),
                is_encrypted ? "true" : "false",
                item.queue,
                item.partition
            };
        } else {
            sql = R"(
                INSERT INTO messages (
                    id, transaction_id, partition_id, payload, is_encrypted, created_at
                )
                SELECT $1, $2, p.id, $3, $4, NOW()
                FROM partitions p
                JOIN queues q ON q.id = p.queue_id
                WHERE q.name = $5 AND p.name = $6
                RETURNING id, trace_id
            )";
            
            params = {
                message_id,
                result.transaction_id,
                payload_str,
                is_encrypted ? "true" : "false",
                item.queue,
                item.partition
            };
        }
        
        sendQueryParamsAsync(conn, sql, params);
        auto query_result = getTuplesResult(conn);
        
        if (PQntuples(query_result.get()) > 0) {
            result.status = "queued";
            result.message_id = PQgetvalue(query_result.get(), 0, 0);
            const char* trace_val = PQgetvalue(query_result.get(), 0, 1);
            if (trace_val && strlen(trace_val) > 0) {
                result.trace_id = trace_val;
            }
        } else {
            result.status = "failed";
            result.error = "Insert returned no result";
        }
        
    } catch (const std::exception& e) {
        result.status = "failed";
        result.error = e.what();
        spdlog::error("Failed to push message in transaction: {}", e.what());
    }
    
    return result;
}

AsyncQueueManager::AckResult AsyncQueueManager::acknowledge_message_transactional(
    PGconn* conn,
    const std::string& transaction_id,
    const std::string& status,
    const std::optional<std::string>& error,
    const std::string& consumer_group,
    const std::optional<std::string>& partition_id_param
) {
    AckResult result;
    result.success = false;
    result.message = "not_found";
    
    try {
        // CRITICAL: partition_id is MANDATORY
        if (!partition_id_param.has_value() || partition_id_param->empty()) {
            spdlog::error("partition_id is required for acknowledgment of transaction {}", transaction_id);
            result.message = "invalid_request";
            result.error = "partition_id is required";
            return result;
        }
        
        std::string partition_id = *partition_id_param;
        
        if (status == "completed") {
            // Update cursor - simplified for transactions (no retry logic)
            std::string sql = R"(
                UPDATE partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    acked_count = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + 1
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                FROM messages m
                WHERE partition_consumers.partition_id = $1::uuid
                  AND partition_consumers.consumer_group = $2
                  AND m.partition_id = $1::uuid
                  AND m.transaction_id = $3
                RETURNING (partition_consumers.lease_expires_at IS NULL) as lease_released
            )";
            
            sendQueryParamsAsync(conn, sql, {partition_id, consumer_group, transaction_id});
            auto query_result = getTuplesResult(conn);
            
            if (PQntuples(query_result.get()) > 0) {
                const char* lease_released_val = PQgetvalue(query_result.get(), 0, 0);
                bool lease_released = (lease_released_val && (std::string(lease_released_val) == "t"));
                
                if (lease_released) {
                    std::string insert_consumed_sql = R"(
                        INSERT INTO messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                        VALUES ($1, $2, 1, 0, NOW())
                    )";
                    sendQueryParamsAsync(conn, insert_consumed_sql, {partition_id, consumer_group});
                    getCommandResult(conn);
                }
                
                result.success = true;
                result.message = "completed";
            }
            
        } else if (status == "failed") {
            // For transactions: Move to DLQ and advance cursor (no retry logic)
            std::string dlq_sql = R"(
                INSERT INTO dead_letter_queue (
                    message_id, partition_id, consumer_group, error_message, 
                    retry_count, original_created_at
                )
                SELECT m.id, m.partition_id, $1::varchar, $2::text, 0, m.created_at
                FROM messages m
                WHERE m.partition_id = $3::uuid
                  AND m.transaction_id = $4
                  AND NOT EXISTS (
                      SELECT 1 FROM dead_letter_queue dlq
                      WHERE dlq.message_id = m.id AND dlq.consumer_group = $1::varchar
                  )
            )";
            
            sendQueryParamsAsync(conn, dlq_sql, {
                consumer_group,
                error.value_or("Message processing failed"),
                partition_id,
                transaction_id
            });
            getCommandResult(conn);
            
            // Advance cursor
            std::string cursor_sql = R"(
                UPDATE partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    batch_retry_count = 0,
                    acked_count = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + 1
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                FROM messages m
                WHERE partition_consumers.partition_id = $1::uuid
                  AND partition_consumers.consumer_group = $2
                  AND m.partition_id = $1::uuid
                  AND m.transaction_id = $3
                RETURNING (partition_consumers.lease_expires_at IS NULL) as lease_released
            )";
            
            sendQueryParamsAsync(conn, cursor_sql, {partition_id, consumer_group, transaction_id});
            auto cursor_result = getTuplesResult(conn);
            
            bool lease_released = PQntuples(cursor_result.get()) > 0 &&
                                 std::string(PQgetvalue(cursor_result.get(), 0, 0)) == "t";
            
            if (lease_released) {
                std::string insert_consumed_sql = R"(
                    INSERT INTO messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                    VALUES ($1, $2, 0, 1, NOW())
                )";
                sendQueryParamsAsync(conn, insert_consumed_sql, {partition_id, consumer_group});
                getCommandResult(conn);
            }
            
            result.success = true;
            result.message = "failed_dlq";
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to acknowledge message in transaction: {}", e.what());
        result.error = e.what();
    }
    
    return result;
}

// ===================================
// TRANSACTION OPERATIONS - ASYNC VERSION
// ===================================

AsyncQueueManager::TransactionResult AsyncQueueManager::execute_transaction(
    const std::vector<nlohmann::json>& operations
) {
    TransactionResult txn_result;
    txn_result.transaction_id = generate_transaction_id();
    txn_result.success = true;
    txn_result.results.reserve(operations.size());
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Begin transaction
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            for (const auto& op : operations) {
                std::string op_type = op.value("type", "");
                nlohmann::json op_result;
                
                if (op_type == "push") {
                    // Push operation - use transactional helper with shared connection
                    if (!op.contains("items") || !op["items"].is_array()) {
                        op_result["type"] = "push";
                        op_result["error"] = "items array required for push operation";
                        txn_result.success = false;
                        txn_result.results.push_back(op_result);
                        continue;
                    }
                    
                    auto items_json = op["items"];
                    op_result["type"] = "push";
                    op_result["count"] = items_json.size();
                    op_result["results"] = nlohmann::json::array();
                    
                    for (const auto& item_json : items_json) {
                        PushItem item;
                        item.queue = item_json.value("queue", "");
                        item.partition = item_json.value("partition", "Default");
                        item.payload = item_json.contains("data") ? item_json["data"] : item_json.value("payload", nlohmann::json{});
                        
                        if (item_json.contains("transactionId") && !item_json["transactionId"].is_null()) {
                            item.transaction_id = item_json["transactionId"].get<std::string>();
                        }
                        if (item_json.contains("traceId") && !item_json["traceId"].is_null()) {
                            item.trace_id = item_json["traceId"].get<std::string>();
                        }
                        
                        // Use transactional helper with shared connection
                        auto pr = push_single_message_transactional(conn.get(), item);
                        
                        nlohmann::json push_res;
                        push_res["status"] = pr.status;
                        push_res["transactionId"] = pr.transaction_id;
                        if (pr.message_id.has_value()) {
                            push_res["messageId"] = *pr.message_id;
                        }
                        if (pr.error.has_value()) {
                            push_res["error"] = *pr.error;
                            txn_result.success = false;
                        }
                        op_result["results"].push_back(push_res);
                    }
                    
                } else if (op_type == "pop") {
                    // Pop operation - WARNING: This acquires its own connection (not truly atomic)
                    // TODO: Create pop_with_conn version for true atomicity
                    std::string queue = op.value("queue", "");
                    std::optional<std::string> partition = (op.contains("partition") && !op["partition"].is_null()) ?
                        std::optional<std::string>(op["partition"].get<std::string>()) : std::nullopt;
                    std::string consumer_group = op.value("consumerGroup", "__QUEUE_MODE__");
                    
                    PopOptions pop_opts;
                    pop_opts.batch = op.value("batch", 1);
                    pop_opts.auto_ack = op.value("autoAck", false);
                    
                    PopResult pop_result;
                    if (partition.has_value()) {
                        pop_result = pop_messages_from_partition(queue, *partition, consumer_group, pop_opts);
                    } else {
                        pop_result = pop_messages_from_queue(queue, consumer_group, pop_opts);
                    }
                    
                    op_result["type"] = "pop";
                    op_result["messages"] = nlohmann::json::array();
                    for (const auto& msg : pop_result.messages) {
                        nlohmann::json msg_json;
                        msg_json["id"] = msg.id;
                        msg_json["transactionId"] = msg.transaction_id;
                        msg_json["partitionId"] = msg.partition_id;
                        msg_json["payload"] = msg.payload;
                        msg_json["queueName"] = msg.queue_name;
                        msg_json["partitionName"] = msg.partition_name;
                        if (!msg.trace_id.empty()) {
                            msg_json["traceId"] = msg.trace_id;
                        }
                        op_result["messages"].push_back(msg_json);
                    }
                    if (pop_result.lease_id.has_value()) {
                        op_result["leaseId"] = *pop_result.lease_id;
                    }
                    
                } else if (op_type == "ack") {
                    // Ack operation - use transactional helper with shared connection
                    std::string transaction_id = op.value("transactionId", "");
                    std::string status = op.value("status", "completed");
                    std::optional<std::string> error = (op.contains("error") && !op["error"].is_null()) ?
                        std::optional<std::string>(op["error"].get<std::string>()) : std::nullopt;
                    std::string consumer_group = op.value("consumerGroup", "__QUEUE_MODE__");
                    std::optional<std::string> partition_id = (op.contains("partitionId") && !op["partitionId"].is_null()) ?
                        std::optional<std::string>(op["partitionId"].get<std::string>()) : std::nullopt;
                    
                    // Use transactional helper with shared connection
                    auto ack_result = acknowledge_message_transactional(conn.get(), transaction_id, status, error, consumer_group, partition_id);
                    
                    op_result["type"] = "ack";
                    op_result["success"] = ack_result.success;
                    op_result["message"] = ack_result.message;
                    if (ack_result.error.has_value()) {
                        op_result["error"] = *ack_result.error;
                    }
                    
                    if (!ack_result.success) {
                        txn_result.success = false;
                    }
                } else {
                    op_result["type"] = "unknown";
                    op_result["error"] = "Unknown operation type: " + op_type;
                    txn_result.success = false;
                }
                
                txn_result.results.push_back(op_result);
            }
            
            // Check if transaction should commit or rollback
            if (txn_result.success) {
                // All operations succeeded - commit
                sendAndWait(conn.get(), "COMMIT");
                getCommandResult(conn.get());
                spdlog::debug("Transaction committed successfully");
            } else {
                // At least one operation failed - rollback
                sendAndWait(conn.get(), "ROLLBACK");
                getCommandResult(conn.get());
                spdlog::warn("Transaction rolled back due to operation failure");
            }
            
        } catch (const std::exception& e) {
            // Rollback on exception
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Transaction failed: {}", e.what());
        txn_result.success = false;
        txn_result.error = e.what();
    }
    
    return txn_result;
}

} // namespace queen

