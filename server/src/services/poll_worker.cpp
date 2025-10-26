#include "queen/poll_worker.hpp"
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>

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
    spdlog::info("=== INITIALIZING LONG-POLLING WITH {} POLL WORKERS ===", worker_count);
    spdlog::info("ThreadPool size: {}", thread_pool->pool_size());
    spdlog::info("Registry running: {}", registry->is_running());
    
    // Push never-returning jobs to ThreadPool to reserve worker threads
    for (int worker_id = 0; worker_id < worker_count; worker_id++) {
        spdlog::info("Submitting poll worker {} to ThreadPool...", worker_id);
        thread_pool->push([=]() {
            spdlog::info("=== POLL WORKER {} LAMBDA EXECUTING ===", worker_id);
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
    
    spdlog::info("=== Long-polling: {} poll workers submitted to ThreadPool ===", worker_count);
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
    while (registry->is_running()) {
        try {
            loop_count++;
            
            // Get all active intentions
            auto all_intentions = registry->get_active_intentions();
            
            // Log every 100 loops or when we have intentions
            if (loop_count % 100 == 0 || (!all_intentions.empty() && loop_count % 10 == 0)) {
                spdlog::debug("Poll worker {} loop {}: registry has {} total intentions", 
                           worker_id, loop_count, all_intentions.size());
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
                    
                    spdlog::debug("Poll worker {} checking group '{}' ({} intentions)", 
                                worker_id, key, batch.size());
                    
                    // Lightweight check: count available messages
                    int available = 0;
                    if (is_queue_based) {
                        available = queue_manager->count_available_messages(
                            batch[0].queue_name,
                            batch[0].partition_name,
                            batch[0].consumer_group
                        );
                        if (available > 0) {
                            spdlog::info("Poll worker {} found {} available messages for queue={} partition={} consumer_group={}", 
                                        worker_id, available, 
                                        batch[0].queue_name.value_or("*"),
                                        batch[0].partition_name.value_or("*"),
                                        batch[0].consumer_group);
                        }
                    } else {
                        available = queue_manager->count_available_messages_namespace(
                            batch[0].namespace_name,
                            batch[0].task_name,
                            batch[0].consumer_group
                        );
                        if (available > 0) {
                            spdlog::info("Poll worker {} found {} available messages for namespace={} task={} consumer_group={}", 
                                        worker_id, available,
                                        batch[0].namespace_name.value_or("*"),
                                        batch[0].task_name.value_or("*"),
                                        batch[0].consumer_group);
                        }
                    }
                    
                    if (available > 0) {
                        // Messages available! Submit actual pop to ThreadPool with priority
                        std::string job_id = "pop_" + generate_unique_id();
                        
                        spdlog::info("Poll worker {} submitting pop job for group '{}' with {} intentions",
                                    worker_id, key, batch.size());
                        
                        // IMPORTANT: Remove intentions BEFORE submitting job to prevent duplicate processing
                        // If the pop fails (e.g., another server got the messages), client will timeout and retry
                        for (const auto& intention : batch) {
                            registry->remove_intention(intention.request_id);
                        }
                        
                        // Capture batch by value for the lambda
                        auto batch_copy = batch;
                        
                        thread_pool->dg_now(job_id, [=]() {
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
                                    
                                    spdlog::info("Long-poll fulfilled {} intentions with {} messages for group '{}'",
                                               fulfilled_ids.size(), result.messages.size(), key);
                                } else {
                                    // No messages (e.g., race condition - consumed between check and pop)
                                    // Send empty 204 response to all waiting clients
                                    spdlog::debug("Pop returned no messages for group '{}' - sending 204 to {} clients (race condition)",
                                                key, batch_copy.size());
                                    
                                    nlohmann::json empty_response;
                                    for (const auto& intention : batch_copy) {
                                        response_queue->push(intention.request_id, empty_response, false, 204);
                                    }
                                }
                            } catch (const std::exception& e) {
                                spdlog::error("Poll worker {} pop job error: {}", worker_id, e.what());
                            }
                        });
                    }
                    
                } catch (const std::exception& e) {
                    spdlog::error("Poll worker {} group processing error: {}", worker_id, e.what());
                }
            }
            
            // Check for expired intentions (timeouts)
            auto now = std::chrono::steady_clock::now();
            for (const auto& intention : my_intentions) {
                if (now >= intention.deadline) {
                    // Timeout - send empty response (204 No Content)
                    nlohmann::json empty_response;
                    response_queue->push(intention.request_id, empty_response, false, 204);
                    registry->remove_intention(intention.request_id);
                    
                    spdlog::debug("Poll worker {} timed out intention {}", 
                                 worker_id, intention.request_id);
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

