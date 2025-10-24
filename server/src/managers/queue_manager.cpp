#include "queen/queue_manager.hpp"
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

QueueManager::QueueManager(std::shared_ptr<DatabasePool> db_pool, const QueueConfig& config)
    : db_pool_(db_pool), config_(config) {
    if (!db_pool_) {
        throw std::invalid_argument("Database pool cannot be null");
    }
}

std::string QueueManager::generate_uuid() {
    static std::mutex uuid_mutex;
    static uint64_t last_ms = 0;
    static uint16_t sequence = 0;
    
    // Static random number generator to be seeded once
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    
    std::lock_guard<std::mutex> lock(uuid_mutex);
    
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    uint64_t current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // If the clock moved backwards or stayed the same, increment the sequence
    if (current_ms <= last_ms) {
        sequence++;
    } else {
        last_ms = current_ms;
        // If time has advanced, we can reset the sequence or re-randomize it
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
    uint16_t sequence_and_version = sequence & 0x0FFF; // Ensure sequence is 12 bits max
    bytes[6] = 0x70 | (sequence_and_version >> 8);
    bytes[7] = sequence_and_version & 0xFF;

    // 2-bit variant (10) and 62-bits of random data
    uint64_t rand_data = gen();
    bytes[8] = 0x80 | ((rand_data >> 56) & 0x3F); // variant is 10xx xxxx
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

/*
std::string QueueManager::generate_uuid() {
    // UUIDv7 with monotonic counter - ensures strict ordering within same millisecond
    static std::mutex uuid_mutex;
    static uint64_t last_timestamp_ms = 0;
    static uint64_t sequence_counter = 0;
    
    std::lock_guard<std::mutex> lock(uuid_mutex);
    
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    // Monotonic counter for same-millisecond UUIDs
    if (ms == last_timestamp_ms) {
        sequence_counter++;
    } else {
        last_timestamp_ms = ms;
        sequence_counter = 0;
    }
    
    // Build 16-byte UUID
    std::array<uint8_t, 16> bytes;
    
    // Timestamp (48 bits = 6 bytes)
    bytes[0] = (ms >> 40) & 0xFF;
    bytes[1] = (ms >> 32) & 0xFF;
    bytes[2] = (ms >> 24) & 0xFF;
    bytes[3] = (ms >> 16) & 0xFF;
    bytes[4] = (ms >> 8) & 0xFF;
    bytes[5] = ms & 0xFF;
    
    // Sequence counter in bytes 6-15 for monotonic ordering
    bytes[6] = (sequence_counter >> 24) & 0xFF;
    bytes[7] = (sequence_counter >> 16) & 0xFF;
    bytes[8] = (sequence_counter >> 8) & 0xFF;
    bytes[9] = sequence_counter & 0xFF;
    bytes[10] = (sequence_counter >> 32) & 0xFF;
    bytes[11] = (sequence_counter >> 40) & 0xFF;
    bytes[12] = (sequence_counter >> 48) & 0xFF;
    bytes[13] = (sequence_counter >> 56) & 0xFF;
    bytes[14] = 0;
    bytes[15] = 0;
    
    // Set version (7) and variant bits
    bytes[6] = (bytes[6] & 0x0F) | 0x70;  // Version 7
    bytes[8] = (bytes[8] & 0x3F) | 0x80;  // Variant 10
    
    // Format as string: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) ss << '-';
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    
    return ss.str();
}*/

// Helper for FileBufferManager - push single message with explicit parameters
void QueueManager::push_single_message(
    const std::string& queue_name,
    const std::string& partition_name,
    const nlohmann::json& payload,
    const std::string& namespace_name,
    const std::string& task,
    const std::string& transaction_id,
    const std::string& trace_id
) {
    auto conn = db_pool_->get_connection();
    
    // Generate transaction ID if not provided
    std::string tx_id = transaction_id.empty() ? generate_uuid() : transaction_id;
    
    // Ensure queue exists
    ensure_queue_exists(queue_name, namespace_name, task);
    
    // Ensure partition exists
    ensure_partition_exists(queue_name, partition_name);
    
    // Get next sequence number
    auto seq_result = QueryResult(conn->exec_params(R"(
        SELECT COALESCE(MAX(sequence), 0) + 1 as next_seq
        FROM queen.messages
        WHERE queue_name = $1 AND partition_name = $2
    )", {queue_name, partition_name}));
    
    int64_t sequence = 1;
    if (seq_result.num_rows() > 0) {
        sequence = std::stoll(seq_result.get_value(0, "next_seq"));
    }
    
    // Serialize payload
    std::string payload_str = payload.dump();
    
    // TODO: Add encryption support
    // For now, encryption is handled by push_messages()
    // This helper is for simple file buffer use case
    
    // Insert message
    auto insert_sql = R"(
        INSERT INTO queen.messages 
        (queue_name, partition_name, payload, transaction_id, trace_id, 
         namespace, task, status, sequence, created_at)
        VALUES ($1, $2, $3, $4, $5, $6, $7, 'pending', $8, NOW())
    )";
    
    conn->exec_params(insert_sql, {
        queue_name,
        partition_name,
        payload_str,
        tx_id,
        trace_id,
        namespace_name,
        task,
        std::to_string(sequence)
    });
    
    db_pool_->return_connection(std::move(conn));
}

std::string QueueManager::generate_transaction_id() {
    return generate_uuid();
}

bool QueueManager::ensure_queue_exists(const std::string& queue_name, 
                                     const std::string& namespace_name,
                                     const std::string& task_name) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string sql = R"(
            INSERT INTO queen.queues (name, namespace, task, priority, lease_time, retry_limit, retry_delay, max_size, ttl)
            VALUES ($1, $2, $3, 0, 300, 3, 1000, 10000, 3600)
            ON CONFLICT (name) DO NOTHING
        )";
        
        std::vector<std::string> params = {
            queue_name,
            namespace_name.empty() ? "" : namespace_name,
            task_name.empty() ? "" : task_name
        };
        
        auto result = QueryResult(conn->exec_params(sql, params));
        return result.is_success();
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to ensure queue exists: {}", e.what());
        return false;
    }
}

bool QueueManager::ensure_partition_exists(const std::string& queue_name, 
                                         const std::string& partition_name) {
    try {
        spdlog::debug("Ensuring partition exists: queue='{}', partition='{}'", queue_name, partition_name);
        
        ScopedConnection conn(db_pool_.get());
        
        std::string sql = R"(
            INSERT INTO queen.partitions (queue_id, name)
            SELECT id, $2 FROM queen.queues WHERE name = $1
            ON CONFLICT (queue_id, name) DO NOTHING
            RETURNING id, name
        )";
        
        std::vector<std::string> params = {queue_name, partition_name};
        auto result = QueryResult(conn->exec_params(sql, params));
        
        if (result.is_success()) {
            if (result.num_rows() > 0) {
                spdlog::debug("Partition created: id='{}', name='{}'", 
                             result.get_value(0, "id"), result.get_value(0, "name"));
            } else {
                spdlog::debug("Partition already exists (ON CONFLICT triggered)");
            }
            return true;
        } else {
            spdlog::error("Failed to create partition: {}", result.error_message());
            return false;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to ensure partition exists: {}", e.what());
        return false;
    }
}

bool QueueManager::delete_queue(const std::string& queue_name) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Start transaction and increase statement timeout for large queue deletions
        // Deleting millions of messages with cascading deletes can take time
        if (!conn->begin_transaction()) {
            spdlog::error("Failed to begin transaction for queue deletion");
            return false;
        }
        
        // Set timeout to 10 minutes for this transaction
        auto timeout_result = QueryResult(conn->exec("SET LOCAL statement_timeout = '600000'"));
        if (!timeout_result.is_success()) {
            conn->rollback_transaction();
            spdlog::error("Failed to set statement timeout");
            return false;
        }
        
        // First, delete consumer state for all partitions in this queue
        std::string delete_consumers_sql = R"(
            DELETE FROM queen.partition_consumers
            WHERE partition_id IN (
                SELECT p.id FROM queen.partitions p
                JOIN queen.queues q ON p.queue_id = q.id
                WHERE q.name = $1
            )
        )";
        auto consumer_result = QueryResult(conn->exec_params(delete_consumers_sql, {queue_name}));
        if (!consumer_result.is_success()) {
            conn->rollback_transaction();
            spdlog::error("Failed to delete consumer state for queue {}", queue_name);
            return false;
        }
        
        // Delete the queue (cascading deletes will handle partitions, messages, etc.)
        std::string sql = "DELETE FROM queen.queues WHERE name = $1 RETURNING id";
        auto result = QueryResult(conn->exec_params(sql, {queue_name}));
        
        if (result.is_success() && result.num_rows() > 0) {
            conn->commit_transaction();
            spdlog::info("Deleted queue and consumer state: {}", queue_name);
            return true;
        }
        
        conn->rollback_transaction();
        return false;
    } catch (const std::exception& e) {
        spdlog::error("Failed to delete queue {}: {}", queue_name, e.what());
        return false;
    }
}

