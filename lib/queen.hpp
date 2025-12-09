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

namespace queen {

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
    {JobType::POP, "POP"},
    {JobType::ACK, "ACK"},
    {JobType::TRANSACTION, "TRANSACTION"},
    {JobType::RENEW_LEASE, "RENEW_LEASE"},
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
        
    // For PUSH: queues to notify after success (queue -> partition list)
    std::vector<std::pair<std::string, std::string>> push_targets;
        
    std::chrono::steady_clock::time_point wait_deadline;  // Absolute timeout
    std::chrono::steady_clock::time_point next_check;     // When to check next

    int batch_size = 1;           // Client's requested batch size
    std::string subscription_mode;
    std::string subscription_from;
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
    PGconn* conn;
    int socket_fd;
    uint16_t idx;
    uv_poll_t poll_handle;
    bool poll_initialized;
    Queen* pool;
    std::vector<PendingJob> jobs;
};

class Queen {
public:    
    Queen(const std::string& conn_str, 
        uint16_t statement_timeout_ms = 30000,
        uint16_t db_connection_count = 10,
        uint16_t queue_interval_ms = 10) noexcept(false) : 
        _loop(nullptr), 
        _loop_initialized(false),
        _queue_interval_ms(queue_interval_ms),
        _conn_str(conn_str),
        _statement_timeout_ms(statement_timeout_ms),
        _db_connection_count(db_connection_count) {
        // Initialize libuv mutex
        uv_mutex_init(&_mutex_job_queue);
        // Start the event loop thread
        _init();
    }

    ~Queen() {
        if (_loop_initialized) {
            uv_loop_close(_loop);
            delete _loop;
        }
        uv_mutex_destroy(&_mutex_job_queue);
    }

