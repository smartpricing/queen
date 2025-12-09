#ifndef _QUEEN_HPP_
#define _QUEEN_HPP_
#ifdef __cplusplus
#include <uv.h>
#include <libpq-fe.h>
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <map>
#include <string>
#include <deque>
#include <iostream>
#include <functional>
#include <memory>
#include <random>
#include <array>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <algorithm>

/**
- logs
- performance tuning
- backoff light query 
- shared state 
- all the queries to custom, find better way for analytics
*/

namespace queen {

// UUIDv7 generator - time-ordered UUIDs for proper message ordering
inline std::string generate_uuidv7() {
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

// ============================================================================
// Schema Initialization
// ============================================================================

namespace detail {

// Helper: Read file contents
inline std::string read_sql_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open SQL file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Helper: Get sorted SQL files from directory
inline std::vector<std::string> get_sql_files(const std::string& dir) {
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

// Helper: Execute SQL (blocking, handles multi-statement)
inline bool exec_sql(PGconn* conn, const std::string& sql, const std::string& context = "") {
    if (!PQsendQuery(conn, sql.c_str())) {
        spdlog::error("[libqueen] [{}] PQsendQuery failed: {}", context, PQerrorMessage(conn));
        return false;
    }
    
    while (PQisBusy(conn)) {
        if (!PQconsumeInput(conn)) {
            spdlog::error("[libqueen] [{}] PQconsumeInput failed: {}", context, PQerrorMessage(conn));
            return false;
        }
    }
    
    bool success = true;
    PGresult* res;
    while ((res = PQgetResult(conn)) != nullptr) {
        ExecStatusType status = PQresultStatus(res);
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
            spdlog::error("[libqueen] [{}] SQL error: {}", context, PQerrorMessage(conn));
            success = false;
        }
        PQclear(res);
    }
    return success;
}

} // namespace detail

/**
 * Initialize database schema and stored procedures.
 * 
 * This function creates the queen schema and loads all SQL files from:
 * - schema/schema.sql (base tables, indexes, triggers)
 * - schema/procedures/NNN_name.sql (stored procedures, loaded in alphabetical order)
 * 
 * The function is idempotent - safe to call multiple times.
 * 
 * @param connection_string PostgreSQL connection string
 * @param schema_dir Path to schema directory (default: "schema")
 * @return true if successful, false on error
 */
inline bool initialize_schema(const std::string& connection_string, 
                              const std::string& schema_dir = "schema") {
    // Connect to database
    PGconn* conn = PQconnectdb(connection_string.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[libqueen] Failed to connect for schema initialization: {}", 
                      PQerrorMessage(conn));
        PQfinish(conn);
        return false;
    }
    
    spdlog::info("[libqueen] Initializing schema from: {}", schema_dir);
    
    try {
        // 1. Create queen schema
        if (!detail::exec_sql(conn, "CREATE SCHEMA IF NOT EXISTS queen", "create schema")) {
            spdlog::error("[libqueen] Failed to create queen schema");
            PQfinish(conn);
            return false;
        }
        
        // 2. Load base schema
        std::string schema_file = schema_dir + "/schema.sql";
        if (!std::filesystem::exists(schema_file)) {
            spdlog::error("[libqueen] Base schema file not found: {}", schema_file);
            PQfinish(conn);
            return false;
        }
        
        std::string schema_sql = detail::read_sql_file(schema_file);
        if (!detail::exec_sql(conn, schema_sql, "base schema")) {
            spdlog::error("[libqueen] Failed to apply base schema");
            PQfinish(conn);
            return false;
        }
        spdlog::info("[libqueen] Base schema applied successfully");
        
        // 3. Load stored procedures (sorted alphabetically)
        std::string procedures_dir = schema_dir + "/procedures";
        auto procedure_files = detail::get_sql_files(procedures_dir);
        
        if (!procedure_files.empty()) {
            spdlog::info("[libqueen] Loading {} stored procedure(s)...", procedure_files.size());
            for (const auto& proc_file : procedure_files) {
                spdlog::debug("[libqueen] Loading: {}", proc_file);
                std::string proc_sql = detail::read_sql_file(proc_file);
                if (!detail::exec_sql(conn, proc_sql, proc_file)) {
                    spdlog::error("[libqueen] Failed to load procedure: {}", proc_file);
                    PQfinish(conn);
                    return false;
                }
            }
            spdlog::info("[libqueen] All stored procedures loaded successfully");
        }
        
        PQfinish(conn);
        spdlog::info("[libqueen] Schema initialization complete");
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("[libqueen] Schema initialization failed: {}", e.what());
        PQfinish(conn);
        return false;
    }
}

enum class 
JobType {
    PUSH,           // Push messages (batchable)
    POP,            // Pop messages - specific partition (NOT batchable)
    ACK,            // Single acknowledge
    TRANSACTION,    // Atomic transaction (NOT batchable)
    RENEW_LEASE,    // Renew message lease (batchable)
    CUSTOM          // Custom SQL query
};

// map between JobType enum and std::string
const std::map<JobType, std::string> JobTypeToSql = {
    {JobType::PUSH, "SELECT queen.push_messages_v2($1::jsonb)"},
    {JobType::POP, "SELECT queen.pop_unified_batch($1::jsonb)"},
    {JobType::ACK, "SELECT queen.ack_messages_v2($1::jsonb)"},
    {JobType::TRANSACTION, "SELECT queen.execute_transaction_v2($1::jsonb)"},
    {JobType::RENEW_LEASE, "SELECT queen.renew_lease_v2($1::jsonb)"},
    {JobType::CUSTOM, "CUSTOM"}
};
    
struct 
JobRequest {
    JobType op_type;  // Operation type
    std::string request_id;
    std::string sql; // For custom SQL queries
    std::vector<std::string> params;

    std::string queue_name;       // For notification matching
    std::string partition_name;   // For grouping
    std::string consumer_group;   // For grouping

    size_t item_count = 0;  // Number of items in this request (for micro-batching decisions)
    std::chrono::steady_clock::time_point queued_at;
        
    std::chrono::steady_clock::time_point wait_deadline;  // Absolute timeout
    std::chrono::steady_clock::time_point next_check;     // When to check next
    uint16_t backoff_count = 0; // Number of times the job has been backoff

    int batch_size = 1;           // Client's requested batch size
    bool auto_ack = false;        // Auto-acknowledge messages on delivery (QoS 0)
    
    // Validate the job request and return error message if invalid
    std::optional<std::string> validate() const {
        switch (op_type) {
            case JobType::PUSH:
                if (params.empty() || params[0].empty()) {
                    return "PUSH requires params with message data (JSON array of items with queue, partition, payload, messageId)";
                }
                break;
                
            case JobType::POP:
                if (params.empty() || params[0].empty()) {
                    return "POP requires params with request data (JSON array with idx, queue_name, partition_name, consumer_group, batch_size, lease_seconds, worker_id)";
                }
                break;
                
            case JobType::ACK:
                if (params.empty() || params[0].empty()) {
                    return "ACK requires params with acknowledgment data (JSON array with index, transactionId, partitionId, status)";
                }
                break;
                
            case JobType::TRANSACTION:
                if (params.empty() || params[0].empty()) {
                    return "TRANSACTION requires params with operations array (JSON array of {type, queue, partition, payload, ...} objects)";
                }
                break;
                
            case JobType::RENEW_LEASE:
                if (params.empty() || params[0].empty()) {
                    return "RENEW_LEASE requires params with lease data (JSON array with index, leaseId, extendSeconds)";
                }
                break;
                
            case JobType::CUSTOM:
                if (sql.empty()) {
                    return "CUSTOM requires sql field with the SQL query to execute";
                }
                break;
        }
        return std::nullopt;  // Valid
    }
}; 

// Job with callback
struct 
PendingJob {
    JobRequest job;
    std::function<void(std::string result)> callback;
};  

// Forward declaration
class Queen;

struct 
DBConnection {
    PGconn* conn = nullptr;
    int socket_fd = -1;
    uint16_t idx = 0;
    uv_poll_t poll_handle;
    bool poll_initialized = false;
    Queen* pool = nullptr;
    std::vector<std::shared_ptr<PendingJob>> jobs;
    std::vector<std::pair<int, int>> job_idx_ranges; // AI // (start_idx, count) for each job
    
    // Atomic flag for lock-free reconnection signaling
    std::atomic<bool> needs_poll_init{false};
    
    // Default constructor
    DBConnection() = default;
    
    // Move constructor (needed because atomic has deleted copy ctor)
    DBConnection(DBConnection&& other) noexcept
        : conn(other.conn)
        , socket_fd(other.socket_fd)
        , idx(other.idx)
        , poll_handle(other.poll_handle)
        , poll_initialized(other.poll_initialized)
        , pool(other.pool)
        , jobs(std::move(other.jobs))
        , job_idx_ranges(std::move(other.job_idx_ranges))
        , needs_poll_init(other.needs_poll_init.load(std::memory_order_relaxed))
    {
        other.conn = nullptr;
        other.socket_fd = -1;
        other.poll_initialized = false;
    }
    
    // Move assignment (needed for vector operations)
    DBConnection& operator=(DBConnection&& other) noexcept {
        if (this != &other) {
            conn = other.conn;
            socket_fd = other.socket_fd;
            idx = other.idx;
            poll_handle = other.poll_handle;
            poll_initialized = other.poll_initialized;
            pool = other.pool;
            jobs = std::move(other.jobs);
            job_idx_ranges = std::move(other.job_idx_ranges);
            needs_poll_init.store(other.needs_poll_init.load(std::memory_order_relaxed), std::memory_order_relaxed);
            
            other.conn = nullptr;
            other.socket_fd = -1;
            other.poll_initialized = false;
        }
        return *this;
    }
    
    // Delete copy operations (atomic is not copyable)
    DBConnection(const DBConnection&) = delete;
    DBConnection& operator=(const DBConnection&) = delete;
};

class Queen {
public:    
    Queen(const std::string& conn_str, 
        uint16_t statement_timeout_ms = 30000,
        uint16_t db_connection_count = 10,
        uint16_t queue_interval_ms = 10,
        uint16_t pop_wait_initial_interval_ms = 100,
        uint16_t pop_wait_backoff_threshold = 3,
        double pop_wait_backoff_multiplier = 3.0,
        uint16_t pop_wait_max_interval_ms = 5000) noexcept(false) : 
        _loop(nullptr), 
        _loop_initialized(false),
        _queue_interval_ms(queue_interval_ms),
        _conn_str(conn_str),
        _statement_timeout_ms(statement_timeout_ms),
        _db_connection_count(db_connection_count),
        _pop_wait_initial_interval_ms(pop_wait_initial_interval_ms),
        _pop_wait_backoff_threshold(pop_wait_backoff_threshold),
        _pop_wait_backoff_multiplier(pop_wait_backoff_multiplier),
        _pop_wait_max_interval_ms(pop_wait_max_interval_ms) {
        // Initialize libuv mutex
        uv_mutex_init(&_mutex_job_queue);
        // Start the event loop thread
        _init();
    }