bool QueueManager::configure_queue(const std::string& queue_name, 
                                 const QueueOptions& options,
                                 const std::string& namespace_name,
                                 const std::string& task_name) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string sql = R"(
            INSERT INTO queen.queues (
                name, namespace, task, priority, lease_time, retry_limit, retry_delay,
                max_size, ttl, dead_letter_queue, dlq_after_max_retries, delayed_processing,
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
                max_size = EXCLUDED.max_size,
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
        
        auto result = QueryResult(conn->exec_params(sql, params));
        if (!result.is_success()) {
            spdlog::error("Failed to configure queue: {}", result.error_message());
            return false;
        }
        
        // Ensure default partition exists
        ensure_partition_exists(queue_name, "Default");
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to configure queue: {}", e.what());
        return false;
    }
}

PushResult QueueManager::push_single_message(const PushItem& item) {
    PushResult result;
    result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
    
    try {
        // Check if queue exists (don't create it automatically)
        ScopedConnection check_conn(db_pool_.get());
        auto queue_check = QueryResult(check_conn->exec_params(
            "SELECT id FROM queen.queues WHERE name = $1", 
            {item.queue}
        ));
        
        if (queue_check.num_rows() == 0) {
            result.status = "failed";
            result.error = "Queue '" + item.queue + "' does not exist. Please create it using the configure endpoint first.";
            return result;
        }
        
        // Ensure partition exists (this can be created automatically)
        if (!ensure_partition_exists(item.queue, item.partition)) {
            result.status = "failed";
            result.error = "Failed to create partition";
            return result;
        }
        
        ScopedConnection conn(db_pool_.get());
        
        // Generate UUIDv7 for message ID to ensure proper time-based ordering
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
        
        auto query_result = QueryResult(conn->exec_params(sql, params));
        if (query_result.is_success() && query_result.num_rows() > 0) {
            result.status = "queued";
            result.message_id = query_result.get_value(0, "id");
            std::string trace_val = query_result.get_value(0, "trace_id");
            if (!trace_val.empty()) {
                result.trace_id = trace_val;
            }
        } else {
            result.status = "failed";
            result.error = query_result.error_message();
        }
        
    } catch (const std::exception& e) {
        result.status = "failed";
        result.error = e.what();
        spdlog::error("Failed to push message: {}", e.what());
    }
    
    return result;
}

std::vector<PushResult> QueueManager::push_messages(const std::vector<PushItem>& items) {
    if (items.empty()) {
        return {};
    }
    
    // Group items by queue and partition while PRESERVING ORDER
    // Use a vector of pairs to maintain insertion order, not std::map
    std::vector<std::pair<std::string, std::vector<size_t>>> partition_groups_ordered;
    std::map<std::string, size_t> key_to_index;
    
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        std::string key = item.queue + ":" + item.partition;
        
        if (key_to_index.find(key) == key_to_index.end()) {
            // New partition group - add to ordered list
            key_to_index[key] = partition_groups_ordered.size();
            partition_groups_ordered.push_back({key, {i}});
        } else {
            // Existing partition group - append index
            size_t group_idx = key_to_index[key];
            partition_groups_ordered[group_idx].second.push_back(i);
        }
    }
    
    std::vector<PushResult> all_results(items.size());
    
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

std::vector<PushResult> QueueManager::push_messages_batch(const std::vector<PushItem>& items) {
    if (items.empty()) {
        return {};
    }
    
    // All items in this batch should have the same queue and partition
    const std::string& queue_name = items[0].queue;
    const std::string& partition_name = items[0].partition;
    
    std::vector<PushResult> results;
    results.reserve(items.size());
    
    // Split into smaller chunks to avoid memory issues and SQL size limits
    const size_t CHUNK_SIZE = config_.batch_push_chunk_size;
    
    for (size_t chunk_start = 0; chunk_start < items.size(); chunk_start += CHUNK_SIZE) {
        size_t chunk_end = std::min(chunk_start + CHUNK_SIZE, items.size());
        std::vector<PushItem> chunk(items.begin() + chunk_start, items.begin() + chunk_end);
        
        auto chunk_results = push_messages_chunk(chunk, queue_name, partition_name);
        results.insert(results.end(), chunk_results.begin(), chunk_results.end());
    }
    
    return results;
}

std::vector<PushResult> QueueManager::push_messages_chunk(const std::vector<PushItem>& items,
                                                          const std::string& queue_name,
                                                          const std::string& partition_name) {
    std::vector<PushResult> results;
    results.reserve(items.size());
    
    try {
        // Check if queue exists (don't create automatically)
        ScopedConnection check_conn(db_pool_.get());
        auto queue_check = QueryResult(check_conn->exec_params(
            "SELECT id FROM queen.queues WHERE name = $1", 
            {queue_name}
        ));
        
        if (queue_check.num_rows() == 0) {
            // All messages in batch fail if queue doesn't exist
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                result.status = "failed";
                result.error = "Queue '" + queue_name + "' does not exist. Please create it using the configure endpoint first.";
                results.push_back(result);
            }
            return results;
        }
        
        // Check max queue size if configured
        std::string size_check_sql = R"(
            SELECT q.max_queue_size, COALESCE(COUNT(m.id), 0) as current_size
            FROM queen.queues q
            LEFT JOIN queen.partitions p ON p.queue_id = q.id
            LEFT JOIN queen.messages m ON m.partition_id = p.id
            WHERE q.name = $1 AND q.max_queue_size > 0
            GROUP BY q.max_queue_size
        )";
        
        auto size_result = QueryResult(check_conn->exec_params(size_check_sql, {queue_name}));
        
        if (size_result.is_success() && size_result.num_rows() > 0) {
            int max_size = std::stoi(size_result.get_value(0, "max_queue_size"));
            int current_size = std::stoi(size_result.get_value(0, "current_size"));
            
            if (current_size + items.size() > max_size) {
                // All messages fail if queue would exceed max size
                for (const auto& item : items) {
                    PushResult result;
                    result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                    result.status = "failed";
                    result.error = "Queue '" + queue_name + "' would exceed max capacity (" + 
                                  std::to_string(max_size) + "). Current: " + std::to_string(current_size) + 
                                  ", Batch: " + std::to_string(items.size());
                    results.push_back(result);
                }
                spdlog::warn("Queue {} rejected push: would exceed max size {} (current: {}, batch: {})",
                           queue_name, max_size, current_size, items.size());
                return results;
            }
        }
        
        // Ensure partition exists
        if (!ensure_partition_exists(queue_name, partition_name)) {
            // All messages fail if partition creation fails
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                result.status = "failed";
                result.error = "Failed to create partition";
                results.push_back(result);
            }
            return results;
        }
        
        // Check if queue has encryption enabled
        std::string encryption_check_sql = "SELECT encryption_enabled FROM queen.queues WHERE name = $1";
        auto encryption_result = QueryResult(check_conn->exec_params(encryption_check_sql, {queue_name}));
        
        bool encryption_enabled = false;
        if (encryption_result.is_success() && encryption_result.num_rows() > 0) {
            std::string enc_str = encryption_result.get_value(0, "encryption_enabled");
            encryption_enabled = (enc_str == "t" || enc_str == "true");
        }
        
        // Get encryption service if needed
        EncryptionService* enc_service = encryption_enabled ? get_encryption_service() : nullptr;
        
        // Get partition ID first
        ScopedConnection conn(db_pool_.get());
        
        std::string partition_id_sql = R"(
            SELECT p.id FROM queen.partitions p
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
        )";
        auto partition_result = QueryResult(conn->exec_params(partition_id_sql, {queue_name, partition_name}));
        
        if (!partition_result.is_success() || partition_result.num_rows() == 0) {
            // All messages fail if partition doesn't exist
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                result.status = "failed";
                result.error = "Partition not found";
                results.push_back(result);
            }
            return results;
        }
        
        std::string partition_id = partition_result.get_value(0, "id");
        
        // Start transaction
        if (!conn->begin_transaction()) {
            throw std::runtime_error("Failed to begin transaction");
        }
        
        try {
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
                
                // Generate UUIDv7 for message ID to ensure proper time-based ordering
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
                // Store empty string for missing trace_id, will convert to NULL in array builder
                trace_ids.push_back(item.trace_id.value_or(""));
                encrypted_flags.push_back(is_encrypted);
                results.push_back(result);
            }
            
            // Use libpq's array parameter binding for UNNEST - preserves order!
            // Build PostgreSQL array literals as single parameters
            auto build_pg_array = [](const std::vector<std::string>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    result += "\"";
                    // Escape for PostgreSQL array: backslash and quote
                    for (char c : vec[i]) {
                        if (c == '\\' || c == '"') result += '\\';
                        result += c;
                    }
                    result += "\"";
                }
                result += "}";
                return result;
            };
            
            // Build array with NULL handling for trace_ids
            auto build_pg_array_with_nulls = [](const std::vector<std::string>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    if (vec[i].empty()) {
                        result += "NULL";  // Actual NULL in array, not quoted
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
            
            // Build arrays
            std::string ids_array = build_pg_array(message_ids);
            std::string txn_array = build_pg_array(transaction_ids);
            std::string part_array = build_pg_array(partition_ids);
            std::string payload_array = build_pg_array(payloads);
            std::string trace_array = build_pg_array_with_nulls(trace_ids);
            std::string encrypted_array = build_bool_array(encrypted_flags);
            
            // UNNEST preserves array order - critical for message ordering!
            std::string sql = R"(
                INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id, is_encrypted)
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
            
            // Batch INSERT via UNNEST (preserves order)
            
            auto insert_result = QueryResult(conn->exec_params(sql, unnest_params));
            if (!insert_result.is_success()) {
                throw std::runtime_error("Batch insert failed: " + insert_result.error_message());
            }
            
            // Update results with actual IDs
            for (int i = 0; i < insert_result.num_rows() && i < static_cast<int>(results.size()); ++i) {
                results[i].status = "queued";
                results[i].message_id = insert_result.get_value(i, "id");
                std::string trace_val = insert_result.get_value(i, "trace_id");
                if (!trace_val.empty()) {
                    results[i].trace_id = trace_val;
                }
            }
            
            // Commit transaction
            if (!conn->commit_transaction()) {
                throw std::runtime_error("Failed to commit transaction");
            }
            
        } catch (const std::exception& e) {
            conn->rollback_transaction();
            throw;
        }
        
    } catch (const std::exception& e) {
        // Mark all as failed
        for (auto& result : results) {
            result.status = "failed";
            result.error = e.what();
        }
        spdlog::error("Batch push failed: {}", e.what());
    }
    
    return results;
}

