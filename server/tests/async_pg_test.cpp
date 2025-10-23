/**
 * Test: Callback-based async connection pool with PostgreSQL + uWebSockets
 * 
 * This demonstrates the PROPER way to handle async database operations:
 * - Non-blocking connection acquisition via callbacks
 * - Direct handoff when connections return
 * - Zero polling overhead
 * - True event-driven architecture
 */

#include <libpq-fe.h>
#include <App.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <string>
#include <chrono>
#include <queue>
#include <mutex>
#include <memory>
#include <functional>

// Platform-specific event constants
#ifdef __linux__
    #include <sys/epoll.h>
    #define LIBUS_SOCKET_READABLE EPOLLIN
#else
    #define LIBUS_SOCKET_READABLE 1
#endif

// ============================================================================
// Simple Async Connection Pool with Callback Support
// ============================================================================

class AsyncConnectionPool {
public:
    using ConnectionCallback = std::function<void(PGconn*)>;
    
    AsyncConnectionPool(const std::string& conn_str, size_t pool_size)
        : conn_str_(conn_str)
        , pool_size_(pool_size)
    {
        spdlog::info("📦 Creating pool with {} connections", pool_size);
        
        // Create initial connections
        for (size_t i = 0; i < pool_size; ++i) {
            PGconn* conn = PQconnectdb(conn_str.c_str());
            if (PQstatus(conn) == CONNECTION_OK) {
                available_.push(conn);
                spdlog::debug("  Connection {}/{} created", i+1, pool_size);
            } else {
                spdlog::error("  Failed to create connection {}", i+1);
                PQfinish(conn);
            }
        }
        
        spdlog::info("✅ Pool created with {}/{} connections", available_.size(), pool_size);
    }
    
    ~AsyncConnectionPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!available_.empty()) {
            PQfinish(available_.front());
            available_.pop();
        }
    }
    
    // Try to get connection immediately (non-blocking)
    PGconn* try_get() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (available_.empty()) {
            return nullptr;
        }
        
        PGconn* conn = available_.front();
        available_.pop();
        return conn;
    }
    
    // Get connection via callback (non-blocking, event-driven!)
    void get_async(ConnectionCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        spdlog::debug("📞 get_async called, available={}, waiting={}", 
                     available_.size(), waiting_.size());
        
        if (!available_.empty()) {
            // Connection available - call immediately
            PGconn* conn = available_.front();
            available_.pop();
            
            spdlog::info("✅ Connection available immediately, calling callback");
            callback(conn);
        } else {
            // No connection - queue the callback
            waiting_.push(callback);
            spdlog::warn("⏳ No connection available, queued callback ({} waiting)", waiting_.size());
        }
    }
    
    // Return connection to pool
    void return_conn(PGconn* conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        spdlog::debug("🔙 Returning connection, waiting={}", waiting_.size());
        
        // Check if anyone is waiting
        if (!waiting_.empty()) {
            auto callback = std::move(waiting_.front());
            waiting_.pop();
            
            spdlog::info("🎁 Giving connection directly to waiting callback ({} still waiting)", 
                        waiting_.size());
            callback(conn);  // Direct handoff! ✨
        } else {
            // No one waiting, return to pool
            available_.push(conn);
            spdlog::debug("  Returned to pool, available={}", available_.size());
        }
    }
    
    size_t available_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return available_.size();
    }
    
    size_t waiting_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return waiting_.size();
    }
    
private:
    std::string conn_str_;
    size_t pool_size_;
    std::queue<PGconn*> available_;
    std::queue<ConnectionCallback> waiting_;
    std::mutex mutex_;
};

// ============================================================================
// Async PostgreSQL Operation (using timers for result polling)
// ============================================================================

struct AsyncPGOperation {
    PGconn* conn;
    AsyncConnectionPool* pool;
    us_timer_t* timer;
    uWS::Loop* loop;
    std::function<void(bool, std::string)> completion_callback;
    std::chrono::steady_clock::time_point start_time;
    bool completed;
    int check_count;
    
    AsyncPGOperation(PGconn* conn, AsyncConnectionPool* pool, uWS::Loop* loop,
                     std::function<void(bool, std::string)> cb)
        : conn(conn)
        , pool(pool)
        , timer(nullptr)
        , loop(loop)
        , completion_callback(cb)
        , start_time(std::chrono::steady_clock::now())
        , completed(false)
        , check_count(0)
    {
    }
};

// Timer callback - checks if PostgreSQL result is ready
static void check_result_timer(us_timer_t* timer) {
    AsyncPGOperation* op = *(AsyncPGOperation**)us_timer_ext(timer);
    
    if (!op || op->completed) {
        if (timer) us_timer_close(timer);
        return;
    }
    
    op->check_count++;
    
    // Consume input from PostgreSQL
    if (PQconsumeInput(op->conn) == 0) {
        spdlog::error("❌ PQconsumeInput failed: {}", PQerrorMessage(op->conn));
        op->completed = true;
        op->completion_callback(false, PQerrorMessage(op->conn));
        op->pool->return_conn(op->conn);
        us_timer_close(timer);
        delete op;
        return;
    }
    
    // Check if still busy
    if (PQisBusy(op->conn)) {
        // Still waiting, timer will fire again
        return;
    }
    
    // Result is ready!
    PGresult* result = PQgetResult(op->conn);
    
    if (!result) {
        // No result yet
        return;
    }
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - op->start_time).count();
    
    ExecStatusType status = PQresultStatus(result);
    
    if (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK) {
        int rows = PQntuples(result);
        std::string result_msg = std::string(PQresStatus(status)) + ", " + std::to_string(rows) + " rows";
        
        spdlog::info("✅ Query completed: {} ({}ms, {} checks)", result_msg, duration, op->check_count);
        
        // Show first row if available
        if (rows > 0 && PQnfields(result) > 0) {
            spdlog::info("   First result: {}", PQgetvalue(result, 0, 0));
        }
        
        op->completion_callback(true, result_msg);
    } else {
        spdlog::error("❌ Query failed: {}", PQresultErrorMessage(result));
        op->completion_callback(false, PQresultErrorMessage(result));
    }
    
    PQclear(result);
    
    // Clear remaining results
    while ((result = PQgetResult(op->conn)) != nullptr) {
        PQclear(result);
    }
    
    // Return connection to pool (might give it to waiting request!)
    spdlog::debug("🔄 Operation complete, returning connection to pool");
    op->pool->return_conn(op->conn);
    
    // Cleanup
    op->completed = true;
    us_timer_close(timer);
    delete op;
}

