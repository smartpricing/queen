#include "queen/async_queue_manager.hpp"
#include "queen/file_buffer.hpp"
#include "queen/encryption.hpp"
#include <spdlog/spdlog.h>
#include <random>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <numeric>
#include <map>

namespace queen {

// --- Constructor ---

AsyncQueueManager::AsyncQueueManager(std::shared_ptr<AsyncDbPool> async_db_pool, 
                                   const QueueConfig& config, 
                                   const std::string& schema_name)
    : async_db_pool_(async_db_pool), config_(config), schema_name_(schema_name) {
    if (!async_db_pool_) {
        throw std::invalid_argument("Async database pool cannot be null");
    }
}

// --- UUID Generation (same as QueueManager) ---

std::string AsyncQueueManager::generate_uuid() {
    static std::mutex uuid_mutex;
    static uint64_t last_ms = 0;
    static uint16_t sequence = 0;
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    
    std::lock_guard<std::mutex> lock(uuid_mutex);
    
    auto now = std::chrono::system_clock::now();
    uint64_t current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    if (current_ms <= last_ms) {
        sequence++;
    } else {
        last_ms = current_ms;
        sequence = 0; 
    }

    std::array<uint8_t, 16> bytes;

    // 48-bit unix_ts_ms (big-endian)
    bytes[0] = (last_ms >> 40) & 0xFF;
    bytes[1] = (last_ms >> 32) & 0xFF;
    bytes[2] = (last_ms >> 24) & 0xFF;
    bytes[3] = (last_ms >> 16) & 0xFF;
    bytes[4] = (last_ms >> 8) & 0xFF;
    bytes[5] = last_ms & 0xFF;

    // 4-bit version (0111) and 12-bit sequence
    uint16_t sequence_and_version = sequence & 0x0FFF;
    bytes[6] = 0x70 | (sequence_and_version >> 8);
    bytes[7] = sequence_and_version & 0xFF;

    // 2-bit variant (10) and 62-bits of random data
    uint64_t rand_data = gen();
    bytes[8] = 0x80 | ((rand_data >> 56) & 0x3F);
    bytes[9] = (rand_data >> 48) & 0xFF;
    bytes[10] = (rand_data >> 40) & 0xFF;
    bytes[11] = (rand_data >> 32) & 0xFF;
    bytes[12] = (rand_data >> 24) & 0xFF;
    bytes[13] = (rand_data >> 16) & 0xFF;
    bytes[14] = (rand_data >> 8) & 0xFF;
    bytes[15] = rand_data & 0xFF;

    // Format to string
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) ss << '-';
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    
    return ss.str();
}

std::string AsyncQueueManager::generate_transaction_id() {
    return generate_uuid();
}

// --- Schema Initialization ---

bool AsyncQueueManager::initialize_schema() {
    try {
        auto conn = async_db_pool_->acquire();
        if (!conn) {
            spdlog::error("Failed to acquire connection for schema initialization");
            return false;
        }
        
        spdlog::info("Initializing schema: {}", schema_name_);
        
        // Helper lambda for async query execution
        auto exec_async = [](PGconn* c, const std::string& sql) -> bool {
            // Send query
            if (!PQsendQuery(c, sql.c_str())) {
                spdlog::error("PQsendQuery failed: {}", PQerrorMessage(c));
                return false;
            }
            
            // Wait for query to complete
            while (PQisBusy(c)) {
                if (!PQconsumeInput(c)) {
                    spdlog::error("PQconsumeInput failed: {}", PQerrorMessage(c));
                    return false;
                }
            }
            
            // Get result
            PGresult* res = PQgetResult(c);
            if (!res) {
                spdlog::error("No result returned");
                return false;
            }
            
            bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
            if (!success) {
                spdlog::error("Query failed: {}", PQresultErrorMessage(res));
            }
            PQclear(res);
            
            // Consume any remaining results
            PGresult* extra;
            while ((extra = PQgetResult(c)) != nullptr) {
                PQclear(extra);
            }
            
            return success;
        };
        
        // Create schema if it doesn't exist
        std::string create_schema_sql = "CREATE SCHEMA IF NOT EXISTS " + schema_name_;
        if (!exec_async(conn.get(), create_schema_sql)) {
            spdlog::error("Failed to create schema {}", schema_name_);
            return false;
        }
        
        // Create tables - Queen Message Queue Schema V3
        std::string create_tables_sql = R"(
            CREATE TABLE IF NOT EXISTS queen.queues (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                name VARCHAR(255) UNIQUE NOT NULL,
                namespace VARCHAR(255),
                task VARCHAR(255),
                priority INTEGER DEFAULT 0,
                lease_time INTEGER DEFAULT 300,
                retry_limit INTEGER DEFAULT 3,
                retry_delay INTEGER DEFAULT 1000,
                ttl INTEGER DEFAULT 3600,
                dead_letter_queue BOOLEAN DEFAULT FALSE,
                dlq_after_max_retries BOOLEAN DEFAULT FALSE,
                delayed_processing INTEGER DEFAULT 0,
                window_buffer INTEGER DEFAULT 0,
                retention_seconds INTEGER DEFAULT 0,
                completed_retention_seconds INTEGER DEFAULT 0,
                retention_enabled BOOLEAN DEFAULT FALSE,
                encryption_enabled BOOLEAN DEFAULT FALSE,
                max_wait_time_seconds INTEGER DEFAULT 0,
                max_queue_size INTEGER DEFAULT 0,
                created_at TIMESTAMPTZ DEFAULT NOW()
            );
            
            CREATE TABLE IF NOT EXISTS queen.partitions (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                queue_id UUID REFERENCES queen.queues(id) ON DELETE CASCADE,
                name VARCHAR(255) NOT NULL DEFAULT 'Default',
                created_at TIMESTAMPTZ DEFAULT NOW(),
                last_activity TIMESTAMPTZ DEFAULT NOW(),
                UNIQUE(queue_id, name)
            );
            
            CREATE TABLE IF NOT EXISTS queen.messages (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                transaction_id VARCHAR(255) NOT NULL,
                trace_id UUID DEFAULT gen_random_uuid(),
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                payload JSONB NOT NULL,
                created_at TIMESTAMPTZ DEFAULT NOW(),
                is_encrypted BOOLEAN DEFAULT FALSE
            );
            
            -- Unique constraint scoped to partition (not global)
            CREATE UNIQUE INDEX IF NOT EXISTS messages_partition_transaction_unique 
                ON queen.messages(partition_id, transaction_id);
            
            CREATE TABLE IF NOT EXISTS queen.partition_consumers (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',
                last_consumed_id UUID DEFAULT '00000000-0000-0000-0000-000000000000',
                last_consumed_created_at TIMESTAMPTZ,
                total_messages_consumed BIGINT DEFAULT 0,
                total_batches_consumed BIGINT DEFAULT 0,
                last_consumed_at TIMESTAMPTZ,
                lease_expires_at TIMESTAMPTZ,
                lease_acquired_at TIMESTAMPTZ,
                message_batch JSONB,
                batch_size INTEGER DEFAULT 0,
                acked_count INTEGER DEFAULT 0,
                worker_id VARCHAR(255),
                pending_estimate BIGINT DEFAULT 0,
                last_stats_update TIMESTAMPTZ,
                batch_retry_count INTEGER DEFAULT 0,
                created_at TIMESTAMPTZ DEFAULT NOW(),
                UNIQUE(partition_id, consumer_group),
                CHECK (
                    (last_consumed_id = '00000000-0000-0000-0000-000000000000' 
                     AND last_consumed_created_at IS NULL)
                    OR 
                    (last_consumed_id != '00000000-0000-0000-0000-000000000000' 
                     AND last_consumed_created_at IS NOT NULL)
                )
            );
            
            CREATE TABLE IF NOT EXISTS queen.messages_consumed (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                consumer_group VARCHAR(255) NOT NULL,
                messages_completed INTEGER DEFAULT 0,
                messages_failed INTEGER DEFAULT 0,
                acked_at TIMESTAMPTZ DEFAULT NOW()
            );
            
            CREATE TABLE IF NOT EXISTS queen.dead_letter_queue (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                message_id UUID REFERENCES queen.messages(id) ON DELETE CASCADE,
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                consumer_group VARCHAR(255),
                error_message TEXT,
                retry_count INTEGER DEFAULT 0,
                original_created_at TIMESTAMPTZ,
                failed_at TIMESTAMPTZ DEFAULT NOW()
            );
            
            CREATE TABLE IF NOT EXISTS queen.retention_history (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                messages_deleted INTEGER DEFAULT 0,
                retention_type VARCHAR(50),
                executed_at TIMESTAMPTZ DEFAULT NOW()
            );
            
            CREATE TABLE IF NOT EXISTS queen.system_metrics (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                timestamp TIMESTAMPTZ NOT NULL,
                hostname TEXT NOT NULL,
                port INTEGER NOT NULL,
                worker_id TEXT NOT NULL,
                sample_count INTEGER NOT NULL DEFAULT 60,
                metrics JSONB NOT NULL,
                CONSTRAINT unique_metric_per_replica 
                    UNIQUE (timestamp, hostname, port, worker_id)
            );
            
            CREATE TABLE IF NOT EXISTS queen.message_traces (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                message_id UUID REFERENCES queen.messages(id) ON DELETE CASCADE,
                partition_id UUID REFERENCES queen.partitions(id) ON DELETE CASCADE,
                transaction_id VARCHAR(255) NOT NULL,
                consumer_group VARCHAR(255),
                event_type VARCHAR(100),
                data JSONB NOT NULL,
                created_at TIMESTAMPTZ DEFAULT NOW(),
                worker_id VARCHAR(255)
            );
            
            CREATE TABLE IF NOT EXISTS queen.message_trace_names (
                trace_id UUID REFERENCES queen.message_traces(id) ON DELETE CASCADE,
                trace_name TEXT NOT NULL,
                PRIMARY KEY (trace_id, trace_name)
            );
            
            -- System state table for shared configuration across instances
            CREATE TABLE IF NOT EXISTS queen.system_state (
                key TEXT PRIMARY KEY,
                value JSONB NOT NULL,
                updated_at TIMESTAMPTZ DEFAULT NOW()
            );
            
            -- ============================================================================
            -- Queen Streaming Schema V3
            -- ============================================================================
            
            -- 1. Stream Definitions
            CREATE TABLE IF NOT EXISTS queen.streams (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                name VARCHAR(255) UNIQUE NOT NULL,
                namespace VARCHAR(255) NOT NULL,
                
                -- TRUE = group by partition_id
                -- FALSE = process all partitions as one global stream
                partitioned BOOLEAN NOT NULL DEFAULT FALSE,

                -- Static Window Configuration
                window_type VARCHAR(50) NOT NULL, -- 'tumbling'
                
                -- For TIME windows
                window_duration_ms BIGINT, 
                
                -- For COUNT windows
                window_size_count INT,
                
                -- For SLIDING windows
                window_slide_ms BIGINT,
                window_slide_count INT,

                -- Common settings
                window_grace_period_ms BIGINT NOT NULL DEFAULT 30000,
                window_lease_timeout_ms BIGINT NOT NULL DEFAULT 30000,

                created_at TIMESTAMPTZ DEFAULT NOW(),
                updated_at TIMESTAMPTZ DEFAULT NOW()
            );

            -- 2. Junction table for stream sources (many-to-many)
            CREATE TABLE IF NOT EXISTS queen.stream_sources (
                stream_id UUID NOT NULL REFERENCES queen.streams(id) ON DELETE CASCADE,
                queue_id UUID NOT NULL REFERENCES queen.queues(id) ON DELETE CASCADE,
                PRIMARY KEY (stream_id, queue_id)
            );

            -- 3. Consumer Offsets (The "Bookmark" table)
            CREATE TABLE IF NOT EXISTS queen.stream_consumer_offsets (
                stream_id UUID REFERENCES queen.streams(id) ON DELETE CASCADE,
                consumer_group VARCHAR(255) NOT NULL,
                
                -- KEY FIX: Stores partition_id::TEXT or '__GLOBAL__'
                stream_key TEXT NOT NULL,

                -- 'bookmark' for TIME-based windows
                last_acked_window_end TIMESTAMPTZ,
                -- 'bookmark' for COUNT-based windows
                last_acked_message_id UUID,

                total_windows_consumed BIGINT DEFAULT 0,
                last_consumed_at TIMESTAMPTZ,
                
                PRIMARY KEY (stream_id, consumer_group, stream_key)
            );

            -- 4. Active Leases (The "In-Flight" table)
            CREATE TABLE IF NOT EXISTS queen.stream_leases (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                stream_id UUID REFERENCES queen.streams(id) ON DELETE CASCADE,
                consumer_group VARCHAR(255) NOT NULL,

                -- KEY FIX: Stores partition_id::TEXT or '__GLOBAL__'
                stream_key TEXT NOT NULL, 
                
                window_start TIMESTAMPTZ NOT NULL,
                window_end TIMESTAMPTZ NOT NULL,
                
                lease_id UUID NOT NULL UNIQUE,
                lease_consumer_id VARCHAR(255),
                lease_expires_at TIMESTAMPTZ NOT NULL,
                
                UNIQUE(stream_id, consumer_group, stream_key, window_start, window_end)
            );

            -- 5. Watermark Table (CRITICAL)
            CREATE TABLE IF NOT EXISTS queen.queue_watermarks (
                queue_id UUID PRIMARY KEY REFERENCES queen.queues(id) ON DELETE CASCADE,
                queue_name VARCHAR(255) NOT NULL,
                max_created_at TIMESTAMPTZ NOT NULL DEFAULT '-infinity'::timestamptz,
                updated_at TIMESTAMPTZ DEFAULT NOW()
            );
        )";
        
        if (!exec_async(conn.get(), create_tables_sql)) {
            spdlog::error("Failed to create tables");
            return false;
        }
        
        // Create indexes for optimal query performance
        std::string create_indexes_sql = R"(
            CREATE INDEX IF NOT EXISTS idx_queues_name ON queen.queues(name);
            CREATE INDEX IF NOT EXISTS idx_queues_priority ON queen.queues(priority DESC);
            CREATE INDEX IF NOT EXISTS idx_queues_namespace ON queen.queues(namespace) WHERE namespace IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_queues_task ON queen.queues(task) WHERE task IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_queues_namespace_task ON queen.queues(namespace, task) WHERE namespace IS NOT NULL AND task IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_queues_retention_enabled ON queen.queues(retention_enabled) WHERE retention_enabled = true;
            CREATE INDEX IF NOT EXISTS idx_partitions_queue_name ON queen.partitions(queue_id, name);
            CREATE INDEX IF NOT EXISTS idx_partitions_last_activity ON queen.partitions(last_activity);
            CREATE INDEX IF NOT EXISTS idx_messages_partition_created_id ON queen.messages(partition_id, created_at, id);
            CREATE INDEX IF NOT EXISTS idx_messages_transaction_id ON queen.messages(transaction_id);
            CREATE INDEX IF NOT EXISTS idx_messages_trace_id ON queen.messages(trace_id);
            CREATE INDEX IF NOT EXISTS idx_messages_created_at ON queen.messages(created_at);
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_lookup ON queen.partition_consumers(partition_id, consumer_group);
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_active_leases ON queen.partition_consumers(partition_id, consumer_group, lease_expires_at) WHERE lease_expires_at IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_expired_leases ON queen.partition_consumers(lease_expires_at) WHERE lease_expires_at IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_progress ON queen.partition_consumers(last_consumed_at DESC);
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_idle ON queen.partition_consumers(partition_id, consumer_group) WHERE lease_expires_at IS NULL;
            CREATE INDEX IF NOT EXISTS idx_partition_consumers_consumer_group ON queen.partition_consumers(consumer_group);
            CREATE INDEX IF NOT EXISTS idx_messages_consumed_acked_at ON queen.messages_consumed(acked_at DESC);
            CREATE INDEX IF NOT EXISTS idx_messages_consumed_partition_acked ON queen.messages_consumed(partition_id, acked_at DESC);
            CREATE INDEX IF NOT EXISTS idx_messages_consumed_consumer_acked ON queen.messages_consumed(consumer_group, acked_at DESC);
            CREATE INDEX IF NOT EXISTS idx_messages_consumed_partition_id ON queen.messages_consumed(partition_id);
            CREATE INDEX IF NOT EXISTS idx_dlq_partition ON queen.dead_letter_queue(partition_id);
            CREATE INDEX IF NOT EXISTS idx_dlq_consumer_group ON queen.dead_letter_queue(consumer_group);
            CREATE INDEX IF NOT EXISTS idx_dlq_failed_at ON queen.dead_letter_queue(failed_at DESC);
            CREATE INDEX IF NOT EXISTS idx_dlq_message_consumer ON queen.dead_letter_queue(message_id, consumer_group);
            CREATE INDEX IF NOT EXISTS idx_retention_history_partition ON queen.retention_history(partition_id);
            CREATE INDEX IF NOT EXISTS idx_retention_history_executed ON queen.retention_history(executed_at);
            CREATE INDEX IF NOT EXISTS idx_system_metrics_timestamp ON queen.system_metrics(timestamp DESC);
            CREATE INDEX IF NOT EXISTS idx_system_metrics_replica ON queen.system_metrics(hostname, port);
            CREATE INDEX IF NOT EXISTS idx_system_metrics_worker ON queen.system_metrics(worker_id);
            CREATE INDEX IF NOT EXISTS idx_system_metrics_metrics ON queen.system_metrics USING GIN (metrics);
            CREATE INDEX IF NOT EXISTS idx_message_traces_message_id ON queen.message_traces(message_id);
            CREATE INDEX IF NOT EXISTS idx_message_traces_transaction_partition ON queen.message_traces(transaction_id, partition_id);
            CREATE INDEX IF NOT EXISTS idx_message_traces_created_at ON queen.message_traces(created_at DESC);
            CREATE INDEX IF NOT EXISTS idx_message_trace_names_name ON queen.message_trace_names(trace_name);
            CREATE INDEX IF NOT EXISTS idx_message_trace_names_trace_id ON queen.message_trace_names(trace_id);
            CREATE INDEX IF NOT EXISTS idx_system_state_key ON queen.system_state(key);
            
            -- Streaming indexes
            CREATE INDEX IF NOT EXISTS idx_queue_watermarks_name ON queen.queue_watermarks(queue_name);
            CREATE INDEX IF NOT EXISTS idx_stream_leases_lookup ON queen.stream_leases(stream_id, consumer_group, stream_key, window_start);
            CREATE INDEX IF NOT EXISTS idx_stream_leases_expires ON queen.stream_leases(lease_expires_at);
            CREATE INDEX IF NOT EXISTS idx_stream_consumer_offsets_lookup ON queen.stream_consumer_offsets(stream_id, consumer_group, stream_key);
        )";
        
        if (!exec_async(conn.get(), create_indexes_sql)) {
            spdlog::warn("Some indexes may not have been created");
        } else {
            spdlog::info("Database indexes created successfully");
        }
        
        // Create triggers
        std::string create_triggers_sql = R"(
            -- Statement-level trigger for partition last_activity (batch-efficient)
            CREATE OR REPLACE FUNCTION update_partition_last_activity()
            RETURNS TRIGGER AS $$
            BEGIN
                -- Update last_activity for all affected partitions in the batch
                UPDATE queen.partitions 
                SET last_activity = NOW() 
                WHERE id IN (SELECT DISTINCT partition_id FROM new_messages);
                RETURN NULL;
            END;
            $$ LANGUAGE plpgsql;

            DROP TRIGGER IF EXISTS trigger_update_partition_activity ON queen.messages;
            CREATE TRIGGER trigger_update_partition_activity
            AFTER INSERT ON queen.messages
            REFERENCING NEW TABLE AS new_messages
            FOR EACH STATEMENT
            EXECUTE FUNCTION update_partition_last_activity();

            -- Statement-level trigger for pending estimate (batch-efficient)
            CREATE OR REPLACE FUNCTION update_pending_on_push()
            RETURNS TRIGGER AS $$
            BEGIN
                -- Increment pending_estimate by the count of messages per partition
                UPDATE queen.partition_consumers pc
                SET pending_estimate = pc.pending_estimate + msg_counts.count,
                    last_stats_update = NOW()
                FROM (
                    SELECT partition_id, COUNT(*) as count
                    FROM new_messages
                    GROUP BY partition_id
                ) msg_counts
                WHERE pc.partition_id = msg_counts.partition_id;
                RETURN NULL;
            END;
            $$ LANGUAGE plpgsql;

            DROP TRIGGER IF EXISTS trigger_update_pending_on_push ON queen.messages;
            CREATE TRIGGER trigger_update_pending_on_push
            AFTER INSERT ON queen.messages
            REFERENCING NEW TABLE AS new_messages
            FOR EACH STATEMENT
            EXECUTE FUNCTION update_pending_on_push();

            -- Streaming watermark trigger function (statement-level for batch efficiency)
            CREATE OR REPLACE FUNCTION update_queue_watermark()
            RETURNS TRIGGER AS $$
            BEGIN
                -- Process all inserted rows in the batch at once
                INSERT INTO queen.queue_watermarks (queue_id, queue_name, max_created_at)
                SELECT 
                    q.id,
                    q.name,
                    MAX(nm.created_at) as max_created_at
                FROM new_messages nm
                JOIN queen.partitions p ON p.id = nm.partition_id
                JOIN queen.queues q ON p.queue_id = q.id
                GROUP BY q.id, q.name
                ON CONFLICT (queue_id)
                DO UPDATE SET
                    max_created_at = GREATEST(queen.queue_watermarks.max_created_at, EXCLUDED.max_created_at),
                    updated_at = NOW();
                RETURN NULL;
            END;
            $$ LANGUAGE plpgsql;

            DROP TRIGGER IF EXISTS trigger_update_watermark ON queen.messages;
            CREATE TRIGGER trigger_update_watermark
            AFTER INSERT ON queen.messages
            REFERENCING NEW TABLE AS new_messages
            FOR EACH STATEMENT
            EXECUTE FUNCTION update_queue_watermark();
        )";
        
        if (!exec_async(conn.get(), create_triggers_sql)) {
            spdlog::warn("Some triggers may not have been created");
        } else {
            spdlog::info("Database triggers created successfully");
        }
        
        // Initialize system state (maintenance mode default value)
        std::string init_system_state = R"(
            INSERT INTO queen.system_state (key, value, updated_at)
            VALUES ('maintenance_mode', '{"enabled": false}'::jsonb, NOW())
            ON CONFLICT (key) DO NOTHING;
        )";
        
        if (!exec_async(conn.get(), init_system_state)) {
            spdlog::warn("Failed to initialize system state");
        } else {
            spdlog::info("System state initialized (maintenance_mode: false)");
        }
        
        spdlog::info("Database schema V3 initialized successfully (tables, indexes, triggers)");
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize schema: {}", e.what());
        return false;
    }
}

