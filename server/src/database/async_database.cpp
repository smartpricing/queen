#include "queen/async_database.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cerrno>
#include <cstring>

// For select() system call (POSIX)
#include <sys/select.h>

namespace queen {

// --- Helper: Socket Polling ---

void waitForSocket(PGconn* conn, bool for_reading) {
    int sock_fd = PQsocket(conn);
    if (sock_fd < 0) {
        throw std::runtime_error("PQsocket returned invalid file descriptor.");
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock_fd, &fds);

    int ret;
    if (for_reading) {
        ret = select(sock_fd + 1, &fds, nullptr, nullptr, nullptr);
    } else {
        ret = select(sock_fd + 1, nullptr, &fds, nullptr, nullptr);
    }

    if (ret < 0) {
        throw std::runtime_error("select() failed: " + std::string(strerror(errno)));
    }
}

// --- Helper: Asynchronous Connection ---

PGConnPtr asyncConnect(const char* conn_str,
                      int statement_timeout_ms,
                      int lock_timeout_ms,
                      int idle_in_transaction_timeout_ms,
                      const std::string& schema) {
    PGconn* conn = PQconnectStart(conn_str);
    if (!conn) {
        throw std::runtime_error("PQconnectStart failed to allocate connection.");
    }
    
    if (PQstatus(conn) == CONNECTION_BAD) {
        std::string errMsg = PQerrorMessage(conn);
        PQfinish(conn);
        throw std::runtime_error("PQconnectStart failed: " + errMsg);
    }

    PostgresPollingStatusType poll_status;
    do {
        poll_status = PQconnectPoll(conn);
        switch (poll_status) {
            case PGRES_POLLING_READING: 
                waitForSocket(conn, true); 
                break;
            case PGRES_POLLING_WRITING: 
                waitForSocket(conn, false); 
                break;
            case PGRES_POLLING_FAILED:
                {
                    std::string errMsg = PQerrorMessage(conn);
                    PQfinish(conn);
                    throw std::runtime_error("Async connection failed: " + errMsg);
                }
            case PGRES_POLLING_OK: 
                break;
        }
    } while (poll_status != PGRES_POLLING_OK);
    
    // Set connection to non-blocking mode
    if (PQsetnonblocking(conn, 1) != 0) {
        std::string errMsg = PQerrorMessage(conn);
        PQfinish(conn);
        throw std::runtime_error("Failed to set non-blocking mode: " + errMsg);
    }

    // Set client encoding to UTF8
    PQsetClientEncoding(conn, "UTF8");

    // Set timeout parameters and search_path
    std::string set_params = 
        "SET statement_timeout = " + std::to_string(statement_timeout_ms) + "; " +
        "SET lock_timeout = " + std::to_string(lock_timeout_ms) + "; " +
        "SET idle_in_transaction_session_timeout = " + std::to_string(idle_in_transaction_timeout_ms) + "; " +
        "SET search_path TO " + schema + ", public;";
    
    // Send the configuration commands
    if (!PQsendQuery(conn, set_params.c_str())) {
        std::string errMsg = PQerrorMessage(conn);
        PQfinish(conn);
        throw std::runtime_error("Failed to send connection parameters: " + errMsg);
    }

    // Wait for the configuration to complete
    while (true) {
        waitForSocket(conn, true);
        if (!PQconsumeInput(conn)) {
            std::string errMsg = PQerrorMessage(conn);
            PQfinish(conn);
            throw std::runtime_error("PQconsumeInput failed during setup: " + errMsg);
        }
        if (PQisBusy(conn) == 0) {
            break;
        }
    }

    // Consume all results from SET commands
    PGresult* result;
    while ((result = PQgetResult(conn)) != nullptr) {
        if (PQresultStatus(result) != PGRES_COMMAND_OK) {
            std::string errMsg = PQresultErrorMessage(result);
            PQclear(result);
            PQfinish(conn);
            throw std::runtime_error("Failed to set connection parameters: " + errMsg);
        }
        PQclear(result);
    }

    return PGConnPtr(conn);
}

// --- Helper: Send Query and Wait ---

void sendAndWait(PGconn* conn, const char* query) {
    if (!PQsendQuery(conn, query)) {
        throw std::runtime_error("PQsendQuery failed: " + 
                                 std::string(PQerrorMessage(conn)));
    }
    
    while (true) {
        waitForSocket(conn, true);
        if (!PQconsumeInput(conn)) {
            throw std::runtime_error("PQconsumeInput failed: " + 
                                     std::string(PQerrorMessage(conn)));
        }
        if (PQisBusy(conn) == 0) {
            break;
        }
    }
}

// --- Helper: Get Command Result ---

void getCommandResult(PGconn* conn) {
    PGResultPtr res(PQgetResult(conn));
    if (!res) {
        throw std::runtime_error("Server returned no result.");
    }
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        throw std::runtime_error("Query command failed: " + 
                                 std::string(PQresultErrorMessage(res.get())));
    }
    
    // Verify no extra results
    PGResultPtr null_check(PQgetResult(conn));
    if (null_check) {
        throw std::runtime_error("Unexpected extra result after command.");
    }
}

// --- Helper: Get Tuples Result ---