// Start an async query
void start_async_query(AsyncConnectionPool* pool, const std::string& query, 
                      uWS::Loop* loop, std::function<void(bool, std::string)> callback) {
    
    spdlog::info("🚀 Starting async query (non-blocking)...");
    
    // Get connection asynchronously via callback
    pool->get_async([query, pool, loop, callback](PGconn* conn) {
        spdlog::info("🔗 Got connection from pool, sending query");
        
        // Set to non-blocking mode
        if (PQsetnonblocking(conn, 1) != 0) {
            spdlog::error("❌ Failed to set non-blocking");
            callback(false, "Failed to set non-blocking");
            pool->return_conn(conn);
            return;
        }
        
        // Send query (non-blocking)
        if (PQsendQuery(conn, query.c_str()) == 0) {
            spdlog::error("❌ PQsendQuery failed: {}", PQerrorMessage(conn));
            callback(false, PQerrorMessage(conn));
            PQsetnonblocking(conn, 0);
            pool->return_conn(conn);
            return;
        }
        
        spdlog::debug("📤 Query sent, flushing...");
        PQflush(conn);
        
        // Create operation to poll for results
        auto* op = new AsyncPGOperation(conn, pool, loop, callback);
        
        // Create timer to check for results every 1ms
        op->timer = us_create_timer((struct us_loop_t*)loop, 0, sizeof(AsyncPGOperation*));
        *(AsyncPGOperation**)us_timer_ext(op->timer) = op;
        us_timer_set(op->timer, check_result_timer, 1, 1);  // Check every 1ms
        
        spdlog::debug("⏱️  Timer started to poll for results");
    });
}

// ============================================================================
// Test Program
// ============================================================================

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("");
    spdlog::info("🧪 ============================================");
    spdlog::info("🧪 Async Connection Pool Test (Callback-Based)");
    spdlog::info("🧪 ============================================");
    spdlog::info("");
    
    // Build connection string
    std::string host = std::getenv("PG_HOST") ? std::getenv("PG_HOST") : "localhost";
    std::string port = std::getenv("PG_PORT") ? std::getenv("PG_PORT") : "5432";
    std::string db = std::getenv("PG_DB") ? std::getenv("PG_DB") : "postgres";
    std::string user = std::getenv("PG_USER") ? std::getenv("PG_USER") : "postgres";
    std::string password = std::getenv("PG_PASSWORD") ? std::getenv("PG_PASSWORD") : "postgres";
    
    std::string conn_str = "host=" + host + " port=" + port + " dbname=" + db + 
                          " user=" + user + " password=" + password + 
                          " sslmode=disable connect_timeout=2";
    
    spdlog::info("📡 Connection: {}@{}:{}/{}", user, host, port, db);
    
    // Create pool with only 2 connections
    AsyncConnectionPool pool(conn_str, 2);
    
    if (pool.available_count() == 0) {
        spdlog::error("❌ Failed to create any connections");
        return 1;
    }
    
    // Create uWebSockets app
    auto app = uWS::App();
    uWS::Loop* loop = uWS::Loop::get();
    
    spdlog::info("✅ uWebSockets event loop ready");
    spdlog::info("");
    
    // Track completions
    int completed = 0;
    int total_requests = 500;  // Try 5 requests with only 2 connections!
    
    auto completion_callback = [&completed, total_requests, loop](bool success, std::string msg) {
        completed++;
        
        if (success) {
            spdlog::info("✅ Request {}/{} completed: {}", completed, total_requests, msg);
        } else {
            spdlog::error("❌ Request {}/{} failed: {}", completed, total_requests, msg);
        }
        
        // Stop event loop when all requests complete
        if (completed == total_requests) {
            spdlog::info("");
            spdlog::info("🏁 All {} requests completed!", total_requests);
            spdlog::info("🏁 Stopping event loop...");
            // Note: Can't actually stop uWS loop easily, so we just exit
            std::exit(0);
        }
    };
    
    spdlog::info("🎬 Starting {} concurrent async queries (pool has only {} connections)...", 
                 total_requests, pool.available_count());
    spdlog::info("");
    
    // Send 5 concurrent requests (but we only have 2 connections!)
    for (int i = 0; i < total_requests; ++i) {
        std::string query = "SELECT '" + std::to_string(i) + "' as request_num, "
                           "pg_sleep(0.1), NOW()";  // 100ms sleep to simulate work
        
        spdlog::info("📨 Request {} - Queuing async query", i);
        
        start_async_query(&pool, query, loop, completion_callback);
    }
    
    spdlog::info("");
    spdlog::info("📊 Pool state: available={}, waiting={}", 
                 pool.available_count(), pool.waiting_count());
    spdlog::info("");
    spdlog::info("🔄 Running event loop (requests will complete as connections become available)...");
    spdlog::info("");
    
    // Run event loop
    app.run();
    
    return 0;
}
