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
    
    // TODO: Batch capacity check could be added here (similar to regular QueueManager)
    
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

} // namespace queen

