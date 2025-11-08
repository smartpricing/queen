#include "queen/stream_manager.hpp"
#include "queen/async_database.hpp"
#include <App.h>
#include <spdlog/spdlog.h>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace queen {

StreamManager::StreamManager(
    std::shared_ptr<AsyncDbPool> db_pool,
    std::shared_ptr<astp::ThreadPool> db_thread_pool,
    const std::vector<std::shared_ptr<ResponseQueue>>& worker_response_queues,
    std::shared_ptr<StreamPollIntentionRegistry> intention_registry,
    std::shared_ptr<ResponseRegistry> response_registry
)
    : db_pool_(db_pool),
      db_thread_pool_(db_thread_pool),
      worker_response_queues_(worker_response_queues),
      stream_intention_registry_(intention_registry),
      response_registry_(response_registry) {
    spdlog::info("StreamManager initialized");
}

StreamManager::~StreamManager() {
    spdlog::info("StreamManager shutting down");
}

// ============================================================================
// HTTP Handlers
// ============================================================================

void StreamManager::handle_define(uWS::HttpResponse<false>* res, uWS::HttpRequest* /* req */, int worker_id) {
    read_json_body(res,
        [this, res, worker_id](const nlohmann::json& body) {
            // Register response in uWebSockets thread
            std::string request_id = response_registry_->register_response(res, worker_id);
            
            spdlog::info("[Worker {}] Stream define request {}", worker_id, request_id);
            
            // Execute in thread pool
            db_thread_pool_->push([this, request_id, worker_id, body]() {
                auto conn = db_pool_->acquire();
                
                try {
                    // Extract parameters
                    std::string name = body.value("name", "");
                    std::string namespace_name = body.value("namespace", "");
                    bool partitioned = body.value("partitioned", false);
                    std::string window_type = body.value("window_type", "tumbling");
                    int64_t window_duration_ms = body.value("window_duration_ms", 60000);
                    int64_t window_grace_period_ms = body.value("window_grace_period_ms", 30000);
                    int64_t window_lease_timeout_ms = body.value("window_lease_timeout_ms", 60000);
                    std::vector<std::string> source_queue_names = body.value("source_queue_names", std::vector<std::string>{});
                    
                    if (name.empty() || namespace_name.empty()) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Missing name or namespace"}}, true, 400);
                        return;
                    }
                    
                    try {
                        sendAndWait(conn.get(), "BEGIN");
                        getCommandResult(conn.get());
                    } catch (const std::exception& e) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Failed to begin transaction"}}, true, 500);
                        return;
                    }
                    
                    // Q1: Create/Update Stream
                    std::string create_stream_sql = R"(
                        INSERT INTO queen.streams (
                            name, namespace, partitioned, window_type, 
                            window_duration_ms, window_grace_period_ms, window_lease_timeout_ms
                        )
                        VALUES ($1, $2, $3, $4, $5, $6, $7)
                        ON CONFLICT (name) 
                        DO UPDATE SET
                            namespace = EXCLUDED.namespace,
                            partitioned = EXCLUDED.partitioned,
                            window_type = EXCLUDED.window_type,
                            window_duration_ms = EXCLUDED.window_duration_ms,
                            window_grace_period_ms = EXCLUDED.window_grace_period_ms,
                            window_lease_timeout_ms = EXCLUDED.window_lease_timeout_ms,
                            updated_at = NOW()
                        RETURNING id
                    )";
                    
                    sendQueryParamsAsync(conn.get(), create_stream_sql,
                        {name, namespace_name, partitioned ? "true" : "false", window_type,
                         std::to_string(window_duration_ms), std::to_string(window_grace_period_ms), 
                         std::to_string(window_lease_timeout_ms)}
                    );
                    auto stream_result = getTuplesResult(conn.get());
                    
                    if (PQntuples(stream_result.get()) == 0) {
                        sendAndWait(conn.get(), "ROLLBACK");
                        getCommandResult(conn.get());
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Failed to create stream"}}, true, 500);
                        return;
                    }
                    
                    std::string stream_id = PQgetvalue(stream_result.get(), 0, 0);
                    
                    // Q2: Link Stream to Queues
                    for (const auto& queue_name : source_queue_names) {
                        std::string link_sql = R"(
                            INSERT INTO queen.stream_sources (stream_id, queue_id)
                            SELECT $1, q.id FROM queen.queues q WHERE q.name = $2
                            ON CONFLICT (stream_id, queue_id) DO NOTHING
                        )";
                        try {
                            sendQueryParamsAsync(conn.get(), link_sql, {stream_id, queue_name});
                            getCommandResult(conn.get());
                        } catch (const std::exception& e) {
                            spdlog::warn("Failed to link queue {} to stream {}: {}", queue_name, name, e.what());
                        }
                    }
                    
                    try {
                        sendAndWait(conn.get(), "COMMIT");
                        getCommandResult(conn.get());
                    } catch (const std::exception& e) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Failed to commit transaction"}}, true, 500);
                        return;
                    }
                    
                    nlohmann::json response = {
                        {"success", true},
                        {"stream_id", stream_id},
                        {"name", name}
                    };
                    
                    worker_response_queues_[worker_id]->push(request_id, response, false, 201);
                    spdlog::info("[Worker {}] Stream defined: {}", worker_id, name);
                    
                } catch (const std::exception& e) {
                    spdlog::error("Failed to define stream: {}", e.what());
                    try {
                        sendAndWait(conn.get(), "ROLLBACK");
                        getCommandResult(conn.get());
                    } catch (...) {}
                    worker_response_queues_[worker_id]->push(request_id, {{"error", std::string("Failed: ") + e.what()}}, true, 500);
                }
            });
        },
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        });
}

