#include "queen/database.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <chrono>
#include <thread>

namespace queen {

// DatabaseConnection Implementation
DatabaseConnection::DatabaseConnection(const std::string& connection_string,
                                     int statement_timeout_ms,
                                     int lock_timeout_ms,
                                     int idle_in_transaction_timeout_ms) 
    : conn_(nullptr), in_use_(false) {
    conn_ = PQconnectdb(connection_string.c_str());
    
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string error = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("Failed to connect to database: " + error);
    }
    
    // Set client encoding to UTF8
    PQsetClientEncoding(conn_, "UTF8");
    
    // CRITICAL: Set timeout parameters via SET commands (works with PgBouncer)
    // These prevent connections from being killed by server-side timeouts
    std::string set_timeouts = 
        "SET statement_timeout = " + std::to_string(statement_timeout_ms) + "; " +
        "SET lock_timeout = " + std::to_string(lock_timeout_ms) + "; " +
        "SET idle_in_transaction_session_timeout = " + std::to_string(idle_in_transaction_timeout_ms) + ";";
    
    PGresult* result = PQexec(conn_, set_timeouts.c_str());
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        std::string error = PQerrorMessage(conn_);
        PQclear(result);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("Failed to set timeout parameters: " + error);
    }
    PQclear(result);
}

DatabaseConnection::~DatabaseConnection() {
    if (conn_) {
        PQfinish(conn_);
    }
}

DatabaseConnection::DatabaseConnection(DatabaseConnection&& other) noexcept 
    : conn_(other.conn_), in_use_(other.in_use_) {
    other.conn_ = nullptr;
    other.in_use_ = false;
}

DatabaseConnection& DatabaseConnection::operator=(DatabaseConnection&& other) noexcept {
    if (this != &other) {
        if (conn_) PQfinish(conn_);
        conn_ = other.conn_;
        in_use_ = other.in_use_;
        other.conn_ = nullptr;
        other.in_use_ = false;
    }
    return *this;
}

bool DatabaseConnection::is_valid() const {
    return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

PGresult* DatabaseConnection::exec(const std::string& query) {
    if (!is_valid()) return nullptr;
    return PQexec(conn_, query.c_str());
}

PGresult* DatabaseConnection::exec_params(const std::string& query, const std::vector<std::string>& params) {
    if (!is_valid()) return nullptr;
    
    std::vector<const char*> param_values;
    param_values.reserve(params.size());
    
    for (const auto& param : params) {
        param_values.push_back(param.c_str());
    }
    
    return PQexecParams(conn_, query.c_str(), static_cast<int>(params.size()),
                       nullptr, param_values.data(), nullptr, nullptr, 0);
}

bool DatabaseConnection::begin_transaction() {
    auto result = QueryResult(exec("BEGIN"));
    return result.is_success();
}

bool DatabaseConnection::commit_transaction() {
    auto result = QueryResult(exec("COMMIT"));
    return result.is_success();
}

bool DatabaseConnection::rollback_transaction() {
    auto result = QueryResult(exec("ROLLBACK"));
    return result.is_success();
}

// DatabasePool Implementation
DatabasePool::DatabasePool(const std::string& connection_string, 
                         size_t pool_size, 
                         int acquisition_timeout_ms,
                         int statement_timeout_ms,
                         int lock_timeout_ms,
                         int idle_in_transaction_timeout_ms)
    : available_connections_(), mutex_(), condition_(), 
      connection_string_(connection_string), 
      pool_size_(pool_size), 
      current_size_(0), 
      acquisition_timeout_ms_(acquisition_timeout_ms),
      statement_timeout_ms_(statement_timeout_ms),
      lock_timeout_ms_(lock_timeout_ms),
      idle_in_transaction_timeout_ms_(idle_in_transaction_timeout_ms) {
    
    // Pre-populate the pool
    for (size_t i = 0; i < pool_size_; ++i) {
        try {
            auto conn = create_connection();
            if (conn && conn->is_valid()) {
                available_connections_.push(std::move(conn));
                ++current_size_;
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to create initial database connection: {}", e.what());
        }
    }
    
    if (current_size_ == 0) {
        throw std::runtime_error("Failed to create any database connections");
    }
    
    spdlog::info("Database pool initialized with {}/{} connections (acquisition timeout: {}ms, statement timeout: {}ms)", 
                current_size_, pool_size_, acquisition_timeout_ms_, statement_timeout_ms_);
}

DatabasePool::~DatabasePool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!available_connections_.empty()) {
        available_connections_.pop();
    }
}