    ~Queen() {
        // Stop reconnection thread first
        _reconnect_running.store(false);
        if (_reconnect_thread.joinable()) {
            _reconnect_thread.join();
        }
        
        if (_loop_initialized) {
            uv_loop_close(_loop);
            delete _loop;
        }
        uv_mutex_destroy(&_mutex_job_queue);
    }

    void 
    submit(JobRequest&& job, std::function<void(std::string result)> cb) {
        // Validate the job request before queueing
        auto validation_error = job.validate();
        if (validation_error.has_value()) {
            spdlog::error("[libqueen] Invalid JobRequest: {}", validation_error.value());
            // Call callback with error response
            if (cb) {
                nlohmann::json error_response = {
                    {"success", false},
                    {"error", validation_error.value()}
                };
                cb(error_response.dump());
            }
            return;
        }
        
        auto pending = std::make_shared<PendingJob>(PendingJob{std::move(job), std::move(cb)});
        uv_mutex_lock(&_mutex_job_queue);
        _job_queue.push_back(pending);
        uv_mutex_unlock(&_mutex_job_queue);
        // uv_async_send(&_queue_signal);
    }

private:
    
    // libuv event loop and handles
    uv_loop_t* _loop;
    uv_timer_t _queue_timer;
    uv_async_t _queue_signal;
    std::atomic<bool> _loop_initialized;    
    uint16_t _queue_interval_ms;