void StreamManager::handle_poll(uWS::HttpResponse<false>* res, uWS::HttpRequest* /* req */, int worker_id) {
    read_json_body(res,
        [this, res, worker_id](const nlohmann::json& body) {
            // Extract poll parameters  
            std::string stream_name = body.value("streamName", "");
            std::string consumer_group = body.value("consumerGroup", "");
            int timeout = body.value("timeout", 30000);
            
            if (stream_name.empty() || consumer_group.empty()) {
                send_error_response(res, "Missing streamName or consumerGroup", 400);
                return;
            }
            
            // Register response with abort callback to clean up intention on disconnect
            std::string request_id = response_registry_->register_response(res, worker_id,
                [this](const std::string& req_id) {
                    // Remove intention from registry when connection aborts
                    stream_intention_registry_->remove_intention(req_id);
                    spdlog::info("Stream Poll: Connection aborted, removed stream poll intention {}", req_id);
                });
            
            spdlog::info("[Worker {}] Stream poll request {}: stream={}, group={}, timeout={}ms", 
                         worker_id, request_id, stream_name, consumer_group, timeout);
            
            // Try to find a window immediately
            db_thread_pool_->push([this, request_id, worker_id, stream_name, consumer_group, timeout]() {
                auto conn = db_pool_->acquire();
                
                try {
                    // Get stream
                    auto stream_opt = get_stream(conn.get(), stream_name);
                    if (!stream_opt.has_value()) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Stream not found"}}, true, 404);
                        return;
                    }
                    
                    auto stream = stream_opt.value();
                    std::string watermark_str = get_watermark(conn.get(), stream.id);
                    auto partitions = get_partitions_and_offsets(conn.get(), stream.id, consumer_group, stream.partitioned);
                    
                    spdlog::debug("Poll check: stream={}, watermark={}, partitions={}", stream_name, watermark_str, partitions.size());
                    
                    // Try to find a ready window
                    for (const auto& partition : partitions) {
                        auto stream_key = partition.stream_key;
                        auto partition_name = partition.partition_name;
                        auto last_end_str = partition.last_acked_window_end;
                        
                        std::string window_start_str;
                        
                        if (last_end_str == "-infinity") {
                            auto first_time_opt = get_first_message_time(conn.get(), stream.id, stream_key, stream.partitioned);
                            if (!first_time_opt.has_value()) {
                                spdlog::debug("No messages yet for key={}", stream_key);
                                continue;
                            }
                            window_start_str = align_to_boundary(first_time_opt.value(), stream.window_duration_ms);
                        } else {
                            window_start_str = last_end_str;
                        }
                        
                        auto window_start = parse_timestamp(window_start_str);
                        auto window_end = window_start + std::chrono::milliseconds(stream.window_duration_ms);
                        std::string window_end_str = format_timestamp(window_end);
                        
                        auto watermark = parse_timestamp(watermark_str);
                        auto grace_boundary = window_end + std::chrono::milliseconds(stream.window_grace_period_ms);
                        
                        spdlog::debug("Window check: key={}, start={}, end={}, watermark={}, grace_boundary={}", 
                                     stream_key, window_start_str, window_end_str, watermark_str, format_timestamp(grace_boundary));
                        
                        if (watermark < grace_boundary) {
                            spdlog::debug("Window not ready: watermark < grace_boundary");
                            continue;
                        }
                        
                    // Delete any expired leases for this window first (cleanup)
                    std::string cleanup_sql = R"(
DELETE FROM queen.stream_leases 
                        WHERE stream_id = $1::UUID
                          AND consumer_group = $2
                          AND stream_key = $3
                          AND window_start = $4::TIMESTAMPTZ
                          AND lease_expires_at < NOW()
                    )";
                    sendQueryParamsAsync(conn.get(), cleanup_sql, {stream.id, consumer_group, stream_key, window_start_str});
                    getCommandResult(conn.get());
                    
                    // Now check if there's a valid (non-expired) lease
                    if (check_lease_exists(conn.get(), stream.id, consumer_group, stream_key, window_start_str)) {
                        spdlog::debug("Window already leased");
                        continue;
                    }
                    
                    // Found a ready window! Try to create lease
                    try {
                        std::string lease_id = create_lease(conn.get(), stream.id, consumer_group, stream_key,
                                                            window_start_str, window_end_str, stream.window_lease_timeout_ms);
                        
                        auto messages = get_messages(conn.get(), stream.id, stream_key, stream.partitioned, 
                                                     window_start_str, window_end_str);
                        
                        nlohmann::json window_json = {
                            {"id", generate_window_id(stream.id, partition_name, window_start_str, window_end_str)},
                            {"leaseId", lease_id},
                            {"key", partition_name},
                            {"start", window_start_str},
                            {"end", window_end_str},
                            {"messages", messages}
                        };
                        
                        nlohmann::json response = {{"window", window_json}};
                        worker_response_queues_[worker_id]->push(request_id, response, false, 200);
                        
                        spdlog::info("[Worker {}] Delivered window {}: stream={}, group={}, key={}, messages={}", 
                                     worker_id, request_id, stream_name, consumer_group, partition_name, messages.size());
                        return;
                        
                    } catch (const std::runtime_error& e) {
                        // Check if it's a duplicate key error (another consumer got this window first)
                        std::string error_msg = e.what();
                        if (error_msg.find("duplicate key") != std::string::npos || 
                            error_msg.find("unique constraint") != std::string::npos) {
                            // Another consumer won the race for this window - continue to next partition
                            spdlog::debug("Window already leased by another consumer (race condition), trying next partition");
                            continue;
                        }
                        // Other errors - log and continue
                        spdlog::error("Error creating lease/delivering window: {}", e.what());
                        continue;
                    }
                }
                
                // No window ready immediately - register long-poll intention
                spdlog::debug("[Worker {}] No windows ready immediately for stream={}, group={} - registering long-poll intention", 
                             worker_id, stream_name, consumer_group);
                
                StreamPollIntention intention{
                    .request_id = request_id,
                    .worker_id = worker_id,
                    .stream_name = stream_name,
                    .consumer_group = consumer_group,
                    .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout),
                    .created_at = std::chrono::steady_clock::now()
                };
                
                stream_intention_registry_->register_intention(intention);
                
                spdlog::debug("[Worker {}] Registered stream poll intention {} for stream={}, group={}, timeout={}ms",
                             worker_id, request_id, stream_name, consumer_group, timeout);
                
            } catch (const std::exception& e) {
                spdlog::error("[Worker {}] Error in poll handler: {}", worker_id, e.what());
                worker_response_queues_[worker_id]->push(request_id, {{"error", std::string("Poll error: ") + e.what()}}, true, 500);
            }
        });
        },
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        });
}