// Overload that accepts an existing connection (optimized for reuse)
std::string QueueManager::acquire_partition_lease(DatabaseConnection* conn,
                                                const std::string& queue_name,
                                                const std::string& partition_name,
                                                const std::string& consumer_group,
                                                int lease_time_seconds,
                                                const PopOptions& options) {
    try {
        
        // Reclaim expired leases FIRST - this makes messages with expired leases available again
        std::string reclaim_sql = R"(
            UPDATE queen.partition_consumers
            SET lease_expires_at = NULL,
                lease_acquired_at = NULL,
                message_batch = NULL,
                batch_size = 0,
                acked_count = 0,
                worker_id = NULL
            WHERE lease_expires_at IS NOT NULL
              AND lease_expires_at < NOW()
        )";
        
        auto reclaim_result = QueryResult(conn->exec(reclaim_sql));
        if (reclaim_result.is_success()) {
            spdlog::debug("Reclaimed expired leases");
        }
        
        std::string lease_id = generate_uuid();
        
        // Check if this is a new consumer group with subscription preferences
        std::string initial_cursor_id = "00000000-0000-0000-0000-000000000000";
        std::string initial_cursor_timestamp_sql = "NULL";
        
        if (consumer_group != "__QUEUE_MODE__" && 
            (options.subscription_mode.has_value() || options.subscription_from.has_value())) {
            
            std::string sub_mode = options.subscription_mode.value_or("");
            std::string sub_from = options.subscription_from.value_or("");
            
            // For 'new', 'new-only', or 'now' - start from latest message
            if (sub_mode == "new" || sub_mode == "new-only" || sub_from == "now") {
                // Get latest message in this partition
                std::string latest_sql = R"(
                    SELECT m.id, m.created_at
                    FROM queen.messages m
                    JOIN queen.partitions p ON m.partition_id = p.id
                    JOIN queen.queues q ON p.queue_id = q.id
                    WHERE q.name = $1 AND p.name = $2
                    ORDER BY m.created_at DESC, m.id DESC
                    LIMIT 1
                )";
                
                auto latest_result = QueryResult(conn->exec_params(latest_sql, {queue_name, partition_name}));
                if (latest_result.is_success() && latest_result.num_rows() > 0) {
                    initial_cursor_id = latest_result.get_value(0, "id");
                    initial_cursor_timestamp_sql = "'" + latest_result.get_value(0, "created_at") + "'";
                    spdlog::debug("Subscription mode '{}' - starting from latest message: {}", sub_mode, initial_cursor_id);
                }
            }
            // else: 'all' mode or no preference - start from beginning
        }
        
        // First, check if this consumer group already exists
        std::string check_sql = R"(
            SELECT pc.id FROM queen.partition_consumers pc
            JOIN queen.partitions p ON pc.partition_id = p.id
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2 AND pc.consumer_group = $3
        )";
        
        auto check_result = QueryResult(conn->exec_params(check_sql, {queue_name, partition_name, consumer_group}));
        bool consumer_exists = check_result.is_success() && check_result.num_rows() > 0;
        
        spdlog::debug("Consumer group '{}' exists: {}, has subscription options: {}", 
                     consumer_group, consumer_exists, 
                     (options.subscription_mode.has_value() || options.subscription_from.has_value()));
        
        std::string sql;
        if (!consumer_exists && consumer_group != "__QUEUE_MODE__" && 
            (options.subscription_mode.has_value() || options.subscription_from.has_value())) {
            // New consumer group with subscription preferences - set initial cursor
            spdlog::debug("Creating new consumer group '{}' with cursor at: {}", consumer_group, initial_cursor_id);
            
            sql = R"(
                INSERT INTO queen.partition_consumers (
                    partition_id, consumer_group, lease_expires_at, lease_acquired_at, worker_id,
                    last_consumed_id, last_consumed_created_at
                )
                SELECT p.id, $1, NOW() + INTERVAL '1 second' * $2, NOW(), $3, 
                       $6::uuid, )" + initial_cursor_timestamp_sql + R"(
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $4 AND p.name = $5
                ON CONFLICT (partition_id, consumer_group) DO NOTHING
                RETURNING worker_id
            )";
        } else {
            // Existing consumer or no subscription preference - just update lease
            sql = R"(
                INSERT INTO queen.partition_consumers (
                    partition_id, consumer_group, lease_expires_at, lease_acquired_at, worker_id
                )
                SELECT p.id, $1, NOW() + INTERVAL '1 second' * $2, NOW(), $3
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $4 AND p.name = $5
                ON CONFLICT (partition_id, consumer_group) DO UPDATE SET
                    lease_expires_at = NOW() + INTERVAL '1 second' * $2,
                    lease_acquired_at = NOW(),
                    worker_id = $3
                WHERE partition_consumers.lease_expires_at IS NULL 
                   OR partition_consumers.lease_expires_at <= NOW()
                RETURNING worker_id
            )";
        }
        
        std::vector<std::string> params;
        if (!consumer_exists && consumer_group != "__QUEUE_MODE__" && 
            (options.subscription_mode.has_value() || options.subscription_from.has_value())) {
            // Include cursor params for new consumer with subscription
            params = {
                consumer_group,
                std::to_string(lease_time_seconds),
                lease_id,
                queue_name,
                partition_name,
                initial_cursor_id
            };
        } else {
            // No cursor params needed
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
        
        auto result = QueryResult(conn->exec_params(sql, params));
        
        spdlog::debug("Lease acquisition returned {} rows, success={}", 
                     result.num_rows(), result.is_success());
        
        if (result.is_success() && result.num_rows() > 0) {
            spdlog::debug("Lease acquired successfully: {}", lease_id);
            return lease_id;
        } else {
            spdlog::debug("Lease acquisition failed - partition may be locked by another consumer");
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to acquire partition lease: {}", e.what());
    }
    
    return "";
}

// Original overload that creates its own connection
std::string QueueManager::acquire_partition_lease(const std::string& queue_name,
                                                const std::string& partition_name,
                                                const std::string& consumer_group,
                                                int lease_time_seconds,
                                                const PopOptions& options) {
    try {
        ScopedConnection conn(db_pool_.get());
        return acquire_partition_lease(conn.operator->(), queue_name, partition_name, 
                                      consumer_group, lease_time_seconds, options);
    } catch (const std::exception& e) {
        spdlog::error("Failed to acquire partition lease (wrapper): {}", e.what());
        return "";
    }
}

