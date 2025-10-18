#pragma once

#include <libpq-fe.h>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <json.hpp>

namespace queen {

class DatabaseConnection {
private:
    PGconn* conn_;
    bool in_use_;
    
public:
    explicit DatabaseConnection(const std::string& connection_string);
    ~DatabaseConnection();
    
    // Non-copyable, movable
    DatabaseConnection(const DatabaseConnection&) = delete;
    DatabaseConnection& operator=(const DatabaseConnection&) = delete;
    DatabaseConnection(DatabaseConnection&& other) noexcept;
    DatabaseConnection& operator=(DatabaseConnection&& other) noexcept;
    
    bool is_valid() const;
    bool is_in_use() const { return in_use_; }
    void set_in_use(bool in_use) { in_use_ = in_use; }
    
    PGresult* exec(const std::string& query);
    PGresult* exec_params(const std::string& query, const std::vector<std::string>& params);
    
    // Transaction support
    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();
};

class DatabasePool {
private:
    std::queue<std::unique_ptr<DatabaseConnection>> available_connections_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::string connection_string_;
    size_t pool_size_;
    size_t current_size_;
    
    std::unique_ptr<DatabaseConnection> create_connection();
    
public:
    explicit DatabasePool(const std::string& connection_string, size_t pool_size = 10);
    ~DatabasePool();
    
    std::unique_ptr<DatabaseConnection> get_connection();
    void return_connection(std::unique_ptr<DatabaseConnection> conn);
    
    // Convenience methods
    PGresult* query(const std::string& sql);
    PGresult* query_params(const std::string& sql, const std::vector<std::string>& params);
    
    size_t size() const { return current_size_; }
    size_t available() const;
};

// RAII connection wrapper
class ScopedConnection {
private:
    DatabasePool* pool_;
    std::unique_ptr<DatabaseConnection> conn_;
    
public:
    ScopedConnection(DatabasePool* pool);
    ~ScopedConnection();
    
    DatabaseConnection* operator->() { return conn_.get(); }
    DatabaseConnection& operator*() { return *conn_; }
    bool is_valid() const { return conn_ && conn_->is_valid(); }
};

// Result wrapper for easier handling
class QueryResult {
private:
    PGresult* result_;
    
public:
    explicit QueryResult(PGresult* result) : result_(result) {}
    ~QueryResult() { if (result_) PQclear(result_); }
    
    // Non-copyable, movable
    QueryResult(const QueryResult&) = delete;
    QueryResult& operator=(const QueryResult&) = delete;
    QueryResult(QueryResult&& other) noexcept : result_(other.result_) { other.result_ = nullptr; }
    QueryResult& operator=(QueryResult&& other) noexcept {
        if (this != &other) {
            if (result_) PQclear(result_);
            result_ = other.result_;
            other.result_ = nullptr;
        }
        return *this;
    }
    
    bool is_valid() const { return result_ != nullptr; }
    bool is_success() const { 
        return result_ && (PQresultStatus(result_) == PGRES_COMMAND_OK || 
                          PQresultStatus(result_) == PGRES_TUPLES_OK);
    }
    
    int num_rows() const { return result_ ? PQntuples(result_) : 0; }
    int num_fields() const { return result_ ? PQnfields(result_) : 0; }
    
    std::string get_value(int row, int col) const {
        if (!result_ || row >= num_rows() || col >= num_fields()) return "";
        const char* val = PQgetvalue(result_, row, col);
        return val ? std::string(val) : "";
    }
    
    std::string get_value(int row, const std::string& field_name) const {
        if (!result_) return "";
        int col = PQfnumber(result_, field_name.c_str());
        if (col == -1) return "";
        return get_value(row, col);
    }
    
    bool is_null(int row, int col) const {
        return result_ && PQgetisnull(result_, row, col);
    }
    
    std::string error_message() const {
        return result_ ? PQresultErrorMessage(result_) : "No result";
    }
    
    // Convert row to JSON
    nlohmann::json row_to_json(int row) const;
    
    // Convert all rows to JSON array
    nlohmann::json to_json() const;
};

} // namespace queen
