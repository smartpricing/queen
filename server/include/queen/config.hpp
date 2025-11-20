#pragma once

#include <string>
#include <cstdlib>
#include <cstring>

namespace queen {

// Helper function to get boolean from environment
inline bool get_env_bool(const char* name, bool default_value) {
    const char* value = std::getenv(name);
    if (!value) return default_value;
    return std::strcmp(value, "true") == 0;
}

// Helper function to get int from environment
inline int get_env_int(const char* name, int default_value) {
    const char* value = std::getenv(name);
    return value ? std::atoi(value) : default_value;
}

// Helper function to get double from environment
inline double get_env_double(const char* name, double default_value) {
    const char* value = std::getenv(name);
    return value ? std::atof(value) : default_value;
}

// Helper function to get string from environment
inline std::string get_env_string(const char* name, const std::string& default_value) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : default_value;
}

struct ServerConfig {
    int port = 6632;
    std::string host = "0.0.0.0";
    std::string worker_id = "cpp-worker-1";
    std::string application_name = "queen-mq";
    bool dev_mode = false;
    int num_workers = 10;  // Number of worker threads
    
    static ServerConfig from_env() {
        ServerConfig config;
        config.port = get_env_int("PORT", 6632);
        config.host = get_env_string("HOST", "0.0.0.0");
        config.worker_id = get_env_string("WORKER_ID", "cpp-worker-1");
        config.application_name = get_env_string("APP_NAME", "queen-mq");
        config.num_workers = get_env_int("NUM_WORKERS", 10);
        return config;
    }
};

struct DatabaseConfig {
    // Connection settings
    std::string user = "postgres";
    std::string host = "localhost";
    std::string database = "postgres";
    std::string password = "postgres";
    std::string port = "5432";
    std::string schema = "queen";
    
    // SSL configuration
    bool use_ssl = false;
    bool ssl_reject_unauthorized = true;
    
    // Pool configuration
    int pool_size = 150;
    int idle_timeout = 30000;           // 30 seconds
    int connection_timeout = 2000;       // 2 seconds
    int statement_timeout = 30000;       // 30 seconds
    int query_timeout = 30000;           // 30 seconds
    int lock_timeout = 10000;            // 10 seconds
    int pool_acquisition_timeout = 10000; // 10 seconds - timeout for acquiring connection from pool
    
    // Pool manager settings
    int max_retries = 3;
    
    static DatabaseConfig from_env() {
        DatabaseConfig config;
        config.user = get_env_string("PG_USER", "postgres");
        config.host = get_env_string("PG_HOST", "localhost");
        config.database = get_env_string("PG_DB", "postgres");
        config.password = get_env_string("PG_PASSWORD", "postgres");
        config.port = get_env_string("PG_PORT", "5432");
        // config.schema = get_env_string("PG_SCHEMA", "queen"); // Not used anymore
        
        config.use_ssl = get_env_bool("PG_USE_SSL", false);
        config.ssl_reject_unauthorized = get_env_bool("PG_SSL_REJECT_UNAUTHORIZED", true);
        
        config.pool_size = get_env_int("DB_POOL_SIZE", 150);
        config.idle_timeout = get_env_int("DB_IDLE_TIMEOUT", 30000);
        config.connection_timeout = get_env_int("DB_CONNECTION_TIMEOUT", 2000);
        config.statement_timeout = get_env_int("DB_STATEMENT_TIMEOUT", 30000);
        config.query_timeout = get_env_int("DB_QUERY_TIMEOUT", 30000);
        config.lock_timeout = get_env_int("DB_LOCK_TIMEOUT", 10000);
        config.pool_acquisition_timeout = get_env_int("DB_POOL_ACQUISITION_TIMEOUT", 10000);
        
        config.max_retries = get_env_int("DB_MAX_RETRIES", 3);
        
        return config;
    }
    
