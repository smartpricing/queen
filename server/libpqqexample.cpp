#include <iostream>
#include <string>
#include <stdexcept>
#include <memory>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono> // For demo sleep

// C API for PostgreSQL
#include <libpq-fe.h>

// For select() system call (POSIX)
#include <sys/select.h>
#include <cerrno>
#include <cstring>

/*
============================================================================
 DATABASE SETUP (Same as before)
============================================================================
 CREATE TABLE IF NOT EXISTS users (id SERIAL PRIMARY KEY, name TEXT, email TEXT);
 CREATE TABLE IF NOT EXISTS user_logs (log_id SERIAL PRIMARY KEY, user_id INT, action TEXT);
 TRUNCATE users, user_logs RESTART IDENTITY;
 INSERT INTO users (name, email) VALUES ('Alice', 'alice@example.com');
============================================================================
*/

// --- Helper: RAII Deleters for libpq ---

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

// --- Helper: Asynchronous Connection (Used by Pool) ---

PGConnPtr asyncConnect(const char* conn_str) {
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
            case PGRES_POLLING_READING: waitForSocket(conn, true); break;
            case PGRES_POLLING_WRITING: waitForSocket(conn, false); break;
            case PGRES_POLLING_FAILED:
                throw std::runtime_error("Async connection failed: " + 
                                         std::string(PQerrorMessage(conn)));
            case PGRES_POLLING_OK: break;
        }
    } while (poll_status != PGRES_POLLING_OK);
    
    if (PQsetnonblocking(conn, 1) != 0) {
         throw std::runtime_error("Failed to set non-blocking mode.");
    }

    return PGConnPtr(conn);
}


/**
 * @brief A thread-safe, blocking connection pool for libpq.
 */
class AsyncDbPool {
public:
    /**
     * @brief Creates the pool and initializes connections.
     * @param conn_str The connection string.
     * @param pool_size The number of connections to create.
     */
    AsyncDbPool(std::string conn_str, int pool_size)
        : conn_str_(std::move(conn_str)) {
        
        if (pool_size <= 0) {
            throw std::invalid_argument("Pool size must be greater than 0");
        }

        std::cout << "[Pool] Initializing " << pool_size << " connections..." << std::endl;
        all_connections_.reserve(pool_size);
        for (int i = 0; i < pool_size; ++i) {
            auto conn = asyncConnect(conn_str_.c_str());
            PGconn* raw_conn_ptr = conn.get();
            all_connections_.push_back(std::move(conn));
            idle_connections_.push(raw_conn_ptr);
        }
        std::cout << "[Pool] Initialization complete." << std::endl;
    }

    /**
     * @brief Destructor. Cleans up all connections.
     */
    ~AsyncDbPool() {
        std::cout << "[Pool] Shutting down..." << std::endl;
        // all_connections_ vector will automatically delete and PQfinish
        // all the PGConnPtrs it owns.
    }

    // Non-copyable
    AsyncDbPool(const AsyncDbPool&) = delete;
    AsyncDbPool& operator=(const AsyncDbPool&) = delete;

    // Define a type for our RAII connection wrapper
    using PooledConnection = std::unique_ptr<PGconn, std::function<void(PGconn*)>>;

    /**
     * @brief Acquires a connection from the pool.
     *
     * If no connections are available, this function will block the
     * calling thread until one is released.
     *
     * @return A unique_ptr that returns the connection to the pool on destruction.
     */
    PooledConnection acquire() {
        std::unique_lock<std::mutex> lock(mtx_);

        // Wait until the queue is no longer empty
        cv_.wait(lock, [this] { return !idle_connections_.empty(); });

        // We have the lock, and the queue is not empty
        PGconn* conn = idle_connections_.front();
        idle_connections_.pop();

        // Return a smart pointer with a custom deleter
        return PooledConnection(conn, [this](PGconn* returned_conn) {
            this->release(returned_conn);
        });
    }

private:
    /**
     * @brief Returns a connection to the pool.
     * @param conn The connection to return.
     */
    void release(PGconn* conn) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            idle_connections_.push(conn);
        }
        // Notify one waiting thread that a connection is available
        cv_.notify_one();
    }

    std::string conn_str_;

    // This vector owns all connections. When the pool is destroyed,
    // these unique_ptrs will call PQfinish on all connections.
    std::vector<PGConnPtr> all_connections_;

    // This queue holds raw pointers to the *idle* connections.
    std::queue<PGconn*> idle_connections_;
    
    // Concurrency primitives
    std::mutex mtx_;
    std::condition_variable cv_;
};


// --- Transaction Logic (Modified) ---
// These helpers are now standalone

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

void getCommandResult(PGconn* conn) {
    PGResultPtr res(PQgetResult(conn));
    if (!res) {
        throw std::runtime_error("Server returned no result.");
    }
    if (PQresultStatus(res.get()) != PGRES_COMMAND_OK) {
        throw std::runtime_error("Query command failed: " + 
                                 std::string(PQresultErrorMessage(res.get())));
    }
    PGResultPtr null_check(PQgetResult(conn));
    if (null_check) {
         throw std::runtime_error("Unexpected extra result after command.");
    }
}

