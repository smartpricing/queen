#pragma once

#include "queen/database.hpp"
#include <json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace queen {

// Forward declarations
class SQLCompiler;
class StreamExecutor;

// Predicate types
struct Predicate {
    enum class Type { COMPARISON, LOGICAL, FIELD };
    
    Type type;
    std::string field;
    std::string operator_str;
    nlohmann::json value;
    std::vector<Predicate> children;
    
    static Predicate from_json(const nlohmann::json& j);
    std::string to_sql() const;
};

// Operation types
struct Operation {
    enum class Type {
        FILTER,
        MAP,
        GROUP_BY,
        AGGREGATE,
        DISTINCT,
        LIMIT,
        SKIP
    };
    
    Type type;
    
    // Filter
    std::optional<Predicate> predicate;
    
    // Map
    nlohmann::json fields;
    
    // GroupBy
    std::vector<std::string> group_by_keys;
    
    // Aggregate
    nlohmann::json aggregations;
    
    // Distinct
    std::optional<std::string> distinct_field;
    
    // Limit/Skip
    std::optional<int> limit_value;
    std::optional<int> skip_value;
    
    static Operation from_json(const nlohmann::json& j);
};

// Execution plan
struct ExecutionPlan {
    std::string source;
    std::string consumer_group;
    std::optional<std::string> partition;
    
    std::vector<Operation> operations;
    
    std::optional<std::string> destination;
    std::optional<std::string> output_table;
    std::string output_mode = "append";
    
    int batch_size = 100;
    bool auto_ack = true;
    
    // Time filtering
    std::optional<std::string> from_time;  // 'latest' or ISO timestamp
    std::optional<std::string> to_time;    // ISO timestamp
    
    static ExecutionPlan from_json(const nlohmann::json& j);
    void validate() const;
};

// Compiled query result
struct CompiledQuery {
    std::string sql;
    std::vector<std::string> params;
    bool has_group_by = false;
    bool has_aggregations = false;
};

// Query result
struct StreamQueryResult {
    std::vector<nlohmann::json> messages;
    bool has_more = false;
    std::optional<std::string> checkpoint_message_id;
    std::optional<std::string> checkpoint_timestamp;
};

// SQL Compiler - converts execution plan to SQL
class SQLCompiler {
public:
    CompiledQuery compile(const ExecutionPlan& plan);
    
private:
    std::string compile_source(const ExecutionPlan& plan);
    std::string compile_consumer_filter(const ExecutionPlan& plan);
    std::vector<std::string> compile_where_clauses(const std::vector<Operation>& ops);
    std::string compile_select_fields(const std::vector<Operation>& ops, bool& has_aggregations);
    std::string compile_select_fields_for_map(const std::vector<Operation>& ops);
    std::string compile_select_fields_for_subquery(const std::vector<Operation>& ops, bool& has_aggregations);
    std::string compile_group_by(const std::vector<Operation>& ops);
    std::string compile_group_by_for_mapped(const std::vector<Operation>& ops);
    std::string compile_order_by();
    std::string compile_limit(const std::vector<Operation>& ops);
    
    std::string jsonb_path_to_sql(const std::string& path);
    std::string escape_identifier(const std::string& identifier);
    std::string quote_literal(const std::string& literal);
};

// Stream Executor - executes compiled queries
class StreamExecutor {
public:
    explicit StreamExecutor(std::shared_ptr<DatabasePool> db_pool);
    
    StreamQueryResult execute(const ExecutionPlan& plan, const CompiledQuery& compiled);
    
private:
    std::shared_ptr<DatabasePool> db_pool_;
    
    void update_consumer_offset(
        const ExecutionPlan& plan,
        const std::string& last_message_id,
        const std::string& last_timestamp
    );
};

// Stream Manager - main interface
class StreamManager {
public:
    explicit StreamManager(std::shared_ptr<DatabasePool> db_pool);
    
    // Execute stream query (one-shot)
    nlohmann::json execute_query(const nlohmann::json& plan_json);
    
    // Validate and compile plan
    CompiledQuery compile_plan(const ExecutionPlan& plan);
    
private:
    std::shared_ptr<DatabasePool> db_pool_;
    std::unique_ptr<SQLCompiler> compiler_;
    std::unique_ptr<StreamExecutor> executor_;
    
    void validate_plan(const ExecutionPlan& plan);
};

} // namespace queen