    std::string connection_string() const {
        std::string conn_str = "host=" + host + " port=" + port + " dbname=" + database + 
                               " user=" + user + " password=" + password;
        
        // Add SSL configuration
        if (use_ssl) {
            conn_str += " sslmode=require";
            if (!ssl_reject_unauthorized) {
                // Allow self-signed certificates
                conn_str += " sslmode=prefer";
            }
        } else {
            conn_str += " sslmode=disable";
        }
        
        // CRITICAL: Add connect_timeout (in seconds) for initial connection
        // This is the only timeout that can safely be in connection string
        // (works with both direct PostgreSQL and PgBouncer)
        conn_str += " connect_timeout=" + std::to_string(connection_timeout / 1000);
        
        // NOTE: statement_timeout, lock_timeout, and idle_in_transaction_session_timeout
        // CANNOT be set via connection string options when using PgBouncer
        // These are now set in DatabaseConnection constructor via SET commands
        
        return conn_str;
    }
};

struct QueueConfig {
    // Pop operation defaults
    int default_timeout = 30000;         // 30 seconds
    int max_timeout = 60000;             // 60 seconds
    int default_batch_size = 1;
    int batch_insert_size = 1000;
    
    // Long polling - Worker configuration
    int poll_worker_count = 10;          // Number of dedicated poll worker threads (scale for higher loads)
    
    // Stream long polling - Worker configuration
    int stream_poll_worker_count = 1;    // Number of dedicated stream poll worker threads
    int stream_poll_worker_interval = 100; // How often stream workers check registry (ms)
    int stream_poll_interval = 1000;     // Min time between stream checks per group (ms)
    int stream_backoff_threshold = 5;    // Consecutive empty checks before backoff
    double stream_backoff_multiplier = 2.0; // Exponential backoff multiplier
    int stream_max_poll_interval = 5000; // Max poll interval after backoff (ms)
    int stream_concurrent_checks = 1;   // Max concurrent window check jobs per worker
    
    // ThreadPool sizing
    int db_thread_pool_service_threads = 5; // Threads for background service DB operations
    
    // Long polling - Dual-interval rate limiting (ACTIVE)
    int poll_worker_interval = 50;       // 50ms - How often poll workers wake up to check registry (in-memory, cheap)
    int poll_db_interval = 100;          // 100ms - Initial DB query interval (aggressive first attempt, then backoff)
    
    // Long polling - Adaptive exponential backoff (ACTIVE)
    int backoff_threshold = 1;           // Number of consecutive empty pops before backoff starts (1 = immediate backoff)
    double backoff_multiplier = 2.0;     // Exponential backoff multiplier (interval *= multiplier each time)
    int max_poll_interval = 2000;        // 2000ms - Maximum poll interval after backoff
    int backoff_cleanup_inactive_threshold = 3600; // 3600s (1 hour) - Remove backoff state entries inactive for N seconds
    
    // Legacy long polling settings (reserved for future use)
    int poll_interval = 100;             // 100ms - Reserved
    int poll_interval_filtered = 50;     // 50ms - Reserved
    
    // Partition selection for filtered pops
    int max_partition_candidates = 100;  // Number of candidate partitions to fetch
    
    // Response queue timer settings
    int response_timer_interval_ms = 25; // Response timer polling interval in ms
    int response_batch_size = 100;       // Base number of responses to process per timer tick
    int response_batch_max = 500;        // Maximum responses per tick even under backlog
    
    // Batch push settings
    int batch_push_chunk_size = 1000;    // DEPRECATED: Legacy count-based batching (kept for backward compatibility)
    
    // Batch push - Size-based dynamic batching (ACTIVE)
    int batch_push_target_size_mb = 4;   // Target batch size in MB (sweet spot: 4-6 MB)
    int batch_push_min_size_mb = 2;      // Minimum batch size in MB (flush at this size minimum)
    int batch_push_max_size_mb = 8;      // Maximum batch size in MB (hard limit per batch)
    int batch_push_min_messages = 100;   // Minimum messages per batch (even if size not reached)
    int batch_push_max_messages = 10000; // Maximum messages per batch (even if under size limit)
    bool batch_push_use_size_based = true; // Enable size-based batching (false = use legacy count-based)
    
    // Queue defaults
    int default_lease_time = 300;        // 5 minutes
    int default_retry_limit = 3;
    int default_retry_delay = 1000;      // 1 second
    int default_max_size = 10000;
    int default_ttl = 3600;              // 1 hour
    int default_priority = 0;
    int default_delayed_processing = 0;
    int default_window_buffer = 0;
    
