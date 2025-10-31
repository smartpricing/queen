#include "queen/stream_manager.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace queen {

// ========== Helper Functions ==========

static std::string quote_literal(const std::string& str) {
    std::string escaped = str;
    size_t pos = 0;
    while ((pos = escaped.find("'", pos)) != std::string::npos) {
        escaped.replace(pos, 1, "''");
        pos += 2;
    }
    return "'" + escaped + "'";
}

// ========== Predicate Implementation ==========

Predicate Predicate::from_json(const nlohmann::json& j) {
    Predicate p;
    
    std::string type_str = j.value("type", "");
    if (type_str == "comparison") {
        p.type = Type::COMPARISON;
        p.field = j.value("field", "");
        p.operator_str = j.value("operator", "");
        p.value = j.value("value", nlohmann::json());
    } else if (type_str == "logical") {
        p.type = Type::LOGICAL;
        p.operator_str = j.value("operator", "AND");
        
        if (j.contains("children") && j["children"].is_array()) {
            for (const auto& child : j["children"]) {
                p.children.push_back(Predicate::from_json(child));
            }
        }
    } else if (type_str == "field") {
        p.type = Type::FIELD;
        p.field = j.value("field", "");
    }
    
    return p;
}

std::string Predicate::to_sql() const {
    if (type == Type::COMPARISON) {
        std::stringstream ss;
        
        // Convert field path to SQL (e.g., "payload.userId" -> "m.payload->>'userId'")
        std::string sql_field = field;
        if (sql_field.find("payload.") == 0) {
            std::string json_field = sql_field.substr(8); // Remove "payload."
            sql_field = "(m.payload->>" + quote_literal(json_field) + ")";
            
            // Add type cast for numeric comparisons
            if (operator_str == ">" || operator_str == "<" || 
                operator_str == ">=" || operator_str == "<=") {
                sql_field += "::numeric";
            }
        } else if (sql_field == "created_at") {
            sql_field = "m.created_at";
        } else if (sql_field.find(".") != std::string::npos) {
            // General nested field handling
            sql_field = "m." + sql_field;
        }
        
        ss << sql_field << " " << operator_str << " ";
        
        // Format value
        if (value.is_string()) {
            ss << quote_literal(value.get<std::string>());
        } else if (value.is_number()) {
            ss << value.dump();
        } else if (value.is_boolean()) {
            ss << (value.get<bool>() ? "true" : "false");
        } else if (value.is_array()) {
            // IN operator
            ss << "(";
            for (size_t i = 0; i < value.size(); i++) {
                if (i > 0) ss << ", ";
                if (value[i].is_string()) {
                    ss << quote_literal(value[i].get<std::string>());
                } else {
                    ss << value[i].dump();
                }
            }
            ss << ")";
        } else {
            ss << value.dump();
        }
        
        return ss.str();
    } else if (type == Type::LOGICAL) {
        if (children.empty()) return "true";
        if (children.size() == 1) return children[0].to_sql();
        
        std::stringstream ss;
        ss << "(";
        for (size_t i = 0; i < children.size(); i++) {
            if (i > 0) ss << " " << operator_str << " ";
            ss << children[i].to_sql();
        }
        ss << ")";
        return ss.str();
    }
    
    return "true";
}

// ========== Operation Implementation ==========

Operation Operation::from_json(const nlohmann::json& j) {
    Operation op;
    
    std::string type_str = j.value("type", "");
    
    if (type_str == "filter") {
        op.type = Type::FILTER;
        if (j.contains("predicate")) {
            op.predicate = Predicate::from_json(j["predicate"]);
        }
    } else if (type_str == "map") {
        op.type = Type::MAP;
        op.fields = j.value("fields", nlohmann::json::object());
    } else if (type_str == "groupBy") {
        op.type = Type::GROUP_BY;
        if (j.contains("keys") && j["keys"].is_array()) {
            for (const auto& key : j["keys"]) {
                op.group_by_keys.push_back(key.get<std::string>());
            }
        }
    } else if (type_str == "aggregate") {
        op.type = Type::AGGREGATE;
        op.aggregations = j.value("aggregations", nlohmann::json::object());
    } else if (type_str == "distinct") {
        op.type = Type::DISTINCT;
        op.distinct_field = j.value("field", "");
    } else if (type_str == "limit") {
        op.type = Type::LIMIT;
        op.limit_value = j.value("limit", 100);
    } else if (type_str == "skip") {
        op.type = Type::SKIP;
        op.skip_value = j.value("skip", 0);
    }
    
    return op;
}