// --- Maintenance Mode ---

bool AsyncQueueManager::check_maintenance_mode_with_cache() {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Check cache TTL
    uint64_t last_check = last_maintenance_check_ms_.load();
    if (now_ms - last_check < MAINTENANCE_CACHE_TTL_MS) {
        return maintenance_mode_cached_.load();
    }
    
    // Cache expired, check database
    try {
        auto conn = async_db_pool_->acquire();
        
        std::string sql = R"(
            SELECT value->>'enabled' as enabled
            FROM queen.system_state
            WHERE key = 'maintenance_mode'
        )";
        
        sendAndWait(conn.get(), sql.c_str());
        auto result = getTuplesResult(conn.get());
        
        bool enabled = false;
        if (PQntuples(result.get()) > 0) {
            const char* val = PQgetvalue(result.get(), 0, 0);
            enabled = (val && std::string(val) == "true");
        }
        
        maintenance_mode_cached_.store(enabled);
        last_maintenance_check_ms_.store(now_ms);
        
        return enabled;
    } catch (const std::exception& e) {
        spdlog::error("Failed to check maintenance mode from database: {}", e.what());
        return maintenance_mode_cached_.load();
    }
}

// --- Queue/Partition Management ---

bool AsyncQueueManager::ensure_queue_exists(PGconn* conn,
                                           const std::string& queue_name, 
                                           const std::string& namespace_name,
                                           const std::string& task_name) {
    try {
        std::string sql = R"(
            INSERT INTO queen.queues (name, namespace, task, priority, lease_time, retry_limit, retry_delay, max_queue_size, ttl)
            VALUES ($1, $2, $3, 0, 300, 3, 1000, 0, 3600)
            ON CONFLICT (name) DO NOTHING
        )";
        
        std::vector<std::string> params = {
            queue_name,
            namespace_name.empty() ? "" : namespace_name,
            task_name.empty() ? "" : task_name
        };
        
        sendQueryParamsAsync(conn, sql, params);
        getCommandResult(conn);
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to ensure queue exists: {}", e.what());
        return false;
    }
}

bool AsyncQueueManager::ensure_partition_exists(PGconn* conn,
                                               const std::string& queue_name, 
                                               const std::string& partition_name) {
    try {
        spdlog::debug("Ensuring partition exists: queue='{}', partition='{}'", queue_name, partition_name);
        
        std::string sql = R"(
            INSERT INTO queen.partitions (queue_id, name)
            SELECT id, $2 FROM queen.queues WHERE name = $1
            ON CONFLICT (queue_id, name) DO NOTHING
            RETURNING id, name
        )";
        
        std::vector<std::string> params = {queue_name, partition_name};
        sendQueryParamsAsync(conn, sql, params);
        
        auto result = getTuplesResult(conn);
        
        if (PQntuples(result.get()) > 0) {
            spdlog::debug("Partition created: id='{}', name='{}'", 
                         PQgetvalue(result.get(), 0, 0), 
                         PQgetvalue(result.get(), 0, 1));
        } else {
            spdlog::debug("Partition already exists (ON CONFLICT triggered)");
        }
        
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to ensure partition exists: {}", e.what());
        return false;
    }
}

// --- Helper for FileBufferManager ---

void AsyncQueueManager::push_single_message(
    const std::string& queue_name,
    const std::string& partition_name,
    const nlohmann::json& payload,
    const std::string& namespace_name,
    const std::string& task,
    const std::string& transaction_id,
    const std::string& trace_id
) {
    auto conn = async_db_pool_->acquire();
    
    // Generate transaction ID if not provided
    std::string tx_id = transaction_id.empty() ? generate_uuid() : transaction_id;
    
    // Ensure queue exists
    ensure_queue_exists(conn.get(), queue_name, namespace_name, task);
    
    // Ensure partition exists
    ensure_partition_exists(conn.get(), queue_name, partition_name);
    
    // Get next sequence number
    std::string seq_sql = R"(
        SELECT COALESCE(MAX(sequence), 0) + 1 as next_seq
        FROM queen.messages
        WHERE queue_name = $1 AND partition_name = $2
    )";
    
    sendQueryParamsAsync(conn.get(), seq_sql, {queue_name, partition_name});
    auto seq_result = getTuplesResult(conn.get());
    
    int64_t sequence = 1;
    if (PQntuples(seq_result.get()) > 0) {
        const char* seq_str = PQgetvalue(seq_result.get(), 0, 0);
        sequence = seq_str ? std::stoll(seq_str) : 1;
    }
    
    // Serialize payload
    std::string payload_str = payload.dump();
    
    // Insert message
    std::string insert_sql = R"(
        INSERT INTO queen.messages 
        (queue_name, partition_name, payload, transaction_id, trace_id, 
         namespace, task, status, sequence, created_at)
        VALUES ($1, $2, $3, $4, $5, $6, $7, 'pending', $8, NOW())
    )";
    
    sendQueryParamsAsync(conn.get(), insert_sql, {
        queue_name,
        partition_name,
        payload_str,
        tx_id,
        trace_id,
        namespace_name,
        task,
        std::to_string(sequence)
    });
    
    getCommandResult(conn.get());
}

// --- Push Single Message ---

PushResult AsyncQueueManager::push_single_message(const PushItem& item) {
    PushResult result;
    result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
    
    // MAINTENANCE MODE: Route to file buffer
    if (check_maintenance_mode_with_cache() && file_buffer_manager_) {
        try {
            nlohmann::json event = {
                {"queue", item.queue},
                {"partition", item.partition},
                {"payload", item.payload},
                {"transactionId", result.transaction_id},
                {"failover", true}
            };
            
            if (item.trace_id.has_value() && !item.trace_id->empty()) {
                event["traceId"] = *item.trace_id;
            }
            
            if (file_buffer_manager_->write_event(event)) {
                result.status = "buffered";
                result.message_id = std::nullopt;
                result.trace_id = item.trace_id;
                return result;
            } else {
                result.status = "failed";
                result.error = "File buffer write failed";
                return result;
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to write to file buffer during maintenance mode: {}", e.what());
            result.status = "failed";
            result.error = std::string("Buffer write error: ") + e.what();
            return result;
        }
    }
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Check if queue exists
        sendQueryParamsAsync(conn.get(), "SELECT id FROM queen.queues WHERE name = $1", {item.queue});
        auto queue_check = getTuplesResult(conn.get());
        
        if (PQntuples(queue_check.get()) == 0) {
            result.status = "failed";
            result.error = "Queue '" + item.queue + "' does not exist. Please create it using the configure endpoint first.";
            return result;
        }
        
        // Ensure partition exists
        if (!ensure_partition_exists(conn.get(), item.queue, item.partition)) {
            result.status = "failed";
            result.error = "Failed to create partition";
            return result;
        }
        
        // Generate UUIDv7 for message ID
        std::string message_id = generate_uuid();
        
        std::string sql;
        std::vector<std::string> params;
        
        if (item.trace_id.has_value() && !item.trace_id->empty()) {
            sql = R"(
                INSERT INTO queen.messages (
                    id, transaction_id, partition_id, payload, trace_id, created_at
                )
                SELECT $1, $2, p.id, $3, $4, NOW()
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $5 AND p.name = $6
                RETURNING id, trace_id
            )";
            
            params = {
                message_id,
                result.transaction_id,
                item.payload.dump(),
                item.trace_id.value(),
                item.queue,
                item.partition
            };
        } else {
            sql = R"(
                INSERT INTO queen.messages (
                    id, transaction_id, partition_id, payload, created_at
                )
                SELECT $1, $2, p.id, $3, NOW()
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $4 AND p.name = $5
                RETURNING id, trace_id
            )";
            
            params = {
                message_id,
                result.transaction_id,
                item.payload.dump(),
                item.queue,
                item.partition
            };
        }
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto query_result = getTuplesResult(conn.get());
        
        if (PQntuples(query_result.get()) > 0) {
            result.status = "queued";
            result.message_id = PQgetvalue(query_result.get(), 0, 0);
            const char* trace_val = PQgetvalue(query_result.get(), 0, 1);
            if (trace_val && std::string(trace_val).length() > 0) {
                result.trace_id = trace_val;
            }
        } else {
            result.status = "failed";
            result.error = "Insert returned no result";
        }
        
    } catch (const std::exception& e) {
        // Database push failed - try file buffer failover
        spdlog::warn("Database push failed: {}. Attempting file buffer failover...", e.what());
        
        if (file_buffer_manager_) {
            // Mark database as unhealthy for faster failover on subsequent requests
            file_buffer_manager_->mark_db_unhealthy();
            
            try {
                nlohmann::json event = {
                    {"queue", item.queue},
                    {"partition", item.partition},
                    {"payload", item.payload},
                    {"transactionId", result.transaction_id},
                    {"failover", true}  // Failover buffer (FIFO, will be replayed when DB recovers)
                };
                
                if (item.trace_id.has_value() && !item.trace_id->empty()) {
                    event["traceId"] = *item.trace_id;
                }
                
                if (file_buffer_manager_->write_event(event)) {
                    spdlog::info("Message successfully failed over to file buffer");
                    result.status = "buffered";
                    result.message_id = std::nullopt;
                    result.trace_id = item.trace_id;
                    result.error = std::nullopt;  // Clear error since failover succeeded
                    return result;
                } else {
                    spdlog::error("File buffer write failed during failover");
                    result.status = "failed";
                    result.error = std::string("Database error: ") + e.what() + "; File buffer also failed";
                    return result;
                }
            } catch (const std::exception& fb_error) {
                spdlog::error("File buffer failover exception: {}", fb_error.what());
                result.status = "failed";
                result.error = std::string("Database error: ") + e.what() + "; Failover error: " + fb_error.what();
                return result;
            }
        } else {
            // No file buffer configured - return database error
            spdlog::error("Database push failed and no file buffer configured: {}", e.what());
            result.status = "failed";
            result.error = e.what();
            return result;
        }
    }
    
    return result;
}

// --- Push Messages (Batch Entry Point) ---

