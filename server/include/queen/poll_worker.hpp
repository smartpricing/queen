#pragma once

#include "queen/poll_intention_registry.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/response_queue.hpp"
#include <memory>
#include <string>
#include <thread>

namespace queen {

/**
 * Initialize long-polling infrastructure
 * 
 * This function spawns dedicated poll worker threads that check the registry
 * for polling intentions and fulfill them when messages are available.
 * Each poll worker executes pop operations directly (no ThreadPool indirection).
 * 
 * @param registry The shared PollIntentionRegistry
 * @param async_queue_manager The AsyncQueueManager for database operations
 * @param worker_response_queues All worker response queues for routing responses
 * @param worker_count Number of poll worker threads to spawn (default: 10)
 * @param poll_worker_interval_ms How often workers wake to check registry (default: 50ms)
 * @param poll_db_interval_ms Minimum time between DB queries per group (default: 500ms)
 * @param backoff_threshold Number of consecutive empty pops before backoff starts (default: 5)
 * @param backoff_multiplier Exponential backoff multiplier (default: 2.0)
 * @param max_poll_interval_ms Maximum poll interval after backoff (default: 2000ms)
 * @param backoff_cleanup_inactive_threshold Cleanup inactive backoff state after N seconds (default: 3600)
 */
void init_long_polling(
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<AsyncQueueManager> async_queue_manager,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    int worker_count = 10,
    int poll_worker_interval_ms = 50,
    int poll_db_interval_ms = 500,
    int backoff_threshold = 5,
    double backoff_multiplier = 2.0,
    int max_poll_interval_ms = 2000,
    int backoff_cleanup_inactive_threshold = 3600
);

/**
 * Poll worker loop - runs indefinitely in its own dedicated thread
 * 
 * This function:
 * 1. Gets active intentions from registry
 * 2. Filters intentions for this worker (load balancing via hash)
 * 3. Groups intentions by (queue, partition, consumer_group)
 * 4. For each group, rate-limits DB queries based on poll_db_interval_ms
 * 5. Implements adaptive exponential backoff when groups consistently return empty
 * 6. Processes ALL intentions in group sequentially, each with own pop operation
 * 7. Each intention gets its own lease and independent message distribution
 * 8. Checks for expired intentions (timeouts)
 * 9. Sleeps for poll_worker_interval_ms, then repeats
 * 
 * SEMANTIC GUARANTEE: One intention = One pop operation = One lease
 * 
 * @param worker_id Worker ID for load balancing (0-based)
 * @param total_workers Total number of poll workers
 * @param registry The shared PollIntentionRegistry
 * @param async_queue_manager The AsyncQueueManager for database operations
 * @param worker_response_queues All worker response queues for routing responses
 * @param poll_worker_interval_ms How often to wake and check registry (default: 50ms)
 * @param poll_db_interval_ms Base minimum time between DB queries per group (default: 500ms)
 * @param backoff_threshold Number of consecutive empty pops before backoff (default: 5)
 * @param backoff_multiplier Exponential backoff multiplier (default: 2.0)
 * @param max_poll_interval_ms Maximum poll interval after backoff (default: 2000ms)
 * @param backoff_cleanup_inactive_threshold Cleanup inactive backoff state after N seconds (default: 3600)
 */
void poll_worker_loop(
    int worker_id,
    int total_workers,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<AsyncQueueManager> async_queue_manager,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    int poll_worker_interval_ms = 50,
    int poll_db_interval_ms = 500,
    int backoff_threshold = 5,
    double backoff_multiplier = 2.0,
    int max_poll_interval_ms = 2000,
    int backoff_cleanup_inactive_threshold = 3600
);

/**
 * Send pop result to a single client
 * 
 * Takes a PopResult with messages and sends them to the specified client
 * via their worker's response queue. Formats messages according to the API contract.
 * 
 * @param intention The client's poll intention
 * @param result The PopResult containing messages
 * @param worker_response_queues All worker response queues for routing
 */
void send_to_single_client(
    const PollIntention& intention,
    const PopResult& result,
    std::vector<std::shared_ptr<ResponseQueue>>& worker_response_queues
);

} // namespace queen