void StreamManager::handle_ack(uWS::HttpResponse<false>* res, uWS::HttpRequest* /* req */, int worker_id) {
    read_json_body(res,
        [this, res, worker_id](const nlohmann::json& body) {
            // Register response
            std::string request_id = response_registry_->register_response(res, worker_id);
            
            // Execute in thread pool
            db_thread_pool_->push([this, request_id, worker_id, body]() {
                auto conn = db_pool_->acquire();
                
                try {
                    std::string lease_id = body.value("leaseId", "");
                    bool success = body.value("success", true);
                    
                    if (lease_id.empty()) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Missing leaseId"}}, true, 400);
                        return;
                    }
                    
                    try {
                        sendAndWait(conn.get(), "BEGIN");
                        getCommandResult(conn.get());
                    } catch (const std::exception& e) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Failed to begin transaction"}}, true, 500);
                        return;
                    }
                    
                    // Q9: Validate Lease
                    std::string validate_sql = R"(
                        SELECT stream_id, consumer_group, stream_key, window_start, window_end
                        FROM queen.stream_leases
                        WHERE lease_id = $1::UUID AND lease_expires_at > NOW()
                    )";
                    
                    sendQueryParamsAsync(conn.get(), validate_sql, {lease_id});
                    auto lease_result = getTuplesResult(conn.get());
                    
                    if (PQntuples(lease_result.get()) == 0) {
                        try {
                        sendAndWait(conn.get(), "ROLLBACK");
                        getCommandResult(conn.get());
                    } catch (...) {}
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Invalid or expired lease"}}, true, 400);
                        return;
                    }
                    
                    std::string stream_id = PQgetvalue(lease_result.get(), 0, PQfnumber(lease_result.get(), "stream_id"));
                    std::string consumer_group = PQgetvalue(lease_result.get(), 0, PQfnumber(lease_result.get(), "consumer_group"));
                    std::string stream_key = PQgetvalue(lease_result.get(), 0, PQfnumber(lease_result.get(), "stream_key"));
                    std::string window_end = PQgetvalue(lease_result.get(), 0, PQfnumber(lease_result.get(), "window_end"));
                    
                    if (success) {
                        // Q10: ACK Window
                        std::string ack_sql = R"(
                            INSERT INTO queen.stream_consumer_offsets (
                                stream_id, consumer_group, stream_key,
                                last_acked_window_end, 
                                total_windows_consumed, last_consumed_at
                            )
                            VALUES ($1::UUID, $2, $3, $4::TIMESTAMPTZ, 1, NOW())
                            ON CONFLICT (stream_id, consumer_group, stream_key)
                            DO UPDATE SET
                                last_acked_window_end = EXCLUDED.last_acked_window_end,
                                total_windows_consumed = stream_consumer_offsets.total_windows_consumed + 1,
                                last_consumed_at = NOW()
                        )";
                        
                        sendQueryParamsAsync(conn.get(), ack_sql, {stream_id, consumer_group, stream_key, window_end});
                        getCommandResult(conn.get());  // INSERT...ON CONFLICT returns COMMAND_OK, not TUPLES_OK
                    }
                    
                    // Q11: Delete Lease
                    std::string delete_lease_sql = "DELETE FROM queen.stream_leases WHERE lease_id = $1::UUID";
                    sendQueryParamsAsync(conn.get(), delete_lease_sql, {lease_id});
                    getCommandResult(conn.get());
                    
                    try {
                        sendAndWait(conn.get(), "COMMIT");
                        getCommandResult(conn.get());
                    } catch (const std::exception& e) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Failed to commit transaction"}}, true, 500);
                        return;
                    }
                    
                    nlohmann::json response = {
                        {"success", true},
                        {"acked", success}
                    };
                    
                    worker_response_queues_[worker_id]->push(request_id, response, false, 200);
                    spdlog::info("[Worker {}] ACK processed: lease={}, success={}", worker_id, lease_id, success);
                    
                } catch (const std::exception& e) {
                    spdlog::error("[Worker {}] Failed to ack window: {}", worker_id, e.what());
                    try {
                        sendAndWait(conn.get(), "ROLLBACK");
                        getCommandResult(conn.get());
                    } catch (...) {}
                    worker_response_queues_[worker_id]->push(request_id, {{"error", std::string("ACK error: ") + e.what()}}, true, 500);
                }
            });
        },
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        });
}