// ========== ExecutionPlan Implementation ==========

ExecutionPlan ExecutionPlan::from_json(const nlohmann::json& j) {
    ExecutionPlan plan;
    
    plan.source = j.value("source", "");
    plan.consumer_group = j.value("consumerGroup", "__STREAM__");
    
    if (j.contains("partition") && !j["partition"].is_null()) {
        plan.partition = j["partition"].get<std::string>();
    }
    
    if (j.contains("operations") && j["operations"].is_array()) {
        for (const auto& op_json : j["operations"]) {
            plan.operations.push_back(Operation::from_json(op_json));
        }
    }
    
    if (j.contains("destination") && !j["destination"].is_null()) {
        plan.destination = j["destination"].get<std::string>();
    }
    
    if (j.contains("outputTable") && !j["outputTable"].is_null()) {
        plan.output_table = j["outputTable"].get<std::string>();
    }
    
    plan.output_mode = j.value("outputMode", "append");
    plan.batch_size = j.value("batchSize", 100);
    plan.auto_ack = j.value("autoAck", true);
    
    return plan;
}

void ExecutionPlan::validate() const {
    if (source.empty()) {
        throw std::runtime_error("Source queue is required");
    }
    
    if (consumer_group.empty()) {
        throw std::runtime_error("Consumer group is required");
    }
    
    // Validate batch size
    if (batch_size < 1 || batch_size > 10000) {
        throw std::runtime_error("Batch size must be between 1 and 10000");
    }
}

// ========== SQLCompiler Implementation ==========

