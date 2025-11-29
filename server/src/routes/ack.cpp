#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/inter_instance_comms.hpp"
#include "queen/poll_intention_registry.hpp"
#include "queen/shared_state_manager.hpp"
#include "queen/sidecar_db_pool.hpp"
#include "queen/response_queue.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

// External globals
namespace queen {
extern std::shared_ptr<SharedStateManager> global_shared_state;
extern std::shared_ptr<ResponseRegistry> global_response_registry;
extern SidecarDbPool* global_sidecar_pool_ptr;
}

namespace queen {
namespace routes {

void setup_ack_routes(uWS::App* app, const RouteContext& ctx) {
    // ASYNC ACK batch
    app->post("/api/v1/ack/batch", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("acknowledgments") || !body["acknowledgments"].is_array()) {
                        send_error_response(res, "acknowledgments array is required", 400);
                        return;
                    }
                    
                    auto acknowledgments = body["acknowledgments"];
                    std::string consumer_group = "__QUEUE_MODE__";
                    
                    if (body.contains("consumerGroup") && !body["consumerGroup"].is_null() && body["consumerGroup"].is_string()) {
                        consumer_group = body["consumerGroup"];
                    }
                    
                    // Validate each acknowledgment has required fields
                    std::vector<nlohmann::json> ack_items;
                    for (const auto& ack_json : acknowledgments) {
                        if (!ack_json.contains("transactionId") || ack_json["transactionId"].is_null() || !ack_json["transactionId"].is_string()) {
                            send_error_response(res, "Each acknowledgment must have a valid transactionId string", 400);
                            return;
                        }
                        
                        // CRITICAL: partition_id is now MANDATORY
                        if (!ack_json.contains("partitionId") || ack_json["partitionId"].is_null() || !ack_json["partitionId"].is_string()) {
                            send_error_response(res, "Each acknowledgment must have a valid partitionId string to ensure message uniqueness", 400);
                            return;
                        }
                        
                        // Build ack item with consumer group
                        nlohmann::json ack_item = ack_json;
                        ack_item["consumerGroup"] = consumer_group;
                        ack_items.push_back(ack_item);
                    }
                    
                    spdlog::info("[Worker {}] ACK BATCH: Executing batch ACK ({} items)", ctx.worker_id, ack_items.size());
                    
                    // Use sidecar for async ACK if enabled
                    if (ctx.config.queue.ack_use_sidecar && global_sidecar_pool_ptr) {
                        std::string request_id = global_response_registry->register_response(
                            res, ctx.worker_id, nullptr
                        );
                        
                        // Build JSON array with index for result routing
                        nlohmann::json ack_json = nlohmann::json::array();
                        int idx = 0;
                        for (const auto& ack : ack_items) {
                            nlohmann::json ack_item = ack;
                            ack_item["index"] = idx++;
                            ack_json.push_back(ack_item);
                        }
                        
                        SidecarRequest req;
                        req.op_type = SidecarOpType::ACK_BATCH;
                        req.request_id = request_id;
                        req.sql = "SELECT queen.ack_messages_v2($1::jsonb)";
                        req.params = {ack_json.dump()};
                        req.worker_id = ctx.worker_id;
                        req.item_count = ack_items.size();
                        
                        global_sidecar_pool_ptr->submit(std::move(req));
                        spdlog::debug("[Worker {}] ACK BATCH: Submitted {} items to sidecar (request_id={})", 
                                     ctx.worker_id, ack_items.size(), request_id);
                        return;
                    }
                    
                    // Fallback: Execute batch ACK operation directly in uWS event loop
                    try {
                        auto ack_start = std::chrono::steady_clock::now();
                        auto batch_result = ctx.async_queue_manager->acknowledge_messages_batch(ack_items);
                        auto ack_end = std::chrono::steady_clock::now();
                        auto ack_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ack_end - ack_start).count();
                        
                        spdlog::info("[Worker {}] ACK BATCH: {} items, {}ms (success: {}, failed: {})", 
                                    ctx.worker_id, batch_result.results.size(), ack_duration_ms,
                                    batch_result.successful_acks, batch_result.failed_acks);
                        
                        // Note: Peer notification for batch ACK would require additional tracking of
                        // queue/partition per item. Currently, peer notification only happens
                        // for single ACKs. Batch ACKs still benefit from peer notifications
                        // triggered by single ACKs from other consumers.
                        
                        nlohmann::json response = {
                            {"successful", batch_result.successful_acks},
                            {"failed", batch_result.failed_acks},
                            {"results", nlohmann::json::array()}
                        };
                        
                        for (const auto& ack_result : batch_result.results) {
                            nlohmann::json result_item = {
                                {"success", ack_result.success},
                                {"message", ack_result.message}
                            };
                            if (ack_result.error.has_value()) {
                                result_item["error"] = *ack_result.error;
                            }
                            response["results"].push_back(result_item);
                        }
                        
                        send_json_response(res, response, 200);
                        spdlog::info("[Worker {}] ACK BATCH: Sent response ({} items)", ctx.worker_id, batch_result.results.size());
                        
                    } catch (const std::exception& e) {
                        send_error_response(res, e.what(), 500);
                        spdlog::error("[Worker {}] ACK BATCH: Error: {}", ctx.worker_id, e.what());
                    }
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
    
