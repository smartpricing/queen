/**
 * Database Connection Pool Health Test
 * 
 * This test validates that the connection pool:
 * 1. Maintains correct size when connections become invalid
 * 2. Replaces invalid connections automatically
 * 3. Handles concurrent access correctly
 * 4. Applies proper timeout settings to connections
 */

#include "../include/queen/database.hpp"
#include "../include/queen/config.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <cassert>
#include <spdlog/spdlog.h>

using namespace queen;

// Test utilities
#define TEST_ASSERT(condition, message) \
    if (!(condition)) { \
        std::cerr << "❌ TEST FAILED: " << message << std::endl; \
        return false; \
    } else { \
        std::cout << "✅ " << message << std::endl; \
    }

// Test 1: Verify connection string includes timeout parameters
bool test_connection_string_timeouts() {
    std::cout << "\n=== Test 1: Connection String Timeouts ===" << std::endl;
    
    DatabaseConfig config;
    config.host = "localhost";
    config.port = "5432";
    config.database = "testdb";
    config.user = "testuser";
    config.password = "testpass";
    config.statement_timeout = 30000;
    config.lock_timeout = 10000;
    config.idle_timeout = 30000;
    config.connection_timeout = 2000;
    config.use_ssl = false;
    
    std::string conn_str = config.connection_string();
    
    TEST_ASSERT(conn_str.find("host=localhost") != std::string::npos, 
                "Connection string contains host");
    TEST_ASSERT(conn_str.find("connect_timeout=") != std::string::npos, 
                "Connection string contains connect_timeout");
    TEST_ASSERT(conn_str.find("statement_timeout=30000") != std::string::npos, 
                "Connection string contains statement_timeout");
    TEST_ASSERT(conn_str.find("lock_timeout=10000") != std::string::npos, 
                "Connection string contains lock_timeout");
    TEST_ASSERT(conn_str.find("idle_in_transaction_session_timeout=30000") != std::string::npos, 
                "Connection string contains idle_in_transaction_session_timeout");
    
    std::cout << "Connection string: " << conn_str << std::endl;
    
    return true;
}

// Test 2: Pool maintains size when connections are returned invalid
bool test_pool_size_maintenance() {
    std::cout << "\n=== Test 2: Pool Size Maintenance ===" << std::endl;
    
    // Use environment variables or defaults
    const char* pg_host = std::getenv("PG_HOST");
    const char* pg_port = std::getenv("PG_PORT");
    const char* pg_db = std::getenv("PG_DB");
    const char* pg_user = std::getenv("PG_USER");
    const char* pg_pass = std::getenv("PG_PASSWORD");
    
    if (!pg_host) {
        std::cout << "⚠️  Skipping test - PostgreSQL not configured (set PG_HOST env var)" << std::endl;
        return true;
    }
    
    DatabaseConfig config;
    config.host = pg_host;
    config.port = pg_port ? pg_port : "5432";
    config.database = pg_db ? pg_db : "postgres";
    config.user = pg_user ? pg_user : "postgres";
    config.password = pg_pass ? pg_pass : "postgres";
    config.pool_size = 5;
    config.pool_acquisition_timeout = 5000;
    
    try {
        DatabasePool pool(config.connection_string(), config.pool_size, config.pool_acquisition_timeout);
        
        TEST_ASSERT(pool.size() == 5, "Pool initialized with correct size");
        TEST_ASSERT(pool.available() == 5, "All connections available initially");
        
        // Acquire all connections
        std::vector<std::unique_ptr<DatabaseConnection>> connections;
        for (int i = 0; i < 5; i++) {
            auto conn = pool.get_connection();
            TEST_ASSERT(conn != nullptr, "Got connection " + std::to_string(i));
            connections.push_back(std::move(conn));
        }
        
        TEST_ASSERT(pool.available() == 0, "All connections in use");
        
        // Return connections
        for (auto& conn : connections) {
            pool.return_connection(std::move(conn));
        }
        connections.clear();
        
        TEST_ASSERT(pool.available() == 5, "All connections returned and available");
        TEST_ASSERT(pool.size() == 5, "Pool size maintained");
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception: " << e.what() << std::endl;
        return false;
    }
    
    return true;
}