    // job queue mutex
    uv_mutex_t _mutex_job_queue;
    // job queue
    std::deque<std::shared_ptr<PendingJob>> _job_queue;

    // connection to the database 
    std::string _conn_str;
    uint16_t _statement_timeout_ms;
    uint16_t _db_connection_count;
    std::vector<uint16_t> _free_slot_indexes;
    std::vector<DBConnection> _db_connections;

    // Pop wait configuration
    uint16_t _pop_wait_initial_interval_ms;
    uint16_t _pop_wait_backoff_threshold;
    double _pop_wait_backoff_multiplier;
    uint16_t _pop_wait_max_interval_ms;

    // Pop backoff tracker - stores shared_ptr to same jobs in _job_queue
    std::unordered_map<std::string, std::map<std::string, std::shared_ptr<PendingJob>>> _pop_backoff_tracker;

    // Reconnection thread (runs in background, reconnects dead DB connections)
    std::thread _reconnect_thread;
    std::atomic<bool> _reconnect_running{false};
    uv_async_t _reconnect_signal;

    // _ _  _ _ 
    // | | || | |
    // | ' || ' |
    // `___'|__/                
    // Setup the uv event loop
    void 
    _init() noexcept(false) {
        std::thread([this] {
            if (_loop_initialized) return;
            
            _loop = new uv_loop_t;
            if (uv_loop_init(_loop) != 0) {
                delete _loop;
                _loop = nullptr;
                throw std::runtime_error("Failed to initialize libuv loop");
            }
            _loop_initialized.store(true);
            
            // Set data pointer so callbacks can access this instance
            uv_handle_set_data((uv_handle_t*)&_queue_timer, this);
            uv_handle_set_data((uv_handle_t*)&_queue_signal, this);
            uv_handle_set_data((uv_handle_t*)&_reconnect_signal, this);
            
            uv_timer_init(_loop, &_queue_timer);
            uv_timer_start(&_queue_timer, _uv_timer_cb, 0, _queue_interval_ms);
            uv_async_init(_loop, &_queue_signal, _uv_async_cb);
            uv_async_init(_loop, &_reconnect_signal, _uv_reconnect_signal_cb);

            // Connect all slots to the database
            if (!_connect_all_slots()) {
                throw std::runtime_error("Failed to connect all slots to the database");
            }

            // Start reconnection thread after initial connections are established
            _start_reconnect_thread();

            uv_run(_loop, UV_RUN_DEFAULT);
        }).detach();
    }
    
