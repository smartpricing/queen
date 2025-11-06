#pragma once

#include <libpq-fe.h>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

namespace queen {

// --- RAII Deleters for libpq ---

struct PGResultDeleter {
    void operator()(PGresult* res) const {
        if (res) PQclear(res);
    }
};
using PGResultPtr = std::unique_ptr<PGresult, PGResultDeleter>;

struct PGConnDeleter {
    void operator()(PGconn* conn) const {
        if (conn) PQfinish(conn);
    }
};
using PGConnPtr = std::unique_ptr<PGconn, PGConnDeleter>;

// --- Helper Functions ---

/**
 * @brief Waits for socket to be ready for reading or writing using select().
 * 
 * @param conn Active PostgreSQL connection
 * @param for_reading True to wait for read, false to wait for write
 * @throws std::runtime_error if socket is invalid or select() fails
 */
void waitForSocket(PGconn* conn, bool for_reading);

/**
 * @brief Asynchronously establishes a PostgreSQL connection.
 * 
 * Uses PQconnectStart/PQconnectPoll for non-blocking connection establishment.
 * Sets connection to non-blocking mode upon success.
 * 
 * @param conn_str PostgreSQL connection string
 * @param statement_timeout_ms Statement timeout in milliseconds
 * @param lock_timeout_ms Lock timeout in milliseconds
 * @param idle_in_transaction_timeout_ms Idle in transaction timeout in milliseconds
 * @param schema Database schema to use (sets search_path)
 * @return PGConnPtr Smart pointer to established connection
 * @throws std::runtime_error if connection fails
 */
PGConnPtr asyncConnect(const char* conn_str,
                      int statement_timeout_ms = 30000,
                      int lock_timeout_ms = 10000,
                      int idle_in_transaction_timeout_ms = 30000,
                      const std::string& schema = "queen");

/**
 * @brief Asynchronously resets a PostgreSQL connection and re-applies parameters.
 * 
 * Uses PQresetStart/PQresetPoll for non-blocking connection reset.
 * After successful reset, re-applies all connection parameters including
 * timeouts and schema settings.
 * 
 * @param conn Existing connection to reset (must not be nullptr)
 * @param statement_timeout_ms Statement timeout in milliseconds
 * @param lock_timeout_ms Lock timeout in milliseconds
 * @param idle_in_transaction_timeout_ms Idle in transaction timeout in milliseconds
 * @param schema Database schema to use (sets search_path)
 * @return true if reset successful, false otherwise
 */
bool asyncReset(PGconn* conn,
               int statement_timeout_ms,
               int lock_timeout_ms,
               int idle_in_transaction_timeout_ms,
               const std::string& schema);

/**
 * @brief Sends a query asynchronously and waits for completion.
 * 
 * Uses PQsendQuery + socket polling to avoid blocking the thread.
 * 
 * @param conn Non-blocking PostgreSQL connection
 * @param query SQL query string
 * @throws std::runtime_error if query send or processing fails
 */
void sendAndWait(PGconn* conn, const char* query);

/**
 * @brief Sends a parameterized query asynchronously and waits for completion.
 * 
 * Uses PQsendQueryParams + socket polling to avoid blocking the thread.
 * 
 * @param conn Non-blocking PostgreSQL connection
 * @param sql SQL query string with $1, $2, etc. placeholders
 * @param params Vector of parameter values
 * @throws std::runtime_error if query send or processing fails
 */
void sendQueryParamsAsync(PGconn* conn, const std::string& sql, const std::vector<std::string>& params);

/**
 * @brief Retrieves and validates a command result (COMMAND_OK).
 * 
 * @param conn PostgreSQL connection
 * @throws std::runtime_error if result is invalid or not COMMAND_OK
 */
void getCommandResult(PGconn* conn);

/**
 * @brief Retrieves and returns a command result for getting affected rows.
 * 
 * @param conn PostgreSQL connection
 * @return PGResultPtr Smart pointer to result (for getting PQcmdTuples)
 * @throws std::runtime_error if result is invalid or not COMMAND_OK
 */
PGResultPtr getCommandResultPtr(PGconn* conn);

/**
 * @brief Retrieves and validates a tuple result (TUPLES_OK).
 * 
 * @param conn PostgreSQL connection
 * @return PGResultPtr Smart pointer to result
 * @throws std::runtime_error if result is invalid or not TUPLES_OK
 */
PGResultPtr getTuplesResult(PGconn* conn);

// --- Async Database Connection Pool ---

/**
 * @brief Thread-safe, asynchronous connection pool for libpq.
 * 
 * Pre-allocates a fixed number of non-blocking PostgreSQL connections.
 * Connections are acquired with RAII wrappers that automatically return
 * them to the pool when destroyed.
 * 
 * This pool is designed for use in multi-threaded servers like uWebSockets,
 * where each worker thread can acquire a connection, perform async operations,
 * and release it back to the pool.
 */
class AsyncDbPool {
public:
    /**
     * @brief Creates the pool and initializes all connections asynchronously.
     * 
     * @param conn_str PostgreSQL connection string
     * @param pool_size Number of connections to pre-create
     * @param statement_timeout_ms Statement timeout in milliseconds
     * @param lock_timeout_ms Lock timeout in milliseconds
     * @param idle_in_transaction_timeout_ms Idle in transaction timeout in milliseconds
     * @param schema Database schema to use (sets search_path)
     * @throws std::invalid_argument if pool_size <= 0
     * @throws std::runtime_error if connection initialization fails
     */
    AsyncDbPool(std::string conn_str, 
               int pool_size,
               int statement_timeout_ms = 30000,
               int lock_timeout_ms = 10000,
               int idle_in_transaction_timeout_ms = 30000,
               const std::string& schema = "queen");