CompiledQuery SQLCompiler::compile(const ExecutionPlan& plan) {
    CompiledQuery result;
    std::stringstream sql;
    
    spdlog::debug("Compiling execution plan for queue: {}, consumer: {}", 
                  plan.source, plan.consumer_group);
    
    // Check if we have aggregations, groupBy, and map
    bool has_aggregations = false;
    bool has_group_by = false;
    bool has_map = false;
    
    for (const auto& op : plan.operations) {
        if (op.type == Operation::Type::AGGREGATE) {
            has_aggregations = true;
        }
        if (op.type == Operation::Type::GROUP_BY) {
            has_group_by = true;
        }
        if (op.type == Operation::Type::MAP) {
            has_map = true;
        }
    }
    
    result.has_aggregations = has_aggregations;
    result.has_group_by = has_group_by;
    
    // If we have map + groupBy/aggregate, we need a subquery
    bool use_subquery = has_map && (has_group_by || has_aggregations);
    
    // Check for distinct operation
    std::string distinct_field;
    for (const auto& op : plan.operations) {
        if (op.type == Operation::Type::DISTINCT && op.distinct_field.has_value()) {
            distinct_field = op.distinct_field.value();
            break;
        }
    }
    
    // If we need a subquery (map + groupBy/aggregate), build it differently
    if (use_subquery) {
        // Outer query for aggregation
        sql << "SELECT ";
        sql << compile_select_fields_for_subquery(plan.operations, has_aggregations);
        sql << " FROM (";
        
        // Inner query for map
        sql << "SELECT ";
        sql << compile_select_fields_for_map(plan.operations);
        sql << " ";
        sql << compile_source(plan);
        
        // WHERE clause for inner query
        sql << "WHERE q.name = '" << escape_identifier(plan.source) << "' ";
        if (plan.partition.has_value()) {
            sql << "AND p.name = '" << escape_identifier(plan.partition.value()) << "' ";
        }
        sql << "AND " << compile_consumer_filter(plan);
        
        auto op_where = compile_where_clauses(plan.operations);
        for (const auto& clause : op_where) {
            sql << "AND " << clause << " ";
        }
        
        sql << ") AS mapped ";
        
        // GROUP BY for outer query
        if (has_group_by) {
            sql << "GROUP BY " << compile_group_by_for_mapped(plan.operations) << " ";
        }
        
    } else {
        // Build SELECT clause
        if (has_aggregations || has_group_by) {
            sql << "SELECT ";
            sql << compile_select_fields(plan.operations, has_aggregations);
            sql << " ";  // Space before FROM
        } else if (!distinct_field.empty()) {
            // DISTINCT operation - use DISTINCT ON
            sql << "SELECT DISTINCT ON (" << jsonb_path_to_sql(distinct_field) << ") ";
            sql << "m.id, m.transaction_id, m.partition_id, m.payload, "
                << "m.created_at, m.trace_id, q.name as queue_name, p.name as partition_name ";
        } else {
            if (has_map) {
                sql << "SELECT ";
                sql << compile_select_fields(plan.operations, has_aggregations);
                sql << " ";
            } else {
                sql << "SELECT m.id, m.transaction_id, m.partition_id, m.payload, "
                    << "m.created_at, m.trace_id, q.name as queue_name, p.name as partition_name ";
            }
        }
        
        // Build FROM clause
        sql << compile_source(plan);
        
        // Build WHERE clause
        sql << "WHERE q.name = '" << escape_identifier(plan.source) << "' ";
        
        // Add partition filter if specified
        if (plan.partition.has_value()) {
            sql << "AND p.name = '" << escape_identifier(plan.partition.value()) << "' ";
        }
        
        // Add consumer filter (only unconsumed messages)
        sql << "AND " << compile_consumer_filter(plan);
        
        // Add operation-specific filters
        auto op_where = compile_where_clauses(plan.operations);
        for (const auto& clause : op_where) {
            sql << "AND " << clause << " ";
        }
        
        // Build GROUP BY clause
        if (has_group_by) {
            std::string group_by = compile_group_by(plan.operations);
            if (!group_by.empty()) {
                sql << "GROUP BY " << group_by << " ";
            }
        }
        
        // Build ORDER BY clause (for consistent results)
        if (!has_aggregations) {
            if (!distinct_field.empty()) {
                // DISTINCT ON requires ORDER BY the same field
                sql << "ORDER BY " << jsonb_path_to_sql(distinct_field) << ", m.created_at, m.id ";
            } else {
                sql << compile_order_by();
            }
        }
        
        // Build LIMIT clause
        std::string limit = compile_limit(plan.operations);
        if (!limit.empty()) {
            sql << "LIMIT " << limit;
        } else {
            sql << "LIMIT " << plan.batch_size;
        }
    }
    
    result.sql = sql.str();
    
    spdlog::debug("Compiled SQL: {}", result.sql);
    
    return result;
}