    // Start the background reconnection thread
    void
    _start_reconnect_thread() noexcept(true) {
        _reconnect_running.store(true);
        _reconnect_thread = std::thread([this] {
            _reconnect_loop();
        });
    }
    
    // Background thread that reconnects dead database connections
    void
    _reconnect_loop() noexcept(true) {
        spdlog::info("[libqueen] Reconnection thread started");
        
        while (_reconnect_running.load()) {
            // Sleep for 1 second between reconnection attempts
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            if (!_reconnect_running.load()) break;
            
            // Check for dead connections and reconnect
            for (uint16_t i = 0; i < _db_connection_count; i++) {
                DBConnection& slot = _db_connections[i];
                
                // Skip if connection is alive or already pending poll init
                if (slot.conn != nullptr || slot.needs_poll_init.load(std::memory_order_acquire)) {
                    continue;
                }
                
                spdlog::info("[libqueen] Reconnecting dead slot {}", i);
                
                // Reconnect (blocking - OK in this thread)
                if (_reconnect_slot_db(slot)) {
                    // Signal event loop to initialize poll handle
                    slot.needs_poll_init.store(true, std::memory_order_release);
                    uv_async_send(&_reconnect_signal);
                    spdlog::info("[libqueen] Slot {} reconnected, signaling event loop", i);
                } else {
                    spdlog::warn("[libqueen] Failed to reconnect slot {}", i);
                }
                
                // Only try ONE slot per cycle to avoid overwhelming
                break;
            }
        }
        
        spdlog::info("[libqueen] Reconnection thread stopped");
    }
    
    // Reconnect slot - DB connection only (called from reconnect thread)
    // Does NOT touch libuv handles (not thread-safe)
    bool
    _reconnect_slot_db(DBConnection& slot) noexcept(true) {
        slot.conn = PQconnectdb(_conn_str.c_str());
        
        if (PQstatus(slot.conn) != CONNECTION_OK) {
            spdlog::error("[libqueen] Reconnection failed: {}", PQerrorMessage(slot.conn));
            PQfinish(slot.conn);
            slot.conn = nullptr;
            return false;
        }
        
        // Set non-blocking mode
        if (PQsetnonblocking(slot.conn, 1) != 0) {
            spdlog::error("[libqueen] Failed to set non-blocking on reconnect: {}", PQerrorMessage(slot.conn));
            PQfinish(slot.conn);
            slot.conn = nullptr;
            return false;
        }
        
        // Set statement timeout
        std::string timeout_sql = "SET statement_timeout = " + std::to_string(_statement_timeout_ms);
        PGresult* res = PQexec(slot.conn, timeout_sql.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::warn("[libqueen] Failed to set statement timeout on reconnect: {}", PQerrorMessage(slot.conn));
        }
        PQclear(res);
        
        // Store socket fd (will be used by event loop for poll init)
        slot.socket_fd = PQsocket(slot.conn);
        
        return true;
    }
    
    // Callback when reconnect signal is received - initialize poll handles
    static void
    _uv_reconnect_signal_cb(uv_async_t* handle) noexcept(false) {
        auto* self = static_cast<Queen*>(uv_handle_get_data((uv_handle_t*)handle));
        self->_finalize_reconnected_slots();
    }
    
    // Finalize reconnected slots by initializing their poll handles (must run on event loop thread)
    void
    _finalize_reconnected_slots() noexcept(true) {
        for (uint16_t i = 0; i < _db_connection_count; i++) {
            DBConnection& slot = _db_connections[i];
            
            if (!slot.needs_poll_init.load(std::memory_order_acquire)) {
                continue;
            }
            
            // Initialize poll handle (must be on event loop thread)
            if (slot.socket_fd >= 0) {
                if (uv_poll_init(_loop, &slot.poll_handle, slot.socket_fd) == 0) {
                    slot.poll_handle.data = &slot;
                    slot.poll_initialized = true;
                    
                    // Add back to free pool
                    _free_slot_indexes.push_back(i);
                    spdlog::info("[libqueen] Slot {} poll initialized and added to pool", i);
                } else {
                    spdlog::error("[libqueen] Failed to init poll for reconnected slot {}", i);
                    // Disconnect and let reconnect thread try again
                    PQfinish(slot.conn);
                    slot.conn = nullptr;
                    slot.socket_fd = -1;
                }
            }
            
            // Clear flag
            slot.needs_poll_init.store(false, std::memory_order_release);
        }
    }

