#include "queen/eviction_service.hpp"

namespace queen {

EvictionService::EvictionService(
    std::shared_ptr<DatabasePool> db_pool,
    std::shared_ptr<astp::ThreadPool> db_thread_pool,
    std::shared_ptr<astp::ThreadPool> system_thread_pool,
    int eviction_interval_ms,
    int eviction_batch_size
) : db_pool_(db_pool),
    db_thread_pool_(db_thread_pool),
    system_thread_pool_(system_thread_pool),
    eviction_interval_ms_(eviction_interval_ms),
    eviction_batch_size_(eviction_batch_size) {
}

EvictionService::~EvictionService() {
    stop();
}

void EvictionService::start() {
    if (running_) {
        spdlog::warn("EvictionService already running");
        return;
    }
    
    running_ = true;
    schedule_next_run();
    
    spdlog::info("EvictionService started: interval={}ms, batch_size={}",
                 eviction_interval_ms_, eviction_batch_size_);
}

void EvictionService::stop() {
    if (!running_) return;
    running_ = false;
    spdlog::info("EvictionService stopped");
}

void EvictionService::schedule_next_run() {
    if (!running_) return;
    
    // Schedule in system threadpool (same pattern as MetricsCollector)
    system_thread_pool_->push([this]() {
        this->eviction_cycle();
    });
}

void EvictionService::eviction_cycle() {
    auto cycle_start = std::chrono::steady_clock::now();
    
    try {
        // Run eviction
        int evicted = evict_expired_waiting_messages();
        
        // Only log if something was evicted
        if (evicted > 0) {
            spdlog::info("EvictionService: Evicted {} messages exceeding max_wait_time", evicted);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("EvictionService cycle error: {}", e.what());
    }
    
    // Calculate sleep time and reschedule
    if (running_) {
        auto cycle_end = std::chrono::steady_clock::now();
        auto cycle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            cycle_end - cycle_start
        );
        
        auto sleep_time = eviction_interval_ms_ - cycle_duration.count();
        if (sleep_time < 0) sleep_time = 0;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        schedule_next_run();  // Recursive scheduling
    }
}

int EvictionService::evict_expired_waiting_messages() {
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Delete messages that have been waiting longer than max_wait_time_seconds
        // This applies to messages that are pending (not yet consumed)
        std::string sql = R"(
            WITH messages_to_evict AS (
                SELECT DISTINCT m.id, p.id as partition_id
                FROM messages m
                JOIN partitions p ON m.partition_id = p.id
                JOIN queues q ON p.queue_id = q.id
                LEFT JOIN partition_consumers pc ON p.id = pc.partition_id
                WHERE q.max_wait_time_seconds > 0
                  AND m.created_at < NOW() - (q.max_wait_time_seconds || ' seconds')::INTERVAL
                  AND (pc.last_consumed_id IS NULL OR m.id > pc.last_consumed_id)
                LIMIT $1
            )
            DELETE FROM messages
            WHERE id IN (SELECT id FROM messages_to_evict)
            RETURNING (SELECT partition_id FROM messages_to_evict LIMIT 1) as partition_id
        )";
        
        auto result = QueryResult(conn->exec_params(sql, {
            std::to_string(eviction_batch_size_)
        }));
        
        if (result.is_success()) {
            std::string affected = result.affected_rows();
            int evicted = affected.empty() ? 0 : std::stoi(affected);
            
            // Record in retention_history if messages were evicted
            if (evicted > 0 && result.num_rows() > 0) {
                try {
                    std::string partition_id = result.get_value(0, "partition_id");
                    std::string insert_history = R"(
                        INSERT INTO retention_history (partition_id, messages_deleted, retention_type)
                        VALUES ($1::uuid, $2, 'max_wait_time_eviction')
                    )";
                    conn->exec_params(insert_history, {partition_id, std::to_string(evicted)});
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to record retention_history for eviction: {}", e.what());
                }
            }
            
            return evicted;
        }
        
    } catch (const std::exception& e) {
        spdlog::error("evict_expired_waiting_messages error: {}", e.what());
    }
    
    return 0;
}

} // namespace queen