std::unique_ptr<DatabaseConnection> DatabasePool::create_connection() {
    return std::make_unique<DatabaseConnection>(connection_string_,
                                                statement_timeout_ms_,
                                                lock_timeout_ms_,
                                                idle_in_transaction_timeout_ms_);
}

std::unique_ptr<DatabaseConnection> DatabasePool::get_connection() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Wait for available connection or timeout
    if (!condition_.wait_for(lock, std::chrono::milliseconds(acquisition_timeout_ms_), 
                            [this] { return !available_connections_.empty(); })) {
        
        // CRITICAL FIX: Pool is empty - try to create a new connection instead of just failing
        // This allows recovery when PostgreSQL comes back online
        spdlog::warn("Pool timeout - attempting to create new connection (pool: {}/{})", current_size_, pool_size_);
        
        try {
            lock.unlock();
            auto new_conn = create_connection();
            lock.lock();
            
            if (new_conn && new_conn->is_valid()) {
                ++current_size_;
                new_conn->set_in_use(true);
                spdlog::info("Created new connection during timeout, pool now {}/{}", current_size_, pool_size_);
                return new_conn;
            } else {
                throw std::runtime_error("Failed to create connection - database may be down");
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to create connection on timeout: {}", e.what());
            throw std::runtime_error("Database connection pool timeout (waited " + std::to_string(acquisition_timeout_ms_) + "ms)");
        }
    }
    
    auto conn = std::move(available_connections_.front());
    available_connections_.pop();
    
    // Verify connection is still valid
    if (!conn->is_valid()) {
        spdlog::warn("Invalid connection found in pool, replacing it");
        --current_size_;
        
        // Try to create a replacement connection
        try {
            lock.unlock();
            auto new_conn = create_connection();
            lock.lock();
            
            if (new_conn && new_conn->is_valid()) {
                ++current_size_;
                new_conn->set_in_use(true);
                return new_conn;
            } else {
                spdlog::error("Failed to create replacement connection, pool size reduced to {}/{}", current_size_, pool_size_);
                throw std::runtime_error("Failed to create valid database connection");
            }
        } catch (const std::exception& e) {
            spdlog::error("Exception creating replacement connection: {}", e.what());
            throw;
        }
    }
    
    conn->set_in_use(true);
    return conn;
}

void DatabasePool::return_connection(std::unique_ptr<DatabaseConnection> conn) {
    if (!conn) return;
    
    conn->set_in_use(false);
    
    std::lock_guard<std::mutex> lock(mutex_);
    if (conn->is_valid()) {
        available_connections_.push(std::move(conn));
        condition_.notify_one();
        
        // POOL REFILL: If pool is below target size, try to create more connections
        // This helps recovery when PostgreSQL comes back after being down
        if (current_size_ < pool_size_) {
            size_t to_create = pool_size_ - current_size_;
            spdlog::info("Pool below target ({}/{}), attempting to create {} connections", 
                        current_size_, pool_size_, to_create);
            
            for (size_t i = 0; i < to_create; ++i) {
                try {
                    auto new_conn = create_connection();
                    if (new_conn && new_conn->is_valid()) {
                        available_connections_.push(std::move(new_conn));
                        ++current_size_;
                        spdlog::info("Refilled pool: {}/{}", current_size_, pool_size_);
                    } else {
                        // Failed to create - probably DB still having issues
                        break;
                    }
                } catch (const std::exception& e) {
                    // Failed to create - DB might still be down
                    spdlog::debug("Failed to refill pool: {}", e.what());
                    break;
                }
            }
            condition_.notify_all();
        }
    } else {
        spdlog::warn("Returned invalid connection to pool, attempting to create replacement");
        --current_size_;
        
        // Try to create a replacement connection in the background
        // We do this asynchronously to avoid blocking the caller
        try {
            auto new_conn = create_connection();
            if (new_conn && new_conn->is_valid()) {
                available_connections_.push(std::move(new_conn));
                ++current_size_;
                condition_.notify_one();
                spdlog::info("Successfully replaced invalid connection, pool at {}/{}", current_size_, pool_size_);
            } else {
                spdlog::error("Failed to create replacement connection, pool size reduced to {}/{}", current_size_, pool_size_);
                condition_.notify_one(); // Still notify in case threads are waiting
            }
        } catch (const std::exception& e) {
            spdlog::error("Exception creating replacement connection: {} - pool size now {}/{}", e.what(), current_size_, pool_size_);
            condition_.notify_one(); // Still notify in case threads are waiting
        }
    }
}