    // Dead Letter Queue
    bool default_dlq_enabled = false;
    bool default_dlq_after_max_retries = false;
    
    // Retention defaults
    int default_retention_seconds = 0;
    int default_completed_retention_seconds = 0;
    bool default_retention_enabled = false;
    
    // Eviction
    int default_max_wait_time_seconds = 0;
    
    // Consumer group subscription
    std::string default_subscription_mode = "";  // "" = all (default), "new" = skip history, "new-only" = same as new
    
    static QueueConfig from_env() {
        QueueConfig config;
        
        config.default_timeout = get_env_int("DEFAULT_TIMEOUT", 30000);
        config.max_timeout = get_env_int("MAX_TIMEOUT", 60000);
        config.default_batch_size = get_env_int("DEFAULT_BATCH_SIZE", 1);
        config.batch_insert_size = get_env_int("BATCH_INSERT_SIZE", 1000);
        
        // Long polling - Worker configuration
        config.poll_worker_count = get_env_int("POLL_WORKER_COUNT", 2);
        
        // Stream long polling - Worker configuration
        config.stream_poll_worker_count = get_env_int("STREAM_POLL_WORKER_COUNT", 1);
        config.stream_poll_worker_interval = get_env_int("STREAM_POLL_WORKER_INTERVAL", 100);
        config.stream_poll_interval = get_env_int("STREAM_POLL_INTERVAL", 1000);
        config.stream_backoff_threshold = get_env_int("STREAM_BACKOFF_THRESHOLD", 5);
        config.stream_backoff_multiplier = get_env_double("STREAM_BACKOFF_MULTIPLIER", 2.0);
        config.stream_max_poll_interval = get_env_int("STREAM_MAX_POLL_INTERVAL", 5000);
        config.stream_concurrent_checks = get_env_int("STREAM_CONCURRENT_CHECKS", 2);
        
        // ThreadPool sizing
        config.db_thread_pool_service_threads = get_env_int("DB_THREAD_POOL_SERVICE_THREADS", 5);
        
        // Long polling - Dual-interval rate limiting
        config.poll_worker_interval = get_env_int("POLL_WORKER_INTERVAL", 50);
        config.poll_db_interval = get_env_int("POLL_DB_INTERVAL", 100);
        
        // Long polling - Adaptive exponential backoff
        config.backoff_threshold = get_env_int("QUEUE_BACKOFF_THRESHOLD", 1);
        config.backoff_multiplier = get_env_double("QUEUE_BACKOFF_MULTIPLIER", 2.0);
        config.max_poll_interval = get_env_int("QUEUE_MAX_POLL_INTERVAL", 2000);
        config.backoff_cleanup_inactive_threshold = get_env_int("QUEUE_BACKOFF_CLEANUP_THRESHOLD", 3600);
        
        // Legacy long polling (reserved)
        config.poll_interval = get_env_int("QUEUE_POLL_INTERVAL", 100);
        config.poll_interval_filtered = get_env_int("QUEUE_POLL_INTERVAL_FILTERED", 50);
        
        config.max_partition_candidates = get_env_int("MAX_PARTITION_CANDIDATES", 100);
        
        config.response_timer_interval_ms = get_env_int("RESPONSE_TIMER_INTERVAL_MS", 25);
        config.response_batch_size = get_env_int("RESPONSE_BATCH_SIZE", 100);
        config.response_batch_max = get_env_int("RESPONSE_BATCH_MAX", 500);
        
        // Batch push settings
        config.batch_push_chunk_size = get_env_int("BATCH_PUSH_CHUNK_SIZE", 1000);
        config.batch_push_target_size_mb = get_env_int("BATCH_PUSH_TARGET_SIZE_MB", 4);
        config.batch_push_min_size_mb = get_env_int("BATCH_PUSH_MIN_SIZE_MB", 2);
        config.batch_push_max_size_mb = get_env_int("BATCH_PUSH_MAX_SIZE_MB", 8);
        config.batch_push_min_messages = get_env_int("BATCH_PUSH_MIN_MESSAGES", 100);
        config.batch_push_max_messages = get_env_int("BATCH_PUSH_MAX_MESSAGES", 10000);
        config.batch_push_use_size_based = get_env_bool("BATCH_PUSH_USE_SIZE_BASED", true);
        
        config.default_lease_time = get_env_int("DEFAULT_LEASE_TIME", 300);
        config.default_retry_limit = get_env_int("DEFAULT_RETRY_LIMIT", 3);
        config.default_retry_delay = get_env_int("DEFAULT_RETRY_DELAY", 1000);
        config.default_max_size = get_env_int("DEFAULT_MAX_SIZE", 10000);
        config.default_ttl = get_env_int("DEFAULT_TTL", 3600);
        config.default_priority = get_env_int("DEFAULT_PRIORITY", 0);
        config.default_delayed_processing = get_env_int("DEFAULT_DELAYED_PROCESSING", 0);
        config.default_window_buffer = get_env_int("DEFAULT_WINDOW_BUFFER", 0);
        
        config.default_dlq_enabled = get_env_bool("DEFAULT_DLQ_ENABLED", false);
        config.default_dlq_after_max_retries = get_env_bool("DEFAULT_DLQ_AFTER_MAX_RETRIES", false);
        
        config.default_retention_seconds = get_env_int("DEFAULT_RETENTION_SECONDS", 0);
        config.default_completed_retention_seconds = get_env_int("DEFAULT_COMPLETED_RETENTION_SECONDS", 0);
        config.default_retention_enabled = get_env_bool("DEFAULT_RETENTION_ENABLED", false);
        
        config.default_max_wait_time_seconds = get_env_int("DEFAULT_MAX_WAIT_TIME_SECONDS", 0);
        
        config.default_subscription_mode = get_env_string("DEFAULT_SUBSCRIPTION_MODE", "");
        
        return config;
    }
};

