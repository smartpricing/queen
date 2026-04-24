#pragma once

#include "queen/async_database.hpp"
#include "threadpool.hpp"
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>

namespace queen {

/**
 * PartitionLookupReconcileService — safety-net reconciler for queen.partition_lookup
 * (PUSHPOPLOOKUPSOL).
 *
 * Background
 * ----------
 * The partition_lookup trigger has been removed. partition_lookup is
 * maintained primarily by queen.update_partition_lookup_v1(), invoked by
 * libqueen after each successful push transaction (see lib/queen.hpp).
 *
 * This service is the safety net for the microsecond window between
 * push commit and the follow-up libqueen-driven update_partition_lookup_v1
 * call, plus any transient failures of that call. Every cycle it runs
 * SELECT queen.reconcile_partition_lookup_v1(p_lookback_seconds) which
 * scans messages created in the recent window and advances any
 * partition_lookup rows that fell behind.
 *
 * Uses a transaction-level advisory lock so only one server instance
 * runs the reconciler at a time when multiple are connected to the
 * same PG.
 */
class PartitionLookupReconcileService {
private:
    std::shared_ptr<AsyncDbPool> db_pool_;
    std::shared_ptr<astp::ThreadPool> system_thread_pool_;

    std::atomic<bool> running_{false};

    int reconcile_interval_ms_;       // How often to run (default: 5000ms).
    int reconcile_lookback_seconds_;  // How far back to scan messages (default: 60s).

public:
    PartitionLookupReconcileService(
        std::shared_ptr<AsyncDbPool> db_pool,
        std::shared_ptr<astp::ThreadPool> system_thread_pool,
        int reconcile_interval_ms,
        int reconcile_lookback_seconds
    );

    ~PartitionLookupReconcileService();

    void start();
    void stop();

private:
    void schedule_next_run();
    void reconcile_cycle();
};

} // namespace queen
