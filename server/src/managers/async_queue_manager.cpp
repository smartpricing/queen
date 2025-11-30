#include "queen/async_queue_manager.hpp"
#include "queen/file_buffer.hpp"
#include "queen/encryption.hpp"
#include "queen/shared_state_manager.hpp"
#include <spdlog/spdlog.h>

// External global for shared state
namespace queen {
extern std::shared_ptr<SharedStateManager> global_shared_state;
}
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

// --- Consumer Group Subscription Metadata ---

void AsyncQueueManager::delete_consumer_group(
    const std::string& consumer_group,
    bool delete_metadata
) {
    auto conn = async_db_pool_->acquire();
    if (!conn) {
        throw std::runtime_error("Failed to acquire DB connection");
    }
    
    try {
        // Delete partition consumers
        std::string delete_pc_sql = R"(
            DELETE FROM queen.partition_consumers
            WHERE consumer_group = $1
        )";
        sendQueryParamsAsync(conn.get(), delete_pc_sql, {consumer_group});
        getCommandResult(conn.get());
        
        // Delete metadata if requested
        if (delete_metadata) {
            std::string delete_metadata_sql = R"(
                DELETE FROM queen.consumer_groups_metadata
                WHERE consumer_group = $1
            )";
            sendQueryParamsAsync(conn.get(), delete_metadata_sql, {consumer_group});
            getCommandResult(conn.get());
            spdlog::info("Deleted consumer group '{}' and its metadata", consumer_group);
        } else {
            spdlog::info("Deleted consumer group '{}' (metadata preserved)", consumer_group);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to delete consumer group '{}': {}", consumer_group, e.what());
        throw;
    }
}

void AsyncQueueManager::update_consumer_group_subscription(
    const std::string& consumer_group,
    const std::string& new_timestamp
) {
    auto conn = async_db_pool_->acquire();
    if (!conn) {
        throw std::runtime_error("Failed to acquire DB connection");
    }
    
    try {
        std::string update_sql = R"(
            UPDATE queen.consumer_groups_metadata
            SET subscription_timestamp = $2::timestamptz
            WHERE consumer_group = $1
        )";
        
        sendQueryParamsAsync(conn.get(), update_sql, {consumer_group, new_timestamp});
        getCommandResult(conn.get());
        
        spdlog::info("Updated subscription timestamp for consumer group '{}' to {}", 
                    consumer_group, new_timestamp);
    } catch (const std::exception& e) {
        spdlog::error("Failed to update subscription timestamp for '{}': {}", 
                     consumer_group, e.what());
        throw;
    }
}