std::vector<PushResult> AsyncQueueManager::push_messages(const std::vector<PushItem>& items) {
    if (items.empty()) {
        return {};
    }
    
    // MAINTENANCE MODE: Route all items to file buffer
    bool is_maintenance = check_maintenance_mode_with_cache();
    spdlog::debug("push_messages: maintenance_mode_check={}, has_file_buffer={}", 
                  is_maintenance, file_buffer_manager_ != nullptr);
    
    if (is_maintenance && file_buffer_manager_) {
        spdlog::info("push_messages: Routing {} messages to file buffer (maintenance mode enabled)", items.size());
        std::vector<PushResult> results;
        results.reserve(items.size());
        
        for (const auto& item : items) {
            try {
                std::string transaction_id = item.transaction_id.value_or(generate_transaction_id());
                
                nlohmann::json event = {
                    {"queue", item.queue},
                    {"partition", item.partition},
                    {"payload", item.payload},
                    {"transactionId", transaction_id},
                    {"failover", true}
                };
                
                if (item.trace_id.has_value() && !item.trace_id->empty()) {
                    event["traceId"] = *item.trace_id;
                }
                
                if (file_buffer_manager_->write_event(event)) {
                    results.push_back({
                        transaction_id,
                        "buffered",
                        std::nullopt,
                        std::nullopt,
                        item.trace_id
                    });
                } else {
                    results.push_back({
                        transaction_id,
                        "failed",
                        "File buffer write failed",
                        std::nullopt,
                        std::nullopt
                    });
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to write to file buffer during maintenance mode: {}", e.what());
                results.push_back({
                    item.transaction_id.value_or(generate_transaction_id()),
                    "failed",
                    std::string("Buffer write error: ") + e.what(),
                    std::nullopt,
                    std::nullopt
                });
            }
        }
        
        return results;
    }
    
    // Group items by queue and partition while PRESERVING ORDER
    std::vector<std::pair<std::string, std::vector<size_t>>> partition_groups_ordered;
    std::map<std::string, size_t> key_to_index;
    
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        std::string key = item.queue + ":" + item.partition;
        
        if (key_to_index.find(key) == key_to_index.end()) {
            key_to_index[key] = partition_groups_ordered.size();
            partition_groups_ordered.push_back({key, {i}});
        } else {
            size_t group_idx = key_to_index[key];
            partition_groups_ordered[group_idx].second.push_back(i);
        }
    }
    
    std::vector<PushResult> all_results(items.size());
    
    // Batch capacity check - check ALL queues in ONE query
    try {
        // Build map of queue -> total batch size across all partitions
        std::map<std::string, int> queue_batch_sizes;
        for (const auto& [partition_key, item_indices] : partition_groups_ordered) {
            size_t colon_pos = partition_key.find(':');
            std::string queue_name = partition_key.substr(0, colon_pos);
            queue_batch_sizes[queue_name] += item_indices.size();
        }
        
        // Check capacity for all queues with max_queue_size set
        if (!queue_batch_sizes.empty()) {
            std::vector<std::string> queues_to_check;
            for (const auto& [queue_name, _] : queue_batch_sizes) {
                queues_to_check.push_back(queue_name);
            }
            
            // Build PostgreSQL array
            auto build_pg_array = [](const std::vector<std::string>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    result += "\"";
                    for (char c : vec[i]) {
                        if (c == '\\' || c == '"') result += '\\';
                        result += c;
                    }
                    result += "\"";
                }
                result += "}";
                return result;
            };
            
            std::string queues_array = build_pg_array(queues_to_check);
            
            auto capacity_conn = async_db_pool_->acquire();
            std::string capacity_check_sql = R"(
                SELECT 
                  q.name,
                  q.max_queue_size,
                  (
                    SELECT COUNT(m.id)::integer
                    FROM queen.messages m
                    JOIN queen.partitions p ON p.id = m.partition_id
                    LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id
                      AND pc.consumer_group = '__QUEUE_MODE__'
                    WHERE p.queue_id = q.id
                      AND (pc.last_consumed_created_at IS NULL 
                           OR m.created_at > pc.last_consumed_created_at
                           OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
                  ) as current_depth
                FROM queen.queues q
                WHERE q.name = ANY($1::varchar[])
                  AND q.max_queue_size > 0
            )";
            
            sendQueryParamsAsync(capacity_conn.get(), capacity_check_sql, {queues_array});
            auto capacity_result = getTuplesResult(capacity_conn.get());
            
            // Check each queue's capacity
            for (int i = 0; i < PQntuples(capacity_result.get()); ++i) {
                std::string queue_name = PQgetvalue(capacity_result.get(), i, 0);
                const char* max_size_str = PQgetvalue(capacity_result.get(), i, 1);
                const char* current_depth_str = PQgetvalue(capacity_result.get(), i, 2);
                
                int max_size = (max_size_str && strlen(max_size_str) > 0) ? std::stoi(max_size_str) : 0;
                int current_depth = (current_depth_str && strlen(current_depth_str) > 0) ? std::stoi(current_depth_str) : 0;
                int batch_size = queue_batch_sizes[queue_name];
                
                if (current_depth + batch_size > max_size) {
                    // Queue would exceed capacity - fail all messages for this queue
                    std::string error_msg = "Queue '" + queue_name + "' would exceed max capacity (" +
                                          std::to_string(max_size) + "). Current: " + 
                                          std::to_string(current_depth) + ", Batch: " + 
                                          std::to_string(batch_size);
                    
                    spdlog::warn(error_msg);
                    
                    // Find all items for this queue and mark them as failed
                    for (size_t item_idx = 0; item_idx < items.size(); ++item_idx) {
                        if (items[item_idx].queue == queue_name) {
                            PushResult failed_result;
                            failed_result.transaction_id = items[item_idx].transaction_id.value_or(generate_transaction_id());
                            failed_result.status = "failed";
                            failed_result.error = error_msg;
                            all_results[item_idx] = failed_result;
                        }
                    }
                    
                    // Remove this queue from partition_groups_ordered
                    partition_groups_ordered.erase(
                        std::remove_if(partition_groups_ordered.begin(), partition_groups_ordered.end(),
                            [&queue_name](const auto& pair) {
                                return pair.first.substr(0, pair.first.find(':')) == queue_name;
                            }),
                        partition_groups_ordered.end()
                    );
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Batch capacity check failed: {}", e.what());
        // Continue with push - individual checks will catch issues
    }
    
    // Process each partition group in order
    for (const auto& [partition_key, item_indices] : partition_groups_ordered) {
        // Build items vector for this partition batch
        std::vector<PushItem> partition_items;
        partition_items.reserve(item_indices.size());
        for (size_t idx : item_indices) {
            partition_items.push_back(items[idx]);
        }
        
        auto batch_results = push_messages_batch(partition_items);
        
        // Place results back in original positions
        for (size_t i = 0; i < batch_results.size(); ++i) {
            all_results[item_indices[i]] = batch_results[i];
        }
    }
    
    return all_results;
}

// --- Size Estimation ---

size_t AsyncQueueManager::estimate_row_size(const PushItem& item, bool encryption_enabled) const {
    size_t size = 500; // Fixed overhead
    size += 20;  // id (UUID)
    size += 40;  // transaction_id
    size += 20;  // trace_id
    size += 20;  // partition_id
    size += 8;   // created_at
    size += 1;   // is_encrypted
    
    std::string payload_str = item.payload.dump();
    size += payload_str.size();
    
    if (encryption_enabled) {
        size += static_cast<size_t>(payload_str.size() * 0.35) + 200;
    }
    
    size += 50;   // JSONB overhead
    size += 250;  // Index overhead
    size += size / 10; // Fragmentation
    
    return size;
}

// --- Push Messages Batch ---

std::vector<PushResult> AsyncQueueManager::push_messages_batch(const std::vector<PushItem>& items) {
    if (items.empty()) {
        return {};
    }
    
    const std::string& queue_name = items[0].queue;
    const std::string& partition_name = items[0].partition;
    
    std::vector<PushResult> results;
    results.reserve(items.size());
    
    // Dynamic size-based batching
    if (config_.batch_push_use_size_based) {
        // Check if queue has encryption enabled
        bool encryption_enabled = false;
        try {
            auto check_conn = async_db_pool_->acquire();
            sendQueryParamsAsync(check_conn.get(), 
                                "SELECT encryption_enabled FROM queen.queues WHERE name = $1", 
                                {queue_name});
            auto encryption_result = getTuplesResult(check_conn.get());
            
            if (PQntuples(encryption_result.get()) > 0) {
                const char* enc_str = PQgetvalue(encryption_result.get(), 0, 0);
                encryption_enabled = (enc_str && (std::string(enc_str) == "t" || std::string(enc_str) == "true"));
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to check encryption status, assuming disabled: {}", e.what());
        }
        
        const size_t TARGET_BATCH_SIZE = static_cast<size_t>(config_.batch_push_target_size_mb) * 1024 * 1024;
        const size_t MIN_BATCH_SIZE = static_cast<size_t>(config_.batch_push_min_size_mb) * 1024 * 1024;
        const size_t MAX_BATCH_SIZE = static_cast<size_t>(config_.batch_push_max_size_mb) * 1024 * 1024;
        const size_t MIN_MESSAGES = static_cast<size_t>(config_.batch_push_min_messages);
        const size_t MAX_MESSAGES = static_cast<size_t>(config_.batch_push_max_messages);
        
        size_t accumulated_size = 0;
        std::vector<PushItem> current_batch;
        
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            size_t row_size = estimate_row_size(item, encryption_enabled);
            
            bool should_flush = false;
            
            if (!current_batch.empty()) {
                if (accumulated_size + row_size > MAX_BATCH_SIZE) {
                    should_flush = true;
                    spdlog::debug("Flushing batch: would exceed MAX_BATCH_SIZE ({} MB)", 
                                config_.batch_push_max_size_mb);
                }
                else if (current_batch.size() >= MAX_MESSAGES) {
                    should_flush = true;
                    spdlog::debug("Flushing batch: hit MAX_MESSAGES ({})", MAX_MESSAGES);
                }
                else if (accumulated_size >= TARGET_BATCH_SIZE && 
                         current_batch.size() >= MIN_MESSAGES) {
                    should_flush = true;
                    spdlog::debug("Flushing batch: reached TARGET_BATCH_SIZE ({} MB) with {} messages", 
                                config_.batch_push_target_size_mb, current_batch.size());
                }
            }
            
            if (should_flush) {
                spdlog::debug("Processing batch: {} messages, ~{} MB", 
                            current_batch.size(), 
                            accumulated_size / (1024.0 * 1024.0));
                
                auto chunk_results = push_messages_chunk(current_batch, queue_name, partition_name);
                results.insert(results.end(), chunk_results.begin(), chunk_results.end());
                
                current_batch.clear();
                accumulated_size = 0;
            }
            
            current_batch.push_back(item);
            accumulated_size += row_size;
        }
        
        // Process final batch
        if (!current_batch.empty()) {
            spdlog::debug("Processing final batch: {} messages, ~{} MB", 
                        current_batch.size(), 
                        accumulated_size / (1024.0 * 1024.0));
            
            auto chunk_results = push_messages_chunk(current_batch, queue_name, partition_name);
            results.insert(results.end(), chunk_results.begin(), chunk_results.end());
        }
        
    } else {
        // Legacy count-based batching
        const size_t CHUNK_SIZE = config_.batch_push_chunk_size;
        spdlog::debug("Using legacy count-based batching: {} messages per chunk", CHUNK_SIZE);
        
        for (size_t chunk_start = 0; chunk_start < items.size(); chunk_start += CHUNK_SIZE) {
            size_t chunk_end = std::min(chunk_start + CHUNK_SIZE, items.size());
            std::vector<PushItem> chunk(items.begin() + chunk_start, items.begin() + chunk_end);
            
            auto chunk_results = push_messages_chunk(chunk, queue_name, partition_name);
            results.insert(results.end(), chunk_results.begin(), chunk_results.end());
        }
    }
    
    return results;
}

// --- Push Messages Chunk (Core Batch INSERT with Async DB) ---

std::vector<PushResult> AsyncQueueManager::push_messages_chunk(const std::vector<PushItem>& items,
                                                               const std::string& queue_name,
                                                               const std::string& partition_name) {
    auto chunk_start = std::chrono::steady_clock::now();
    std::vector<PushResult> results;
    results.reserve(items.size());
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Check if queue exists
        auto queue_check_start = std::chrono::steady_clock::now();
        sendQueryParamsAsync(conn.get(), "SELECT id FROM queen.queues WHERE name = $1", {queue_name});
        auto queue_check = getTuplesResult(conn.get());
        auto queue_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - queue_check_start).count();
        spdlog::debug("TIMING: Queue check took {}ms", queue_check_ms);
        
        if (PQntuples(queue_check.get()) == 0) {
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                result.status = "failed";
                result.error = "Queue '" + queue_name + "' does not exist. Please create it using the configure endpoint first.";
                results.push_back(result);
            }
            return results;
        }
        
        // Ensure partition exists
        auto partition_check_start = std::chrono::steady_clock::now();
        if (!ensure_partition_exists(conn.get(), queue_name, partition_name)) {
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                result.status = "failed";
                result.error = "Failed to create partition";
                results.push_back(result);
            }
            return results;
        }
        auto partition_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - partition_check_start).count();
        spdlog::debug("TIMING: Partition check took {}ms", partition_check_ms);
        
        // Check encryption enabled
        auto encryption_check_start = std::chrono::steady_clock::now();
        sendQueryParamsAsync(conn.get(), "SELECT encryption_enabled FROM queen.queues WHERE name = $1", {queue_name});
        auto encryption_result = getTuplesResult(conn.get());
        
        bool encryption_enabled = false;
        if (PQntuples(encryption_result.get()) > 0) {
            const char* enc_str = PQgetvalue(encryption_result.get(), 0, 0);
            encryption_enabled = (enc_str && (std::string(enc_str) == "t" || std::string(enc_str) == "true"));
        }
        
        // Get encryption service if needed
        EncryptionService* enc_service = encryption_enabled ? get_encryption_service() : nullptr;
        
        // Get partition ID
        std::string partition_id_sql = R"(
            SELECT p.id FROM queen.partitions p
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
        )";
        sendQueryParamsAsync(conn.get(), partition_id_sql, {queue_name, partition_name});
        auto partition_result = getTuplesResult(conn.get());
        
        if (PQntuples(partition_result.get()) == 0) {
            for (const auto& item : items) {
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                result.status = "failed";
                result.error = "Partition not found";
                results.push_back(result);
            }
            return results;
        }
        
        std::string partition_id = PQgetvalue(partition_result.get(), 0, 0);
        auto encryption_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - encryption_check_start).count();
        spdlog::debug("TIMING: Encryption check + partition ID lookup took {}ms", encryption_check_ms);
        
        // Begin transaction
        auto txn_start = std::chrono::steady_clock::now();
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            auto build_arrays_start = std::chrono::steady_clock::now();
            
            // Build arrays for batch insert
            std::vector<std::string> message_ids;
            std::vector<std::string> transaction_ids;
            std::vector<std::string> payloads;
            std::vector<std::string> trace_ids;
            std::vector<std::string> partition_ids;
            std::vector<bool> encrypted_flags;
            
            for (size_t idx = 0; idx < items.size(); ++idx) {
                const auto& item = items[idx];
                PushResult result;
                result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                
                std::string msg_id = generate_uuid();
                message_ids.push_back(msg_id);
                transaction_ids.push_back(result.transaction_id);
                partition_ids.push_back(partition_id);
                
                // Prepare payload - encrypt if needed
                std::string payload_to_store;
                bool is_encrypted = false;
                
                if (enc_service && enc_service->is_enabled()) {
                    auto encrypted = enc_service->encrypt_payload(item.payload.dump());
                    if (encrypted.has_value()) {
                        nlohmann::json encrypted_json = {
                            {"encrypted", encrypted->encrypted},
                            {"iv", encrypted->iv},
                            {"authTag", encrypted->auth_tag}
                        };
                        payload_to_store = encrypted_json.dump();
                        is_encrypted = true;
                    } else {
                        spdlog::warn("Encryption failed for message, storing unencrypted");
                        payload_to_store = item.payload.dump();
                    }
                } else {
                    payload_to_store = item.payload.dump();
                }
                
                payloads.push_back(payload_to_store);
                trace_ids.push_back(item.trace_id.value_or(""));
                encrypted_flags.push_back(is_encrypted);
                results.push_back(result);
            }
            auto build_arrays_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - build_arrays_start).count();
            spdlog::debug("TIMING: Building arrays took {}ms", build_arrays_ms);
            
            // Duplicate detection
            auto dup_check_start = std::chrono::steady_clock::now();
            std::map<std::string, std::string> existing_txn_map;
            
            bool has_explicit_txn_ids = false;
            for (const auto& item : items) {
                if (item.transaction_id.has_value() && !item.transaction_id->empty()) {
                    has_explicit_txn_ids = true;
                    break;
                }
            }
            
            if (has_explicit_txn_ids) {
                // Build array of transaction IDs
                auto build_pg_array = [](const std::vector<std::string>& vec) -> std::string {
                    std::string result = "{";
                    for (size_t i = 0; i < vec.size(); ++i) {
                        if (i > 0) result += ",";
                        result += "\"";
                        for (char c : vec[i]) {
                            if (c == '\\' || c == '"') result += '\\';
                            result += c;
                        }
                        result += "\"";
                    }
                    result += "}";
                    return result;
                };
                
                std::string txn_check_array = build_pg_array(transaction_ids);
                
                std::string dup_check_sql = R"(
                    SELECT t.txn_id as transaction_id, m.id as message_id
                    FROM UNNEST($1::varchar[]) AS t(txn_id)
                    LEFT JOIN queen.messages m ON m.transaction_id = t.txn_id 
                        AND m.partition_id = $2::uuid
                    WHERE m.id IS NOT NULL
                )";
                
                sendQueryParamsAsync(conn.get(), dup_check_sql, {txn_check_array, partition_id});
                auto dup_result = getTuplesResult(conn.get());
                
                for (int i = 0; i < PQntuples(dup_result.get()); ++i) {
                    std::string txn_id = PQgetvalue(dup_result.get(), i, 0);
                    std::string msg_id = PQgetvalue(dup_result.get(), i, 1);
                    existing_txn_map[txn_id] = msg_id;
                }
                
                if (!existing_txn_map.empty()) {
                    spdlog::debug("Found {} duplicate transaction IDs in batch of {}", 
                                existing_txn_map.size(), transaction_ids.size());
                }
            }
            auto dup_check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - dup_check_start).count();
            spdlog::debug("TIMING: Duplicate check took {}ms (found {} duplicates)", dup_check_ms, existing_txn_map.size());
            
            // Filter out duplicates
            auto filter_start = std::chrono::steady_clock::now();
            std::vector<std::string> filtered_message_ids;
            std::vector<std::string> filtered_transaction_ids;
            std::vector<std::string> filtered_partition_ids;
            std::vector<std::string> filtered_payloads;
            std::vector<std::string> filtered_trace_ids;
            std::vector<bool> filtered_encrypted_flags;
            std::vector<size_t> filtered_result_indices;
            
            for (size_t i = 0; i < transaction_ids.size(); ++i) {
                auto it = existing_txn_map.find(transaction_ids[i]);
                if (it != existing_txn_map.end()) {
                    results[i].status = "duplicate";
                    results[i].message_id = it->second;
                    continue;
                }
                
                filtered_message_ids.push_back(message_ids[i]);
                filtered_transaction_ids.push_back(transaction_ids[i]);
                filtered_partition_ids.push_back(partition_ids[i]);
                filtered_payloads.push_back(payloads[i]);
                filtered_trace_ids.push_back(trace_ids[i]);
                filtered_encrypted_flags.push_back(encrypted_flags[i]);
                filtered_result_indices.push_back(i);
            }
            
            // Build PostgreSQL arrays
            auto build_pg_array = [](const std::vector<std::string>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    result += "\"";
                    for (char c : vec[i]) {
                        if (c == '\\' || c == '"') result += '\\';
                        result += c;
                    }
                    result += "\"";
                }
                result += "}";
                return result;
            };
            
            auto build_pg_array_with_nulls = [](const std::vector<std::string>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    if (vec[i].empty()) {
                        result += "NULL";
                    } else {
                        result += "\"";
                        for (char c : vec[i]) {
                            if (c == '\\' || c == '"') result += '\\';
                            result += c;
                        }
                        result += "\"";
                    }
                }
                result += "}";
                return result;
            };
            
            auto build_bool_array = [](const std::vector<bool>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    result += vec[i] ? "true" : "false";
                }
                result += "}";
                return result;
            };
            
            std::string ids_array = build_pg_array(filtered_message_ids);
            std::string txn_array = build_pg_array(filtered_transaction_ids);
            std::string part_array = build_pg_array(filtered_partition_ids);
            std::string payload_array = build_pg_array(filtered_payloads);
            std::string trace_array = build_pg_array_with_nulls(filtered_trace_ids);
            std::string encrypted_array = build_bool_array(filtered_encrypted_flags);
            auto filter_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - filter_start).count();
            spdlog::debug("TIMING: Filtering and building PG arrays took {}ms ({} items to insert)", filter_ms, filtered_message_ids.size());
            
            // Batch INSERT
            if (!filtered_message_ids.empty()) {
                auto insert_start = std::chrono::steady_clock::now();
                std::string sql = R"(
                    INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id, is_encrypted)
                    SELECT * FROM UNNEST(
                        $1::uuid[],
                        $2::varchar[],
                        $3::uuid[],
                        $4::jsonb[],
                        $5::uuid[],
                        $6::boolean[]
                    )
                    RETURNING id, transaction_id, trace_id
                )";
                
                std::vector<std::string> unnest_params = {
                    ids_array,
                    txn_array,
                    part_array,
                    payload_array,
                    trace_array,
                    encrypted_array
                };
                
                sendQueryParamsAsync(conn.get(), sql, unnest_params);
                auto insert_result = getTuplesResult(conn.get());
                auto insert_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - insert_start).count();
                spdlog::debug("TIMING: INSERT took {}ms ({} items)", insert_ms, filtered_message_ids.size());
                
                // Update results
                for (int i = 0; i < PQntuples(insert_result.get()) && i < static_cast<int>(filtered_result_indices.size()); ++i) {
                    size_t result_idx = filtered_result_indices[i];
                    results[result_idx].status = "queued";
                    results[result_idx].message_id = PQgetvalue(insert_result.get(), i, 0);
                    const char* trace_val = PQgetvalue(insert_result.get(), i, 2);
                    if (trace_val && std::string(trace_val).length() > 0) {
                        results[result_idx].trace_id = trace_val;
                    }
                }
            }
            
            // Commit
            auto commit_start = std::chrono::steady_clock::now();
            sendAndWait(conn.get(), "COMMIT");
            getCommandResult(conn.get());
            auto commit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - commit_start).count();
            spdlog::debug("TIMING: COMMIT took {}ms", commit_ms);
            
            auto txn_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - txn_start).count();
            spdlog::debug("TIMING: Total transaction took {}ms", txn_total_ms);
            
        } catch (const std::exception& e) {
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        // Database batch push failed - try file buffer failover for all items
        spdlog::warn("Database batch push failed: {}. Attempting file buffer failover for {} items...", 
                    e.what(), items.size());
        
        if (file_buffer_manager_) {
            // Mark database as unhealthy for faster failover on subsequent requests
            file_buffer_manager_->mark_db_unhealthy();
            
            // Try to failover all items to file buffer
            size_t failover_success_count = 0;
            for (size_t i = 0; i < items.size(); ++i) {
                const auto& item = items[i];
                
                // Initialize result if empty
                if (i >= results.size()) {
                    PushResult result;
                    result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
                    results.push_back(result);
                }
                
                try {
                    nlohmann::json event = {
                        {"queue", item.queue},
                        {"partition", item.partition},
                        {"payload", item.payload},
                        {"transactionId", results[i].transaction_id},
                        {"failover", true}  // Failover buffer (FIFO, will be replayed when DB recovers)
                    };
                    
                    if (item.trace_id.has_value() && !item.trace_id->empty()) {
                        event["traceId"] = *item.trace_id;
                    }
                    
                    if (file_buffer_manager_->write_event(event)) {
                        results[i].status = "buffered";
                        results[i].message_id = std::nullopt;
                        results[i].trace_id = item.trace_id;
                        results[i].error = std::nullopt;
                        failover_success_count++;
                    } else {
                        results[i].status = "failed";
                        results[i].error = std::string("Database error: ") + e.what() + "; File buffer write failed";
                    }
                } catch (const std::exception& fb_error) {
                    spdlog::error("File buffer failover exception for item {}: {}", i, fb_error.what());
                    results[i].status = "failed";
                    results[i].error = std::string("Database error: ") + e.what() + "; Failover error: " + fb_error.what();
                }
            }
            
            spdlog::info("Batch failover complete: {}/{} messages buffered successfully", 
                        failover_success_count, items.size());
        } else {
            // No file buffer configured - mark all as failed with database error
            for (auto& result : results) {
                if (result.status.empty()) {
                    result.status = "failed";
                    result.error = e.what();
                }
            }
            spdlog::error("Batch push failed and no file buffer configured: {}", e.what());
        }
    }
    
    auto chunk_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - chunk_start).count();
    spdlog::debug("TIMING: Total chunk processing took {}ms ({} items)", chunk_total_ms, items.size());
    
    return results;
}

