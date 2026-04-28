#pragma once

#include "queen/async_database.hpp"
#include "threadpool.hpp"
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <string>

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
 * Concurrency model: stats-leader election
 * ----------------------------------------
 * The service runs on every replica but only the elected stats leader
 * (queen.is_stats_leader(hostname, pid)) actually executes the reconcile
 * query. Followers wake up, check leadership, and skip the heavy work.
 *
 * This replaced an earlier transaction-scoped pg_try_advisory_xact_lock
 * approach that, while preventing simultaneous execution, did NOT rate-limit
 * across a time window: with N replicas in different phase offsets the
 * reconcile query effectively ran every interval/N seconds because the lock
 * was only held for the ~1.3s the query took, leaving ~3.7s of every cycle
 * for another replica to grab it. At ~5k partitions that drove the database
 * CPU cost of the query family to ~half a core continuously. Leader election
 * keeps the configured cadence honest regardless of replica count.
 *
 * Failover: a dead leader stays "current" until its queen.worker_metrics
 * row ages out of the 2-minute staleness window in queen.is_stats_leader,
 * so worst-case repair latency for a stuck partition_lookup row goes from
 * the previous ~5s to ~2 minutes. Acceptable because the reconciler is a
 * safety net — partition_lookup also self-heals on the next push to the
 * affected partition via the libqueen post-commit hook.
 */
class PartitionLookupReconcileService {
private:
    std::shared_ptr<AsyncDbPool> db_pool_;
    std::shared_ptr<astp::ThreadPool> system_thread_pool_;

    std::atomic<bool> running_{false};

    int reconcile_interval_ms_;       // How often to run (default: 5000ms).
    int reconcile_lookback_seconds_;  // How far back to scan messages (default: 60s).
    std::string hostname_;            // For stats-leader election (queen.is_stats_leader).

public:
    PartitionLookupReconcileService(
        std::shared_ptr<AsyncDbPool> db_pool,
        std::shared_ptr<astp::ThreadPool> system_thread_pool,
        const std::string& hostname,
        int reconcile_interval_ms,
        int reconcile_lookback_seconds
    );

    ~PartitionLookupReconcileService();

    void start();
    void stop();

private:
    void schedule_next_run();
    void reconcile_cycle();

    // Leader election — only one server runs reconcile in multi-instance
    // deployments. Shared with StatsService via queen.is_stats_leader.
    bool is_stats_leader();
};

} // namespace queen
