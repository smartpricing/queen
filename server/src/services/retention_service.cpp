#include "queen/retention_service.hpp"
#include "queen/shared_state_manager.hpp"

namespace queen {

// External global for shared state
extern std::shared_ptr<SharedStateManager> global_shared_state;

RetentionService::RetentionService(
    std::shared_ptr<AsyncDbPool> db_pool,
    std::shared_ptr<astp::ThreadPool> db_thread_pool,
    std::shared_ptr<astp::ThreadPool> system_thread_pool,
    int retention_interval_ms,
    int retention_batch_size,
    int partition_cleanup_days,
    int metrics_retention_days
) : db_pool_(db_pool),
    db_thread_pool_(db_thread_pool),
    system_thread_pool_(system_thread_pool),
    retention_interval_ms_(retention_interval_ms),
    retention_batch_size_(retention_batch_size),
    partition_cleanup_days_(partition_cleanup_days),
    metrics_retention_days_(metrics_retention_days) {
}

RetentionService::~RetentionService() {
    stop();
}

void RetentionService::start() {
    if (running_) {
        spdlog::warn("RetentionService already running");
        return;
    }
    
    running_ = true;
    schedule_next_run();
    
    spdlog::info("RetentionService started: interval={}ms, batch_size={}, "
                 "partition_cleanup_days={}, metrics_retention_days={}",
                 retention_interval_ms_, retention_batch_size_,
                 partition_cleanup_days_, metrics_retention_days_);
}

void RetentionService::stop() {
    if (!running_) return;
    running_ = false;
    spdlog::info("RetentionService stopped");
}

void RetentionService::schedule_next_run() {
    if (!running_) return;
    
    // Schedule in system threadpool (same pattern as MetricsCollector)
    system_thread_pool_->push([this]() {
        this->cleanup_cycle();
    });
}

void RetentionService::cleanup_cycle() {
    auto cycle_start = std::chrono::steady_clock::now();
    
    try {
        // Run all cleanup operations
        int expired = cleanup_expired_messages();
        int completed = cleanup_completed_messages();
        int partitions = cleanup_inactive_partitions();
        int metrics = cleanup_old_metrics();
        
        // Only log if something was cleaned up
        if (expired > 0 || completed > 0 || partitions > 0 || metrics > 0) {
            spdlog::info("RetentionService: Cleaned up expired_messages={}, completed_messages={}, "
                        "inactive_partitions={}, old_metrics={}",
                        expired, completed, partitions, metrics);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("RetentionService cycle error: {}", e.what());
    }
    
    // Calculate sleep time and reschedule
    if (running_) {
        auto cycle_end = std::chrono::steady_clock::now();
        auto cycle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            cycle_end - cycle_start
        );
        
        auto sleep_time = retention_interval_ms_ - cycle_duration.count();
        if (sleep_time < 0) sleep_time = 0;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        schedule_next_run();  // Recursive scheduling
    }
}

int RetentionService::cleanup_expired_messages() {
    try {
        auto conn = db_pool_->acquire();
        
        // Delete messages older than retention_seconds (for queues with retention enabled)
        std::string sql = R"(
DELETE FROM queen.messages 
            WHERE id IN (
                SELECT m.id 
                FROM queen.messages m
                JOIN queen.partitions p ON m.partition_id = p.id
                JOIN queen.queues q ON p.queue_id = q.id
                WHERE q.retention_enabled = true
                  AND q.retention_seconds > 0
                  AND m.created_at < NOW() - (q.retention_seconds || ' seconds')::INTERVAL
                LIMIT $1
            )
        )";
        
        sendQueryParamsAsync(conn.get(), sql, {std::to_string(retention_batch_size_)});
        auto result = getCommandResultPtr(conn.get());
        
        char* affected_str = PQcmdTuples(result.get());
        return (affected_str && *affected_str) ? std::stoi(affected_str) : 0;
        
    } catch (const std::exception& e) {
        spdlog::error("cleanup_expired_messages error: {}", e.what());
    }
    
    return 0;
}

int RetentionService::cleanup_completed_messages() {
    try {
        auto conn = db_pool_->acquire();
        
        // Delete completed messages older than completed_retention_seconds
        // Only delete messages that have been consumed (id <= last_consumed_id)
        std::string sql = R"(
            WITH messages_to_delete AS (
                SELECT DISTINCT m.id, p.id as partition_id
                FROM queen.messages m
                JOIN queen.partitions p ON m.partition_id = p.id
                JOIN queen.partition_consumers pc ON p.id = pc.partition_id
                JOIN queen.queues q ON p.queue_id = q.id
                WHERE q.retention_enabled = true
                  AND q.completed_retention_seconds > 0
                  AND m.id <= pc.last_consumed_id
                  AND m.created_at < NOW() - (q.completed_retention_seconds || ' seconds')::INTERVAL
                LIMIT $1
            ) DELETE FROM queen.messages 
            WHERE id IN (SELECT id FROM messages_to_delete)
            RETURNING (SELECT partition_id FROM messages_to_delete LIMIT 1) as partition_id
        )";
        
        sendQueryParamsAsync(conn.get(), sql, {std::to_string(retention_batch_size_)});
        auto result = getTuplesResult(conn.get());
        
        // Get affected rows count
        char* affected_str = PQcmdTuples(result.get());
        int deleted = (affected_str && *affected_str) ? std::stoi(affected_str) : 0;
            
            // Record in retention_history if messages were deleted
        if (deleted > 0 && PQntuples(result.get()) > 0) {
                try {
                std::string partition_id = PQgetvalue(result.get(), 0, PQfnumber(result.get(), "partition_id"));
                    std::string insert_history = R"(
                        INSERT INTO queen.retention_history (partition_id, messages_deleted, retention_type)
                        VALUES ($1::uuid, $2, 'completed_retention')
                    )";
                sendQueryParamsAsync(conn.get(), insert_history, {partition_id, std::to_string(deleted)});
                getCommandResult(conn.get());
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to record retention_history: {}", e.what());
                }
            }
            
            return deleted;
        
    } catch (const std::exception& e) {
        spdlog::error("cleanup_completed_messages error: {}", e.what());
    }
    
    return 0;
}