// --- Pool Statistics ---

AsyncQueueManager::PoolStats AsyncQueueManager::get_pool_stats() const {
    return {
        async_db_pool_->size(),
        async_db_pool_->available(),
        async_db_pool_->size() - async_db_pool_->available()
    };
}

// --- Health Check ---

bool AsyncQueueManager::health_check() {
    try {
        auto conn = async_db_pool_->acquire();
        sendAndWait(conn.get(), "SELECT 1");
        auto result = getTuplesResult(conn.get());
        return PQntuples(result.get()) > 0;
    } catch (const std::exception& e) {
        spdlog::error("Health check failed: {}", e.what());
        return false;
    }
}

// ===================================
// POP OPERATIONS - ASYNC VERSIONS
// ===================================

// --- Lease Management ---

std::string AsyncQueueManager::acquire_partition_lease(
    PGconn* conn,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group,
    int lease_time_seconds,
    const PopOptions& options
) {
    try {
        // Reclaim expired leases FIRST
        std::string reclaim_sql = R"(
            UPDATE queen.partition_consumers
            SET lease_expires_at = NULL,
                lease_acquired_at = NULL,
                message_batch = NULL,
                batch_size = 0,
                acked_count = 0,
                worker_id = NULL
            WHERE lease_expires_at IS NOT NULL
              AND lease_expires_at < NOW()
        )";
        
        sendAndWait(conn, reclaim_sql.c_str());
        getCommandResult(conn);
        spdlog::debug("Reclaimed expired leases");
        
        std::string lease_id = generate_uuid();
        
        // Check subscription preferences for initial cursor
        std::string initial_cursor_id = "00000000-0000-0000-0000-000000000000";
        std::string initial_cursor_timestamp_sql = "NULL";
        
        if (consumer_group != "__QUEUE_MODE__" && 
            (options.subscription_mode.has_value() || options.subscription_from.has_value())) {
            
            std::string sub_mode = options.subscription_mode.value_or("");
            std::string sub_from = options.subscription_from.value_or("");
            
            // For 'new', 'new-only', or 'now' - start from latest message
            if (sub_mode == "new" || sub_mode == "new-only" || sub_from == "now") {
                std::string latest_sql = R"(
                    SELECT m.id, m.created_at
                    FROM queen.messages m
                    JOIN queen.partitions p ON m.partition_id = p.id
                    JOIN queen.queues q ON p.queue_id = q.id
                    WHERE q.name = $1 AND p.name = $2
                    ORDER BY m.created_at DESC, m.id DESC
                    LIMIT 1
                )";
                
                sendQueryParamsAsync(conn, latest_sql, {queue_name, partition_name});
                auto latest_result = getTuplesResult(conn);
                
                if (PQntuples(latest_result.get()) > 0) {
                    initial_cursor_id = PQgetvalue(latest_result.get(), 0, 0);
                    initial_cursor_timestamp_sql = "'" + std::string(PQgetvalue(latest_result.get(), 0, 1)) + "'";
                    spdlog::debug("Subscription mode '{}' - starting from latest message: {}", sub_mode, initial_cursor_id);
                }
            }
        }
        
        // Check if consumer group already exists
        std::string check_sql = R"(
            SELECT pc.id FROM queen.partition_consumers pc
            JOIN queen.partitions p ON pc.partition_id = p.id
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2 AND pc.consumer_group = $3
        )";
        
        sendQueryParamsAsync(conn, check_sql, {queue_name, partition_name, consumer_group});
        auto check_result = getTuplesResult(conn);
        bool consumer_exists = PQntuples(check_result.get()) > 0;
        
        spdlog::debug("Consumer group '{}' exists: {}, has subscription options: {}", 
                     consumer_group, consumer_exists, 
                     (options.subscription_mode.has_value() || options.subscription_from.has_value()));
        
        std::string sql;
        std::vector<std::string> params;
        
        if (!consumer_exists && consumer_group != "__QUEUE_MODE__" && 
            (options.subscription_mode.has_value() || options.subscription_from.has_value())) {
            // New consumer group with subscription preferences
            spdlog::debug("Creating new consumer group '{}' with cursor at: {}", consumer_group, initial_cursor_id);
            
            sql = R"(
                INSERT INTO queen.partition_consumers (
                    partition_id, consumer_group, lease_expires_at, lease_acquired_at, worker_id,
                    last_consumed_id, last_consumed_created_at
                )
                SELECT p.id, $1, NOW() + INTERVAL '1 second' * $2, NOW(), $3, 
                       $6::uuid, )" + initial_cursor_timestamp_sql + R"(
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $4 AND p.name = $5
                ON CONFLICT (partition_id, consumer_group) DO NOTHING
                RETURNING worker_id
            )";
            
            params = {
                consumer_group,
                std::to_string(lease_time_seconds),
                lease_id,
                queue_name,
                partition_name,
                initial_cursor_id
            };
        } else {
            // Existing consumer or no subscription preference
            sql = R"(
                INSERT INTO queen.partition_consumers (
                    partition_id, consumer_group, lease_expires_at, lease_acquired_at, worker_id
                )
                SELECT p.id, $1, NOW() + INTERVAL '1 second' * $2, NOW(), $3
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $4 AND p.name = $5
                ON CONFLICT (partition_id, consumer_group) DO UPDATE SET
                    lease_expires_at = NOW() + INTERVAL '1 second' * $2,
                    lease_acquired_at = NOW(),
                    worker_id = $3
                WHERE partition_consumers.lease_expires_at IS NULL 
                   OR partition_consumers.lease_expires_at <= NOW()
                RETURNING worker_id
            )";
            
            params = {
                consumer_group,
                std::to_string(lease_time_seconds),
                lease_id,
                queue_name,
                partition_name
            };
        }
        
        spdlog::debug("Lease acquisition query params: consumer_group={}, lease_time={}, lease_id={}, queue={}, partition={}", 
                     consumer_group, lease_time_seconds, lease_id, queue_name, partition_name);
        
        sendQueryParamsAsync(conn, sql, params);
        auto result = getTuplesResult(conn);
        
        spdlog::debug("Lease acquisition returned {} rows", PQntuples(result.get()));
        
        if (PQntuples(result.get()) > 0) {
            spdlog::debug("Lease acquired successfully: {}", lease_id);
            return lease_id;
        } else {
            spdlog::debug("Lease acquisition failed - partition may be locked by another consumer");
            return "";
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to acquire lease: {}", e.what());
        return "";
    }
}

void AsyncQueueManager::release_partition_lease(
    PGconn* conn,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group
) {
    try {
        std::string release_sql = R"(
            UPDATE queen.partition_consumers
            SET lease_expires_at = NULL,
                lease_acquired_at = NULL,
                worker_id = NULL
            WHERE partition_id = (
                SELECT p.id FROM queen.partitions p
                JOIN queen.queues q ON p.queue_id = q.id
                WHERE q.name = $1 AND p.name = $2
            )
            AND consumer_group = $3
        )";
        
        sendQueryParamsAsync(conn, release_sql, {queue_name, partition_name, consumer_group});
        getCommandResult(conn);
        
    } catch (const std::exception& e) {
        spdlog::warn("Failed to release lease: {}", e.what());
    }
}

bool AsyncQueueManager::ensure_consumer_group_exists(
    PGconn* conn,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group
) {
    try {
        std::string sql = R"(
            INSERT INTO queen.partition_consumers (partition_id, consumer_group)
            SELECT p.id, $3
            FROM queen.partitions p
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
            ON CONFLICT (partition_id, consumer_group) DO NOTHING
        )";
        
        sendQueryParamsAsync(conn, sql, {queue_name, partition_name, consumer_group});
        getCommandResult(conn);
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to ensure consumer group exists: {}", e.what());
        return false;
    }
}

// --- Pop from Specific Partition ---

