#pragma once

#include <memory>
#include <string>

namespace queen {

// Forward declarations
class AsyncQueueManager;
class AnalyticsManager;
class FileBufferManager;
class SidecarDbPool;
class PushFailoverStorage;
struct Config;

} // namespace queen

namespace astp {
class ThreadPool;
}

namespace queen {
namespace routes {

/**
 * Context object containing all dependencies needed by route handlers.
 * This is passed to each route setup function and captured by lambdas.
 */
struct RouteContext {
    // Core queue operations manager
    std::shared_ptr<AsyncQueueManager> async_queue_manager;
    
    // Analytics and dashboard queries
    std::shared_ptr<AnalyticsManager> analytics_manager;
    
    // File buffer for maintenance mode and failover
    std::shared_ptr<FileBufferManager> file_buffer;
    
    // Per-worker sidecar for async DB operations
    // Raw pointer - lifetime managed by worker thread
    SidecarDbPool* sidecar;
    
    // Configuration reference
    const Config& config;
    
    // Worker identifier for logging and routing
    int worker_id;
    
    // Database thread pool
    std::shared_ptr<astp::ThreadPool> db_thread_pool;
    
    // Storage for pending push items (for file buffer failover on sidecar failure)
    std::shared_ptr<PushFailoverStorage> push_failover_storage;
    
    RouteContext(
        std::shared_ptr<AsyncQueueManager> qm,
        std::shared_ptr<AnalyticsManager> am,
        std::shared_ptr<FileBufferManager> fb,
        SidecarDbPool* sc,
        const Config& cfg,
        int wid,
        std::shared_ptr<astp::ThreadPool> dbtp,
        std::shared_ptr<PushFailoverStorage> pfs = nullptr
    ) : async_queue_manager(std::move(qm)),
        analytics_manager(std::move(am)),
        file_buffer(std::move(fb)),
        sidecar(sc),
        config(cfg),
        worker_id(wid),
        db_thread_pool(std::move(dbtp)),
        push_failover_storage(std::move(pfs))
    {}
};

} // namespace routes
} // namespace queen