    /**
     * @brief Destructor. Cleans up all connections.
     */
    ~AsyncDbPool();

    // Non-copyable, non-movable
    AsyncDbPool(const AsyncDbPool&) = delete;
    AsyncDbPool& operator=(const AsyncDbPool&) = delete;
    AsyncDbPool(AsyncDbPool&&) = delete;
    AsyncDbPool& operator=(AsyncDbPool&&) = delete;

    /**
     * @brief RAII wrapper for pooled connections.
     * 
     * Automatically returns the connection to the pool when destroyed.
     */
    using PooledConnection = std::unique_ptr<PGconn, std::function<void(PGconn*)>>;

    /**
     * @brief Acquires a connection from the pool.
     * 
     * If no connections are available, this function will block the
     * calling thread until one is released.
     * 
     * @return PooledConnection RAII wrapper that returns connection on destruction
     */
    PooledConnection acquire();

    /**
     * @brief Gets current pool size.
     * 
     * @return Number of connections in the pool
     */
    size_t size() const { return all_connections_.size(); }

    /**
     * @brief Gets number of available (idle) connections.
     * 
     * @return Number of connections currently available
     */
    size_t available() const;

    /**
     * @brief Resets all idle connections in the pool.
     * 
     * Useful for recovering from database restarts. Only resets
     * connections that are currently idle (not in use).
     * 
     * @return Number of connections successfully reset
     */
    size_t resetAllIdle();

private:
    /**
     * @brief Returns a connection to the pool.
     * 
     * Called automatically by PooledConnection deleter.
     * 
     * @param conn Connection to return
     */
    void release(PGconn* conn);

    /**
     * @brief Checks if a connection is healthy and resets if needed.
     * 
     * @param conn Connection to check
     * @return true if connection is healthy or was successfully reset
     */
    bool ensureConnectionHealthy(PGconn* conn);

    std::string conn_str_;
    int statement_timeout_ms_;
    int lock_timeout_ms_;
    int idle_in_transaction_timeout_ms_;
    std::string schema_;

    // This vector owns all connections. When the pool is destroyed,
    // these unique_ptrs will call PQfinish on all connections.
    std::vector<PGConnPtr> all_connections_;

    // This queue holds raw pointers to the *idle* connections.
    std::queue<PGconn*> idle_connections_;
    
    // Concurrency primitives
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};

} // namespace queen

