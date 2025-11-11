#include "queen/poll_worker.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <thread>
#include <unordered_map>
#include <mutex>

namespace queen {

void init_long_polling(
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
    
    // Spawn dedicated poll worker threads
    for (int worker_id = 0; worker_id < worker_count; worker_id++) {
        std::thread([=]() {
            poll_worker_loop(
                worker_id,
                worker_count,
                registry,
                async_queue_manager,
                worker_response_queues,
                poll_worker_interval_ms,
                poll_db_interval_ms,
                backoff_threshold,
                backoff_multiplier,
                max_poll_interval_ms,
                backoff_cleanup_inactive_threshold
            );
        }).detach();
    }
    
    spdlog::info("Long-polling: {} dedicated poll worker threads spawned", worker_count);
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
    
    // Track backoff state per group for adaptive exponential backoff
    std::unordered_map<std::string, GroupBackoffState> backoff_states;
    
    // Cleanup configuration
    constexpr int CLEANUP_INTERVAL_LOOPS = 600;  // Clean every 600 loops (~60 seconds at 100ms interval)
    
    while (registry->is_running()) {
        loop_count++;
        
        try {
            // PERIODIC CLEANUP: Remove old backoff state entries
            if (loop_count % CLEANUP_INTERVAL_LOOPS == 0) {
                auto now = std::chrono::steady_clock::now();
                
                size_t initial_backoff_size = backoff_states.size();
                size_t initial_query_size = last_query_times.size();
                
                // Cleanup backoff_states
                for (auto it = backoff_states.begin(); it != backoff_states.end();) {
                    auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                        now - it->second.last_accessed
                    ).count();
                    
                    if (age_seconds > backoff_cleanup_inactive_threshold) {
                        it = backoff_states.erase(it);
                    } else {
                        ++it;
                    }
                }
                
                // Cleanup last_query_times
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
                
                size_t final_backoff_size = backoff_states.size();
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
                    
                    // Initialize backoff state if new group
                    if (backoff_states.find(key) == backoff_states.end()) {
                        backoff_states.emplace(key, GroupBackoffState(poll_db_interval_ms));
                    }
                    auto& backoff_state = backoff_states[key];
                    backoff_state.last_accessed = std::chrono::steady_clock::now();
                    
                    int current_interval_ms = backoff_state.current_interval_ms;
                    int current_empty_count = backoff_state.consecutive_empty_pops;
                    
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
                    
                    // Check if this GROUP is already being processed (shared across all workers)
                    bool success = registry->mark_group_in_flight(key);
                    
                    if (!success) {
                        spdlog::debug("Poll worker {} skipping group '{}' - already in-flight",
                                    worker_id, key);
                        continue; // Skip this group - another worker is processing it
                    }
                    
                    spdlog::debug("Poll worker {} processing {} intentions in group '{}'",
                                worker_id, batch.size(), key);
                    
                    // Process ALL intentions in this group sequentially
                    // SEMANTIC GUARANTEE: One intention = One pop operation = One lease
                    int successful_pops = 0;
                    int empty_pops = 0;
                    
                    try {
                        for (const auto& intention : batch) {
                            // Each intention gets its own pop operation with its own lease
                            PopResult result;
                            PopOptions opts;
                            opts.wait = false;
                            opts.batch = intention.batch_size;  // Use THIS client's batch size
                            opts.subscription_mode = intention.subscription_mode;
                            opts.subscription_from = intention.subscription_from;
                            
                            // Execute pop based on intention type
                            if (intention.is_queue_based()) {
                                if (intention.partition_name.has_value()) {
                                    // Specific partition
                                    result = async_queue_manager->pop_messages_from_partition(
                                        intention.queue_name.value(),
                                        intention.partition_name.value(),
                                        intention.consumer_group,
                                        opts
                                    );
                                } else {
                                    // Any partition in queue
                                    result = async_queue_manager->pop_messages_from_queue(
                                        intention.queue_name.value(),
                                        intention.consumer_group,
                                        opts
                                    );
                                }
                            } else {
                                // Namespace/task-based pop (bus mode)
                                result = async_queue_manager->pop_messages_filtered(
                                    intention.namespace_name,
                                    intention.task_name,
                                    intention.consumer_group,
                                    opts
                                );
                            }
                            
                            if (!result.messages.empty()) {
                                // Success - send to this client
                                send_to_single_client(intention, result, worker_response_queues);
                                registry->remove_intention(intention.request_id);
                                successful_pops++;
                                
                                spdlog::info("Poll worker {} fulfilled intention {} with {} messages (group '{}')",
                                           worker_id, intention.request_id, result.messages.size(), key);
                            } else {
                                // No messages for this client
                                empty_pops++;
                                spdlog::debug("Poll worker {} - no messages for intention {} (group '{}')",
                                            worker_id, intention.request_id, key);
                            }
                        }
                        
                        // Update backoff based on results
                        if (successful_pops > 0) {
                            // At least one pop succeeded - reset backoff
                            if (backoff_state.consecutive_empty_pops > 0) {
                                spdlog::debug("Resetting backoff for group '{}' (was: {}ms, {} empty)",
                                            key, backoff_state.current_interval_ms, 
                                            backoff_state.consecutive_empty_pops);
                            }
                            backoff_state.consecutive_empty_pops = 0;
                            backoff_state.current_interval_ms = poll_db_interval_ms;
                        } else if (empty_pops == static_cast<int>(batch.size())) {
                            // All pops were empty - increment backoff
                            backoff_state.consecutive_empty_pops++;
                            
                            if (backoff_state.consecutive_empty_pops >= backoff_threshold) {
                                int old_interval = backoff_state.current_interval_ms;
                                backoff_state.current_interval_ms = std::min(
                                    static_cast<int>(backoff_state.current_interval_ms * backoff_multiplier),
                                    max_poll_interval_ms
                                );
                                
                                if (backoff_state.current_interval_ms > old_interval) {
                                    spdlog::info("Backoff activated for group '{}': {}ms -> {}ms (empty count: {})",
                                                key, old_interval, backoff_state.current_interval_ms,
                                                backoff_state.consecutive_empty_pops);
                                }
                            }
                        }
                        
                        if (successful_pops > 0) {
                            spdlog::info("Poll worker {} completed group '{}': {} successful, {} empty",
                                       worker_id, key, successful_pops, empty_pops);
                        }
                        
                    } catch (const std::exception& e) {
                        spdlog::error("Poll worker {} error processing group '{}': {}", 
                                    worker_id, key, e.what());
                    }
                    
                    // Remove group from in-flight tracking
                    registry->unmark_group_in_flight(key);
                    
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

void send_to_single_client(
    const PollIntention& intention,
    const PopResult& result,
    std::vector<std::shared_ptr<ResponseQueue>>& worker_response_queues
) {
    if (result.messages.empty()) {
        return;
    }
    
    // Build response for this client
    nlohmann::json response = {{"messages", nlohmann::json::array()}};
    
    // Add all messages from the result (already limited by batch_size in pop query)
    for (const auto& msg : result.messages) {
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
    
    spdlog::debug("Sent {} messages to intention {} (worker {})", 
                 result.messages.size(), intention.request_id, intention.worker_id);
}

} // namespace queen