PopResult QueueManager::pop_from_queue_partition(const std::string& queue_name,
                                               const std::string& partition_name,
                                               const std::string& consumer_group,
                                               const PopOptions& options) {
    PopResult result;
    
    try {
        // OPTIMIZATION: Use single connection for all operations to reduce pool contention
        ScopedConnection conn(db_pool_.get());
        
        // Check if partition is accessible considering window_buffer
        std::string window_check_sql = R"(
            SELECT p.id, q.window_buffer
            FROM queen.partitions p
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
              AND (q.window_buffer = 0 OR NOT EXISTS (
                SELECT 1 FROM queen.messages m
                WHERE m.partition_id = p.id
                  AND m.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
              ))
        )";
        
        auto window_result = QueryResult(conn->exec_params(window_check_sql, {queue_name, partition_name}));
        
        if (!window_result.is_success() || window_result.num_rows() == 0) {
            std::string window_buffer_str = window_result.num_rows() > 0 ? window_result.get_value(0, "window_buffer") : "unknown";
            spdlog::debug("Partition {} blocked by window buffer ({}s)", partition_name, window_buffer_str);
            return result; // Partition not accessible due to window buffer
        }
        
        // Get queue configuration for delayed_processing, max_wait_time_seconds, and lease_time
        // CRITICAL: Must read lease_time BEFORE acquiring lease!
        std::string config_sql = R"(
            SELECT q.delayed_processing, q.max_wait_time_seconds, q.lease_time
            FROM queen.queues q
            WHERE q.name = $1
        )";
        
        auto config_result = QueryResult(conn->exec_params(config_sql, {queue_name}));
        int delayed_processing = 0;
        int max_wait_time = 0;
        int lease_time = 300; // Default to 300 seconds if not specified
        
        if (config_result.is_success() && config_result.num_rows() > 0) {
            std::string delay_str = config_result.get_value(0, "delayed_processing");
            std::string wait_str = config_result.get_value(0, "max_wait_time_seconds");
            std::string lease_str = config_result.get_value(0, "lease_time");
            delayed_processing = delay_str.empty() ? 0 : std::stoi(delay_str);
            max_wait_time = wait_str.empty() ? 0 : std::stoi(wait_str);
            lease_time = lease_str.empty() ? 300 : std::stoi(lease_str);
        }
        
        // Acquire lease for this partition (reusing the same connection)
        std::string lease_id = acquire_partition_lease(conn.operator->(), queue_name, partition_name, 
                                                     consumer_group, lease_time, options);
        if (lease_id.empty()) {
            return result; // No lease acquired, return empty result
        }
        
        result.lease_id = lease_id;
        
        // Build WHERE clause with delayed processing and eviction filters
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
        
        // Add delayed processing filter if configured
        if (delayed_processing > 0) {
            where_clause += " AND m.created_at <= NOW() - INTERVAL '1 second' * $" + std::to_string(params.size() + 1);
            params.push_back(std::to_string(delayed_processing));
            spdlog::debug("Delayed processing filter active: messages must be at least {} seconds old", delayed_processing);
        }
        
        // Add max wait time (eviction) filter if configured
        if (max_wait_time > 0) {
            where_clause += " AND m.created_at > NOW() - INTERVAL '1 second' * $" + std::to_string(params.size() + 1);
            params.push_back(std::to_string(max_wait_time));
        }
        
        // Add batch limit
        params.push_back(std::to_string(options.batch));
        std::string limit_param = "$" + std::to_string(params.size());
        
        // Get messages from this partition with queue priority and encryption flag
        // CRITICAL: Use FOR UPDATE OF m SKIP LOCKED for row-level locking and concurrency
        std::string sql = R"(
            SELECT m.id, m.transaction_id, m.payload, m.trace_id, m.created_at, m.is_encrypted,
                   q.name as queue_name, p.name as partition_name, q.priority as queue_priority
            FROM queen.messages m
            JOIN queen.partitions p ON p.id = m.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            JOIN queen.partition_consumers pc ON pc.partition_id = p.id
            )" + where_clause + R"(
            ORDER BY m.created_at ASC, m.id ASC
            LIMIT )" + limit_param + R"(
            FOR UPDATE OF m SKIP LOCKED
        )";
        
        spdlog::debug("Pop query params: queue={}, partition={}, consumer_group={}, lease_id={}, batch={}", 
                     queue_name, partition_name, consumer_group, lease_id, options.batch);
        
        auto query_result = QueryResult(conn->exec_params(sql, params));
        
        spdlog::debug("Pop query returned {} rows, success={}", 
                     query_result.num_rows(), query_result.is_success());
        
        if (!query_result.is_success()) {
            spdlog::error("Pop query failed: {}", query_result.error_message());
        }
        
        if (query_result.is_success() && query_result.num_rows() > 0) {
            // Update batch_size to track how many messages were popped
            int num_messages = query_result.num_rows();
            std::string update_batch_size = R"(
                UPDATE queen.partition_consumers
                SET batch_size = $1,
                    acked_count = 0
                WHERE partition_id = (
                    SELECT p.id FROM queen.partitions p
                    JOIN queen.queues q ON p.queue_id = q.id
                    WHERE q.name = $2 AND p.name = $3
                )
                AND consumer_group = $4
                AND worker_id = $5
            )";
            
            std::vector<std::string> batch_params = {
                std::to_string(num_messages),
                queue_name,
                partition_name,
                consumer_group,
                lease_id
            };
            
            auto batch_update = QueryResult(conn->exec_params(update_batch_size, batch_params));
            if (!batch_update.is_success()) {
                spdlog::warn("Failed to update batch_size: {}", batch_update.error_message());
            }
        }
        
        if (query_result.is_success()) {
            // Check if no messages were found - release lease to allow retry
            if (query_result.num_rows() == 0) {
                spdlog::debug("No messages found - releasing lease for partition: {}", partition_name);
                
                std::string release_sql = R"(
                    UPDATE queen.partition_consumers
                    SET lease_expires_at = NULL,
                        lease_acquired_at = NULL,
                        worker_id = NULL
                    WHERE partition_id = (
                        SELECT p.id FROM queen.partitions p
                        JOIN queen.queues q ON p.queue_id = q.id
                        WHERE q.name = $1 AND p.name = $2
                    )
                    AND consumer_group = $3
                )";
                
                auto release_result = QueryResult(conn->exec_params(release_sql, {queue_name, partition_name, consumer_group}));
                if (!release_result.is_success()) {
                    spdlog::warn("Failed to release lease: {}", release_result.error_message());
                }
                
                return result; // Return empty result
            }
            
            for (int i = 0; i < query_result.num_rows(); ++i) {
                Message msg;
                msg.id = query_result.get_value(i, "id");
                msg.transaction_id = query_result.get_value(i, "transaction_id");
                msg.queue_name = query_result.get_value(i, "queue_name");
                msg.partition_name = query_result.get_value(i, "partition_name");
                std::string trace_val = query_result.get_value(i, "trace_id");
                msg.trace_id = trace_val;
                msg.status = "processing";
                
                // Get priority from queue
                std::string priority_str = query_result.get_value(i, "queue_priority");
                msg.priority = priority_str.empty() ? 0 : std::stoi(priority_str);
                
                // Retry count is always 0 for messages being popped (not retried yet)
                msg.retry_count = 0;
                
                // Parse created_at timestamp with milliseconds
                std::string created_at_str = query_result.get_value(i, "created_at");
                if (!created_at_str.empty()) {
                    // Parse PostgreSQL timestamp format: "2025-10-18 09:10:14.710+02"
                    std::tm tm = {};
                    int milliseconds = 0;
                    
                    // First parse the date and time part
                    std::istringstream ss(created_at_str);
                    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                    
                    // Check if there's a fractional seconds part
                    if (ss.peek() == '.') {
                        ss.get(); // consume the '.'
                        std::string ms_str;
                        while (ss.peek() != EOF && std::isdigit(ss.peek())) {
                            ms_str += ss.get();
                        }
                        if (!ms_str.empty()) {
                            // Pad or truncate to 3 digits
                            if (ms_str.length() < 3) {
                                ms_str.append(3 - ms_str.length(), '0');
                            } else if (ms_str.length() > 3) {
                                ms_str = ms_str.substr(0, 3);
                            }
                            milliseconds = std::stoi(ms_str);
                        }
                    }
                    
                    auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                    msg.created_at = time_point + std::chrono::milliseconds(milliseconds);
                }
                
                // Parse payload JSON - decrypt if encrypted
                try {
                    std::string payload_str = query_result.get_value(i, "payload");
                    std::string is_encrypted_str = query_result.get_value(i, "is_encrypted");
                    bool is_encrypted = (is_encrypted_str == "t" || is_encrypted_str == "true");
                    
                    if (!payload_str.empty()) {
                        if (is_encrypted) {
                            // Decrypt the payload
                            auto encrypted_json = nlohmann::json::parse(payload_str);
                            
                            EncryptionService::EncryptedData encrypted_data;
                            encrypted_data.encrypted = encrypted_json["encrypted"];
                            encrypted_data.iv = encrypted_json["iv"];
                            encrypted_data.auth_tag = encrypted_json["authTag"];
                            
                            auto enc_service = get_encryption_service();
                            if (enc_service && enc_service->is_enabled()) {
                                auto decrypted = enc_service->decrypt_payload(encrypted_data);
                                if (decrypted.has_value()) {
                                    msg.payload = nlohmann::json::parse(*decrypted);
                                } else {
                                    spdlog::error("Failed to decrypt message payload");
                                    msg.payload = payload_str;  // Return encrypted data as fallback
                                }
                            } else {
                                spdlog::warn("Message is encrypted but encryption service not available");
                                msg.payload = payload_str;
                            }
                        } else {
                            msg.payload = nlohmann::json::parse(payload_str);
                        }
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to parse message payload as JSON: {}", e.what());
                    msg.payload = query_result.get_value(i, "payload");
                }
                
                result.messages.push_back(std::move(msg));
            }
            
            // Auto-ack: Immediately update cursor and release lease if configured
            if (options.auto_ack && !result.messages.empty()) {
                spdlog::debug("Auto-ack enabled, immediately updating cursor for {} messages", result.messages.size());
                
                // Get the last message to update cursor
                const auto& last_msg = result.messages.back();
                
                std::string auto_ack_sql = R"(
                    UPDATE queen.partition_consumers
                    SET last_consumed_id = $1,
                        last_consumed_created_at = (SELECT created_at FROM queen.messages WHERE id = $1),
                        last_consumed_at = NOW(),
                        total_messages_consumed = total_messages_consumed + $2,
                        lease_expires_at = NULL,
                        lease_acquired_at = NULL,
                        batch_size = 0,
                        acked_count = 0
                    WHERE partition_id = (
                        SELECT p.id FROM queen.partitions p
                        JOIN queen.queues q ON p.queue_id = q.id
                        WHERE q.name = $3 AND p.name = $4
                    )
                    AND consumer_group = $5
                )";
                
                auto auto_ack_result = QueryResult(conn->exec_params(auto_ack_sql, {
                    last_msg.id,
                    std::to_string(result.messages.size()),
                    queue_name,
                    partition_name,
                    consumer_group
                }));
                
                if (!auto_ack_result.is_success()) {
                    spdlog::warn("Failed to auto-ack messages: {}", auto_ack_result.error_message());
                } else {
                    spdlog::debug("Auto-acked {} messages, cursor updated to {}", result.messages.size(), last_msg.id);
                    
                    // Insert into messages_consumed for analytics
                    std::string consumed_sql = R"(
                        INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                        SELECT p.id, $2, $3, 0, NOW()
                        FROM queen.partitions p
                        JOIN queen.queues q ON p.queue_id = q.id
                        WHERE q.name = $4 AND p.name = $5
                    )";
                    
                    conn->exec_params(consumed_sql, {
                        consumer_group,
                        std::to_string(result.messages.size()),
                        queue_name,
                        partition_name
                    });
                }
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pop messages: {}", e.what());
    }
    
    return result;
}

