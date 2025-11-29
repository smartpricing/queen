#include "queen/poll_worker.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <mutex>

namespace queen {

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
    
    spdlog::info("Long-polling: {} poll workers reserved from ThreadPool", worker_count);
}

// Note: GroupBackoffState is now defined in poll_intention_registry.hpp
// and backoff state is managed by PollIntentionRegistry for peer notification support

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
    
    // Track last query time per group key for rate limiting
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_query_times;
    
    // Note: Backoff state is now managed centrally by PollIntentionRegistry for peer notification support
    
    // Cleanup and logging timing
    constexpr int CLEANUP_INTERVAL_SECONDS = 60;  // Clean every 60 seconds
    constexpr int LOG_INTERVAL_SECONDS = 10;  // Log every 10 seconds
    auto last_cleanup_time = std::chrono::steady_clock::now();
    auto last_log_time = std::chrono::steady_clock::now();
    
    while (registry->is_running()) {
        try {
            auto now = std::chrono::steady_clock::now();
            
            // PERIODIC CLEANUP: Remove old backoff state entries
            auto seconds_since_cleanup = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_cleanup_time).count();
            
            if (seconds_since_cleanup >= CLEANUP_INTERVAL_SECONDS) {
                last_cleanup_time = now;
                
                size_t initial_query_size = last_query_times.size();
                
                // Cleanup backoff_states via registry (centralized for peer notification support)
                registry->cleanup_backoff_states(backoff_cleanup_inactive_threshold);
                
                // Cleanup last_query_times (still local per worker)
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
                
                size_t final_query_size = last_query_times.size();
                size_t cleaned_query = initial_query_size - final_query_size;
                
                if (cleaned_query > 0) {
                    spdlog::debug("[Worker {}] Cleaned {} local query times ({} -> {})",
                                worker_id, cleaned_query, initial_query_size, final_query_size);
                }
            }
            
            // Get all active intentions
            auto all_intentions = registry->get_active_intentions();
            
            // Log periodically when we have intentions
            auto seconds_since_log = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_log_time).count();
            
            if (!all_intentions.empty() && seconds_since_log >= LOG_INTERVAL_SECONDS) {
                spdlog::debug("Poll worker {} status: registry has {} total intentions", 
                           worker_id, all_intentions.size());
                last_log_time = now;
            }
            
            // CHECK TIMEOUTS FIRST (before processing groups)
            // This ensures timeouts fire on time even if group processing is slow
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
                    
                    // Get current interval from registry (centralized backoff for peer notification support)
                    int current_interval_ms = registry->get_current_interval(key);
                    
                    // Rate-limit DB queries per group (using adaptive interval)
                    auto now = std::chrono::steady_clock::now();
                    auto last_query_it = last_query_times.find(key);
                    
                    if (last_query_it != last_query_times.end()) {
                        auto time_since_last_query = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - last_query_it->second).count();
                        
                        if (time_since_last_query < current_interval_ms) {
                            spdlog::debug("Poll worker {} skipping group '{}' - last query {}ms ago (current interval: {}ms)",
                                        worker_id, key, time_since_last_query, current_interval_ms);
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
                            
                            // Execute pop using stored procedure
                            result = async_queue_manager->pop_messages_sp(
                                        intention.queue_name.value(),
                                intention.partition_name.value_or(""),  // Empty for any partition
                                    intention.consumer_group,
                                    opts
                                );
                            
                            if (!result.messages.empty()) {
                                // Success - send to this client
                                send_to_single_client(intention, result, worker_response_queues);
                                registry->remove_intention(intention.request_id);
                                successful_pops++;
                                
                                spdlog::info("Poll worker {} fulfilled intention {} with {} messages (group '{}')",
                                           worker_id, intention.request_id, result.messages.size(), key);
                            } else {
                                // No messages for this client - queue is empty, no point trying remaining intentions
                                empty_pops++;
                                spdlog::debug("Poll worker {} - no messages for intention {} (group '{}'), breaking early to avoid unnecessary DB queries",
                                            worker_id, intention.request_id, key);
                                break; // Exit early - if queue is empty now, it will be empty for remaining intentions
                            }
                        }
                        
                        // Update backoff via registry (centralized for peer notification support)
                        bool had_messages = (successful_pops > 0);
                        registry->update_backoff_state(key, had_messages, backoff_threshold, 
                                                       backoff_multiplier, max_poll_interval_ms);
                        
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
        
        // Sleep based on worker interval
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