struct SystemEventsConfig {
    // Enable/disable system event propagation
    bool enabled = false;
    
    // Batching window for event publishing (milliseconds)
    int batch_ms = 10;
    
    // Timeout for startup synchronization (milliseconds)
    int sync_timeout = 30000;
    
    static SystemEventsConfig from_env() {
        SystemEventsConfig config;
        config.enabled = get_env_bool("QUEEN_SYSTEM_EVENTS_ENABLED", false);
        config.batch_ms = get_env_int("QUEEN_SYSTEM_EVENTS_BATCH_MS", 10);
        config.sync_timeout = get_env_int("QUEEN_SYSTEM_EVENTS_SYNC_TIMEOUT", 30000);
        return config;
    }
};

struct JobsConfig {
    // Lease reclamation
    int lease_reclaim_interval = 5000;   // 5 seconds
    
    // Retention service
    int retention_interval = 300000;     // 5 minutes
    int retention_batch_size = 1000;
    int partition_cleanup_days = 30;
    
    // Metrics retention (messages_consumed table)
    int metrics_retention_days = 90;     // Keep 90 days of metrics
    
    // Metrics collector intervals
    int metrics_sample_interval_ms = 1000;    // 1 second - How often to sample metrics
    int metrics_aggregate_interval_s = 60;    // 60 seconds - How often to aggregate and save to DB
    
    // Eviction service
    int eviction_interval = 60000;       // 1 minute
    int eviction_batch_size = 1000;
    
    // WebSocket updates
    int queue_depth_update_interval = 5000;    // 5 seconds
    int system_stats_update_interval = 10000;  // 10 seconds
    
    static JobsConfig from_env() {
        JobsConfig config;
        config.lease_reclaim_interval = get_env_int("LEASE_RECLAIM_INTERVAL", 5000);
        
        config.retention_interval = get_env_int("RETENTION_INTERVAL", 300000);
        config.retention_batch_size = get_env_int("RETENTION_BATCH_SIZE", 1000);
        config.partition_cleanup_days = get_env_int("PARTITION_CLEANUP_DAYS", 30);
        
        config.metrics_retention_days = get_env_int("METRICS_RETENTION_DAYS", 90);
        
        config.metrics_sample_interval_ms = get_env_int("METRICS_SAMPLE_INTERVAL_MS", 1000);
        config.metrics_aggregate_interval_s = get_env_int("METRICS_AGGREGATE_INTERVAL_S", 60);
        
        config.eviction_interval = get_env_int("EVICTION_INTERVAL", 60000);
        config.eviction_batch_size = get_env_int("EVICTION_BATCH_SIZE", 1000);
        
        config.queue_depth_update_interval = get_env_int("QUEUE_DEPTH_UPDATE_INTERVAL", 5000);
        config.system_stats_update_interval = get_env_int("SYSTEM_STATS_UPDATE_INTERVAL", 10000);
        
        return config;
    }
};