PGresult* DatabasePool::query(const std::string& sql) {
    auto conn = get_connection();
    try {
        auto result = conn->exec(sql);
        return_connection(std::move(conn));
        return result;
    } catch (...) {
        return_connection(std::move(conn));
        throw;
    }
}

PGresult* DatabasePool::query_params(const std::string& sql, const std::vector<std::string>& params) {
    auto conn = get_connection();
    try {
        auto result = conn->exec_params(sql, params);
        return_connection(std::move(conn));
        return result;
    } catch (...) {
        return_connection(std::move(conn));
        throw;
    }
}

size_t DatabasePool::available() const {
    std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return available_connections_.size();
}

// ScopedConnection Implementation
ScopedConnection::ScopedConnection(DatabasePool* pool) : pool_(pool) {
    if (!pool_) {
        throw std::invalid_argument("Database pool cannot be null");
    }
    conn_ = pool_->get_connection();
}

ScopedConnection::~ScopedConnection() {
    if (pool_ && conn_) {
        pool_->return_connection(std::move(conn_));
    }
}

// QueryResult Implementation
nlohmann::json QueryResult::row_to_json(int row) const {
    if (!result_ || row >= num_rows()) {
        return nlohmann::json::object();
    }
    
    nlohmann::json json_row = nlohmann::json::object();
    
    for (int col = 0; col < num_fields(); ++col) {
        const char* field_name = PQfname(result_, col);
        if (!field_name) continue;
        
        if (is_null(row, col)) {
            json_row[field_name] = nullptr;
        } else {
            std::string value = get_value(row, col);
            
            // Try to determine the PostgreSQL type and convert accordingly
            Oid type_oid = PQftype(result_, col);
            
            switch (type_oid) {
                case 16: // bool
                    json_row[field_name] = (value == "t" || value == "true");
                    break;
                case 20: // int8 (bigint)
                case 21: // int2 (smallint)  
                case 23: // int4 (integer)
                    try {
                        json_row[field_name] = std::stoll(value);
                    } catch (...) {
                        json_row[field_name] = value;
                    }
                    break;
                case 700: // float4
                case 701: // float8
                case 1700: // numeric
                    try {
                        json_row[field_name] = std::stod(value);
                    } catch (...) {
                        json_row[field_name] = value;
                    }
                    break;
                case 114: // json
                case 3802: // jsonb
                    try {
                        json_row[field_name] = nlohmann::json::parse(value);
                    } catch (...) {
                        json_row[field_name] = value;
                    }
                    break;
                default:
                    json_row[field_name] = value;
                    break;
            }
        }
    }
    
    return json_row;
}

nlohmann::json QueryResult::to_json() const {
    nlohmann::json json_array = nlohmann::json::array();
    
    for (int row = 0; row < num_rows(); ++row) {
        json_array.push_back(row_to_json(row));
    }
    
    return json_array;
}

} // namespace queen