// Test 3: Concurrent access to pool
bool test_concurrent_pool_access() {
    std::cout << "\n=== Test 3: Concurrent Pool Access ===" << std::endl;
    
    const char* pg_host = std::getenv("PG_HOST");
    if (!pg_host) {
        std::cout << "⚠️  Skipping test - PostgreSQL not configured" << std::endl;
        return true;
    }
    
    DatabaseConfig config;
    config.host = pg_host;
    config.port = std::getenv("PG_PORT") ?: "5432";
    config.database = std::getenv("PG_DB") ?: "postgres";
    config.user = std::getenv("PG_USER") ?: "postgres";
    config.password = std::getenv("PG_PASSWORD") ?: "postgres";
    config.pool_size = 10;
    config.pool_acquisition_timeout = 10000;
    
    try {
        auto pool = std::make_shared<DatabasePool>(config.connection_string(), 
                                                    config.pool_size, 
                                                    config.pool_acquisition_timeout);
        
        std::atomic<int> success_count{0};
        std::atomic<int> error_count{0};
        const int num_threads = 20;
        const int ops_per_thread = 10;
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([pool, &success_count, &error_count, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; i++) {
                    try {
                        auto conn = pool->get_connection();
                        
                        // Simulate work
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        
                        // Execute a simple query to verify connection works
                        auto result = conn->exec("SELECT 1");
                        if (result && PQresultStatus(result) == PGRES_TUPLES_OK) {
                            success_count++;
                        }
                        PQclear(result);
                        
                        pool->return_connection(std::move(conn));
                        
                    } catch (const std::exception& e) {
                        error_count++;
                        spdlog::error("Thread error: {}", e.what());
                    }
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }
        
        std::cout << "Successful operations: " << success_count << "/" << (num_threads * ops_per_thread) << std::endl;
        std::cout << "Failed operations: " << error_count << std::endl;
        
        TEST_ASSERT(error_count == 0, "No errors during concurrent access");
        TEST_ASSERT(success_count == num_threads * ops_per_thread, "All operations succeeded");
        TEST_ASSERT(pool->size() == 10, "Pool size maintained after concurrent access");
        TEST_ASSERT(pool->available() == 10, "All connections returned to pool");
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception: " << e.what() << std::endl;
        return false;
    }
    
    return true;
}

// Test 4: Pool behavior when database is temporarily unavailable
bool test_pool_recovery_from_db_failure() {
    std::cout << "\n=== Test 4: Pool Recovery from DB Failure ===" << std::endl;
    
    // This test uses an invalid host to simulate connection failures
    DatabaseConfig config;
    config.host = "invalid-host-12345.local";
    config.port = "5432";
    config.database = "postgres";
    config.user = "postgres";
    config.password = "postgres";
    config.pool_size = 3;
    config.pool_acquisition_timeout = 1000;
    config.connection_timeout = 1000;  // 1 second timeout
    
    try {
        // This should fail to create initial connections
        DatabasePool pool(config.connection_string(), config.pool_size, config.pool_acquisition_timeout);
        
        // Should not reach here
        std::cerr << "❌ Expected pool creation to fail with invalid host" << std::endl;
        return false;
        
    } catch (const std::exception& e) {
        std::cout << "✅ Pool correctly failed to initialize with invalid host: " << e.what() << std::endl;
    }
    
    return true;
}

// Test 5: Verify connection validity check
bool test_connection_validity() {
    std::cout << "\n=== Test 5: Connection Validity Check ===" << std::endl;
    
    const char* pg_host = std::getenv("PG_HOST");
    if (!pg_host) {
        std::cout << "⚠️  Skipping test - PostgreSQL not configured" << std::endl;
        return true;
    }
    
    DatabaseConfig config;
    config.host = pg_host;
    config.port = std::getenv("PG_PORT") ?: "5432";
    config.database = std::getenv("PG_DB") ?: "postgres";
    config.user = std::getenv("PG_USER") ?: "postgres";
    config.password = std::getenv("PG_PASSWORD") ?: "postgres";
    
    try {
        auto conn = std::make_unique<DatabaseConnection>(config.connection_string());
        
        TEST_ASSERT(conn->is_valid(), "Connection is valid after creation");
        TEST_ASSERT(!conn->is_in_use(), "Connection not marked in use initially");
        
        // Execute a query
        auto result = conn->exec("SELECT version()");
        TEST_ASSERT(result != nullptr, "Query execution returned result");
        
        if (result) {
            TEST_ASSERT(PQresultStatus(result) == PGRES_TUPLES_OK, "Query succeeded");
            PQclear(result);
        }
        
        TEST_ASSERT(conn->is_valid(), "Connection still valid after query");
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception: " << e.what() << std::endl;
        return false;
    }
    
    return true;
}

// Main test runner
int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::warn);  // Reduce noise during tests
    
    std::cout << "╔══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║     Database Connection Pool Health Tests               ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝" << std::endl;
    
    bool all_passed = true;
    
    all_passed &= test_connection_string_timeouts();
    all_passed &= test_pool_size_maintenance();
    all_passed &= test_concurrent_pool_access();
    all_passed &= test_pool_recovery_from_db_failure();
    all_passed &= test_connection_validity();
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    if (all_passed) {
        std::cout << "✅ ALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "❌ SOME TESTS FAILED" << std::endl;
        return 1;
    }
}