    // Timer callback to process the job queue
    static void 
    _uv_timer_cb(uv_timer_t* handle) noexcept(false) {
        auto* self = static_cast<Queen*>(uv_handle_get_data((uv_handle_t*)handle));
        // Drain the queue
        std::vector<std::shared_ptr<PendingJob>> batch;
        uv_mutex_lock(&self->_mutex_job_queue);
        while (!self->_job_queue.empty()) {
            batch.push_back(std::move(self->_job_queue.front()));
            self->_job_queue.pop_front();
        }
        uv_mutex_unlock(&self->_mutex_job_queue);

        if (batch.empty()) {
            return;
        }

        auto grouped_jobs = self->_group_jobs_by_type(batch);
        std::vector<std::shared_ptr<PendingJob>> jobs_to_requeue;
        for (auto& [type, jobs] : grouped_jobs) {
            if (type == JobType::POP) {
                std::vector<std::shared_ptr<PendingJob>> ready_jobs;
                for (auto& job : jobs) {
                    bool has_wait_deadline = (job->job.wait_deadline != std::chrono::steady_clock::time_point{});
                    if (!has_wait_deadline) { // This if for pop with no wait deadline
                        ready_jobs.push_back(job);
                    } else if (job->job.next_check > std::chrono::steady_clock::now() &&
                        job->job.wait_deadline < std::chrono::steady_clock::now()) {
                           self->_send_empty_response(job);
                    } else if (job->job.next_check <= std::chrono::steady_clock::now() &&
                        job->job.wait_deadline > std::chrono::steady_clock::now()) {
                        ready_jobs.push_back(job);
                    } else if (job->job.next_check > std::chrono::steady_clock::now() &&
                        job->job.wait_deadline > std::chrono::steady_clock::now()) {
                        jobs_to_requeue.push_back(job);
                    } else {
                        self->_send_empty_response(job);
                    }
                }
                if (!ready_jobs.empty()) {
                    self->_send_jobs_to_slot(type, ready_jobs, jobs_to_requeue);
                }
            } else if (type != JobType::CUSTOM) {
                self->_send_jobs_to_slot(type, jobs, jobs_to_requeue);
            } else {
                // For loop for custom jobs
            }
        }

        self->_requeue_jobs(jobs_to_requeue);
    }

    void
    _start_watching_slot(DBConnection& slot, int events) noexcept(true) {
        if (slot.poll_initialized && slot.socket_fd >= 0) {
            uv_poll_start(&slot.poll_handle, events, _uv_socket_event_cb);
        }
    } 

    static void
    _uv_socket_event_cb(uv_poll_t* handle, int status, int events) noexcept(false) {
        auto* slot = static_cast<DBConnection*>(handle->data);
        if (!slot || !slot->pool) return;

        auto* pool = slot->pool;
        
        if (status < 0) {
            spdlog::error("[libqueen] Poll error: {}", uv_strerror(status));
            return;
        }
                
        // Handle writable event (flushing send buffer)
        if (events & UV_WRITABLE) {
            int flush_result = PQflush(slot->conn);
            if (flush_result == 0) {
                // All data sent - switch to read-only (CRITICAL: prevents 100% CPU!)
                uv_poll_start(handle, UV_READABLE, _uv_socket_event_cb);
            } else if (flush_result == -1) {
                // Flush failed - error
                spdlog::error("[libqueen] PQflush failed");
                return;
            }
            // flush_result == 1: more to send, keep watching WRITABLE
        }
        
        // Handle readable event (data from PostgreSQL)
        if (events & UV_READABLE) {
            if (!PQconsumeInput(slot->conn)) {
                pool->_handle_slot_error(*slot, std::string("PQconsumeInput failed: ") + PQerrorMessage(slot->conn));
                return;
            }
            
            // Check if query is complete
            if (PQisBusy(slot->conn) == 0) {
                pool->_process_slot_result(*slot);
            }
            // If still busy, keep waiting for more data
        }
    }

    // Async callback to process the job queue on signal
    static void 
    _uv_async_cb(uv_async_t* handle) noexcept(false) {
        auto* self = static_cast<Queen*>(uv_handle_get_data((uv_handle_t*)handle));
        (void)self; // TODO: process jobs
    }

