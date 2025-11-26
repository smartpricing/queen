#include "queen/async_database.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/analytics_manager.hpp"
#include "queen/config.hpp"
#include "queen/encryption.hpp"
#include "queen/file_buffer.hpp"
#include "queen/response_queue.hpp"
#include "queen/metrics_collector.hpp"
#include "queen/retention_service.hpp"
#include "queen/eviction_service.hpp"
#include "queen/poll_intention_registry.hpp"
#include "queen/poll_worker.hpp"
#include "queen/stream_poll_worker.hpp"
#include "queen/stream_poll_intention_registry.hpp"
#include "queen/stream_manager.hpp"
#include "queen/inter_instance_comms.hpp"
#include "threadpool.hpp"
#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include <App.h>
#include <json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <optional>
#include <memory>
#include <unistd.h>

// Global shared resources for all workers (declared early for use in handlers)
static std::shared_ptr<astp::ThreadPool> global_db_thread_pool;
static std::shared_ptr<astp::ThreadPool> global_system_thread_pool;
// Removed: global_db_pool - Now using only AsyncDbPool
static std::shared_ptr<queen::AsyncDbPool> global_async_db_pool;  // Async DB pool for non-blocking push
static std::vector<std::shared_ptr<queen::ResponseQueue>> worker_response_queues;  // Per-worker queues
static std::shared_ptr<queen::MetricsCollector> global_metrics_collector;
static std::shared_ptr<queen::RetentionService> global_retention_service;
static std::shared_ptr<queen::EvictionService> global_eviction_service;

// These globals need to be accessible from route files (non-static, in queen namespace)
namespace queen {
std::shared_ptr<ResponseRegistry> global_response_registry;
std::shared_ptr<PollIntentionRegistry> global_poll_intention_registry;
std::shared_ptr<StreamPollIntentionRegistry> global_stream_poll_registry;
std::shared_ptr<StreamManager> global_stream_manager;
}

static std::once_flag global_pool_init_flag;
static int num_workers_global = 0;  // Track number of workers for queue array sizing

// System information for metrics
struct SystemInfo {
    std::string hostname;
    int port;
    
    static SystemInfo get_current() {
        SystemInfo info;
        
        // Get hostname
        char hostname_buf[256];
        if (gethostname(hostname_buf, sizeof(hostname_buf)) == 0) {
            info.hostname = hostname_buf;
        } else {
            info.hostname = "unknown";
        }
        
        // Port will be set from config
        info.port = 0;
        
        return info;
    }
};

static SystemInfo global_system_info;

namespace queen {

} // namespace queen

#include <json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <regex>
#include <set>