std::string SQLCompiler::compile_source(const ExecutionPlan& plan) {
    std::stringstream ss;
    
    ss << "FROM queen.messages m "
       << "JOIN queen.partitions p ON p.id = m.partition_id "
       << "JOIN queen.queues q ON q.id = p.queue_id "
       << "LEFT JOIN queen.partition_consumers pc "
       << "  ON pc.partition_id = p.id "
       << "  AND pc.consumer_group = '" << escape_identifier(plan.consumer_group) << "' ";
    
    return ss.str();
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

std::vector<std::string> SQLCompiler::compile_where_clauses(const std::vector<Operation>& ops) {
    std::vector<std::string> clauses;
    
    for (const auto& op : ops) {
        if (op.type == Operation::Type::FILTER && op.predicate.has_value()) {
            clauses.push_back(op.predicate->to_sql());
        } else if (op.type == Operation::Type::DISTINCT && op.distinct_field.has_value()) {
            // DISTINCT will be handled in SELECT with DISTINCT ON
            // For now, we'll handle it post-processing
        }
    }
    
    return clauses;
}

std::string SQLCompiler::compile_select_fields(const std::vector<Operation>& ops, bool& has_aggregations) {
    std::stringstream ss;
    std::vector<std::string> fields;
    
    // Find groupBy, aggregate, and map operations
    std::vector<std::string> group_keys;
    nlohmann::json aggregations;
    nlohmann::json map_fields;
    
    for (const auto& op : ops) {
        if (op.type == Operation::Type::GROUP_BY) {
            group_keys = op.group_by_keys;
        } else if (op.type == Operation::Type::AGGREGATE) {
            aggregations = op.aggregations;
            has_aggregations = true;
        } else if (op.type == Operation::Type::MAP) {
            map_fields = op.fields;
        }
    }
    
    // If we have group by, include group keys
    if (!group_keys.empty()) {
        for (const auto& key : group_keys) {
            std::string sql_key = jsonb_path_to_sql(key);
            std::string alias = key;
            
            // Extract just the field name for alias
            size_t last_dot = alias.rfind('.');
            if (last_dot != std::string::npos) {
                alias = alias.substr(last_dot + 1);
            }
            
            fields.push_back(sql_key + " AS " + escape_identifier(alias));
        }
    }
    
    // If we have aggregations, add them
    if (!aggregations.empty() && aggregations.is_object()) {
        for (auto it = aggregations.begin(); it != aggregations.end(); ++it) {
            std::string name = it.key();
            const auto& agg_spec = it.value();
            
            std::stringstream agg_sql;
            
            if (agg_spec.contains("$count")) {
                agg_sql << "COUNT(*)";
            } else if (agg_spec.contains("$sum")) {
                std::string field = agg_spec["$sum"].get<std::string>();
                // Check if field references a mapped field (no payload prefix)
                if (field.find("payload.") != 0 && !map_fields.empty()) {
                    // It's a reference to a mapped field, use it directly (lowercase)
                    std::string field_lower = field;
                    std::transform(field_lower.begin(), field_lower.end(), field_lower.begin(), ::tolower);
                    agg_sql << "SUM(" << escape_identifier(field_lower) << "::numeric)";
                } else {
                    agg_sql << "SUM(" << jsonb_path_to_sql(field) << "::numeric)";
                }
            } else if (agg_spec.contains("$avg")) {
                std::string field = agg_spec["$avg"].get<std::string>();
                if (field.find("payload.") != 0 && !map_fields.empty()) {
                    std::string field_lower = field;
                    std::transform(field_lower.begin(), field_lower.end(), field_lower.begin(), ::tolower);
                    agg_sql << "AVG(" << escape_identifier(field_lower) << "::numeric)";
                } else {
                    agg_sql << "AVG(" << jsonb_path_to_sql(field) << "::numeric)";
                }
            } else if (agg_spec.contains("$min")) {
                std::string field = agg_spec["$min"].get<std::string>();
                if (field.find("payload.") != 0 && !map_fields.empty()) {
                    std::string field_lower = field;
                    std::transform(field_lower.begin(), field_lower.end(), field_lower.begin(), ::tolower);
                    agg_sql << "MIN(" << escape_identifier(field_lower) << "::numeric)";
                } else {
                    agg_sql << "MIN(" << jsonb_path_to_sql(field) << "::numeric)";
                }
            } else if (agg_spec.contains("$max")) {
                std::string field = agg_spec["$max"].get<std::string>();
                if (field.find("payload.") != 0 && !map_fields.empty()) {
                    std::string field_lower = field;
                    std::transform(field_lower.begin(), field_lower.end(), field_lower.begin(), ::tolower);
                    agg_sql << "MAX(" << escape_identifier(field_lower) << "::numeric)";
                } else {
                    agg_sql << "MAX(" << jsonb_path_to_sql(field) << "::numeric)";
                }
            }
            
            agg_sql << " AS " << escape_identifier(name);
            fields.push_back(agg_sql.str());
        }
    } else if (!map_fields.empty() && map_fields.is_object()) {
        // If we have map but no aggregations
        for (auto it = map_fields.begin(); it != map_fields.end(); ++it) {
            std::string name = it.key();
            std::string source = it.value().get<std::string>();
            
            std::string sql_field = jsonb_path_to_sql(source);
            
            // Use lowercase alias to match PostgreSQL behavior
            std::string alias = name;
            std::transform(alias.begin(), alias.end(), alias.begin(), ::tolower);
            
            fields.push_back(sql_field + " AS " + escape_identifier(alias));
        }
    }
    
    // Join all fields
    for (size_t i = 0; i < fields.size(); i++) {
        if (i > 0) ss << ", ";
        ss << fields[i];
    }
    
    return ss.str();
}

std::string SQLCompiler::compile_select_fields_for_map(const std::vector<Operation>& ops) {
    // Get map fields for the inner subquery
    for (const auto& op : ops) {
        if (op.type == Operation::Type::MAP && !op.fields.empty() && op.fields.is_object()) {
            std::stringstream ss;
            int field_count = 0;
            
            for (auto it = op.fields.begin(); it != op.fields.end(); ++it) {
                if (field_count > 0) ss << ", ";
                
                std::string name = it.key();
                std::string source = it.value().get<std::string>();
                std::string sql_field = jsonb_path_to_sql(source);
                
                // Use lowercase alias to match PostgreSQL behavior
                std::string alias = name;
                std::transform(alias.begin(), alias.end(), alias.begin(), ::tolower);
                
                ss << sql_field << " AS " << escape_identifier(alias);
                field_count++;
            }
            
            return ss.str();
        }
    }
    return "";
}

std::string SQLCompiler::compile_select_fields_for_subquery(const std::vector<Operation>& ops, bool& has_aggregations) {
    // Build SELECT for outer query when we have a subquery (map + groupBy/aggregate)
    // In this context, group keys and aggregate fields reference mapped aliases
    std::stringstream ss;
    std::vector<std::string> fields;
    
    std::vector<std::string> group_keys;
    nlohmann::json aggregations;
    
    for (const auto& op : ops) {
        if (op.type == Operation::Type::GROUP_BY) {
            group_keys = op.group_by_keys;
        } else if (op.type == Operation::Type::AGGREGATE) {
            aggregations = op.aggregations;
            has_aggregations = true;
        }
    }
    
    // Add group keys (use mapped field names directly, lowercase)
    for (const auto& key : group_keys) {
        std::string key_lower = key;
        std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
        fields.push_back(escape_identifier(key_lower));
    }
    
    // Add aggregations (reference mapped field names directly, lowercase)
    if (!aggregations.empty() && aggregations.is_object()) {
        for (auto it = aggregations.begin(); it != aggregations.end(); ++it) {
            std::string name = it.key();
            const auto& agg_spec = it.value();
            
            std::stringstream agg_sql;
            
            if (agg_spec.contains("$count")) {
                agg_sql << "COUNT(*)";
            } else if (agg_spec.contains("$sum")) {
                std::string field = agg_spec["$sum"].get<std::string>();
                std::string field_lower = field;
                std::transform(field_lower.begin(), field_lower.end(), field_lower.begin(), ::tolower);
                agg_sql << "SUM(" << escape_identifier(field_lower) << "::numeric)";
            } else if (agg_spec.contains("$avg")) {
                std::string field = agg_spec["$avg"].get<std::string>();
                std::string field_lower = field;
                std::transform(field_lower.begin(), field_lower.end(), field_lower.begin(), ::tolower);
                agg_sql << "AVG(" << escape_identifier(field_lower) << "::numeric)";
            } else if (agg_spec.contains("$min")) {
                std::string field = agg_spec["$min"].get<std::string>();
                std::string field_lower = field;
                std::transform(field_lower.begin(), field_lower.end(), field_lower.begin(), ::tolower);
                agg_sql << "MIN(" << escape_identifier(field_lower) << "::numeric)";
            } else if (agg_spec.contains("$max")) {
                std::string field = agg_spec["$max"].get<std::string>();
                std::string field_lower = field;
                std::transform(field_lower.begin(), field_lower.end(), field_lower.begin(), ::tolower);
                agg_sql << "MAX(" << escape_identifier(field_lower) << "::numeric)";
            }
            
            agg_sql << " AS " << escape_identifier(name);
            fields.push_back(agg_sql.str());
        }
    }
    
    // Join all fields
    for (size_t i = 0; i < fields.size(); i++) {
        if (i > 0) ss << ", ";
        ss << fields[i];
    }
    
    return ss.str();
}

std::string SQLCompiler::compile_group_by_for_mapped(const std::vector<Operation>& ops) {
    // GroupBy on mapped fields (use the alias names directly)
    for (const auto& op : ops) {
        if (op.type == Operation::Type::GROUP_BY) {
            std::stringstream ss;
            for (size_t i = 0; i < op.group_by_keys.size(); i++) {
                if (i > 0) ss << ", ";
                
                // Use the key directly (it's already the mapped field name)
                std::string key = op.group_by_keys[i];
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                ss << escape_identifier(key);
            }
            return ss.str();
        }
    }
    return "";
}

std::string SQLCompiler::compile_group_by(const std::vector<Operation>& ops) {
    for (const auto& op : ops) {
        if (op.type == Operation::Type::GROUP_BY) {
            std::stringstream ss;
            for (size_t i = 0; i < op.group_by_keys.size(); i++) {
                if (i > 0) ss << ", ";
                ss << jsonb_path_to_sql(op.group_by_keys[i]);
            }
            return ss.str();
        }
    }
    return "";
}

std::string SQLCompiler::compile_order_by() {
    return "ORDER BY m.created_at, m.id ";
}

std::string SQLCompiler::compile_limit(const std::vector<Operation>& ops) {
    for (const auto& op : ops) {
        if (op.type == Operation::Type::LIMIT && op.limit_value.has_value()) {
            return std::to_string(op.limit_value.value());
        }
    }
    return "";
}

std::string SQLCompiler::jsonb_path_to_sql(const std::string& path) {
    // Convert "payload.userId" to "(m.payload->>'userId')"
    // Convert "created_at" to "m.created_at"
    
    if (path == "created_at") {
        return "m.created_at";
    }
    
    if (path.find("payload.") == 0) {
        std::string field = path.substr(8); // Remove "payload."
        
        // Check if there are nested fields
        size_t dot_pos = field.find('.');
        if (dot_pos != std::string::npos) {
            // Nested: payload.user.name -> m.payload->'user'->>'name'
            std::string first = field.substr(0, dot_pos);
            std::string rest = field.substr(dot_pos + 1);
            
            return "(m.payload->'" + first + "'->>" + quote_literal(rest) + ")";
        } else {
            // Simple: payload.userId -> m.payload->>'userId'
            return "(m.payload->>'" + field + "')";
        }
    }
    
    // Default: assume it's a column name
    return "m." + path;
}

std::string SQLCompiler::escape_identifier(const std::string& identifier) {
    // Simple escaping - just check for dangerous characters
    std::string safe = identifier;
    
    // Remove any quotes
    safe.erase(std::remove(safe.begin(), safe.end(), '\''), safe.end());
    safe.erase(std::remove(safe.begin(), safe.end(), '"'), safe.end());
    
    return safe;
}

std::string SQLCompiler::quote_literal(const std::string& literal) {
    std::string escaped = literal;
    size_t pos = 0;
    while ((pos = escaped.find("'", pos)) != std::string::npos) {
        escaped.replace(pos, 1, "''");
        pos += 2;
    }
    return "'" + escaped + "'";
}

// ========== StreamExecutor Implementation ==========

StreamExecutor::StreamExecutor(std::shared_ptr<DatabasePool> db_pool)
    : db_pool_(db_pool) {
}

StreamQueryResult StreamExecutor::execute(const ExecutionPlan& plan, const CompiledQuery& compiled) {
    StreamQueryResult result;
    
    try {
        ScopedConnection conn(db_pool_.get());
        
        spdlog::debug("Executing stream query for queue: {}", plan.source);
        
        // Execute the query
        auto query_result = QueryResult(conn->exec(compiled.sql));
        
        if (!query_result.is_success()) {
            spdlog::error("Stream query failed: {}", query_result.error_message());
            throw std::runtime_error("Query execution failed: " + query_result.error_message());
        }
        
        int num_rows = query_result.num_rows();
        spdlog::debug("Stream query returned {} rows", num_rows);
        
        // Process results
        for (int i = 0; i < num_rows; i++) {
            nlohmann::json row;
            
            // Get all column names and values
            int num_fields = query_result.num_fields();
            for (int j = 0; j < num_fields; j++) {
                // Get column name using libpq directly
                const char* col_name_ptr = PQfname(query_result.get_result(), j);
                std::string col_name = col_name_ptr ? std::string(col_name_ptr) : "";
                std::string value = query_result.get_value(i, j);
                
                // Try to parse as JSON if it looks like JSON
                if (!value.empty() && (value[0] == '{' || value[0] == '[')) {
                    try {
                        row[col_name] = nlohmann::json::parse(value);
                    } catch (...) {
                        row[col_name] = value;
                    }
                } else if (value.empty() || value == "NULL") {
                    row[col_name] = nullptr;
                } else {
                    // Try to parse as number
                    try {
                        if (value.find('.') != std::string::npos) {
                            row[col_name] = std::stod(value);
                        } else {
                            row[col_name] = std::stoll(value);
                        }
                    } catch (...) {
                        row[col_name] = value;
                    }
                }
            }
            
            result.messages.push_back(row);
        }
        
        // Update consumer offset if we got results and auto_ack is true
        if (plan.auto_ack && !result.messages.empty() && !compiled.has_aggregations) {
            auto& last = result.messages.back();
            
            if (last.contains("id") && last.contains("created_at")) {
                std::string last_id = last["id"].is_string() ? 
                    last["id"].get<std::string>() : last["id"].dump();
                std::string last_ts = last["created_at"].is_string() ? 
                    last["created_at"].get<std::string>() : last["created_at"].dump();
                
                update_consumer_offset(plan, last_id, last_ts);
                
                result.checkpoint_message_id = last_id;
                result.checkpoint_timestamp = last_ts;
            }
        }
        
        result.has_more = (num_rows == plan.batch_size);
        
    } catch (const std::exception& e) {
        spdlog::error("Stream execution error: {}", e.what());
        throw;
    }
    
    return result;
}

void StreamExecutor::update_consumer_offset(
    const ExecutionPlan& plan,
    const std::string& last_message_id,
    const std::string& last_timestamp
) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        // This is a simplified version - just log for now
        spdlog::debug("Would update consumer offset for {}/{} to message {}",
                     plan.source, plan.consumer_group, last_message_id);
        
        // TODO: Implement actual consumer offset update
        // This would update the partition_consumers table
        
    } catch (const std::exception& e) {
        spdlog::warn("Failed to update consumer offset: {}", e.what());
    }
}