PopResult AsyncQueueManager::pop_messages_from_partition(
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& consumer_group,
    const PopOptions& options
) {
    PopResult result;
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Check if partition is accessible considering window_buffer
        std::string window_check_sql = R"(
            SELECT p.id, q.window_buffer
            FROM queen.partitions p
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE q.name = $1 AND p.name = $2
              AND (q.window_buffer = 0 OR NOT EXISTS (
                SELECT 1 FROM queen.messages m
                WHERE m.partition_id = p.id
                  AND m.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
              ))
        )";
        
        sendQueryParamsAsync(conn.get(), window_check_sql, {queue_name, partition_name});
        auto window_result = getTuplesResult(conn.get());
        
        if (PQntuples(window_result.get()) == 0) {
            spdlog::debug("Partition {} blocked by window buffer", partition_name);
            return result;
        }
        
        // Get queue configuration for delayed_processing, max_wait_time_seconds, and lease_time
        std::string config_sql = R"(
            SELECT q.delayed_processing, q.max_wait_time_seconds, q.lease_time
            FROM queen.queues q
            WHERE q.name = $1
        )";
        
        sendQueryParamsAsync(conn.get(), config_sql, {queue_name});
        auto config_result = getTuplesResult(conn.get());
        
        int delayed_processing = 0;
        int max_wait_time = 0;
        int lease_time = 300;
        
        if (PQntuples(config_result.get()) > 0) {
            const char* delay_str = PQgetvalue(config_result.get(), 0, 0);
            const char* wait_str = PQgetvalue(config_result.get(), 0, 1);
            const char* lease_str = PQgetvalue(config_result.get(), 0, 2);
            
            delayed_processing = (delay_str && strlen(delay_str) > 0) ? std::stoi(delay_str) : 0;
            max_wait_time = (wait_str && strlen(wait_str) > 0) ? std::stoi(wait_str) : 0;
            lease_time = (lease_str && strlen(lease_str) > 0) ? std::stoi(lease_str) : 300;
        }
        
        // Acquire lease for this partition
        std::string lease_id = acquire_partition_lease(conn.get(), queue_name, partition_name, 
                                                      consumer_group, lease_time, options);
        if (lease_id.empty()) {
            return result;
        }
        
        result.lease_id = lease_id;
        
        // Build WHERE clause
        std::string where_clause = R"(
            WHERE q.name = $1 AND p.name = $2 AND pc.consumer_group = $3
              AND pc.worker_id = $4 AND pc.lease_expires_at > NOW()
              AND (pc.last_consumed_created_at IS NULL 
                   OR m.created_at > pc.last_consumed_created_at
                   OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
        )";
        
        std::vector<std::string> params = {
            queue_name,
            partition_name,
            consumer_group,
            lease_id
        };
        
        // Add delayed processing filter
        if (delayed_processing > 0) {
            where_clause += " AND m.created_at <= NOW() - INTERVAL '1 second' * $" + std::to_string(params.size() + 1);
            params.push_back(std::to_string(delayed_processing));
            spdlog::debug("Delayed processing filter active: {} seconds", delayed_processing);
        }
        
        // Add max wait time filter
        if (max_wait_time > 0) {
            where_clause += " AND m.created_at > NOW() - INTERVAL '1 second' * $" + std::to_string(params.size() + 1);
            params.push_back(std::to_string(max_wait_time));
        }
        
        // Add batch limit
        params.push_back(std::to_string(options.batch));
        std::string limit_param = "$" + std::to_string(params.size());
        
        // Get messages
        std::string sql = R"(
            SELECT m.id, m.transaction_id, m.partition_id, m.payload, m.trace_id, m.created_at, m.is_encrypted,
                   q.name as queue_name, p.name as partition_name, q.priority as queue_priority
            FROM queen.messages m
            JOIN queen.partitions p ON p.id = m.partition_id
            JOIN queen.queues q ON q.id = p.queue_id
            JOIN queen.partition_consumers pc ON pc.partition_id = p.id
            )" + where_clause + R"(
            ORDER BY m.created_at ASC, m.id ASC
            LIMIT )" + limit_param + R"(
            FOR UPDATE OF m SKIP LOCKED
        )";
        
        spdlog::debug("Pop query params: queue={}, partition={}, consumer_group={}, lease_id={}, batch={}", 
                     queue_name, partition_name, consumer_group, lease_id, options.batch);
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto query_result = getTuplesResult(conn.get());
        
        int num_messages = PQntuples(query_result.get());
        
        spdlog::debug("Pop query returned {} rows (requested: {})", num_messages, options.batch);
        
        if (num_messages < options.batch) {
            spdlog::debug("Returned fewer messages than requested - likely fewer messages available in partition");
        }
        
        if (num_messages > 0) {
            // Update batch_size
            std::string update_batch_size = R"(
                UPDATE queen.partition_consumers
                SET batch_size = $1,
                    acked_count = 0
                WHERE partition_id = (
                    SELECT p.id FROM queen.partitions p
                    JOIN queen.queues q ON p.queue_id = q.id
                    WHERE q.name = $2 AND p.name = $3
                )
                AND consumer_group = $4
                AND worker_id = $5
            )";
            
            sendQueryParamsAsync(conn.get(), update_batch_size, {
                std::to_string(num_messages),
                queue_name,
                partition_name,
                consumer_group,
                lease_id
            });
            getCommandResult(conn.get());
        }
        
        if (num_messages == 0) {
            // No messages found - release lease
            spdlog::debug("No messages found - releasing lease for partition: {}", partition_name);
            release_partition_lease(conn.get(), queue_name, partition_name, consumer_group);
            return result;
        }
        
        // Get encryption service if needed
        EncryptionService* enc_service = get_encryption_service();
        
        // Parse messages
        for (int i = 0; i < num_messages; ++i) {
            Message msg;
            msg.id = PQgetvalue(query_result.get(), i, 0);
            msg.transaction_id = PQgetvalue(query_result.get(), i, 1);
            msg.partition_id = PQgetvalue(query_result.get(), i, 2);
            
            std::string payload_str = PQgetvalue(query_result.get(), i, 3);
            const char* trace_val = PQgetvalue(query_result.get(), i, 4);
            msg.trace_id = (trace_val && strlen(trace_val) > 0) ? trace_val : "";
            
            // Parse created_at
            const char* created_str = PQgetvalue(query_result.get(), i, 5);
            if (created_str && strlen(created_str) > 0) {
                std::tm tm = {};
                std::istringstream ss(created_str);
                ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                msg.created_at = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
            
            const char* is_encrypted_val = PQgetvalue(query_result.get(), i, 6);
            bool is_encrypted = (is_encrypted_val && (std::string(is_encrypted_val) == "t" || std::string(is_encrypted_val) == "true"));
            
            msg.queue_name = PQgetvalue(query_result.get(), i, 7);
            msg.partition_name = PQgetvalue(query_result.get(), i, 8);
            
            // Decrypt if needed
            if (is_encrypted && enc_service && enc_service->is_enabled()) {
                try {
                    auto encrypted_json = nlohmann::json::parse(payload_str);
                    EncryptionService::EncryptedData encrypted_data{
                        encrypted_json["encrypted"],
                        encrypted_json["iv"],
                        encrypted_json["authTag"]
                    };
                    auto decrypted = enc_service->decrypt_payload(encrypted_data);
                    
                    if (decrypted.has_value()) {
                        msg.payload = nlohmann::json::parse(decrypted.value());
                    } else {
                        spdlog::error("Decryption failed for message {}", msg.id);
                        msg.payload = nlohmann::json::parse(payload_str);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Error decrypting message {}: {}", msg.id, e.what());
                    msg.payload = nlohmann::json::parse(payload_str);
                }
            } else {
                msg.payload = nlohmann::json::parse(payload_str);
            }
            
            result.messages.push_back(msg);
        }
        
        // Auto-ack if enabled
        if (options.auto_ack && !result.messages.empty()) {
            spdlog::debug("Auto-ack enabled - acknowledging {} messages", result.messages.size());
            for (const auto& msg : result.messages) {
                acknowledge_message(msg.transaction_id, "completed", std::nullopt, 
                                  consumer_group, lease_id, msg.partition_id);
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pop from partition: {}", e.what());
    }
    
    return result;
}

// --- Pop from Any Partition in Queue ---

PopResult AsyncQueueManager::pop_messages_from_queue(
    const std::string& queue_name,
    const std::string& consumer_group,
    const PopOptions& options
) {
    PopResult result;
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Get queue configuration for window_buffer
        std::string config_sql = "SELECT window_buffer FROM queen.queues WHERE name = $1";
        sendQueryParamsAsync(conn.get(), config_sql, {queue_name});
        auto config_result = getTuplesResult(conn.get());
        
        int window_buffer = 0;
        if (PQntuples(config_result.get()) > 0) {
            const char* buffer_str = PQgetvalue(config_result.get(), 0, 0);
            window_buffer = (buffer_str && strlen(buffer_str) > 0) ? std::stoi(buffer_str) : 0;
        }
        
        // Build query to find available partitions
        std::string sql;
        std::vector<std::string> params = {queue_name, consumer_group};
        
        if (window_buffer > 0) {
            sql = R"(
                SELECT p.id, p.name, COUNT(m.id) as message_count
                FROM queen.partitions p
                JOIN queen.queues q ON p.queue_id = q.id
                LEFT JOIN queen.messages m ON m.partition_id = p.id
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
                    AND pc.consumer_group = $2
                WHERE q.name = $1
                  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
                  AND m.id IS NOT NULL
                  AND NOT EXISTS (
                      SELECT 1 FROM queen.messages m2
                      WHERE m2.partition_id = p.id
                        AND m2.created_at > NOW() - INTERVAL '1 second' * $3
                  )
                  AND (pc.last_consumed_created_at IS NULL
                       OR m.created_at > pc.last_consumed_created_at
                       OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
                GROUP BY p.id, p.name
                HAVING COUNT(m.id) > 0
                ORDER BY COUNT(m.id) DESC
                LIMIT 10
            )";
            params.push_back(std::to_string(window_buffer));
        } else {
            sql = R"(
                SELECT p.id, p.name, COUNT(m.id) as message_count
                FROM queen.partitions p
                JOIN queen.queues q ON p.queue_id = q.id
                LEFT JOIN queen.messages m ON m.partition_id = p.id
                LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id 
                    AND pc.consumer_group = $2
                WHERE q.name = $1
                  AND (pc.lease_expires_at IS NULL OR pc.lease_expires_at <= NOW())
                  AND m.id IS NOT NULL
                  AND (pc.last_consumed_created_at IS NULL
                       OR m.created_at > pc.last_consumed_created_at
                       OR (m.created_at = pc.last_consumed_created_at AND m.id > pc.last_consumed_id))
                GROUP BY p.id, p.name
                HAVING COUNT(m.id) > 0
                ORDER BY COUNT(m.id) DESC
                LIMIT 10
            )";
        }
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto partitions_result = getTuplesResult(conn.get());
        
        if (PQntuples(partitions_result.get()) == 0) {
            spdlog::debug("No available partitions found for queue: {}", queue_name);
            return result;
        }
        
        // Try each partition until we get messages
        for (int i = 0; i < PQntuples(partitions_result.get()); ++i) {
            std::string partition_name = PQgetvalue(partitions_result.get(), i, 1);
            const char* count_str = PQgetvalue(partitions_result.get(), i, 2);
            int message_count = (count_str && strlen(count_str) > 0) ? std::stoi(count_str) : 0;
            
            spdlog::debug("Trying partition '{}' with {} messages", partition_name, message_count);
            
            result = pop_messages_from_partition(queue_name, partition_name, consumer_group, options);
            
            if (!result.messages.empty()) {
                return result;
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pop from any partition: {}", e.what());
    }
    
    return result;
}

// --- Pop with Namespace/Task Filters ---

PopResult AsyncQueueManager::pop_messages_filtered(
    const std::optional<std::string>& namespace_name,
    const std::optional<std::string>& task_name,
    const std::string& consumer_group,
    const PopOptions& options
) {
    PopResult result;
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Find queues matching filters with window buffer check
        std::string sql = R"(
            SELECT 
                q.name as queue_name,
                p.name as partition_name,
                q.window_buffer
            FROM queen.queues q
            JOIN queen.partitions p ON p.queue_id = q.id
            WHERE 1=1
        )";
        
        std::vector<std::string> params;
        
        if (namespace_name.has_value()) {
            sql += " AND q.namespace = $" + std::to_string(params.size() + 1);
            params.push_back(*namespace_name);
        }
        
        if (task_name.has_value()) {
            sql += " AND q.task = $" + std::to_string(params.size() + 1);
            params.push_back(*task_name);
        }
        
        sql += R"(
              AND (q.window_buffer = 0 OR NOT EXISTS (
                SELECT 1 FROM queen.messages m
                WHERE m.partition_id = p.id
                  AND m.created_at > NOW() - INTERVAL '1 second' * q.window_buffer
              ))
            LIMIT 10
        )";
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto queues_result = getTuplesResult(conn.get());
        
        if (PQntuples(queues_result.get()) == 0) {
            spdlog::debug("No queues found matching namespace/task filters");
            return result;
        }
        
        // Try each matching queue/partition
        for (int i = 0; i < PQntuples(queues_result.get()); ++i) {
            std::string queue_name = PQgetvalue(queues_result.get(), i, 0);
            std::string partition_name = PQgetvalue(queues_result.get(), i, 1);
            
            result = pop_messages_from_partition(queue_name, partition_name, consumer_group, options);
            
            if (!result.messages.empty()) {
                return result;
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to pop with namespace/task: {}", e.what());
    }
    
    return result;
}

// ===================================
// ACK OPERATIONS - ASYNC VERSIONS
// ===================================

AsyncQueueManager::AckResult AsyncQueueManager::acknowledge_message(
    const std::string& transaction_id,
    const std::string& status,
    const std::optional<std::string>& error,
    const std::string& consumer_group,
    const std::optional<std::string>& lease_id,
    const std::optional<std::string>& partition_id_param
) {
    AckResult result;
    result.success = false;
    result.message = "not_found";
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // CRITICAL: partition_id is MANDATORY
        if (!partition_id_param.has_value() || partition_id_param->empty()) {
            spdlog::error("partition_id is required for acknowledgment of transaction {}", transaction_id);
            result.message = "invalid_request";
            result.error = "partition_id is required";
            return result;
        }
        
        std::string partition_id = *partition_id_param;
        spdlog::debug("Using provided partition_id {} for transaction {}", partition_id, transaction_id);
        
        // Validate lease if provided
        if (lease_id.has_value()) {
            spdlog::debug("Validating lease {} for transaction {}", *lease_id, transaction_id);
            std::string validate_sql = R"(
                SELECT 1 FROM queen.partition_consumers pc
                WHERE pc.partition_id = $1::uuid
                  AND pc.worker_id = $2
                  AND pc.lease_expires_at > NOW()
            )";
            
            sendQueryParamsAsync(conn.get(), validate_sql, {partition_id, *lease_id});
            auto valid = getTuplesResult(conn.get());
            
            if (PQntuples(valid.get()) == 0) {
                spdlog::warn("Invalid lease {} for partition {}", *lease_id, partition_id);
                result.message = "invalid_lease";
                result.error = "Lease validation failed";
                return result;
            }
            spdlog::debug("Lease {} validated successfully", *lease_id);
        }
        
        if (status == "completed") {
            // Update cursor and release lease if all messages ACKed
            std::string sql = R"(
                UPDATE queen.partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    acked_count = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + 1
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                FROM queen.messages m
                WHERE partition_consumers.partition_id = $1::uuid
                  AND partition_consumers.consumer_group = $2
                  AND m.partition_id = $1::uuid
                  AND m.transaction_id = $3
                RETURNING 
                    (partition_consumers.lease_expires_at IS NULL) as lease_released,
                    partition_consumers.partition_id
            )";
            
            sendQueryParamsAsync(conn.get(), sql, {partition_id, consumer_group, transaction_id});
            auto query_result = getTuplesResult(conn.get());
            
            if (PQntuples(query_result.get()) > 0) {
                const char* lease_released_val = PQgetvalue(query_result.get(), 0, 0);
                bool lease_released = (lease_released_val && (std::string(lease_released_val) == "t"));
                
                if (lease_released) {
                    spdlog::debug("Lease released after ACK for transaction: {}", transaction_id);
                    
                    // Insert into messages_consumed for analytics
                    std::string partition_id_ret = PQgetvalue(query_result.get(), 0, 1);
                    std::string insert_consumed_sql = R"(
                        INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                        VALUES ($1, $2, 1, 0, NOW())
                    )";
                    
                    sendQueryParamsAsync(conn.get(), insert_consumed_sql, {partition_id_ret, consumer_group});
                    getCommandResult(conn.get());
                }
                
                result.success = true;
                result.message = "completed";
            }
            
            return result;
            
        } else if (status == "failed") {
            // Check if this failure completes the batch and if we should retry
            std::string batch_info_sql = R"(
                SELECT 
                    pc.batch_size,
                    pc.acked_count,
                    pc.batch_retry_count,
                    q.retry_limit
                FROM queen.partition_consumers pc
                JOIN queen.partitions p ON p.id = pc.partition_id
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE pc.partition_id = $1::uuid
                  AND pc.consumer_group = $2
                LIMIT 1
            )";
            
            sendQueryParamsAsync(conn.get(), batch_info_sql, {partition_id, consumer_group});
            auto batch_info = getTuplesResult(conn.get());
            
            int batch_size = 0;
            int acked_count = 0;
            int batch_retry_count = 0;
            int retry_limit = 3;
            
            if (PQntuples(batch_info.get()) > 0) {
                const char* bs = PQgetvalue(batch_info.get(), 0, 0);
                const char* ac = PQgetvalue(batch_info.get(), 0, 1);
                const char* brc = PQgetvalue(batch_info.get(), 0, 2);
                const char* rl = PQgetvalue(batch_info.get(), 0, 3);
                
                batch_size = (bs && strlen(bs) > 0) ? std::stoi(bs) : 0;
                acked_count = (ac && strlen(ac) > 0) ? std::stoi(ac) : 0;
                batch_retry_count = (brc && strlen(brc) > 0) ? std::stoi(brc) : 0;
                retry_limit = (rl && strlen(rl) > 0) ? std::stoi(rl) : 3;
            }
            
            // Check if this is the last message in batch being ACKed
            bool is_last_in_batch = (batch_size > 0) && (acked_count + 1 >= batch_size);
            
            if (is_last_in_batch && batch_retry_count < retry_limit) {
                // This is a batch failure and we haven't exceeded retry limit
                // Increment retry counter, release lease, DON'T advance cursor (retry!)
                spdlog::info("Single message failure completing batch - retry {}/{}", batch_retry_count + 1, retry_limit);
                
                std::string retry_sql = R"(
                    UPDATE queen.partition_consumers
                    SET lease_expires_at = NULL,
                        lease_acquired_at = NULL,
                        message_batch = NULL,
                        batch_size = 0,
                        acked_count = 0,
                        worker_id = NULL,
                        batch_retry_count = COALESCE(batch_retry_count, 0) + 1
                    WHERE partition_id = $1::uuid
                      AND consumer_group = $2
                )";
                
                sendQueryParamsAsync(conn.get(), retry_sql, {partition_id, consumer_group});
                getCommandResult(conn.get());
                
                result.success = true;
                result.message = "failed_retry";
                return result;
            }
            
            // Either not last in batch, or retry limit exceeded - move to DLQ
            std::string dlq_sql = R"(
                INSERT INTO queen.dead_letter_queue (
                    message_id, partition_id, consumer_group, error_message, 
                    retry_count, original_created_at
                )
                SELECT m.id, m.partition_id, $1::varchar, $2::text, 0, m.created_at
                FROM queen.messages m
                WHERE m.partition_id = $3::uuid
                  AND m.transaction_id = $4
                  AND NOT EXISTS (
                      SELECT 1 FROM queen.dead_letter_queue dlq
                      WHERE dlq.message_id = m.id AND dlq.consumer_group = $1::varchar
                  )
            )";
            
            sendQueryParamsAsync(conn.get(), dlq_sql, {
                consumer_group,
                error.value_or("Message processing failed"),
                partition_id,
                transaction_id
            });
            getCommandResult(conn.get());
            
            // Advance cursor for failed messages
            std::string cursor_sql = R"(
                UPDATE queen.partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    batch_retry_count = 0,
                    acked_count = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + 1
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                FROM queen.messages m
                WHERE partition_consumers.partition_id = $1::uuid
                  AND partition_consumers.consumer_group = $2
                  AND m.partition_id = $1::uuid
                  AND m.transaction_id = $3
                RETURNING (partition_consumers.lease_expires_at IS NULL) as lease_released
            )";
            
            sendQueryParamsAsync(conn.get(), cursor_sql, {partition_id, consumer_group, transaction_id});
            auto cursor_result = getTuplesResult(conn.get());
            
            bool lease_released = PQntuples(cursor_result.get()) > 0 &&
                                 std::string(PQgetvalue(cursor_result.get(), 0, 0)) == "t";
            
            if (lease_released) {
                // Insert analytics for failed message
                std::string insert_consumed_sql = R"(
                    INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                    VALUES ($1, $2, 0, 1, NOW())
                )";
                sendQueryParamsAsync(conn.get(), insert_consumed_sql, {partition_id, consumer_group});
                getCommandResult(conn.get());
            }
            
            spdlog::warn("Message failed and moved to DLQ: {} - {}", transaction_id, error.value_or("No error message"));
            result.success = true;
            result.message = "failed_dlq";
            return result;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to acknowledge message: {}", e.what());
        result.error = e.what();
    }
    
    return result;
}