void StreamManager::handle_renew(uWS::HttpResponse<false>* res, uWS::HttpRequest* /* req */, int worker_id) {
    read_json_body(res,
        [this, res, worker_id](const nlohmann::json& body) {
            // Register response
            std::string request_id = response_registry_->register_response(res, worker_id);
            
            // Execute in thread pool
            db_thread_pool_->push([this, request_id, worker_id, body]() {
                auto conn = db_pool_->acquire();
                
                try {
                    std::string lease_id = body.value("leaseId", "");
                    int64_t extend_ms = body.value("extend_ms", 30000);
                    
                    if (lease_id.empty()) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Missing leaseId"}}, true, 400);
                        return;
                    }
                    
                    // Q12: Renew Lease
                    std::string renew_sql = R"(
 UPDATE queen.stream_leases 
                        SET lease_expires_at = NOW() + ($2 || ' milliseconds')::interval
                        WHERE lease_id = $1::UUID AND lease_expires_at > NOW()
                        RETURNING lease_expires_at
                    )";
                    
                    sendQueryParamsAsync(conn.get(), renew_sql, {lease_id, std::to_string(extend_ms)});
                    auto result = getTuplesResult(conn.get());
                    
                    if (PQntuples(result.get()) == 0) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Lease not found or expired"}}, true, 404);
                        return;
                    }
                    
                    nlohmann::json response = {
                        {"success", true},
                        {"lease_expires_at", PQgetvalue(result.get(), 0, 0)}
                    };
                    
                    worker_response_queues_[worker_id]->push(request_id, response, false, 200);
                    
                } catch (const std::exception& e) {
                    spdlog::error("[Worker {}] Failed to renew lease: {}", worker_id, e.what());
                    worker_response_queues_[worker_id]->push(request_id, {{"error", std::string("Renew error: ") + e.what()}}, true, 500);
                }
            });
        },
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        });
}

void StreamManager::handle_seek(uWS::HttpResponse<false>* res, uWS::HttpRequest* /* req */, int worker_id) {
    read_json_body(res,
        [this, res, worker_id](const nlohmann::json& body) {
            // Register response
            std::string request_id = response_registry_->register_response(res, worker_id);
            
            // Execute in thread pool
            db_thread_pool_->push([this, request_id, worker_id, body]() {
                auto conn = db_pool_->acquire();
                
                try {
                    std::string stream_name = body.value("streamName", "");
                    std::string consumer_group = body.value("consumerGroup", "");
                    std::string seek_timestamp = body.value("timestamp", "");
                    
                    if (stream_name.empty() || consumer_group.empty() || seek_timestamp.empty()) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Missing required parameters"}}, true, 400);
                        return;
                    }
                    
                    auto stream_opt = get_stream(conn.get(), stream_name);
                    if (!stream_opt.has_value()) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Stream not found"}}, true, 404);
                        return;
                    }
                    
                    std::string stream_id = stream_opt->id;
                    
                    try {
                        sendAndWait(conn.get(), "BEGIN");
                        getCommandResult(conn.get());
                    } catch (const std::exception& e) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Failed to begin transaction"}}, true, 500);
                        return;
                    }
                    
                    // Q13: Seek Offset
                    std::string seek_sql = R"(
                        INSERT INTO queen.stream_consumer_offsets (
                            stream_id, consumer_group, stream_key,
                            last_acked_window_end
                        )
                        SELECT 
                            ss.stream_id, $2, p.id::TEXT, $3::TIMESTAMPTZ
                        FROM queen.stream_sources ss
                        JOIN queen.partitions p ON ss.queue_id = p.queue_id
                        WHERE ss.stream_id = $1::UUID
                        UNION
                        SELECT $1::UUID, $2, '__GLOBAL__', $3::TIMESTAMPTZ

                        ON CONFLICT (stream_id, consumer_group, stream_key)
                        DO UPDATE SET
                            last_acked_window_end = EXCLUDED.last_acked_window_end,
                            total_windows_consumed = 0,
                            last_consumed_at = NOW()
                    )";
                    
                    sendQueryParamsAsync(conn.get(), seek_sql, {stream_id, consumer_group, seek_timestamp});
                    getCommandResult(conn.get());  // INSERT...ON CONFLICT returns COMMAND_OK, not TUPLES_OK
                    
                    try {
                        sendAndWait(conn.get(), "COMMIT");
                        getCommandResult(conn.get());
                    } catch (const std::exception& e) {
                        worker_response_queues_[worker_id]->push(request_id, {{"error", "Failed to commit transaction"}}, true, 500);
                        return;
                    }
                    
                    nlohmann::json response = {
                        {"success", true}
                    };
                    
                    worker_response_queues_[worker_id]->push(request_id, response, false, 200);
                    spdlog::info("[Worker {}] Seek completed: stream={}, group={}, timestamp={}", 
                                 worker_id, stream_name, consumer_group, seek_timestamp);
                    
                } catch (const std::exception& e) {
                    spdlog::error("[Worker {}] Failed to seek offset: {}", worker_id, e.what());
                    try {
                        sendAndWait(conn.get(), "ROLLBACK");
                        getCommandResult(conn.get());
                    } catch (...) {}
                    worker_response_queues_[worker_id]->push(request_id, {{"error", std::string("Seek error: ") + e.what()}}, true, 500);
                }
            });
        },
        [this, res](const std::string& error) {
            send_error_response(res, error, 400);
        });
}