    void
    _process_slot_result(DBConnection& slot) noexcept(false) {
        PGresult* res;
        while ((res = PQgetResult(slot.conn)) != NULL) {
            // Check status
            ExecStatusType status = PQresultStatus(res);
            if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) {
                // Success - parse result once and dispatch by idx
                auto data = PQgetvalue(res, 0, 0);
                auto json_results = nlohmann::json::parse(data);
                
                // Handle non-array results (e.g., TRANSACTION returns a single object)
                // In this case, send the entire result to all jobs
                if (!json_results.is_array()) {
                    for (auto& job : slot.jobs) {
                        job->callback(json_results.dump());
                    }
                    slot.jobs.clear();
                    slot.job_idx_ranges.clear();
                    PQclear(res);
                    continue;
                }
                
                // Build map from idx to result for O(1) lookup
                // Note: Different stored procedures use different field names:
                //   - POP uses "idx"
                //   - PUSH/ACK use "index"
                std::map<int, nlohmann::json> results_by_idx;
                for (auto& result_item : json_results) {
                    int idx = -1;
                    if (result_item.contains("idx")) {
                        idx = result_item["idx"].get<int>();
                    } else if (result_item.contains("index")) {
                        idx = result_item["index"].get<int>();
                    }
                    if (idx >= 0) {
                        results_by_idx[idx] = std::move(result_item);
                    }
                }
                
                // Dispatch results to jobs using idx ranges
                // Each job may have contributed multiple items (e.g., PUSH with many messages)
                for (size_t job_idx = 0; job_idx < slot.jobs.size(); ++job_idx) {
                    auto& job = slot.jobs[job_idx];
                    
                    // Get idx range for this job (set in _send_jobs_to_slot)
                    int start_idx = 0, count = 1;  // defaults for single-item jobs
                    if (job_idx < slot.job_idx_ranges.size()) {
                        start_idx = slot.job_idx_ranges[job_idx].first;
                        count = slot.job_idx_ranges[job_idx].second;
                    }
                    
                    // Collect all results for this job's idx range
                    nlohmann::json job_results = nlohmann::json::array();
                    for (int i = start_idx; i < start_idx + count; ++i) {
                        auto it = results_by_idx.find(i);
                        if (it != results_by_idx.end()) {
                            job_results.push_back(it->second);
                        }
                    }
                    
                    if (job_results.empty()) {
                        spdlog::warn("[libqueen] No results found for job {} (idx range [{}, {}))", 
                                    job_idx, start_idx, start_idx + count);
                        continue;
                    }
                    
                    switch (job->job.op_type) {
                        case JobType::POP: {
                            // For POP, we expect exactly 1 result per job
                            nlohmann::json& result_item = job_results[0];
                            bool has_messages = result_item.contains("result") &&
                                              result_item["result"].contains("messages") &&
                                              !result_item["result"]["messages"].empty();
                            bool has_wait_deadline = (job->job.wait_deadline != std::chrono::steady_clock::time_point{});

                            if (!has_messages && has_wait_deadline) {
                                // Set next backoff time and requeue (same shared_ptr!)
                                _set_next_backoff_time(job);
                                _requeue_jobs({job});
                            } else {
                                // Remove the job from the pop backoff tracker
                                auto& queue_map = _pop_backoff_tracker[job->job.queue_name];
                                queue_map.erase(job->job.request_id);
                                if (queue_map.empty()) {
                                    _pop_backoff_tracker.erase(job->job.queue_name);
                                }
                                
                                // Inject partitionId into each message for client compatibility
                                // Client expects partitionId on each message for ACK operations
                                if (result_item.contains("result") && 
                                    result_item["result"].contains("partitionId") &&
                                    result_item["result"].contains("messages")) {
                                    auto partition_id = result_item["result"]["partitionId"];
                                    auto lease_id = result_item["result"].value("leaseId", nlohmann::json(nullptr));
                                    for (auto& msg : result_item["result"]["messages"]) {
                                        msg["partitionId"] = partition_id;
                                        if (!lease_id.is_null()) {
                                            msg["leaseId"] = lease_id;
                                        }
                                    }
                                }
                                
                                // Wrap result in array for callback compatibility
                                nlohmann::json wrapped = nlohmann::json::array();
                                wrapped.push_back(result_item);
                                job->callback(wrapped.dump());
                            }
                            break;
                        }
                        case JobType::PUSH: {
                            // Signal backoff tracker that the push is successful
                            _update_pop_backoff_tracker(job->job.queue_name);
                            // Return all results for this job (may have multiple messages)
                            job->callback(job_results.dump());
                            break;
                        }
                        default: {
                            // Return all results for this job
                            job->callback(job_results.dump());
                            break;
                        }
                    }
                }
                slot.jobs.clear();
                slot.job_idx_ranges.clear();
            } else {
                spdlog::error("[libqueen] Query failed: {}", PQresultErrorMessage(res));
            }
            PQclear(res);  // Free the result
        }
        
        // All results consumed - stop watching, return slot to pool
        uv_poll_stop(&slot.poll_handle);
        _free_slot(slot);
    }