PopResult QueueManager::pop_messages(const std::string& queue_name,
                                   const std::optional<std::string>& partition_name,
                                   const std::string& consumer_group,
                                   const PopOptions& options) {
    
    // If partition is specified, pop from that specific partition
    if (partition_name.has_value()) {
        if (options.wait) {
            return wait_for_messages([&]() {
                return pop_from_queue_partition(queue_name, *partition_name, consumer_group, options);
            }, options.timeout);
        } else {
            return pop_from_queue_partition(queue_name, *partition_name, consumer_group, options);
        }
    }
    
    // No partition specified - try to pop from any available partition in the queue
    // This is needed for the FIFO ordering test which pushes to multiple partitions
    // and expects to pop from any of them
    
    if (options.wait) {
        return wait_for_messages([&]() {
            return pop_from_any_partition(queue_name, consumer_group, options);
        }, options.timeout);
    } else {
        return pop_from_any_partition(queue_name, consumer_group, options);
    }
}

PopResult QueueManager::pop_from_any_partition(const std::string& queue_name,
                                             const std::string& consumer_group,
                                             const PopOptions& options) {
    PopResult result;
    
    try {
        // Get all partitions for this queue that have messages and no active lease
        ScopedConnection conn(db_pool_.get());
        
        // Get queue configuration for window_buffer
        std::string config_sql = "SELECT window_buffer FROM queen.queues WHERE name = $1";
        auto config_result = QueryResult(conn->exec_params(config_sql, {queue_name}));
        int window_buffer = 0;
        
        if (config_result.is_success() && config_result.num_rows() > 0) {
            std::string buffer_str = config_result.get_value(0, "window_buffer");
            window_buffer = buffer_str.empty() ? 0 : std::stoi(buffer_str);
        }
        
        // Build query with window buffer filter
        std::string sql;
        std::vector<std::string> params = {queue_name, consumer_group};
        
        if (window_buffer > 0) {
            // Exclude partitions with recent message activity (window buffer)
            sql = R"(
                SELECT p.id, p.name, COUNT(m.id) as message_count
                FROM queen.partitions p
                JOIN queen.queues q ON p.queue_id = q.id
                LEFT JOIN queen.messages m ON m.partition_id = p.id
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
                    AND pc.consumer_group = $2
                WHERE q.name = $1
                  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
                  AND m.id IS NOT NULL
                  -- Window buffer: exclude partitions with recent activity
                  AND NOT EXISTS (
                      SELECT 1 FROM queen.messages m2
                      WHERE m2.partition_id = p.id
                        AND m2.created_at > NOW() - INTERVAL '1 second' * $3
                  )
                  -- Only include messages that haven't been consumed yet
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
            // No window buffer - standard query
            sql = R"(
                SELECT p.id, p.name, COUNT(m.id) as message_count
                FROM queen.partitions p
                JOIN queen.queues q ON p.queue_id = q.id
                LEFT JOIN queen.messages m ON m.partition_id = p.id
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
                    AND pc.consumer_group = $2
                WHERE q.name = $1
                  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
                  AND m.id IS NOT NULL
                  -- Only include messages that haven't been consumed yet
                  AND (pc.last_consumed_created_at IS NULL
                       OR m.created_at > pc.last_consumed_created_at
                       OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
                GROUP BY p.id, p.name
                HAVING COUNT(m.id) > 0
                ORDER BY COUNT(m.id) DESC
                LIMIT 10
            )";
        }
        
        auto partitions_result = QueryResult(conn->exec_params(sql, params));
        
        if (!partitions_result.is_success() || partitions_result.num_rows() == 0) {
            spdlog::debug("No available partitions found for queue: {}", queue_name);
            return result; // Empty result
        }
        
        // Try each partition until we successfully acquire a lease and get messages
        for (int i = 0; i < partitions_result.num_rows(); ++i) {
            std::string partition_name = partitions_result.get_value(i, "name");
            int message_count = std::stoi(partitions_result.get_value(i, "message_count"));
            
            spdlog::debug("Trying partition '{}' with {} messages", partition_name, message_count);
            
            result = pop_from_queue_partition(queue_name, partition_name, consumer_group, options);
            
            if (!result.messages.empty()) {
                return result; // Successfully got messages
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pop from any partition: {}", e.what());
    }
    
    return result; // Empty result
}

PopResult QueueManager::pop_with_namespace_task(const std::optional<std::string>& namespace_name,
                                              const std::optional<std::string>& task_name,
                                              const std::string& consumer_group,
                                              const PopOptions& options) {
    PopResult result;
    
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Get queue configuration and find partitions matching namespace/task with window buffer check
        std::string sql = R"(
            SELECT 
                q.name as queue_name,
                p.name as partition_name,
                q.window_buffer
            FROM queen.queues q
            JOIN queen.partitions p ON p.queue_id = q.id
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
        
        // Add window buffer check
        sql += R"(
              AND (q.window_buffer = 0 OR NOT EXISTS (
                SELECT 1 FROM queen.messages m
                WHERE m.partition_id = p.id
                  AND m.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
              ))
            LIMIT 10
        )";
        
        auto queues_result = QueryResult(conn->exec_params(sql, params));
        
        if (!queues_result.is_success() || queues_result.num_rows() == 0) {
            spdlog::debug("No queues found matching namespace/task filters");
            return result;
        }
        
        // Try each matching queue/partition
        for (int i = 0; i < queues_result.num_rows(); ++i) {
            std::string queue_name = queues_result.get_value(i, "queue_name");
            std::string partition_name = queues_result.get_value(i, "partition_name");
            
            result = pop_from_queue_partition(queue_name, partition_name, consumer_group, options);
            
            if (!result.messages.empty()) {
                return result; // Got messages
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pop with namespace/task: {}", e.what());
    }
    
    return result;
}

PopResult QueueManager::wait_for_messages(const std::function<PopResult()>& pop_function,
                                        int timeout_ms) {
    auto start_time = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::milliseconds(timeout_ms);
    
    int poll_interval = config_.poll_interval;
    
    while (true) {
        PopResult result = pop_function();
        
        if (!result.messages.empty()) {
            return result;
        }
        
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= timeout_duration) {
            break;
        }
        
        // Sleep for poll interval
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval));
        
        // Exponential backoff
        poll_interval = std::min(poll_interval * 2, config_.max_poll_interval);
    }
    
    return PopResult{}; // Empty result
}