// ============================================================================
// Database Query Helpers
// ============================================================================

std::optional<StreamDefinition> StreamManager::get_stream(PGconn* conn, const std::string& stream_name) {
    try {
        sendQueryParamsAsync(conn, "SELECT * FROM queen.streams WHERE name = $1", {stream_name});
        auto result = getTuplesResult(conn);
        
        if (PQntuples(result.get()) == 0) {
            return std::nullopt;
        }
        
        StreamDefinition def;
        def.id = PQgetvalue(result.get(), 0, PQfnumber(result.get(), "id"));
        def.name = PQgetvalue(result.get(), 0, PQfnumber(result.get(), "name"));
        def.namespace_name = PQgetvalue(result.get(), 0, PQfnumber(result.get(), "namespace"));
        def.partitioned = std::string(PQgetvalue(result.get(), 0, PQfnumber(result.get(), "partitioned"))) == "t";
        def.window_type = PQgetvalue(result.get(), 0, PQfnumber(result.get(), "window_type"));
        def.window_duration_ms = std::stoll(PQgetvalue(result.get(), 0, PQfnumber(result.get(), "window_duration_ms")));
        def.window_grace_period_ms = std::stoll(PQgetvalue(result.get(), 0, PQfnumber(result.get(), "window_grace_period_ms")));
        def.window_lease_timeout_ms = std::stoll(PQgetvalue(result.get(), 0, PQfnumber(result.get(), "window_lease_timeout_ms")));
        
        return def;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to get stream: {}", e.what());
        return std::nullopt;
    }
}

std::vector<StreamPartitionOffset> StreamManager::get_partitions_and_offsets(
    PGconn* conn,
    const std::string& stream_id,
    const std::string& consumer_group,
    bool partitioned
) {
    std::vector<StreamPartitionOffset> result;
    
    try {
        if (partitioned) {
            // Q4: Partitioned query - GROUP BY partition NAME (not UUID)
            // This allows multiple queues with same partition names to be grouped together
            std::string sql = R"(
                SELECT DISTINCT
                    p.name as stream_key,
                    p.name as partition_name,
                    COALESCE(
                        o.last_acked_window_end::TEXT, 
                        '-infinity'
                    ) as last_acked_window_end
                FROM queen.partitions p
                JOIN queen.stream_sources ss ON p.queue_id = ss.queue_id
                LEFT JOIN queen.stream_consumer_offsets o 
                    ON ss.stream_id = o.stream_id
                    AND p.name = o.stream_key
                    AND o.consumer_group = $2
                WHERE ss.stream_id = $1::UUID
            )";
            
            sendQueryParamsAsync(conn, sql, {stream_id, consumer_group});
            auto query_result = getTuplesResult(conn);
            
            for (int i = 0; i < PQntuples(query_result.get()); i++) {
                    StreamPartitionOffset offset;
                offset.stream_key = PQgetvalue(query_result.get(), i, PQfnumber(query_result.get(), "stream_key"));
                offset.partition_name = PQgetvalue(query_result.get(), i, PQfnumber(query_result.get(), "partition_name"));
                offset.last_acked_window_end = PQgetvalue(query_result.get(), i, PQfnumber(query_result.get(), "last_acked_window_end"));
                    result.push_back(offset);
            }
        } else {
            // Q4: Global query
            std::string sql = R"(
                SELECT 
                    '__GLOBAL__' as stream_key,
                    '__GLOBAL__' as partition_name,
                    COALESCE(
                        o.last_acked_window_end::TEXT, 
                        '-infinity'
                    ) as last_acked_window_end
                FROM queen.stream_consumer_offsets o
                WHERE o.stream_id = $1::UUID
                  AND o.consumer_group = $2
                  AND o.stream_key = '__GLOBAL__'
            )";
            
            sendQueryParamsAsync(conn, sql, {stream_id, consumer_group});
            auto query_result = getTuplesResult(conn);
            
            if (PQntuples(query_result.get()) > 0) {
                StreamPartitionOffset offset;
                offset.stream_key = PQgetvalue(query_result.get(), 0, PQfnumber(query_result.get(), "stream_key"));
                offset.partition_name = PQgetvalue(query_result.get(), 0, PQfnumber(query_result.get(), "partition_name"));
                offset.last_acked_window_end = PQgetvalue(query_result.get(), 0, PQfnumber(query_result.get(), "last_acked_window_end"));
                result.push_back(offset);
            } else {
                // Create default entry
                StreamPartitionOffset offset;
                offset.stream_key = "__GLOBAL__";
                offset.partition_name = "__GLOBAL__";
                offset.last_acked_window_end = "-infinity";
                result.push_back(offset);
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to get partitions and offsets: {}", e.what());
    }
    
    return result;
}

