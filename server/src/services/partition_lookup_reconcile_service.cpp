#include "queen/partition_lookup_reconcile_service.hpp"
#include <unistd.h>  // getpid()

namespace queen {

PartitionLookupReconcileService::PartitionLookupReconcileService(
    std::shared_ptr<AsyncDbPool> db_pool,
    std::shared_ptr<astp::ThreadPool> system_thread_pool,
    const std::string& hostname,
    int reconcile_interval_ms,
    int reconcile_lookback_seconds
) : db_pool_(db_pool),
    system_thread_pool_(system_thread_pool),
    reconcile_interval_ms_(reconcile_interval_ms),
    reconcile_lookback_seconds_(reconcile_lookback_seconds),
    hostname_(hostname) {
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

    spdlog::info(
        "PartitionLookupReconcileService started (stats-leader gated): "
        "interval={}ms, lookback={}s, hostname={}",
        reconcile_interval_ms_, reconcile_lookback_seconds_, hostname_);
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

bool PartitionLookupReconcileService::is_stats_leader() {
    try {
        auto conn = db_pool_->acquire();

        // Pass hostname AND pid: handles multiple instances on the same host
        // (e.g. local dev) and matches the StatsService convention.
        sendQueryParamsAsync(conn.get(),
            "SELECT queen.is_stats_leader($1, $2)",
            {hostname_, std::to_string(getpid())});
        auto result = getTuplesResult(conn.get());

        if (PQntuples(result.get()) > 0) {
            const char* val = PQgetvalue(result.get(), 0, 0);
            return val && val[0] == 't';
        }
        // Empty result is unexpected; assume leader to avoid silent
        // safety-net outages. Same fail-open posture as StatsService.
        return true;

    } catch (const std::exception& e) {
        spdlog::warn(
            "PartitionLookupReconcileService: leader check failed ({}); "
            "assuming leader to avoid safety-net gap", e.what());
        return true;
    }
}

void PartitionLookupReconcileService::reconcile_cycle() {
    auto cycle_start = std::chrono::steady_clock::now();
    int fixed_count = 0;

    try {
        if (!is_stats_leader()) {
            // Followers skip the heavy query entirely. The leader runs it.
            // No advisory lock needed: queen.is_stats_leader is the
            // single point of coordination.
            spdlog::debug(
                "PartitionLookupReconcileService: skipping cycle "
                "(not stats leader, hostname={})", hostname_);
        } else {
            auto conn = db_pool_->acquire();
            sendQueryParamsAsync(conn.get(),
                "SELECT queen.reconcile_partition_lookup_v1($1::int)",
                {std::to_string(reconcile_lookback_seconds_)});
            auto result = getTuplesResult(conn.get());

            if (PQntuples(result.get()) > 0) {
                const char* val = PQgetvalue(result.get(), 0, 0);
                if (val && *val) fixed_count = std::atoi(val);
            }

            if (fixed_count > 0) {
                spdlog::info(
                    "PartitionLookupReconcileService: fixed {} partition_lookup rows "
                    "(stats leader, hostname={})",
                    fixed_count, hostname_);
            } else {
                spdlog::debug(
                    "PartitionLookupReconcileService: no partitions needed reconciliation "
                    "(stats leader, hostname={})", hostname_);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("PartitionLookupReconcileService cycle error: {}", e.what());
    }

    if (running_) {
        auto cycle_end = std::chrono::steady_clock::now();
        auto cycle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            cycle_end - cycle_start
        );
        // Followers' cycle is just one cheap is_stats_leader() call (~ms).
        // The leader's cycle includes the heavy query. Either way we sleep
        // until the next interval boundary relative to cycle start so the
        // configured cadence is honored.
        auto sleep_time = reconcile_interval_ms_ - cycle_duration.count();
        if (sleep_time < 0) sleep_time = 0;

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        schedule_next_run();
    }
}

} // namespace queen