int RetentionService::cleanup_inactive_partitions() {
    try {
        auto conn = db_pool_->acquire();
        
        // First, get the partitions we're about to delete (for cache invalidation)
        std::string select_sql = R"(
            SELECT q.name as queue_name, p.name as partition_name
            FROM queen.partitions p
            JOIN queen.queues q ON p.queue_id = q.id
            WHERE p.last_activity < NOW() - ($1 || ' days')::INTERVAL
              AND p.id NOT IN (SELECT DISTINCT partition_id FROM queen.messages WHERE partition_id IS NOT NULL)
            LIMIT 1000
        )";
        
        sendQueryParamsAsync(conn.get(), select_sql, {std::to_string(partition_cleanup_days_)});
        auto select_result = getTuplesResult(conn.get());
        
        int num_to_delete = PQntuples(select_result.get());
        if (num_to_delete == 0) {
            return 0;
        }
        
        // Collect partition info for broadcasting
        std::vector<std::pair<std::string, std::string>> partitions_to_invalidate;
        for (int i = 0; i < num_to_delete; i++) {
            std::string queue_name = PQgetvalue(select_result.get(), i, 0);
            std::string partition_name = PQgetvalue(select_result.get(), i, 1);
            partitions_to_invalidate.emplace_back(queue_name, partition_name);
        }
        
        // Delete partitions that:
        // 1. Have no activity for partition_cleanup_days
        // 2. Have no messages
        std::string sql = R"(
            DELETE FROM queen.partitions
            WHERE last_activity < NOW() - ($1 || ' days')::INTERVAL
              AND id NOT IN (SELECT DISTINCT partition_id FROM queen.messages WHERE partition_id IS NOT NULL)
        )";
        
        sendQueryParamsAsync(conn.get(), sql, {std::to_string(partition_cleanup_days_)});
        auto result = getCommandResultPtr(conn.get());
        
        char* affected_str = PQcmdTuples(result.get());
        int deleted = (affected_str && *affected_str) ? std::stoi(affected_str) : 0;
        
        // Phase 6: Broadcast PARTITION_DELETED to invalidate caches on peers
        if (deleted > 0 && global_shared_state && global_shared_state->is_enabled()) {
            for (const auto& [queue_name, partition_name] : partitions_to_invalidate) {
                global_shared_state->invalidate_partition(queue_name, partition_name);
            }
            spdlog::debug("RetentionService: Broadcast PARTITION_DELETED for {} partitions", 
                         partitions_to_invalidate.size());
        }
        
        return deleted;
        
    } catch (const std::exception& e) {
        spdlog::error("cleanup_inactive_partitions error: {}", e.what());
    }
    
    return 0;
}

int RetentionService::cleanup_old_metrics() {
    try {
        auto conn = db_pool_->acquire();
        
        int total_deleted = 0;
        
        // Clean messages_consumed metrics
        std::string sql1 = R"(
            DELETE FROM queen.messages_consumed
            WHERE acked_at < NOW() - ($1 || ' days')::INTERVAL
        )";
        
        sendQueryParamsAsync(conn.get(), sql1, {std::to_string(metrics_retention_days_)});
        auto result1 = getCommandResultPtr(conn.get());
        char* affected1_str = PQcmdTuples(result1.get());
        total_deleted += (affected1_str && *affected1_str) ? std::stoi(affected1_str) : 0;
        
        // Clean system_metrics
        std::string sql2 = R"(
DELETE FROM queen.system_metrics 
            WHERE timestamp < NOW() - ($1 || ' days')::INTERVAL
        )";
        
        sendQueryParamsAsync(conn.get(), sql2, {std::to_string(metrics_retention_days_)});
        auto result2 = getCommandResultPtr(conn.get());
        char* affected2_str = PQcmdTuples(result2.get());
        total_deleted += (affected2_str && *affected2_str) ? std::stoi(affected2_str) : 0;
        
        // Clean retention_history itself (older than metrics_retention_days)
        std::string sql3 = R"(
            DELETE FROM queen.retention_history
            WHERE executed_at < NOW() - ($1 || ' days')::INTERVAL
        )";
        
        sendQueryParamsAsync(conn.get(), sql3, {std::to_string(metrics_retention_days_)});
        auto result3 = getCommandResultPtr(conn.get());
        char* affected3_str = PQcmdTuples(result3.get());
        total_deleted += (affected3_str && *affected3_str) ? std::stoi(affected3_str) : 0;
        
        return total_deleted;
        
    } catch (const std::exception& e) {
        spdlog::error("cleanup_old_metrics error: {}", e.what());
    }
    
    return 0;
}

} // namespace queen

