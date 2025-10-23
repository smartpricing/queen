#include "queen/async_database.hpp"
#include "queen/queue_manager.hpp"
#include "queen/encryption.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>
#include <mutex>
#include <libusockets.h>

// uSockets event constants (from internal headers)
#ifdef __linux__
    #include <sys/epoll.h>
    #define LIBUS_SOCKET_READABLE EPOLLIN
    #define LIBUS_SOCKET_WRITABLE EPOLLOUT
    #define LIBUS_SOCKET_CONTEXT_POLL_TYPE_SOCKET_POLL 0
#else
    // macOS/BSD/kqueue or GCD
    #define LIBUS_SOCKET_READABLE 1
    #define LIBUS_SOCKET_WRITABLE 2
    #define LIBUS_SOCKET_CONTEXT_POLL_TYPE_SOCKET_POLL 0
#endif

namespace queen {

// UUID v7 generator (copied from queue_manager.cpp to avoid circular dependency)
static std::string generate_uuid_v7() {
    static std::mutex uuid_mutex;
    static uint64_t last_ms = 0;
    static uint16_t sequence = 0;
    
    // Static random number generator to be seeded once
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    
    std::lock_guard<std::mutex> lock(uuid_mutex);
    
    auto now = std::chrono::system_clock::now();
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // Handle monotonic sequence
    if (ms == last_ms) {
        sequence++;
        if (sequence == 0) {  // Overflow - wait for next ms
            while (ms <= last_ms) {
                now = std::chrono::system_clock::now();
                ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();
            }
            sequence = 0;
        }
    } else {
        last_ms = ms;
        sequence = 0;
    }
    
    // Generate random bytes for the last 62 bits
    uint64_t rand_a = dis(gen);
    
    // Build UUID v7 (binary):
    // 48 bits: timestamp (ms)
    // 4 bits: version (7)
    // 12 bits: sequence counter
    // 2 bits: variant (10)
    // 62 bits: random
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    // timestamp_ms (48 bits = 6 bytes)
    oss << std::setw(8) << ((ms >> 16) & 0xFFFFFFFF);
    oss << '-';
    oss << std::setw(4) << ((ms & 0xFFFF));
    oss << '-';
    
    // version (4 bits) + sequence (12 bits)
    oss << std::setw(4) << (0x7000 | (sequence & 0x0FFF));
    oss << '-';
    
    // variant (2 bits) + random (14 bits)
    oss << std::setw(4) << (0x8000 | ((rand_a >> 48) & 0x3FFF));
    oss << '-';
    
    // random (48 bits)
    oss << std::setw(12) << (rand_a & 0xFFFFFFFFFFFF);
    
    return oss.str();
}

// Timer callback for AsyncPushOperation - called periodically to check if PostgreSQL socket is ready
static void async_push_timer_callback(us_timer_t* timer) {
    AsyncPushOperation* op = *(AsyncPushOperation**)us_timer_ext(timer);
    
    if (!op) {
        // Operation already deleted, stop timer
        us_timer_close(timer);
        return;
    }
    
    // If completed, delete the operation and stop
    if (op->completed_) {
        // CRITICAL: Clear the pointer FIRST to prevent double-delete on next timer tick
        *(AsyncPushOperation**)us_timer_ext(timer) = nullptr;
        delete op;  // This will close the timer in destructor
        return;
    }
    
    // Check if socket has data available
    op->check_socket_ready();
}

// Deletion timer callback
static void async_delete_callback(us_timer_t* timer) {
    AsyncPushOperation* op = *(AsyncPushOperation**)us_timer_ext(timer);
    if (op) {
        spdlog::warn("[AsyncPush] 🗑️  Deletion timer fired, deleting operation");
        delete op;
    }
}

// ============================================================================
// AsyncPushOperation Implementation
// ============================================================================

AsyncPushOperation::AsyncPushOperation(
    std::unique_ptr<DatabaseConnection> conn,
    DatabasePool* pool,
    const std::vector<PushItem>& items,
    const std::string& queue_name,
    const std::string& partition_name,
    Callback callback,
    uWS::Loop* loop
)
    : conn_(std::move(conn))
    , pool_(pool)
    , pg_conn_(nullptr)
    , items_(items)
    , queue_name_(queue_name)
    , partition_name_(partition_name)
    , callback_(callback)
    , state_(State::CHECKING_QUEUE)
    , encryption_enabled_(false)
    , completed_(false)  // Public member, initialized first
    , loop_(loop)
    , timer_(nullptr)
{
    results_.reserve(items_.size());
}

AsyncPushOperation::~AsyncPushOperation() {
    // CRITICAL: Do NOT close timer here!
    // The timer will be closed automatically when the callback returns and sees null pointer
    // Closing a timer from within its own callback causes heap corruption
    if (timer_) {
        timer_ = nullptr;
    }
    
    // Return connection to pool if not already done
    if (conn_ && pool_) {
        // Set back to blocking mode before returning to pool
        if (pg_conn_) {
            int result = PQsetnonblocking(pg_conn_, 0);
            if (result != 0) {
                spdlog::error("[AsyncPush] Failed to set blocking mode: {}", PQerrorMessage(pg_conn_));
            }
        }
        pool_->return_connection(std::move(conn_));
    }
}

void AsyncPushOperation::start() {
    if (!conn_ || !conn_->is_valid()) {
        fail_operation("Invalid database connection");
        return;
    }
    
    // Get raw PostgreSQL connection
    pg_conn_ = conn_->get_raw_connection();
    
    // CRITICAL: Set connection to non-blocking mode
    if (PQsetnonblocking(pg_conn_, 1) != 0) {
        fail_operation("Failed to set non-blocking mode: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    // Create timer for periodic socket checking
    timer_ = us_create_timer((struct us_loop_t*)loop_, 0, sizeof(AsyncPushOperation*));
    *(AsyncPushOperation**)us_timer_ext(timer_) = this;
    
    // Start state machine
    transition_to_next_state();
    
    // Start timer to check socket every 1ms
    us_timer_set(timer_, async_push_timer_callback, 1, 1);
}

void AsyncPushOperation::check_socket_ready() {
    if (completed_) return;
    
    // First, flush any pending output
    int flush_result = PQflush(pg_conn_);
    
    if (flush_result == -1) {
        fail_operation("Failed to flush output: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    if (flush_result == 1) {
        // Still have data to send, wait for next timer tick
        return;
    }
    
    // Consume available input from PostgreSQL
    if (PQconsumeInput(pg_conn_) == 0) {
        fail_operation("Failed to consume input: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    // Check if query is still processing
    if (PQisBusy(pg_conn_)) {
        // Still waiting for more data, timer will call us again
        return;
    }
    
    // Get result (non-blocking now that PQisBusy returned false)
    PGresult* result = PQgetResult(pg_conn_);
    
    if (!result) {
        // No result yet, wait for next timer tick
        return;
    }
    
    spdlog::debug("[AsyncPush] Got result in state {}", static_cast<int>(state_));
    handle_query_result(result);
    PQclear(result);
    
    // Get any additional results (for multi-statement queries)
    while ((result = PQgetResult(pg_conn_)) != nullptr) {
        PQclear(result);
    }
    
    // Move to next state (only after result is processed)
    if (!completed_) {
        transition_to_next_state();
    }
}

void AsyncPushOperation::transition_to_next_state() {
    if (completed_) return;
    
    switch (state_) {
        case State::CHECKING_QUEUE:
            send_check_queue();
            break;
        
        case State::CHECKING_SIZE:
            send_check_size();
            break;
        
        case State::CHECKING_ENCRYPTION:
            send_check_encryption();
            break;
        
        case State::ENSURING_PARTITION:
            send_ensure_partition();
            break;
        
        case State::GETTING_PARTITION_ID:
            send_get_partition_id();
            break;
        
        case State::BEGINNING_TRANSACTION:
            send_begin_transaction();
            break;
        
        case State::INSERTING_MESSAGES:
            send_insert_messages();
            break;
        
        case State::COMMITTING_TRANSACTION:
            send_commit_transaction();
            break;
        
        case State::DONE:
            complete_operation();
            break;
        
        case State::ERROR:
            // Already handled
            break;
    }
}

void AsyncPushOperation::send_check_queue() {
    spdlog::debug("[AsyncPush] Checking if queue exists: {}", queue_name_);
    
    const char* param_values[] = { queue_name_.c_str() };
    
    if (PQsendQueryParams(pg_conn_,
                          "SELECT id FROM queen.queues WHERE name = $1",
                          1,
                          nullptr,
                          param_values,
                          nullptr,
                          nullptr,
                          0) == 0) {
        fail_operation("Failed to send queue check: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    // Query sent, timer will check when response is ready
    PQflush(pg_conn_);
}

void AsyncPushOperation::send_check_size() {
    spdlog::debug("[AsyncPush] Checking queue size limits");
    
    const char* query = R"(
        SELECT q.max_queue_size, COALESCE(COUNT(m.id), 0) as current_size
        FROM queen.queues q
        LEFT JOIN queen.partitions p ON p.queue_id = q.id
        LEFT JOIN queen.messages m ON m.partition_id = p.id
        WHERE q.name = $1 AND q.max_queue_size > 0
        GROUP BY q.max_queue_size
    )";
    
    const char* param_values[] = { queue_name_.c_str() };
    
    if (PQsendQueryParams(pg_conn_, query, 1, nullptr, param_values, nullptr, nullptr, 0) == 0) {
        fail_operation("Failed to send size check: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    PQflush(pg_conn_);
}

void AsyncPushOperation::send_check_encryption() {
    spdlog::debug("[AsyncPush] Checking encryption setting");
    
    const char* param_values[] = { queue_name_.c_str() };
    
    if (PQsendQueryParams(pg_conn_,
                          "SELECT encryption_enabled FROM queen.queues WHERE name = $1",
                          1,
                          nullptr,
                          param_values,
                          nullptr,
                          nullptr,
                          0) == 0) {
        fail_operation("Failed to send encryption check: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    PQflush(pg_conn_);
}

void AsyncPushOperation::send_ensure_partition() {
    spdlog::debug("[AsyncPush] Ensuring partition exists: {}", partition_name_);
    
    const char* query = R"(
        INSERT INTO queen.partitions (queue_id, name)
        SELECT id, $2 FROM queen.queues WHERE name = $1
        ON CONFLICT (queue_id, name) DO NOTHING
    )";
    
    const char* param_values[] = { queue_name_.c_str(), partition_name_.c_str() };
    
    if (PQsendQueryParams(pg_conn_, query, 2, nullptr, param_values, nullptr, nullptr, 0) == 0) {
        fail_operation("Failed to ensure partition: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    PQflush(pg_conn_);
}

void AsyncPushOperation::send_get_partition_id() {
    spdlog::debug("[AsyncPush] Getting partition ID");
    
    const char* query = R"(
        SELECT p.id FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name = $1 AND p.name = $2
    )";
    
    const char* param_values[] = { queue_name_.c_str(), partition_name_.c_str() };
    
    if (PQsendQueryParams(pg_conn_, query, 2, nullptr, param_values, nullptr, nullptr, 0) == 0) {
        fail_operation("Failed to get partition ID: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    PQflush(pg_conn_);
}

void AsyncPushOperation::send_begin_transaction() {
    spdlog::debug("[AsyncPush] Beginning transaction");
    
    if (PQsendQuery(pg_conn_, "BEGIN") == 0) {
        fail_operation("Failed to begin transaction: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    PQflush(pg_conn_);
}

void AsyncPushOperation::send_insert_messages() {
    spdlog::debug("[AsyncPush] Inserting {} messages", items_.size());
    
    // Build message data
    std::vector<std::string> message_ids;
    std::vector<std::string> transaction_ids;
    std::vector<std::string> payloads;
    std::vector<std::string> trace_ids;
    std::vector<std::string> partition_ids;
    std::vector<bool> encrypted_flags;
    
    EncryptionService* enc_service = encryption_enabled_ ? get_encryption_service() : nullptr;
    
    for (const auto& item : items_) {
        PushResult result;
        result.transaction_id = item.transaction_id.value_or(generate_uuid());
        
        message_ids.push_back(generate_uuid());
        transaction_ids.push_back(result.transaction_id);
        partition_ids.push_back(partition_id_);
        
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
                payload_to_store = item.payload.dump();
            }
        } else {
            payload_to_store = item.payload.dump();
        }
        
        payloads.push_back(payload_to_store);
        trace_ids.push_back(item.trace_id.value_or(""));
        encrypted_flags.push_back(is_encrypted);
        results_.push_back(result);
    }
    
    // Build PostgreSQL arrays
    std::string ids_array = build_pg_array(message_ids);
    std::string txn_array = build_pg_array(transaction_ids);
    std::string part_array = build_pg_array(partition_ids);
    std::string payload_array = build_pg_array(payloads);
    std::string trace_array = build_pg_array_with_nulls(trace_ids);
    std::string encrypted_array = build_bool_array(encrypted_flags);
    
    const char* query = R"(
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
    
    const char* param_values[] = {
        ids_array.c_str(),
        txn_array.c_str(),
        part_array.c_str(),
        payload_array.c_str(),
        trace_array.c_str(),
        encrypted_array.c_str()
    };
    
    if (PQsendQueryParams(pg_conn_, query, 6, nullptr, param_values, nullptr, nullptr, 0) == 0) {
        fail_operation("Failed to insert messages: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    PQflush(pg_conn_);
}

void AsyncPushOperation::send_commit_transaction() {
    spdlog::debug("[AsyncPush] Committing transaction");
    
    if (PQsendQuery(pg_conn_, "COMMIT") == 0) {
        fail_operation("Failed to commit transaction: " + std::string(PQerrorMessage(pg_conn_)));
        return;
    }
    
    PQflush(pg_conn_);
}

void AsyncPushOperation::handle_query_result(PGresult* result) {
    ExecStatusType status = PQresultStatus(result);
    
    switch (state_) {
        case State::CHECKING_QUEUE: {
            if (status != PGRES_TUPLES_OK) {
                fail_operation("Queue check failed: " + std::string(PQresultErrorMessage(result)));
                return;
            }
            
            if (PQntuples(result) == 0) {
                fail_operation("Queue '" + queue_name_ + "' does not exist");
                return;
            }
            
            state_ = State::CHECKING_SIZE;
            break;
        }
        
        case State::CHECKING_SIZE: {
            if (status != PGRES_TUPLES_OK) {
                fail_operation("Size check failed: " + std::string(PQresultErrorMessage(result)));
                return;
            }
            
            if (PQntuples(result) > 0) {
                int max_size = std::atoi(PQgetvalue(result, 0, 0));
                int current_size = std::atoi(PQgetvalue(result, 0, 1));
                
                if (current_size + items_.size() > max_size) {
                    fail_operation("Queue would exceed max capacity");
                    return;
                }
            }
            
            state_ = State::CHECKING_ENCRYPTION;
            break;
        }
        
        case State::CHECKING_ENCRYPTION: {
            if (status != PGRES_TUPLES_OK) {
                fail_operation("Encryption check failed: " + std::string(PQresultErrorMessage(result)));
                return;
            }
            
            if (PQntuples(result) > 0) {
                std::string enc_str = PQgetvalue(result, 0, 0);
                encryption_enabled_ = (enc_str == "t" || enc_str == "true");
            }
            
            state_ = State::ENSURING_PARTITION;
            break;
        }
        
        case State::ENSURING_PARTITION: {
            if (status != PGRES_COMMAND_OK) {
                fail_operation("Failed to ensure partition: " + std::string(PQresultErrorMessage(result)));
                return;
            }
            
            state_ = State::GETTING_PARTITION_ID;
            break;
        }
        
        case State::GETTING_PARTITION_ID: {
            if (status != PGRES_TUPLES_OK || PQntuples(result) == 0) {
                fail_operation("Partition not found");
                return;
            }
            
            partition_id_ = PQgetvalue(result, 0, 0);
            state_ = State::BEGINNING_TRANSACTION;
            break;
        }
        
        case State::BEGINNING_TRANSACTION: {
            if (status != PGRES_COMMAND_OK) {
                fail_operation("Failed to begin transaction: " + std::string(PQresultErrorMessage(result)));
                return;
            }
            
            state_ = State::INSERTING_MESSAGES;
            break;
        }
        
        case State::INSERTING_MESSAGES: {
            if (status != PGRES_TUPLES_OK) {
                fail_operation("Insert failed: " + std::string(PQresultErrorMessage(result)));
                // Rollback will happen in destructor
                return;
            }
            
            // Update results with IDs from database
            int num_rows = PQntuples(result);
            for (int i = 0; i < num_rows && i < static_cast<int>(results_.size()); ++i) {
                results_[i].status = "queued";
                results_[i].message_id = PQgetvalue(result, i, 0);
                
                const char* trace_val = PQgetvalue(result, i, 2);
                if (trace_val && strlen(trace_val) > 0) {
                    results_[i].trace_id = trace_val;
                }
            }
            
            state_ = State::COMMITTING_TRANSACTION;
            break;
        }
        
        case State::COMMITTING_TRANSACTION: {
            if (status != PGRES_COMMAND_OK) {
                fail_operation("Failed to commit: " + std::string(PQresultErrorMessage(result)));
                return;
            }
            
            state_ = State::DONE;
            break;
        }
        
        default:
            break;
    }
}

void AsyncPushOperation::fail_operation(const std::string& error) {
    if (completed_) return;
    
    spdlog::error("[AsyncPush] Operation failed: {}", error);
    
    state_ = State::ERROR;
    
    // Rollback if in transaction
    if (pg_conn_) {
        PQsendQuery(pg_conn_, "ROLLBACK");
    }
    
    // Fill results with error
    for (size_t i = results_.size(); i < items_.size(); ++i) {
        PushResult result;
        result.transaction_id = items_[i].transaction_id.value_or(generate_uuid());
        result.status = "failed";
        result.error = error;
        results_.push_back(result);
    }
    
    for (auto& result : results_) {
        if (result.status != "queued") {
            result.status = "failed";
            result.error = error;
        }
    }
    
    // Call callback
    if (callback_) {
        callback_(results_);
    }
    
    // Mark as completed - the timer will delete us on next tick
    completed_ = true;
    spdlog::info("[AsyncPush] Operation failed, marked for deletion on next timer tick");
}

void AsyncPushOperation::complete_operation() {
    if (completed_) return;
    
    // Call callback with results
    if (callback_) {
        callback_(results_);
    }
    
    // Mark as completed - the timer will delete us on next tick
    completed_ = true;
}

std::string AsyncPushOperation::generate_uuid() {
    return generate_uuid_v7();
}

std::string AsyncPushOperation::build_pg_array(const std::vector<std::string>& vec) {
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
}

std::string AsyncPushOperation::build_pg_array_with_nulls(const std::vector<std::string>& vec) {
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
}

std::string AsyncPushOperation::build_bool_array(const std::vector<bool>& vec) {
    std::string result = "{";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) result += ",";
        result += vec[i] ? "true" : "false";
    }
    result += "}";
    return result;
}

// ============================================================================
// AsyncDatabaseManager Implementation
// ============================================================================

AsyncDatabaseManager::AsyncDatabaseManager(DatabasePool* pool)
    : pool_(pool)
{
}

void AsyncDatabaseManager::push_messages_async(
    const std::vector<PushItem>& items,
    const std::string& queue_name,
    const std::string& partition_name,
    uWS::Loop* loop,
    AsyncPushOperation::Callback callback
) {
    // Get connection asynchronously via callback (event-driven!)
    pool_->get_connection_async([items, queue_name, partition_name, callback, loop, this](
        std::unique_ptr<DatabaseConnection> conn) {
        
        if (!conn) {
            spdlog::error("[AsyncPush] Callback received null connection");
            
            std::vector<PushResult> error_results;
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_uuid_v7());
                result.status = "failed";
                result.error = "Failed to get database connection";
                error_results.push_back(result);
            }
            callback(error_results);
            return;
        }
        
        // Create and start async operation
        auto* operation = new AsyncPushOperation(
            std::move(conn),
            pool_,
            items,
            queue_name,
            partition_name,
            callback,
            loop
        );
        
        operation->start();
    });
}

} // namespace queen