std::string StreamManager::get_watermark(PGconn* conn, const std::string& stream_id) {
    try {
        // Q5: Get Watermark
        std::string sql = R"(
            SELECT 
                MIN(w.max_created_at)::TEXT as current_watermark
            FROM queen.queue_watermarks w
            JOIN queen.stream_sources ss ON w.queue_id = ss.queue_id
            WHERE ss.stream_id = $1::UUID
        )";
        
        sendQueryParamsAsync(conn, sql, {stream_id});
        auto result = getTuplesResult(conn);
        
        if (PQntuples(result.get()) == 0 || PQgetisnull(result.get(), 0, 0)) {
            return "-infinity";
        }
        
        return PQgetvalue(result.get(), 0, 0);
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to get watermark: {}", e.what());
        return "-infinity";
    }
}

bool StreamManager::check_lease_exists(
    PGconn* conn,
    const std::string& stream_id,
    const std::string& consumer_group,
    const std::string& stream_key,
    const std::string& window_start
) {
    try {
        // Q6: Check for Active Lease
        std::string sql = R"(
            SELECT 1 FROM queen.stream_leases
            WHERE stream_id = $1::UUID
              AND consumer_group = $2
              AND stream_key = $3
              AND window_start = $4::TIMESTAMPTZ
              AND lease_expires_at > NOW()
            LIMIT 1
        )";
        
        sendQueryParamsAsync(conn, sql, {stream_id, consumer_group, stream_key, window_start});
        auto result = getTuplesResult(conn);
        
        return PQntuples(result.get()) > 0;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to check lease: {}", e.what());
        return false;
    }
}

std::string StreamManager::create_lease(
    PGconn* conn,
    const std::string& stream_id,
    const std::string& consumer_group,
    const std::string& stream_key,
    const std::string& window_start,
    const std::string& window_end,
    int64_t lease_timeout_ms
) {
    try {
        // Q7: Create Lease
        std::string sql = R"(
            INSERT INTO queen.stream_leases (
                stream_id, consumer_group, stream_key,
                window_start, window_end, 
                lease_id, lease_expires_at
            )
            VALUES ($1::UUID, $2, $3, $4::TIMESTAMPTZ, $5::TIMESTAMPTZ, gen_random_uuid(), NOW() + ($6 || ' milliseconds')::interval)
            RETURNING lease_id
        )";
        
        sendQueryParamsAsync(conn, sql, {stream_id, consumer_group, stream_key, window_start, window_end, std::to_string(lease_timeout_ms)});
        auto result = getTuplesResult(conn);
        
        if (PQntuples(result.get()) == 0) {
            spdlog::error("Create lease returned no rows");
            spdlog::error("Params: stream_id={}, consumer_group={}, stream_key={}, window_start={}, window_end={}, timeout={}ms", 
                         stream_id, consumer_group, stream_key, window_start, window_end, lease_timeout_ms);
            throw std::runtime_error("No lease_id returned");
        }
        
        return PQgetvalue(result.get(), 0, 0);
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to create lease: {}", e.what());
        throw;
    }
}

nlohmann::json StreamManager::get_messages(
    PGconn* conn,
    const std::string& stream_id,
    const std::string& stream_key,
    bool partitioned,
    const std::string& window_start,
    const std::string& window_end
) {
    nlohmann::json messages = nlohmann::json::array();
    
    try {
        if (partitioned) {
            // Q8: Partitioned query - Query by partition NAME across all source queues
            std::string sql = R"(
                SELECT m.id, m.payload, m.created_at
                FROM queen.messages m
                JOIN queen.partitions p ON m.partition_id = p.id
                JOIN queen.stream_sources ss ON p.queue_id = ss.queue_id
                WHERE ss.stream_id = $1::UUID
                  AND p.name = $2
                  AND m.created_at >= $3::TIMESTAMPTZ
                  AND m.created_at < $4::TIMESTAMPTZ
                ORDER BY m.created_at, m.id
            )";
            
            sendQueryParamsAsync(conn, sql, {stream_id, stream_key, window_start, window_end});
            auto result = getTuplesResult(conn);
            
            for (int i = 0; i < PQntuples(result.get()); i++) {
                    nlohmann::json msg = {
                    {"id", PQgetvalue(result.get(), i, PQfnumber(result.get(), "id"))},
                    {"data", nlohmann::json::parse(PQgetvalue(result.get(), i, PQfnumber(result.get(), "payload")))},
                    {"created_at", PQgetvalue(result.get(), i, PQfnumber(result.get(), "created_at"))}
                    };
                    messages.push_back(msg);
            }
        } else {
            // Q8: Global query
            std::string sql = R"(
                SELECT m.id, m.payload, m.created_at
                FROM queen.messages m
                JOIN queen.partitions p ON m.partition_id = p.id
                JOIN queen.stream_sources ss ON p.queue_id = ss.queue_id
                WHERE ss.stream_id = $1::UUID
                  AND m.created_at >= $2::TIMESTAMPTZ
                  AND m.created_at < $3::TIMESTAMPTZ
                ORDER BY m.created_at, m.id
            )";
            
            sendQueryParamsAsync(conn, sql, {stream_id, window_start, window_end});
            auto result = getTuplesResult(conn);
            
            for (int i = 0; i < PQntuples(result.get()); i++) {
                    nlohmann::json msg = {
                    {"id", PQgetvalue(result.get(), i, PQfnumber(result.get(), "id"))},
                    {"data", nlohmann::json::parse(PQgetvalue(result.get(), i, PQfnumber(result.get(), "payload")))},
                    {"created_at", PQgetvalue(result.get(), i, PQfnumber(result.get(), "created_at"))}
                    };
                    messages.push_back(msg);
            }
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to get messages: {}", e.what());
    }
    
    return messages;
}

