#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/queue_types.hpp"
#include "queen/file_buffer.hpp"
#include "queen/response_queue.hpp"
#include "queen/sidecar_db_pool.hpp"
#include "queen/encryption.hpp"
#include "queen/shared_state_manager.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <set>

namespace queen {

// External globals (declared in acceptor_server.cpp)
extern std::vector<std::shared_ptr<ResponseRegistry>> worker_response_registries;
extern std::shared_ptr<SharedStateManager> global_shared_state;

namespace routes {

void setup_push_routes(uWS::App* app, const RouteContext& ctx) {
    // PUSH endpoint - uses per-worker sidecar for true async non-blocking operation
    app->post("/api/v1/push", [ctx](auto* res, auto* req) {
        (void)req;
        read_json_body(res,
            [res, ctx](const nlohmann::json& body) {
                try {
                    if (!body.contains("items") || !body["items"].is_array()) {
                        send_error_response(res, "items array is required", 400);
                        return;
                    }
                    
                    size_t item_count = body["items"].size();
                    spdlog::debug("[Worker {}] PUSH: Processing {} items", ctx.worker_id, item_count);
                    
                    // Parse and validate items
                    std::vector<PushItem> items;
                    for (const auto& item_json : body["items"]) {
                        // Validate partition type if present
                        if (item_json.contains("partition") && !item_json["partition"].is_string()) {
                            send_error_response(res, "Partition must be a string", 400);
                            return;
                        }
                        
                        // Validate queue type
                        if (!item_json.contains("queue") || !item_json["queue"].is_string()) {
                            send_error_response(res, "Queue must be a string", 400);
                            return;
                        }
                        
                        // Validate transactionId type if present
                        if (item_json.contains("transactionId") && !item_json["transactionId"].is_string()) {
                            send_error_response(res, "TransactionId must be a string", 400);
                            return;
                        }
                        
                        // Validate traceId type if present
                        if (item_json.contains("traceId") && !item_json["traceId"].is_string()) {
                            send_error_response(res, "TraceId must be a string", 400);
                            return;
                        }
                        
                        PushItem item;
                        item.queue = item_json["queue"];
                        item.partition = item_json.value("partition", "Default");
                        item.payload = item_json.value("payload", nlohmann::json{});
                        if (item_json.contains("transactionId")) {
                            item.transaction_id = item_json["transactionId"];
                        }
                        if (item_json.contains("traceId")) {
                            item.trace_id = item_json["traceId"];
                        }
                        items.push_back(std::move(item));
                    }
                    
                    // Handle empty push immediately - no need for DB call
                    if (items.empty()) {
                        send_json_response(res, nlohmann::json::array(), 201);
                        return;
                    }
                    
                    // MAINTENANCE MODE: Route to file buffer instead of sidecar
                    // Uses global_shared_state for zero-cost cached check (atomic bool read)
                    // Cache is refreshed periodically by SharedStateManager background thread
                    if (global_shared_state && global_shared_state->get_maintenance_mode() && ctx.file_buffer) {
                        spdlog::debug("[Worker {}] PUSH: Maintenance mode active, buffering {} items", 
                                     ctx.worker_id, items.size());
                        
                        nlohmann::json results = nlohmann::json::array();
                        bool all_buffered = true;
                        
                        for (const auto& item : items) {
                            nlohmann::json event = {
                                {"queue", item.queue},
                                {"partition", item.partition},
                                {"payload", item.payload},
                                {"failover", true}
                            };
                            
                            if (item.transaction_id.has_value() && !item.transaction_id->empty()) {
                                event["transactionId"] = *item.transaction_id;
                            } else {
                                event["transactionId"] = ctx.async_queue_manager->generate_uuid();
                            }
                            
                            if (item.trace_id.has_value() && !item.trace_id->empty()) {
                                event["traceId"] = *item.trace_id;
                            }
                            
                            if (ctx.file_buffer->write_event(event)) {
                                nlohmann::json result = {
                                    {"status", "buffered"},
                                    {"queue", item.queue},
                                    {"partition", item.partition}
                                };
                                if (item.transaction_id.has_value()) {
                                    result["transactionId"] = *item.transaction_id;
                                }
                                results.push_back(result);
                            } else {
                                all_buffered = false;
                                results.push_back({
                                    {"status", "failed"},
                                    {"queue", item.queue},
                                    {"partition", item.partition},
                                    {"error", "File buffer write failed"}
                                });
                            }
                        }
                        
                        spdlog::info("[Worker {}] PUSH: Buffered {} items during maintenance mode", 
                                    ctx.worker_id, items.size());
                        
                        send_json_response(res, results, all_buffered ? 201 : 500);
                        return;
                    }
                    
                    // Register response for async delivery (per-worker registry - no contention!)
                    std::string request_id = worker_response_registries[ctx.worker_id]->register_response(
                        res, ctx.worker_id, nullptr
                    );
                        
                    // Build JSON array for stored procedure
                    // Generate UUIDv7 message IDs on C++ side
                    // Get encryption service once for all items
                    EncryptionService* enc_service = get_encryption_service();
                    
                    nlohmann::json items_json = nlohmann::json::array();
                    for (const auto& item : items) {
                        // Check if queue has encryption enabled
                        // Note: SharedStateManager caches queue configs even when inter-instance sync is disabled
                        bool queue_encryption_enabled = false;
                        if (global_shared_state) {
                            auto cached_config = global_shared_state->get_queue_config(item.queue);
                            if (cached_config) {
                                queue_encryption_enabled = cached_config->encryption_enabled;
                            } else {
                                spdlog::warn("[Worker {}] PUSH: Queue '{}' config NOT in cache!", 
                                            ctx.worker_id, item.queue);
                            }
                        } else {
                            spdlog::warn("[Worker {}] PUSH: global_shared_state is NULL!", ctx.worker_id);
                        }
                        
                        // Encrypt payload if queue requires it
                        nlohmann::json payload_to_store = item.payload;
                        bool is_encrypted = false;
                        
                        if (queue_encryption_enabled && enc_service && enc_service->is_enabled()) {
                            std::string payload_str = item.payload.dump();
                            auto encrypted = enc_service->encrypt_payload(payload_str);
                            if (encrypted.has_value()) {
                                payload_to_store = {
                                    {"encrypted", encrypted->encrypted},
                                    {"iv", encrypted->iv},
                                    {"authTag", encrypted->auth_tag}
                                };
                                is_encrypted = true;
                            } else {
                                spdlog::warn("[Worker {}] PUSH: Encryption failed for queue '{}', storing plaintext", 
                                            ctx.worker_id, item.queue);
                            }
                        } else if (queue_encryption_enabled) {
                            spdlog::warn("[Worker {}] PUSH: Queue '{}' requires encryption but service not available", 
                                        ctx.worker_id, item.queue);
                        }
                        
                            nlohmann::json item_json = {
                                {"queue", item.queue},
                                {"partition", item.partition},
                                {"payload", payload_to_store},
                            {"is_encrypted", is_encrypted},
                                {"messageId", ctx.async_queue_manager->generate_uuid()}  // UUIDv7
                            };
                            if (item.transaction_id.has_value() && !item.transaction_id->empty()) {
                                item_json["transactionId"] = *item.transaction_id;
                            }
                            if (item.trace_id.has_value() && !item.trace_id->empty()) {
                                item_json["traceId"] = *item.trace_id;
                            }
                            items_json.push_back(item_json);
                        }
                        
                        // Build sidecar request
                    SidecarRequest sidecar_req;
                    sidecar_req.op_type = SidecarOpType::PUSH;
                    sidecar_req.request_id = request_id;
                    sidecar_req.sql = "SELECT queen.push_messages_v2($1::jsonb, $2::boolean, $3::boolean)";
                    sidecar_req.params = {items_json.dump(), "true", "true"};
                    sidecar_req.item_count = items.size();
                        
                    // Collect unique queue/partition pairs for notification and logging
                    std::set<std::pair<std::string, std::string>> targets;
                    for (const auto& item : items) {
                        targets.insert({item.queue, item.partition});
                    }
                    
                    // Store targets for notification after successful push
                    for (const auto& [queue, partition] : targets) {
                        sidecar_req.push_targets.push_back({queue, partition});
                    }
                    
                    // Store items for failover before submitting to sidecar
                    // If sidecar fails (DB down), callback will retrieve and write to file buffer
                    if (ctx.push_failover_storage) {
                        ctx.push_failover_storage->store(request_id, items_json.dump());
                    }
                    
                    // Submit to per-worker sidecar - RETURNS IMMEDIATELY!
                    ctx.sidecar->submit(std::move(sidecar_req));
                        
                    // Don't send response here - sidecar will deliver it via loop->defer()
                    
                    // TESTING: Send immediate 200 response
                    //send_json_response(res, nlohmann::json::array(), 200);
                    //return;
                    
                } catch (const std::exception& e) {
                    spdlog::error("[Worker {}] PUSH: Error: {}", ctx.worker_id, e.what());
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
