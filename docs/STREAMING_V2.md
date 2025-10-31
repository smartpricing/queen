# Queen Streaming V2: Declarative Stream DSL

**Engineering Implementation Plan**

This document outlines the complete engineering plan for implementing a Kafka Streams-like declarative API on top of Queen's PostgreSQL foundation.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Database Schema](#database-schema)
3. [Server Implementation (C++)](#server-implementation-c)
4. [Client Implementation (JavaScript)](#client-implementation-javascript)
5. [Protocol Specification](#protocol-specification)
6. [Implementation Phases](#implementation-phases)
7. [Testing Strategy](#testing-strategy)
8. [Performance Considerations](#performance-considerations)

---

## Architecture Overview

### Core Concept

**Client** builds an execution plan (AST) → **Server** compiles to optimized SQL → Executes continuously with state tracking

```
┌─────────────────────────────────────────────────────────────┐
│                         CLIENT (JS)                          │
│                                                               │
│  Stream DSL API                                              │
│    ↓                                                         │
│  Execution Plan Builder                                      │
│    ↓                                                         │
│  JSON Serialization                                          │
└────────────────────────┬────────────────────────────────────┘
                         │ HTTP POST
                         ↓
┌─────────────────────────────────────────────────────────────┐
│                        SERVER (C++)                          │
│                                                               │
│  Plan Validator        → Security checks                     │
│    ↓                                                         │
│  SQL Compiler          → Generate optimized SQL              │
│    ↓                                                         │
│  Execution Engine      → Run with state tracking             │
│    ↓                                                         │
│  Result Streamer       → Send results back                   │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ↓
┌─────────────────────────────────────────────────────────────┐
│                      POSTGRESQL                              │
│                                                               │
│  queen.messages                                              │
│  queen.stream_state         (new)                           │
│  queen.stream_windows       (new)                           │
│  queen.stream_joins         (new)                           │
└─────────────────────────────────────────────────────────────┘
```

### Key Design Principles

1. **Immutable Operations**: Each DSL operation returns a new stream object
2. **Lazy Evaluation**: Build execution plan, execute on terminal operation
3. **Server-Side Execution**: All SQL runs server-side for security and performance
4. **Stateful Processing**: Consumer groups track stream processing state
5. **Type Safety**: TypeScript definitions for full type checking
6. **Composability**: Operations can be chained and combined

---

## Database Schema

### New Tables for Stream Processing

```sql
-- Stream execution registry
CREATE TABLE IF NOT EXISTS queen.stream_executions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    stream_name VARCHAR(255) UNIQUE NOT NULL,
    consumer_group VARCHAR(255) NOT NULL,
    execution_plan JSONB NOT NULL,
    compiled_sql TEXT NOT NULL,
    
    -- Status
    status VARCHAR(50) DEFAULT 'running',  -- running, paused, stopped, failed
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    last_executed_at TIMESTAMPTZ,
    
    -- Metadata
    created_by VARCHAR(255),
    error_message TEXT,
    execution_count BIGINT DEFAULT 0,
    
    UNIQUE(stream_name, consumer_group)
);

CREATE INDEX idx_stream_executions_status ON queen.stream_executions(status);
CREATE INDEX idx_stream_executions_consumer_group ON queen.stream_executions(consumer_group);

-- Stream state store (for stateful operations)
CREATE TABLE IF NOT EXISTS queen.stream_state (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    stream_execution_id UUID REFERENCES queen.stream_executions(id) ON DELETE CASCADE,
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    
    -- State key (e.g., userId for groupBy)
    state_key VARCHAR(255) NOT NULL,
    
    -- State value (flexible JSON)
    state_value JSONB NOT NULL,
    
    -- Timestamps
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    last_accessed_at TIMESTAMPTZ DEFAULT NOW(),
    ttl_expires_at TIMESTAMPTZ,  -- For state expiration
    
    UNIQUE(stream_execution_id, partition_id, state_key)
);

CREATE INDEX idx_stream_state_execution ON queen.stream_state(stream_execution_id);
CREATE INDEX idx_stream_state_key ON queen.stream_state(state_key);
CREATE INDEX idx_stream_state_ttl ON queen.stream_state(ttl_expires_at) WHERE ttl_expires_at IS NOT NULL;
CREATE INDEX idx_stream_state_value ON queen.stream_state USING GIN(state_value);

-- Window state tracking
CREATE TABLE IF NOT EXISTS queen.stream_windows (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    stream_execution_id UUID REFERENCES queen.stream_executions(id) ON DELETE CASCADE,
    partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
    
    window_key VARCHAR(255) NOT NULL,  -- Group key
    window_type VARCHAR(50) NOT NULL,  -- tumbling, sliding, session
    
    -- Window boundaries
    window_start TIMESTAMPTZ,
    window_end TIMESTAMPTZ,
    
    -- For count windows
    window_start_sequence BIGINT,
    window_end_sequence BIGINT,
    
    -- Window state
    status VARCHAR(50) DEFAULT 'open',  -- open, closing, closed, emitted
    message_count INTEGER DEFAULT 0,
    state_value JSONB,
    
    -- Timestamps
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    closed_at TIMESTAMPTZ,
    
    UNIQUE(stream_execution_id, partition_id, window_key, window_start)
);

CREATE INDEX idx_stream_windows_execution ON queen.stream_windows(stream_execution_id);
CREATE INDEX idx_stream_windows_status ON queen.stream_windows(status);
CREATE INDEX idx_stream_windows_boundaries ON queen.stream_windows(window_start, window_end);
CREATE INDEX idx_stream_windows_type ON queen.stream_windows(window_type);

-- Join state tracking (for stream-stream joins)
CREATE TABLE IF NOT EXISTS queen.stream_joins (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    stream_execution_id UUID REFERENCES queen.stream_executions(id) ON DELETE CASCADE,
    
    -- Join sides
    left_message_id UUID,
    right_message_id UUID,
    
    -- Join keys
    left_key VARCHAR(255),
    right_key VARCHAR(255),
    
    -- Timing
    left_timestamp TIMESTAMPTZ,
    right_timestamp TIMESTAMPTZ,
    joined_at TIMESTAMPTZ DEFAULT NOW(),
    
    -- Status
    status VARCHAR(50) DEFAULT 'pending',  -- pending, joined, expired
    
    UNIQUE(stream_execution_id, left_message_id, right_message_id)
);

CREATE INDEX idx_stream_joins_execution ON queen.stream_joins(stream_execution_id);
CREATE INDEX idx_stream_joins_left_key ON queen.stream_joins(left_key, status);
CREATE INDEX idx_stream_joins_right_key ON queen.stream_joins(right_key, status);
CREATE INDEX idx_stream_joins_timestamps ON queen.stream_joins(left_timestamp, right_timestamp);

-- Materialized view registry
CREATE TABLE IF NOT EXISTS queen.stream_materialized_views (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    view_name VARCHAR(255) UNIQUE NOT NULL,
    stream_execution_id UUID REFERENCES queen.stream_executions(id) ON DELETE CASCADE,
    
    -- Target table
    target_schema VARCHAR(255) DEFAULT 'queen',
    target_table VARCHAR(255) NOT NULL,
    
    -- Update mode
    update_mode VARCHAR(50) DEFAULT 'upsert',  -- upsert, append, replace
    key_columns TEXT[],  -- For upsert mode
    
    -- Status
    last_updated_at TIMESTAMPTZ,
    row_count BIGINT DEFAULT 0,
    
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_stream_materialized_views_execution ON queen.stream_materialized_views(stream_execution_id);
```

---

## Server Implementation (C++)

### File Structure

```
server/
├── include/queen/
│   ├── stream/
│   │   ├── execution_plan.hpp      # Execution plan data structures
│   │   ├── sql_compiler.hpp        # Plan → SQL compiler
│   │   ├── stream_executor.hpp     # Stream execution engine
│   │   ├── window_manager.hpp      # Window state management
│   │   ├── join_processor.hpp      # Join logic
│   │   ├── state_store.hpp         # State management
│   │   └── operators.hpp           # Operation implementations
│   └── stream_manager.hpp          # Main stream manager
├── src/
│   ├── stream/
│   │   ├── execution_plan.cpp
│   │   ├── sql_compiler.cpp
│   │   ├── stream_executor.cpp
│   │   ├── window_manager.cpp
│   │   ├── join_processor.cpp
│   │   ├── state_store.cpp
│   │   └── operators.cpp
│   └── managers/
│       └── stream_manager.cpp
└── routes/
    └── stream_routes.cpp           # HTTP endpoints
```

### 1. Execution Plan Data Structures

**`include/queen/stream/execution_plan.hpp`**

```cpp
#pragma once

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <json.hpp>

namespace queen::stream {

// Operation types
enum class OperationType {
    FILTER,
    MAP,
    FLAT_MAP,
    GROUP_BY,
    AGGREGATE,
    WINDOW,
    JOIN,
    DISTINCT,
    LIMIT,
    SKIP,
    SAMPLE,
    BRANCH,
    STATEFUL_MAP,
    FOREACH,
    DEBOUNCE,
    THROTTLE
};

// Predicate for filtering
struct Predicate {
    enum class Type { COMPARISON, LOGICAL, JSONB_OPERATOR, FUNCTION };
    
    Type type;
    std::string field;              // e.g., "payload.amount"
    std::string operator_str;       // e.g., ">", "=", "IN", "AND", "OR"
    nlohmann::json value;           // comparison value
    std::vector<Predicate> children; // for logical operators
    
    static Predicate from_json(const nlohmann::json& j);
    std::string to_sql() const;
};

// Field mapping for map operations
struct FieldMapping {
    std::string output_name;
    std::string source_path;        // e.g., "payload.userId"
    std::optional<std::string> transform; // e.g., "UPPER", "::numeric"
    
    static FieldMapping from_json(const nlohmann::json& j);
    std::string to_sql() const;
};

// Aggregation specification
struct Aggregation {
    enum class Function { COUNT, SUM, AVG, MIN, MAX, ARRAY_AGG, 
                         COUNT_DISTINCT, STDDEV, PERCENTILE, FIRST, LAST };
    
    Function function;
    std::string output_name;
    std::optional<std::string> field;
    nlohmann::json parameters;      // e.g., percentile value
    
    static Aggregation from_json(const nlohmann::json& j);
    std::string to_sql() const;
};

// Window specification
struct WindowSpec {
    enum class Type { TUMBLING, SLIDING, SESSION, HOPPING, CUSTOM };
    
    Type type;
    
    // Time-based
    std::optional<int> duration_seconds;
    std::optional<int> slide_seconds;
    std::optional<int> grace_seconds;
    
    // Count-based
    std::optional<int> count;
    std::optional<int> slide_count;
    
    // Session
    std::optional<int> gap_seconds;
    std::optional<int> max_duration_seconds;
    
    // Custom
    std::vector<std::string> boundaries;
    std::optional<std::string> timezone;
    
    static WindowSpec from_json(const nlohmann::json& j);
};

// Join specification
struct JoinSpec {
    enum class Type { INNER, LEFT, RIGHT, OUTER };
    
    Type type;
    std::string right_source;       // queue@consumerGroup
    std::string left_key;
    std::string right_key;
    std::optional<int> window_seconds;
    std::optional<nlohmann::json> condition;  // Additional join condition
    std::optional<nlohmann::json> select;     // Output field selection
    
    static JoinSpec from_json(const nlohmann::json& j);
};

// Generic operation
struct Operation {
    OperationType type;
    
    // Operation-specific data
    std::optional<Predicate> predicate;
    std::vector<FieldMapping> field_mappings;
    std::vector<std::string> group_by_keys;
    std::vector<Aggregation> aggregations;
    std::optional<WindowSpec> window;
    std::optional<JoinSpec> join;
    
    // Generic parameters
    nlohmann::json parameters;
    
    static Operation from_json(const nlohmann::json& j);
};

// Complete execution plan
struct ExecutionPlan {
    // Source
    std::string source_queue;
    std::string consumer_group;
    std::optional<std::string> partition;
    
    // Operations chain
    std::vector<Operation> operations;
    
    // Output
    std::optional<std::string> destination_queue;
    std::optional<std::string> output_table;
    std::string output_mode = "append";  // append, update, complete
    
    // Execution options
    int batch_size = 100;
    bool auto_ack = true;
    std::optional<int> checkpoint_interval_ms;
    
    // Metadata
    std::optional<std::string> stream_name;
    std::optional<std::string> created_by;
    
    static ExecutionPlan from_json(const nlohmann::json& j);
    nlohmann::json to_json() const;
    void validate() const;  // Throws if invalid
};

} // namespace queen::stream
```

### 2. SQL Compiler

**`include/queen/stream/sql_compiler.hpp`**

```cpp
#pragma once

#include "execution_plan.hpp"
#include <string>
#include <sstream>

namespace queen::stream {

class SQLCompiler {
public:
    struct CompiledQuery {
        std::string sql;
        std::vector<std::string> params;
        bool is_stateful;           // Requires state management
        bool is_windowed;           // Requires window management
        bool has_joins;             // Has stream-stream joins
        std::vector<std::string> required_indices;
    };
    
    CompiledQuery compile(const ExecutionPlan& plan);
    
private:
    // Compilation stages
    std::string compile_source(const ExecutionPlan& plan);
    std::string compile_consumer_filter(const ExecutionPlan& plan);
    std::vector<std::string> compile_where_clauses(const std::vector<Operation>& ops);
    std::string compile_select_fields(const std::vector<Operation>& ops);
    std::string compile_group_by(const std::vector<Operation>& ops);
    std::string compile_having(const std::vector<Operation>& ops);
    std::string compile_order_by(const std::vector<Operation>& ops);
    std::string compile_limit(const std::vector<Operation>& ops);
    
    // Operation-specific compilation
    std::string compile_filter(const Operation& op);
    std::string compile_map(const Operation& op);
    std::string compile_aggregate(const Operation& op, const std::string& group_by);
    std::string compile_window(const Operation& op);
    std::string compile_join(const Operation& op, const ExecutionPlan& plan);
    
    // Helper functions
    std::string jsonb_path_to_sql(const std::string& path);
    std::string escape_identifier(const std::string& identifier);
    std::string quote_literal(const std::string& literal);
    
    // Optimization hints
    void add_optimization_hints(std::stringstream& sql, const ExecutionPlan& plan);
    void suggest_indices(CompiledQuery& query, const ExecutionPlan& plan);
};

} // namespace queen::stream
```

**`src/stream/sql_compiler.cpp`** (key methods):

```cpp
#include "queen/stream/sql_compiler.hpp"
#include <spdlog/spdlog.h>

namespace queen::stream {

SQLCompiler::CompiledQuery SQLCompiler::compile(const ExecutionPlan& plan) {
    CompiledQuery result;
    std::stringstream sql;
    
    // Validate plan first
    plan.validate();
    
    // Determine query characteristics
    result.is_stateful = has_stateful_operations(plan);
    result.is_windowed = has_window_operations(plan);
    result.has_joins = has_join_operations(plan);
    
    // Build base query
    sql << compile_source(plan);
    
    // Add filters
    auto where_clauses = compile_where_clauses(plan.operations);
    where_clauses.push_back(compile_consumer_filter(plan));
    
    if (!where_clauses.empty()) {
        sql << " WHERE " << join_strings(where_clauses, " AND ");
    }
    
    // Add grouping
    auto group_by = compile_group_by(plan.operations);
    if (!group_by.empty()) {
        sql << " GROUP BY " << group_by;
        
        auto having = compile_having(plan.operations);
        if (!having.empty()) {
            sql << " HAVING " << having;
        }
    }
    
    // Add ordering
    auto order_by = compile_order_by(plan.operations);
    if (!order_by.empty()) {
        sql << " ORDER BY " << order_by;
    } else {
        // Default ordering for stream processing
        sql << " ORDER BY m.created_at, m.id";
    }
    
    // Add limit
    auto limit = compile_limit(plan.operations);
    if (!limit.empty()) {
        sql << " LIMIT " << limit;
    } else {
        sql << " LIMIT " << plan.batch_size;
    }
    
    result.sql = sql.str();
    suggest_indices(result, plan);
    
    spdlog::debug("Compiled SQL: {}", result.sql);
    
    return result;
}

std::string SQLCompiler::compile_source(const ExecutionPlan& plan) {
    std::stringstream sql;
    
    // Parse source queue
    auto [queue_name, consumer_group] = parse_source(plan.source_queue);
    
    sql << "SELECT m.* ";
    sql << "FROM queen.messages m ";
    sql << "JOIN queen.partitions p ON p.id = m.partition_id ";
    sql << "JOIN queen.queues q ON q.id = p.queue_id ";
    sql << "LEFT JOIN queen.partition_consumers pc ";
    sql << "  ON pc.partition_id = p.id ";
    sql << "  AND pc.consumer_group = '" << escape_identifier(consumer_group) << "' ";
    
    return sql.str();
}

std::string SQLCompiler::compile_consumer_filter(const ExecutionPlan& plan) {
    // Only get messages not yet consumed by this consumer group
    return R"(
        (pc.last_consumed_created_at IS NULL 
         OR m.created_at > pc.last_consumed_created_at
         OR (DATE_TRUNC('milliseconds', m.created_at) = DATE_TRUNC('milliseconds', pc.last_consumed_created_at) 
             AND m.id > pc.last_consumed_id))
    )";
}

std::string SQLCompiler::compile_filter(const Operation& op) {
    if (!op.predicate) {
        throw std::runtime_error("Filter operation missing predicate");
    }
    return op.predicate->to_sql();
}

std::string SQLCompiler::compile_map(const Operation& op) {
    std::vector<std::string> fields;
    
    for (const auto& mapping : op.field_mappings) {
        fields.push_back(mapping.to_sql() + " AS " + escape_identifier(mapping.output_name));
    }
    
    return join_strings(fields, ", ");
}

std::string SQLCompiler::compile_aggregate(const Operation& op, const std::string& group_by) {
    std::vector<std::string> fields;
    
    // Include group by keys
    if (!group_by.empty()) {
        fields.push_back(group_by);
    }
    
    // Add aggregations
    for (const auto& agg : op.aggregations) {
        fields.push_back(agg.to_sql() + " AS " + escape_identifier(agg.output_name));
    }
    
    return join_strings(fields, ", ");
}

std::string SQLCompiler::jsonb_path_to_sql(const std::string& path) {
    // Convert "payload.userId" to "m.payload->>'userId'"
    // Convert "payload.nested.field" to "m.payload->'nested'->>'field'"
    
    std::vector<std::string> parts = split_string(path, '.');
    
    if (parts.empty()) {
        throw std::runtime_error("Invalid JSONB path: " + path);
    }
    
    std::stringstream sql;
    sql << "m." << parts[0];
    
    for (size_t i = 1; i < parts.size(); i++) {
        if (i == parts.size() - 1) {
            // Last element: use ->> for text extraction
            sql << "->>" << quote_literal(parts[i]);
        } else {
            // Intermediate elements: use -> for JSON extraction
            sql << "->" << quote_literal(parts[i]);
        }
    }
    
    return sql.str();
}

} // namespace queen::stream
```

### 3. Stream Executor

**`include/queen/stream/stream_executor.hpp`**

```cpp
#pragma once

#include "execution_plan.hpp"
#include "sql_compiler.hpp"
#include "state_store.hpp"
#include "window_manager.hpp"
#include "queen/database.hpp"
#include <functional>

namespace queen::stream {

class StreamExecutor {
public:
    using MessageCallback = std::function<void(const nlohmann::json&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    StreamExecutor(DatabasePool& db_pool);
    
    // Execute stream and return results via callback
    void execute(
        const ExecutionPlan& plan,
        MessageCallback on_message,
        ErrorCallback on_error
    );
    
    // Execute and output to queue
    void execute_to_queue(const ExecutionPlan& plan);
    
    // Execute and materialize to table
    void execute_to_table(const ExecutionPlan& plan);
    
    // Register persistent stream
    std::string register_stream(const ExecutionPlan& plan);
    
    // Stop stream
    void stop_stream(const std::string& stream_id);
    
    // Get stream status
    nlohmann::json get_stream_status(const std::string& stream_id);
    
private:
    DatabasePool& db_pool_;
    SQLCompiler compiler_;
    std::unique_ptr<StateStore> state_store_;
    std::unique_ptr<WindowManager> window_manager_;
    
    // Execution helpers
    void execute_simple_query(
        const ExecutionPlan& plan,
        const SQLCompiler::CompiledQuery& compiled,
        MessageCallback on_message
    );
    
    void execute_stateful_query(
        const ExecutionPlan& plan,
        const SQLCompiler::CompiledQuery& compiled,
        MessageCallback on_message
    );
    
    void execute_windowed_query(
        const ExecutionPlan& plan,
        const SQLCompiler::CompiledQuery& compiled,
        MessageCallback on_message
    );
    
    void execute_with_joins(
        const ExecutionPlan& plan,
        const SQLCompiler::CompiledQuery& compiled,
        MessageCallback on_message
    );
    
    // State management
    void update_consumer_offset(
        const ExecutionPlan& plan,
        const std::string& last_message_id,
        const std::string& last_timestamp
    );
    
    void checkpoint_state(const std::string& stream_id);
    
    // Output handling
    void push_to_queue(const std::string& queue, const nlohmann::json& message);
    void upsert_to_table(const std::string& table, const nlohmann::json& row);
};

} // namespace queen::stream
```

### 4. State Store

**`include/queen/stream/state_store.hpp`**

```cpp
#pragma once

#include "queen/database.hpp"
#include <json.hpp>
#include <optional>
#include <string>

namespace queen::stream {

class StateStore {
public:
    StateStore(DatabasePool& db_pool);
    
    // Get state for a key
    std::optional<nlohmann::json> get(
        const std::string& stream_execution_id,
        const std::string& partition_id,
        const std::string& key
    );
    
    // Put state for a key
    void put(
        const std::string& stream_execution_id,
        const std::string& partition_id,
        const std::string& key,
        const nlohmann::json& value,
        std::optional<int> ttl_seconds = std::nullopt
    );
    
    // Delete state
    void remove(
        const std::string& stream_execution_id,
        const std::string& partition_id,
        const std::string& key
    );
    
    // Get all state for a partition
    std::vector<std::pair<std::string, nlohmann::json>> get_all(
        const std::string& stream_execution_id,
        const std::string& partition_id
    );
    
    // Clear expired state
    void clear_expired();
    
    // Checkpoint (flush to disk)
    void checkpoint(const std::string& stream_execution_id);
    
private:
    DatabasePool& db_pool_;
    
    // In-memory cache for performance
    struct CacheEntry {
        nlohmann::json value;
        std::chrono::system_clock::time_point last_access;
        bool dirty;
    };
    
    std::unordered_map<std::string, CacheEntry> cache_;
    std::mutex cache_mutex_;
    
    std::string make_cache_key(
        const std::string& stream_execution_id,
        const std::string& partition_id,
        const std::string& key
    );
};

} // namespace queen::stream
```

### 5. Window Manager

**`include/queen/stream/window_manager.hpp`**

```cpp
#pragma once

#include "execution_plan.hpp"
#include "queen/database.hpp"
#include <json.hpp>
#include <vector>

namespace queen::stream {

struct WindowInstance {
    std::string window_id;
    std::string window_key;
    WindowSpec::Type type;
    
    std::optional<std::string> window_start;
    std::optional<std::string> window_end;
    std::optional<int64_t> start_sequence;
    std::optional<int64_t> end_sequence;
    
    std::string status;  // open, closing, closed, emitted
    int message_count;
    nlohmann::json state;
};

class WindowManager {
public:
    WindowManager(DatabasePool& db_pool);
    
    // Process message for window
    std::vector<WindowInstance> process_message(
        const std::string& stream_execution_id,
        const std::string& partition_id,
        const nlohmann::json& message,
        const WindowSpec& window_spec,
        const std::string& window_key
    );
    
    // Get ready windows (ready to be emitted)
    std::vector<WindowInstance> get_ready_windows(
        const std::string& stream_execution_id,
        const WindowSpec& window_spec
    );
    
    // Mark window as emitted
    void mark_emitted(const std::string& window_id);
    
    // Close expired session windows
    void close_expired_sessions(
        const std::string& stream_execution_id,
        const WindowSpec& window_spec
    );
    
private:
    DatabasePool& db_pool_;
    
    // Window type handlers
    WindowInstance process_tumbling_time(
        const std::string& stream_execution_id,
        const std::string& partition_id,
        const nlohmann::json& message,
        const WindowSpec& spec,
        const std::string& key
    );
    
    WindowInstance process_sliding_time(
        const std::string& stream_execution_id,
        const std::string& partition_id,
        const nlohmann::json& message,
        const WindowSpec& spec,
        const std::string& key
    );
    
    WindowInstance process_session(
        const std::string& stream_execution_id,
        const std::string& partition_id,
        const nlohmann::json& message,
        const WindowSpec& spec,
        const std::string& key
    );
    
    // Helper functions
    std::string calculate_window_start(
        const std::string& timestamp,
        int duration_seconds,
        const std::string& align = "epoch"
    );
    
    bool is_window_ready(
        const WindowInstance& window,
        const WindowSpec& spec
    );
};

} // namespace queen::stream
```

### 6. Stream Manager (Main Interface)

**`include/queen/stream_manager.hpp`**

```cpp
#pragma once

#include "queen/database.hpp"
#include "stream/execution_plan.hpp"
#include "stream/stream_executor.hpp"
#include <json.hpp>

namespace queen {

class StreamManager {
public:
    StreamManager(DatabasePool& db_pool);
    
    // Execute stream query (synchronous, returns results)
    nlohmann::json execute_query(const nlohmann::json& plan_json);
    
    // Start continuous stream processing
    std::string start_stream(const nlohmann::json& plan_json);
    
    // Stop stream
    void stop_stream(const std::string& stream_id);
    
    // Get stream status
    nlohmann::json get_stream_status(const std::string& stream_id);
    
    // List all streams
    std::vector<nlohmann::json> list_streams();
    
    // Execute plan and stream results (async)
    void stream_results(
        const nlohmann::json& plan_json,
        std::function<void(const nlohmann::json&)> callback
    );
    
private:
    DatabasePool& db_pool_;
    std::unique_ptr<stream::StreamExecutor> executor_;
    
    // Validation
    void validate_plan(const stream::ExecutionPlan& plan);
    void check_permissions(const stream::ExecutionPlan& plan, const std::string& user_id);
    void enforce_resource_limits(const stream::ExecutionPlan& plan);
};

} // namespace queen
```

### 7. HTTP Routes

**`server/src/routes/stream_routes.cpp`**

```cpp
#include "queen/stream_manager.hpp"
#include <json.hpp>

namespace queen {

void register_stream_routes(uWS::App& app, StreamManager& stream_manager) {
    
    // Execute stream query (one-shot)
    app.post("/api/v1/stream/query", [&](auto* res, auto* req) {
        std::string body = std::string(req->getBody());
        
        try {
            auto plan_json = nlohmann::json::parse(body);
            auto result = stream_manager.execute_query(plan_json);
            
            res->writeStatus("200 OK")
               ->writeHeader("Content-Type", "application/json")
               ->end(result.dump());
               
        } catch (const std::exception& e) {
            nlohmann::json error = {
                {"error", e.what()},
                {"type", "query_error"}
            };
            
            res->writeStatus("400 Bad Request")
               ->writeHeader("Content-Type", "application/json")
               ->end(error.dump());
        }
    });
    
    // Stream results (long-lived connection)
    app.post("/api/v1/stream/consume", [&](auto* res, auto* req) {
        std::string body = std::string(req->getBody());
        
        res->writeHeader("Content-Type", "application/x-ndjson");
        res->writeHeader("Transfer-Encoding", "chunked");
        
        try {
            auto plan_json = nlohmann::json::parse(body);
            
            stream_manager.stream_results(plan_json, [res](const nlohmann::json& message) {
                std::string line = message.dump() + "\n";
                res->write(line);
            });
            
            res->end();
            
        } catch (const std::exception& e) {
            nlohmann::json error = {
                {"error", e.what()}
            };
            res->end(error.dump() + "\n");
        }
    });
    
    // Start continuous stream
    app.post("/api/v1/stream/start", [&](auto* res, auto* req) {
        std::string body = std::string(req->getBody());
        
        try {
            auto plan_json = nlohmann::json::parse(body);
            std::string stream_id = stream_manager.start_stream(plan_json);
            
            nlohmann::json response = {
                {"streamId", stream_id},
                {"status", "started"}
            };
            
            res->writeStatus("200 OK")
               ->writeHeader("Content-Type", "application/json")
               ->end(response.dump());
               
        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            res->writeStatus("400 Bad Request")
               ->writeHeader("Content-Type", "application/json")
               ->end(error.dump());
        }
    });
    
    // Stop stream
    app.post("/api/v1/stream/:streamId/stop", [&](auto* res, auto* req) {
        std::string stream_id = std::string(req->getParameter(0));
        
        try {
            stream_manager.stop_stream(stream_id);
            
            nlohmann::json response = {
                {"streamId", stream_id},
                {"status", "stopped"}
            };
            
            res->writeStatus("200 OK")
               ->writeHeader("Content-Type", "application/json")
               ->end(response.dump());
               
        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            res->writeStatus("404 Not Found")
               ->writeHeader("Content-Type", "application/json")
               ->end(error.dump());
        }
    });
    
    // Get stream status
    app.get("/api/v1/stream/:streamId/status", [&](auto* res, auto* req) {
        std::string stream_id = std::string(req->getParameter(0));
        
        try {
            auto status = stream_manager.get_stream_status(stream_id);
            
            res->writeStatus("200 OK")
               ->writeHeader("Content-Type", "application/json")
               ->end(status.dump());
               
        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            res->writeStatus("404 Not Found")
               ->writeHeader("Content-Type", "application/json")
               ->end(error.dump());
        }
    });
    
    // List all streams
    app.get("/api/v1/stream/list", [&](auto* res, auto* req) {
        try {
            auto streams = stream_manager.list_streams();
            
            nlohmann::json response = {
                {"streams", streams}
            };
            
            res->writeStatus("200 OK")
               ->writeHeader("Content-Type", "application/json")
               ->end(response.dump());
               
        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            res->writeStatus("500 Internal Server Error")
               ->writeHeader("Content-Type", "application/json")
               ->end(error.dump());
        }
    });
}

} // namespace queen
```

---

## Client Implementation (JavaScript)

### File Structure

```
client-js/
├── stream/
│   ├── Stream.js              # Base Stream class
│   ├── GroupedStream.js       # After groupBy()
│   ├── WindowedStream.js      # After window()
│   ├── JoinedStream.js        # After join()
│   ├── OperationBuilder.js    # Builds operation objects
│   ├── PredicateBuilder.js    # Builds predicates
│   └── Serializer.js          # Serializes functions/predicates
├── index.js                   # Export stream API
└── types/
    └── stream.d.ts            # TypeScript definitions
```

### 1. Base Stream Class

**`client-js/stream/Stream.js`**

```javascript
import { OperationBuilder } from './OperationBuilder.js'
import { PredicateBuilder } from './PredicateBuilder.js'
import { GroupedStream } from './GroupedStream.js'
import { Serializer } from './Serializer.js'

export class Stream {
  #queen
  #source
  #operations
  #options
  
  constructor(queen, source, operations = [], options = {}) {
    this.#queen = queen
    this.#source = source
    this.#operations = operations
    this.#options = options
  }
  
  // ========== FILTERING ==========
  
  filter(predicate) {
    const operation = typeof predicate === 'function'
      ? OperationBuilder.filter(Serializer.serializePredicate(predicate))
      : OperationBuilder.filter(PredicateBuilder.build(predicate))
    
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  // ========== TRANSFORMATION ==========
  
  map(mapper) {
    const operation = typeof mapper === 'function'
      ? OperationBuilder.map(Serializer.serializeMapper(mapper))
      : OperationBuilder.map(mapper)
    
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  flatMap(mapper) {
    const operation = OperationBuilder.flatMap(Serializer.serializeMapper(mapper))
    
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  mapValues(mapper) {
    return this.map(msg => ({
      ...msg,
      payload: typeof mapper === 'function' ? mapper(msg.payload) : mapper
    }))
  }
  
  // ========== GROUPING ==========
  
  groupBy(key) {
    const operation = OperationBuilder.groupBy(
      Array.isArray(key) ? key : [key]
    )
    
    return new GroupedStream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  repartition(options) {
    const operation = OperationBuilder.repartition(options)
    
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  // ========== JOINING ==========
  
  join(otherStream, joinSpec) {
    return this.#createJoin('inner', otherStream, joinSpec)
  }
  
  leftJoin(otherStream, joinSpec) {
    return this.#createJoin('left', otherStream, joinSpec)
  }
  
  rightJoin(otherStream, joinSpec) {
    return this.#createJoin('right', otherStream, joinSpec)
  }
  
  outerJoin(otherStream, joinSpec) {
    return this.#createJoin('outer', otherStream, joinSpec)
  }
  
  joinTable(tableName, joinSpec) {
    const operation = OperationBuilder.joinTable(tableName, joinSpec)
    
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  #createJoin(type, otherStream, joinSpec) {
    const operation = OperationBuilder.join({
      type,
      rightSource: otherStream.#source,
      ...joinSpec
    })
    
    const { JoinedStream } = require('./JoinedStream.js')
    
    return new JoinedStream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  // ========== UTILITY OPERATIONS ==========
  
  distinct(field) {
    const operation = OperationBuilder.distinct(field)
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  limit(n) {
    const operation = OperationBuilder.limit(n)
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  skip(n) {
    const operation = OperationBuilder.skip(n)
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  sample(rate) {
    const operation = OperationBuilder.sample(rate)
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  // ========== BRANCHING ==========
  
  branch(predicates) {
    const branches = predicates.map((predicate, index) => {
      return new Stream(
        this.#queen,
        this.#source,
        [
          ...this.#operations,
          OperationBuilder.filter(
            typeof predicate === 'function'
              ? Serializer.serializePredicate(predicate)
              : PredicateBuilder.build(predicate)
          )
        ],
        this.#options
      )
    })
    
    return branches
  }
  
  // ========== SIDE EFFECTS ==========
  
  foreach(callback) {
    const operation = OperationBuilder.foreach(Serializer.serializeFunction(callback))
    return new Stream(
      this.#queen,
      this.#source,
      [...this.#operations, operation],
      this.#options
    )
  }
  
  // ========== OUTPUT ==========
  
  async outputTo(destination) {
    const plan = this.#buildExecutionPlan({ destination })
    
    const response = await this.#queen._http.post('/api/v1/stream/start', plan)
    
    return response.streamId
  }
  
  async toTable(tableName, options = {}) {
    const plan = this.#buildExecutionPlan({
      outputTable: tableName,
      outputMode: options.mode || 'upsert',
      outputOptions: options
    })
    
    const response = await this.#queen._http.post('/api/v1/stream/start', plan)
    
    return response.streamId
  }
  
  // ========== CONSUMPTION ==========
  
  async *[Symbol.asyncIterator]() {
    const plan = this.#buildExecutionPlan()
    
    const response = await this.#queen._http.post('/api/v1/stream/consume', plan, {
      stream: true
    })
    
    for await (const line of response.body) {
      if (!line.trim()) continue
      
      const message = JSON.parse(line)
      yield message
    }
  }
  
  async take(n) {
    const results = []
    let count = 0
    
    for await (const message of this.limit(n)) {
      results.push(message)
      if (++count >= n) break
    }
    
    return results
  }
  
  async collect() {
    const results = []
    
    for await (const message of this) {
      results.push(message)
    }
    
    return results
  }
  
  // ========== EXECUTION PLAN ==========
  
  #buildExecutionPlan(output = {}) {
    const [queue, consumerGroup, partition] = this.#parseSource(this.#source)
    
    return {
      source: queue,
      consumerGroup: consumerGroup || '__STREAM__',
      partition: partition || null,
      operations: this.#operations,
      destination: output.destination || null,
      outputTable: output.outputTable || null,
      outputMode: output.outputMode || 'append',
      outputOptions: output.outputOptions || {},
      batchSize: this.#options.batchSize || 100,
      autoAck: this.#options.autoAck !== false,
      checkpointIntervalMs: this.#options.checkpointIntervalMs || 5000
    }
  }
  
  #parseSource(source) {
    // Parse "queue@group/partition" format
    const [queuePart, partition] = source.split('/')
    const [queue, group] = queuePart.split('@')
    
    return [queue, group, partition]
  }
}
```

### 2. Grouped Stream

**`client-js/stream/GroupedStream.js`**

```javascript
import { Stream } from './Stream.js'
import { OperationBuilder } from './OperationBuilder.js'
import { WindowedStream } from './WindowedStream.js'

export class GroupedStream extends Stream {
  // ========== AGGREGATIONS ==========
  
  count() {
    return this.aggregate({
      count: { $count: '*' }
    })
  }
  
  sum(field) {
    return this.aggregate({
      sum: { $sum: field }
    })
  }
  
  avg(field) {
    return this.aggregate({
      avg: { $avg: field }
    })
  }
  
  min(field) {
    return this.aggregate({
      min: { $min: field }
    })
  }
  
  max(field) {
    return this.aggregate({
      max: { $max: field }
    })
  }
  
  aggregate(aggregations) {
    const operation = OperationBuilder.aggregate(aggregations)
    
    return new Stream(
      this._queen,
      this._source,
      [...this._operations, operation],
      this._options
    )
  }
  
  // ========== WINDOWING ==========
  
  window(windowSpec) {
    const operation = OperationBuilder.window(windowSpec)
    
    return new WindowedStream(
      this._queen,
      this._source,
      [...this._operations, operation],
      this._options
    )
  }
  
  // ========== STATEFUL OPERATIONS ==========
  
  reduce(options) {
    const operation = OperationBuilder.reduce(options)
    
    return new Stream(
      this._queen,
      this._source,
      [...this._operations, operation],
      this._options
    )
  }
  
  statefulMap(options) {
    const operation = OperationBuilder.statefulMap(options)
    
    return new Stream(
      this._queen,
      this._source,
      [...this._operations, operation],
      this._options
    )
  }
  
  deduplicate(field, ttl) {
    const operation = OperationBuilder.deduplicate(field, ttl)
    
    return new Stream(
      this._queen,
      this._source,
      [...this._operations, operation],
      this._options
    )
  }
  
  // ========== SCAN ==========
  
  scan(options) {
    const operation = OperationBuilder.scan(options)
    
    return new Stream(
      this._queen,
      this._source,
      [...this._operations, operation],
      this._options
    )
  }
}
```

### 3. Operation Builder

**`client-js/stream/OperationBuilder.js`**

```javascript
export class OperationBuilder {
  static filter(predicate) {
    return {
      type: 'filter',
      predicate
    }
  }
  
  static map(fields) {
    return {
      type: 'map',
      fields
    }
  }
  
  static flatMap(mapper) {
    return {
      type: 'flatMap',
      mapper
    }
  }
  
  static groupBy(keys) {
    return {
      type: 'groupBy',
      keys
    }
  }
  
  static aggregate(aggregations) {
    return {
      type: 'aggregate',
      aggregations
    }
  }
  
  static window(spec) {
    return {
      type: 'window',
      window: spec
    }
  }
  
  static join(spec) {
    return {
      type: 'join',
      join: spec
    }
  }
  
  static joinTable(tableName, spec) {
    return {
      type: 'joinTable',
      tableName,
      join: spec
    }
  }
  
  static distinct(field) {
    return {
      type: 'distinct',
      field
    }
  }
  
  static limit(n) {
    return {
      type: 'limit',
      limit: n
    }
  }
  
  static skip(n) {
    return {
      type: 'skip',
      skip: n
    }
  }
  
  static sample(rate) {
    return {
      type: 'sample',
      rate
    }
  }
  
  static foreach(callback) {
    return {
      type: 'foreach',
      callback
    }
  }
  
  static statefulMap(options) {
    return {
      type: 'statefulMap',
      initialState: options.initialState,
      mapper: options.mapper
    }
  }
  
  static reduce(options) {
    return {
      type: 'reduce',
      initialValue: options.initialValue,
      accumulator: options.accumulator
    }
  }
  
  static deduplicate(field, ttl) {
    return {
      type: 'deduplicate',
      field,
      ttl
    }
  }
  
  static scan(options) {
    return {
      type: 'scan',
      initialValue: options.initialValue,
      accumulator: options.accumulator
    }
  }
  
  static repartition(options) {
    return {
      type: 'repartition',
      key: options.key,
      partitions: options.partitions,
      outputQueue: options.outputQueue
    }
  }
}
```

### 4. Predicate Builder

**`client-js/stream/PredicateBuilder.js`**

```javascript
export class PredicateBuilder {
  static build(obj) {
    if (Array.isArray(obj)) {
      return {
        type: 'logical',
        operator: 'AND',
        children: obj.map(p => PredicateBuilder.build(p))
      }
    }
    
    const predicates = []
    
    for (const [field, condition] of Object.entries(obj)) {
      if (typeof condition === 'object' && condition !== null) {
        // Operator object: { $gt: 1000 }
        for (const [op, value] of Object.entries(condition)) {
          predicates.push({
            type: 'comparison',
            field,
            operator: PredicateBuilder.#mapOperator(op),
            value
          })
        }
      } else {
        // Simple equality: { status: 'completed' }
        predicates.push({
          type: 'comparison',
          field,
          operator: '=',
          value: condition
        })
      }
    }
    
    if (predicates.length === 1) {
      return predicates[0]
    }
    
    return {
      type: 'logical',
      operator: 'AND',
      children: predicates
    }
  }
  
  static #mapOperator(op) {
    const mapping = {
      '$eq': '=',
      '$ne': '!=',
      '$gt': '>',
      '$gte': '>=',
      '$lt': '<',
      '$lte': '<=',
      '$in': 'IN',
      '$nin': 'NOT IN',
      '$contains': '@>',
      '$contained': '<@',
      '$exists': '?',
      '$regex': '~',
      '$iregex': '~*'
    }
    
    return mapping[op] || op
  }
}
```

### 5. Serializer (Function Serialization)

**`client-js/stream/Serializer.js`**

```javascript
export class Serializer {
  static serializePredicate(fn) {
    // Convert JS function to AST-like structure
    const fnStr = fn.toString()
    
    // Simple parser for arrow functions like: msg => msg.payload.amount > 1000
    const arrowMatch = fnStr.match(/\(?\s*(\w+)\s*\)?\s*=>\s*(.+)/)
    
    if (arrowMatch) {
      const [, param, body] = arrowMatch
      
      // Parse the body
      return this.#parseExpression(body.trim(), param)
    }
    
    throw new Error('Unable to serialize predicate function. Use object syntax instead.')
  }
  
  static serializeMapper(fn) {
    const fnStr = fn.toString()
    const arrowMatch = fnStr.match(/\(?\s*(\w+)\s*\)?\s*=>\s*(\{[\s\S]*\}|\(.+\)|.+)/)
    
    if (arrowMatch) {
      const [, param, body] = arrowMatch
      
      // For object literals: msg => ({ userId: msg.payload.userId })
      const objectMatch = body.match(/^\(\s*\{([\s\S]*)\}\s*\)$/) || body.match(/^\{([\s\S]*)\}$/)
      
      if (objectMatch) {
        const fields = this.#parseObjectLiteral(objectMatch[1], param)
        return { type: 'object', fields }
      }
      
      // For simple expressions: msg => msg.payload.userId
      return { type: 'expression', expr: this.#parseExpression(body, param) }
    }
    
    throw new Error('Unable to serialize mapper function. Use object syntax instead.')
  }
  
  static serializeFunction(fn) {
    // For functions we can't serialize, we'll need to handle server-side
    // For now, return a reference
    return {
      type: 'function_ref',
      code: fn.toString()
    }
  }
  
  static #parseExpression(expr, param) {
    // Simple expression parser
    // msg.payload.amount > 1000 => { field: 'payload.amount', operator: '>', value: 1000 }
    
    const comparisonOps = ['>=', '<=', '===', '!==', '==', '!=', '>', '<']
    
    for (const op of comparisonOps) {
      if (expr.includes(op)) {
        const [left, right] = expr.split(op).map(s => s.trim())
        
        return {
          type: 'comparison',
          field: this.#extractField(left, param),
          operator: this.#normalizeOperator(op),
          value: this.#parseValue(right)
        }
      }
    }
    
    // Logical operators
    if (expr.includes('&&')) {
      const parts = expr.split('&&').map(p => this.#parseExpression(p.trim(), param))
      return {
        type: 'logical',
        operator: 'AND',
        children: parts
      }
    }
    
    if (expr.includes('||')) {
      const parts = expr.split('||').map(p => this.#parseExpression(p.trim(), param))
      return {
        type: 'logical',
        operator: 'OR',
        children: parts
      }
    }
    
    throw new Error(`Unable to parse expression: ${expr}`)
  }
  
  static #extractField(expr, param) {
    // msg.payload.userId => 'payload.userId'
    if (expr.startsWith(param + '.')) {
      return expr.substring(param.length + 1)
    }
    
    return expr
  }
  
  static #normalizeOperator(op) {
    const mapping = {
      '===': '=',
      '==': '=',
      '!==': '!=',
      '!=': '!='
    }
    
    return mapping[op] || op
  }
  
  static #parseValue(value) {
    // Try to parse as JSON
    try {
      return JSON.parse(value)
    } catch {
      // If it's a string literal
      if (value.startsWith("'") || value.startsWith('"')) {
        return value.slice(1, -1)
      }
      
      return value
    }
  }
  
  static #parseObjectLiteral(str, param) {
    // Parse { userId: msg.payload.userId, amount: msg.payload.amount }
    const fields = {}
    
    // Simple regex-based parser (could be improved)
    const fieldRegex = /(\w+)\s*:\s*([^,}]+)/g
    let match
    
    while ((match = fieldRegex.exec(str)) !== null) {
      const [, key, value] = match
      fields[key] = this.#extractField(value.trim(), param)
    }
    
    return fields
  }
}
```

### 6. Integration with Queen Client

**`client-js/client-v2/Queen.js`** (additions):

```javascript
import { Stream } from '../stream/Stream.js'

export class Queen {
  // ... existing methods ...
  
  /**
   * Create a stream from a queue
   * @param {string} source - Queue source (queue@group or queue@group/partition)
   * @param {Object} options - Stream options
   * @returns {Stream}
   */
  stream(source, options = {}) {
    return new Stream(this, source, [], options)
  }
  
  /**
   * Create merged stream from multiple sources
   * @param {Array<string>} sources - Array of queue sources
   * @param {Object} options - Stream options
   * @returns {Stream}
   */
  streams(sources, options = {}) {
    // TODO: Implement merge logic
    throw new Error('Not implemented yet')
  }
  
  /**
   * Merge multiple streams
   * @param {Array<Stream>} streams - Streams to merge
   * @returns {Stream}
   */
  merge(streams) {
    // TODO: Implement merge logic
    throw new Error('Not implemented yet')
  }
}
```

---

## Protocol Specification

### Execution Plan JSON Format

```json
{
  "source": "events",
  "consumerGroup": "analytics",
  "partition": null,
  "operations": [
    {
      "type": "filter",
      "predicate": {
        "type": "comparison",
        "field": "payload.amount",
        "operator": ">",
        "value": 1000
      }
    },
    {
      "type": "map",
      "fields": {
        "userId": "payload.userId",
        "amount": "payload.amount",
        "timestamp": "created_at"
      }
    },
    {
      "type": "groupBy",
      "keys": ["userId"]
    },
    {
      "type": "aggregate",
      "aggregations": {
        "count": { "$count": "*" },
        "total": { "$sum": "amount" }
      }
    }
  ],
  "destination": "user-stats",
  "outputTable": null,
  "outputMode": "append",
  "batchSize": 100,
  "autoAck": true,
  "checkpointIntervalMs": 5000
}
```

### Response Formats

**Single Batch (POST /api/v1/stream/query):**

```json
{
  "messages": [
    {
      "userId": "user-123",
      "count": 42,
      "total": 125000
    }
  ],
  "hasMore": true,
  "checkpoint": {
    "lastMessageId": "msg-uuid",
    "lastTimestamp": "2024-01-01T12:00:00Z"
  }
}
```

**Streaming (POST /api/v1/stream/consume):**

```
{"userId":"user-123","count":42,"total":125000}
{"userId":"user-456","count":15,"total":45000}
{"userId":"user-789","count":8,"total":12000}
...
```

**Start Stream (POST /api/v1/stream/start):**

```json
{
  "streamId": "stream-uuid",
  "status": "started",
  "consumerGroup": "analytics"
}
```

---

## Implementation Phases

### Phase 1: Foundation (Core Infrastructure)

**Goal**: Basic stream processing with filter, map, groupBy, aggregate

**Server:**
1. Create database schema (stream_executions, stream_state)
2. Implement ExecutionPlan data structures
3. Implement basic SQLCompiler (filter, map, groupBy, aggregate)
4. Implement StreamExecutor (simple query execution)
5. Add HTTP endpoints (/api/v1/stream/query, /api/v1/stream/consume)

**Client:**
1. Create Stream class with filter(), map(), groupBy()
2. Create GroupedStream with aggregate functions
3. Implement OperationBuilder
4. Implement PredicateBuilder
5. Implement Serializer (basic function parsing)
6. Add integration to Queen client

**Tests:**
1. Filter by single condition
2. Filter by multiple conditions
3. Map to new fields
4. GroupBy + count
5. GroupBy + sum/avg/min/max
6. Chain operations: filter → map → groupBy → aggregate

**Deliverable**: Working stream API for basic queries

---

### Phase 2: Windows

**Goal**: Time and count-based windowing

**Server:**
1. Create stream_windows table
2. Implement WindowManager
3. Implement tumbling time windows
4. Implement tumbling count windows
5. Implement sliding time windows
6. Implement session windows
7. Update SQLCompiler to handle windows
8. Update StreamExecutor for windowed queries

**Client:**
1. Create WindowedStream class
2. Add window() method to GroupedStream
3. Support all window types in API
4. Add window-specific operations

**Tests:**
1. Tumbling time window (5 minute buckets)
2. Tumbling count window (100 messages)
3. Sliding time window (10m window, 1m slide)
4. Session window (30m gap)
5. Window with aggregations
6. Grace period handling

**Deliverable**: Full windowing support

---

### Phase 3: Joins

**Goal**: Stream-stream and stream-table joins

**Server:**
1. Create stream_joins table
2. Implement JoinProcessor
3. Implement inner join
4. Implement left/right/outer joins
5. Implement stream-table joins (PostgreSQL tables)
6. Update SQLCompiler for joins
7. Add join state management

**Client:**
1. Add join(), leftJoin(), rightJoin(), outerJoin() methods
2. Add joinTable() method
3. Create JoinedStream class
4. Support join window specifications

**Tests:**
1. Inner join (orders + payments)
2. Left join (orders + optional payments)
3. Stream-table join (enrich with user data)
4. Join with time window
5. Multi-way join (3+ streams)

**Deliverable**: Complete join functionality

---

### Phase 4: State Management

**Goal**: Stateful operations and exactly-once processing

**Server:**
1. Implement StateStore with caching
2. Implement stateful map/reduce operations
3. Add state checkpointing
4. Add TTL-based state expiration
5. Add deduplication support

**Client:**
1. Add statefulMap() method
2. Add reduce() method
3. Add scan() method
4. Add deduplicate() method
5. Support state configuration

**Tests:**
1. Stateful map (running totals)
2. Reduce with state
3. Deduplication by field
4. State expiration (TTL)
5. State recovery after restart

**Deliverable**: Stateful stream processing

---

### Phase 5: Output & Materialization

**Goal**: Multiple output modes and materialized views

**Server:**
1. Create stream_materialized_views table
2. Implement output to queue
3. Implement output to table (upsert mode)
4. Implement output to table (append mode)
5. Implement output to table (replace mode)
6. Add background workers for continuous streams

**Client:**
1. Add outputTo() method
2. Add toTable() method
3. Support output modes
4. Add start/stop stream management

**Tests:**
1. Output to queue
2. Materialize to table (upsert)
3. Materialize to table (append)
4. Continuous stream execution
5. Start/stop/restart stream

**Deliverable**: Complete output functionality

---

### Phase 6: Advanced Operations

**Goal**: Additional stream operations

**Server & Client:**
1. Implement distinct()
2. Implement limit() / skip()
3. Implement sample()
4. Implement branch()
5. Implement foreach()
6. Implement debounce()
7. Implement throttle()

**Tests:**
1. Distinct by field
2. Limit and skip
3. Probabilistic sampling
4. Stream branching
5. Side effects (foreach)

**Deliverable**: Full operation set

---

### Phase 7: Error Handling & Reliability

**Goal**: Production-ready error handling

**Server & Client:**
1. Add retry logic
2. Add circuit breaker
3. Add DLQ integration
4. Add error callbacks
5. Implement graceful degradation
6. Add health checks

**Tests:**
1. Retry on transient errors
2. Circuit breaker trips
3. Failed messages to DLQ
4. Recovery from errors

**Deliverable**: Production-ready reliability

---

### Phase 8: Performance & Optimization

**Goal**: Production performance

**Server:**
1. Query optimization and caching
2. State store optimization
3. Connection pooling tuning
4. Index recommendations
5. Batch processing optimization

**Client:**
1. Request batching
2. Response streaming optimization
3. Memory management

**Tests:**
1. Benchmark filter operations
2. Benchmark aggregations
3. Benchmark joins
4. Benchmark stateful operations
5. Load testing (1M+ messages)

**Deliverable**: Optimized performance

---

## Testing Strategy

### Unit Tests

**Server (C++):**
- `execution_plan_test.cpp` - Plan parsing and validation
- `sql_compiler_test.cpp` - SQL generation
- `predicate_test.cpp` - Predicate to SQL conversion
- `window_manager_test.cpp` - Window state management
- `state_store_test.cpp` - State operations
- `join_processor_test.cpp` - Join logic

**Client (JavaScript):**
- `Stream.test.js` - Stream operations
- `GroupedStream.test.js` - Aggregations
- `WindowedStream.test.js` - Windowing
- `PredicateBuilder.test.js` - Predicate building
- `Serializer.test.js` - Function serialization

### Integration Tests

**`client-js/test-v2/streaming/`:**
- `basic_operations.js` - Filter, map, groupBy, aggregate
- `windows.js` - All window types
- `joins.js` - Stream-stream and stream-table joins
- `stateful.js` - Stateful operations
- `output.js` - Output modes
- `error_handling.js` - Error cases
- `performance.js` - Performance benchmarks

### End-to-End Tests

**Real-world scenarios:**
- Fraud detection pipeline
- E-commerce funnel analytics
- IoT sensor monitoring
- Real-time dashboards
- Event-driven workflows

### Load Tests

**Benchmarks:**
- 1,000 msg/s sustained
- 10,000 msg/s sustained
- 100,000 msg/s burst
- 1M messages total processing
- Join performance with 100K messages on each side
- Window processing with 10K windows
- State store with 100K keys

---

## Performance Considerations

### SQL Optimization

**Index Requirements:**
```sql
-- Already exist:
CREATE INDEX idx_messages_partition_created_id 
  ON queen.messages(partition_id, created_at, id);

-- May need to add:
CREATE INDEX idx_messages_payload_gin 
  ON queen.messages USING GIN(payload);

CREATE INDEX idx_messages_created_at_id 
  ON queen.messages(created_at, id);
```

**Query Optimization:**
- Use `EXPLAIN ANALYZE` to verify query plans
- Limit batch sizes to prevent memory issues
- Use CTEs for complex aggregations
- Consider partitioning messages table by time

### State Store Optimization

**Caching Strategy:**
- In-memory LRU cache for hot keys
- Write-through caching
- Periodic flush to database
- TTL-based eviction

**Database Tuning:**
- Dedicated indexes on state_key
- GIN index on state_value for JSONB queries
- Regular VACUUM on state tables

### Window Processing

**Optimization:**
- Process windows in batches
- Cache window boundaries
- Use database triggers for session timeout
- Prune completed windows regularly

### Join Processing

**Optimization:**
- Use time-based indexes for join windows
- Limit join buffer size
- Expire old join state
- Consider join buffer in-memory for small windows

### Connection Pooling

**Tuning:**
- Increase `DB_POOL_SIZE` for streaming workloads
- Separate pool for stream processing vs regular operations
- Monitor pool utilization

### Memory Management

**Client-Side:**
- Stream results instead of buffering
- Use async iterators for lazy evaluation
- Implement backpressure

**Server-Side:**
- Limit batch sizes
- Stream results to client
- Clear state periodically

---

## Security Considerations

### Query Validation

**Server-side checks:**
- Validate all field references
- Whitelist allowed tables
- Prevent access to system tables
- Enforce resource limits (query timeout, max rows)

### Multi-tenancy

**Automatic injection:**
```sql
-- Inject tenant filter in all queries
WHERE partition_id IN (
  SELECT p.id FROM queen.partitions p
  JOIN queen.queues q ON q.id = p.queue_id
  WHERE q.tenant_id = $current_tenant_id
)
```

### Resource Limits

**Per-user limits:**
- Max concurrent streams
- Max query execution time
- Max result set size
- Max state store size
- Rate limiting

### Audit Logging

**Log all stream operations:**
- Stream creation
- Execution plan
- User/tenant
- Resource usage
- Errors

---

## Monitoring & Observability

### Metrics to Track

**Stream Execution:**
- Messages processed per second
- Query execution time
- State store size
- Window count
- Join buffer size

**Consumer Lag:**
- Time lag (NOW() - last_consumed_created_at)
- Offset lag (unconsumed message count)
- Per consumer group metrics

**Resource Usage:**
- CPU usage
- Memory usage
- Database connection pool utilization
- State store cache hit rate

### Endpoints

```
GET /api/v1/stream/:streamId/metrics
GET /api/v1/stream/:streamId/lag
GET /api/v1/stream/list
GET /api/v1/stream/stats
```

---

## Future Enhancements

**Not in initial implementation:**

1. **Exactly-once semantics** with distributed transactions
2. **Backpressure** control
3. **Dynamic scaling** of stream processors
4. **Query optimization** hints
5. **Streaming SQL** (SQL interface instead of DSL)
6. **Graph-based** execution plans
7. **Late arrival** handling with watermarks
8. **Side output** streams
9. **Custom UDFs** (User Defined Functions)
10. **Stream versioning** and migrations

---

## Summary

This implementation plan provides:

✅ **Complete server architecture** - All C++ components defined
✅ **Complete client API** - Full JavaScript DSL
✅ **Database schema** - All required tables
✅ **Protocol spec** - JSON format for execution plans
✅ **Implementation phases** - 8 phases from basic to advanced
✅ **Testing strategy** - Unit, integration, e2e, load tests
✅ **Performance tuning** - Optimization guidelines
✅ **Security** - Multi-tenancy, validation, limits

**Engineering Complexity**: High but achievable
**Lines of Code Estimate**: 
- Server: ~8,000 lines C++
- Client: ~3,000 lines JavaScript
- Tests: ~5,000 lines

**Next Step**: Start with Phase 1 - implement basic filter/map/groupBy/aggregate functionality.

