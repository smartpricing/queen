#pragma once

#include "queen/stream_poll_intention_registry.hpp"
#include "queen/stream_manager.hpp"
#include "queen/response_queue.hpp"
#include <threadpool.hpp>
#include <memory>
#include <string>

namespace queen {

// Forward declaration
class StreamManager;

/**
 * Initialize stream long-polling infrastructure
 * 
 * This function reserves worker threads from the ThreadPool at startup.
 * These workers run stream_poll_worker_loop() indefinitely, checking the registry
 * for polling intentions and fulfilling them when windows are available.
 * 
 * @param thread_pool The ThreadPool to reserve workers from
 * @param registry The shared StreamPollIntentionRegistry
 * @param stream_manager The StreamManager for stream operations
 * @param worker_response_queues All worker response queues for routing responses
 * @param worker_count Number of poll worker threads to reserve (default: 2)
 * @param poll_worker_interval_ms How often workers wake to check registry (default: 100ms)
 * @param poll_stream_interval_ms Minimum time between stream checks per group (default: 1000ms)
 * @param backoff_threshold Number of consecutive empty checks before backoff starts (default: 5)
 * @param backoff_multiplier Exponential backoff multiplier (default: 2.0)
 * @param max_poll_interval_ms Maximum poll interval after backoff (default: 5000ms)
 */
void init_stream_long_polling(
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::shared_ptr<StreamPollIntentionRegistry> registry,
    std::shared_ptr<StreamManager> stream_manager,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    int worker_count = 2,
    int poll_worker_interval_ms = 100,
    int poll_stream_interval_ms = 1000,
    int backoff_threshold = 5,
    double backoff_multiplier = 2.0,
    int max_poll_interval_ms = 5000,
    int backoff_cleanup_inactive_threshold = 3600
);

/**
 * Stream poll worker loop - runs indefinitely in a ThreadPool thread
 * 
 * This function:
 * 1. Gets active intentions from registry
 * 2. Checks for expired intentions (timeouts) and sends 204
 * 3. Filters intentions for this worker (load balancing)
 * 4. Groups intentions by (stream_name, consumer_group)
 * 5. For each group, rate-limits stream checks based on poll_stream_interval_ms
 * 6. Implements adaptive exponential backoff when groups consistently have no ready windows
 * 7. If time since last check >= effective_interval, checks for ready windows
 * 8. Sleeps for poll_worker_interval_ms, then repeats
 * 
 * @param worker_id Worker ID for load balancing (0-based)
 * @param total_workers Total number of poll workers
 * @param registry The shared StreamPollIntentionRegistry
 * @param stream_manager The StreamManager for stream operations
 * @param thread_pool The ThreadPool for submitting window check jobs
 * @param worker_response_queues All worker response queues for routing responses
 * @param poll_worker_interval_ms How often to wake and check registry
 * @param poll_stream_interval_ms Base minimum time between stream checks per group
 * @param backoff_threshold Number of consecutive empty checks before backoff
 * @param backoff_multiplier Exponential backoff multiplier
 * @param max_poll_interval_ms Maximum poll interval after backoff
 */
void stream_poll_worker_loop(
    int worker_id,
    int total_workers,
    std::shared_ptr<StreamPollIntentionRegistry> registry,
    std::shared_ptr<StreamManager> stream_manager,
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    int poll_worker_interval_ms = 100,
    int poll_stream_interval_ms = 1000,
    int backoff_threshold = 5,
    double backoff_multiplier = 2.0,
    int max_poll_interval_ms = 5000,
    int backoff_cleanup_inactive_threshold = 3600
);

} // namespace queen

