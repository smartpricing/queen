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
#include "worker_metrics.hpp"


/**
X messages route return encryped data
- use multiple slots for each tick
X when consumer disconnect, invalidated queen lib
X logs, with atomic counters every second?
- in push, trigger or query to update partition consumers?
X udp on push - shared state only for push
X retnetion cleanup improve
X backoff push per partition, not queue
X improve metrics analtyics query
X only one worker shodul write analtyics
X maintenace mode pop and reset pt
- readme libqueen
- backoff light query, not necessary?
- sc test
X deploy to stage
- article about libqueen/async io
*/

namespace queen {

// UUIDv7 generator - time-ordered UUIDs for proper message ordering
inline std::string 
generate_uuidv7() {
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
inline std::string 
read_sql_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open SQL file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Helper: Get sorted SQL files from directory
inline std::vector<std::string> 
get_sql_files(const std::string& dir) {
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
inline bool 
exec_sql(PGconn* conn, const std::string& sql, const std::string& context = "") {
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
inline bool 
initialize_schema(const std::string& connection_string, 
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
        uint16_t pop_wait_max_interval_ms = 5000,
        uint16_t worker_id = 0,
        const std::string& hostname = "localhost") noexcept(false) : 
        _hostname(hostname),
        _loop(nullptr), 
        _loop_initialized(false),
        _queue_interval_ms(queue_interval_ms),
        _conn_str(conn_str),
        _statement_timeout_ms(statement_timeout_ms),
        _db_connection_count(db_connection_count),
        _pop_wait_initial_interval_ms(pop_wait_initial_interval_ms),
        _pop_wait_backoff_threshold(pop_wait_backoff_threshold),
        _pop_wait_backoff_multiplier(pop_wait_backoff_multiplier),
        _pop_wait_max_interval_ms(pop_wait_max_interval_ms),
        _worker_id(worker_id) {
        // Initialize libuv mutexes
        uv_mutex_init(&_mutex_job_queue);
        uv_mutex_init(&_mutex_backoff_signal);
        
        // Initialize metrics collector
        _metrics = std::make_unique<WorkerMetrics>(
            _hostname,
            _worker_id,
            [this](const std::string& sql, const std::vector<std::string>& params) {
                // Submit metrics write as internal job
                _submit_metrics_write(sql, params);
            }
        );
        
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
        uv_mutex_destroy(&_mutex_backoff_signal);
    }

    void 
    submit(JobRequest&& job, std::function<void(std::string result)> cb) {
        auto pending = std::make_shared<PendingJob>(PendingJob{std::move(job), std::move(cb)});
        
        uv_mutex_lock(&_mutex_job_queue);
        _job_queue.push_back(pending);
        uv_mutex_unlock(&_mutex_job_queue);
        // uv_async_send(&_queue_signal);
    }

    bool 
    invalidate_request(const std::string& request_id) 
    noexcept(true) {
        uv_mutex_lock(&_mutex_job_queue); 
        try {
            // Here we need to find and delete the request from the job queue 
            // and from the pop backoff tracker
            _job_queue.erase(std::remove_if(_job_queue.begin(), _job_queue.end(), [&request_id, this](const std::shared_ptr<PendingJob>& job) {
                return job->job.request_id == request_id;
            }), _job_queue.end());
            
            for (auto it = _pop_backoff_tracker.begin(); it != _pop_backoff_tracker.end(); ) {
                it->second.erase(request_id);
                if (it->second.empty()) {
                    it = _pop_backoff_tracker.erase(it);
                } else {
                    ++it;
                }
            }
            uv_mutex_unlock(&_mutex_job_queue);
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[Worker {}] [libqueen] Failed to invalidate request: {}", _worker_id, e.what());
            uv_mutex_unlock(&_mutex_job_queue);
            return false;
        }
    }

    /**
     * Thread-safe method to update pop backoff tracker.
     * Can be called from any thread (e.g., UDP receive thread).
     * Queues the update and signals the event loop to process it.
     */
    void
    update_pop_backoff_tracker(const std::string& queue_name, const std::string& partition_name) {
        uv_mutex_lock(&_mutex_backoff_signal);
        _backoff_signal_queue.push_back({queue_name, partition_name});
        uv_mutex_unlock(&_mutex_backoff_signal);
        uv_async_send(&_backoff_signal);
    }

private:
    // Worker identity (must be first for initializer order)
    std::string _hostname;
    
    // Performance stats 
    std::chrono::steady_clock::time_point _last_timer_expected;
    uint64_t _event_loop_lag; 
    uint64_t _jobs_done = 0;
    uint16_t _stats_interval_ms = 1000;
    
    // Metrics collector
    std::unique_ptr<WorkerMetrics> _metrics;

    // libuv event loop and handles
    uv_loop_t* _loop;
    uv_timer_t _queue_timer;
    uv_timer_t _stats_timer;
    uv_async_t _queue_signal;
    std::atomic<bool> _loop_initialized;    
    uint16_t _queue_interval_ms;

    // Thread-safe backoff signal (for UDP notifications from other threads)
    struct BackoffSignal {
        std::string queue_name;
        std::string partition_name;
    };
    uv_async_t _backoff_signal;
    uv_mutex_t _mutex_backoff_signal;
    std::deque<BackoffSignal> _backoff_signal_queue;

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
    uint16_t _max_backoff_age_seconds = 65;
    uint16_t _worker_id;

    // Pop backoff tracker - stores shared_ptr to same jobs in _job_queue
    // Key format: "queue_name/partition_name" for specific partition, "queue_name/*" for wildcard
    std::unordered_map<std::string, std::map<std::string, std::shared_ptr<PendingJob>>> _pop_backoff_tracker;

    // Helper to generate backoff key from queue and partition names
    // Specific partition: "myqueue/5", Wildcard: "myqueue/*"
    static std::string 
    _get_backoff_key(const std::string& queue_name, const std::string& partition_name) noexcept(true) {
        if (partition_name.empty()) {
            return queue_name + "/*";
        }
        return queue_name + "/" + partition_name;
    }

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
            uv_handle_set_data((uv_handle_t*)&_stats_timer, this);
            uv_handle_set_data((uv_handle_t*)&_queue_signal, this);
            uv_handle_set_data((uv_handle_t*)&_reconnect_signal, this);
            uv_handle_set_data((uv_handle_t*)&_backoff_signal, this);
            
            uv_timer_init(_loop, &_queue_timer);
            uv_timer_init(_loop, &_stats_timer);
            uv_timer_start(&_queue_timer, _uv_timer_cb, 0, _queue_interval_ms);
            uv_timer_start(&_stats_timer, _uv_stats_timer_cb, 0, _stats_interval_ms);
            uv_async_init(_loop, &_queue_signal, _uv_async_cb);
            uv_async_init(_loop, &_reconnect_signal, _uv_reconnect_signal_cb);
            uv_async_init(_loop, &_backoff_signal, _uv_backoff_signal_cb);

            // Connect all slots to the database
            if (!_connect_all_slots()) {
                throw std::runtime_error("Failed to connect all slots to the database");
            }

            // Start reconnection thread after initial connections are established
            _start_reconnect_thread();
            
            _last_timer_expected = std::chrono::steady_clock::now();
            uv_run(_loop, UV_RUN_DEFAULT);
        }).detach();
    }
    