namespace queen {

// Global shutdown flag
std::atomic<bool> g_shutdown{false};

// Setup routes for a worker app using route registry
static void setup_worker_routes(uWS::App* app, 
                                std::shared_ptr<queen::AsyncQueueManager> async_queue_manager,
                                std::shared_ptr<AnalyticsManager> analytics_manager,
                                std::shared_ptr<FileBufferManager> file_buffer,
                                const Config& config,
                                int worker_id,
                                std::shared_ptr<astp::ThreadPool> db_thread_pool) {
    
    // Create route context with all dependencies
    queen::routes::RouteContext ctx(
        async_queue_manager,
        analytics_manager,
        file_buffer,
        config,
        worker_id,
        db_thread_pool
    );
    
    // Setup all routes in organized categories
    spdlog::debug("[Worker {}] Setting up CORS routes...", worker_id);
    queen::routes::setup_cors_routes(app);
    
    spdlog::debug("[Worker {}] Setting up health routes...", worker_id);
    queen::routes::setup_health_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up maintenance routes...", worker_id);
    queen::routes::setup_maintenance_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up configure routes...", worker_id);
    queen::routes::setup_configure_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up push routes...", worker_id);
    queen::routes::setup_push_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up pop routes...", worker_id);
    queen::routes::setup_pop_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up ack routes...", worker_id);
    queen::routes::setup_ack_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up transaction routes...", worker_id);
    queen::routes::setup_transaction_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up lease routes...", worker_id);
    queen::routes::setup_lease_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up metrics routes...", worker_id);
    queen::routes::setup_metrics_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up resource routes...", worker_id);
    queen::routes::setup_resource_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up message routes...", worker_id);
    queen::routes::setup_message_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up DLQ routes...", worker_id);
    queen::routes::setup_dlq_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up trace routes...", worker_id);
    queen::routes::setup_trace_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up stream routes...", worker_id);
    queen::routes::setup_stream_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up status routes...", worker_id);
    queen::routes::setup_status_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up consumer group routes...", worker_id);
    queen::routes::setup_consumer_group_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up internal routes (peer notification)...", worker_id);
    queen::routes::setup_internal_routes(app, ctx);
    
    spdlog::debug("[Worker {}] Setting up static file routes...", worker_id);
    queen::routes::setup_static_file_routes(app, ctx);
}

// NOTE: All helper functions (CORS, JSON, query params, static files) have been moved to
// src/routes/route_helpers.cpp and declared in include/queen/routes/route_helpers.hpp

// Global shared resources initialized in worker_thread

// Context struct for response timer callback
struct ResponseTimerContext {
    queen::ResponseQueue* queue;
    int batch_size;
    int batch_max;
};

// Timer callback for processing response queue
static void response_timer_callback(us_timer_t* timer) {
    auto* ctx = (ResponseTimerContext*)us_timer_ext(timer);
    
    queen::ResponseQueue::ResponseItem item;
    int processed = 0;
    
    // Adaptive batch sizing based on queue backlog
    size_t queue_size = ctx->queue->size();
    int batch_limit = ctx->batch_size;
    
    // Scale up batch size if there's backlog (2x for every 100 items backlog)
    if (queue_size > static_cast<size_t>(ctx->batch_size)) {
        int backlog_multiplier = 1 + (static_cast<int>(queue_size) / 100);
        batch_limit = std::min(ctx->batch_size * backlog_multiplier, ctx->batch_max);
        
        if (queue_size > static_cast<size_t>(ctx->batch_size * 2)) {
            spdlog::warn("Response queue backlog detected: {} items, processing {} this tick", 
                        queue_size, batch_limit);
        }
    }
    
    // Process up to batch_limit responses per timer tick to avoid blocking event loop
    while (processed < batch_limit && ctx->queue->pop(item)) {
        bool sent = queen::global_response_registry->send_response(
            item.request_id, item.data, item.is_error, item.status_code);
        
        if (!sent) {
            spdlog::debug("Response {} was aborted or expired", item.request_id);
        }
        processed++;
    }
    
    // Log if we're still behind after processing
    if (processed == batch_limit) {
        size_t remaining = ctx->queue->size();
        if (remaining > 0) {
            spdlog::debug("Processed {} responses, {} still in queue", processed, remaining);
        }
    }
    
    // Cleanup expired responses every 200 timer ticks (~5 seconds at 25ms intervals)
    // Use 120s timeout to allow for long-polling requests up to 60s + buffer
    static int cleanup_counter = 0;
    if (++cleanup_counter >= 200) {
        queen::global_response_registry->cleanup_expired(std::chrono::seconds(120));
        cleanup_counter = 0;
    }
}

// Worker thread function
static void worker_thread(const Config& config, int worker_id, int num_workers,
                         std::mutex& init_mutex,
                         std::vector<uWS::App*>& worker_apps,
                         std::shared_ptr<FileBufferManager>& shared_file_buffer,
                         std::mutex& file_buffer_mutex,
                         std::condition_variable& file_buffer_ready) {
    spdlog::info("[Worker {}] Starting...", worker_id);
    
    try {
        // Initialize global shared resources (only once)
        std::call_once(global_pool_init_flag, [&config, num_workers]() {
            // Calculate global pool sizes with 5% safety buffer
            int total_connections = static_cast<int>(config.database.pool_size * 0.95); // 95% of total
            
            // ALL connections go to async pool (100% utilization)
            // No more DatabasePool - everything uses AsyncDbPool
            
            // Thread pool sizing calculations:
            // ALL workers use ThreadPool for proper resource management and visibility
            // - Regular poll workers: reserve threads in db_thread_pool
            // - Stream poll workers: reserve threads + submit concurrent window checks
            // - Background services: use system_thread_pool for scheduling
            
            // DB ThreadPool Formula:
            // = Regular poll workers (reserved)
            // + Stream poll workers (reserved) 
            // + Stream concurrent checks (N × C)
            // + Service threads (metrics, etc.)
            int poll_workers = config.queue.poll_worker_count;
            int stream_workers = config.queue.stream_poll_worker_count;
            int stream_concurrent = config.queue.stream_poll_worker_count * config.queue.stream_concurrent_checks;
            int service_threads = config.queue.db_thread_pool_service_threads;
            int db_thread_pool_size = poll_workers + stream_workers + stream_concurrent + service_threads;
            
            // System ThreadPool: Used by background services (metrics sampling, retention, eviction)
            int system_threads = 4; // MetricsCollector, RetentionService, EvictionService scheduling
            
            spdlog::info("Initializing GLOBAL shared resources (ASYNC-ONLY MODE):");
            spdlog::info("  - Total DB connections: {} (95% of {}) - ALL ASYNC", total_connections, config.database.pool_size);
            spdlog::info("  - DB ThreadPool size: {} = {} poll + {} stream + {} stream concurrent + {} service", 
                        db_thread_pool_size, poll_workers, stream_workers, stream_concurrent, service_threads);
            spdlog::info("    * Regular poll workers: {} (reserved threads for long-polling)", poll_workers);
            spdlog::info("    * Stream poll workers: {} (reserved threads)", stream_workers);
            spdlog::info("    * Stream concurrent checks: {} ({} per worker)", stream_concurrent, config.queue.stream_concurrent_checks);
            spdlog::info("    * Service DB threads: {} (metrics, retention, eviction)", service_threads);
            spdlog::info("  - System ThreadPool threads: {} (for background service scheduling)", system_threads);
            spdlog::info("  - Number of HTTP workers: {}", num_workers);
            
            // Create ONLY async connection pool (for ALL operations including analytics)
            spdlog::info("  - Creating Async DB Pool ({} connections) for ALL operations", total_connections);
            global_async_db_pool = std::make_shared<queen::AsyncDbPool>(
                config.database.connection_string(),
                total_connections,  // Use ALL connections
                config.database.statement_timeout,
                config.database.lock_timeout,
                config.database.idle_timeout,
                config.database.schema
            );
            
            // Create global DB operations ThreadPool (for stream workers + services)
            global_db_thread_pool = std::make_shared<astp::ThreadPool>(db_thread_pool_size);
            
            // Create global System operations ThreadPool (for metrics, cleanup, etc.)
            global_system_thread_pool = std::make_shared<astp::ThreadPool>(system_threads);
            
            // Create per-worker response queues
            num_workers_global = num_workers;
            worker_response_queues.resize(num_workers);
            for (int i = 0; i < num_workers; i++) {
                worker_response_queues[i] = std::make_shared<queen::ResponseQueue>();
                spdlog::info("  - Created response queue for worker {}", i);
            }
            
            // Create global response registry (shared across workers)
            queen::global_response_registry = std::make_shared<queen::ResponseRegistry>();
            
            // Create global poll intention registry (shared by all workers)
            queen::global_poll_intention_registry = std::make_shared<queen::PollIntentionRegistry>();
            
            // Set base poll interval for backoff tracking (for peer notification support)
            queen::global_poll_intention_registry->set_base_poll_interval(config.queue.poll_db_interval);
            
            // Create global stream poll registry (shared by all workers)
            queen::global_stream_poll_registry = std::make_shared<queen::StreamPollIntentionRegistry>();
            
            // Create global stream manager (shared by all workers)
            queen::global_stream_manager = std::make_shared<queen::StreamManager>(
                global_async_db_pool,
                global_db_thread_pool,
                worker_response_queues,
                queen::global_stream_poll_registry,
                queen::global_response_registry
            );
            
            // Get system info for metrics
            global_system_info = SystemInfo::get_current();
            global_system_info.port = config.server.port;
            
            // Initialize Inter-Instance Communication (peer notification)
            // Always create the instance - it will only broadcast if peers are configured
            queen::global_inter_instance_comms = std::make_shared<queen::InterInstanceComms>(
                config.inter_instance,
                queen::global_poll_intention_registry,
                config.server.host,
                config.server.port,
                global_system_info.hostname  // For K8s self-detection
            );
            
            if (config.inter_instance.has_peers()) {
                spdlog::info("Inter-Instance Peer Notifications:");
                spdlog::info("  - Peers: {}", config.inter_instance.peers);
                spdlog::info("  - Batch interval: {}ms", config.inter_instance.batch_ms);
                queen::global_inter_instance_comms->start();
                spdlog::info("Peer notifications started with {} peer(s)", 
                            config.inter_instance.parse_peer_urls().size());
            } else {
                spdlog::info("Peer notifications: Disabled (single server mode, set QUEEN_PEERS to enable)");
            }
            
            spdlog::info("System info: hostname={}, port={}", 
                         global_system_info.hostname, 
                         global_system_info.port);
            spdlog::info("Global shared resources initialized successfully");
        });
        
        // Use global shared resources
        auto async_db_pool = global_async_db_pool;
        auto db_thread_pool = global_db_thread_pool;
        
        // Thread-local ASYNC queue manager for ALL operations
        auto async_queue_manager = std::make_shared<queen::AsyncQueueManager>(
            async_db_pool, config.queue, config.database.schema
        );
        
        // Thread-local analytics manager (uses async pool for dashboard queries)
        auto analytics_manager = std::make_shared<AnalyticsManager>(async_db_pool);
        
        spdlog::info("[Worker {}] Using GLOBAL shared ThreadPool and Async Database Pool", worker_id);
        
        // Test database connection and log pool stats
        // FAILOVER: Don't fail at startup if DB is down - use file buffer instead
        bool db_available = async_queue_manager->health_check();
        
        auto pool_stats = async_queue_manager->get_pool_stats();
        
        if (db_available) {
            spdlog::info("[Worker {}] Database connection: OK | Pool: {}/{} conn available", 
                         worker_id, pool_stats.available, pool_stats.total);
            
            // Only first worker initializes schema (only if DB is available)
            if (worker_id == 0) {
                spdlog::info("[Worker 0] Initializing database schema...");
                async_queue_manager->initialize_schema();
                
                // Start metrics collector
                spdlog::info("[Worker 0] Starting background metrics collector (sample: {}ms, aggregate: {}s)...",
                            config.jobs.metrics_sample_interval_ms, config.jobs.metrics_aggregate_interval_s);
                global_metrics_collector = std::make_shared<queen::MetricsCollector>(
                    global_async_db_pool,
                    global_db_thread_pool,
                    global_system_thread_pool,
                    queen::global_poll_intention_registry,
                    queen::global_stream_poll_registry,
                    queen::global_response_registry,
                    global_system_info.hostname,
                    global_system_info.port,
                    config.server.worker_id,
                    config.jobs.metrics_sample_interval_ms,
                    config.jobs.metrics_aggregate_interval_s
                );
                global_metrics_collector->start();
                
                // Start retention service (cleanup old messages, partitions, metrics)
                spdlog::info("[Worker 0] Starting background retention service...");
                global_retention_service = std::make_shared<queen::RetentionService>(
                    global_async_db_pool,
                    global_db_thread_pool,
                    global_system_thread_pool,
                    config.jobs.retention_interval,
                    config.jobs.retention_batch_size,
                    config.jobs.partition_cleanup_days,
                    config.jobs.metrics_retention_days
                );
                global_retention_service->start();
                
                // Start eviction service (evict messages exceeding max_wait_time)
                spdlog::info("[Worker 0] Starting background eviction service...");
                global_eviction_service = std::make_shared<queen::EvictionService>(
                    global_async_db_pool,
                    global_db_thread_pool,
                    global_system_thread_pool,
                    config.jobs.eviction_interval,
                    config.jobs.eviction_batch_size
                );
                global_eviction_service->start();
            }
        } else {
            spdlog::warn("[Worker {}] Database connection: UNAVAILABLE (Pool: 0/{}) - Will use file buffer for failover", 
                         worker_id, pool_stats.total);
            spdlog::warn("[Worker {}] Server will operate with file buffer until PostgreSQL becomes available", worker_id);
        }
        
        // Initialize long-polling poll workers (Worker 0 only, regardless of DB status)
        if (worker_id == 0) {
            spdlog::info("[Worker 0] Starting long-polling poll workers...");
            queen::init_long_polling(
                global_db_thread_pool,
                queen::global_poll_intention_registry,
                async_queue_manager,
                worker_response_queues,  // All worker queues (poll workers will route to correct one)
                config.queue.poll_worker_count,  // Number of poll workers (configurable via POLL_WORKER_COUNT env var)
                config.queue.poll_worker_interval,
                config.queue.poll_db_interval,
                config.queue.backoff_threshold,
                config.queue.backoff_multiplier,
                config.queue.max_poll_interval,
                config.queue.backoff_cleanup_inactive_threshold
            );
            
            // Initialize stream long-polling poll workers
            spdlog::info("[Worker 0] Starting stream long-polling poll workers...");
            queen::init_stream_long_polling(
                global_db_thread_pool,
                queen::global_stream_poll_registry,
                queen::global_stream_manager,
                worker_response_queues,
                config.queue.stream_poll_worker_count,
                config.queue.stream_poll_worker_interval,
                config.queue.stream_poll_interval,
                config.queue.stream_backoff_threshold,
                config.queue.stream_backoff_multiplier,
                config.queue.stream_max_poll_interval,
                config.queue.backoff_cleanup_inactive_threshold
            );
        }
        
        // Create or wait for SHARED FileBufferManager
        // Worker 0 creates it, other workers wait for it to be ready
        std::shared_ptr<FileBufferManager> file_buffer;
        
        if (worker_id == 0) {
            spdlog::info("[Worker 0] Creating SHARED file buffer manager (dir={})...", 
                         config.file_buffer.buffer_dir);
            
            auto new_file_buffer = std::make_shared<FileBufferManager>(
                async_queue_manager,
                config.file_buffer.buffer_dir,
                config.file_buffer.flush_interval_ms,
                config.file_buffer.max_batch_size,
                config.file_buffer.max_events_per_file,
                true  // Do startup recovery
            );
            
            // Wait for recovery to complete
            while (!new_file_buffer->is_ready()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            spdlog::info("[Worker 0] File buffer ready - Pending: {}, Failed: {}, DB: {}", 
                         new_file_buffer->get_pending_count(),
                         new_file_buffer->get_failed_count(),
                         new_file_buffer->is_db_healthy() ? "healthy" : "down");
            
            // Share with other workers
            spdlog::info("[Worker 0] Sharing file buffer with other workers...");
            {
                std::lock_guard<std::mutex> lock(file_buffer_mutex);
                shared_file_buffer = new_file_buffer;
                file_buffer = new_file_buffer;
            }
            file_buffer_ready.notify_all();
            spdlog::info("[Worker 0] File buffer shared successfully");
            
        } else {
            // Wait for Worker 0 to create the shared file buffer
            spdlog::info("[Worker {}] Waiting for shared file buffer from Worker 0...", worker_id);
            {
                std::unique_lock<std::mutex> lock(file_buffer_mutex);
                file_buffer_ready.wait(lock, [&shared_file_buffer]() { 
                    return shared_file_buffer != nullptr; 
                });
                file_buffer = shared_file_buffer;
            }
            spdlog::info("[Worker {}] Using shared file buffer from Worker 0", worker_id);
        }
        
        // Connect file buffer to async queue manager for maintenance mode
        async_queue_manager->set_file_buffer_manager(file_buffer);
        spdlog::info("[Worker {}] File buffer connected to async queue manager for maintenance mode", worker_id);
        
        // Create worker App
        spdlog::info("[Worker {}] Creating uWS::App...", worker_id);
        auto worker_app = new uWS::App();
        
        // Setup routes
        spdlog::info("[Worker {}] Setting up routes...", worker_id);
        setup_worker_routes(worker_app, async_queue_manager, analytics_manager, file_buffer, config, worker_id, db_thread_pool);
        spdlog::info("[Worker {}] Routes configured", worker_id);
        
        // Register this worker app with the acceptor (thread-safe)
        {
            std::lock_guard<std::mutex> lock(init_mutex);
            worker_apps.push_back(worker_app);
            spdlog::info("[Worker {}] Registered with acceptor", worker_id);
        }
        
        // IMPORTANT: Workers do NOT listen on the main port in acceptor/worker pattern!
        // Only the acceptor listens on the main port (6632).
        // Workers listen on a dummy localhost-only port just to keep event loops alive.
        // They will receive actual sockets via adoptSocket() from the acceptor.
        int dummy_port = 50000 + worker_id;  // Each worker gets unique dummy port
        worker_app->listen("127.0.0.1", dummy_port, [worker_id, dummy_port](auto* listen_socket) {
            if (listen_socket) {
                spdlog::debug("[Worker {}] Listening on dummy port 127.0.0.1:{} (keeps event loop alive)", 
                             worker_id, dummy_port);
            }
        });
        
        spdlog::info("[Worker {}] Event loop ready to receive adopted sockets from acceptor", worker_id);
        
        // Setup response timer for this worker (each worker has its own timer and queue)
        // CRITICAL: Timer must be created AFTER listen() to ensure event loop is properly initialized
        spdlog::info("[Worker {}] Setting up response timer...", worker_id);
        us_timer_t* response_timer = us_create_timer((us_loop_t*)uWS::Loop::get(), 0, sizeof(ResponseTimerContext));
        
        // Initialize timer context with queue and config
        auto* timer_ctx = (ResponseTimerContext*)us_timer_ext(response_timer);
        timer_ctx->queue = worker_response_queues[worker_id].get();
        timer_ctx->batch_size = config.queue.response_batch_size;
        timer_ctx->batch_max = config.queue.response_batch_max;
        
        // Poll using configured interval for good balance between latency and CPU usage
        int timer_interval = config.queue.response_timer_interval_ms;
        us_timer_set(response_timer, response_timer_callback, timer_interval, timer_interval);
        spdlog::info("[Worker {}] Response timer configured: {}ms interval, batch size {}-{}", 
                    worker_id, timer_interval, timer_ctx->batch_size, timer_ctx->batch_max);
        
        // Run worker event loop (blocks forever)
        // Will receive sockets adopted from the acceptor
        worker_app->run();
        
        spdlog::info("[Worker {}] Event loop exited", worker_id);
        
    } catch (const std::exception& e) {
        spdlog::error("[Worker {}] FATAL: {}", worker_id, e.what());
    }
}

// Main server start function using acceptor/worker pattern
bool start_acceptor_server(const Config& config) {
    // Initialize encryption globally
    bool encryption_enabled = init_encryption();
    spdlog::info("Encryption: {}", encryption_enabled ? "enabled" : "disabled");
    
    // Determine number of workers
    int hardware_threads = static_cast<int>(std::thread::hardware_concurrency());
    int num_workers = config.server.num_workers;
    
    // Only cap if hardware_threads is valid AND user requested more than 2x the hardware
    // This allows users to override for containers/VMs where hardware_concurrency() may be wrong
    if (hardware_threads > 0 && num_workers > hardware_threads * 2) {
        spdlog::warn("NUM_WORKERS ({}) is more than 2x hardware concurrency ({}), this may cause performance issues", 
                     num_workers, hardware_threads);
    }
    
    spdlog::info("Starting acceptor/worker pattern with {} workers (hardware cores: {})", 
                 num_workers, hardware_threads > 0 ? hardware_threads : 0);
    
    // Shared data for worker registration
    std::mutex init_mutex;
    std::vector<uWS::App*> worker_apps;
    std::vector<std::thread> worker_threads;
    
    // Create SHARED FileBufferManager (will be initialized by Worker 0)
    std::shared_ptr<FileBufferManager> shared_file_buffer;
    std::mutex file_buffer_mutex;
    std::condition_variable file_buffer_ready;
    
    // Create worker threads
    for (int i = 0; i < num_workers; i++) {
        worker_threads.emplace_back(worker_thread, config, i, num_workers,
                                   std::ref(init_mutex), std::ref(worker_apps),
                                   std::ref(shared_file_buffer), std::ref(file_buffer_mutex),
                                   std::ref(file_buffer_ready));
    }
    
    // Wait for all workers to register
    spdlog::info("Waiting for workers to initialize...");
    auto start_wait = std::chrono::steady_clock::now();
    
    while (true) {
        {
            std::lock_guard<std::mutex> lock(init_mutex);
            if (worker_apps.size() == static_cast<size_t>(num_workers)) {
                auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_wait
                ).count();
                spdlog::info("All {} workers initialized in {}ms", num_workers, wait_time);
                break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Timeout after 3600 seconds (matches MAX_STARTUP_RECOVERY_SECONDS in file_buffer.cpp)
        // TODO: Make this configurable and improve recovery to be non-blocking
        // See README.md "Known Issues & Roadmap" section for details
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_wait
        ).count();
        if (elapsed > 3600) {
            spdlog::error("Timeout waiting for workers to initialize (3600s)");
            spdlog::error("This may indicate file buffer recovery is taking too long");
            spdlog::error("Check pending event count and database connectivity");
            break;
        }
    }
    
    // Create acceptor app
    spdlog::info("Creating acceptor app...");
    auto acceptor = new uWS::App();
    
    // Register all worker apps with acceptor
    {
        std::lock_guard<std::mutex> lock(init_mutex);
        for (auto* worker : worker_apps) {
            acceptor->addChildApp(worker);
        }
        spdlog::info("Registered {} worker apps with acceptor", worker_apps.size());
    }
    
    // Acceptor listens on port and distributes in round-robin
    acceptor->listen(config.server.host, config.server.port, [config, num_workers](auto* listen_socket) {
        if (listen_socket) {
            spdlog::info("Acceptor listening on {}:{}", config.server.host, config.server.port);
            spdlog::info("Round-robin load balancing across {} workers", num_workers);
        } else {
            spdlog::error("Failed to listen on {}:{}", config.server.host, config.server.port);
        }
    });
    
    // Run acceptor event loop
    spdlog::info("Starting acceptor event loop...");
    acceptor->run();
    
    // Wait for shutdown
    spdlog::info("Shutdown - waiting for workers...");
    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    spdlog::info("Clean shutdown");
    return true;
}

} // namespace queen