struct WebSocketConfig {
    int compression = 0;
    int max_payload_length = 16384;      // 16KB
    int idle_timeout = 60;               // 60 seconds
    int max_connections = 1000;
    int heartbeat_interval = 30000;      // 30 seconds
    
    static WebSocketConfig from_env() {
        WebSocketConfig config;
        config.compression = get_env_int("WS_COMPRESSION", 0);
        config.max_payload_length = get_env_int("WS_MAX_PAYLOAD_LENGTH", 16384);
        config.idle_timeout = get_env_int("WS_IDLE_TIMEOUT", 60);
        config.max_connections = get_env_int("WS_MAX_CONNECTIONS", 1000);
        config.heartbeat_interval = get_env_int("WS_HEARTBEAT_INTERVAL", 30000);
        return config;
    }
};

struct EncryptionConfig {
    std::string key_env_var = "QUEEN_ENCRYPTION_KEY";
    std::string algorithm = "aes-256-gcm";
    int key_length = 32;                 // bytes
    int iv_length = 16;                  // bytes
    
    // No from_env needed - these are constants
    // Actual key is read directly via std::getenv in encryption service
};

struct ClientConfig {
    std::string default_base_url = "http://localhost:6632";
    int default_retry_attempts = 3;
    int default_retry_delay = 1000;      // 1 second
    double default_retry_backoff = 2.0;
    int connection_pool_size = 10;
    int request_timeout = 30000;         // 30 seconds
    
    static ClientConfig from_env() {
        ClientConfig config;
        config.default_base_url = get_env_string("QUEEN_BASE_URL", "http://localhost:6632");
        config.default_retry_attempts = get_env_int("CLIENT_RETRY_ATTEMPTS", 3);
        config.default_retry_delay = get_env_int("CLIENT_RETRY_DELAY", 1000);
        config.default_retry_backoff = get_env_double("CLIENT_RETRY_BACKOFF", 2.0);
        config.connection_pool_size = get_env_int("CLIENT_POOL_SIZE", 10);
        config.request_timeout = get_env_int("CLIENT_REQUEST_TIMEOUT", 30000);
        return config;
    }
};

struct ApiConfig {
    int max_body_size = 100 * 1024 * 1024; // 100MB
    
    // Pagination
    int default_limit = 100;
    int max_limit = 1000;
    int default_offset = 0;
    
    // CORS
    int cors_max_age = 86400;            // 24 hours
    std::string cors_allowed_origins = "*";
    std::string cors_allowed_methods = "GET, POST, PUT, DELETE, OPTIONS";
    std::string cors_allowed_headers = "Content-Type, Authorization";
    
    static ApiConfig from_env() {
        ApiConfig config;
        config.max_body_size = get_env_int("MAX_BODY_SIZE", 100 * 1024 * 1024);
        
        config.default_limit = get_env_int("API_DEFAULT_LIMIT", 100);
        config.max_limit = get_env_int("API_MAX_LIMIT", 1000);
        config.default_offset = get_env_int("API_DEFAULT_OFFSET", 0);
        
        config.cors_max_age = get_env_int("CORS_MAX_AGE", 86400);
        config.cors_allowed_origins = get_env_string("CORS_ALLOWED_ORIGINS", "*");
        config.cors_allowed_methods = get_env_string("CORS_ALLOWED_METHODS", "GET, POST, PUT, DELETE, OPTIONS");
        config.cors_allowed_headers = get_env_string("CORS_ALLOWED_HEADERS", "Content-Type, Authorization");
        
        return config;
    }
};

struct AnalyticsConfig {
    // Processing time calculations
    int recent_completion_hours = 24;
    int min_completed_for_stats = 5;
    
    // Time windows
    int recent_message_window = 60;      // 1 minute in seconds
    int related_message_window = 3600;   // 1 hour in seconds
    int max_related_messages = 10;
    