    // Callback when reconnect signal is received - initialize poll handles
    static void
    _uv_reconnect_signal_cb(uv_async_t* handle) noexcept(false) {
        auto* self = static_cast<Queen*>(uv_handle_get_data((uv_handle_t*)handle));
        self->_finalize_reconnected_slots();
    }
    
    // Callback when backoff signal is received - process pending backoff updates
    // Called on the event loop thread when UDP notifications arrive from other threads
    static void
    _uv_backoff_signal_cb(uv_async_t* handle) noexcept(true) {
        auto* self = static_cast<Queen*>(uv_handle_get_data((uv_handle_t*)handle));
        self->_process_backoff_signals();
    }
    
    // Process all pending backoff signals (runs on event loop thread)
    void
    _process_backoff_signals() noexcept(true) {
        std::deque<BackoffSignal> signals;
        
        // Drain the queue under lock
        uv_mutex_lock(&_mutex_backoff_signal);
        signals.swap(_backoff_signal_queue);
        uv_mutex_unlock(&_mutex_backoff_signal);
        
        // Process all signals (now lock-free)
        for (const auto& sig : signals) {
            _update_pop_backoff_tracker(sig.queue_name, sig.partition_name);
        }
        
        if (!signals.empty()) {
            spdlog::debug("[Worker {}] [libqueen] Processed {} backoff signals from UDP", 
                         _worker_id, signals.size());
        }
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
                    spdlog::info("[Worker {}] [libqueen] Slot {} poll initialized and added to pool", _worker_id, i);
                } else {
                    spdlog::error("[Worker {}] [libqueen] Failed to init poll for reconnected slot {}", _worker_id, i);
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
        
        // Performance stats
        auto now = std::chrono::steady_clock::now();
        uint64_t lag = std::chrono::duration_cast<std::chrono::milliseconds>(now - self->_last_timer_expected).count();
        self->_event_loop_lag = lag;
        self->_last_timer_expected = now;

        if (lag > 100 /*self->_queue_interval_ms * 2*/) {
            spdlog::warn("[Worker {}] [libqueen] Event loop lag is too high: {}ms, expected: {}ms", self->_worker_id, lag, self->_queue_interval_ms);
        }

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
        //spdlog::info("[libqueen] Draining queue: {} jobs", batch.size());

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
            } else if (type == JobType::CUSTOM) {
                // CUSTOM jobs: execute one per slot, no batching
                for (auto& job : jobs) {
                    self->_send_custom_job_to_slot(job, jobs_to_requeue);
                }
            } else {
                self->_send_jobs_to_slot(type, jobs, jobs_to_requeue);
            }
        }

        self->_requeue_jobs(jobs_to_requeue);
    }

    static void
    _uv_stats_timer_cb(uv_timer_t* handle) noexcept(false) {
        auto* self = static_cast<Queen*>(uv_handle_get_data((uv_handle_t*)handle));
        
        // Periodic cleanup of stale backoff entries (every stats interval)
        // Entries are stale if their next_check time is more than 5 minutes old
        self->_cleanup_stale_backoff_entries();
        
        // Get metrics values
        size_t backoff_size = self->_pop_backoff_tracker.size();
        size_t free_slot_indexes_size = self->_free_slot_indexes.size();
        size_t db_connections_size = self->_db_connections.size();
        size_t job_queue_size = self->_job_queue.size();
        int64_t event_loop_lag = static_cast<int64_t>(self->_event_loop_lag) - static_cast<int64_t>(self->_queue_interval_ms);
        if (event_loop_lag < 0) {
            event_loop_lag = 0;
        }
        
        // Record stats sample for metrics (always, even if no jobs done)
        if (self->_metrics) {
            self->_metrics->record_stats_sample(
                static_cast<uint64_t>(event_loop_lag),
                static_cast<uint16_t>(free_slot_indexes_size),
                static_cast<uint16_t>(db_connections_size),
                job_queue_size,
                backoff_size,
                self->_jobs_done
            );
            
            // Check minute boundary and flush if needed
            self->_metrics->check_and_flush();
        }
        
        // Log only if there was activity (avoid log noise)
        if (self->_jobs_done > 0) {
        auto jobs_per_second = self->_jobs_done / (self->_stats_interval_ms / 1000.0);
            spdlog::info("[Worker {}] [libqueen] EVL: {}ms, Slots: {}/{}, Queue: {}, Backoff: {}, JobsDone: {}, jobs/s: {}", 
                self->_worker_id, event_loop_lag, free_slot_indexes_size, db_connections_size, 
                job_queue_size, backoff_size, self->_jobs_done, jobs_per_second);
        }
        
        self->_jobs_done = 0;
    }
    
    // Cleanup stale backoff entries - entries where the job is no longer in the queue
    // or entries that haven't been touched in a long time (5 minutes)
    void
    _cleanup_stale_backoff_entries() noexcept(true) {
        auto now = std::chrono::steady_clock::now();
        auto STALE_THRESHOLD = std::chrono::seconds(_max_backoff_age_seconds);
        
        size_t removed = 0;
        
        for (auto queue_it = _pop_backoff_tracker.begin(); queue_it != _pop_backoff_tracker.end(); ) {
            auto& request_map = queue_it->second;
            
            for (auto req_it = request_map.begin(); req_it != request_map.end(); ) {
                auto& job_ptr = req_it->second;
                
                // Check if job is stale (next_check is very old)
                // This means the job was never removed properly (consumer disconnected without cleanup)
                if (job_ptr && (now - job_ptr->job.next_check) > STALE_THRESHOLD) {
                    spdlog::debug("[Worker {}] [libqueen] Removing stale backoff entry: {}/{}", 
                                 _worker_id, queue_it->first, req_it->first);
                    req_it = request_map.erase(req_it);
                    removed++;
                } else {
                    ++req_it;
                }
            }
            
            // Remove empty queue entries
            if (request_map.empty()) {
                queue_it = _pop_backoff_tracker.erase(queue_it);
            } else {
                ++queue_it;
            }
        }
        
        if (removed > 0) {
            spdlog::info("[Worker {}] [libqueen] Cleaned up {} stale backoff entries", _worker_id, removed);
        }
    }
    
    // Submit metrics write as an internal CUSTOM job
    // This is called from WorkerMetrics::flush_to_database via callback
    void 
    _submit_metrics_write(const std::string& sql, const std::vector<std::string>& params) {
        JobRequest job_req;
        job_req.op_type = JobType::CUSTOM;
        job_req.request_id = "metrics_" + generate_uuidv7();
        job_req.sql = sql;
        job_req.params = params;
        
        // Submit with empty callback (fire and forget)
        submit(std::move(job_req), [](std::string) {});
    }

    static void
    _uv_socket_event_cb(uv_poll_t* handle, int status, int events) noexcept(false) {
        auto* slot = static_cast<DBConnection*>(handle->data);
        if (!slot || !slot->pool) return;

        auto* pool = slot->pool;
        
        if (status < 0) {
            spdlog::error("[Worker {}] [libqueen] Poll error: {}", pool->_worker_id, uv_strerror(status));
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
                spdlog::error("[Worker {}] [libqueen] PQflush failed", pool->_worker_id);
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
    _start_watching_slot(DBConnection& slot, int events) noexcept(true) {
        if (slot.poll_initialized && slot.socket_fd >= 0) {
            uv_poll_start(&slot.poll_handle, events, _uv_socket_event_cb);
        }
    } 

    void
    _process_slot_result(DBConnection& slot) noexcept(false) {
        PGresult* res;
        while ((res = PQgetResult(slot.conn)) != NULL) {
            // Check status
            ExecStatusType status = PQresultStatus(res);
            if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) {
                // Handle CUSTOM queries - return raw result as JSON
                if (!slot.jobs.empty() && slot.jobs[0]->job.op_type == JobType::CUSTOM) {
                    _process_custom_result(slot, res);
                    PQclear(res);
                    continue;
                }
                
                // Success - parse result once and dispatch by idx
                auto data = PQgetvalue(res, 0, 0);
                auto json_results = nlohmann::json::parse(data);
                
                // Handle non-array results (e.g., TRANSACTION returns a single object)
                // In this case, send the entire result to all jobs
                if (!json_results.is_array()) {
                    for (auto& job : slot.jobs) {
                        job->callback(json_results.dump());
                        _jobs_done++;
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
                        spdlog::warn("[Worker {}] [libqueen] No results found for job {} (idx range [{}, {}))", 
                                    _worker_id, job_idx, start_idx, start_idx + count);
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
                                std::string backoff_key = _get_backoff_key(job->job.queue_name, job->job.partition_name);
                                auto& queue_map = _pop_backoff_tracker[backoff_key];
                                queue_map.erase(job->job.request_id);
                                if (queue_map.empty()) {
                                    _pop_backoff_tracker.erase(backoff_key);
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
                                
                                // Record pop metrics
                                if (_metrics) {
                                    _metrics->record_pop_request();  // One request
                                    
                                    // Record each message with lag (from DB result, not item_count)
                                    if (has_messages) {
                                        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch()
                                        ).count();
                                        
                                        size_t message_count = result_item["result"]["messages"].size();
                                        
                                        for (const auto& msg : result_item["result"]["messages"]) {
                                            uint64_t lag_ms = 0;
                                            // Try to compute lag from createdAt timestamp
                                            if (msg.contains("createdAt") && msg["createdAt"].is_string()) {
                                                try {
                                                    // Parse ISO timestamp (e.g., "2024-01-15T10:30:00.123Z")
                                                    std::string created_str = msg["createdAt"].get<std::string>();
                                                    std::tm tm = {};
                                                    int ms = 0;
                                                    // Parse up to seconds
                                                    if (sscanf(created_str.c_str(), "%d-%d-%dT%d:%d:%d.%d",
                                                              &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                                                              &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) >= 6) {
                                                        tm.tm_year -= 1900;
                                                        tm.tm_mon -= 1;
                                                        auto created_time = timegm(&tm);
                                                        auto created_ms = static_cast<int64_t>(created_time) * 1000 + ms;
                                                        lag_ms = static_cast<uint64_t>(std::max(0LL, static_cast<long long>(now_ms - created_ms)));
                                                    }
                                                } catch (...) {
                                                    // If parsing fails, use 0 lag
                                                }
                                            }
                                            _metrics->record_pop_with_lag(job->job.queue_name, lag_ms);
                                        }
                                        
                                        // If auto_ack, also record acks (all succeed since pop succeeded)
                                        if (job->job.auto_ack) {
                                            _metrics->record_ack_request();
                                            _metrics->record_ack_messages(message_count, message_count, 0);
                                        }
                                    }
                                }
                                
                                // Wrap result in array for callback compatibility
                                nlohmann::json wrapped = nlohmann::json::array();
                                wrapped.push_back(result_item);
                                _jobs_done++;
                                job->callback(wrapped.dump());
                            }
                            break;
                        }
                        case JobType::PUSH: {
                            // Signal backoff tracker that the push is successful
                            // Wakes both specific partition consumers and wildcard consumers
                            _update_pop_backoff_tracker(job->job.queue_name, job->job.partition_name);
                            
                            // Record push metrics
                            if (_metrics) {
                                _metrics->record_push_request();  // One request
                                if (job->job.item_count > 0) {
                                    _metrics->record_push_messages(job->job.item_count);  // N messages
                                }
                            }
                            
                            // Return all results for this job (may have multiple messages)
                            _jobs_done++;
                            job->callback(job_results.dump());
                            break;
                        }
                        case JobType::ACK: {
                            // Record ack metrics
                            if (_metrics) {
                                _metrics->record_ack_request();  // One request
                                
                                // Count success/failed from results
                                size_t total = job->job.item_count > 0 ? job->job.item_count : job_results.size();
                                size_t success = 0;
                                size_t failed = 0;
                                size_t dlq = 0;
                                
                                for (const auto& result : job_results) {
                                    // ACK stored procedure returns success directly (not wrapped in "result")
                                    bool is_success = result.contains("success") && 
                                                     result["success"].get<bool>();
                                    if (is_success) {
                                        success++;
                                    } else {
                                        failed++;
                                    }
                                    
                                    // Check for DLQ (also direct, not wrapped)
                                    if (result.contains("dlq") && result["dlq"].get<bool>()) {
                                        dlq++;
                                    }
                                }
                                
                                _metrics->record_ack_messages(total, success, failed);
                                for (size_t i = 0; i < dlq; ++i) {
                                    _metrics->record_dlq();
                                }
                            }
                            // Return all results for this job
                            _jobs_done++;
                            job->callback(job_results.dump());
                            break;
                        }
                        case JobType::TRANSACTION: {
                            // Record transaction metrics
                            if (_metrics) {
                                _metrics->record_transaction();
                                // Check for any DLQ operations in transaction results
                                for (const auto& result : job_results) {
                                    if (result.contains("result") && 
                                        result["result"].contains("dlq") &&
                                        result["result"]["dlq"].get<bool>()) {
                                        _metrics->record_dlq();
                                    }
                                }
                            }
                            // Return all results for this job
                            _jobs_done++;
                            job->callback(job_results.dump());
                            break;
                        }
                        default: {
                            // Return all results for this job (RENEW_LEASE, CUSTOM, etc.)
                            _jobs_done++;
                            job->callback(job_results.dump());
                            break;
                        }
                    }
                }
                slot.jobs.clear();
                slot.job_idx_ranges.clear();
            } else {
                // Query failed - send error to all jobs
                std::string error_msg = PQresultErrorMessage(res);
                spdlog::error("[Worker {}] [libqueen] Query failed: {}", _worker_id, error_msg);
                
                // Record DB error metric
                if (_metrics) {
                    _metrics->record_db_error();
                }
                
                nlohmann::json error_result = {
                    {"success", false},
                    {"error", error_msg}
                };
                
                for (auto& job : slot.jobs) {
                    job->callback(error_result.dump());
                    _jobs_done++;
                }
                slot.jobs.clear();
                slot.job_idx_ranges.clear();
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
                        // Renumber idx/index for correct dispatch when batching multiple jobs
                        // POP uses "idx", ACK/PUSH use "index" - set both to be safe
                        item["idx"] = sequential_idx;
                        item["index"] = sequential_idx;
                        sequential_idx++;
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
                spdlog::error("[Worker {}] [libqueen] Failed to send jobs to the database: {}", _worker_id, PQerrorMessage(slot.conn));
                jobs_to_requeue.insert(jobs_to_requeue.end(), jobs.begin(), jobs.end());
                slot.jobs.clear();  // Clear jobs from slot since we're requeuing them
                
                // Check if connection is dead - if so, disconnect it
                // Reconnection thread will automatically reconnect it
                if (PQstatus(slot.conn) != CONNECTION_OK) {
                    spdlog::warn("[Worker {}] [libqueen] Connection dead, marking for reconnection", _worker_id);
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

    // Process result for CUSTOM queries - converts PGresult to JSON
    void
    _process_custom_result(DBConnection& slot, PGresult* res) noexcept(true) {
        nlohmann::json result;
        
        int num_rows = PQntuples(res);
        int num_cols = PQnfields(res);
        
        if (num_rows == 0) {
            // No rows - return empty result with success flag
            result = {
                {"success", true},
                {"rows", nlohmann::json::array()},
                {"rowCount", 0},
                {"command", PQcmdStatus(res) ? PQcmdStatus(res) : ""}
            };
        } else if (num_rows == 1 && num_cols == 1) {
            // Single value - try to parse as JSON, otherwise return as string
            const char* value = PQgetvalue(res, 0, 0);
            if (value && strlen(value) > 0) {
                try {
                    result = nlohmann::json::parse(value);
                } catch (...) {
                    result = {
                        {"success", true},
                        {"value", value}
                    };
                }
            } else {
                result = {{"success", true}, {"value", nullptr}};
            }
        } else {
            // Multiple rows/columns - build array of objects
            nlohmann::json rows = nlohmann::json::array();
            
            for (int row = 0; row < num_rows; ++row) {
                nlohmann::json row_obj;
                for (int col = 0; col < num_cols; ++col) {
                    const char* col_name = PQfname(res, col);
                    if (PQgetisnull(res, row, col)) {
                        row_obj[col_name] = nullptr;
                    } else {
                        const char* value = PQgetvalue(res, row, col);
                        // Try to parse JSON values, otherwise keep as string
                        if (value && (value[0] == '{' || value[0] == '[')) {
                            try {
                                row_obj[col_name] = nlohmann::json::parse(value);
                            } catch (...) {
                                row_obj[col_name] = value;
                            }
                        } else {
                            row_obj[col_name] = value ? value : "";
                        }
                    }
                }
                rows.push_back(std::move(row_obj));
            }
            
            result = {
                {"success", true},
                {"rows", std::move(rows)},
                {"rowCount", num_rows}
            };
        }
        
        // Send result to the single job (CUSTOM queries are not batched)
        if (!slot.jobs.empty()) {
            slot.jobs[0]->callback(result.dump());
            _jobs_done++;
        }
        slot.jobs.clear();
        slot.job_idx_ranges.clear();
    }

    // Send a single CUSTOM query to a slot (no batching)
    void
    _send_custom_job_to_slot(std::shared_ptr<PendingJob>& job, std::vector<std::shared_ptr<PendingJob>>& jobs_to_requeue) noexcept(false) {
        if (!_has_free_slot()) {
            jobs_to_requeue.push_back(job);
            return;
        }
        
        DBConnection& slot = _get_free_slot();
        slot.jobs.push_back(job);
        slot.job_idx_ranges.push_back({0, 1});  // Single result expected
        
        // Build parameter pointers for PQsendQueryParams
        std::vector<const char*> param_ptrs;
        param_ptrs.reserve(job->job.params.size());
        for (const auto& param : job->job.params) {
            param_ptrs.push_back(param.c_str());
        }
        
        int sent = PQsendQueryParams(
            slot.conn,
            job->job.sql.c_str(),
            static_cast<int>(param_ptrs.size()),
            nullptr,  // Let PostgreSQL infer parameter types
            param_ptrs.empty() ? nullptr : param_ptrs.data(),
            nullptr,  // Text format
            nullptr,  // Text format
            0);       // Text result format
        
        if (!sent) {
            spdlog::error("[Worker {}] [libqueen] Failed to send custom query: {}", _worker_id, PQerrorMessage(slot.conn));
            jobs_to_requeue.push_back(job);
            slot.jobs.clear();
            
            if (PQstatus(slot.conn) != CONNECTION_OK) {
                spdlog::warn("[Worker {}] [libqueen] Connection dead, marking for reconnection", _worker_id);
                _disconnect_slot(slot);
            } else {
                _free_slot(slot);
            }
            return;
        }
        
        // Start watching for write (to flush) and read (for results)
        _start_watching_slot(slot, UV_WRITABLE | UV_READABLE);
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
        // Key is "queue/partition" for specific or "queue/*" for wildcard
        std::string backoff_key = _get_backoff_key(job->job.queue_name, job->job.partition_name);
        _pop_backoff_tracker[backoff_key][job->job.request_id] = job;
    }

    // Wake up backoff consumers when a PUSH arrives
    // Wakes both specific partition consumers AND wildcard consumers
    void 
    _update_pop_backoff_tracker(const std::string& queue_name, const std::string& partition_name) noexcept(true) {
        auto now = std::chrono::steady_clock::now();
        
        // 1. Wake up consumers waiting for this specific partition (queue/partition)
        std::string specific_key = _get_backoff_key(queue_name, partition_name);
        auto it = _pop_backoff_tracker.find(specific_key);
        if (it != _pop_backoff_tracker.end()) {
            for (auto& [request_id, job_ptr] : it->second) {
                job_ptr->job.next_check = now;
                job_ptr->job.backoff_count = 0;  // Reset backoff on activity
            }
        }
        
        // 2. Also wake up wildcard consumers (queue/*) who could pop from any partition
        std::string wildcard_key = queue_name + "/*";
        it = _pop_backoff_tracker.find(wildcard_key);
        if (it != _pop_backoff_tracker.end()) {
            for (auto& [request_id, job_ptr] : it->second) {
                job_ptr->job.next_check = now;
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
            spdlog::error("[Worker {}] [libqueen] Connection failed: {}", _worker_id, PQerrorMessage(slot.conn));
            PQfinish(slot.conn);
            slot.conn = nullptr;
            return false;
        }
        
        // Set non-blocking mode
        if (PQsetnonblocking(slot.conn, 1) != 0) {
            spdlog::error("[Worker {}] [libqueen] Failed to set non-blocking: {}", _worker_id, PQerrorMessage(slot.conn));
            PQfinish(slot.conn);
            slot.conn = nullptr;
            return false;
        }
        
        // Set statement timeout
        std::string timeout_sql = "SET statement_timeout = " + std::to_string(_statement_timeout_ms);
        PGresult* res = PQexec(slot.conn, timeout_sql.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::warn("[Worker {}] [libqueen] Failed to set statement timeout: {}", _worker_id, PQerrorMessage(slot.conn));
        }
        PQclear(res);
        
        slot.socket_fd = PQsocket(slot.conn);

        // Initialize libuv poll handle for this connection's socket
        if (_loop_initialized && slot.socket_fd >= 0) {
            if (uv_poll_init(_loop, &slot.poll_handle, slot.socket_fd) == 0) {
                slot.poll_handle.data = &slot;
                slot.poll_initialized = true;
            } else {
                spdlog::warn("[Worker {}] [libqueen] Failed to initialize uv_poll for slot", _worker_id);
                slot.poll_initialized = false;
            }
        }
        
        return true;
    }

    void
    _handle_slot_error(DBConnection& slot, const std::string& error_msg) noexcept(false) {
        spdlog::error("[Worker {}] [libqueen] Slot error: {}", _worker_id, error_msg);
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
        spdlog::info("[Worker {}] [libqueen] Reconnection thread started", _worker_id);
        
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
                
                spdlog::info("[Worker {}] [libqueen] Reconnecting dead slot {}", _worker_id, i);
                
                // Reconnect (blocking - OK in this thread)
                if (_reconnect_slot_db(slot)) {
                    // Signal event loop to initialize poll handle
                    slot.needs_poll_init.store(true, std::memory_order_release);
                    uv_async_send(&_reconnect_signal);
                    spdlog::info("[Worker {}] [libqueen] Slot {} reconnected, signaling event loop", _worker_id, i);
                } else {
                    spdlog::warn("[Worker {}] [libqueen] Failed to reconnect slot {}", _worker_id, i);
                }
                
                // Only try ONE slot per cycle to avoid overwhelming
                break;
            }
        }
        
        spdlog::info("[Worker {}] [libqueen] Reconnection thread stopped", _worker_id);
    }
    
    // Reconnect slot - DB connection only (called from reconnect thread)
    // Does NOT touch libuv handles (not thread-safe)
    bool
    _reconnect_slot_db(DBConnection& slot) noexcept(true) {
        slot.conn = PQconnectdb(_conn_str.c_str());
        
        if (PQstatus(slot.conn) != CONNECTION_OK) {
            spdlog::error("[Worker {}] [libqueen] Reconnection failed: {}", _worker_id, PQerrorMessage(slot.conn));
            PQfinish(slot.conn);
            slot.conn = nullptr;
            return false;
        }
        
        // Set non-blocking mode
        if (PQsetnonblocking(slot.conn, 1) != 0) {
            spdlog::error("[Worker {}] [libqueen] Failed to set non-blocking on reconnect: {}", _worker_id, PQerrorMessage(slot.conn));
            PQfinish(slot.conn);
            slot.conn = nullptr;
            return false;
        }
        
        // Set statement timeout
        std::string timeout_sql = "SET statement_timeout = " + std::to_string(_statement_timeout_ms);
        PGresult* res = PQexec(slot.conn, timeout_sql.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::warn("[Worker {}] [libqueen] Failed to set statement timeout on reconnect: {}", _worker_id, PQerrorMessage(slot.conn));
        }
        PQclear(res);
        
        // Store socket fd (will be used by event loop for poll init)
        slot.socket_fd = PQsocket(slot.conn);
        
        return true;
    }    
};

}

#endif /* __cplusplus */
#endif /* _QUEEN_HPP_ */
