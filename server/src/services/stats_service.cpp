#include "queen/stats_service.hpp"

namespace queen {

StatsService::StatsService(
    std::shared_ptr<AsyncDbPool> db_pool,
    std::shared_ptr<astp::ThreadPool> db_thread_pool,
    std::shared_ptr<astp::ThreadPool> system_thread_pool,
    int stats_interval_ms,
    int reconcile_interval_ms,
    int history_bucket_minutes,
    int history_retention_days
) : db_pool_(db_pool),
    db_thread_pool_(db_thread_pool),
    system_thread_pool_(system_thread_pool),
    stats_interval_ms_(stats_interval_ms),
    reconcile_interval_ms_(reconcile_interval_ms),
    history_bucket_minutes_(history_bucket_minutes),
    history_retention_days_(history_retention_days) {
}

StatsService::~StatsService() {
    stop();
}

void StatsService::start() {
    if (running_) {
        spdlog::warn("StatsService already running");
        return;
    }
    
    running_ = true;
    last_reconcile_ = std::chrono::steady_clock::now();
    cycle_count_ = 0;
    
    // Run initial full refresh
    spdlog::info("StatsService: Running initial full stats refresh...");
    run_full_reconciliation();
    
    schedule_next_run();
    
    spdlog::info("StatsService started: stats_interval={}ms, reconcile_interval={}ms, "
                 "history_bucket={}min, history_retention={}days",
                 stats_interval_ms_, reconcile_interval_ms_,
                 history_bucket_minutes_, history_retention_days_);
}

void StatsService::stop() {
    if (!running_) return;
    running_ = false;
    spdlog::info("StatsService stopped");
}

void StatsService::schedule_next_run() {
    if (!running_) return;
    
    system_thread_pool_->push([this]() {
        this->stats_cycle();
    });
}

void StatsService::stats_cycle() {
    auto cycle_start = std::chrono::steady_clock::now();
    
    try {
        cycle_count_++;
        
        // Check if we need full reconciliation
        auto now = std::chrono::steady_clock::now();
        auto since_reconcile = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_reconcile_
        ).count();
        
        if (since_reconcile >= reconcile_interval_ms_) {
            // Full reconciliation: recompute partition stats from messages
            run_full_reconciliation();
            last_reconcile_ = now;
        } else {
            // Fast aggregation: only roll up existing stats
            run_fast_aggregation();
        }
        
        // Write history snapshot
        write_history_snapshot();
        
        // Cleanup old history periodically (every 100 cycles)
        if (cycle_count_ % 100 == 0) {
            cleanup_old_history();
            cleanup_orphaned_stats();
        }
        
    } catch (const std::exception& e) {
        spdlog::error("StatsService cycle error: {}", e.what());
    }
    
    // Calculate sleep time and reschedule
    if (running_) {
        auto cycle_end = std::chrono::steady_clock::now();
        auto cycle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            cycle_end - cycle_start
        );
        
        auto sleep_time = stats_interval_ms_ - cycle_duration.count();
        if (sleep_time < 0) sleep_time = 0;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        schedule_next_run();
    }
}

void StatsService::run_full_reconciliation() {
    try {
        auto conn = db_pool_->acquire();
        
        // Call the full refresh stored procedure
        std::string sql = "SELECT queen.refresh_all_stats_v1()";
        
        sendQueryParamsAsync(conn.get(), sql, {});
        auto result = getTuplesResult(conn.get());
        
        if (PQntuples(result.get()) > 0) {
            const char* json_result = PQgetvalue(result.get(), 0, 0);
            spdlog::debug("StatsService: Full reconciliation complete: {}", json_result);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("StatsService run_full_reconciliation error: {}", e.what());
    }
}

void StatsService::run_fast_aggregation() {
    try {
        auto conn = db_pool_->acquire();
        
        // Step 1: Aggregate queue stats from partition stats
        sendQueryParamsAsync(conn.get(), "SELECT queen.aggregate_queue_stats_v1()", {});
        getTuplesResult(conn.get());
        
        // Step 2: Aggregate namespace stats
        sendQueryParamsAsync(conn.get(), "SELECT queen.aggregate_namespace_stats_v1()", {});
        getTuplesResult(conn.get());
        
        // Step 3: Aggregate task stats
        sendQueryParamsAsync(conn.get(), "SELECT queen.aggregate_task_stats_v1()", {});
        getTuplesResult(conn.get());
        
        // Step 4: Aggregate system stats
        sendQueryParamsAsync(conn.get(), "SELECT queen.aggregate_system_stats_v1()", {});
        getTuplesResult(conn.get());
        
        spdlog::trace("StatsService: Fast aggregation complete");
        
    } catch (const std::exception& e) {
        spdlog::error("StatsService run_fast_aggregation error: {}", e.what());
    }
}

void StatsService::write_history_snapshot() {
    try {
        auto conn = db_pool_->acquire();
        
        std::string sql = "SELECT queen.write_stats_history_v1($1::integer)";
        sendQueryParamsAsync(conn.get(), sql, {std::to_string(history_bucket_minutes_)});
        auto result = getTuplesResult(conn.get());
        
        // Log only if something was written (avoid spam)
        if (PQntuples(result.get()) > 0) {
            const char* json_result = PQgetvalue(result.get(), 0, 0);
            spdlog::trace("StatsService: History snapshot: {}", json_result);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("StatsService write_history_snapshot error: {}", e.what());
    }
}

void StatsService::cleanup_old_history() {
    try {
        auto conn = db_pool_->acquire();
        
        std::string sql = "SELECT queen.cleanup_stats_history_v1($1::integer)";
        sendQueryParamsAsync(conn.get(), sql, {std::to_string(history_retention_days_)});
        auto result = getTuplesResult(conn.get());
        
        if (PQntuples(result.get()) > 0) {
            const char* json_result = PQgetvalue(result.get(), 0, 0);
            spdlog::debug("StatsService: History cleanup: {}", json_result);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("StatsService cleanup_old_history error: {}", e.what());
    }
}

void StatsService::cleanup_orphaned_stats() {
    try {
        auto conn = db_pool_->acquire();
        
        sendQueryParamsAsync(conn.get(), "SELECT queen.cleanup_orphaned_stats_v1()", {});
        auto result = getTuplesResult(conn.get());
        
        if (PQntuples(result.get()) > 0) {
            const char* json_result = PQgetvalue(result.get(), 0, 0);
            spdlog::debug("StatsService: Orphaned stats cleanup: {}", json_result);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("StatsService cleanup_orphaned_stats error: {}", e.what());
    }
}

void StatsService::trigger_full_refresh() {
    spdlog::info("StatsService: Manual full refresh triggered");
    run_full_reconciliation();
}

void StatsService::trigger_aggregation() {
    spdlog::info("StatsService: Manual aggregation triggered");
    run_fast_aggregation();
}

} // namespace queen