    static AnalyticsConfig from_env() {
        AnalyticsConfig config;
        config.recent_completion_hours = get_env_int("ANALYTICS_RECENT_HOURS", 24);
        config.min_completed_for_stats = get_env_int("ANALYTICS_MIN_COMPLETED", 5);
        config.recent_message_window = get_env_int("RECENT_MESSAGE_WINDOW", 60);
        config.related_message_window = get_env_int("RELATED_MESSAGE_WINDOW", 3600);
        config.max_related_messages = get_env_int("MAX_RELATED_MESSAGES", 10);
        return config;
    }
};

struct MonitoringConfig {
    bool enable_request_counting = true;
    bool enable_message_counting = true;
    bool metrics_endpoint_enabled = true;
    bool health_check_enabled = true;
    
    static MonitoringConfig from_env() {
        MonitoringConfig config;
        config.enable_request_counting = get_env_bool("ENABLE_REQUEST_COUNTING", true);
        config.enable_message_counting = get_env_bool("ENABLE_MESSAGE_COUNTING", true);
        config.metrics_endpoint_enabled = get_env_bool("METRICS_ENDPOINT_ENABLED", true);
        config.health_check_enabled = get_env_bool("HEALTH_CHECK_ENABLED", true);
        return config;
    }
};

struct LoggingConfig {
    bool enable_logging = true;
    std::string log_level = "info";
    std::string log_format = "json";
    bool log_timestamp = true;
    
    static LoggingConfig from_env() {
        LoggingConfig config;
        config.enable_logging = get_env_bool("ENABLE_LOGGING", true);
        config.log_level = get_env_string("LOG_LEVEL", "info");
        config.log_format = get_env_string("LOG_FORMAT", "json");
        config.log_timestamp = get_env_bool("LOG_TIMESTAMP", true);
        return config;
    }
};

struct FileBufferConfig {
    // Platform-specific default
    #ifdef __APPLE__
        std::string buffer_dir = "/tmp/queen";
    #else
        std::string buffer_dir = "/var/lib/queen/buffers";
    #endif
    
    int flush_interval_ms = 100;
    size_t max_batch_size = 100;
    size_t max_events_per_file = 10000;  // Create new buffer file after N events
    
    static FileBufferConfig from_env() {
        FileBufferConfig config;
        
        // Get from environment or use platform default
        const char* env_dir = std::getenv("FILE_BUFFER_DIR");
        if (env_dir && env_dir[0] != '\0') {
            config.buffer_dir = env_dir;
        }
        // else: use default already set above
        
        // Ensure buffer_dir is never empty - fallback to platform default
        if (config.buffer_dir.empty()) {
            #ifdef __APPLE__
                config.buffer_dir = "/tmp/queen";
            #else
                config.buffer_dir = "/var/lib/queen/buffers";
            #endif
        }
        
        config.flush_interval_ms = get_env_int("FILE_BUFFER_FLUSH_MS", 100);
        config.max_batch_size = get_env_int("FILE_BUFFER_MAX_BATCH", 100);
        config.max_events_per_file = get_env_int("FILE_BUFFER_EVENTS_PER_FILE", 10000);
        
        return config;
    }
};

struct Config {
    ServerConfig server;
    DatabaseConfig database;
    QueueConfig queue;
    SystemEventsConfig system_events;
    JobsConfig jobs;
    WebSocketConfig websocket;
    EncryptionConfig encryption;
    ClientConfig client;
    ApiConfig api;
    AnalyticsConfig analytics;
    MonitoringConfig monitoring;
    LoggingConfig logging;
    FileBufferConfig file_buffer;
    
    static Config load() {
        Config config;
        config.server = ServerConfig::from_env();
        config.database = DatabaseConfig::from_env();
        config.queue = QueueConfig::from_env();
        config.system_events = SystemEventsConfig::from_env();
        config.jobs = JobsConfig::from_env();
        config.websocket = WebSocketConfig::from_env();
        // encryption config has no env vars to load
        config.client = ClientConfig::from_env();
        config.api = ApiConfig::from_env();
        config.analytics = AnalyticsConfig::from_env();
        config.monitoring = MonitoringConfig::from_env();
        config.logging = LoggingConfig::from_env();
        config.file_buffer = FileBufferConfig::from_env();
        return config;
    }
};

} // namespace queen
