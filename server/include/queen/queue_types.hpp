#pragma once

#include <json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace queen {

// ============================================================================
// Shared Queue Types
// These types are used by both QueueManager (legacy) and AsyncQueueManager
// ============================================================================

struct Message {
    std::string id;
    std::string transaction_id;
    std::string partition_id;  // Partition UUID for partition-scoped operations
    nlohmann::json payload;
    std::string queue_name;
    std::string partition_name;
    std::string trace_id;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point lease_expires_at;
    std::string status; // "pending", "processing", "completed", "failed"
    int priority = 0;
    int retry_count = 0;
};

struct PushItem {
    std::string queue;
    std::string partition = "Default";
    nlohmann::json payload;
    std::optional<std::string> transaction_id;
    std::optional<std::string> trace_id;
};

struct PushResult {
    std::string transaction_id;
    std::string status; // "queued", "buffered", "failed"
    std::optional<std::string> error;
    std::optional<std::string> message_id;
    std::optional<std::string> trace_id;
};

struct PopOptions {
    bool wait = false;
    int timeout = 30000; // milliseconds
    int batch = 1;
    std::optional<std::string> subscription_mode;
    std::optional<std::string> subscription_from;
    bool auto_ack = false;  // Auto-acknowledge messages on delivery (QoS 0)
};

struct PopResult {
    std::vector<Message> messages;
    std::optional<std::string> lease_id;
};

struct QueueStats {
    std::string queue_name;
    int64_t pending_count = 0;
    int64_t processing_count = 0;
    int64_t completed_count = 0;
    int64_t failed_count = 0;
    double avg_processing_time = 0.0;
};

// Queue configuration options
struct QueueOptions {
    int lease_time = 300;
    int max_size = 10000;
    int ttl = 3600;
    int retry_limit = 3;
    int retry_delay = 1000;
    bool dead_letter_queue = false;
    bool dlq_after_max_retries = false;
    int priority = 0;
    int delayed_processing = 0;
    int window_buffer = 0;
    int retention_seconds = 0;
    int completed_retention_seconds = 0;
    bool retention_enabled = false;
    bool encryption_enabled = false;
    int max_wait_time_seconds = 0;
};

} // namespace queen

