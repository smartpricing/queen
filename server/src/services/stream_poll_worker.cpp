#include "queen/stream_poll_worker.hpp"
#include "queen/database.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace queen {

void init_stream_long_polling(
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::shared_ptr<StreamPollIntentionRegistry> registry,
    std::shared_ptr<StreamManager> stream_manager,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    int worker_count,
    int poll_worker_interval_ms,
    int poll_stream_interval_ms,
    int backoff_threshold,
    double backoff_multiplier,
    int max_poll_interval_ms
) {
    spdlog::info("Initializing stream long-polling with {} poll workers (worker_interval={}ms, stream_interval={}ms, backoff: {}x after {} empty)", 
                worker_count, poll_worker_interval_ms, poll_stream_interval_ms, backoff_multiplier, backoff_threshold);
    
    // Push never-returning jobs to ThreadPool to reserve worker threads
    for (int worker_id = 0; worker_id < worker_count; worker_id++) {
        thread_pool->push([=]() {
            stream_poll_worker_loop(
                worker_id,
                worker_count,
                registry,
                stream_manager,
                thread_pool,
                worker_response_queues,
                poll_worker_interval_ms,
                poll_stream_interval_ms,
                backoff_threshold,
                backoff_multiplier,
                max_poll_interval_ms
            );
        });
    }
    
    spdlog::info("Stream long-polling: {} poll workers reserved from ThreadPool", worker_count);
}

// Per-group backoff state for streams
struct StreamGroupBackoffState {
    int consecutive_empty_checks = 0;
    int current_interval_ms;
    
    StreamGroupBackoffState() : consecutive_empty_checks(0), current_interval_ms(1000) {}
    StreamGroupBackoffState(int base_interval) : consecutive_empty_checks(0), current_interval_ms(base_interval) {}
};

