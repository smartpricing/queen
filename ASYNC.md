# Async Database Implementation Plan

## Executive Summary

After thorough analysis of the Queen message queue system, this document outlines a comprehensive plan to implement asynchronous PostgreSQL operations using a custom Boost.Asio integration inspired by the postgres++ library approach.

## Current System Analysis

### Database Operations Inventory

**Critical Blocking Operations Identified:**

1. **Queue Manager Operations (queue_manager.cpp)**
   - `push_messages()` - 8-step pipeline with transactions
   - `pop_from_queue_partition()` - Complex lease management with FOR UPDATE SKIP LOCKED
   - `acknowledge_message()` - Multi-step transaction with DLQ handling
   - `acknowledge_messages()` - Batch ACK with complex transaction logic
   - `acquire_partition_lease()` - Complex UPSERT operations
   - `delete_queue()` - Long-running cascading deletes
   - `configure_queue()` - UPSERT with many parameters
   - `initialize_schema()` - Schema creation operations

2. **Analytics Manager Operations (analyticsManager.cpp)**
   - `get_metrics()` - Complex aggregation queries
   - `get_queues()` - Multi-table joins
   - `get_queue()` - Detailed queue statistics
   - `list_messages()` - Paginated message listing
   - `get_message()` - Single message lookup
   - `get_status()` - Dashboard statistics
   - All analytics operations with complex WHERE clauses and JOINs

3. **File Buffer Operations (file_buffer.cpp)**
   - `flush_batched_to_db()` - Batch database writes
   - `flush_single_to_db()` - Single message writes
   - Recovery operations during startup

4. **HTTP Routes (acceptor_server.cpp)**
   - `/api/v1/push` - Message publishing
   - `/api/v1/pop` - Message consumption  
   - `/api/v1/ack` - Message acknowledgment
   - `/api/v1/transaction` - Atomic operations
   - All analytics/dashboard routes
   - Health check operations

### Identified Issues & Risks

#### **CRITICAL ISSUES:**

1. **Complex Transaction Dependencies**
   - Push operations require 8 sequential database calls
   - Pop operations involve lease acquisition, message selection, and cursor updates
   - ACK operations handle DLQ, analytics, and lease management
   - **Risk**: Async conversion complexity is extremely high

2. **FOR UPDATE SKIP LOCKED Usage**
   - Used in `pop_from_queue_partition()` for row-level locking
   - **Risk**: Async conversion may break locking semantics
   - **Impact**: Message duplication or loss possible

3. **Lease Management Complexity**
   - `acquire_partition_lease()` uses complex UPSERT logic
   - Lease expiration and renewal logic is tightly coupled
   - **Risk**: Race conditions in async environment

4. **File Buffer Integration**
   - FileBufferManager calls database operations synchronously
   - Recovery operations are blocking and complex
   - **Risk**: Startup recovery may take hours with async conversion

5. **Connection Pool Architecture**
   - Current pool uses blocking `get_connection()` with timeouts
   - Thread-local pools per worker
   - **Risk**: Async conversion requires complete pool redesign

6. **Transaction Rollback Complexity**
   - Multiple operations use explicit BEGIN/COMMIT/ROLLBACK
   - Error handling relies on synchronous rollback
   - **Risk**: Async rollback error handling is complex

#### **ARCHITECTURAL ISSUES:**

1. **uWebSockets Response Object Lifetime**
   - Response objects cannot be safely shared across threads
   - Current sync operations complete within request context
   - **Risk**: Response object may be destroyed before async callback

2. **Error Propagation**
   - Current error handling uses exceptions and immediate returns
   - Async operations require callback-based error handling
   - **Risk**: Complex error state management

3. **Analytics Query Complexity**
   - Dashboard queries involve complex JOINs and aggregations
   - Some queries may take 100ms+ on large datasets
   - **Risk**: Async conversion may not provide significant benefit

## Implementation Strategy

### Phase 1: Foundation (Weeks 1-2)

#### **1.1 Custom Async Database Layer**

Create a custom async database implementation based on postgres++ approach:

```cpp
// include/queen/async_database.hpp
namespace queen {

class AsyncConnection {
private:
    PGconn* conn_;
    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::socket socket_;
    std::atomic<bool> transaction_active_{false};
    
public:
    AsyncConnection(boost::asio::io_context& ioc, const std::string& conn_str);
    
    template<typename Handler>
    void async_query(const std::string& sql, 
                    const std::vector<std::string>& params,
                    Handler&& handler);
    
    template<typename Handler>
    void async_transaction(std::function<void(AsyncTransaction&)> operations,
                          Handler&& completion_handler);
};

class AsyncTransaction {
private:
    AsyncConnection& conn_;
    std::vector<std::function<void()>> operations_;
    
public:
    template<typename Handler>
    void async_exec(const std::string& sql,
                   const std::vector<std::string>& params,
                   Handler&& handler);
    
    template<typename Handler>
    void commit(Handler&& handler);
    
    template<typename Handler>
    void rollback(Handler&& handler);
};

}
```

