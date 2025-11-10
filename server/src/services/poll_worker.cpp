#include "queen/poll_worker.hpp"
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <mutex>

namespace queen {

std::string generate_unique_id() {
    // Simple unique ID generation for dispatch groups
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 16; i++) {
        ss << dis(gen);
    }
    return ss.str();
}

void init_long_polling(
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<AsyncQueueManager> async_queue_manager,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    int worker_count,
    int poll_worker_interval_ms,
    int poll_db_interval_ms,
    int backoff_threshold,
    double backoff_multiplier,
    int max_poll_interval_ms,
    int backoff_cleanup_inactive_threshold
) {
    spdlog::info("Initializing long-polling with {} poll workers (worker_interval={}ms, db_interval={}ms, backoff: {}x after {} empty, cleanup_threshold={}s)", 
                worker_count, poll_worker_interval_ms, poll_db_interval_ms, backoff_multiplier, backoff_threshold, backoff_cleanup_inactive_threshold);
    
    // Push never-returning jobs to ThreadPool to reserve worker threads
    for (int worker_id = 0; worker_id < worker_count; worker_id++) {
        thread_pool->push([=]() {
            poll_worker_loop(
                worker_id,
                worker_count,
                registry,
                async_queue_manager,
                thread_pool,
                worker_response_queues,
                poll_worker_interval_ms,
                poll_db_interval_ms,
                backoff_threshold,
                backoff_multiplier,
                max_poll_interval_ms,
                backoff_cleanup_inactive_threshold
            );
        });
    }
    
    spdlog::info("Long-polling: {} poll workers reserved from ThreadPool (size: {})", 
                worker_count, thread_pool->pool_size());
}

// Per-group backoff state
struct GroupBackoffState {
    int consecutive_empty_pops = 0;
    int current_interval_ms;
    std::chrono::steady_clock::time_point last_accessed;
    
    GroupBackoffState() : consecutive_empty_pops(0), current_interval_ms(100), 
                          last_accessed(std::chrono::steady_clock::now()) {}
    GroupBackoffState(int base_interval) : consecutive_empty_pops(0), 
                                           current_interval_ms(base_interval),
                                           last_accessed(std::chrono::steady_clock::now()) {}
};