PGResultPtr getTuplesResult(PGconn* conn) {
    PGresult* result = PQgetResult(conn);
    if (!result) {
        throw std::runtime_error("Server returned no result for SELECT.");
    }
    
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        std::string errMsg = PQresultErrorMessage(result);
        PQclear(result);
        throw std::runtime_error("SELECT query failed: " + errMsg);
    }
    
    // Verify no extra results
    PGResultPtr null_check(PQgetResult(conn));
    if (null_check) {
        PQclear(result);
        throw std::runtime_error("Unexpected extra result after SELECT.");
    }
    
    return PGResultPtr(result);
}

// --- AsyncDbPool Implementation ---

AsyncDbPool::AsyncDbPool(std::string conn_str, 
                       int pool_size,
                       int statement_timeout_ms,
                       int lock_timeout_ms,
                       int idle_in_transaction_timeout_ms,
                       const std::string& schema)
    : conn_str_(std::move(conn_str)),
      statement_timeout_ms_(statement_timeout_ms),
      lock_timeout_ms_(lock_timeout_ms),
      idle_in_transaction_timeout_ms_(idle_in_transaction_timeout_ms),
      schema_(schema) {
    
    if (pool_size <= 0) {
        throw std::invalid_argument("Pool size must be greater than 0");
    }

    spdlog::info("[AsyncDbPool] Initializing {} async connections...", pool_size);
    
    all_connections_.reserve(pool_size);
    
    for (int i = 0; i < pool_size; ++i) {
        try {
            auto conn = asyncConnect(conn_str_.c_str(), 
                                    statement_timeout_ms_,
                                    lock_timeout_ms_,
                                    idle_in_transaction_timeout_ms_,
                                    schema_);
            PGconn* raw_conn_ptr = conn.get();
            all_connections_.push_back(std::move(conn));
            idle_connections_.push(raw_conn_ptr);
            
            spdlog::debug("[AsyncDbPool] Connection {}/{} initialized", i + 1, pool_size);
        } catch (const std::exception& e) {
            spdlog::error("[AsyncDbPool] Failed to create connection {}/{}: {}", 
                         i + 1, pool_size, e.what());
            throw;
        }
    }
    
    spdlog::info("[AsyncDbPool] Initialization complete. {} connections ready.", 
                all_connections_.size());
}

AsyncDbPool::~AsyncDbPool() {
    spdlog::info("[AsyncDbPool] Shutting down pool with {} connections...", 
                all_connections_.size());
    
    // Wait for all connections to be returned
    {
        std::unique_lock<std::mutex> lock(mtx_);
        // Give a grace period for connections to be returned
        if (idle_connections_.size() != all_connections_.size()) {
            spdlog::warn("[AsyncDbPool] Waiting for {} connections to be returned...",
                        all_connections_.size() - idle_connections_.size());
            
            // Wait up to 5 seconds for connections to return
            cv_.wait_for(lock, std::chrono::seconds(5), [this] {
                return idle_connections_.size() == all_connections_.size();
            });
            
            if (idle_connections_.size() != all_connections_.size()) {
                spdlog::error("[AsyncDbPool] {} connections still in use during shutdown!",
                            all_connections_.size() - idle_connections_.size());
            }
        }
    }
    
    // all_connections_ vector will automatically delete and PQfinish
    // all the PGConnPtrs it owns.
    spdlog::info("[AsyncDbPool] Shutdown complete.");
}

AsyncDbPool::PooledConnection AsyncDbPool::acquire() {
    std::unique_lock<std::mutex> lock(mtx_);

    // Wait until a connection is available
    cv_.wait(lock, [this] { return !idle_connections_.empty(); });

    // We have the lock, and the queue is not empty
    PGconn* conn = idle_connections_.front();
    idle_connections_.pop();

    spdlog::debug("[AsyncDbPool] Connection acquired ({} remaining)", 
                 idle_connections_.size());

    // Return a smart pointer with a custom deleter
    return PooledConnection(conn, [this](PGconn* returned_conn) {
        this->release(returned_conn);
    });
}

void AsyncDbPool::release(PGconn* conn) {
    if (!conn) return;
    
    // CRITICAL: Drain any pending results to prevent "another command is already in progress" errors
    // This can happen if a connection is released after an exception, before all results were consumed
    PGresult* drain_result;
    int drained_count = 0;
    while ((drain_result = PQgetResult(conn)) != nullptr) {
        PQclear(drain_result);
        drained_count++;
    }
    if (drained_count > 0) {
        spdlog::debug("[AsyncDbPool] Drained {} pending results from connection before release", drained_count);
    }
    
    // Check if connection is still valid
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::warn("[AsyncDbPool] Connection invalid on release, attempting reset...");
        PQreset(conn);
        
        if (PQstatus(conn) == CONNECTION_OK) {
            // Re-enable non-blocking mode after reset
            if (PQsetnonblocking(conn, 1) != 0) {
                spdlog::error("[AsyncDbPool] Failed to set non-blocking after reset");
            } else {
                spdlog::info("[AsyncDbPool] Connection reset successfully");
            }
        } else {
            spdlog::error("[AsyncDbPool] Connection reset failed: {}", 
                         PQerrorMessage(conn));
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(mtx_);
        idle_connections_.push(conn);
        spdlog::debug("[AsyncDbPool] Connection released ({} available)", 
                     idle_connections_.size());
    }
    
    // Notify one waiting thread that a connection is available
    cv_.notify_one();
}

size_t AsyncDbPool::available() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return idle_connections_.size();
}

} // namespace queen