    //    _       _       
    //   | | ___ | |_  ___
    //  _| |/ . \| . \<_-<
    //  \__/\___/|___//__/
    //      
    std::map<JobType, std::vector<std::shared_ptr<PendingJob>>>
    _group_jobs_by_type(const std::vector<std::shared_ptr<PendingJob>>& jobs) noexcept(true) {
        std::map<JobType, std::vector<std::shared_ptr<PendingJob>>> grouped_jobs;
        for (auto& job : jobs) {
            grouped_jobs[job->job.op_type].push_back(job);
        }
        return grouped_jobs;
    }

    void
    _send_jobs_to_slot(JobType type, const std::vector<std::shared_ptr<PendingJob>>& jobs, std::vector<std::shared_ptr<PendingJob>>& jobs_to_requeue) noexcept(false) {
        // Send all the batched jobs to the database if we have a free slot
        if (_has_free_slot()) {
            DBConnection& slot = _get_free_slot();
            // Join all the jobs params into a single jsonb
            // IMPORTANT: Renumber idx sequentially so we can dispatch results correctly
            // Also track idx ranges per job for proper result dispatch

            nlohmann::json combined = nlohmann::json::array();
            std::vector<std::pair<int, int>> idx_ranges;  // (start_idx, count) per job
            int sequential_idx = 0;
            for (const auto& pending : jobs) {
                int start_idx = sequential_idx;
                int count = 0;
                if (!pending->job.params.empty()) {
                    auto arr = nlohmann::json::parse(pending->job.params[0]);
                    for (auto& item : arr) {
                        item["idx"] = sequential_idx++;  // Renumber idx for correct dispatch
                        combined.push_back(std::move(item));
                        count++;
                    }
                }
                idx_ranges.push_back({start_idx, count});
            }
                    
            std::string merged_jsonb = combined.dump();
            const char* param_ptrs[] = { merged_jsonb.c_str() };
                    
            // Send the jobs to the database
            auto sql_string_for_type = JobTypeToSql.at(type);
            slot.jobs.insert(slot.jobs.end(), jobs.begin(), jobs.end());
            slot.job_idx_ranges = std::move(idx_ranges);
            int sent = PQsendQueryParams(
                slot.conn, 
                sql_string_for_type.c_str(), 
                1,//static_cast<int>(param_ptrs.size()),
                nullptr, 
                param_ptrs,//param_ptrs.data(),
                nullptr, nullptr, 0);

            if (!sent) { 
                spdlog::error("[libqueen] Failed to send jobs to the database: {}", PQerrorMessage(slot.conn));
                jobs_to_requeue.insert(jobs_to_requeue.end(), jobs.begin(), jobs.end());
                slot.jobs.clear();  // Clear jobs from slot since we're requeuing them
                
                // Check if connection is dead - if so, disconnect it
                // Reconnection thread will automatically reconnect it
                if (PQstatus(slot.conn) != CONNECTION_OK) {
                    spdlog::warn("[libqueen] Connection dead, marking for reconnection");
                    _disconnect_slot(slot);
                } else {
                    // Connection still OK, return slot to pool
                    _free_slot(slot);
                }
                return;  // Don't start watching a failed/disconnected slot
            }

            // Start watching for write (to flush) and read (for results)
            _start_watching_slot(slot, UV_WRITABLE | UV_READABLE);              
        } else {
            jobs_to_requeue.insert(jobs_to_requeue.end(), jobs.begin(), jobs.end());
        }
    }


    // TODO: check starvation
    void
    _requeue_jobs(const std::vector<std::shared_ptr<PendingJob>>& jobs) noexcept(true) {
        uv_mutex_lock(&_mutex_job_queue);
        for (const auto& job : jobs) {
            _job_queue.push_front(job);
        }
        uv_mutex_unlock(&_mutex_job_queue);
    }