void poll_worker_loop(
    int worker_id,
    int total_workers,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<AsyncQueueManager> async_queue_manager,
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    int poll_worker_interval_ms,
    int poll_db_interval_ms,
    int backoff_threshold,
    double backoff_multiplier,
    int max_poll_interval_ms,
    int backoff_cleanup_inactive_threshold
) {
    spdlog::info("Poll worker {} started (1 of {}) - worker_interval={}ms, db_interval={}ms, backoff={}x@{}, max={}ms, cleanup_threshold={}s", 
                worker_id, total_workers, poll_worker_interval_ms, poll_db_interval_ms, 
                backoff_multiplier, backoff_threshold, max_poll_interval_ms, backoff_cleanup_inactive_threshold);
    
    int loop_count = 0;
    
    // Track last query time per group key for rate limiting
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_query_times;
    
    // Track backoff state per group for adaptive exponential backoff (thread-safe shared state)
    auto backoff_states = std::make_shared<std::unordered_map<std::string, GroupBackoffState>>();
    auto backoff_mutex = std::make_shared<std::mutex>();
    
    // Cleanup configuration
    constexpr int CLEANUP_INTERVAL_LOOPS = 600;  // Clean every 600 loops (~60 seconds at 100ms interval)
    
    while (registry->is_running()) {
        loop_count++;
        
        try {
            // PERIODIC CLEANUP: Remove old backoff state entries
            if (loop_count % CLEANUP_INTERVAL_LOOPS == 0) {
                auto now = std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(*backoff_mutex);
                
                size_t initial_backoff_size = backoff_states->size();
                size_t initial_query_size = last_query_times.size();
                
                // Cleanup backoff_states
                for (auto it = backoff_states->begin(); it != backoff_states->end();) {
                    auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                        now - it->second.last_accessed
                    ).count();
                    
                    if (age_seconds > backoff_cleanup_inactive_threshold) {
                        it = backoff_states->erase(it);
                    } else {
                        ++it;
                    }
                }
                
                // Cleanup last_query_times (outside the lock - it's not shared)
                for (auto it = last_query_times.begin(); it != last_query_times.end();) {
                    auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                        now - it->second
                    ).count();
                    
                    if (age_seconds > backoff_cleanup_inactive_threshold) {
                        it = last_query_times.erase(it);
                    } else {
                        ++it;
                    }
                }
                
                size_t final_backoff_size = backoff_states->size();
                size_t final_query_size = last_query_times.size();
                size_t cleaned_backoff = initial_backoff_size - final_backoff_size;
                size_t cleaned_query = initial_query_size - final_query_size;
                
                if (cleaned_backoff > 0 || cleaned_query > 0) {
                    spdlog::info("[Worker {}] Backoff cleanup: removed {} backoff states ({} -> {}), {} query times ({} -> {})",
                                worker_id, cleaned_backoff, initial_backoff_size, final_backoff_size,
                                cleaned_query, initial_query_size, final_query_size);
                }
            }
            
            // Get all active intentions
            auto all_intentions = registry->get_active_intentions();
            
            // Log every 100 loops or when we have intentions
            if (loop_count % 100 == 0 || (!all_intentions.empty() && loop_count % 10 == 0)) {
                spdlog::debug("Poll worker {} loop {}: registry has {} total intentions", 
                           worker_id, loop_count, all_intentions.size());
            }
            
            // CHECK TIMEOUTS FIRST (before processing groups)
            // This ensures timeouts fire on time even if group processing is slow
            auto now = std::chrono::steady_clock::now();
            for (const auto& intention : all_intentions) {
                if (now >= intention.deadline) {
                    // Try to mark group as in-flight to get exclusive access for timeout
                    std::string group_key = intention.grouping_key();
                    if (!registry->mark_group_in_flight(group_key)) {
                        // Another worker is handling this group
                        continue;
                    }
                    
                    // We got exclusive access - send timeout response to correct worker's queue
                    nlohmann::json empty_response;
                    worker_response_queues[intention.worker_id]->push(intention.request_id, empty_response, false, 204);
                    registry->remove_intention(intention.request_id);
                    registry->unmark_group_in_flight(group_key);
                    
                    spdlog::info("Poll worker {} TIMEOUT: intention {} exceeded deadline (sent 204)", 
                                 worker_id, intention.request_id);
                }
            }
            
            if (all_intentions.empty()) {
                // No intentions, sleep and continue
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_worker_interval_ms));
                continue;
            }
            
            // Filter to this worker's intentions (load balancing)
            auto my_intentions = filter_by_worker(all_intentions, worker_id, total_workers);
            
            spdlog::debug("Poll worker {} has {} intentions (out of {} total)", 
                        worker_id, my_intentions.size(), all_intentions.size());
            
            if (my_intentions.empty()) {
                // No work for this worker
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_worker_interval_ms));
                continue;
            }
            
            // Group intentions by (queue, partition, consumer_group)
            auto grouped = group_intentions(my_intentions);
            
            spdlog::debug("Poll worker {} processing {} intentions in {} groups", 
                        worker_id, my_intentions.size(), grouped.size());
            
            // Process each group
            for (auto& [key, batch] : grouped) {
                try {
                    spdlog::debug("Poll worker {} processing group '{}' ({} intentions)", 
                                worker_id, key, batch.size());
                    
                    // Get current backoff state for this group (thread-safe)
                    int current_interval_ms;
                    int current_empty_count;
                    {
                        std::lock_guard<std::mutex> lock(*backoff_mutex);
                        // Initialize backoff state if new group
                        if (backoff_states->find(key) == backoff_states->end()) {
                            backoff_states->emplace(key, GroupBackoffState(poll_db_interval_ms));
                        }
                        auto& backoff_state = (*backoff_states)[key];
                        backoff_state.last_accessed = std::chrono::steady_clock::now(); // Update access time
                        current_interval_ms = backoff_state.current_interval_ms;
                        current_empty_count = backoff_state.consecutive_empty_pops;
                    }
                    
                    // Rate-limit DB queries per group (using adaptive interval)
                    auto now = std::chrono::steady_clock::now();
                    auto last_query_it = last_query_times.find(key);
                    
                    if (last_query_it != last_query_times.end()) {
                        auto time_since_last_query = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - last_query_it->second).count();
                        
                        if (time_since_last_query < current_interval_ms) {
                            spdlog::debug("Poll worker {} skipping group '{}' - last query {}ms ago (current interval: {}ms, empty_count: {})",
                                        worker_id, key, time_since_last_query, current_interval_ms, current_empty_count);
                            continue; // Too soon since last query
                        }
                    }
                    
                    // Update last query time for this group
                    last_query_times[key] = now;
                    
                    {
                        // Check if this GROUP is already being processed (shared across all workers)
                        bool success = registry->mark_group_in_flight(key);
                        
                        if (!success) {
                            spdlog::debug("Poll worker {} skipping group '{}' - already in-flight",
                                        worker_id, key);
                            continue; // Skip this group - another worker is processing it
                        }
                        
                        spdlog::debug("Poll worker {} submitting pop job for group '{}' with {} intentions",
                                    worker_id, key, batch.size());
                        
                        // Capture batch and group key by value for the lambda
                        auto batch_copy = batch;
                        auto group_key = key;  // Explicit copy
                        
                        // Capture everything by value (including shared_ptrs)
                        thread_pool->push([=]() {
                            try {
                                // Calculate batch size based on consumer group mode
                                // For consumer groups: use original batch size (don't sum) - partition lease protection ensures fair distribution
                                // For queue mode: sum all batch sizes to fetch enough messages for all waiting clients
                                int total_batch;
                                if (batch_copy[0].consumer_group != "__QUEUE_MODE__") {
                                    // Consumer group mode: use first intention's batch size (don't batch requests)
                                    total_batch = batch_copy[0].batch_size;
                                    spdlog::debug("Consumer group '{}': using original batch size {} (not summing {} intentions)",
                                                batch_copy[0].consumer_group, total_batch, batch_copy.size());
                                } else {
                                    // Queue mode: sum all batch sizes
                                    total_batch = 0;
                                    for (const auto& intention : batch_copy) {
                                        total_batch += intention.batch_size;
                                    }
                                    spdlog::debug("Queue mode: summed batch size {} from {} intentions",
                                                total_batch, batch_copy.size());
                                }
                                
                                // Execute the actual pop with locking
                                PopResult result;
                                if (batch_copy[0].is_queue_based()) {
                                    // Queue-based pop
                                    PopOptions opts;
                                    opts.wait = false;
                                    opts.batch = total_batch;
                                    opts.subscription_mode = batch_copy[0].subscription_mode;
                                    opts.subscription_from = batch_copy[0].subscription_from;
                                    
                                    if (batch_copy[0].partition_name.has_value()) {
                                        // Specific partition
                                        result = async_queue_manager->pop_messages_from_partition(
                                            batch_copy[0].queue_name.value(),
                                            batch_copy[0].partition_name.value(),
                                            batch_copy[0].consumer_group,
                                            opts
                                        );
                                    } else {
                                        // Any partition in queue
                                        result = async_queue_manager->pop_messages_from_queue(
                                        batch_copy[0].queue_name.value(),
                                        batch_copy[0].consumer_group,
                                            opts
                                    );
                                    }
                                } else {
                                    // Namespace/task-based pop
                                    PopOptions opts;
                                    opts.wait = false;
                                    opts.batch = total_batch;
                                    opts.subscription_mode = batch_copy[0].subscription_mode;
                                    opts.subscription_from = batch_copy[0].subscription_from;
                                    
                                    result = async_queue_manager->pop_messages_filtered(
                                        batch_copy[0].namespace_name,
                                        batch_copy[0].task_name,
                                        batch_copy[0].consumer_group,
                                        opts
                                    );
                                }
                                
                                if (!result.messages.empty()) {
                                    // Messages found - reset backoff for this group
                                    {
                                        std::lock_guard<std::mutex> lock(*backoff_mutex);
                                        auto it = backoff_states->find(group_key);
                                        if (it != backoff_states->end()) {
                                            if (it->second.consecutive_empty_pops > 0) {
                                                spdlog::debug("Resetting backoff for group '{}' (was: {}ms, {} empty)",
                                                            group_key, it->second.current_interval_ms, 
                                                            it->second.consecutive_empty_pops);
                                            }
                                            it->second.consecutive_empty_pops = 0;
                                            it->second.current_interval_ms = poll_db_interval_ms; // Reset to base
                                            it->second.last_accessed = std::chrono::steady_clock::now(); // Update access time
                                        }
                                    }
                                    
                                    // Distribute messages to waiting clients
                                    auto fulfilled_ids = distribute_to_clients(result, batch_copy, worker_response_queues);
                                    
                                    // Remove fulfilled intentions from registry AFTER responses queued
                                    for (const auto& request_id : fulfilled_ids) {
                                        registry->remove_intention(request_id);
                                    }
                                    
                                    spdlog::info("Long-poll fulfilled {} intentions with {} messages for group '{}'",
                                               fulfilled_ids.size(), result.messages.size(), group_key);
                                } else {
                                    // No messages - increment backoff counter and apply exponential backoff
                                    {
                                        std::lock_guard<std::mutex> lock(*backoff_mutex);
                                        auto it = backoff_states->find(group_key);
                                        if (it != backoff_states->end()) {
                                            it->second.consecutive_empty_pops++;
                                            it->second.last_accessed = std::chrono::steady_clock::now(); // Update access time
                                            
                                            // Apply exponential backoff if threshold reached
                                            if (it->second.consecutive_empty_pops >= backoff_threshold) {
                                                int old_interval = it->second.current_interval_ms;
                                                it->second.current_interval_ms = std::min(
                                                    static_cast<int>(it->second.current_interval_ms * backoff_multiplier),
                                                    max_poll_interval_ms
                                                );
                                                
                                                if (it->second.current_interval_ms > old_interval) {
                                                    spdlog::info("Backoff activated for group '{}': {}ms -> {}ms (empty count: {})",
                                                                group_key, old_interval, it->second.current_interval_ms,
                                                                it->second.consecutive_empty_pops);
                                                }
                                            }
                                        }
                                    }
                                    
                                    // Keep intentions in registry - poll worker will try again later
                                    // Timeout handler will send 204 when deadline is reached
                                    spdlog::debug("Pop returned no messages for group '{}' - keeping {} intentions in registry",
                                                group_key, batch_copy.size());
                                }
                                
                                // Remove group from in-flight tracking
                                registry->unmark_group_in_flight(group_key);
                                
                            } catch (const std::exception& e) {
                                spdlog::error("Pop job error for group '{}': {}", group_key, e.what());
                                
                                // Remove group from in-flight even on error
                                registry->unmark_group_in_flight(group_key);
                            }
                        });
                    }
                    
                } catch (const std::exception& e) {
                    spdlog::error("Poll worker {} group processing error: {}", worker_id, e.what());
                }
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Poll worker {} error: {}", worker_id, e.what());
        }
        
        // Sleep based on worker interval (checking registry is cheap)
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_worker_interval_ms));
    }
    
    spdlog::info("Poll worker {} stopped", worker_id);
}