AsyncQueueManager::BatchAckResult AsyncQueueManager::acknowledge_messages_batch(
    const std::vector<nlohmann::json>& acknowledgments
) {
    BatchAckResult batch_result;
    batch_result.successful_acks = 0;
    batch_result.failed_acks = 0;
    
    if (acknowledgments.empty()) {
        return batch_result;
    }
    
    try {
        // CRITICAL: Use ONE connection for entire batch
        auto conn = async_db_pool_->acquire();
        
        spdlog::debug("Batch ACK: Processing {} acknowledgments with SINGLE connection", acknowledgments.size());
        
        // Begin transaction for atomic batch ACK
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            // Extract consumer_group from first ack (should be same for all)
            std::string consumer_group = acknowledgments[0].value("consumerGroup", "__QUEUE_MODE__");
            
            // Group by status
            std::vector<nlohmann::json> completed;
            std::vector<nlohmann::json> failed;
            
            for (const auto& ack : acknowledgments) {
                std::string status = ack.value("status", "completed");
                if (status == "completed") {
                    completed.push_back(ack);
                } else if (status == "failed") {
                    failed.push_back(ack);
                }
            }
            
            int total_messages = acknowledgments.size();
            int failed_count = failed.size();
            int success_count = completed.size();
            
            // Build array of all transaction IDs
            std::vector<std::string> all_txn_ids;
            for (const auto& ack : acknowledgments) {
                all_txn_ids.push_back(ack.value("transactionId", ""));
            }
            
            // Build PostgreSQL array
            auto build_pg_array = [](const std::vector<std::string>& vec) -> std::string {
                std::string result = "{";
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (i > 0) result += ",";
                    result += "\"";
                    for (char c : vec[i]) {
                        if (c == '\\' || c == '"') result += '\\';
                        result += c;
                    }
                    result += "\"";
                }
                result += "}";
                return result;
            };
            
            std::string txn_array = build_pg_array(all_txn_ids);
            
            // Get partition_id from first ack
            if (!acknowledgments[0].contains("partitionId") || acknowledgments[0]["partitionId"].is_null()) {
                throw std::runtime_error("partition_id is required for batch acknowledgment");
            }
            
            std::string partition_id = acknowledgments[0]["partitionId"].get<std::string>();
            
            // Get batch size for the partition
            std::string batch_size_sql = R"(
                SELECT pc.batch_size
                FROM queen.partition_consumers pc
                WHERE pc.partition_id = $1::uuid
                  AND pc.consumer_group = $2
                LIMIT 1
            )";
            
            sendQueryParamsAsync(conn.get(), batch_size_sql, {partition_id, consumer_group});
            auto batch_size_result = getTuplesResult(conn.get());
            
            if (PQntuples(batch_size_result.get()) == 0) {
                throw std::runtime_error("Cannot find batch size for partition");
            }
            
            const char* batch_size_str = PQgetvalue(batch_size_result.get(), 0, 0);
            int leased_batch_size = (batch_size_str && strlen(batch_size_str) > 0) ? std::stoi(batch_size_str) : 0;
            
            // Check if this is a total batch failure
            bool is_total_batch_failure = (failed_count == total_messages) && (total_messages == leased_batch_size);
            
            if (is_total_batch_failure) {
                // Get retry info
                std::string retry_info_sql = R"(
                    SELECT 
                        q.retry_limit,
                        pc.batch_retry_count
                    FROM queen.messages m
                    JOIN queen.partitions p ON p.id = m.partition_id
                    JOIN queen.queues q ON q.id = p.queue_id
                    JOIN queen.partition_consumers pc ON pc.partition_id = m.partition_id 
                      AND pc.consumer_group = $2
                    WHERE m.partition_id = $3::uuid
                      AND m.transaction_id = ANY($1::varchar[])
                    LIMIT 1
                )";
                
                sendQueryParamsAsync(conn.get(), retry_info_sql, {txn_array, consumer_group, partition_id});
                auto retry_info = getTuplesResult(conn.get());
                
                if (PQntuples(retry_info.get()) == 0) {
                    throw std::runtime_error("Cannot find queue retry limit");
                }
                
                const char* retry_limit_str = PQgetvalue(retry_info.get(), 0, 0);
                const char* batch_retry_str = PQgetvalue(retry_info.get(), 0, 1);
                
                int retry_limit = (retry_limit_str && strlen(retry_limit_str) > 0) ? std::stoi(retry_limit_str) : 3;
                int current_retry_count = (batch_retry_str && strlen(batch_retry_str) > 0) ? std::stoi(batch_retry_str) : 0;
                
                if (current_retry_count >= retry_limit) {
                    // Retry limit exceeded - move to DLQ and advance cursor
                    spdlog::warn("Batch retry limit exceeded ({}/{}), moving to DLQ", current_retry_count, retry_limit);
                    
                    std::vector<std::string> failed_errors;
                    for (const auto& ack : failed) {
                        std::string error_msg = "Batch retry limit exceeded";
                        if (ack.contains("error") && !ack["error"].is_null()) {
                            error_msg = ack["error"].get<std::string>();
                        }
                        failed_errors.push_back(error_msg);
                    }
                    
                    std::string error_array = build_pg_array(failed_errors);
                    
                    std::string dlq_sql = R"(
                        INSERT INTO queen.dead_letter_queue (message_id, partition_id, consumer_group, error_message, original_created_at)
                        SELECT 
                            m.id,
                            m.partition_id,
                            $2,
                            e.error_message,
                            m.created_at
                        FROM queen.messages m
                        CROSS JOIN LATERAL UNNEST($1::varchar[], $4::text[]) AS e(txn_id, error_message)
                        WHERE m.partition_id = $3::uuid
                          AND m.transaction_id = e.txn_id
                        ON CONFLICT DO NOTHING
                    )";
                    
                    sendQueryParamsAsync(conn.get(), dlq_sql, {txn_array, consumer_group, partition_id, error_array});
                    getCommandResult(conn.get());
                    
                    // Get last message for cursor
                    std::string last_msg_sql = R"(
                        SELECT id, created_at
                        FROM queen.messages m
                        WHERE m.partition_id = $1::uuid
                          AND m.transaction_id = ANY($2::varchar[])
                        ORDER BY m.created_at DESC, m.id DESC
                        LIMIT 1
                    )";
                    
                    sendQueryParamsAsync(conn.get(), last_msg_sql, {partition_id, txn_array});
                    auto last_msg = getTuplesResult(conn.get());
                    
                    std::string last_id = PQgetvalue(last_msg.get(), 0, 0);
                    std::string last_created = PQgetvalue(last_msg.get(), 0, 1);
                    
                    // Advance cursor and reset retry count
                    std::string cursor_sql = R"(
                        UPDATE queen.partition_consumers
                        SET 
                            last_consumed_created_at = $1,
                            last_consumed_id = $2,
                            total_messages_consumed = total_messages_consumed + $3,
                            total_batches_consumed = total_batches_consumed + 1,
                            last_consumed_at = NOW(),
                            batch_retry_count = 0,
                            pending_estimate = GREATEST(0, pending_estimate - $3),
                            last_stats_update = NOW(),
                            lease_expires_at = NULL,
                            lease_acquired_at = NULL,
                            message_batch = NULL,
                            batch_size = 0,
                            acked_count = 0,
                            worker_id = NULL
                        WHERE partition_id = $4 AND consumer_group = $5
                    )";
                    
                    sendQueryParamsAsync(conn.get(), cursor_sql, {last_created, last_id, std::to_string(total_messages), partition_id, consumer_group});
                    getCommandResult(conn.get());
                    
                    // Insert analytics
                    std::string insert_consumed_sql = R"(
                        INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                        VALUES ($1, $2, 0, $3, NOW())
                    )";
                    sendQueryParamsAsync(conn.get(), insert_consumed_sql, {partition_id, consumer_group, std::to_string(failed_count)});
                    getCommandResult(conn.get());
                    
                    for (const auto& ack : failed) {
                        AckResult result;
                        result.success = true;
                        result.message = "failed_dlq";
                        batch_result.results.push_back(result);
                        batch_result.successful_acks++;
                    }
                    
                } else {
                    // Batch retry - increment counter, release lease, DON'T advance cursor
                    spdlog::info("Total batch failure - retry {}/{}", current_retry_count + 1, retry_limit);
                    
                    std::string retry_sql = R"(
                        UPDATE queen.partition_consumers pc
                        SET lease_expires_at = NULL,
                            lease_acquired_at = NULL,
                            message_batch = NULL,
                            batch_size = 0,
                            acked_count = 0,
                            worker_id = NULL,
                            batch_retry_count = COALESCE(batch_retry_count, 0) + 1
                        WHERE pc.partition_id = $1 AND pc.consumer_group = $2
                    )";
                    
                    sendQueryParamsAsync(conn.get(), retry_sql, {partition_id, consumer_group});
                    getCommandResult(conn.get());
                    
                    for (const auto& ack : failed) {
                        AckResult result;
                        result.success = true;
                        result.message = "failed_retry";
                        batch_result.results.push_back(result);
                        batch_result.successful_acks++;
                    }
                }
                
                // Commit and exit early for total batch failure
                sendAndWait(conn.get(), "COMMIT");
                getCommandResult(conn.get());
                
                return batch_result;
            }
            
            // Partial success - advance cursor, DLQ individual failures
            
            // Get last message info for cursor update
            std::string batch_info_sql = R"(
                SELECT 
                    m.id,
                    m.created_at
                FROM queen.messages m
                WHERE m.partition_id = $1::uuid
                  AND m.transaction_id = ANY($2::varchar[])
                ORDER BY m.created_at DESC, m.id DESC
                LIMIT 1
            )";
            
            sendQueryParamsAsync(conn.get(), batch_info_sql, {partition_id, txn_array});
            auto batch_info = getTuplesResult(conn.get());
            
            if (PQntuples(batch_info.get()) == 0) {
                throw std::runtime_error("No messages found for acknowledgment");
            }
            
            std::string last_id = PQgetvalue(batch_info.get(), 0, 0);
            std::string last_created = PQgetvalue(batch_info.get(), 0, 1);
            
            // Move failed messages to DLQ
            if (!failed.empty()) {
                std::vector<std::string> failed_txn_ids;
                std::vector<std::string> failed_errors;
                
                for (const auto& ack : failed) {
                    failed_txn_ids.push_back(ack.value("transactionId", ""));
                    std::string error_msg = "Unknown error";
                    if (ack.contains("error") && !ack["error"].is_null()) {
                        error_msg = ack["error"].get<std::string>();
                    }
                    failed_errors.push_back(error_msg);
                }
                
                std::string failed_array = build_pg_array(failed_txn_ids);
                std::string error_array = build_pg_array(failed_errors);
                
                std::string dlq_sql = R"(
                    INSERT INTO queen.dead_letter_queue (message_id, partition_id, consumer_group, error_message, original_created_at)
                    SELECT 
                        m.id,
                        m.partition_id,
                        $2,
                        e.error_message,
                        m.created_at
                    FROM queen.messages m
                    CROSS JOIN LATERAL UNNEST($1::varchar[], $4::text[]) AS e(txn_id, error_message)
                    WHERE m.partition_id = $3::uuid
                      AND m.transaction_id = e.txn_id
                    ON CONFLICT DO NOTHING
                )";
                
                sendQueryParamsAsync(conn.get(), dlq_sql, {failed_array, consumer_group, partition_id, error_array});
                getCommandResult(conn.get());
                
                spdlog::info("Moved {} failed messages to DLQ", failed_count);
            }
            
            // Advance cursor atomically
            std::string cursor_sql = R"(
                UPDATE queen.partition_consumers
                SET 
                    last_consumed_created_at = $1,
                    last_consumed_id = $2,
                    total_messages_consumed = total_messages_consumed + $3,
                    total_batches_consumed = total_batches_consumed + 1,
                    last_consumed_at = NOW(),
                    batch_retry_count = 0,
                    pending_estimate = GREATEST(0, pending_estimate - $3),
                    last_stats_update = NOW(),
                    acked_count = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + $3
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    message_batch = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE message_batch 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + $3 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                WHERE partition_id = $4 AND consumer_group = $5
                RETURNING (lease_expires_at IS NULL) as lease_released
            )";
            
            sendQueryParamsAsync(conn.get(), cursor_sql, {
                last_created, last_id, std::to_string(total_messages), partition_id, consumer_group
            });
            auto cursor_result = getTuplesResult(conn.get());
            
            bool lease_released = PQntuples(cursor_result.get()) > 0 && 
                                 std::string(PQgetvalue(cursor_result.get(), 0, 0)) == "t";
            
            spdlog::debug("Batch ACK: Success={} Failed={} Lease={}", 
                        success_count, failed_count, lease_released ? "Released" : "Active");
            
            // Insert analytics record if lease was released (batch complete)
            if (lease_released) {
                std::string insert_consumed_sql = R"(
                    INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                    VALUES ($1, $2, $3, $4, NOW())
                )";
                
                sendQueryParamsAsync(conn.get(), insert_consumed_sql, {
                    partition_id, 
                    consumer_group, 
                    std::to_string(success_count), 
                    std::to_string(failed_count)
                });
                getCommandResult(conn.get());
            }
            
            // Commit transaction
            sendAndWait(conn.get(), "COMMIT");
            getCommandResult(conn.get());
            
            // Build results
            for (const auto& ack : acknowledgments) {
                AckResult result;
                std::string status = ack.value("status", "completed");
                result.success = true;
                result.message = (status == "completed") ? "completed" : "failed_dlq";
                batch_result.results.push_back(result);
                batch_result.successful_acks++;
            }
            
        } catch (const std::exception& e) {
            // Rollback on error
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Batch acknowledgment failed: {}", e.what());
        
        // Return all as failed
        for (size_t i = 0; i < acknowledgments.size(); ++i) {
            AckResult result;
            result.success = false;
            result.message = "batch_failed";
            result.error = e.what();
            batch_result.results.push_back(result);
            batch_result.failed_acks++;
        }
    }
    
    return batch_result;
}

// ===================================
// TRANSACTION HELPERS - USE PROVIDED CONNECTION
// ===================================

PushResult AsyncQueueManager::push_single_message_transactional(
    PGconn* conn,
    const PushItem& item
) {
    PushResult result;
    result.transaction_id = item.transaction_id.value_or(generate_transaction_id());
    
    try {
        // Ensure queue exists
        if (!ensure_queue_exists(conn, item.queue, "", "")) {
            result.status = "failed";
            result.error = "Failed to ensure queue exists";
            return result;
        }
        
        // Ensure partition exists
        if (!ensure_partition_exists(conn, item.queue, item.partition)) {
            result.status = "failed";
            result.error = "Failed to create partition";
            return result;
        }
        
        // Generate message ID
        std::string message_id = generate_uuid();
        
        // Check encryption
        sendQueryParamsAsync(conn, "SELECT encryption_enabled FROM queen.queues WHERE name = $1", {item.queue});
        auto encryption_result = getTuplesResult(conn);
        
        bool encryption_enabled = false;
        if (PQntuples(encryption_result.get()) > 0) {
            const char* enc_str = PQgetvalue(encryption_result.get(), 0, 0);
            encryption_enabled = (enc_str && (std::string(enc_str) == "t" || std::string(enc_str) == "true"));
        }
        
        // Prepare payload
        std::string payload_str = item.payload.dump();
        bool is_encrypted = false;
        
        if (encryption_enabled) {
            EncryptionService* enc_service = get_encryption_service();
            if (enc_service && enc_service->is_enabled()) {
                auto encrypted = enc_service->encrypt_payload(payload_str);
                if (encrypted.has_value()) {
                    nlohmann::json encrypted_json = {
                        {"encrypted", encrypted->encrypted},
                        {"iv", encrypted->iv},
                        {"authTag", encrypted->auth_tag}
                    };
                    payload_str = encrypted_json.dump();
                    is_encrypted = true;
                }
            }
        }
        
        // Insert message
        std::string sql;
        std::vector<std::string> params;
        
        if (item.trace_id.has_value() && !item.trace_id->empty()) {
            sql = R"(
                INSERT INTO queen.messages (
                    id, transaction_id, partition_id, payload, trace_id, is_encrypted, created_at
                )
                SELECT $1, $2, p.id, $3, $4, $5, NOW()
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $6 AND p.name = $7
                RETURNING id, trace_id
            )";
            
            params = {
                message_id,
                result.transaction_id,
                payload_str,
                item.trace_id.value(),
                is_encrypted ? "true" : "false",
                item.queue,
                item.partition
            };
        } else {
            sql = R"(
                INSERT INTO queen.messages (
                    id, transaction_id, partition_id, payload, is_encrypted, created_at
                )
                SELECT $1, $2, p.id, $3, $4, NOW()
                FROM queen.partitions p
                JOIN queen.queues q ON q.id = p.queue_id
                WHERE q.name = $5 AND p.name = $6
                RETURNING id, trace_id
            )";
            
            params = {
                message_id,
                result.transaction_id,
                payload_str,
                is_encrypted ? "true" : "false",
                item.queue,
                item.partition
            };
        }
        
        sendQueryParamsAsync(conn, sql, params);
        auto query_result = getTuplesResult(conn);
        
        if (PQntuples(query_result.get()) > 0) {
            result.status = "queued";
            result.message_id = PQgetvalue(query_result.get(), 0, 0);
            const char* trace_val = PQgetvalue(query_result.get(), 0, 1);
            if (trace_val && strlen(trace_val) > 0) {
                result.trace_id = trace_val;
            }
        } else {
            result.status = "failed";
            result.error = "Insert returned no result";
        }
        
    } catch (const std::exception& e) {
        result.status = "failed";
        result.error = e.what();
        spdlog::error("Failed to push message in transaction: {}", e.what());
    }
    
    return result;
}

AsyncQueueManager::AckResult AsyncQueueManager::acknowledge_message_transactional(
    PGconn* conn,
    const std::string& transaction_id,
    const std::string& status,
    const std::optional<std::string>& error,
    const std::string& consumer_group,
    const std::optional<std::string>& partition_id_param
) {
    AckResult result;
    result.success = false;
    result.message = "not_found";
    
    try {
        // CRITICAL: partition_id is MANDATORY
        if (!partition_id_param.has_value() || partition_id_param->empty()) {
            spdlog::error("partition_id is required for acknowledgment of transaction {}", transaction_id);
            result.message = "invalid_request";
            result.error = "partition_id is required";
            return result;
        }
        
        std::string partition_id = *partition_id_param;
        
        if (status == "completed") {
            // Update cursor - simplified for transactions (no retry logic)
            std::string sql = R"(
                UPDATE queen.partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    acked_count = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + 1
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                FROM queen.messages m
                WHERE partition_consumers.partition_id = $1::uuid
                  AND partition_consumers.consumer_group = $2
                  AND m.partition_id = $1::uuid
                  AND m.transaction_id = $3
                RETURNING (partition_consumers.lease_expires_at IS NULL) as lease_released
            )";
            
            sendQueryParamsAsync(conn, sql, {partition_id, consumer_group, transaction_id});
            auto query_result = getTuplesResult(conn);
            
            if (PQntuples(query_result.get()) > 0) {
                const char* lease_released_val = PQgetvalue(query_result.get(), 0, 0);
                bool lease_released = (lease_released_val && (std::string(lease_released_val) == "t"));
                
                if (lease_released) {
                    std::string insert_consumed_sql = R"(
                        INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                        VALUES ($1, $2, 1, 0, NOW())
                    )";
                    sendQueryParamsAsync(conn, insert_consumed_sql, {partition_id, consumer_group});
                    getCommandResult(conn);
                }
                
                result.success = true;
                result.message = "completed";
            }
            
        } else if (status == "failed") {
            // For transactions: Move to DLQ and advance cursor (no retry logic)
            std::string dlq_sql = R"(
                INSERT INTO queen.dead_letter_queue (
                    message_id, partition_id, consumer_group, error_message, 
                    retry_count, original_created_at
                )
                SELECT m.id, m.partition_id, $1::varchar, $2::text, 0, m.created_at
                FROM queen.messages m
                WHERE m.partition_id = $3::uuid
                  AND m.transaction_id = $4
                  AND NOT EXISTS (
                      SELECT 1 FROM queen.dead_letter_queue dlq
                      WHERE dlq.message_id = m.id AND dlq.consumer_group = $1::varchar
                  )
            )";
            
            sendQueryParamsAsync(conn, dlq_sql, {
                consumer_group,
                error.value_or("Message processing failed"),
                partition_id,
                transaction_id
            });
            getCommandResult(conn);
            
            // Advance cursor
            std::string cursor_sql = R"(
                UPDATE queen.partition_consumers 
                SET last_consumed_id = m.id,
                    last_consumed_created_at = m.created_at,
                    last_consumed_at = NOW(),
                    total_messages_consumed = total_messages_consumed + 1,
                    batch_retry_count = 0,
                    acked_count = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0
                        ELSE acked_count + 1
                    END,
                    lease_expires_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_expires_at 
                    END,
                    lease_acquired_at = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE lease_acquired_at 
                    END,
                    batch_size = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN 0 
                        ELSE batch_size 
                    END,
                    worker_id = CASE 
                        WHEN acked_count + 1 >= batch_size AND batch_size > 0 
                        THEN NULL 
                        ELSE worker_id 
                    END
                FROM queen.messages m
                WHERE partition_consumers.partition_id = $1::uuid
                  AND partition_consumers.consumer_group = $2
                  AND m.partition_id = $1::uuid
                  AND m.transaction_id = $3
                RETURNING (partition_consumers.lease_expires_at IS NULL) as lease_released
            )";
            
            sendQueryParamsAsync(conn, cursor_sql, {partition_id, consumer_group, transaction_id});
            auto cursor_result = getTuplesResult(conn);
            
            bool lease_released = PQntuples(cursor_result.get()) > 0 &&
                                 std::string(PQgetvalue(cursor_result.get(), 0, 0)) == "t";
            
            if (lease_released) {
                std::string insert_consumed_sql = R"(
                    INSERT INTO queen.messages_consumed (partition_id, consumer_group, messages_completed, messages_failed, acked_at)
                    VALUES ($1, $2, 0, 1, NOW())
                )";
                sendQueryParamsAsync(conn, insert_consumed_sql, {partition_id, consumer_group});
                getCommandResult(conn);
            }
            
            result.success = true;
            result.message = "failed_dlq";
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to acknowledge message in transaction: {}", e.what());
        result.error = e.what();
    }
    
    return result;
}