    void
    _set_next_backoff_time(std::shared_ptr<PendingJob>& job) noexcept(true) {
        // TODO check shared state
        job->job.backoff_count++;
        // Compute the next backoff time
        // if backoff count is greater than the threshold, set the next backoff time to the max interval
        if (job->job.backoff_count > _pop_wait_backoff_threshold) {
            job->job.next_check = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(_pop_wait_initial_interval_ms * job->job.backoff_count * _pop_wait_backoff_multiplier)); 
            if (job->job.next_check > std::chrono::steady_clock::now() + std::chrono::milliseconds(_pop_wait_max_interval_ms)) {
                job->job.next_check = std::chrono::steady_clock::now() + std::chrono::milliseconds(_pop_wait_max_interval_ms);
            }
        } else {
            job->job.next_check = std::chrono::steady_clock::now() + std::chrono::milliseconds(_pop_wait_initial_interval_ms);
        }
        // Store the SAME shared_ptr in tracker (not a copy!) - now updates propagate to queued job
        _pop_backoff_tracker[job->job.queue_name][job->job.request_id] = job;
    }

    void 
    _update_pop_backoff_tracker (const std::string& queue_name) noexcept(true) {
        auto it = _pop_backoff_tracker.find(queue_name);
        if (it != _pop_backoff_tracker.end()) {
            for (auto& [request_id, job_ptr] : it->second) {
                job_ptr->job.next_check = std::chrono::steady_clock::now();
                job_ptr->job.backoff_count = 0;  // Reset backoff on activity
            }
        }
    }

    void
    _send_empty_response(std::shared_ptr<PendingJob>& job) noexcept(true) {
        auto response = nlohmann::json::array();
        response.push_back({
            {"idx", 0},
            {"result", {
                {"success", true},
                {"queue", job->job.queue_name},
                {"partition", job->job.partition_name},
                {"partitionId", nullptr},
                {"leaseId", nullptr},
                {"consumerGroup", job->job.consumer_group},
                {"messages", nlohmann::json::array()}
            }}
        });
        job->callback(response.dump());
    }
    
    //  ___                            _    _               
    // |  _> ___ ._ _ ._ _  ___  ___ _| |_ <_> ___ ._ _  ___
    // | <__/ . \| ' || ' |/ ._>/ | ' | |  | |/ . \| ' |<_-<
    // `___/\___/|_|_||_|_|\___.\_|_. |_|  |_|\___/|_|_|/__/
    //   

    bool
    _has_free_slot() noexcept(true) {
        return !_free_slot_indexes.empty();
    }
    
    DBConnection& 
    _get_free_slot() noexcept(true) {
        uint16_t idx = _free_slot_indexes.back();
        _free_slot_indexes.pop_back();
        DBConnection& slot = _db_connections[idx];
        slot.idx = idx;
        return slot;
    }

    void
    _free_slot(DBConnection& slot) noexcept(true) {
        _free_slot_indexes.push_back(slot.idx);
    }

    // Connect all slots to the database
    bool _connect_all_slots() noexcept(true) {
        _db_connections.resize(_db_connection_count);
        for (uint16_t i = 0; i < _db_connection_count; i++) {
            DBConnection& slot = _db_connections[i];
            slot.pool = this;
            if (!_connect_slot(slot)) {
                return false;
            }
            _free_slot_indexes.push_back(i);
        }
        return true;
    }

    // Connect to the database
    bool 
    _connect_slot(DBConnection& slot) noexcept(true) {
        slot.conn = PQconnectdb(_conn_str.c_str());
    
        if (PQstatus(slot.conn) != CONNECTION_OK) {
            spdlog::error("[libqueen] Connection failed: {}", PQerrorMessage(slot.conn));
            PQfinish(slot.conn);
            slot.conn = nullptr;
            return false;
        }
        
        // Set non-blocking mode
        if (PQsetnonblocking(slot.conn, 1) != 0) {
            spdlog::error("[libqueen] Failed to set non-blocking: {}", PQerrorMessage(slot.conn));
            PQfinish(slot.conn);
            slot.conn = nullptr;
            return false;
        }
        
        // Set statement timeout
        std::string timeout_sql = "SET statement_timeout = " + std::to_string(_statement_timeout_ms);
        PGresult* res = PQexec(slot.conn, timeout_sql.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::warn("[libqueen] Failed to set statement timeout: {}", PQerrorMessage(slot.conn));
        }
        PQclear(res);
        
        slot.socket_fd = PQsocket(slot.conn);

        // Initialize libuv poll handle for this connection's socket
        if (_loop_initialized && slot.socket_fd >= 0) {
            if (uv_poll_init(_loop, &slot.poll_handle, slot.socket_fd) == 0) {
                slot.poll_handle.data = &slot;
                slot.poll_initialized = true;
            } else {
                spdlog::warn("[libqueen] Failed to initialize uv_poll for slot");
                slot.poll_initialized = false;
            }
        }
        
        return true;
    }

    void
    _handle_slot_error(DBConnection& slot, const std::string& error_msg) noexcept(false) {
        spdlog::error("[libqueen] Slot error: {}", error_msg);
        _disconnect_slot(slot);
        _requeue_jobs(slot.jobs);
        slot.jobs.clear();
    }    

    // Disconnect from the database
    void _disconnect_slot(DBConnection& slot) noexcept(true) {
        // Stop polling before disconnecting
        if (slot.poll_initialized) {
            uv_poll_stop(&slot.poll_handle);
            slot.poll_initialized = false;
        }

        if (slot.conn) {
            PQfinish(slot.conn);
            slot.conn = nullptr;
            slot.socket_fd = -1;
        }
        
        // Reset reconnection flag - slot is now available for reconnect thread
        slot.needs_poll_init.store(false, std::memory_order_release);
    }
};

}

#endif /* __cplusplus */
#endif /* _QUEEN_HPP_ */