std::vector<std::string> distribute_to_clients(
    const PopResult& result,
    const std::vector<PollIntention>& batch,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues
) {
    std::vector<std::string> fulfilled;
    
    if (result.messages.empty()) {
        return fulfilled;
    }
    
    // Distribute messages to clients in order
    size_t message_idx = 0;
    
    for (const auto& intention : batch) {
        if (message_idx >= result.messages.size()) {
            break; // No more messages to distribute
        }
        
        // Build response for this client
        nlohmann::json response = {{"messages", nlohmann::json::array()}};
        
        // Add messages up to this client's batch_size
        int messages_for_client = std::min(
            intention.batch_size,
            static_cast<int>(result.messages.size() - message_idx)
        );
        
        for (int i = 0; i < messages_for_client; i++) {
            const auto& msg = result.messages[message_idx++];
            
            // Format timestamp
            auto time_t = std::chrono::system_clock::to_time_t(msg.created_at);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                msg.created_at.time_since_epoch()) % 1000;
            
            std::stringstream created_at_ss;
            created_at_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
            created_at_ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
            
            // Match the exact format from acceptor_server.cpp pop responses
            nlohmann::json msg_json = {
                {"id", msg.id},
                {"transactionId", msg.transaction_id},
                {"partitionId", msg.partition_id},
                {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                {"queue", msg.queue_name},
                {"partition", msg.partition_name},
                {"data", msg.payload},  // Use "data", not "payload"
                {"retryCount", msg.retry_count},
                {"priority", msg.priority},
                {"createdAt", created_at_ss.str()},
                {"consumerGroup", intention.consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(intention.consumer_group)},
                {"leaseId", result.lease_id.has_value() ? nlohmann::json(*result.lease_id) : nlohmann::json(nullptr)}
            };
            
            response["messages"].push_back(msg_json);
        }
        
        // Send response to client's worker queue
        worker_response_queues[intention.worker_id]->push(intention.request_id, response, false, 200);
        fulfilled.push_back(intention.request_id);
        
        spdlog::debug("Distributed {} messages to intention {}", 
                     messages_for_client, intention.request_id);
    }
    
    return fulfilled;
}

} // namespace queen

