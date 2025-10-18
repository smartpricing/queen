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
    // UUIDv7 implementation (time-ordered like Node.js)
    // Format: TTTTTTTT-TTTT-7RRR-VRRR-RRRRRRRRRRRR
    // T = timestamp (48 bits), R = random (74 bits), V = variant (2 bits), 7 = version
    
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(0, 0xFFFFFFFFFFFFFFFF);
    
    // Generate random bits
    uint64_t rand1 = dis(gen);
    uint64_t rand2 = dis(gen);
    
    // Build UUIDv7
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    // Timestamp (48 bits) - first 12 hex chars
    ss << std::setw(8) << ((ms >> 16) & 0xFFFFFFFF);
    ss << "-";
    ss << std::setw(4) << (ms & 0xFFFF);
    
    // Version (4 bits) + random (12 bits)
    ss << "-7" << std::setw(3) << (rand1 & 0xFFF);
    
    // Variant (2 bits) + random (14 bits)  
    ss << "-" << std::setw(1) << (8 | ((rand1 >> 12) & 0x3));
    ss << std::setw(3) << ((rand1 >> 16) & 0xFFF);
    
    // Random (48 bits)
    ss << "-" << std::setw(12) << (rand2 & 0xFFFFFFFFFFFF);
    
    return ss.str();
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
    
    // Group items by queue and partition for efficient batch processing
    std::map<std::string, std::vector<PushItem>> partition_groups;
    for (const auto& item : items) {
        std::string key = item.queue + ":" + item.partition;
        partition_groups[key].push_back(item);
    }
    
    std::vector<PushResult> all_results;
    all_results.reserve(items.size());
    
    // Process each partition group as a batch
    for (const auto& [partition_key, partition_items] : partition_groups) {
        auto batch_results = push_messages_batch(partition_items);
        all_results.insert(all_results.end(), batch_results.begin(), batch_results.end());
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
        
        // For simplicity, process each message individually but within the same transaction
        // This ensures atomicity while keeping the code simple
        ScopedConnection conn(db_pool_.get());
        
        // Start transaction
        if (!conn->begin_transaction()) {
            throw std::runtime_error("Failed to begin transaction");
        }
        
        try {
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                
                std::string sql;
                std::vector<std::string> params;
                
                // Generate UUIDv7 for message ID to ensure proper ordering
                std::string message_id = generate_uuid();
                
                // Prepare payload - encrypt if needed
                std::string payload_to_store;
                bool is_encrypted = false;
                
                if (enc_service && enc_service->is_enabled()) {
                    // Encrypt the payload
                    auto encrypted = enc_service->encrypt_payload(item.payload.dump());
                    if (encrypted.has_value()) {
                        // Store encrypted data as JSON object
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
                
                if (item.trace_id.has_value() && !item.trace_id->empty()) {
                    sql = R"(
                        INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id, created_at, is_encrypted)
                        SELECT $1, $2, p.id, $3, $4, NOW(), $7
                        FROM queen.partitions p
                        JOIN queen.queues q ON p.queue_id = q.id
                        WHERE q.name = $5 AND p.name = $6
                        RETURNING id, trace_id
                    )";
                    params = {message_id, result.transaction_id, payload_to_store, item.trace_id.value(), queue_name, partition_name, is_encrypted ? "true" : "false"};
                } else {
                    sql = R"(
                        INSERT INTO queen.messages (id, transaction_id, partition_id, payload, created_at, is_encrypted)
                        SELECT $1, $2, p.id, $3, NOW(), $6
                        FROM queen.partitions p
                        JOIN queen.queues q ON p.queue_id = q.id
                        WHERE q.name = $4 AND p.name = $5
                        RETURNING id, trace_id
                    )";
                    params = {message_id, result.transaction_id, payload_to_store, queue_name, partition_name, is_encrypted ? "true" : "false"};
                }
                
                auto insert_result = QueryResult(conn->exec_params(sql, params));
                if (insert_result.is_success() && insert_result.num_rows() > 0) {
                    result.status = "queued";
                    result.message_id = insert_result.get_value(0, "id");
                    std::string trace_val = insert_result.get_value(0, "trace_id");
                    if (!trace_val.empty()) {
                        result.trace_id = trace_val;
                    }
                } else {
                    result.status = "failed";
                    result.error = insert_result.error_message();
                }
                
                results.push_back(result);
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

std::string QueueManager::acquire_partition_lease(const std::string& queue_name,
                                                const std::string& partition_name,
                                                const std::string& consumer_group,
                                                int lease_time_seconds,
                                                const PopOptions& options) {
    try {
        ScopedConnection conn(db_pool_.get());
        
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
            SELECT id FROM queen.partition_consumers pc
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

PopResult QueueManager::pop_from_queue_partition(const std::string& queue_name,
                                               const std::string& partition_name,
                                               const std::string& consumer_group,
                                               const PopOptions& options) {
    PopResult result;
    
    try {
        ScopedConnection check_conn(db_pool_.get());
        
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
        
        auto window_result = QueryResult(check_conn->exec_params(window_check_sql, {queue_name, partition_name}));
        
        if (!window_result.is_success() || window_result.num_rows() == 0) {
            std::string window_buffer_str = window_result.num_rows() > 0 ? window_result.get_value(0, "window_buffer") : "unknown";
            spdlog::debug("Partition {} blocked by window buffer ({}s)", partition_name, window_buffer_str);
            return result; // Partition not accessible due to window buffer
        }
        
        // Acquire lease for this partition
        std::string lease_id = acquire_partition_lease(queue_name, partition_name, 
                                                     consumer_group, 300, options);
        if (lease_id.empty()) {
            return result; // No lease acquired, return empty result
        }
        
        result.lease_id = lease_id;
        
        ScopedConnection conn(db_pool_.get());
        
        // Get queue configuration for delayed_processing and max_wait_time_seconds
        std::string config_sql = R"(
            SELECT q.delayed_processing, q.max_wait_time_seconds
            FROM queen.queues q
            WHERE q.name = $1
        )";
        
        auto config_result = QueryResult(conn->exec_params(config_sql, {queue_name}));
        int delayed_processing = 0;
        int max_wait_time = 0;
        
        if (config_result.is_success() && config_result.num_rows() > 0) {
            std::string delay_str = config_result.get_value(0, "delayed_processing");
            std::string wait_str = config_result.get_value(0, "max_wait_time_seconds");
            delayed_processing = delay_str.empty() ? 0 : std::stoi(delay_str);
            max_wait_time = wait_str.empty() ? 0 : std::stoi(wait_str);
        }
        
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
        std::string sql = R"(
            WITH message_batch AS (
                SELECT m.id, m.transaction_id, m.payload, m.trace_id, m.created_at, m.is_encrypted,
                       q.name as queue_name, p.name as partition_name, q.priority as queue_priority
                FROM queen.messages m
                JOIN queen.partitions p ON p.id = m.partition_id
                JOIN queen.queues q ON q.id = p.queue_id
                JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                )" + where_clause + R"(
                ORDER BY m.created_at ASC, m.id ASC
                LIMIT )" + limit_param + R"(
            )
            SELECT * FROM message_batch
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
                
                // Parse created_at timestamp
                std::string created_at_str = query_result.get_value(i, "created_at");
                if (!created_at_str.empty()) {
                    // Parse PostgreSQL timestamp format
                    std::tm tm = {};
                    std::istringstream ss(created_at_str);
                    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                    msg.created_at = std::chrono::system_clock::from_time_t(std::mktime(&tm));
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

bool QueueManager::acknowledge_message(const std::string& transaction_id,
                                     const std::string& status,
                                     const std::optional<std::string>& error,
                                     const std::string& consumer_group) {
    try {
        ScopedConnection conn(db_pool_.get());
        
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
            auto result = QueryResult(conn->exec_params(sql, params));
            
            if (result.is_success() && result.num_rows() > 0) {
                bool lease_released = result.get_value(0, "lease_released") == "t";
                if (lease_released) {
                    spdlog::debug("Lease released after ACK for transaction: {}", transaction_id);
                }
            }
            
            return result.is_success();
            
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
                return false;
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
            return cursor_result.is_success();
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to acknowledge message: {}", e.what());
    }
    
    return false;
}

std::vector<bool> QueueManager::acknowledge_messages(const std::vector<AckItem>& acks,
                                                   const std::string& consumer_group) {
    std::vector<bool> results;
    results.reserve(acks.size());
    
    for (const auto& ack : acks) {
        results.push_back(acknowledge_message(ack.transaction_id, ack.status, ack.error, consumer_group));
    }
    
    return results;
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
        
        // Create tables (simplified version of the Node.js schema)
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
                transaction_id UUID UNIQUE NOT NULL,
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                payload JSONB,
                trace_id UUID NULL,
                created_at TIMESTAMPTZ DEFAULT NOW()
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
                UNIQUE(partition_id, consumer_group)
            );
            
            CREATE TABLE IF NOT EXISTS queen.dead_letter_queue (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                message_id UUID REFERENCES queen.messages(id) ON DELETE CASCADE,
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                consumer_group VARCHAR(255),
                error_message TEXT,
                retry_count INTEGER DEFAULT 0,
                original_created_at TIMESTAMPTZ,
                failed_at TIMESTAMPTZ DEFAULT NOW(),
                UNIQUE(message_id, consumer_group)
            );
        )";
        
        auto result2 = QueryResult(conn->exec(create_tables_sql));
        if (!result2.is_success()) {
            spdlog::error("Failed to create tables: {}", result2.error_message());
            return false;
        }
        
        // Fix existing trace_id column to not have default UUID generation
        auto fix_trace_id = QueryResult(conn->exec(
            "ALTER TABLE queen.messages ALTER COLUMN trace_id DROP DEFAULT"
        ));
        if (!fix_trace_id.is_success()) {
            spdlog::warn("Could not remove trace_id default (table might not exist yet): {}", 
                        fix_trace_id.error_message());
        } else {
            spdlog::info("Removed trace_id default UUID generation");
        }
        
        spdlog::info("Database schema initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize schema: {}", e.what());
        return false;
    }
}

} // namespace queen