void AsyncQueueManager::record_consumer_group_subscription(
    const std::string& consumer_group,
    const std::string& queue_name,
    const std::string& partition_name,
    const std::string& namespace_name,
    const std::string& task_name,
    const std::string& subscription_mode,
    const std::string& subscription_timestamp_sql
) {
    auto conn = async_db_pool_->acquire();
    if (!conn) {
        spdlog::warn("Failed to acquire DB connection for recording CG subscription");
        return; // Non-fatal - continue with pop
    }
    
    std::string upsert_sql = R"(
        INSERT INTO queen.consumer_groups_metadata (
            consumer_group, queue_name, partition_name, namespace, task,
            subscription_mode, subscription_timestamp, created_at
        ) VALUES (
            $1, $2, $3, $4, $5, $6, 
            )" + subscription_timestamp_sql + R"(, 
            NOW()
        ) ON CONFLICT (consumer_group, queue_name, partition_name, namespace, task)
        DO NOTHING
    )";
    
    std::vector<std::string> params = {
        consumer_group,
        queue_name,
        partition_name,
        namespace_name,
        task_name,
        subscription_mode
    };
    
    try {
        sendQueryParamsAsync(conn.get(), upsert_sql, params);
        getCommandResult(conn.get());
        spdlog::debug("Recorded CG subscription: group={}, mode={}, queue={}, partition={}", 
                     consumer_group, subscription_mode, queue_name, partition_name);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to record CG subscription (non-fatal): {}", e.what());
        // Non-fatal - continue with pop even if metadata recording fails
    }
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
            
            CREATE TABLE IF NOT EXISTS queen.consumer_groups_metadata (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                consumer_group TEXT NOT NULL,
                queue_name TEXT NOT NULL DEFAULT '',
                partition_name TEXT NOT NULL DEFAULT '',
                namespace TEXT NOT NULL DEFAULT '',
                task TEXT NOT NULL DEFAULT '',
                subscription_mode TEXT NOT NULL,
                subscription_timestamp TIMESTAMPTZ NOT NULL,
                created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
                UNIQUE (consumer_group, queue_name, partition_name, namespace, task)
            );
            
            CREATE INDEX IF NOT EXISTS idx_consumer_groups_metadata_lookup 
            ON queen.consumer_groups_metadata(consumer_group, queue_name, namespace, task);
            
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
            
            -- 6. Partition Lookup Table (Performance Optimization)
            CREATE TABLE IF NOT EXISTS queen.partition_lookup (
                id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                queue_name VARCHAR(255) NOT NULL REFERENCES queen.queues(name) ON DELETE CASCADE,
                partition_id UUID NOT NULL REFERENCES queen.partitions(id) ON DELETE CASCADE,
                last_message_id UUID NOT NULL,
                last_message_created_at TIMESTAMPTZ NOT NULL,
                updated_at TIMESTAMPTZ DEFAULT NOW(),
                UNIQUE(queue_name, partition_id)
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
            
            -- Partition lookup indexes
            CREATE INDEX IF NOT EXISTS idx_partition_lookup_queue_name ON queen.partition_lookup(queue_name);
            CREATE INDEX IF NOT EXISTS idx_partition_lookup_partition_id ON queen.partition_lookup(partition_id);
            CREATE INDEX IF NOT EXISTS idx_partition_lookup_timestamp ON queen.partition_lookup(last_message_created_at DESC);
            
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

            -- Partition lookup trigger (statement-level, batch-efficient)
            CREATE OR REPLACE FUNCTION queen.update_partition_lookup_trigger()
            RETURNS TRIGGER AS $$
            BEGIN
                -- Find the latest message per partition in this batch
                WITH batch_max AS (
                    SELECT DISTINCT ON (partition_id)
                        partition_id, 
                        created_at as max_created_at,
                        id as max_id
                    FROM new_messages
                    ORDER BY partition_id, created_at DESC, id DESC
                )
                -- Update lookup table once per partition
                INSERT INTO queen.partition_lookup (
                    queue_name, partition_id, last_message_id, last_message_created_at, updated_at
                )
                SELECT 
                    q.name, 
                    bm.partition_id, 
                    bm.max_id, 
                    bm.max_created_at, 
                    NOW()
                FROM batch_max bm
                JOIN queen.partitions p ON p.id = bm.partition_id
                JOIN queen.queues q ON q.id = p.queue_id
                ON CONFLICT (queue_name, partition_id)
                DO UPDATE SET
                    last_message_id = EXCLUDED.last_message_id,
                    last_message_created_at = EXCLUDED.last_message_created_at,
                    updated_at = NOW()
                WHERE 
                    -- Only update if new message is actually newer (handles out-of-order commits)
                    EXCLUDED.last_message_created_at > queen.partition_lookup.last_message_created_at
                    OR (EXCLUDED.last_message_created_at = queen.partition_lookup.last_message_created_at 
                        AND EXCLUDED.last_message_id > queen.partition_lookup.last_message_id);
                RETURN NULL;
            END;
            $$ LANGUAGE plpgsql;

            DROP TRIGGER IF EXISTS trg_update_partition_lookup ON queen.messages;
            CREATE TRIGGER trg_update_partition_lookup
            AFTER INSERT ON queen.messages
            REFERENCING NEW TABLE AS new_messages
            FOR EACH STATEMENT
            EXECUTE FUNCTION queen.update_partition_lookup_trigger();
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
    // NO CACHE: Always check database for maintenance mode
    // This ensures all workers see the current state immediately
    // Performance impact is minimal since maintenance mode toggles are rare
    
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
        
        // Update local cache for status endpoint
        maintenance_mode_cached_.store(enabled);
        
        return enabled;
    } catch (const std::exception& e) {
        spdlog::error("Failed to check maintenance mode from database: {}", e.what());
        // On DB error, return cached value as fallback
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
// --- Push Messages Internal (Bypasses Maintenance Mode) ---

std::vector<PushResult> AsyncQueueManager::push_messages_internal(const std::vector<PushItem>& items) {
    if (items.empty()) {
        return {};
    }
    
    // NOTE: This method NEVER checks maintenance mode
    // It's used ONLY for internal operations like file buffer drain
    // DO NOT call from user-facing code paths!
    
    spdlog::debug("push_messages_internal: Processing {} items (bypassing maintenance mode)", items.size());
    
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
    
    // Process each partition group using stored procedure
    for (const auto& [partition_key, item_indices] : partition_groups_ordered) {
    try {
        auto conn = async_db_pool_->acquire();
        
            // Build JSON array for stored procedure
            nlohmann::json messages_array = nlohmann::json::array();
            
            for (size_t idx : item_indices) {
                const auto& item = items[idx];
                std::string msg_id = generate_uuid();
                std::string txn_id = item.transaction_id.value_or(generate_transaction_id());
                
                // Check encryption
        bool encryption_enabled = false;
                std::string payload_str = item.payload.dump();
        
        if (global_shared_state && global_shared_state->is_enabled()) {
                    auto cached_config = global_shared_state->get_queue_config(item.queue);
            if (cached_config) {
                encryption_enabled = cached_config->encryption_enabled;
                    }
            }
                
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
                
                nlohmann::json msg_obj;
                msg_obj["messageId"] = msg_id;
                msg_obj["transactionId"] = txn_id;
                msg_obj["queue"] = item.queue;
                msg_obj["partition"] = item.partition;
                msg_obj["payload"] = payload_str;
                msg_obj["is_encrypted"] = is_encrypted;
                if (item.trace_id.has_value() && !item.trace_id->empty()) {
                    msg_obj["traceId"] = *item.trace_id;
                }
                
                messages_array.push_back(msg_obj);
                
                // Pre-populate result
                PushResult result;
                result.transaction_id = txn_id;
                result.message_id = msg_id;
                result.status = "queued";
                all_results[idx] = result;
                }
                
            // Call stored procedure
            std::string sql = "SELECT queen.push_messages_v2($1::jsonb)";
            sendQueryParamsAsync(conn.get(), sql, {messages_array.dump()});
            auto result = getTuplesResult(conn.get());
            
            if (PQntuples(result.get()) > 0) {
                const char* result_str = PQgetvalue(result.get(), 0, 0);
                if (result_str && strlen(result_str) > 0) {
                    try {
                        nlohmann::json sp_result = nlohmann::json::parse(result_str);
                        
                        if (sp_result.contains("error")) {
                            std::string error_msg = sp_result["error"].get<std::string>();
                            for (size_t idx : item_indices) {
                                all_results[idx].status = "failed";
                                all_results[idx].error = error_msg;
                    }
                }
                    } catch (...) {
                        // SP returned success without detailed JSON
                    }
                }
            }
            
        } catch (const std::exception& e) {
            spdlog::error("push_messages_internal failed for partition group: {}", e.what());
            for (size_t idx : item_indices) {
                all_results[idx].status = "failed";
                all_results[idx].error = e.what();
            }
        }
    }
    
    return all_results;
        }
        
// --- Helper: Check if error is connection-related (not retryable via cache invalidation) ---
static bool is_connection_error(const std::string& error_msg) {
    // Connection errors should NOT trigger cache invalidation retry
    // These indicate the DB is down, not that cache is stale
    return error_msg.find("connection") != std::string::npos ||
           error_msg.find("Connection") != std::string::npos ||
           error_msg.find("server closed") != std::string::npos ||
           error_msg.find("could not connect") != std::string::npos ||
           error_msg.find("timeout") != std::string::npos ||
           error_msg.find("SSL") != std::string::npos;
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

// --- Pop using Stored Procedure (Single Round Trip) ---
// HIGH-PERFORMANCE version used by poll workers for wait=true long-polling

PopResult AsyncQueueManager::pop_messages_sp(
    const std::string& queue_name,
    const std::string& partition_name,  // Empty for any partition
    const std::string& consumer_group,
    const PopOptions& options
) {
    PopResult result;
    
    try {
        auto conn = async_db_pool_->acquire();
        
        // Call the stored procedure
        std::string sql = "SELECT queen.pop_messages_v2($1, $2, $3, $4, $5, $6, $7)";
        std::vector<std::string> params = {
            queue_name,
            partition_name,  // Empty string will be treated as NULL by SP
            consumer_group,
            std::to_string(options.batch),
            "0",  // lease_time_seconds = 0 means use queue config
            options.subscription_mode.value_or("all"),
            options.subscription_from.value_or("")
        };
        
        sendQueryParamsAsync(conn.get(), sql, params);
        auto query_result = getTuplesResult(conn.get());
        
        if (PQntuples(query_result.get()) == 0) {
            return result;
        }
        
        // Parse JSON result
        const char* json_str = PQgetvalue(query_result.get(), 0, 0);
        if (!json_str || strlen(json_str) == 0) {
            return result;
        }
        
        auto json_result = nlohmann::json::parse(json_str);
        
        // Extract lease ID
        if (json_result.contains("leaseId") && !json_result["leaseId"].is_null()) {
            result.lease_id = json_result["leaseId"].get<std::string>();
        }
        
        // Extract messages
        if (!json_result.contains("messages") || !json_result["messages"].is_array()) {
            return result;
        }
        
        auto& messages_json = json_result["messages"];
        if (messages_json.empty()) {
            return result;
        }
        
        // Get encryption service if needed
        EncryptionService* enc_service = get_encryption_service();
        
        // Parse each message
        for (const auto& msg_json : messages_json) {
            Message msg;
            
            msg.id = msg_json.value("id", "");
            msg.transaction_id = msg_json.value("transactionId", "");
            msg.partition_id = msg_json.value("partitionId", "");
            msg.queue_name = msg_json.value("queue", "");
            msg.partition_name = msg_json.value("partition", "");
            msg.retry_count = msg_json.value("retryCount", 0);
            msg.priority = msg_json.value("priority", 0);
            
            // Parse trace_id
            if (msg_json.contains("traceId") && !msg_json["traceId"].is_null()) {
                msg.trace_id = msg_json["traceId"].get<std::string>();
            }
            
            // Parse created_at timestamp
            if (msg_json.contains("createdAt") && !msg_json["createdAt"].is_null()) {
                std::string ts_str = msg_json["createdAt"].get<std::string>();
                // Parse ISO8601 timestamp
                std::tm tm = {};
                int ms = 0;
                if (sscanf(ts_str.c_str(), "%d-%d-%dT%d:%d:%d.%dZ",
                          &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                          &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) >= 6) {
                    tm.tm_year -= 1900;
                    tm.tm_mon -= 1;
                    time_t time = timegm(&tm);
                    msg.created_at = std::chrono::system_clock::from_time_t(time) + 
                                    std::chrono::milliseconds(ms);
            }
            }
            
            // Handle payload/data - check if encrypted
            nlohmann::json payload_data;
            if (msg_json.contains("data")) {
                payload_data = msg_json["data"];
            }
            
            // Check if encrypted and decrypt
            if (payload_data.is_object() && 
                payload_data.contains("encrypted") && 
                payload_data.contains("iv") && 
                payload_data.contains("authTag") &&
                enc_service && enc_service->is_enabled()) {
                try {
                    EncryptionService::EncryptedData encrypted_data{
                        payload_data["encrypted"].get<std::string>(),
                        payload_data["iv"].get<std::string>(),
                        payload_data["authTag"].get<std::string>()
                    };
                    auto decrypted = enc_service->decrypt_payload(encrypted_data);
                    if (decrypted.has_value()) {
                        msg.payload = nlohmann::json::parse(decrypted.value());
                    } else {
                        msg.payload = payload_data;
                    }
                } catch (...) {
                    msg.payload = payload_data;
                }
            } else {
                msg.payload = payload_data;
            }
            
            result.messages.push_back(msg);
        }
        
        // Auto-ack if enabled - use stored procedure directly
        if (options.auto_ack && !result.messages.empty() && result.lease_id.has_value()) {
            spdlog::debug("Auto-ack enabled - acknowledging {} messages via stored procedure", result.messages.size());
            
            try {
                // Build JSON array for ack stored procedure
                nlohmann::json ack_array = nlohmann::json::array();
                for (const auto& msg : result.messages) {
                    nlohmann::json ack_obj;
                    ack_obj["transaction_id"] = msg.transaction_id;
                    ack_obj["partition_id"] = msg.partition_id;
                    ack_obj["consumer_group"] = consumer_group;
                    ack_obj["status"] = "completed";
                    ack_array.push_back(ack_obj);
                }
                
                std::string ack_sql = "SELECT queen.ack_messages_v2($1::jsonb)";
                sendQueryParamsAsync(conn.get(), ack_sql, {ack_array.dump()});
                getTuplesResult(conn.get());
        
                spdlog::debug("Auto-ack completed for {} messages", result.messages.size());
    } catch (const std::exception& e) {
                spdlog::error("Auto-ack failed: {}", e.what());
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("pop_messages_sp failed: {}", e.what());
    }
    
    return result;
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
        
        // Handle file buffer state changes
        if (file_buffer_manager_) {
            if (enabled) {
                // ENABLE: Pause background drain to prevent infinite loop
                file_buffer_manager_->pause_background_drain();
                spdlog::info("Maintenance mode ENABLED - PUSHes routing to file buffer, drain paused");
            } else {
                // DISABLE: Force finalize .tmp files and resume drain
                spdlog::info("Maintenance mode DISABLED - finalizing buffered messages and resuming drain");
                
                // Step 1: Force finalize all .tmp files to make messages visible
                file_buffer_manager_->force_finalize_all();
                
                // Step 2: Resume background drain (will wake processor)
                file_buffer_manager_->resume_background_drain();
                
                size_t pending = file_buffer_manager_->get_pending_count();
                spdlog::info("File buffer will drain {} pending messages to database", pending);
            }
        } else {
            spdlog::info("Maintenance mode {} (persisted to database for all instances)", 
                        enabled ? "ENABLED - routing PUSHes to file buffer" : "DISABLED - resuming normal operations");
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
        
        // Invalidate partition cache since partition may have been recreated with new ID
        if (global_shared_state && global_shared_state->is_enabled()) {
            global_shared_state->invalidate_partition(queue_name, "Default");
            spdlog::debug("Invalidated partition cache for {}:Default", queue_name);
        }
        
        // Broadcast queue config to peers (Phase 2: Queue Config Cache)
        if (global_shared_state && global_shared_state->is_enabled()) {
            caches::CachedQueueConfig cached_config;
            cached_config.name = queue_name;
            cached_config.namespace_name = namespace_name;
            cached_config.task = task_name;
            cached_config.priority = options.priority;
            cached_config.lease_time = options.lease_time;
            cached_config.retry_limit = options.retry_limit;
            cached_config.retry_delay = options.retry_delay;
            cached_config.max_size = options.max_size;
            cached_config.ttl = options.ttl;
            cached_config.dlq_enabled = options.dead_letter_queue;
            cached_config.dlq_after_max_retries = options.dlq_after_max_retries;
            cached_config.delayed_processing = options.delayed_processing;
            cached_config.window_buffer = options.window_buffer;
            cached_config.retention_seconds = options.retention_seconds;
            cached_config.completed_retention_seconds = options.completed_retention_seconds;
            cached_config.encryption_enabled = options.encryption_enabled;
            global_shared_state->set_queue_config(queue_name, cached_config);
            spdlog::debug("Queue '{}' config broadcast to peers", queue_name);
        }
        
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
            
            // Broadcast queue deletion to peers (Phase 2: Queue Config Cache)
            if (global_shared_state && global_shared_state->is_enabled()) {
                global_shared_state->delete_queue_config(queue_name);
                spdlog::debug("Queue '{}' deletion broadcast to peers", queue_name);
            }
            
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

