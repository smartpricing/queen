#pragma once

#include "queen/poll_intention_registry.hpp"
#include "queen/queue_manager.hpp"
#include "queen/response_queue.hpp"
#include <threadpool.hpp>
#include <memory>
#include <string>

namespace queen {

/**
 * Initialize long-polling infrastructure
 * 
 * This function reserves worker threads from the ThreadPool at startup.
 * These workers run poll_worker_loop() indefinitely, checking the registry
 * for polling intentions and fulfilling them when messages are available.
 * 
 * @param thread_pool The ThreadPool to reserve workers from
 * @param registry The shared PollIntentionRegistry
 * @param queue_manager The QueueManager for database operations
 * @param response_queue The ResponseQueue for sending responses
 * @param worker_count Number of poll worker threads to reserve (default: 2)
 */
void init_long_polling(
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<QueueManager> queue_manager,
    std::shared_ptr<ResponseQueue> response_queue,
    int worker_count = 2
);

/**
 * Poll worker loop - runs indefinitely in a ThreadPool thread
 * 
 * This function:
 * 1. Gets active intentions from registry
 * 2. Filters intentions for this worker (load balancing)
 * 3. Groups intentions by (queue, partition, consumer_group)
 * 4. For each group, checks if messages are available
 * 5. If available, submits a pop job to ThreadPool (with priority)
 * 6. Checks for expired intentions (timeouts)
 * 7. Sleeps for poll_interval_ms, then repeats
 * 
 * @param worker_id Worker ID for load balancing (0-based)
 * @param total_workers Total number of poll workers
 * @param registry The shared PollIntentionRegistry
 * @param queue_manager The QueueManager for database operations
 * @param thread_pool The ThreadPool for submitting pop jobs
 * @param response_queue The ResponseQueue for sending responses
 * @param poll_interval_ms How often to check intentions (default: 100ms)
 */
void poll_worker_loop(
    int worker_id,
    int total_workers,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<QueueManager> queue_manager,
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::shared_ptr<ResponseQueue> response_queue,
    int poll_interval_ms = 100
);

/**
 * Distribute messages to waiting clients
 * 
 * Takes a PopResult with messages and distributes them to the waiting
 * clients represented by the batch of intentions. Messages are distributed
 * in order, respecting each client's batch_size limit.
 * 
 * @param result The PopResult containing messages
 * @param batch The group of intentions waiting for these messages
 * @param response_queue The ResponseQueue for sending responses
 * @return Vector of request_ids that were fulfilled (should be removed from registry)
 */
std::vector<std::string> distribute_to_clients(
    const PopResult& result,
    const std::vector<PollIntention>& batch,
    std::shared_ptr<ResponseQueue> response_queue
);

/**
 * Generate a unique ID for dispatch groups
 */
std::string generate_unique_id();

} // namespace queen