QueueManager::AckResult QueueManager::acknowledge_message(const std::string& transaction_id,
                                     const std::string& status,
                                     const std::optional<std::string>& error,
                                     const std::string& consumer_group,
                                     const std::optional<std::string>& lease_id) {
    QueueManager::AckResult result;
    result.transaction_id = transaction_id;
    result.status = "not_found";
    result.success = false;
    
    try {
        ScopedConnection conn(db_pool_.get());
        
        // If lease ID provided, validate it first
        if (lease_id.has_value()) {
            spdlog::debug("Validating lease {} for transaction {}", *lease_id, transaction_id);
            std::string validate_sql = R"(
                SELECT 1 FROM queen.partition_consumers pc
                JOIN queen.messages m ON m.partition_id = pc.partition_id
                WHERE m.transaction_id = $1 
                  AND pc.worker_id = $2
                  AND pc.lease_expires_at > NOW()
            )";
            
            auto valid = QueryResult(conn->exec_params(validate_sql, {transaction_id, *lease_id}));
            
            if (!valid.is_success() || valid.num_rows() == 0) {
                spdlog::warn("Invalid lease {} for transaction {} - validation failed", *lease_id, transaction_id);
                result.status = "invalid_lease";
                return result;
            }
            spdlog::debug("Lease {} validated successfully", *lease_id);
        }
        
        if (status == "completed") {
            // Update cursor and release lease if all messages in batch are ACKed
            std::string sql = R"(
                UPDATE queen.partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    acked_count = acked_count + 1,
                    -- Release lease when all messages in batch are ACKed
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
                    END
                FROM queen.messages m
                JOIN queen.partitions p ON p.id = m.partition_id
                WHERE partition_consumers.partition_id = p.id
                  AND partition_consumers.consumer_group = $1
                  AND m.transaction_id = $2
                RETURNING (partition_consumers.lease_expires_at IS NULL) as lease_released
            )";
            
            std::vector<std::string> params = {consumer_group, transaction_id};
            auto query_result = QueryResult(conn->exec_params(sql, params));
            
            if (query_result.is_success() && query_result.num_rows() > 0) {
                bool lease_released = query_result.get_value(0, "lease_released") == "t";
                if (lease_released) {
                    spdlog::debug("Lease released after ACK for transaction: {}", transaction_id);
                    
                    // CRITICAL: Insert into messages_consumed for analytics when batch completes
                    // Get partition_id for this transaction
                    std::string partition_sql = R"(
                        SELECT p.id as partition_id
                        FROM queen.messages m
                        JOIN queen.partitions p ON p.id = m.partition_id
                        WHERE m.transaction_id = $1
                    )";
                    auto partition_result = QueryResult(conn->exec_params(partition_sql, {transaction_id}));
                    if (partition_result.is_success() && partition_result.num_rows() > 0) {
                        std::string partition_id = partition_result.get_value(0, "partition_id");
                        std::string insert_consumed_sql = R"(
                            INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                            VALUES ($1, $2, 1, 0, NOW())
                        )";
                        conn->exec_params(insert_consumed_sql, {partition_id, consumer_group});
                    }
                }
                result.status = "completed";
                result.success = true;
            }
            
            return result;
            
        } else if (status == "failed") {
            // Insert into Dead Letter Queue for this consumer group
            std::string dlq_sql = R"(
                INSERT INTO queen.dead_letter_queue (
                    message_id, partition_id, consumer_group, error_message, 
                    retry_count, original_created_at
                )
                SELECT m.id, m.partition_id, $1::varchar, $2::text, 0, m.created_at
                FROM queen.messages m
                WHERE m.transaction_id = $3
                  AND NOT EXISTS (
                      SELECT 1 FROM queen.dead_letter_queue dlq
                      WHERE dlq.message_id = m.id AND dlq.consumer_group = $1::varchar
                  )
            )";
            
            std::vector<std::string> dlq_params = {
                consumer_group,
                error.value_or("Message processing failed"),
                transaction_id
            };
            
            auto dlq_result = QueryResult(conn->exec_params(dlq_sql, dlq_params));
            
            if (!dlq_result.is_success()) {
                spdlog::error("Failed to insert into DLQ: {}", dlq_result.error_message());
                return result;
            }
            
            // Still advance cursor for failed messages (they are "consumed")
            std::string cursor_sql = R"(
                UPDATE queen.partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    acked_count = acked_count + 1,
                    -- Release lease when all messages in batch are ACKed
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
                    END
                FROM queen.messages m
                JOIN queen.partitions p ON p.id = m.partition_id
                WHERE partition_consumers.partition_id = p.id
                  AND partition_consumers.consumer_group = $1
                  AND m.transaction_id = $2
            )";
            
            auto cursor_result = QueryResult(conn->exec_params(cursor_sql, {consumer_group, transaction_id}));
            
            spdlog::warn("Message failed and moved to DLQ: {} - {}", transaction_id, error.value_or("No error message"));
            result.status = "failed_dlq";
            result.success = cursor_result.is_success();
            return result;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to acknowledge message: {}", e.what());
    }
    
    return result;
}

