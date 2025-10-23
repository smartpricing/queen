#pragma once

#include "queen/database.hpp"
#include <App.h>
#include <libpq-fe.h>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <chrono>
#include <json.hpp>

namespace queen {

// Forward declarations
struct PushItem;
struct PushResult;
class QueueManager;

/**
 * Async Push Operation State Machine
 * 
 * Handles the multi-step async process of pushing messages:
 * 1. Check if queue exists
 * 2. Check queue size limits
 * 3. Ensure partition exists
 * 4. Get partition ID
 * 5. Begin transaction
 * 6. Batch insert messages
 * 7. Commit transaction
 * 8. Return results via callback
 */
class AsyncPushOperation {
public:
    enum class State {
        CHECKING_QUEUE,
        CHECKING_SIZE,
        CHECKING_ENCRYPTION,
        ENSURING_PARTITION,
        GETTING_PARTITION_ID,
        BEGINNING_TRANSACTION,
        INSERTING_MESSAGES,
        COMMITTING_TRANSACTION,
        DONE,
        ERROR
    };
    
    using Callback = std::function<void(std::vector<PushResult>)>;
    
    AsyncPushOperation(
        std::unique_ptr<DatabaseConnection> conn,
        DatabasePool* pool,
        const std::vector<PushItem>& items,
        const std::string& queue_name,
        const std::string& partition_name,
        Callback callback,
        uWS::Loop* loop
    );
    
    ~AsyncPushOperation();
    
    // Start the async operation
    void start();
    
    // Check if socket is ready (called by timer)
    void check_socket_ready();
    
    // Make completed_ accessible to timer callback
    bool completed_;
    
private:
    // State machine
    void transition_to_next_state();
    void handle_query_result(PGresult* result);
    void fail_operation(const std::string& error);
    void complete_operation();
    
    // Query senders for each state
    void send_check_queue();
    void send_check_size();
    void send_check_encryption();
    void send_ensure_partition();
    void send_get_partition_id();
    void send_begin_transaction();
    void send_insert_messages();
    void send_commit_transaction();
    
    // Helper methods
    std::string generate_uuid();
    std::string build_pg_array(const std::vector<std::string>& vec);
    std::string build_pg_array_with_nulls(const std::vector<std::string>& vec);
    std::string build_bool_array(const std::vector<bool>& vec);
    
    // Connection and pool
    std::unique_ptr<DatabaseConnection> conn_;
    DatabasePool* pool_;
    PGconn* pg_conn_;  // Raw libpq connection
    
    // Operation data
    std::vector<PushItem> items_;
    std::string queue_name_;
    std::string partition_name_;
    Callback callback_;
    std::vector<PushResult> results_;
    
    // State tracking
    State state_;
    std::string partition_id_;
    bool encryption_enabled_;
    
    // Event loop integration
    uWS::Loop* loop_;
    us_timer_t* timer_;
};

/**
 * Async Database Manager
 * 
 * Provides async database operations integrated with uWebSockets event loop
 */
class AsyncDatabaseManager {
public:
    explicit AsyncDatabaseManager(DatabasePool* pool);
    
    // Async push messages
    void push_messages_async(
        const std::vector<PushItem>& items,
        const std::string& queue_name,
        const std::string& partition_name,
        uWS::Loop* loop,
        AsyncPushOperation::Callback callback
    );
    
private:
    DatabasePool* pool_;
};

} // namespace queen