    // ASYNC Single ACK
    app->post("/api/v1/ack", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    std::string transaction_id = "";
                    if (body.contains("transactionId") && !body["transactionId"].is_null() && body["transactionId"].is_string()) {
                        transaction_id = body["transactionId"];
                    } else {
                        send_error_response(res, "transactionId is required", 400);
                        return;
                    }
                    
                    std::string status = "completed";
                    if (body.contains("status") && !body["status"].is_null() && body["status"].is_string()) {
                        status = body["status"];
                    }
                    
                    std::string consumer_group = "__QUEUE_MODE__";
                    if (body.contains("consumerGroup") && !body["consumerGroup"].is_null() && body["consumerGroup"].is_string()) {
                        consumer_group = body["consumerGroup"];
                    }
                    
                    std::optional<std::string> error;
                    if (body.contains("error") && !body["error"].is_null() && body["error"].is_string()) {
                        error = body["error"];
                    }
                    
                    std::optional<std::string> lease_id;
                    if (body.contains("leaseId") && !body["leaseId"].is_null() && body["leaseId"].is_string()) {
                        lease_id = body["leaseId"];
                    }
                    
                    // CRITICAL: partition_id is now MANDATORY to prevent acking wrong message
                    // when transactionId is not unique across partitions
                    std::optional<std::string> partition_id;
                    if (body.contains("partitionId") && !body["partitionId"].is_null() && body["partitionId"].is_string()) {
                        partition_id = body["partitionId"];
                    } else {
                        send_error_response(res, "partitionId is required to ensure message uniqueness", 400);
                        return;
                    }
                    
                    spdlog::info("[Worker {}] ACK: Executing ACK", ctx.worker_id);
                    
                    // Use sidecar for async ACK if enabled
                    if (ctx.config.queue.ack_use_sidecar && global_sidecar_pool_ptr) {
                        std::string request_id = global_response_registry->register_response(
                            res, ctx.worker_id, nullptr
                        );
                        
                        // Build JSON array with single ACK
                        nlohmann::json ack_json = nlohmann::json::array();
                        ack_json.push_back({
                            {"index", 0},
                            {"transactionId", transaction_id},
                            {"partitionId", partition_id.value()},
                            {"leaseId", lease_id.value_or("")},
                            {"status", status},
                            {"consumerGroup", consumer_group},
                            {"error", error.value_or("")}
                        });
                        
                        SidecarRequest req;
                        req.op_type = SidecarOpType::ACK_BATCH;  // Use batch procedure even for single ACK
                        req.request_id = request_id;
                        req.sql = "SELECT queen.ack_messages_v2($1::jsonb)";
                        req.params = {ack_json.dump()};
                        req.worker_id = ctx.worker_id;
                        req.item_count = 1;
                        
                        global_sidecar_pool_ptr->submit(std::move(req));
                        spdlog::debug("[Worker {}] ACK: Submitted to sidecar (request_id={})", 
                                     ctx.worker_id, request_id);
                        return;
                    }
                    
                    // Fallback: Execute ACK operation directly in uWS event loop
                    try {
                        auto ack_start = std::chrono::steady_clock::now();
                        auto ack_result = ctx.async_queue_manager->acknowledge_message(transaction_id, status, error, consumer_group, lease_id, partition_id);
                        auto ack_end = std::chrono::steady_clock::now();
                        auto ack_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ack_end - ack_start).count();
                        
                        spdlog::debug("[Worker {}] ACK: 1 item, {}ms", ctx.worker_id, ack_duration_ms);
                        
                        if (ack_result.success) {
                            // Notify local poll registry + peers that partition is free
                            if (ack_result.queue_name.has_value() &&
                                ack_result.partition_name.has_value()) {
                                
                                // Notify LOCAL poll registry (this server's own poll workers)
                                if (queen::global_poll_intention_registry) {
                                    std::string group_key = *ack_result.queue_name + ":" +
                                                           *ack_result.partition_name + ":" +
                                                           consumer_group;
                                    queen::global_poll_intention_registry->reset_backoff_for_group(group_key);
                                    
                                    // Also reset wildcard partition
                                    std::string wildcard_key = *ack_result.queue_name + ":*:" + consumer_group;
                                    queen::global_poll_intention_registry->reset_backoff_for_group(wildcard_key);
                                    
                                    spdlog::info("[Worker {}] ACK: Notify local poll workers for {}:{}:{}", 
                                                ctx.worker_id, *ack_result.queue_name, *ack_result.partition_name, consumer_group);
                                }
                                
                                // Notify PEER servers via WebSocket
                                if (global_inter_instance_comms && global_inter_instance_comms->is_enabled()) {
                                    global_inter_instance_comms->notify_partition_free(
                                        *ack_result.queue_name,
                                        *ack_result.partition_name,
                                        consumer_group
                                    );
                                    spdlog::info("[Worker {}] ACK: Notify peers for {}:{}:{}", 
                                                ctx.worker_id, *ack_result.queue_name, *ack_result.partition_name, consumer_group);
                                }
                                
                                // Phase 5: Broadcast lease release hint
                                if (partition_id.has_value() && global_shared_state && global_shared_state->is_enabled()) {
                                    global_shared_state->hint_lease_released(*partition_id, consumer_group);
                                    spdlog::trace("[Worker {}] ACK: Broadcast lease release hint for {}:{}", 
                                                 ctx.worker_id, *partition_id, consumer_group);
                                }
                            }
                            
                            auto now = std::chrono::system_clock::now();
                            std::string ack_timestamp = format_timestamp_iso8601(now);
                            
                            nlohmann::json response = {
                                {"transactionId", transaction_id},
                                {"status", ack_result.message},
                                {"consumerGroup", consumer_group == "__QUEUE_MODE__" ? nlohmann::json(nullptr) : nlohmann::json(consumer_group)},
                                {"acknowledgedAt", ack_timestamp}
                            };
                            
                            send_json_response(res, response, 200);
                            spdlog::info("[Worker {}] ACK: Sent success response", ctx.worker_id);
                        } else {
                            nlohmann::json error_response = {{"error", "Failed to acknowledge message: " + ack_result.message}};
                            send_json_response(res, error_response, 500);
                            spdlog::warn("[Worker {}] ACK: Sent error response: {}", ctx.worker_id, ack_result.message);
                        }
                        
                    } catch (const std::exception& e) {
                        send_error_response(res, e.what(), 500);
                        spdlog::error("[Worker {}] ACK: Error: {}", ctx.worker_id, e.what());
                    }
                    
                } catch (const std::exception& e) {
                    send_error_response(res, e.what(), 500);
                }
            },
            [res](const std::string& error) {
                send_error_response(res, error, 400);
            }
        );
    });
}

} // namespace routes
} // namespace queen