// ========== StreamManager Implementation ==========

StreamManager::StreamManager(std::shared_ptr<DatabasePool> db_pool)
    : db_pool_(db_pool),
      compiler_(std::make_unique<SQLCompiler>()),
      executor_(std::make_unique<StreamExecutor>(db_pool)) {
    
    spdlog::info("StreamManager initialized");
}

nlohmann::json StreamManager::execute_query(const nlohmann::json& plan_json) {
    try {
        spdlog::debug("Received stream query request");
        
        // Parse execution plan
        auto plan = ExecutionPlan::from_json(plan_json);
        
        // Validate plan
        validate_plan(plan);
        
        // Compile to SQL
        auto compiled = compiler_->compile(plan);
        
        // Execute query
        auto result = executor_->execute(plan, compiled);
        
        // Build response
        nlohmann::json response;
        response["messages"] = result.messages;
        response["hasMore"] = result.has_more;
        
        if (result.checkpoint_message_id.has_value()) {
            response["checkpoint"] = {
                {"lastMessageId", result.checkpoint_message_id.value()},
                {"lastTimestamp", result.checkpoint_timestamp.value_or("")}
            };
        }
        
        spdlog::debug("Stream query completed, returned {} messages", result.messages.size());
        
        return response;
        
    } catch (const std::exception& e) {
        spdlog::error("Stream query error: {}", e.what());
        
        nlohmann::json error_response;
        error_response["error"] = e.what();
        error_response["messages"] = nlohmann::json::array();
        
        return error_response;
    }
}

CompiledQuery StreamManager::compile_plan(const ExecutionPlan& plan) {
    validate_plan(plan);
    return compiler_->compile(plan);
}

void StreamManager::validate_plan(const ExecutionPlan& plan) {
    plan.validate();
    
    // Additional security validation
    if (plan.source.find("'") != std::string::npos ||
        plan.source.find(";") != std::string::npos ||
        plan.source.find("--") != std::string::npos) {
        throw std::runtime_error("Invalid characters in source queue name");
    }
    
    if (plan.consumer_group.find("'") != std::string::npos ||
        plan.consumer_group.find(";") != std::string::npos) {
        throw std::runtime_error("Invalid characters in consumer group name");
    }
}

} // namespace queen