std::vector<QueueManager::AckResult> QueueManager::acknowledge_messages(const std::vector<AckItem>& acks,
                                                   const std::string& consumer_group) {
    std::vector<QueueManager::AckResult> results;
    results.reserve(acks.size());
    
    if (acks.empty()) {
        return results;
    }
    
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Start transaction for atomic batch ACK
        if (!conn->begin_transaction()) {
            spdlog::error("Failed to begin transaction - connection may be exhausted");
            throw std::runtime_error("Failed to begin transaction");
        }
        
        try {
            const std::string actual_consumer_group = consumer_group.empty() ? "__QUEUE_MODE__" : consumer_group;
            
            // Group by status
            std::vector<AckItem> completed;
            std::vector<AckItem> failed;
    
    for (const auto& ack : acks) {
                if (ack.status == "completed") {
                    completed.push_back(ack);
                } else if (ack.status == "failed") {
                    failed.push_back(ack);
                }
            }
            
            int total_messages = acks.size();
            int failed_count = failed.size();
            int success_count = completed.size();
            
            // Build array of all transaction IDs
            std::vector<std::string> all_txn_ids;
            for (const auto& ack : acks) {
                all_txn_ids.push_back(ack.transaction_id);
            }
            
            // Get batch size info to determine if this is a total batch failure
            std::string batch_size_sql = R"(
                SELECT pc.batch_size, pc.partition_id
                FROM queen.messages m
                JOIN queen.partition_consumers pc ON pc.partition_id = m.partition_id 
                  AND pc.consumer_group = $2
                WHERE m.transaction_id = ANY($1::varchar[])
                LIMIT 1
            )";
            
            auto build_txn_array = [](const std::vector<std::string>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    result += "\"" + vec[i] + "\"";
                }
                result += "}";
                return result;
            };
            
            std::string txn_array = build_txn_array(all_txn_ids);
            auto batch_info_result = QueryResult(conn->exec_params(batch_size_sql, {txn_array, actual_consumer_group}));
            
            if (!batch_info_result.is_success() || batch_info_result.num_rows() == 0) {
                throw std::runtime_error("Cannot find leased batch size for acknowledgment");
            }
            
            int leased_batch_size = std::stoi(batch_info_result.get_value(0, "batch_size"));
            std::string partition_id = batch_info_result.get_value(0, "partition_id");
            
            // Total batch failure: all failed AND complete batch
            bool is_total_batch_failure = (failed_count == total_messages) && (total_messages == leased_batch_size);
            
            if (is_total_batch_failure) {
                // Get retry info
                std::string retry_info_sql = R"(
                    SELECT 
                        q.retry_limit,
                        pc.batch_retry_count,
                        pc.partition_id,
                        p.name as partition_name
                    FROM queen.messages m
                    JOIN queen.partitions p ON p.id = m.partition_id
                    JOIN queen.queues q ON q.id = p.queue_id
                    JOIN queen.partition_consumers pc ON pc.partition_id = m.partition_id 
                      AND pc.consumer_group = $2
                    WHERE m.transaction_id = ANY($1::varchar[])
                    LIMIT 1
                )";
                
                auto retry_info = QueryResult(conn->exec_params(retry_info_sql, {txn_array, actual_consumer_group}));
                
                if (!retry_info.is_success() || retry_info.num_rows() == 0) {
                    throw std::runtime_error("Cannot find queue retry limit");
                }
                
                int retry_limit = std::stoi(retry_info.get_value(0, "retry_limit"));
                std::string batch_retry_str = retry_info.get_value(0, "batch_retry_count");
                int current_retry_count = batch_retry_str.empty() ? 0 : std::stoi(batch_retry_str);
                
                if (current_retry_count >= retry_limit) {
                    // Retry limit exceeded - move to DLQ and advance cursor
                    spdlog::warn("Batch retry limit exceeded ({}/{}), moving to DLQ", current_retry_count, retry_limit);
                    
                    // Move all to DLQ
                    std::vector<std::string> error_msgs;
                    for (const auto& ack : failed) {
                        error_msgs.push_back(ack.error.value_or("Batch retry limit exceeded"));
                    }
                    
                    std::string error_array = build_txn_array(error_msgs);
                    std::string dlq_sql = R"(
                        INSERT INTO queen.dead_letter_queue (message_id, partition_id, consumer_group, error_message, original_created_at)
                        SELECT 
                            m.id,
                            m.partition_id,
                            $2,
                            e.error_message,
                            m.created_at
                        FROM queen.messages m
                        CROSS JOIN LATERAL UNNEST($1::varchar[], $3::text[]) AS e(txn_id, error_message)
                        WHERE m.transaction_id = e.txn_id
                        ON CONFLICT DO NOTHING
                    )";
                    
                    conn->exec_params(dlq_sql, {txn_array, actual_consumer_group, error_array});
                    
                    // Get last message for cursor
                    std::string last_msg_sql = R"(
                        SELECT id, created_at
                        FROM queen.messages m
                        WHERE m.transaction_id = ANY($1::varchar[])
                        ORDER BY m.created_at DESC, m.id DESC
                        LIMIT 1
                    )";
                    
                    auto last_msg = QueryResult(conn->exec_params(last_msg_sql, {txn_array}));
                    std::string last_id = last_msg.get_value(0, "id");
                    std::string last_created = last_msg.get_value(0, "created_at");
                    
                    // Advance cursor and reset retry count
                    std::string cursor_sql = R"(
                        UPDATE queen.partition_consumers
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
                            acked_count = 0
                        WHERE partition_id = $4 AND consumer_group = $5
                    )";
                    
                    conn->exec_params(cursor_sql, {last_created, last_id, std::to_string(total_messages), partition_id, actual_consumer_group});
                    
                    // CRITICAL: Insert into messages_consumed for analytics/throughput tracking
                    std::string insert_consumed_sql = R"(
                        INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                        VALUES ($1, $2, 0, $3, NOW())
                    )";
                    conn->exec_params(insert_consumed_sql, {partition_id, actual_consumer_group, std::to_string(failed_count)});
                    
                    for (const auto& ack : failed) {
                        QueueManager::AckResult result;
                        result.transaction_id = ack.transaction_id;
                        result.status = "failed_dlq";
                        result.success = true;
                        results.push_back(result);
                    }
                    
                } else {
                    // Batch retry - increment counter, release lease, DON'T advance cursor
                    spdlog::info("Total batch failure - retry {}/{}", current_retry_count + 1, retry_limit);
                    
                    std::string retry_sql = R"(
                        UPDATE queen.partition_consumers pc
                        SET lease_expires_at = NULL,
                            lease_acquired_at = NULL,
                            message_batch = NULL,
                            batch_size = 0,
                            acked_count = 0,
                            batch_retry_count = COALESCE(batch_retry_count, 0) + 1
                        WHERE pc.partition_id = $1 AND pc.consumer_group = $2
                    )";
                    
                    conn->exec_params(retry_sql, {partition_id, actual_consumer_group});
                    
                    for (const auto& ack : failed) {
                        QueueManager::AckResult result;
                        result.transaction_id = ack.transaction_id;
                        result.status = "failed_retry";
                        result.success = true;
                        results.push_back(result);
                    }
                }
                
            } else {
                // Partial success - advance cursor, DLQ individual failures
                
                // Get last message info
                std::string batch_info_sql = R"(
                    SELECT 
                        m.id,
                        m.transaction_id,
                        m.created_at,
                        m.partition_id,
                        p.name as partition_name
                    FROM queen.messages m
                    JOIN queen.partitions p ON p.id = m.partition_id
                    WHERE m.transaction_id = ANY($1::varchar[])
                    ORDER BY m.created_at DESC, m.id DESC
                    LIMIT 1
                )";
                
                auto batch_info = QueryResult(conn->exec_params(batch_info_sql, {txn_array}));
                
                if (!batch_info.is_success() || batch_info.num_rows() == 0) {
                    throw std::runtime_error("No messages found for acknowledgment");
                }
                
                std::string last_id = batch_info.get_value(0, "id");
                std::string last_created = batch_info.get_value(0, "created_at");
                std::string partition_id = batch_info.get_value(0, "partition_id");
                
                // Advance cursor atomically
                std::string cursor_sql = R"(
                    UPDATE queen.partition_consumers
                    SET 
                        last_consumed_created_at = $1,
                        last_consumed_id = $2,
                        total_messages_consumed = total_messages_consumed + $3,
                        total_batches_consumed = total_batches_consumed + 1,
                        last_consumed_at = NOW(),
                        batch_retry_count = 0,
                        pending_estimate = GREATEST(0, pending_estimate - $3),
                        last_stats_update = NOW(),
                        acked_count = acked_count + $3,
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
                        END
                    WHERE partition_id = $4 AND consumer_group = $5
                    RETURNING (lease_expires_at IS NULL) as lease_released
                )";
                
                auto cursor_result = QueryResult(conn->exec_params(cursor_sql, {
                    last_created, last_id, std::to_string(total_messages), partition_id, actual_consumer_group
                }));
                
                bool lease_released = cursor_result.is_success() && cursor_result.num_rows() > 0 && 
                                     cursor_result.get_value(0, "lease_released") == "t";
                
                spdlog::debug("Batch ACK: Success={} Failed={} Lease={}", 
                            success_count, failed_count, lease_released ? "Released" : "Active");
                
                // Move failed messages to DLQ
                if (!failed.empty()) {
                    std::vector<std::string> failed_txn_ids;
                    std::vector<std::string> failed_errors;
                    
                    for (const auto& ack : failed) {
                        failed_txn_ids.push_back(ack.transaction_id);
                        failed_errors.push_back(ack.error.value_or("Unknown error"));
                    }
                    
                    std::string failed_array = build_txn_array(failed_txn_ids);
                    std::string error_array = build_txn_array(failed_errors);
                    
                    std::string dlq_sql = R"(
                        INSERT INTO queen.dead_letter_queue (message_id, partition_id, consumer_group, error_message, original_created_at)
                        SELECT 
                            m.id,
                            m.partition_id,
                            $2,
                            e.error_message,
                            m.created_at
                        FROM queen.messages m
                        CROSS JOIN LATERAL UNNEST($1::varchar[], $3::text[]) AS e(txn_id, error_message)
                        WHERE m.transaction_id = e.txn_id
                        ON CONFLICT DO NOTHING
                    )";
                    
                    conn->exec_params(dlq_sql, {failed_array, actual_consumer_group, error_array});
                    
                    spdlog::info("Moved {} failed messages to DLQ", failed_count);
                }
                
                // CRITICAL: Insert into messages_consumed for analytics/throughput tracking
                std::string insert_consumed_sql = R"(
                    INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                    VALUES ($1, $2, $3, $4, NOW())
                )";
                conn->exec_params(insert_consumed_sql, {
                    partition_id, 
                    actual_consumer_group, 
                    std::to_string(success_count), 
                    std::to_string(failed_count)
                });
                
                // Build results
                for (const auto& ack : completed) {
                    QueueManager::AckResult result;
                    result.transaction_id = ack.transaction_id;
                    result.status = "completed";
                    result.success = true;
                    results.push_back(result);
                }
                
                for (const auto& ack : failed) {
                    QueueManager::AckResult result;
                    result.transaction_id = ack.transaction_id;
                    result.status = "failed_dlq";
                    result.success = true;
                    results.push_back(result);
                }
            }
            
            if (!conn->commit_transaction()) {
                spdlog::error("Failed to commit ACK transaction");
                conn->rollback_transaction();
                throw std::runtime_error("Failed to commit transaction");
            }
            
        } catch (const std::exception& e) {
            spdlog::warn("Error in batch ACK transaction, rolling back: {}", e.what());
            try {
                conn->rollback_transaction();
            } catch (const std::exception& rollback_error) {
                spdlog::error("Failed to rollback transaction: {}", rollback_error.what());
                // Connection is corrupted, will be discarded when ScopedConnection destructs
            }
            throw;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Batch ACK failed: {}", e.what());
        // Return failure results
        for (const auto& ack : acks) {
            QueueManager::AckResult result;
            result.transaction_id = ack.transaction_id;
            result.status = "error";
            result.success = false;
            results.push_back(result);
        }
    }
    
    return results;
}

bool QueueManager::extend_message_lease(const std::string& lease_id, int seconds) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        std::string sql = R"(
            UPDATE queen.partition_consumers
            SET lease_expires_at = GREATEST(lease_expires_at, NOW() + INTERVAL '1 second' * $1)
            WHERE worker_id = $2 AND lease_expires_at > NOW()
            RETURNING lease_expires_at
        )";
        
        auto result = QueryResult(conn->exec_params(sql, {std::to_string(seconds), lease_id}));
        
        if (result.is_success() && result.num_rows() > 0) {
            spdlog::debug("Lease {} extended by {} seconds", lease_id, seconds);
            return true;
        }
        
        spdlog::debug("Lease {} not found or expired", lease_id);
        return false;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to extend lease: {}", e.what());
        return false;
    }
}

QueueManager::PoolStats QueueManager::get_pool_stats() const {
    PoolStats stats;
    stats.total = db_pool_->size();
    stats.available = db_pool_->available();
    stats.in_use = stats.total - stats.available;
    return stats;
}

bool QueueManager::health_check() {
    try {
        auto result = QueryResult(db_pool_->query("SELECT 1"));
        return result.is_success();
    } catch (const std::exception& e) {
        spdlog::error("Health check failed: {}", e.what());
        return false;
    }
}