#### **1.2 Response Safety Layer**

Implement safe response handling for uWebSockets:

```cpp
// include/queen/response_keeper.hpp
class ResponseKeeper {
private:
    uWS::HttpResponse<false>* response_;
    std::atomic<bool> valid_;
    std::mutex mutex_;
    
public:
    ResponseKeeper(uWS::HttpResponse<false>* res);
    bool is_valid() const;
    void send_json(const nlohmann::json& data);
    void send_error(const std::string& error, int status = 500);
};
```

#### **1.3 Async Pool Manager**

Replace current DatabasePool with async-capable version:

```cpp
// include/queen/async_pool.hpp
class AsyncConnectionPool {
private:
    boost::asio::io_context& io_context_;
    std::queue<std::unique_ptr<AsyncConnection>> available_;
    std::queue<std::function<void(std::unique_ptr<AsyncConnection>)>> waiting_;
    std::mutex mutex_;
    
public:
    template<typename Handler>
    void get_connection_async(Handler&& handler);
    
    void return_connection(std::unique_ptr<AsyncConnection> conn);
};
```

### Phase 2: Core Operations (Weeks 3-5)

#### **2.1 Async Push Pipeline**

Convert the 8-step push operation to async:

```cpp
class AsyncPushPipeline {
private:
    std::shared_ptr<AsyncConnection> conn_;
    std::vector<PushItem> items_;
    std::function<void(std::vector<PushResult>)> completion_callback_;
    
    // Pipeline steps
    void step1_check_queue();
    void step2_check_size_limits();
    void step3_check_encryption();
    void step4_ensure_partition();
    void step5_get_partition_id();
    void step6_begin_transaction();
    void step7_insert_messages();
    void step8_commit_transaction();
    
public:
    void execute(std::shared_ptr<AsyncConnection> conn,
                const std::vector<PushItem>& items,
                std::function<void(std::vector<PushResult>)> callback);
};
```

#### **2.2 Async Pop Operations**

Handle complex lease management asynchronously:

```cpp
class AsyncPopOperation {
private:
    std::shared_ptr<AsyncConnection> conn_;
    std::string queue_name_;
    std::string partition_name_;
    std::string consumer_group_;
    PopOptions options_;
    
    void step1_check_window_buffer();
    void step2_get_queue_config();
    void step3_acquire_lease();
    void step4_select_messages();
    void step5_update_cursors();
    
public:
    void execute(std::shared_ptr<AsyncConnection> conn,
                const std::string& queue_name,
                const std::string& partition_name,
                const std::string& consumer_group,
                const PopOptions& options,
                std::function<void(PopResult)> callback);
};
```

#### **2.3 Async ACK Operations**

Convert complex ACK logic with DLQ handling:

```cpp
class AsyncAckOperation {
private:
    void handle_single_ack();
    void handle_batch_ack();
    void move_to_dlq();
    void update_analytics();
    void release_lease();
    
public:
    void execute_single(/* params */, std::function<void(AckResult)> callback);
    void execute_batch(/* params */, std::function<void(std::vector<AckResult>)> callback);
};
```

### Phase 3: Integration (Weeks 6-7)

#### **3.1 Worker Thread Integration**

Integrate async operations with uWebSockets workers:

```cpp
class AsyncWorker {
private:
    uWS::App* uws_app_;
    boost::asio::io_context asio_context_;
    std::unique_ptr<AsyncConnectionPool> db_pool_;
    std::thread asio_thread_;
    
public:
    AsyncWorker(int worker_id, const DatabaseConfig& db_config);
    void setup_routes();
    void start();
    void stop();
};
```

#### **3.2 Route Conversion**

Convert HTTP routes to use async operations:

```cpp
// Example: Async push route
app->post("/api/v1/push", [this](auto* res, auto* req) {
    auto response_keeper = std::make_shared<ResponseKeeper>(res);
    
    read_json_body(res, [this, response_keeper](const nlohmann::json& body) {
        auto items = parse_push_items(body);
        
        // Execute async push pipeline
        auto pipeline = std::make_shared<AsyncPushPipeline>();
        
        db_pool_->get_connection_async([pipeline, items, response_keeper](auto conn) {
            pipeline->execute(conn, items, [response_keeper](auto results) {
                if (response_keeper->is_valid()) {
                    response_keeper->send_json(results);
                }
            });
        });
    });
});
```

### Phase 4: File Buffer Integration (Week 8)

#### **4.1 Async File Buffer Operations**

Convert FileBufferManager to use async database operations:

```cpp
class AsyncFileBufferManager {
private:
    std::shared_ptr<AsyncConnectionPool> db_pool_;
    
    void async_flush_batched_to_db(const std::vector<nlohmann::json>& events,
                                  std::function<void(bool)> callback);
    
    void async_recovery_operations();
    
public:
    void start_async_processing();
};
```

### Phase 5: Analytics Integration (Week 9)

#### **5.1 Async Analytics Operations**

Convert analytics queries to async (lower priority):

```cpp
class AsyncAnalyticsManager {
private:
    std::shared_ptr<AsyncConnectionPool> db_pool_;
    
public:
    void async_get_metrics(std::function<void(nlohmann::json)> callback);
    void async_get_queues(std::function<void(nlohmann::json)> callback);
    // ... other analytics operations
};
```

### Phase 6: Testing & Optimization (Week 10)

#### **6.1 Load Testing**

- Compare sync vs async performance
- Measure latency improvements
- Test error handling scenarios
- Validate transaction integrity

#### **6.2 Rollback Strategy**

- Feature flag to switch between sync/async
- Gradual rollout per route
- Performance monitoring

## Risk Mitigation

### **HIGH RISK ITEMS:**

1. **Transaction Integrity**
   - **Mitigation**: Extensive testing of async transaction rollback
   - **Validation**: Compare database state between sync/async implementations

2. **FOR UPDATE SKIP LOCKED Semantics**
   - **Mitigation**: Careful testing of concurrent pop operations
   - **Validation**: Load test with multiple consumers

3. **Response Object Lifetime**
   - **Mitigation**: ResponseKeeper pattern with proper cleanup
   - **Validation**: Stress test with connection drops

4. **File Buffer Recovery**
   - **Mitigation**: Maintain sync recovery as fallback
   - **Validation**: Test recovery with large buffer files

### **MEDIUM RISK ITEMS:**

1. **Connection Pool Exhaustion**
   - **Mitigation**: Proper connection lifecycle management
   - **Monitoring**: Pool utilization metrics

2. **Memory Leaks in Async Operations**
   - **Mitigation**: RAII patterns and shared_ptr usage
   - **Validation**: Long-running memory tests

## Success Metrics

### **Performance Targets:**

- **Throughput**: 5,000-15,000 req/s (vs current ~300 req/s)
- **Latency**: <1ms for cached operations
- **Connection Efficiency**: 90%+ pool utilization
- **Memory**: No increase in baseline memory usage

### **Reliability Targets:**

- **Zero message loss** during async conversion
- **Zero message duplication** in concurrent scenarios
- **100% transaction integrity** maintained
- **<1 second** failover time to file buffer

## Implementation Timeline

| Week | Phase | Deliverables |
|------|-------|-------------|
| 1-2  | Foundation | AsyncConnection, ResponseKeeper, AsyncPool |
| 3-4  | Push Pipeline | Async push operations with full testing |
| 5    | Pop Pipeline | Async pop with lease management |
| 6    | ACK Pipeline | Async ACK with DLQ handling |
| 7    | Integration | Worker integration and route conversion |
| 8    | File Buffer | Async file buffer operations |
| 9    | Analytics | Async analytics (optional) |
| 10   | Testing | Load testing and optimization |

## Conclusion

This async implementation is **technically feasible** but **extremely complex** due to:

1. **Complex transaction pipelines** (8 steps for push)
2. **Advanced PostgreSQL features** (FOR UPDATE SKIP LOCKED)
3. **Tight integration** with file buffer and lease management
4. **uWebSockets response lifetime** management

**Recommendation**: Proceed with **Phase 1-2 only** as proof of concept. The complexity and risk may not justify the performance gains for all operations.

**Alternative**: Focus on **scaling the current sync approach** with more workers and optimized queries, which may provide 80% of the benefit with 20% of the risk.

## Appendix: Current System Bottlenecks

### **Database Operations by Frequency:**

1. **High Frequency** (>1000/sec)
   - `push_messages()` - 8 DB calls per request
   - `pop_from_queue_partition()` - 4-6 DB calls per request
   - `acknowledge_message()` - 3-5 DB calls per request

2. **Medium Frequency** (100-1000/sec)
   - Analytics queries for dashboard
   - Health checks
   - File buffer flushes

3. **Low Frequency** (<100/sec)
   - Queue configuration
   - Schema operations
   - Administrative operations

### **Async Conversion Priority:**

1. **Priority 1**: Push/Pop/ACK operations (core message flow)
2. **Priority 2**: File buffer operations (failover critical)
3. **Priority 3**: Analytics operations (dashboard performance)
4. **Priority 4**: Administrative operations (low impact)

---

*This document represents a comprehensive analysis of async database implementation for the Queen message queue system. Implementation should proceed incrementally with extensive testing at each phase.*
