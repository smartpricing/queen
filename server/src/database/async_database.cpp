#include "queen/async_database.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>

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

    // Set timeout parameters
    std::string set_params = 
        "SET statement_timeout = " + std::to_string(statement_timeout_ms) + "; " +
        "SET lock_timeout = " + std::to_string(lock_timeout_ms) + "; " +
        "SET idle_in_transaction_session_timeout = " + std::to_string(idle_in_transaction_timeout_ms) + ";";
    
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

// --- Helper: Asynchronous Connection Reset ---

bool asyncReset(PGconn* conn,
               int statement_timeout_ms,
               int lock_timeout_ms,
               int idle_in_transaction_timeout_ms,
               const std::string& schema) {
    if (!conn) {
        spdlog::error("[asyncReset] Cannot reset null connection");
        return false;
    }

    // Start asynchronous reset
    if (PQresetStart(conn) == 0) {
        spdlog::error("[asyncReset] PQresetStart failed");
        return false;
    }

    // Poll until reset completes
    PostgresPollingStatusType poll_status;
    do {
        poll_status = PQresetPoll(conn);
        switch (poll_status) {
            case PGRES_POLLING_READING:
                try {
                    waitForSocket(conn, true);
                } catch (const std::exception& e) {
                    spdlog::error("[asyncReset] Socket wait (read) failed: {}", e.what());
                    return false;
                }
                break;
            case PGRES_POLLING_WRITING:
                try {
                    waitForSocket(conn, false);
                } catch (const std::exception& e) {
                    spdlog::error("[asyncReset] Socket wait (write) failed: {}", e.what());
                    return false;
                }
                break;
            case PGRES_POLLING_FAILED:
                spdlog::error("[asyncReset] Reset failed: {}", PQerrorMessage(conn));
                return false;
            case PGRES_POLLING_OK:
                break;
        }
    } while (poll_status != PGRES_POLLING_OK);

    // Verify connection is now valid
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[asyncReset] Connection still invalid after reset: {}", PQerrorMessage(conn));
        return false;
    }

    // Set connection to non-blocking mode
    if (PQsetnonblocking(conn, 1) != 0) {
        spdlog::error("[asyncReset] Failed to set non-blocking mode: {}", PQerrorMessage(conn));
        return false;
    }

    // Set client encoding to UTF8
    PQsetClientEncoding(conn, "UTF8");

    // Re-apply timeout parameters
    std::string set_params = 
        "SET statement_timeout = " + std::to_string(statement_timeout_ms) + "; " +
        "SET lock_timeout = " + std::to_string(lock_timeout_ms) + "; " +
        "SET idle_in_transaction_session_timeout = " + std::to_string(idle_in_transaction_timeout_ms) + ";";
    
    // Send the configuration commands
    if (!PQsendQuery(conn, set_params.c_str())) {
        spdlog::error("[asyncReset] Failed to send connection parameters: {}", PQerrorMessage(conn));
        return false;
    }

    // Wait for the configuration to complete
    try {
        while (true) {
            waitForSocket(conn, true);
            if (!PQconsumeInput(conn)) {
                spdlog::error("[asyncReset] PQconsumeInput failed during setup: {}", PQerrorMessage(conn));
                return false;
            }
            if (PQisBusy(conn) == 0) {
                break;
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("[asyncReset] Failed waiting for parameter setup: {}", e.what());
        return false;
    }

    // Consume all results from SET commands
    PGresult* result;
    while ((result = PQgetResult(conn)) != nullptr) {
        if (PQresultStatus(result) != PGRES_COMMAND_OK) {
            std::string errMsg = PQresultErrorMessage(result);
            PQclear(result);
            spdlog::error("[asyncReset] Failed to set connection parameters: {}", errMsg);
            return false;
        }
        PQclear(result);
    }

    spdlog::info("[asyncReset] Connection reset and reconfigured successfully");
    return true;
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

// --- Helper: Get Command Result Pointer (for PQcmdTuples) ---

PGResultPtr getCommandResultPtr(PGconn* conn) {
    PGresult* result = PQgetResult(conn);
    if (!result) {
        throw std::runtime_error("Server returned no result.");
    }
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::string errMsg = PQresultErrorMessage(result);
        PQclear(result);
        throw std::runtime_error("Query command failed: " + errMsg);
    }
    
    // Verify no extra results
    PGResultPtr null_check(PQgetResult(conn));
    if (null_check) {
        PQclear(result);
        throw std::runtime_error("Unexpected extra result after command.");
    }
    
    return PGResultPtr(result);
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

// --- Helper: Send Parameterized Query Async ---

void sendQueryParamsAsync(PGconn* conn, const std::string& sql, const std::vector<std::string>& params) {
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
    // Try to get a healthy connection, with a limit to prevent infinite loops
    // When DB is down, trying each connection once is enough (they'll all fail fast)
    const int MAX_HEALTH_CHECK_ATTEMPTS = static_cast<int>(all_connections_.size());
    int attempts = 0;
    
    while (attempts < MAX_HEALTH_CHECK_ATTEMPTS) {
        std::unique_lock<std::mutex> lock(mtx_);

        // Wait until a connection is available
        cv_.wait(lock, [this] { return !idle_connections_.empty(); });

        // We have the lock, and the queue is not empty
        PGconn* conn = idle_connections_.front();
        idle_connections_.pop();

        spdlog::debug("[AsyncDbPool] Connection acquired ({} remaining)", 
                     idle_connections_.size());

        // Release the lock before health check to avoid blocking other threads
        lock.unlock();

        // Proactive health check - catch stale connections before use
        if (ensureConnectionHealthy(conn)) {
            // Connection is healthy, return it
            return PooledConnection(conn, [this](PGconn* returned_conn) {
                this->release(returned_conn);
            });
        }
        
        // Connection is unhealthy, return to pool and try again
        spdlog::debug("[AsyncDbPool] Connection health check failed (attempt {}/{}), trying next connection", 
                     attempts + 1, MAX_HEALTH_CHECK_ATTEMPTS);
        release(conn);
        attempts++;
    }
    
    // All attempts exhausted - database is likely down
    spdlog::error("[AsyncDbPool] Failed to acquire healthy connection after {} attempts. Database may be unavailable.", 
                 MAX_HEALTH_CHECK_ATTEMPTS);
    throw std::runtime_error("Failed to acquire healthy database connection: all connections unhealthy. Database may be down.");
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
    
    // Check if connection is still valid and reset if needed
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::warn("[AsyncDbPool] Connection invalid on release, attempting async reset...");
        
        if (!asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                       idle_in_transaction_timeout_ms_, schema_)) {
            spdlog::error("[AsyncDbPool] Async connection reset failed, connection may be unusable");
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

bool AsyncDbPool::ensureConnectionHealthy(PGconn* conn) {
    if (!conn) {
        spdlog::error("[AsyncDbPool] Cannot check health of null connection");
        return false;
    }

    // Check connection status
    ConnStatusType status = PQstatus(conn);
    
    if (status != CONNECTION_OK) {
        // Connection is not OK, attempt reset immediately
        spdlog::warn("[AsyncDbPool] Connection status not OK ({}), attempting reset...", 
                    static_cast<int>(status));
        return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                        idle_in_transaction_timeout_ms_, schema_);
    }
    
    // Connection status is OK - do a quick ping to verify it's responsive
    // Send a simple ping query
    if (!PQsendQuery(conn, "SELECT 1")) {
        spdlog::warn("[AsyncDbPool] Connection failed health check (send), attempting reset...");
        return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                        idle_in_transaction_timeout_ms_, schema_);
    }

    // Wait for response with a timeout
    try {
        // Use select with a short timeout to check if the connection is responsive
        int sock_fd = PQsocket(conn);
        if (sock_fd < 0) {
            spdlog::warn("[AsyncDbPool] Invalid socket during health check, attempting reset...");
            // Drain the query we just sent
            PGresult* drain;
            while ((drain = PQgetResult(conn)) != nullptr) {
                PQclear(drain);
            }
            return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                            idle_in_transaction_timeout_ms_, schema_);
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock_fd, &fds);
        
        // 100ms timeout for health check (fail fast when DB is down)
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms
        
        int ret = select(sock_fd + 1, &fds, nullptr, nullptr, &tv);
            
            if (ret < 0) {
                spdlog::warn("[AsyncDbPool] Health check select failed, attempting reset...");
                // Drain the query
                PGresult* drain;
                while ((drain = PQgetResult(conn)) != nullptr) {
                    PQclear(drain);
                }
                return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                                idle_in_transaction_timeout_ms_, schema_);
            }
            
            if (ret == 0) {
                // Timeout - connection is unresponsive
                spdlog::warn("[AsyncDbPool] Health check timeout, attempting reset...");
                // Drain the query
                PGresult* drain;
                while ((drain = PQgetResult(conn)) != nullptr) {
                    PQclear(drain);
                }
                return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                                idle_in_transaction_timeout_ms_, schema_);
            }

            // Data is available, consume it
            if (!PQconsumeInput(conn)) {
                spdlog::warn("[AsyncDbPool] Failed to consume health check input, attempting reset...");
                // Drain the query
                PGresult* drain;
                while ((drain = PQgetResult(conn)) != nullptr) {
                    PQclear(drain);
                }
                return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                                idle_in_transaction_timeout_ms_, schema_);
            }

            // Check if query is complete
            while (PQisBusy(conn)) {
                waitForSocket(conn, true);
                if (!PQconsumeInput(conn)) {
                    spdlog::warn("[AsyncDbPool] Failed to consume input, attempting reset...");
                    return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                                    idle_in_transaction_timeout_ms_, schema_);
                }
            }

            // Get and verify result
            PGresult* result = PQgetResult(conn);
            if (!result) {
                spdlog::warn("[AsyncDbPool] No result from health check, attempting reset...");
                return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                                idle_in_transaction_timeout_ms_, schema_);
            }

            ExecStatusType res_status = PQresultStatus(result);
            PQclear(result);
            
            // Drain any remaining results
            PGresult* drain;
            while ((drain = PQgetResult(conn)) != nullptr) {
                PQclear(drain);
            }

            if (res_status != PGRES_TUPLES_OK) {
                spdlog::warn("[AsyncDbPool] Health check query failed, attempting reset...");
                return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                                idle_in_transaction_timeout_ms_, schema_);
            }

            // Connection is healthy
            spdlog::debug("[AsyncDbPool] Connection passed health check");
            return true;

        } catch (const std::exception& e) {
            spdlog::warn("[AsyncDbPool] Health check exception: {}, attempting reset...", e.what());
            // Drain any pending results
            PGresult* drain;
            while ((drain = PQgetResult(conn)) != nullptr) {
                PQclear(drain);
            }
            return asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                            idle_in_transaction_timeout_ms_, schema_);
        }
}