std::optional<std::string> StreamManager::get_first_message_time(
    PGconn* conn,
    const std::string& stream_id,
    const std::string& stream_key,
    bool partitioned
) {
    try {
        if (partitioned) {
            // Q15: Partitioned query - Query by partition NAME across all source queues
            std::string sql = R"(
                SELECT MIN(m.created_at)::TEXT as first_time
                FROM queen.messages m
                JOIN queen.partitions p ON m.partition_id = p.id
                JOIN queen.stream_sources ss ON p.queue_id = ss.queue_id
                WHERE ss.stream_id = $1::UUID
                  AND p.name = $2
            )";
            
            sendQueryParamsAsync(conn, sql, {stream_id, stream_key});
            auto result = getTuplesResult(conn);
            
            if (PQntuples(result.get()) == 0 || PQgetisnull(result.get(), 0, 0)) {
                return std::nullopt;
            }
            
            return PQgetvalue(result.get(), 0, 0);
        } else {
            // Q15: Global query
            std::string sql = R"(
                SELECT MIN(m.created_at)::TEXT as first_time
                FROM queen.messages m
                JOIN queen.partitions p ON m.partition_id = p.id
                JOIN queen.stream_sources ss ON p.queue_id = ss.queue_id
                WHERE ss.stream_id = $1::UUID
            )";
            
            sendQueryParamsAsync(conn, sql, {stream_id});
            auto result = getTuplesResult(conn);
            
            if (PQntuples(result.get()) == 0 || PQgetisnull(result.get(), 0, 0)) {
                return std::nullopt;
            }
            
            return PQgetvalue(result.get(), 0, 0);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to get first message time: {}", e.what());
        return std::nullopt;
    }
}

// ============================================================================
// Utility Methods
// ============================================================================

std::string StreamManager::align_to_boundary(const std::string& timestamp, int64_t duration_ms) {
    auto tp = parse_timestamp(timestamp);
    auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    auto aligned_ms = (epoch_ms / duration_ms) * duration_ms;
    auto aligned_tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(aligned_ms));
    return format_timestamp(aligned_tp);
}

std::string StreamManager::format_timestamp(const std::chrono::system_clock::time_point& tp) {
    // Use PostgreSQL's timestamp format
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count() % 1000000;
    
    std::tm tm;
    gmtime_r(&time_t, &tm);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(6) << us << "+00";
    
    return oss.str();
}

