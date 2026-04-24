#include "queen/partition_lookup_reconcile_service.hpp"

namespace queen {

PartitionLookupReconcileService::PartitionLookupReconcileService(
    std::shared_ptr<AsyncDbPool> db_pool,
    std::shared_ptr<astp::ThreadPool> system_thread_pool,
    int reconcile_interval_ms,
    int reconcile_lookback_seconds
) : db_pool_(db_pool),
    system_thread_pool_(system_thread_pool),
    reconcile_interval_ms_(reconcile_interval_ms),
    reconcile_lookback_seconds_(reconcile_lookback_seconds) {
}

PartitionLookupReconcileService::~PartitionLookupReconcileService() {
    stop();
}

void PartitionLookupReconcileService::start() {
    if (running_) {
        spdlog::warn("PartitionLookupReconcileService already running");
        return;
    }

    running_ = true;
    schedule_next_run();

    spdlog::info("PartitionLookupReconcileService started: interval={}ms, lookback={}s",
                 reconcile_interval_ms_, reconcile_lookback_seconds_);
}

void PartitionLookupReconcileService::stop() {
    if (!running_) return;
    running_ = false;
    spdlog::info("PartitionLookupReconcileService stopped");
}

void PartitionLookupReconcileService::schedule_next_run() {
    if (!running_) return;

    system_thread_pool_->push([this]() {
        this->reconcile_cycle();
    });
}

void PartitionLookupReconcileService::reconcile_cycle() {
    auto cycle_start = std::chrono::steady_clock::now();

    // Advisory lock id distinct from RetentionService/EvictionService (737001).
    // Prevents multiple server instances from running the reconciler
    // concurrently. Transaction-level lock is PgBouncer-compatible.
    constexpr int64_t RECONCILE_LOCK_ID = 737002;

    AsyncDbPool::PooledConnection lock_conn;
    bool has_lock = false;
    bool in_transaction = false;
    int fixed_count = 0;

    try {
        lock_conn = db_pool_->acquire();

        // Begin transaction to hold the xact-level advisory lock.
        sendQueryParamsAsync(lock_conn.get(), "BEGIN", {});
        getCommandResult(lock_conn.get());
        in_transaction = true;

        sendQueryParamsAsync(lock_conn.get(),
            "SELECT pg_try_advisory_xact_lock($1::bigint)",
            {std::to_string(RECONCILE_LOCK_ID)});
        auto lock_result = getTuplesResult(lock_conn.get());

        if (PQntuples(lock_result.get()) > 0) {
            std::string result_str = PQgetvalue(lock_result.get(), 0, 0);
            has_lock = (result_str == "t");
        }

        if (!has_lock) {
            spdlog::debug("PartitionLookupReconcileService: skipping cycle, another instance holds the lock");
        } else {
            sendQueryParamsAsync(lock_conn.get(),
                "SELECT queen.reconcile_partition_lookup_v1($1::int)",
                {std::to_string(reconcile_lookback_seconds_)});
            auto result = getTuplesResult(lock_conn.get());

            if (PQntuples(result.get()) > 0) {
                const char* val = PQgetvalue(result.get(), 0, 0);
                if (val && *val) fixed_count = std::atoi(val);
            }

            if (fixed_count > 0) {
                spdlog::info("PartitionLookupReconcileService: fixed {} partition_lookup rows",
                             fixed_count);
            } else {
                spdlog::debug("PartitionLookupReconcileService: no partitions needed reconciliation");
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("PartitionLookupReconcileService cycle error: {}", e.what());
    }

    // COMMIT releases the advisory lock.
    if (in_transaction && lock_conn) {
        try {
            sendQueryParamsAsync(lock_conn.get(), "COMMIT", {});
            getCommandResult(lock_conn.get());
        } catch (const std::exception& e) {
            spdlog::debug("PartitionLookupReconcileService: failed to commit lock transaction: {}",
                          e.what());
        }
    }

    if (running_) {
        auto cycle_end = std::chrono::steady_clock::now();
        auto cycle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            cycle_end - cycle_start
        );
        auto sleep_time = reconcile_interval_ms_ - cycle_duration.count();
        if (sleep_time < 0) sleep_time = 0;

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        schedule_next_run();
    }
}

} // namespace queen
