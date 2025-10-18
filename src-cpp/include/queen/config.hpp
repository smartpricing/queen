#pragma once

#include <string>
#include <cstdlib>

namespace queen {

struct DatabaseConfig {
    std::string host = "localhost";
    std::string port = "5432";
    std::string database = "postgres";
    std::string user = "postgres";
    std::string password = "postgres";
    int pool_size = 150;
    int connection_timeout = 2000;
    int statement_timeout = 30000;
    
    static DatabaseConfig from_env() {
        DatabaseConfig config;
        if (const char* host = std::getenv("PG_HOST")) config.host = host;
        if (const char* port = std::getenv("PG_PORT")) config.port = port;
        if (const char* db = std::getenv("PG_DB")) config.database = db;
        if (const char* user = std::getenv("PG_USER")) config.user = user;
        if (const char* pass = std::getenv("PG_PASSWORD")) config.password = pass;
        if (const char* pool = std::getenv("DB_POOL_SIZE")) config.pool_size = std::atoi(pool);
        return config;
    }
    
    std::string connection_string() const {
        return "host=" + host + " port=" + port + " dbname=" + database + 
               " user=" + user + " password=" + password;
    }
};

struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 6632;
    std::string worker_id = "cpp-worker-1";
    bool dev_mode = false;
    
    static ServerConfig from_env() {
        ServerConfig config;
        if (const char* host = std::getenv("HOST")) config.host = host;
        if (const char* port = std::getenv("PORT")) config.port = std::atoi(port);
        if (const char* worker = std::getenv("WORKER_ID")) config.worker_id = worker;
        return config;
    }
};

struct QueueConfig {
    int default_timeout = 30000;
    int max_timeout = 60000;
    int default_batch_size = 1;
    int batch_insert_size = 1000;
    int poll_interval = 100;
    int max_poll_interval = 2000;
    int lease_reclaim_interval = 5000;
    
    static QueueConfig from_env() {
        QueueConfig config;
        if (const char* timeout = std::getenv("DEFAULT_TIMEOUT")) 
            config.default_timeout = std::atoi(timeout);
        if (const char* batch = std::getenv("DEFAULT_BATCH_SIZE")) 
            config.default_batch_size = std::atoi(batch);
        return config;
    }
};

struct Config {
    DatabaseConfig database;
    ServerConfig server;
    QueueConfig queue;
    
    static Config load() {
        Config config;
        config.database = DatabaseConfig::from_env();
        config.server = ServerConfig::from_env();
        config.queue = QueueConfig::from_env();
        return config;
    }
};

} // namespace queen