    void 
    submit(JobRequest&& job, std::function<void(std::string result)> cb) {
        uv_mutex_lock(&_mutex_job_queue);
        _job_queue.push_back({std::move(job), std::move(cb)});
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
    std::deque<PendingJob> _job_queue;

    // connection to the database 
    std::string _conn_str;
    uint16_t _statement_timeout_ms;
    uint16_t _db_connection_count;
    std::vector<uint16_t> _free_slot_indexes;
    std::vector<DBConnection> _db_connections;

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
            
            uv_timer_init(_loop, &_queue_timer);
            uv_timer_start(&_queue_timer, _uv_timer_cb, 0, _queue_interval_ms);
            uv_async_init(_loop, &_queue_signal, _uv_async_cb);

            // Connect all slots to the database
            if (!_connect_all_slots()) {
                throw std::runtime_error("Failed to connect all slots to the database");
            }

            uv_run(_loop, UV_RUN_DEFAULT);
        }).detach();
    }

    // Timer callback to process the job queue
    static void 
    _uv_timer_cb(uv_timer_t* handle) noexcept(false) {
        auto* self = static_cast<Queen*>(uv_handle_get_data((uv_handle_t*)handle));
        
        // Drain the queue
        std::vector<PendingJob> batch;
        uv_mutex_lock(&self->_mutex_job_queue);
        while (!self->_job_queue.empty()) {
            batch.push_back(std::move(self->_job_queue.front()));
            self->_job_queue.pop_front();
        }
        uv_mutex_unlock(&self->_mutex_job_queue);

        if (batch.empty()) {
            return;
        }
        /* Here we should to:
            - Group jobs by type  
            - Get a free slot for each type 
            - For pop request, understand if they are backoff or not
            - If we a have slot, send the request to the database 
            - Else, requeue the job to the queue 
        */
        auto grouped_jobs = self->_group_jobs_by_type(batch);
        std::vector<PendingJob> jobs_to_requeue;
        for (auto& [type, jobs] : grouped_jobs) {
            if (type == JobType::POP) {
                std::vector<PendingJob> ready_jobs;
                for (auto& job : jobs) {
                    if (job.job.next_check < std::chrono::steady_clock::now() &&
                        job.job.wait_deadline > std::chrono::steady_clock::now()) {
                        jobs_to_requeue.push_back(std::move(job));
                    } else if (job.job.next_check > std::chrono::steady_clock::now() &&
                        job.job.wait_deadline > std::chrono::steady_clock::now()) {
                        // TODO check shared state
                        ready_jobs.push_back(std::move(job));
                    } else {
                        jobs_to_requeue.push_back(std::move(job));
                    }
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
    _handle_slot_error(DBConnection& slot, const std::string& error_msg) noexcept(false) {
        spdlog::error("[libqueen] Slot error: {}", error_msg);
        _disconnect_slot(slot);
        _requeue_jobs(slot.jobs);
    }

    void
    _process_slot_result(DBConnection& slot) noexcept(false) {
        PGresult* res;
        while ((res = PQgetResult(slot.conn)) != NULL) {
            // Check status
            ExecStatusType status = PQresultStatus(res);
            if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) {
                // Success - process result
                std::cout << "Processing slot result" << std::endl;
                // TODO: parse and dispatch callbacks
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
    std::map<JobType, std::vector<PendingJob>>
    _group_jobs_by_type(const std::vector<PendingJob>& jobs) noexcept(true) {
        std::map<JobType, std::vector<PendingJob>> grouped_jobs;
        for (auto& job : jobs) {
            grouped_jobs[job.job.op_type].push_back(job);
        }
        return grouped_jobs;
    }

    void
    _send_jobs_to_slot(JobType type, const std::vector<PendingJob>& jobs, std::vector<PendingJob>& jobs_to_requeue) noexcept(false) {
        // Send all the batched jobs to the database if we have a free slot
        if (_has_free_slot()) {
            DBConnection& slot = _get_free_slot();
            // Join all the jobs params into a single jsonb

            nlohmann::json combined = nlohmann::json::array();
            for (const auto& pending : jobs) {
                if (!pending.job.params.empty()) {
                    auto arr = nlohmann::json::parse(pending.job.params[0]);
                    for (auto& item : arr) {
                        combined.push_back(std::move(item));
                    }
                }
            }
                    
            std::string merged_jsonb = combined.dump();
            const char* param_ptrs[] = { merged_jsonb.c_str() };
                    
            // Send the jobs to the database
            auto sql_string_for_type = JobTypeToSql.at(type);
            slot.jobs.insert(slot.jobs.end(), jobs.begin(), jobs.end());
            int sent = PQsendQueryParams(
                slot.conn, 
                sql_string_for_type.c_str(), 
                1,//static_cast<int>(param_ptrs.size()),
                nullptr, 
                param_ptrs,//param_ptrs.data(),
                nullptr, nullptr, 0);

            if (!sent) { 
                // TODO manage errors cases
                spdlog::error("[libqueen] Failed to send jobs to the database: {}", PQerrorMessage(slot.conn));
                jobs_to_requeue.insert(jobs_to_requeue.end(), jobs.begin(), jobs.end());
            }
            
            // Start watching for write (to flush) and read (for results)
            std::cout << slot.poll_initialized << " " << slot.socket_fd << std::endl;
            _start_watching_slot(slot, UV_WRITABLE | UV_READABLE);              
        } else {
            jobs_to_requeue.insert(jobs_to_requeue.end(), jobs.begin(), jobs.end());
        }
    }


    // TODO: check starvation
    void
    _requeue_jobs(const std::vector<PendingJob>& jobs) noexcept(true) {
        uv_mutex_lock(&_mutex_job_queue);
        for (auto& job : jobs) {
            _job_queue.push_front(std::move(job));
        }
        uv_mutex_unlock(&_mutex_job_queue);
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
        //slot.busy = false;
        //slot.pool = this;  // Back-reference for callbacks
        
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

    // Disconnect from the database
    void _disconnect_slot(DBConnection& slot) noexcept(true) {
        // Stop polling before disconnecting
        if (slot.poll_initialized) {
            uv_poll_stop(&slot.poll_handle);
            // Mark as needing reinitialization for reconnection
            slot.poll_initialized = false;
        }

        if (slot.conn) {
            PQfinish(slot.conn);
            slot.conn = nullptr;
            slot.socket_fd = -1;
            //slot.busy = false;
        }
    }
};

}

#endif /* __cplusplus */
#endif /* _QUEEN_HPP_ */