std::chrono::system_clock::time_point StreamManager::parse_timestamp(const std::string& ts_str) {
    if (ts_str == "-infinity") {
        return std::chrono::system_clock::time_point::min();
    }
    
    // Parse PostgreSQL timestamp format: "2025-11-02 18:55:30.123456+00"
    std::tm tm = {};
    int microseconds = 0;
    
    // Parse the main timestamp part
    std::istringstream ss(ts_str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    // Parse microseconds if present
    if (ss.peek() == '.') {
        ss.get(); // consume '.'
        std::string us_str;
        while (std::isdigit(ss.peek())) {
            us_str += ss.get();
        }
        if (!us_str.empty()) {
            microseconds = std::stoi(us_str);
            // Pad or truncate to 6 digits
            while (us_str.length() < 6) us_str += "0";
            microseconds = std::stoi(us_str.substr(0, 6));
        }
    }
    
    auto time_t = timegm(&tm);
    auto tp = std::chrono::system_clock::from_time_t(time_t);
    tp += std::chrono::microseconds(microseconds);
    
    return tp;
}

std::string StreamManager::generate_window_id(
    const std::string& stream_id,
    const std::string& partition_name,
    const std::string& window_start,
    const std::string& window_end
) {
    return stream_id + ":" + partition_name + ":" + window_start + ":" + window_end;
}

void StreamManager::send_json_response(uWS::HttpResponse<false>* res, const nlohmann::json& data, int status_code) {
    if (!res) return;
    
    try {
        std::string json_str = data.dump();
        res->writeStatus(std::to_string(status_code) + " OK");
        res->writeHeader("Content-Type", "application/json");
        res->end(json_str);
    } catch (const std::exception& e) {
        spdlog::error("Failed to send JSON response: {}", e.what());
    }
}

void StreamManager::send_error_response(uWS::HttpResponse<false>* res, const std::string& error, int status_code) {
    nlohmann::json error_json = {
        {"error", error}
    };
    send_json_response(res, error_json, status_code);
}

void StreamManager::read_json_body(
    uWS::HttpResponse<false>* res,
    std::function<void(const nlohmann::json&)> on_success,
    std::function<void(const std::string&)> on_error
) {
    auto buffer = std::make_shared<std::string>();
    auto completed = std::make_shared<bool>(false);
    
    res->onData([on_success, on_error, buffer, completed](std::string_view chunk, bool is_last) {
        buffer->append(chunk.data(), chunk.size());
        
        if (is_last && !*completed) {
            *completed = true;
            try {
                if (buffer->empty()) {
                    on_error("Empty request body");
                } else {
                    nlohmann::json body = nlohmann::json::parse(*buffer);
                    on_success(body);
                }
            } catch (const std::exception& e) {
                on_error(std::string("Invalid JSON: ") + e.what());
            }
        }
    });
    
    res->onAborted([completed]() {
        *completed = true;
    });
}

// Poll worker support - check for ready windows and deliver to waiting intentions
bool StreamManager::check_and_deliver_window_for_poll(
    const std::string& stream_name,
    const std::string& consumer_group,
    const std::vector<StreamPollIntention>& intentions,
    std::vector<std::shared_ptr<ResponseQueue>> worker_response_queues
) {
    if (intentions.empty()) {
        return false;
    }
    
    auto conn = db_pool_->acquire();
    
    try {
        // Get stream
        auto stream_opt = get_stream(conn.get(), stream_name);
        if (!stream_opt.has_value()) {
            // Stream not found - this shouldn't happen, but send error to first intention
            spdlog::error("Stream not found in poll worker: stream={}", stream_name);
            return false;
        }
        
        auto stream = stream_opt.value();
        std::string watermark_str = get_watermark(conn.get(), stream.id);
        auto partitions = get_partitions_and_offsets(conn.get(), stream.id, consumer_group, stream.partitioned);
        
        // Try to find a ready window (same logic as handle_poll immediate check)
        for (const auto& partition : partitions) {
            auto stream_key = partition.stream_key;
            auto partition_name = partition.partition_name;
            auto last_end_str = partition.last_acked_window_end;
            
            std::string window_start_str;
            
            if (last_end_str == "-infinity") {
                auto first_time_opt = get_first_message_time(conn.get(), stream.id, stream_key, stream.partitioned);
                if (!first_time_opt.has_value()) {
                    continue; // No messages yet for this partition
                }
                window_start_str = align_to_boundary(first_time_opt.value(), stream.window_duration_ms);
            } else {
                window_start_str = last_end_str;
            }
            
            auto window_start = parse_timestamp(window_start_str);
            auto window_end = window_start + std::chrono::milliseconds(stream.window_duration_ms);
            std::string window_end_str = format_timestamp(window_end);
            
            auto watermark = parse_timestamp(watermark_str);
            auto grace_boundary = window_end + std::chrono::milliseconds(stream.window_grace_period_ms);
            
            if (watermark < grace_boundary) {
                continue; // Window not ready yet
            }
            
            // Delete any expired leases for this window first
            std::string cleanup_sql = R"(
DELETE FROM queen.stream_leases 
                WHERE stream_id = $1::UUID
                  AND consumer_group = $2
                  AND stream_key = $3
                  AND window_start = $4::TIMESTAMPTZ
                  AND lease_expires_at < NOW()
            )";
            sendQueryParamsAsync(conn.get(), cleanup_sql, {stream.id, consumer_group, stream_key, window_start_str});
            getCommandResult(conn.get());
            
            // Check if there's a valid (non-expired) lease
            if (check_lease_exists(conn.get(), stream.id, consumer_group, stream_key, window_start_str)) {
                continue; // Window already leased
            }
            
            // Found a ready window! Try to create lease and deliver to FIRST intention
            try {
                std::string lease_id = create_lease(conn.get(), stream.id, consumer_group, stream_key,
                                                    window_start_str, window_end_str, stream.window_lease_timeout_ms);
                
                auto messages = get_messages(conn.get(), stream.id, stream_key, stream.partitioned, 
                                             window_start_str, window_end_str);
                
                nlohmann::json window_json = {
                    {"id", generate_window_id(stream.id, partition_name, window_start_str, window_end_str)},
                    {"leaseId", lease_id},
                    {"key", partition_name},
                    {"start", window_start_str},
                    {"end", window_end_str},
                    {"messages", messages}
                };
                
                nlohmann::json response = {{"window", window_json}};
                
                // Deliver to FIRST intention only (one window per poll worker check)
                const auto& first_intention = intentions[0];
                worker_response_queues[first_intention.worker_id]->push(
                    first_intention.request_id, 
                    response, 
                    false, 
                    200
                );
                
                spdlog::info("[Worker {}] Delivered window {}: stream={}, group={}, key={}, messages={}", 
                             first_intention.worker_id, first_intention.request_id, stream_name, consumer_group, partition_name, messages.size());
                
                return true; // Window was delivered
                
            } catch (const std::runtime_error& e) {
                // Check if it's a duplicate key error (race condition)
                std::string error_msg = e.what();
                if (error_msg.find("duplicate key") != std::string::npos || 
                    error_msg.find("unique constraint") != std::string::npos) {
                    continue; // Try next partition
                }
                // Other errors - log and continue
                spdlog::error("Error creating lease/delivering window in poll worker: {}", e.what());
                continue;
            }
        }
        
        // No ready window found
        return false;
        
    } catch (const std::exception& e) {
        spdlog::error("Error in check_and_deliver_window_for_poll: stream={}, group={}, error={}", 
                     stream_name, consumer_group, e.what());
        return false;
    }
}

} // namespace queen