// ===================================
// TRANSACTION OPERATIONS - ASYNC VERSION
// ===================================

AsyncQueueManager::TransactionResult AsyncQueueManager::execute_transaction(
    const std::vector<nlohmann::json>& operations
) {
    TransactionResult txn_result;
    txn_result.transaction_id = generate_transaction_id();
    txn_result.success = true;
    txn_result.results.reserve(operations.size());
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Begin transaction
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            for (const auto& op : operations) {
                std::string op_type = op.value("type", "");
                nlohmann::json op_result;
                
                if (op_type == "push") {
                    // Push operation - use transactional helper with shared connection
                    if (!op.contains("items") || !op["items"].is_array()) {
                        op_result["type"] = "push";
                        op_result["error"] = "items array required for push operation";
                        txn_result.success = false;
                        txn_result.results.push_back(op_result);
                        continue;
                    }
                    
                    auto items_json = op["items"];
                    op_result["type"] = "push";
                    op_result["count"] = items_json.size();
                    op_result["results"] = nlohmann::json::array();
                    
                    for (const auto& item_json : items_json) {
                        PushItem item;
                        item.queue = item_json.value("queue", "");
                        item.partition = item_json.value("partition", "Default");
                        item.payload = item_json.contains("data") ? item_json["data"] : item_json.value("payload", nlohmann::json{});
                        
                        if (item_json.contains("transactionId") && !item_json["transactionId"].is_null()) {
                            item.transaction_id = item_json["transactionId"].get<std::string>();
                        }
                        if (item_json.contains("traceId") && !item_json["traceId"].is_null()) {
                            item.trace_id = item_json["traceId"].get<std::string>();
                        }
                        
                        // Use transactional helper with shared connection
                        auto pr = push_single_message_transactional(conn.get(), item);
                        
                        nlohmann::json push_res;
                        push_res["status"] = pr.status;
                        push_res["transactionId"] = pr.transaction_id;
                        if (pr.message_id.has_value()) {
                            push_res["messageId"] = *pr.message_id;
                        }
                        if (pr.error.has_value()) {
                            push_res["error"] = *pr.error;
                            txn_result.success = false;
                        }
                        op_result["results"].push_back(push_res);
                    }
                    
                } else if (op_type == "pop") {
                    // Pop operation - WARNING: This acquires its own connection (not truly atomic)
                    // TODO: Create pop_with_conn version for true atomicity
                    std::string queue = op.value("queue", "");
                    std::optional<std::string> partition = (op.contains("partition") && !op["partition"].is_null()) ?
                        std::optional<std::string>(op["partition"].get<std::string>()) : std::nullopt;
                    std::string consumer_group = op.value("consumerGroup", "__QUEUE_MODE__");
                    
                    PopOptions pop_opts;
                    pop_opts.batch = op.value("batch", 1);
                    pop_opts.auto_ack = op.value("autoAck", false);
                    
                    PopResult pop_result;
                    if (partition.has_value()) {
                        pop_result = pop_messages_from_partition(queue, *partition, consumer_group, pop_opts);
                    } else {
                        pop_result = pop_messages_from_queue(queue, consumer_group, pop_opts);
                    }
                    
                    op_result["type"] = "pop";
                    op_result["messages"] = nlohmann::json::array();
                    for (const auto& msg : pop_result.messages) {
                        nlohmann::json msg_json;
                        msg_json["id"] = msg.id;
                        msg_json["transactionId"] = msg.transaction_id;
                        msg_json["partitionId"] = msg.partition_id;
                        msg_json["payload"] = msg.payload;
                        msg_json["queueName"] = msg.queue_name;
                        msg_json["partitionName"] = msg.partition_name;
                        if (!msg.trace_id.empty()) {
                            msg_json["traceId"] = msg.trace_id;
                        }
                        op_result["messages"].push_back(msg_json);
                    }
                    if (pop_result.lease_id.has_value()) {
                        op_result["leaseId"] = *pop_result.lease_id;
                    }
                    
                } else if (op_type == "ack") {
                    // Ack operation - use transactional helper with shared connection
                    std::string transaction_id = op.value("transactionId", "");
                    std::string status = op.value("status", "completed");
                    std::optional<std::string> error = (op.contains("error") && !op["error"].is_null()) ?
                        std::optional<std::string>(op["error"].get<std::string>()) : std::nullopt;
                    std::string consumer_group = op.value("consumerGroup", "__QUEUE_MODE__");
                    std::optional<std::string> partition_id = (op.contains("partitionId") && !op["partitionId"].is_null()) ?
                        std::optional<std::string>(op["partitionId"].get<std::string>()) : std::nullopt;
                    
                    // Use transactional helper with shared connection
                    auto ack_result = acknowledge_message_transactional(conn.get(), transaction_id, status, error, consumer_group, partition_id);
                    
                    op_result["type"] = "ack";
                    op_result["success"] = ack_result.success;
                    op_result["message"] = ack_result.message;
                    if (ack_result.error.has_value()) {
                        op_result["error"] = *ack_result.error;
                    }
                    
                    if (!ack_result.success) {
                        txn_result.success = false;
                    }
                } else {
                    op_result["type"] = "unknown";
                    op_result["error"] = "Unknown operation type: " + op_type;
                    txn_result.success = false;
                }
                
                txn_result.results.push_back(op_result);
            }
            
            // Check if transaction should commit or rollback
            if (txn_result.success) {
                // All operations succeeded - commit
                sendAndWait(conn.get(), "COMMIT");
                getCommandResult(conn.get());
                spdlog::debug("Transaction committed successfully");
            } else {
                // At least one operation failed - rollback
                sendAndWait(conn.get(), "ROLLBACK");
                getCommandResult(conn.get());
                spdlog::warn("Transaction rolled back due to operation failure");
            }
            
        } catch (const std::exception& e) {
            // Rollback on exception
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Transaction failed: {}", e.what());
        txn_result.success = false;
        txn_result.error = e.what();
    }
    
    return txn_result;
}

// ===================================
// MAINTENANCE MODE
// ===================================

void AsyncQueueManager::set_maintenance_mode(bool enabled) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Persist to database for multi-instance support
        std::string sql = R"(
            INSERT INTO queen.system_state (key, value, updated_at)
            VALUES ('maintenance_mode', $1::jsonb, NOW())
            ON CONFLICT (key) DO UPDATE
            SET value = EXCLUDED.value,
                updated_at = NOW()
        )";
        
        nlohmann::json value = {{"enabled", enabled}};
        sendQueryParamsAsync(conn.get(), sql, {value.dump()});
        getCommandResult(conn.get());
        
        // Update cache immediately on this instance
        maintenance_mode_cached_.store(enabled);
        last_maintenance_check_ms_.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        spdlog::info("Maintenance mode {} (persisted to database for all instances)", 
                    enabled ? "ENABLED - routing PUSHes to file buffer" : "DISABLED - resuming normal operations");
        
        if (!enabled && file_buffer_manager_) {
            spdlog::info("File buffer will drain {} pending messages to database", 
                        file_buffer_manager_->get_pending_count());
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to set maintenance mode: {}", e.what());
        throw;
    }
}

bool AsyncQueueManager::get_maintenance_mode_fresh() {
    // Force cache invalidation by setting last_check to 0
    last_maintenance_check_ms_.store(0);
    return check_maintenance_mode_with_cache();
}

size_t AsyncQueueManager::get_buffer_pending_count() const {
    if (!file_buffer_manager_) {
        return 0;
    }
    return file_buffer_manager_->get_pending_count();
}

bool AsyncQueueManager::is_buffer_healthy() const {
    if (!file_buffer_manager_) {
        return true;  // No buffer = healthy by default
    }
    return file_buffer_manager_->is_db_healthy();
}

nlohmann::json AsyncQueueManager::get_buffer_stats() const {
    if (!file_buffer_manager_) {
        return {
            {"pendingCount", 0},
            {"failedCount", 0},
            {"dbHealthy", true},
            {"failedFiles", {
                {"count", 0},
                {"totalBytes", 0},
                {"totalMB", 0.0},
                {"failoverCount", 0},
                {"qos0Count", 0}
            }}
        };
    }
    
    auto failed_stats = file_buffer_manager_->get_failed_files_stats();
    double total_mb = failed_stats.total_bytes / (1024.0 * 1024.0);
    
    return {
        {"pendingCount", file_buffer_manager_->get_pending_count()},
        {"failedCount", file_buffer_manager_->get_failed_count()},
        {"dbHealthy", file_buffer_manager_->is_db_healthy()},
        {"failedFiles", {
            {"count", failed_stats.file_count},
            {"totalBytes", failed_stats.total_bytes},
            {"totalMB", total_mb},
            {"failoverCount", failed_stats.failover_count},
            {"qos0Count", failed_stats.qos0_count}
        }}
    };
}

// ===================================
// QUEUE CONFIGURATION
// ===================================

bool AsyncQueueManager::configure_queue(const std::string& queue_name, 
                                       const QueueOptions& options,
                                       const std::string& namespace_name,
                                       const std::string& task_name) {
    try {
        auto conn = async_db_pool_->acquire();
        
        std::string sql = R"(
            INSERT INTO queen.queues (
                name, namespace, task, priority, lease_time, retry_limit, retry_delay,
                max_queue_size, ttl, dead_letter_queue, dlq_after_max_retries, delayed_processing,
                window_buffer, retention_seconds, completed_retention_seconds, 
                retention_enabled, encryption_enabled, max_wait_time_seconds
            ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18)
            ON CONFLICT (name) DO UPDATE SET
                namespace = EXCLUDED.namespace,
                task = EXCLUDED.task,
                priority = EXCLUDED.priority,
                lease_time = EXCLUDED.lease_time,
                retry_limit = EXCLUDED.retry_limit,
                retry_delay = EXCLUDED.retry_delay,
                max_queue_size = EXCLUDED.max_queue_size,
                ttl = EXCLUDED.ttl,
                dead_letter_queue = EXCLUDED.dead_letter_queue,
                dlq_after_max_retries = EXCLUDED.dlq_after_max_retries,
                delayed_processing = EXCLUDED.delayed_processing,
                window_buffer = EXCLUDED.window_buffer,
                retention_seconds = EXCLUDED.retention_seconds,
                completed_retention_seconds = EXCLUDED.completed_retention_seconds,
                retention_enabled = EXCLUDED.retention_enabled,
                encryption_enabled = EXCLUDED.encryption_enabled,
                max_wait_time_seconds = EXCLUDED.max_wait_time_seconds
        )";
        
        std::vector<std::string> params = {
            queue_name,
            namespace_name.empty() ? "" : namespace_name,
            task_name.empty() ? "" : task_name,
            std::to_string(options.priority),
            std::to_string(options.lease_time),
            std::to_string(options.retry_limit),
            std::to_string(options.retry_delay),
            std::to_string(options.max_size),
            std::to_string(options.ttl),
            options.dead_letter_queue ? "true" : "false",
            options.dlq_after_max_retries ? "true" : "false",
            std::to_string(options.delayed_processing),
            std::to_string(options.window_buffer),
            std::to_string(options.retention_seconds),
            std::to_string(options.completed_retention_seconds),
            options.retention_enabled ? "true" : "false",
            options.encryption_enabled ? "true" : "false",
            std::to_string(options.max_wait_time_seconds)
        };
        
        sendQueryParamsAsync(conn.get(), sql, params);
        getCommandResult(conn.get());
        
        spdlog::debug("Queue '{}' configured successfully in database", queue_name);
        
        // Ensure default partition exists
        ensure_partition_exists(conn.get(), queue_name, "Default");
        
        spdlog::info("Queue '{}' fully configured with Default partition", queue_name);
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to configure queue '{}': {}", queue_name, e.what());
        return false;
    }
}

bool AsyncQueueManager::delete_queue(const std::string& queue_name) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Begin transaction and increase statement timeout for large queue deletions
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            // Set timeout to 10 minutes for this transaction
            sendAndWait(conn.get(), "SET LOCAL statement_timeout = '600000'");
            getCommandResult(conn.get());
            
            // First, delete consumer state for all partitions in this queue
            std::string delete_consumers_sql = R"(
                DELETE FROM queen.partition_consumers
                WHERE partition_id IN (
                    SELECT p.id FROM queen.partitions p
                    JOIN queen.queues q ON p.queue_id = q.id
                    WHERE q.name = $1
                )
            )";
            sendQueryParamsAsync(conn.get(), delete_consumers_sql, {queue_name});
            getCommandResult(conn.get());
            
            // Delete the queue (CASCADE will handle partitions and messages)
            std::string delete_queue_sql = "DELETE FROM queen.queues WHERE name = $1 RETURNING id";
            sendQueryParamsAsync(conn.get(), delete_queue_sql, {queue_name});
            auto result = getTuplesResult(conn.get());
            
            if (PQntuples(result.get()) == 0) {
                sendAndWait(conn.get(), "ROLLBACK");
                getCommandResult(conn.get());
                spdlog::warn("Queue not found: {}", queue_name);
                return false;
            }
            
            // Commit transaction
            sendAndWait(conn.get(), "COMMIT");
            getCommandResult(conn.get());
            
            spdlog::info("Queue deleted successfully: {}", queue_name);
            return true;
            
        } catch (const std::exception& e) {
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to delete queue: {}", e.what());
        return false;
    }
}

bool AsyncQueueManager::extend_message_lease(const std::string& lease_id, int seconds) {
    try {
        auto conn = async_db_pool_->acquire();
        
        std::string sql = R"(
            UPDATE queen.partition_consumers
            SET lease_expires_at = GREATEST(lease_expires_at, NOW() + INTERVAL '1 second' * $1)
            WHERE worker_id = $2 AND lease_expires_at > NOW()
            RETURNING lease_expires_at
        )";
        
        sendQueryParamsAsync(conn.get(), sql, {std::to_string(seconds), lease_id});
        auto result = getTuplesResult(conn.get());
        
        if (PQntuples(result.get()) > 0) {
            spdlog::debug("Lease {} extended by {} seconds", lease_id, seconds);
            return true;
        }
        
        spdlog::debug("Lease {} not found or expired", lease_id);
        return false;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to extend lease: {}", e.what());
        return false;
    }
}

// ===================================
// MESSAGE TRACING
// ===================================

bool AsyncQueueManager::record_trace(
    const std::string& transaction_id,
    const std::string& partition_id,
    const std::string& consumer_group,
    const std::vector<std::string>& trace_names,
    const std::string& event_type,
    const nlohmann::json& data,
    const std::string& worker_id
) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Start transaction for atomicity
        sendAndWait(conn.get(), "BEGIN");
        getCommandResult(conn.get());
        
        try {
            // Get message_id from transaction_id + partition_id
            std::string query = R"(
                SELECT id FROM queen.messages 
                WHERE transaction_id = $1 AND partition_id = $2
                LIMIT 1
            )";
            
            sendQueryParamsAsync(conn.get(), query, {transaction_id, partition_id});
            auto result = getTuplesResult(conn.get());
            
            if (PQntuples(result.get()) == 0) {
                spdlog::warn("Message not found for trace: {}/{}", partition_id, transaction_id);
                sendAndWait(conn.get(), "ROLLBACK");
                getCommandResult(conn.get());
                return false;
            }
            
            std::string message_id = PQgetvalue(result.get(), 0, 0);
            
            // Insert main trace record
            std::string insert_query = R"(
                INSERT INTO queen.message_traces 
                (message_id, partition_id, transaction_id, consumer_group, event_type, data, worker_id)
                VALUES ($1, $2, $3, $4, $5, $6, $7)
                RETURNING id
            )";
            
            sendQueryParamsAsync(conn.get(), insert_query, {
                message_id,
                partition_id,
                transaction_id,
                consumer_group,
                event_type,
                data.dump(),
                worker_id
            });
            auto insert_result = getTuplesResult(conn.get());
            
            if (PQntuples(insert_result.get()) == 0) {
                spdlog::error("Failed to insert trace");
                sendAndWait(conn.get(), "ROLLBACK");
                getCommandResult(conn.get());
                return false;
            }
            
            std::string trace_id = PQgetvalue(insert_result.get(), 0, 0);
            
            // Insert trace names (for filtering/searching)
            if (!trace_names.empty()) {
                for (const auto& name : trace_names) {
                    std::string name_insert = R"(
                        INSERT INTO queen.message_trace_names (trace_id, trace_name)
                        VALUES ($1, $2)
                        ON CONFLICT (trace_id, trace_name) DO NOTHING
                    )";
                    sendQueryParamsAsync(conn.get(), name_insert, {trace_id, name});
                    getCommandResult(conn.get());
                }
            }
            
            // Commit transaction
            sendAndWait(conn.get(), "COMMIT");
            getCommandResult(conn.get());
            
            return true;
            
        } catch (const std::exception& e) {
            sendAndWait(conn.get(), "ROLLBACK");
            getCommandResult(conn.get());
            throw;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to record trace: {}", e.what());
        return false;
    }
}

nlohmann::json AsyncQueueManager::get_message_traces(
    const std::string& partition_id,
    const std::string& transaction_id
) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Get traces with their associated names
        std::string query = R"(
            SELECT 
                mt.id,
                mt.event_type,
                mt.data,
                mt.consumer_group,
                mt.worker_id,
                mt.created_at,
                COALESCE(
                    json_agg(mtn.trace_name ORDER BY mtn.trace_name) 
                    FILTER (WHERE mtn.trace_name IS NOT NULL),
                    '[]'::json
                ) as trace_names
            FROM queen.message_traces mt
            LEFT JOIN queen.message_trace_names mtn ON mt.id = mtn.trace_id
            WHERE mt.partition_id = $1 AND mt.transaction_id = $2
            GROUP BY mt.id, mt.event_type, mt.data, mt.consumer_group, mt.worker_id, mt.created_at
            ORDER BY mt.created_at ASC
        )";
        
        sendQueryParamsAsync(conn.get(), query, {partition_id, transaction_id});
        auto result = getTuplesResult(conn.get());
        
        nlohmann::json traces = nlohmann::json::array();
        int num_rows = PQntuples(result.get());
        int num_cols = PQnfields(result.get());
        
        for (int i = 0; i < num_rows; i++) {
            nlohmann::json row;
            for (int j = 0; j < num_cols; j++) {
                std::string field_name = PQfname(result.get(), j);
                if (PQgetisnull(result.get(), i, j)) {
                    row[field_name] = nullptr;
                } else {
                    std::string value = PQgetvalue(result.get(), i, j);
                    // Try to parse JSON fields
                    if (field_name == "data" || field_name == "trace_names") {
                        try {
                            row[field_name] = nlohmann::json::parse(value);
                        } catch (...) {
                            row[field_name] = value;
                        }
                    } else {
                        row[field_name] = value;
                    }
                }
            }
            traces.push_back(row);
        }
        
        return {
            {"traces", traces},
            {"count", num_rows}
        };
    } catch (const std::exception& e) {
        spdlog::error("Failed to get traces: {}", e.what());
        throw;
    }
}

