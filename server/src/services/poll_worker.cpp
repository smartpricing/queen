#include "queen/poll_worker.hpp"
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>
#include <unordered_set>

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
    std::shared_ptr<QueueManager> queue_manager,
    std::shared_ptr<ResponseQueue> response_queue,
    int worker_count
) {
    spdlog::info("Initializing long-polling with {} poll workers", worker_count);
    
    // Push never-returning jobs to ThreadPool to reserve worker threads
    for (int worker_id = 0; worker_id < worker_count; worker_id++) {
        thread_pool->push([=]() {
            poll_worker_loop(
                worker_id,
                worker_count,
                registry,
                queue_manager,
                thread_pool,
                response_queue,
                100  // 100ms poll interval
            );
        });
    }
    
    spdlog::info("Long-polling: {} poll workers reserved from ThreadPool (size: {})", 
                worker_count, thread_pool->pool_size());
}

void poll_worker_loop(
    int worker_id,
    int total_workers,
    std::shared_ptr<PollIntentionRegistry> registry,
    std::shared_ptr<QueueManager> queue_manager,
    std::shared_ptr<astp::ThreadPool> thread_pool,
    std::shared_ptr<ResponseQueue> response_queue,
    int poll_interval_ms
) {
    spdlog::info("Poll worker {} started (1 of {})", worker_id, total_workers);
    
    int loop_count = 0;
    int consecutive_empty_pops = 0;  // Track empty results for backoff
    
    while (registry->is_running()) {
        loop_count++;
        
        try {
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
                    
                    // We got exclusive access - send timeout response
                    nlohmann::json empty_response;
                    response_queue->push(intention.request_id, empty_response, false, 204);
                    registry->remove_intention(intention.request_id);
                    registry->unmark_group_in_flight(group_key);
                    
                    spdlog::info("Poll worker {} TIMEOUT: intention {} exceeded deadline (sent 204)", 
                                 worker_id, intention.request_id);
                }
            }
            
            if (all_intentions.empty()) {
                // No intentions, sleep and continue
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
                continue;
            }
            
            // Filter to this worker's intentions (load balancing)
            auto my_intentions = filter_by_worker(all_intentions, worker_id, total_workers);
            
            spdlog::debug("Poll worker {} has {} intentions (out of {} total)", 
                        worker_id, my_intentions.size(), all_intentions.size());
            
            if (my_intentions.empty()) {
                // No work for this worker
                std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
                continue;
            }
            
            // Group intentions by (queue, partition, consumer_group)
            auto grouped = group_intentions(my_intentions);
            
            spdlog::debug("Poll worker {} processing {} intentions in {} groups", 
                        worker_id, my_intentions.size(), grouped.size());
            
            // Process each group
            for (auto& [key, batch] : grouped) {
                try {
                    // Determine if this is queue-based or namespace-based
                    bool is_queue_based = batch[0].is_queue_based();
                    
                    spdlog::debug("Poll worker {} processing group '{}' ({} intentions)", 
                                worker_id, key, batch.size());
                    
                    // Skip lightweight count - just try the pop directly
                    // The count was causing false positives (said messages available but pop returned 0)
                    // This happens because count doesn't account for SKIP LOCKED or active leases
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
                        
                        thread_pool->push([=]() {
                            try {
                                // Calculate total batch size for all waiting clients
                                int total_batch = 0;
                                for (const auto& intention : batch_copy) {
                                    total_batch += intention.batch_size;
                                }
                                
                                // Execute the actual pop with locking
                                PopResult result;
                                if (batch_copy[0].is_queue_based()) {
                                    result = queue_manager->pop_messages(
                                        batch_copy[0].queue_name.value(),
                                        batch_copy[0].partition_name,
                                        batch_copy[0].consumer_group,
                                        {.wait = false, .batch = total_batch}
                                    );
                                } else {
                                    result = queue_manager->pop_with_namespace_task(
                                        batch_copy[0].namespace_name,
                                        batch_copy[0].task_name,
                                        batch_copy[0].consumer_group,
                                        {.wait = false, .batch = total_batch}
                                    );
                                }
                                
                                if (!result.messages.empty()) {
                                    // Distribute messages to waiting clients
                                    auto fulfilled_ids = distribute_to_clients(result, batch_copy, response_queue);
                                    
                                    // Remove fulfilled intentions from registry AFTER responses queued
                                    for (const auto& request_id : fulfilled_ids) {
                                        registry->remove_intention(request_id);
                                    }
                                    
                                    spdlog::info("Long-poll fulfilled {} intentions with {} messages for group '{}'",
                                               fulfilled_ids.size(), result.messages.size(), group_key);
                                } else {
                                    // No messages yet - keep intentions in registry
                                    // Poll worker will try again on next cycle (100ms later)
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
        
        // Adaptive sleep based on poll interval
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
    }
    
    spdlog::info("Poll worker {} stopped", worker_id);
}

std::vector<std::string> distribute_to_clients(
    const PopResult& result,
    const std::vector<PollIntention>& batch,
    std::shared_ptr<ResponseQueue> response_queue
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
                {"traceId", msg.trace_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.trace_id)},
                {"queue", msg.queue_name},
                {"partition", msg.partition_name},
                {"data", msg.payload},  // Use "data", not "payload"
                {"retryCount", msg.retry_count},
                {"priority", msg.priority},
                {"createdAt", created_at_ss.str()},
                {"consumerGroup", nlohmann::json(nullptr)},
                {"leaseId", result.lease_id.has_value() ? nlohmann::json(*result.lease_id) : nlohmann::json(nullptr)}
            };
            
            response["messages"].push_back(msg_json);
        }
        
        // Send response to client
        response_queue->push(intention.request_id, response, false, 200);
        fulfilled.push_back(intention.request_id);
        
        spdlog::debug("Distributed {} messages to intention {}", 
                     messages_for_client, intention.request_id);
    }
    
    return fulfilled;
}

} // namespace queen