void stream_poll_worker_loop(
    int worker_id,
    int total_workers,
    std::shared_ptr<StreamPollIntentionRegistry> registry,
    std::shared_ptr<StreamManager> stream_manager,
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues,
    int poll_worker_interval_ms,
    int poll_stream_interval_ms,
    int backoff_threshold,
    double backoff_multiplier,
    int max_poll_interval_ms
) {
    spdlog::info("Stream poll worker {} started (1 of {}) - worker_interval={}ms, stream_interval={}ms, backoff={}x@{}, max={}ms", 
                worker_id, total_workers, poll_worker_interval_ms, poll_stream_interval_ms, 
                backoff_multiplier, backoff_threshold, max_poll_interval_ms);
    
    int loop_count = 0;
    
    // Track last check time per group key for rate limiting
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_check_times;
    
    // Track backoff state per group for adaptive exponential backoff
    auto backoff_states = std::make_shared<std::unordered_map<std::string, StreamGroupBackoffState>>();
    auto backoff_mutex = std::make_shared<std::mutex>();
    
    while (registry->is_running()) {
        loop_count++;
        
        try {
            // Get all active intentions
            auto all_intentions = registry->get_active_intentions();
            
            // Log every 100 loops or when we have intentions
            if (loop_count % 100 == 0 || (!all_intentions.empty() && loop_count % 10 == 0)) {
                spdlog::debug("Stream poll worker {} loop {}: registry has {} total intentions", 
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
                    
                    spdlog::info("Stream poll worker {} TIMEOUT: intention {} exceeded deadline (sent 204)", 
                                 worker_id, intention.request_id);
                }
            }
            
            if (all_intentions.empty()) {
                // No intentions, sleep and continue
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_worker_interval_ms));
                continue;
            }
            
            // Group intentions by (stream_name, consumer_group)
            auto grouped = group_stream_intentions(all_intentions);
            
            spdlog::debug("Stream poll worker {} processing {} intentions in {} groups", 
                        worker_id, all_intentions.size(), grouped.size());
            
            // Process each group (simple round-robin by worker_id)
            int group_idx = 0;
            for (auto& [group_key, batch] : grouped) {
                // Simple load balancing: worker processes groups that hash to it
                if ((group_idx % total_workers) != worker_id) {
                    group_idx++;
                    continue;
                }
                group_idx++;
                
                try {
                    spdlog::debug("Stream poll worker {} processing group '{}' ({} intentions)", 
                                worker_id, group_key, batch.size());
                    
                    // Get current backoff state for this group
                    int current_interval_ms;
                    int current_empty_count;
                    {
                        std::lock_guard<std::mutex> lock(*backoff_mutex);
                        if (backoff_states->find(group_key) == backoff_states->end()) {
                            backoff_states->emplace(group_key, StreamGroupBackoffState(poll_stream_interval_ms));
                        }
                        auto& backoff_state = (*backoff_states)[group_key];
                        current_interval_ms = backoff_state.current_interval_ms;
                        current_empty_count = backoff_state.consecutive_empty_checks;
                    }
                    
                    // Rate-limit stream checks per group (using adaptive interval)
                    now = std::chrono::steady_clock::now();
                    auto last_check_it = last_check_times.find(group_key);
                    
                    if (last_check_it != last_check_times.end()) {
                        auto time_since_last_check = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - last_check_it->second).count();
                        
                        if (time_since_last_check < current_interval_ms) {
                            spdlog::debug("Stream poll worker {} skipping group '{}' - last check {}ms ago (interval: {}ms, empty_count: {})",
                                        worker_id, group_key, time_since_last_check, current_interval_ms, current_empty_count);
                            continue;
                        }
                    }
                    
                    // Update last check time for this group
                    last_check_times[group_key] = now;
                    
                    // Check if this GROUP is already being processed
                    bool success = registry->mark_group_in_flight(group_key);
                    
                    if (!success) {
                        spdlog::debug("Stream poll worker {} skipping group '{}' - already in-flight",
                                    worker_id, group_key);
                        continue;
                    }
                    
                    spdlog::debug("Stream poll worker {} checking for ready windows for group '{}'",
                                worker_id, group_key);
                    
                    // Get stream info from first intention (all in group have same stream/consumer_group)
                    const auto& first_intention = batch[0];
                    std::string stream_name = first_intention.stream_name;
                    std::string consumer_group = first_intention.consumer_group;
                    
                    // Submit window check job to ThreadPool
                    // Capture by value for lambda safety
                    auto batch_copy = batch;
                    thread_pool->push([=]() {
                        try {
                            // This will be implemented in StreamManager as check_and_deliver_window
                            // For now, we'll call it directly (needs to be added to StreamManager)
                            bool window_delivered = stream_manager->check_and_deliver_window_for_poll(
                                stream_name,
                                consumer_group,
                                batch_copy,
                                worker_response_queues
                            );
                            
                            if (window_delivered) {
                                // Window found and delivered - reset backoff
                                {
                                    std::lock_guard<std::mutex> lock(*backoff_mutex);
                                    auto it = backoff_states->find(group_key);
                                    if (it != backoff_states->end()) {
                                        if (it->second.consecutive_empty_checks > 0) {
                                            spdlog::debug("Resetting backoff for stream group '{}' (was: {}ms, {} empty)",
                                                        group_key, it->second.current_interval_ms, 
                                                        it->second.consecutive_empty_checks);
                                        }
                                        it->second.consecutive_empty_checks = 0;
                                        it->second.current_interval_ms = poll_stream_interval_ms;
                                    }
                                }
                                
                                // Remove fulfilled intentions from registry
                                for (const auto& intention : batch_copy) {
                                    registry->remove_intention(intention.request_id);
                                }
                                
                                spdlog::info("Stream poll fulfilled {} intentions for group '{}'",
                                           batch_copy.size(), group_key);
                            } else {
                                // No window ready - increment backoff counter
                                {
                                    std::lock_guard<std::mutex> lock(*backoff_mutex);
                                    auto it = backoff_states->find(group_key);
                                    if (it != backoff_states->end()) {
                                        it->second.consecutive_empty_checks++;
                                        
                                        // Apply exponential backoff if threshold reached
                                        if (it->second.consecutive_empty_checks >= backoff_threshold) {
                                            int old_interval = it->second.current_interval_ms;
                                            it->second.current_interval_ms = std::min(
                                                static_cast<int>(it->second.current_interval_ms * backoff_multiplier),
                                                max_poll_interval_ms
                                            );
                                            
                                            if (it->second.current_interval_ms > old_interval) {
                                                spdlog::info("Backoff activated for stream group '{}': {}ms -> {}ms (empty count: {})",
                                                            group_key, old_interval, it->second.current_interval_ms,
                                                            it->second.consecutive_empty_checks);
                                            }
                                        }
                                    }
                                }
                                
                                spdlog::debug("No ready windows for stream group '{}' - keeping {} intentions in registry",
                                            group_key, batch_copy.size());
                            }
                            
                            // Remove group from in-flight tracking
                            registry->unmark_group_in_flight(group_key);
                            
                        } catch (const std::exception& e) {
                            spdlog::error("Stream window check job error for group '{}': {}", group_key, e.what());
                            registry->unmark_group_in_flight(group_key);
                        }
                    });
                    
                } catch (const std::exception& e) {
                    spdlog::error("Stream poll worker {} group processing error: {}", worker_id, e.what());
                }
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Stream poll worker {} error: {}", worker_id, e.what());
        }
        
        // Sleep based on worker interval
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_worker_interval_ms));
    }
    
    spdlog::info("Stream poll worker {} stopped", worker_id);
}

} // namespace queen