/**
 * @brief Executes the transaction using an *existing* connection.
 *
 * This function no longer connects or disconnects.
 *
 * @param conn An active PGconn* from the pool.
 * @return A raw PGresult* from the final SELECT. The caller is
 * responsible for calling PQclear()!
 */
PGresult* execute_transaction_with_libpq(PGconn* conn) {
    PGresult* final_select_result = nullptr;
    bool transaction_failed = false;

    // Check connection status before starting
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "[Worker] Bad connection from pool, attempting reset..." << std::endl;
        PQreset(conn);
        if (PQstatus(conn) != CONNECTION_OK) {
            throw std::runtime_error("Connection is bad and reset failed.");
        }
        // Also re-enable non-blocking mode
        PQsetnonblocking(conn, 1);
        std::cerr << "[Worker] Connection reset successfully." << std::endl;
    }

    try {
        // --- 1. Run Transaction ---
        sendAndWait(conn, "BEGIN");
        getCommandResult(conn);

        sendAndWait(conn, "UPDATE users SET email = 'alice.new@example.com' WHERE id = 1");
        getCommandResult(conn);

        sendAndWait(conn, "INSERT INTO user_logs (user_id, action) VALUES (1, 'email updated again')");
        getCommandResult(conn);

        sendAndWait(conn, "SELECT id, name, email FROM users WHERE id = 1");
        
        final_select_result = PQgetResult(conn);
        if (!final_select_result) {
            throw std::runtime_error("Server returned no result for SELECT.");
        }
        if (PQresultStatus(final_select_result) != PGRES_TUPLES_OK) {
            std::string errMsg = PQresultErrorMessage(final_select_result);
            PQclear(final_select_result);
            throw std::runtime_error("SELECT query failed: " + errMsg);
        }
        
        PGResultPtr null_check(PQgetResult(conn));
        if (null_check) {
            PQclear(final_select_result); // Clean up the result we were holding
            throw std::runtime_error("Unexpected extra result after SELECT.");
        }

        sendAndWait(conn, "COMMIT");
        getCommandResult(conn);

    } catch (const std::exception& e) {
        std::cerr << "[Worker] *** TX ERROR: " << e.what() << std::endl;
        transaction_failed = true;

        if (final_select_result) {
            PQclear(final_select_result);
            final_select_result = nullptr;
        }
        
        // Attempt to ROLLBACK
        if (PQstatus(conn) == CONNECTION_OK) {
            std::cerr << "[Worker] Attempting to ROLLBACK..." << std::endl;
            PGResultPtr rollback_res(PQexec(conn, "ROLLBACK")); // Blocking exec is fine for error path
        }
        
        throw; // Re-throw the original exception
    }

    return final_select_result;
}

// --- Demo: Simulating uWebSockets Worker Threads ---

void worker_thread(int id, AsyncDbPool& pool) {
    try {
        std::cout << "[Worker " << id << "] Started." << std::endl;
        
        // Simulate doing 2 jobs
        for (int i = 0; i < 2; ++i) {
            std::cout << "[Worker " << id << "] Job " << i << ": Waiting to acquire connection..." << std::endl;

            // 1. Acquire connection from pool (blocks if pool is empty)
            auto conn = pool.acquire();
            
            std::cout << "[Worker " << id << "] Job " << i << ": Connection acquired. Executing TX..." << std::endl;

            // 2. Do the work
            PGResultPtr result(execute_transaction_with_libpq(conn.get()));

            // In a real app, you'd format this result and send it
            // std::cout << "[Worker " << id << "] Job " << i << ": TX complete." << std::endl;
            // printResult(result.get()); // (Helper not included for brevity)

            // 3. Connection is automatically released when 'conn' goes out of scope
            std::cout << "[Worker " << id << "] Job " << i << ": Releasing connection." << std::endl;

            // Simulate other work
            std::this_thread::sleep_for(std::chrono::milliseconds(50 + (rand() % 100)));
        }
        
        std::cout << "[Worker " << id << "] Finished." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[Worker " << id << "] DIED WITH EXCEPTION: " << e.what() << std::endl;
    }
}

int main() {
    // !! Set your connection string here !!
    const char* conn_str = "postgresql://postgres:postgres@localhost:5432/postgres";
    
    // Create a pool with 4 connections
    const int POOL_SIZE = 4;
    AsyncDbPool pool(conn_str, POOL_SIZE);

    // Simulate 10 concurrent requests (more than the pool size)
    const int NUM_WORKERS = 10;
    std::vector<std::thread> workers;

    std::cout << "\n--- Starting " << NUM_WORKERS << " worker threads ---" << std::endl;

    for (int i = 0; i < NUM_WORKERS; ++i) {
        workers.emplace_back(worker_thread, i, std::ref(pool));
    }

    // Wait for all workers to finish
    for (auto& t : workers) {
        t.join();
    }

    std::cout << "--- All workers finished ---" << std::endl;

    // Pool will be destroyed here, closing all connections
    return 0;
}