nlohmann::json AsyncQueueManager::get_traces_by_name(
    const std::string& trace_name,
    int limit,
    int offset
) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Query traces by any matching name
        std::string query = R"(
            SELECT 
                mt.id,
                mt.transaction_id,
                mt.partition_id,
                mt.event_type,
                mt.data,
                mt.consumer_group,
                mt.worker_id,
                mt.created_at,
                m.payload as message_payload,
                q.name as queue_name,
                p.name as partition_name,
                COALESCE(
                    json_agg(mtn2.trace_name ORDER BY mtn2.trace_name) 
                    FILTER (WHERE mtn2.trace_name IS NOT NULL),
                    '[]'::json
                ) as trace_names
            FROM queen.message_trace_names mtn
            JOIN queen.message_traces mt ON mtn.trace_id = mt.id
            LEFT JOIN queen.message_trace_names mtn2 ON mt.id = mtn2.trace_id
            LEFT JOIN queen.messages m ON mt.message_id = m.id
            LEFT JOIN queen.partitions p ON mt.partition_id = p.id
            LEFT JOIN queen.queues q ON p.queue_id = q.id
            WHERE mtn.trace_name = $1
            GROUP BY mt.id, mt.transaction_id, mt.partition_id, mt.event_type, 
                     mt.data, mt.consumer_group, mt.worker_id, mt.created_at,
                     m.payload, q.name, p.name
            ORDER BY mt.created_at ASC
            LIMIT $2 OFFSET $3
        )";
        
        sendQueryParamsAsync(conn.get(), query, {
            trace_name,
            std::to_string(limit),
            std::to_string(offset)
        });
        auto result = getTuplesResult(conn.get());
        
        nlohmann::json traces = nlohmann::json::array();
        int num_rows = PQntuples(result.get());
        int num_cols = PQnfields(result.get());
        
        for (int i = 0; i < num_rows; i++) {
            nlohmann::json row;
            for (int j = 0; j < num_cols; j++) {
                std::string field_name = PQfname(result.get(), j);
                if (PQgetisnull(result.get(), i, j)) {
                    row[field_name] = nullptr;
                } else {
                    std::string value = PQgetvalue(result.get(), i, j);
                    // Try to parse JSON fields
                    if (field_name == "data" || field_name == "trace_names" || field_name == "message_payload") {
                        try {
                            row[field_name] = nlohmann::json::parse(value);
                        } catch (...) {
                            row[field_name] = value;
                        }
                    } else {
                        row[field_name] = value;
                    }
                }
            }
            traces.push_back(row);
        }
        
        // Get total count
        std::string count_query = R"(
            SELECT COUNT(DISTINCT mt.id)
            FROM queen.message_trace_names mtn
            JOIN queen.message_traces mt ON mtn.trace_id = mt.id
            WHERE mtn.trace_name = $1
        )";
        
        sendQueryParamsAsync(conn.get(), count_query, {trace_name});
        auto count_result = getTuplesResult(conn.get());
        int total = (PQntuples(count_result.get()) > 0) ? std::stoi(PQgetvalue(count_result.get(), 0, 0)) : 0;
        
        return {
            {"traces", traces},
            {"count", num_rows},
            {"total", total},
            {"limit", limit},
            {"offset", offset}
        };
    } catch (const std::exception& e) {
        spdlog::error("Failed to get traces by name: {}", e.what());
        throw;
    }
}

nlohmann::json AsyncQueueManager::get_available_trace_names(
    int limit,
    int offset
) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Query for distinct trace names with counts
        std::string query = R"(
            SELECT 
                trace_name,
                COUNT(DISTINCT trace_id) as trace_count,
                COUNT(DISTINCT mt.transaction_id) as message_count,
                MAX(mt.created_at) as last_seen
            FROM queen.message_trace_names mtn
            JOIN queen.message_traces mt ON mtn.trace_id = mt.id
            GROUP BY trace_name
            ORDER BY last_seen DESC
            LIMIT $1 OFFSET $2
        )";
        
        sendQueryParamsAsync(conn.get(), query, {
            std::to_string(limit),
            std::to_string(offset)
        });
        auto result = getTuplesResult(conn.get());
        
        nlohmann::json trace_names = nlohmann::json::array();
        int num_rows = PQntuples(result.get());
        int num_cols = PQnfields(result.get());
        
        for (int i = 0; i < num_rows; i++) {
            nlohmann::json row;
            for (int j = 0; j < num_cols; j++) {
                std::string field_name = PQfname(result.get(), j);
                if (PQgetisnull(result.get(), i, j)) {
                    row[field_name] = nullptr;
                } else {
                    std::string value = PQgetvalue(result.get(), i, j);
                    // Convert numeric fields
                    if (field_name == "trace_count" || field_name == "message_count") {
                        row[field_name] = std::stoi(value);
                    } else {
                        row[field_name] = value;
                    }
                }
            }
            trace_names.push_back(row);
        }
        
        // Get total count
        std::string count_query = R"(
            SELECT COUNT(DISTINCT trace_name) as total
            FROM queen.message_trace_names
        )";
        
        sendQueryParamsAsync(conn.get(), count_query, {});
        auto count_result = getTuplesResult(conn.get());
        int total = (PQntuples(count_result.get()) > 0) ? std::stoi(PQgetvalue(count_result.get(), 0, 0)) : 0;
        
        return {
            {"trace_names", trace_names},
            {"count", num_rows},
            {"total", total},
            {"limit", limit},
            {"offset", offset}
        };
    } catch (const std::exception& e) {
        spdlog::error("Failed to get available trace names: {}", e.what());
        throw;
    }
}

// ===================================
// STREAM MANAGEMENT
// ===================================

nlohmann::json AsyncQueueManager::list_streams() {
    try {
        auto conn = async_db_pool_->acquire();
        
        sendAndWait(conn.get(), "SELECT * FROM queen.streams ORDER BY created_at DESC");
        auto result = getTuplesResult(conn.get());
        
        nlohmann::json streams = nlohmann::json::array();
        int num_rows = PQntuples(result.get());
        
        for (int i = 0; i < num_rows; i++) {
            std::string stream_id = PQgetvalue(result.get(), i, PQfnumber(result.get(), "id"));
            
            nlohmann::json stream = {
                {"id", stream_id},
                {"name", PQgetvalue(result.get(), i, PQfnumber(result.get(), "name"))},
                {"namespace", PQgetvalue(result.get(), i, PQfnumber(result.get(), "namespace"))},
                {"partitioned", std::string(PQgetvalue(result.get(), i, PQfnumber(result.get(), "partitioned"))) == "t"},
                {"windowType", PQgetvalue(result.get(), i, PQfnumber(result.get(), "window_type"))},
                {"windowDurationMs", std::stoll(PQgetvalue(result.get(), i, PQfnumber(result.get(), "window_duration_ms")))},
                {"windowGracePeriodMs", std::stoll(PQgetvalue(result.get(), i, PQfnumber(result.get(), "window_grace_period_ms")))},
                {"windowLeaseTimeoutMs", std::stoll(PQgetvalue(result.get(), i, PQfnumber(result.get(), "window_lease_timeout_ms")))},
                {"createdAt", PQgetvalue(result.get(), i, PQfnumber(result.get(), "created_at"))},
                {"updatedAt", PQgetvalue(result.get(), i, PQfnumber(result.get(), "updated_at"))}
            };
            
            // Get source queues for this stream
            sendQueryParamsAsync(conn.get(),
                "SELECT q.name FROM queen.stream_sources ss JOIN queen.queues q ON ss.queue_id = q.id WHERE ss.stream_id = $1::UUID",
                {stream_id}
            );
            auto sources_result = getTuplesResult(conn.get());
            nlohmann::json source_queues = nlohmann::json::array();
            for (int j = 0; j < PQntuples(sources_result.get()); j++) {
                source_queues.push_back(PQgetvalue(sources_result.get(), j, 0));
            }
            stream["sourceQueues"] = source_queues;
            
            // Get active leases count
            sendQueryParamsAsync(conn.get(),
                "SELECT COUNT(*) as count FROM queen.stream_leases WHERE stream_id = $1::UUID AND lease_expires_at > NOW()",
                {stream_id}
            );
            auto leases_result = getTuplesResult(conn.get());
            stream["activeLeases"] = (PQntuples(leases_result.get()) > 0) ? std::stoi(PQgetvalue(leases_result.get(), 0, 0)) : 0;
            
            // Get consumer groups count
            sendQueryParamsAsync(conn.get(),
                "SELECT COUNT(DISTINCT consumer_group) as count FROM queen.stream_consumer_offsets WHERE stream_id = $1::UUID",
                {stream_id}
            );
            auto consumers_result = getTuplesResult(conn.get());
            stream["consumerGroups"] = (PQntuples(consumers_result.get()) > 0) ? std::stoi(PQgetvalue(consumers_result.get(), 0, 0)) : 0;
            
            streams.push_back(stream);
        }
        
        return {{"streams", streams}};
    } catch (const std::exception& e) {
        spdlog::error("Failed to list streams: {}", e.what());
        throw;
    }
}

nlohmann::json AsyncQueueManager::get_stream_stats() {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Total streams
        sendAndWait(conn.get(), "SELECT COUNT(*) as count FROM queen.streams");
        auto streams_result = getTuplesResult(conn.get());
        int total_streams = (PQntuples(streams_result.get()) > 0) ? std::stoi(PQgetvalue(streams_result.get(), 0, 0)) : 0;
        
        // Partitioned streams count
        sendAndWait(conn.get(), "SELECT COUNT(*) as count FROM queen.streams WHERE partitioned = true");
        auto partitioned_result = getTuplesResult(conn.get());
        int partitioned_streams = (PQntuples(partitioned_result.get()) > 0) ? std::stoi(PQgetvalue(partitioned_result.get(), 0, 0)) : 0;
        
        // Active leases (windows being processed)
        sendAndWait(conn.get(), "SELECT COUNT(*) as count FROM queen.stream_leases WHERE lease_expires_at > NOW()");
        auto leases_result = getTuplesResult(conn.get());
        int active_leases = (PQntuples(leases_result.get()) > 0) ? std::stoi(PQgetvalue(leases_result.get(), 0, 0)) : 0;
        
        // Total unique consumer groups across all streams
        sendAndWait(conn.get(), "SELECT COUNT(DISTINCT consumer_group) as count FROM queen.stream_consumer_offsets");
        auto consumer_groups_result = getTuplesResult(conn.get());
        int total_consumer_groups = (PQntuples(consumer_groups_result.get()) > 0) ? std::stoi(PQgetvalue(consumer_groups_result.get(), 0, 0)) : 0;
        
        // Active consumers (consumer groups that consumed in last hour)
        sendAndWait(conn.get(),
            "SELECT COUNT(DISTINCT consumer_group) as count FROM queen.stream_consumer_offsets "
            "WHERE last_consumed_at > NOW() - INTERVAL '1 hour'"
        );
        auto active_consumers_result = getTuplesResult(conn.get());
        int active_consumers = (PQntuples(active_consumers_result.get()) > 0) ? std::stoi(PQgetvalue(active_consumers_result.get(), 0, 0)) : 0;
        
        // Total windows processed (sum of all consumer groups)
        sendAndWait(conn.get(), "SELECT COALESCE(SUM(total_windows_consumed), 0) as total FROM queen.stream_consumer_offsets");
        auto windows_processed_result = getTuplesResult(conn.get());
        long long total_windows_processed = (PQntuples(windows_processed_result.get()) > 0) ? std::stoll(PQgetvalue(windows_processed_result.get(), 0, 0)) : 0;
        
        // Windows processed in last hour
        sendAndWait(conn.get(),
            "SELECT COUNT(*) as count FROM queen.stream_consumer_offsets "
            "WHERE last_consumed_at > NOW() - INTERVAL '1 hour'"
        );
        auto windows_hour_result = getTuplesResult(conn.get());
        int windows_last_hour = (PQntuples(windows_hour_result.get()) > 0) ? std::stoi(PQgetvalue(windows_hour_result.get(), 0, 0)) : 0;
        
        // Average lease time (in seconds)
        sendAndWait(conn.get(),
            "SELECT COALESCE(AVG(EXTRACT(EPOCH FROM (lease_expires_at - CURRENT_TIMESTAMP))), 0) as avg_seconds "
            "FROM queen.stream_leases WHERE lease_expires_at > NOW()"
        );
        auto avg_lease_result = getTuplesResult(conn.get());
        int avg_lease_time = (PQntuples(avg_lease_result.get()) > 0) ? static_cast<int>(std::stod(PQgetvalue(avg_lease_result.get(), 0, 0))) : 0;
        
        return {
            {"totalStreams", total_streams},
            {"partitionedStreams", partitioned_streams},
            {"activeLeases", active_leases},
            {"totalConsumerGroups", total_consumer_groups},
            {"activeConsumers", active_consumers},
            {"totalWindowsProcessed", total_windows_processed},
            {"windowsLastHour", windows_last_hour},
            {"avgLeaseTime", avg_lease_time}
        };
    } catch (const std::exception& e) {
        spdlog::error("Failed to get stream stats: {}", e.what());
        throw;
    }
}

nlohmann::json AsyncQueueManager::get_stream_details(const std::string& stream_name) {
    try {
        auto conn = async_db_pool_->acquire();
        
        sendQueryParamsAsync(conn.get(), "SELECT * FROM queen.streams WHERE name = $1", {stream_name});
        auto result = getTuplesResult(conn.get());
        
        if (PQntuples(result.get()) == 0) {
            throw std::runtime_error("Stream not found");
        }
        
        return {
            {"id", PQgetvalue(result.get(), 0, PQfnumber(result.get(), "id"))},
            {"name", PQgetvalue(result.get(), 0, PQfnumber(result.get(), "name"))},
            {"namespace", PQgetvalue(result.get(), 0, PQfnumber(result.get(), "namespace"))},
            {"partitioned", std::string(PQgetvalue(result.get(), 0, PQfnumber(result.get(), "partitioned"))) == "t"},
            {"windowType", PQgetvalue(result.get(), 0, PQfnumber(result.get(), "window_type"))},
            {"windowDurationMs", std::stoll(PQgetvalue(result.get(), 0, PQfnumber(result.get(), "window_duration_ms")))},
            {"windowGracePeriodMs", std::stoll(PQgetvalue(result.get(), 0, PQfnumber(result.get(), "window_grace_period_ms")))},
            {"windowLeaseTimeoutMs", std::stoll(PQgetvalue(result.get(), 0, PQfnumber(result.get(), "window_lease_timeout_ms")))},
            {"createdAt", PQgetvalue(result.get(), 0, PQfnumber(result.get(), "created_at"))},
            {"updatedAt", PQgetvalue(result.get(), 0, PQfnumber(result.get(), "updated_at"))}
        };
    } catch (const std::exception& e) {
        spdlog::error("Failed to get stream details: {}", e.what());
        throw;
    }
}

nlohmann::json AsyncQueueManager::get_stream_consumers(const std::string& stream_name) {
    try {
        auto conn = async_db_pool_->acquire();
        
        // Get stream ID
        sendQueryParamsAsync(conn.get(), "SELECT id FROM queen.streams WHERE name = $1", {stream_name});
        auto stream_result = getTuplesResult(conn.get());
        
        if (PQntuples(stream_result.get()) == 0) {
            throw std::runtime_error("Stream not found");
        }
        
        std::string stream_id = PQgetvalue(stream_result.get(), 0, 0);
        
        // Get consumer groups for this stream
        std::string query = R"(
            SELECT 
                consumer_group,
                last_partition_name,
                last_window_start,
                last_window_end,
                total_windows_consumed,
                last_consumed_at
            FROM queen.stream_consumer_offsets
            WHERE stream_id = $1::UUID
            ORDER BY last_consumed_at DESC
        )";
        
        sendQueryParamsAsync(conn.get(), query, {stream_id});
        auto result = getTuplesResult(conn.get());
        
        nlohmann::json consumers = nlohmann::json::array();
        int num_rows = PQntuples(result.get());
        int num_cols = PQnfields(result.get());
        
        for (int i = 0; i < num_rows; i++) {
            nlohmann::json consumer;
            for (int j = 0; j < num_cols; j++) {
                std::string field_name = PQfname(result.get(), j);
                if (PQgetisnull(result.get(), i, j)) {
                    consumer[field_name] = nullptr;
                } else {
                    std::string value = PQgetvalue(result.get(), i, j);
                    // Convert numeric fields
                    if (field_name == "total_windows_consumed") {
                        consumer[field_name] = std::stoll(value);
                    } else {
                        consumer[field_name] = value;
                    }
                }
            }
            consumers.push_back(consumer);
        }
        
        return {{"consumers", consumers}};
    } catch (const std::exception& e) {
        spdlog::error("Failed to get stream consumers: {}", e.what());
        throw;
    }
}

bool AsyncQueueManager::delete_stream(const std::string& stream_name) {
    try {
        auto conn = async_db_pool_->acquire();
        
        sendQueryParamsAsync(conn.get(), "DELETE FROM queen.streams WHERE name = $1 RETURNING id", {stream_name});
        auto result = getTuplesResult(conn.get());
        
        if (PQntuples(result.get()) == 0) {
            spdlog::warn("Stream not found: {}", stream_name);
            return false;
        }
        
        spdlog::info("Stream deleted successfully: {}", stream_name);
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to delete stream: {}", e.what());
        return false;
    }
}

} // namespace queen