bool QueueManager::initialize_schema() {
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Create schema if it doesn't exist
        auto result1 = QueryResult(conn->exec("CREATE SCHEMA IF NOT EXISTS queen"));
        if (!result1.is_success()) {
            spdlog::error("Failed to create schema: {}", result1.error_message());
            return false;
        }
        
        // Create tables - Queen Message Queue Schema V3
        std::string create_tables_sql = R"(
            CREATE TABLE IF NOT EXISTS queen.queues (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                name VARCHAR(255) UNIQUE NOT NULL,
                namespace VARCHAR(255),
                task VARCHAR(255),
                priority INTEGER DEFAULT 0,
                lease_time INTEGER DEFAULT 300,
                retry_limit INTEGER DEFAULT 3,
                retry_delay INTEGER DEFAULT 1000,
                max_size INTEGER DEFAULT 10000,
                ttl INTEGER DEFAULT 3600,
                dead_letter_queue BOOLEAN DEFAULT FALSE,
                dlq_after_max_retries BOOLEAN DEFAULT FALSE,
                delayed_processing INTEGER DEFAULT 0,
                window_buffer INTEGER DEFAULT 0,
                retention_seconds INTEGER DEFAULT 0,
                completed_retention_seconds INTEGER DEFAULT 0,
                retention_enabled BOOLEAN DEFAULT FALSE,
                encryption_enabled BOOLEAN DEFAULT FALSE,
                max_wait_time_seconds INTEGER DEFAULT 0,
                max_queue_size INTEGER DEFAULT 0,
                created_at TIMESTAMPTZ DEFAULT NOW()
            );
            
            CREATE TABLE IF NOT EXISTS queen.partitions (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
                name VARCHAR(255) NOT NULL DEFAULT 'Default',
                created_at TIMESTAMPTZ DEFAULT NOW(),
                last_activity TIMESTAMPTZ DEFAULT NOW(),
                UNIQUE(queue_id, name)
            );
            
            CREATE TABLE IF NOT EXISTS queen.messages (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                transaction_id VARCHAR(255) UNIQUE NOT NULL,
                trace_id UUID DEFAULT gen_random_uuid(),
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                payload JSONB NOT NULL,
                created_at TIMESTAMPTZ DEFAULT NOW(),
                is_encrypted BOOLEAN DEFAULT FALSE
            );
            
            CREATE TABLE IF NOT EXISTS queen.partition_consumers (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',
                last_consumed_id UUID DEFAULT '00000000-0000-0000-0000-000000000000',
                last_consumed_created_at TIMESTAMPTZ,
                total_messages_consumed BIGINT DEFAULT 0,
                total_batches_consumed BIGINT DEFAULT 0,
                last_consumed_at TIMESTAMPTZ,
                lease_expires_at TIMESTAMPTZ,
                lease_acquired_at TIMESTAMPTZ,
                message_batch JSONB,
                batch_size INTEGER DEFAULT 0,
                acked_count INTEGER DEFAULT 0,
                worker_id VARCHAR(255),
                pending_estimate BIGINT DEFAULT 0,
                last_stats_update TIMESTAMPTZ,
                batch_retry_count INTEGER DEFAULT 0,
                created_at TIMESTAMPTZ DEFAULT NOW(),
                UNIQUE(partition_id, consumer_group),
                CHECK (
                    (last_consumed_id = '00000000-0000-0000-0000-000000000000' 
                     AND last_consumed_created_at IS NULL)
                    OR 
                    (last_consumed_id != '00000000-0000-0000-0000-000000000000' 
                     AND last_consumed_created_at IS NOT NULL)
                )
            );
            
            CREATE TABLE IF NOT EXISTS queen.messages_consumed (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                consumer_group VARCHAR(255) NOT NULL,
                messages_completed INTEGER DEFAULT 0,
                messages_failed INTEGER DEFAULT 0,
                acked_at TIMESTAMPTZ DEFAULT NOW()
            );
            
            CREATE TABLE IF NOT EXISTS queen.dead_letter_queue (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                message_id UUID REFERENCES queen.messages(id) ON DELETE CASCADE,
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                consumer_group VARCHAR(255),
                error_message TEXT,
                retry_count INTEGER DEFAULT 0,
                original_created_at TIMESTAMPTZ,
                failed_at TIMESTAMPTZ DEFAULT NOW()
            );
            
            CREATE TABLE IF NOT EXISTS queen.retention_history (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                messages_deleted INTEGER DEFAULT 0,
                retention_type VARCHAR(50),
                executed_at TIMESTAMPTZ DEFAULT NOW()
            );
        )";
        
        auto result2 = QueryResult(conn->exec(create_tables_sql));
        if (!result2.is_success()) {
            spdlog::error("Failed to create tables: {}", result2.error_message());
            return false;
        }
        
        // Create indexes for optimal query performance
        std::string create_indexes_sql = R"(
            CREATE INDEX IF NOT EXISTS idx_queues_name ON queen.queues(name);
            CREATE INDEX IF NOT EXISTS idx_queues_priority ON queen.queues(priority DESC);
            CREATE INDEX IF NOT EXISTS idx_queues_namespace ON queen.queues(namespace) WHERE namespace IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_queues_task ON queen.queues(task) WHERE task IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_queues_namespace_task ON queen.queues(namespace, task) WHERE namespace IS NOT NULL AND task IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_queues_retention_enabled ON queen.queues(retention_enabled) WHERE retention_enabled = true;
            CREATE INDEX IF NOT EXISTS idx_partitions_queue_name ON queen.partitions(queue_id, name);
            CREATE INDEX IF NOT EXISTS idx_partitions_last_activity ON queen.partitions(last_activity);
            CREATE INDEX IF NOT EXISTS idx_messages_partition_created_id ON queen.messages(partition_id, created_at, id);
            CREATE INDEX IF NOT EXISTS idx_messages_transaction_id ON queen.messages(transaction_id);
            CREATE INDEX IF NOT EXISTS idx_messages_trace_id ON queen.messages(trace_id);
            CREATE INDEX IF NOT EXISTS idx_messages_created_at ON queen.messages(created_at);
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_lookup ON queen.partition_consumers(partition_id, consumer_group);
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_active_leases ON queen.partition_consumers(partition_id, consumer_group, lease_expires_at) WHERE lease_expires_at IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_expired_leases ON queen.partition_consumers(lease_expires_at) WHERE lease_expires_at IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_progress ON queen.partition_consumers(last_consumed_at DESC);
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_idle ON queen.partition_consumers(partition_id, consumer_group) WHERE lease_expires_at IS NULL;
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_consumer_group ON queen.partition_consumers(consumer_group);
            CREATE INDEX IF NOT EXISTS idx_messages_consumed_acked_at ON queen.messages_consumed(acked_at DESC);
            CREATE INDEX IF NOT EXISTS idx_messages_consumed_partition_acked ON queen.messages_consumed(partition_id, acked_at DESC);
            CREATE INDEX IF NOT EXISTS idx_messages_consumed_consumer_acked ON queen.messages_consumed(consumer_group, acked_at DESC);
            CREATE INDEX IF NOT EXISTS idx_messages_consumed_partition_id ON queen.messages_consumed(partition_id);
            CREATE INDEX IF NOT EXISTS idx_dlq_partition ON queen.dead_letter_queue(partition_id);
            CREATE INDEX IF NOT EXISTS idx_dlq_consumer_group ON queen.dead_letter_queue(consumer_group);
            CREATE INDEX IF NOT EXISTS idx_dlq_failed_at ON queen.dead_letter_queue(failed_at DESC);
            CREATE INDEX IF NOT EXISTS idx_dlq_message_consumer ON queen.dead_letter_queue(message_id, consumer_group);
            CREATE INDEX IF NOT EXISTS idx_retention_history_partition ON queen.retention_history(partition_id);
            CREATE INDEX IF NOT EXISTS idx_retention_history_executed ON queen.retention_history(executed_at);
        )";
        
        auto result3 = QueryResult(conn->exec(create_indexes_sql));
        if (!result3.is_success()) {
            spdlog::warn("Some indexes may not have been created: {}", result3.error_message());
        } else {
            spdlog::info("Database indexes created successfully");
        }
        
        // Create triggers
        std::string create_triggers_sql = R"(
            CREATE OR REPLACE FUNCTION update_partition_last_activity()
            RETURNS TRIGGER AS $$
            BEGIN
                UPDATE queen.partitions 
                SET last_activity = NOW() 
                WHERE id = NEW.partition_id;
                RETURN NEW;
            END;
            $$ LANGUAGE plpgsql;

            DROP TRIGGER IF EXISTS trigger_update_partition_activity ON queen.messages;
            CREATE TRIGGER trigger_update_partition_activity
            AFTER INSERT ON queen.messages
            FOR EACH ROW
            EXECUTE FUNCTION update_partition_last_activity();

            CREATE OR REPLACE FUNCTION update_pending_on_push()
            RETURNS TRIGGER AS $$
            BEGIN
                UPDATE queen.partition_consumers
                SET pending_estimate = pending_estimate + 1,
                    last_stats_update = NOW()
                WHERE partition_id = NEW.partition_id;
                RETURN NEW;
            END;
            $$ LANGUAGE plpgsql;

            DROP TRIGGER IF EXISTS trigger_update_pending_on_push ON queen.messages;
            CREATE TRIGGER trigger_update_pending_on_push
            AFTER INSERT ON queen.messages
            FOR EACH ROW
            EXECUTE FUNCTION update_pending_on_push();
        )";
        
        auto result4 = QueryResult(conn->exec(create_triggers_sql));
        if (!result4.is_success()) {
            spdlog::warn("Some triggers may not have been created: {}", result4.error_message());
        } else {
            spdlog::info("Database triggers created successfully");
        }
        
        spdlog::info("Database schema V3 initialized successfully (tables, indexes, triggers)");
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize schema: {}", e.what());
        return false;
    }
}

} // namespace queen