size_t AsyncDbPool::resetAllIdle() {
    std::vector<PGconn*> connections_to_reset;
    
    // Collect all idle connections
    {
        std::lock_guard<std::mutex> lock(mtx_);
        size_t idle_count = idle_connections_.size();
        
        spdlog::info("[AsyncDbPool] Resetting {} idle connections...", idle_count);
        
        // Move all idle connections to a temporary vector
        while (!idle_connections_.empty()) {
            connections_to_reset.push_back(idle_connections_.front());
            idle_connections_.pop();
        }
    }
    
    // Reset connections outside the lock to avoid blocking other operations
    size_t success_count = 0;
    for (PGconn* conn : connections_to_reset) {
        if (asyncReset(conn, statement_timeout_ms_, lock_timeout_ms_, 
                      idle_in_transaction_timeout_ms_, schema_)) {
            success_count++;
        } else {
            spdlog::error("[AsyncDbPool] Failed to reset connection during bulk reset");
        }
    }
    
    // Return all connections to the pool
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (PGconn* conn : connections_to_reset) {
            idle_connections_.push(conn);
        }
    }
    
    // Notify waiting threads
    cv_.notify_all();
    
    spdlog::info("[AsyncDbPool] Bulk reset complete: {}/{} connections reset successfully", 
                success_count, connections_to_reset.size());
    
    return success_count;
}

} // namespace queen

