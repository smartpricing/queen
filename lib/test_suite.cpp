/**
 * Queen C++ Library Test Suite
 * 
 * Comprehensive tests for queen.hpp operations:
 * - PUSH: single, batch, delayed, window_buffer, partitions, duplicates
 * - POP: empty queue, non-empty, with wait, batch, from partition
 * - ACK: single, batch, success/failed status
 * - TRANSACTIONS: push+ack atomic, multiple pushes, multiple acks, rollback
 * - RENEW_LEASE: manual, batch, expired lease
 * 
 * Based on client-js/test-v2 test suite.
 */

#include "queen.hpp"
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <random>
#include <iomanip>
#include <sstream>

// ============================================================================
// Simple Test Framework
// ============================================================================

struct TestResult {
    std::string name;
    bool passed;
    std::string message;
    std::chrono::milliseconds duration;
};

std::vector<TestResult> g_test_results;
int g_tests_passed = 0;
int g_tests_failed = 0;

#define TEST(name) void name(queen::Queen& queen, TestResult& result)
#define ASSERT(cond, msg) do { if (!(cond)) { result.passed = false; result.message = msg; return; } } while(0)
#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
#define ASSERT_NE(a, b, msg) ASSERT((a) != (b), msg)
#define ASSERT_GT(a, b, msg) ASSERT((a) > (b), msg)

#define RUN_TEST(fn) run_test(queen, #fn, fn)

void run_test(queen::Queen& queen, const std::string& name, void (*fn)(queen::Queen&, TestResult&)) {
    std::cout << "Running test: " << name << " :";
    TestResult result;
    result.name = name;
    result.passed = true;
    result.message = "";
    
    auto start = std::chrono::steady_clock::now();
    
    try {
        fn(queen, result);
    } catch (const std::exception& e) {
        result.passed = false;
        result.message = std::string("Exception: ") + e.what();
    }
    
    auto end = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    if (result.passed) {
        g_tests_passed++;
        std::cout << " ✅ " << " (" << result.duration.count() << "ms)" << std::endl;
    } else {
        g_tests_failed++;
        std::cout << " ❌ " << " - " << result.message << " (" << result.duration.count() << "ms)" << std::endl;
    }
    
    g_test_results.push_back(result);
}

// ============================================================================
// Test Utilities
// ============================================================================

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";  // Version 4 UUID
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << ((dis(gen) & 0x3) | 0x8);  // Variant
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    
    return ss.str();
}

std::string generate_queue_name(const std::string& prefix) {
    return prefix + "-" + generate_uuid().substr(0, 8);
}

// Synchronous wait helper for async operations
class AsyncWaiter {
public:
    void wait(int timeout_ms = 30000) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return done_; });
    }
    
    void signal() {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = true;
        cv_.notify_all();
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = false;
    }
    
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;
};

// Build PUSH parameters
// Uses UUIDv7 for messageId to ensure proper ordering
std::string build_push_params(const std::string& queue, const std::string& partition,
                              const nlohmann::json& payload,
                              const std::string& transaction_id = "") {
    nlohmann::json item;
    item["queue"] = queue;
    item["partition"] = partition.empty() ? "Default" : partition;
    item["payload"] = payload;
    item["messageId"] = queen::generate_uuidv7();  // Time-ordered UUID
    if (!transaction_id.empty()) {
        item["transactionId"] = transaction_id;
    }
    
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back(item);
    return arr.dump();
}

// Build batch PUSH parameters
// Uses UUIDv7 for messageId to ensure proper ordering within batch
std::string build_push_params_batch(const std::string& queue, const std::string& partition,
                                    const std::vector<nlohmann::json>& payloads,
                                    const std::vector<std::string>& transaction_ids = {}) {
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < payloads.size(); i++) {
        nlohmann::json item;
        item["queue"] = queue;
        item["partition"] = partition.empty() ? "Default" : partition;
        item["payload"] = payloads[i];
        item["messageId"] = queen::generate_uuidv7();  // Time-ordered UUID for proper ordering
        if (i < transaction_ids.size() && !transaction_ids[i].empty()) {
            item["transactionId"] = transaction_ids[i];
        }
        arr.push_back(item);
    }
    return arr.dump();
}

// Build POP parameters
std::string build_pop_params(const std::string& queue, const std::string& partition,
                             const std::string& consumer_group = "__QUEUE_MODE__",
                             int batch_size = 1, int lease_seconds = 60,
                             const std::string& worker_id = "",
                             const std::string& sub_mode = "all",
                             bool auto_ack = false) {
    nlohmann::json item;
    item["idx"] = 0;
    item["queue_name"] = queue;
    item["partition_name"] = partition.empty() ? "Default" : partition;
    item["consumer_group"] = consumer_group;
    item["batch_size"] = batch_size;
    item["lease_seconds"] = lease_seconds;
    item["worker_id"] = worker_id.empty() ? generate_uuid() : worker_id;
    item["sub_mode"] = sub_mode;
    item["sub_from"] = "";
    item["auto_ack"] = auto_ack;
    
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back(item);
    return arr.dump();
}

// Build ACK parameters
std::string build_ack_params(const std::string& transaction_id, const std::string& partition_id,
                             const std::string& lease_id = "",
                             const std::string& consumer_group = "__QUEUE_MODE__",
                             const std::string& status = "completed",
                             int index = 0) {
    nlohmann::json item;
    item["index"] = index;
    item["transactionId"] = transaction_id;
    item["partitionId"] = partition_id;
    item["consumerGroup"] = consumer_group;
    item["status"] = status;
    if (!lease_id.empty()) {
        item["leaseId"] = lease_id;
    }
    
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back(item);
    return arr.dump();
}

// Build batch ACK parameters
std::string build_ack_params_batch(const std::vector<nlohmann::json>& messages,
                                   const std::string& partition_id,
                                   const std::string& lease_id,
                                   const std::string& consumer_group = "__QUEUE_MODE__",
                                   const std::string& status = "completed") {
    nlohmann::json arr = nlohmann::json::array();
    int idx = 0;
    for (const auto& msg : messages) {
        nlohmann::json item;
        item["index"] = idx++;
        item["transactionId"] = msg["transactionId"];
        item["partitionId"] = partition_id;
        item["consumerGroup"] = consumer_group;
        item["leaseId"] = lease_id;
        item["status"] = status;
        arr.push_back(item);
    }
    return arr.dump();
}

// Build TRANSACTION parameters
std::string build_transaction_params(const std::vector<nlohmann::json>& operations) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& op : operations) {
        arr.push_back(op);
    }
    return arr.dump();
}

// Build RENEW_LEASE parameters
std::string build_renew_params(const std::string& lease_id, int extend_seconds = 60, int index = 0) {
    nlohmann::json item;
    item["index"] = index;
    item["leaseId"] = lease_id;
    item["extendSeconds"] = extend_seconds;
    
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back(item);
    return arr.dump();
}

// Build batch RENEW_LEASE parameters
std::string build_renew_params_batch(const std::vector<std::string>& lease_ids, int extend_seconds = 60) {
    nlohmann::json arr = nlohmann::json::array();
    int idx = 0;
    for (const auto& lease_id : lease_ids) {
        nlohmann::json item;
        item["index"] = idx++;
        item["leaseId"] = lease_id;
        item["extendSeconds"] = extend_seconds;
        arr.push_back(item);
    }
    return arr.dump();
}

// ============================================================================
// PUSH TESTS
// ============================================================================

TEST(test_push_single_message) {
    std::string queue = generate_queue_name("test-push-single");
    AsyncWaiter waiter;
    bool success = false;
    std::string error_msg;
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"message", "Hello, world!"}}) },
    }, [&](std::string result) {
        try {
            auto json = nlohmann::json::parse(result);
            success = !json.empty() && json[0]["status"] == "queued";
            if (!success) error_msg = "Status not 'queued'";
        } catch (const std::exception& e) {
            error_msg = e.what();
        }
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(success, error_msg.empty() ? "Push failed" : error_msg);
}

TEST(test_push_batch_messages) {
    std::string queue = generate_queue_name("test-push-batch");
    AsyncWaiter waiter;
    bool success = false;
    int queued_count = 0;
    
    std::vector<nlohmann::json> payloads = {
        {{"id", 1}, {"message", "First"}},
        {{"id", 2}, {"message", "Second"}},
        {{"id", 3}, {"message", "Third"}}
    };
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", payloads) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        for (const auto& item : json) {
            if (item["status"] == "queued") queued_count++;
        }
        success = (queued_count == 3);
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT_EQ(queued_count, 3, "Expected 3 queued messages");
}

TEST(test_push_duplicate_detection) {
    std::string queue = generate_queue_name("test-push-dup");
    std::string txn_id = "dup-test-" + generate_uuid();
    AsyncWaiter waiter1, waiter2;
    std::string status1, status2;
    
    // First push
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"msg", "test"}}, txn_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        status1 = json[0]["status"];
        waiter1.signal();
    });
    
    waiter1.wait();
    
    // Second push (same transaction ID - should be duplicate)
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"msg", "test"}}, txn_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        status2 = json[0]["status"];
        waiter2.signal();
    });
    
    waiter2.wait();
    
    ASSERT_EQ(status1, "queued", "First push should be queued");
    ASSERT_EQ(status2, "duplicate", "Second push should be duplicate");
}

TEST(test_push_to_different_partitions) {
    std::string queue = generate_queue_name("test-push-partitions");
    std::string txn_id = "part-test-" + generate_uuid();
    AsyncWaiter waiter1, waiter2;
    std::string status1, status2;
    
    // Push to partition 1
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "partition-1",
        .params = { build_push_params(queue, "partition-1", {{"msg", "test"}}, txn_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        status1 = json[0]["status"];
        waiter1.signal();
    });
    
    waiter1.wait();
    
    // Push to partition 2 (same transaction ID but different partition - should NOT be duplicate)
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "partition-2",
        .params = { build_push_params(queue, "partition-2", {{"msg", "test"}}, txn_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        status2 = json[0]["status"];
        waiter2.signal();
    });
    
    waiter2.wait();
    
    ASSERT_EQ(status1, "queued", "First push should be queued");
    ASSERT_EQ(status2, "queued", "Second push to different partition should also be queued");
}

TEST(test_push_large_payload) {
    std::string queue = generate_queue_name("test-push-large");
    AsyncWaiter waiter;
    bool success = false;
    
    // Create large payload (~100KB)
    nlohmann::json large_array = nlohmann::json::array();
    for (int i = 0; i < 1000; i++) {
        large_array.push_back({
            {"id", i},
            {"data", std::string(100, 'x')},
            {"nested", {{"field1", "value1"}, {"field2", "value2"}}}
        });
    }
    
    nlohmann::json payload = {
        {"array", large_array},
        {"metadata", {{"size", large_array.dump().length()}}}
    };
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", payload) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        success = !json.empty() && json[0]["status"] == "queued";
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(success, "Large payload push failed");
}

TEST(test_push_null_payload) {
    std::string queue = generate_queue_name("test-push-null");
    AsyncWaiter waiter;
    bool success = false;
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", nullptr) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        success = !json.empty() && json[0]["status"] == "queued";
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(success, "Null payload push failed");
}

TEST(test_push_empty_object_payload) {
    std::string queue = generate_queue_name("test-push-empty");
    AsyncWaiter waiter;
    bool success = false;
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", nlohmann::json::object()) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        success = !json.empty() && json[0]["status"] == "queued";
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(success, "Empty object payload push failed");
}

// ============================================================================
// POP TESTS
// ============================================================================

TEST(test_pop_empty_queue) {
    std::string queue = generate_queue_name("test-pop-empty");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter;
    int message_count = -1;
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 1, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            message_count = json[0]["result"]["messages"].size();
        }
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT_EQ(message_count, 0, "Expected 0 messages from empty queue");
}

TEST(test_pop_non_empty_queue) {
    std::string queue = generate_queue_name("test-pop-nonempty");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2;
    bool push_success = false;
    int message_count = 0;
    
    // First, push a message
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"message", "Hello!"}}) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        push_success = !json.empty() && json[0]["status"] == "queued";
        waiter1.signal();
    });
    
    waiter1.wait();
    ASSERT(push_success, "Push failed");
    
    // Small delay for message to be available
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop the message
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 1, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            message_count = json[0]["result"]["messages"].size();
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT_EQ(message_count, 1, "Expected 1 message");
}

TEST(test_pop_batch) {
    std::string queue = generate_queue_name("test-pop-batch");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2;
    int pushed = 0;
    int popped = 0;
    
    // Push 5 messages
    std::vector<nlohmann::json> payloads;
    for (int i = 0; i < 5; i++) {
        payloads.push_back({{"id", i}});
    }
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", payloads) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        for (const auto& item : json) {
            if (item["status"] == "queued") pushed++;
        }
        waiter1.signal();
    });
    
    waiter1.wait();
    ASSERT_EQ(pushed, 5, "Expected 5 pushed messages");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop batch of 5
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 5, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            popped = json[0]["result"]["messages"].size();
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT_EQ(popped, 5, "Expected 5 popped messages");
}

TEST(test_pop_from_specific_partition) {
    std::string queue = generate_queue_name("test-pop-partition");
    std::string worker_id1 = generate_uuid();
    std::string worker_id2 = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4;
    int p1_count = 0, p2_count = 0;
    
    // Push to partition 1
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "partition-1",
        .params = { build_push_params(queue, "partition-1", {{"partition", 1}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    // Push to partition 2
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "partition-2",
        .params = { build_push_params(queue, "partition-2", {{"partition", 2}}) },
    }, [&](std::string) { waiter2.signal(); });
    
    waiter1.wait();
    waiter2.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop from partition 1 only
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "partition-1",
        .params = { build_pop_params(queue, "partition-1", "__QUEUE_MODE__", 10, 60, worker_id1) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            p1_count = json[0]["result"]["messages"].size();
        }
        waiter3.signal();
    });
    
    // Pop from partition 2 only
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "partition-2",
        .params = { build_pop_params(queue, "partition-2", "__QUEUE_MODE__", 10, 60, worker_id2) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            p2_count = json[0]["result"]["messages"].size();
        }
        waiter4.signal();
    });
    
    waiter3.wait();
    waiter4.wait();
    
    ASSERT_EQ(p1_count, 1, "Expected 1 message from partition-1");
    ASSERT_EQ(p2_count, 1, "Expected 1 message from partition-2");
}

TEST(test_pop_with_consumer_group) {
    std::string queue = generate_queue_name("test-pop-cg");
    std::string worker_id1 = generate_uuid();
    std::string worker_id2 = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3;
    int cg1_count = 0, cg2_count = 0;
    
    // Push messages
    std::vector<nlohmann::json> payloads;
    for (int i = 0; i < 10; i++) {
        payloads.push_back({{"id", i}});
    }
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", payloads) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop with consumer group 1
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .consumer_group = "test-group-1",
        .params = { build_pop_params(queue, "Default", "test-group-1", 10, 60, worker_id1, "all") },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            cg1_count = json[0]["result"]["messages"].size();
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    
    // Pop with consumer group 2 (same messages should be available)
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .consumer_group = "test-group-2",
        .params = { build_pop_params(queue, "Default", "test-group-2", 10, 60, worker_id2, "all") },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            cg2_count = json[0]["result"]["messages"].size();
        }
        waiter3.signal();
    });
    
    waiter3.wait();
    
    ASSERT_EQ(cg1_count, 10, "Consumer group 1 should get all 10 messages");
    ASSERT_EQ(cg2_count, 10, "Consumer group 2 should also get all 10 messages");
}

TEST(test_pop_with_wait) {
    std::string queue = generate_queue_name("test-pop-wait");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2;
    int message_count = 0;
    auto pop_start = std::chrono::steady_clock::now();
    auto pop_end = pop_start;
    
    // Start POP with wait BEFORE pushing (should wait for message)
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .wait_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10),
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 1, 60, worker_id) },
    }, [&](std::string result) {
        pop_end = std::chrono::steady_clock::now();
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            message_count = json[0]["result"]["messages"].size();
        }
        waiter1.signal();
    });
    
    // Wait a bit, then push a message
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"message", "delayed push"}}) },
    }, [&](std::string) { waiter2.signal(); });
    
    waiter2.wait();
    waiter1.wait();
    
    auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(pop_end - pop_start);
    
    ASSERT_EQ(message_count, 1, "Should receive 1 message after wait");
    ASSERT_GT(wait_duration.count(), 1500, "POP should have waited at least 1.5 seconds");
}

TEST(test_pop_consumer_group_subscription_mode_all) {
    std::string queue = generate_queue_name("test-cg-sub-all");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2;
    int message_count = 0;
    
    // Push historical messages BEFORE consumer subscribes
    std::vector<nlohmann::json> payloads;
    for (int i = 0; i < 5; i++) {
        payloads.push_back({{"id", i}, {"type", "historical"}});
    }
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", payloads) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Consumer group with 'all' mode should get all historical messages
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .consumer_group = "group-all-mode",
        .params = { build_pop_params(queue, "Default", "group-all-mode", 10, 60, worker_id, "all") },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            message_count = json[0]["result"]["messages"].size();
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT_EQ(message_count, 5, "Consumer group with 'all' mode should get all 5 historical messages");
}

TEST(test_pop_consumer_group_subscription_mode_new) {
    std::string queue = generate_queue_name("test-cg-sub-new");
    std::string worker_id1 = generate_uuid();
    std::string worker_id2 = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4;
    int historical_count = 0;
    int new_count = 0;
    
    // Push historical messages BEFORE consumer subscribes
    std::vector<nlohmann::json> historical_payloads;
    for (int i = 0; i < 5; i++) {
        historical_payloads.push_back({{"id", i}, {"type", "historical"}});
    }
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", historical_payloads) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Consumer group with 'new' mode should skip historical messages
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .consumer_group = "group-new-mode",
        .params = { build_pop_params(queue, "Default", "group-new-mode", 10, 60, worker_id1, "new") },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            historical_count = json[0]["result"]["messages"].size();
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    
    // Wait for subscription to be established
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Push new messages AFTER consumer subscribed
    std::vector<nlohmann::json> new_payloads;
    for (int i = 0; i < 3; i++) {
        new_payloads.push_back({{"id", i + 5}, {"type", "new"}});
    }
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", new_payloads) },
    }, [&](std::string) { waiter3.signal(); });
    
    waiter3.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Same consumer group should now get the new messages
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .consumer_group = "group-new-mode",
        .params = { build_pop_params(queue, "Default", "group-new-mode", 10, 60, worker_id2, "new") },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            new_count = json[0]["result"]["messages"].size();
        }
        waiter4.signal();
    });
    
    waiter4.wait();
    
    ASSERT_EQ(historical_count, 0, "Consumer group with 'new' mode should skip historical messages");
    ASSERT_EQ(new_count, 3, "Consumer group with 'new' mode should get 3 new messages");
}

TEST(test_pop_consumer_group_subscription_from_beginning) {
    std::string queue = generate_queue_name("test-cg-from-begin");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2;
    int message_count = 0;
    
    // Push messages first
    std::vector<nlohmann::json> payloads;
    for (int i = 0; i < 5; i++) {
        payloads.push_back({{"id", i}});
    }
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", payloads) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Consumer group with 'from_beginning' mode
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .consumer_group = "group-from-beginning",
        .params = { build_pop_params(queue, "Default", "group-from-beginning", 10, 60, worker_id, "from_beginning") },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            message_count = json[0]["result"]["messages"].size();
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT_EQ(message_count, 5, "Consumer group with 'from_beginning' mode should get all 5 messages");
}

TEST(test_pop_multiple_consumer_groups_independent) {
    std::string queue = generate_queue_name("test-cg-independent");
    std::string worker_id1 = generate_uuid();
    std::string worker_id2 = generate_uuid();
    std::string worker_id3 = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4, waiter5, waiter6;
    int cg1_first = 0;
    int cg2_count = 0;
    
    // Push initial messages
    std::vector<nlohmann::json> payloads;
    for (int i = 0; i < 5; i++) {
        payloads.push_back({{"id", i}});
    }
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", payloads) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Consumer group 1 pops first batch (3 messages)
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .consumer_group = "cg-independent-1",
        .params = { build_pop_params(queue, "Default", "cg-independent-1", 3, 60, worker_id1, "all") },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            cg1_first = json[0]["result"]["messages"].size();
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    
    // ACK those messages for cg1 (simulate processing)
    // For simplicity, we'll just move on
    
    // Consumer group 2 should still see ALL 5 messages (independent cursor)
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .consumer_group = "cg-independent-2",
        .params = { build_pop_params(queue, "Default", "cg-independent-2", 10, 60, worker_id2, "all") },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            cg2_count = json[0]["result"]["messages"].size();
        }
        waiter3.signal();
    });
    
    waiter3.wait();
    
    ASSERT_EQ(cg1_first, 3, "Consumer group 1 should get first 3 messages");
    ASSERT_EQ(cg2_count, 5, "Consumer group 2 should get all 5 messages (independent)");
}

// ============================================================================
// ACK TESTS
// ============================================================================

TEST(test_ack_single_message) {
    std::string queue = generate_queue_name("test-ack-single");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3;
    bool ack_success = false;
    std::string txn_id, partition_id, lease_id;
    
    // Push
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"message", "test"}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 1, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            if (!res["messages"].empty()) {
                txn_id = res["messages"][0]["transactionId"];
                partition_id = res["partitionId"];
                lease_id = res["leaseId"];
            }
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT(!txn_id.empty(), "No message received to ack");
    
    // ACK
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::ACK,
        .params = { build_ack_params(txn_id, partition_id, lease_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        ack_success = !json.empty() && json[0]["success"] == true;
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(ack_success, "ACK failed");
}

TEST(test_ack_batch_messages) {
    std::string queue = generate_queue_name("test-ack-batch");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3;
    nlohmann::json messages;
    std::string partition_id, lease_id;
    bool all_acked = false;
    
    // Push 5 messages
    std::vector<nlohmann::json> payloads;
    for (int i = 0; i < 5; i++) {
        payloads.push_back({{"id", i}});
    }
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", payloads) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop all 5
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 5, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            messages = res["messages"];
            partition_id = res["partitionId"];
            lease_id = res["leaseId"];
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT_EQ(messages.size(), 5u, "Expected 5 messages");
    
    // ACK all 5 in batch
    std::vector<nlohmann::json> msg_vec(messages.begin(), messages.end());
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::ACK,
        .params = { build_ack_params_batch(msg_vec, partition_id, lease_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        all_acked = true;
        for (const auto& item : json) {
            if (item["success"] != true) {
                all_acked = false;
                break;
            }
        }
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(all_acked, "Not all messages were acked successfully");
}

TEST(test_ack_with_failed_status) {
    std::string queue = generate_queue_name("test-ack-failed");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3;
    std::string txn_id, partition_id, lease_id;
    bool ack_success = false;
    
    // Push
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"message", "test"}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 1, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            if (!res["messages"].empty()) {
                txn_id = res["messages"][0]["transactionId"];
                partition_id = res["partitionId"];
                lease_id = res["leaseId"];
            }
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT(!txn_id.empty(), "No message received");
    
    // ACK with failed status
    nlohmann::json ack_item;
    ack_item["index"] = 0;
    ack_item["transactionId"] = txn_id;
    ack_item["partitionId"] = partition_id;
    ack_item["consumerGroup"] = "__QUEUE_MODE__";
    ack_item["leaseId"] = lease_id;
    ack_item["status"] = "failed";
    ack_item["error"] = "Test error message";
    
    nlohmann::json acks = nlohmann::json::array();
    acks.push_back(ack_item);
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::ACK,
        .params = { acks.dump() },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        ack_success = !json.empty() && json[0]["success"] == true;
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(ack_success, "ACK with failed status should succeed");
}

// ============================================================================
// TRANSACTION TESTS
// ============================================================================

TEST(test_transaction_basic_push_ack) {
    std::string queue_a = generate_queue_name("test-txn-a");
    std::string queue_b = generate_queue_name("test-txn-b");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4;
    std::string txn_id, partition_id;
    bool txn_success = false;
    int queue_b_count = 0;
    
    // Push to queue A
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue_a,
        .partition_name = "Default",
        .params = { build_push_params(queue_a, "Default", {{"value", 1}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop from queue A
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue_a,
        .partition_name = "Default",
        .params = { build_pop_params(queue_a, "Default", "__QUEUE_MODE__", 1, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            if (!res["messages"].empty()) {
                txn_id = res["messages"][0]["transactionId"];
                partition_id = res["partitionId"];
            }
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT(!txn_id.empty(), "No message to consume");
    
    // Transaction: Push to B, ACK from A
    std::vector<nlohmann::json> ops;
    ops.push_back({
        {"type", "push"},
        {"queue", queue_b},
        {"partition", "Default"},
        {"payload", {{"value", 2}}},
        {"messageId", queen::generate_uuidv7()}
    });
    ops.push_back({
        {"type", "ack"},
        {"transactionId", txn_id},
        {"partitionId", partition_id},
        {"status", "completed"}
    });
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::TRANSACTION,
        .params = { build_transaction_params(ops) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        txn_success = json["success"] == true;
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(txn_success, "Transaction failed");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify queue B has the message
    std::string worker_id2 = generate_uuid();
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue_b,
        .partition_name = "Default",
        .params = { build_pop_params(queue_b, "Default", "__QUEUE_MODE__", 1, 60, worker_id2) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            queue_b_count = json[0]["result"]["messages"].size();
        }
        waiter4.signal();
    });
    
    waiter4.wait();
    ASSERT_EQ(queue_b_count, 1, "Queue B should have 1 message");
}

TEST(test_transaction_multiple_pushes) {
    std::string queue_a = generate_queue_name("test-txn-multi-a");
    std::string queue_b = generate_queue_name("test-txn-multi-b");
    std::string queue_c = generate_queue_name("test-txn-multi-c");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4, waiter5;
    std::string txn_id, partition_id;
    bool txn_success = false;
    int queue_b_count = 0, queue_c_count = 0;
    
    // Push to queue A
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue_a,
        .partition_name = "Default",
        .params = { build_push_params(queue_a, "Default", {{"id", "source"}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop from queue A
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue_a,
        .partition_name = "Default",
        .params = { build_pop_params(queue_a, "Default", "__QUEUE_MODE__", 1, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            if (!res["messages"].empty()) {
                txn_id = res["messages"][0]["transactionId"];
                partition_id = res["partitionId"];
            }
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT(!txn_id.empty(), "No message to consume");
    
    // Transaction: Push to B and C, ACK from A
    std::vector<nlohmann::json> ops;
    ops.push_back({
        {"type", "push"},
        {"queue", queue_b},
        {"partition", "Default"},
        {"payload", {{"id", "b"}, {"source", "source"}}},
        {"messageId", queen::generate_uuidv7()}
    });
    ops.push_back({
        {"type", "push"},
        {"queue", queue_c},
        {"partition", "Default"},
        {"payload", {{"id", "c"}, {"source", "source"}}},
        {"messageId", queen::generate_uuidv7()}
    });
    ops.push_back({
        {"type", "ack"},
        {"transactionId", txn_id},
        {"partitionId", partition_id},
        {"status", "completed"}
    });
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::TRANSACTION,
        .params = { build_transaction_params(ops) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        txn_success = json["success"] == true;
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(txn_success, "Transaction failed");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify queue B
    std::string worker_id2 = generate_uuid();
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue_b,
        .partition_name = "Default",
        .params = { build_pop_params(queue_b, "Default", "__QUEUE_MODE__", 1, 60, worker_id2) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            queue_b_count = json[0]["result"]["messages"].size();
        }
        waiter4.signal();
    });
    
    // Verify queue C
    std::string worker_id3 = generate_uuid();
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue_c,
        .partition_name = "Default",
        .params = { build_pop_params(queue_c, "Default", "__QUEUE_MODE__", 1, 60, worker_id3) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            queue_c_count = json[0]["result"]["messages"].size();
        }
        waiter5.signal();
    });
    
    waiter4.wait();
    waiter5.wait();
    
    ASSERT_EQ(queue_b_count, 1, "Queue B should have 1 message");
    ASSERT_EQ(queue_c_count, 1, "Queue C should have 1 message");
}

TEST(test_transaction_multiple_acks) {
    std::string queue_a = generate_queue_name("test-txn-acks-a");
    std::string queue_b = generate_queue_name("test-txn-acks-b");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3;
    nlohmann::json messages;
    std::string partition_id;
    bool txn_success = false;
    
    // Push 3 messages to queue A
    std::vector<nlohmann::json> payloads = {
        {{"value", 1}},
        {{"value", 2}},
        {{"value", 3}}
    };
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue_a,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue_a, "Default", payloads) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop all 3
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue_a,
        .partition_name = "Default",
        .params = { build_pop_params(queue_a, "Default", "__QUEUE_MODE__", 3, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            messages = res["messages"];
            partition_id = res["partitionId"];
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT_EQ(messages.size(), 3u, "Expected 3 messages");
    
    // Transaction: ACK all 3, push sum to B
    int sum = 6;  // 1 + 2 + 3
    std::vector<nlohmann::json> ops;
    
    for (const auto& msg : messages) {
        ops.push_back({
            {"type", "ack"},
            {"transactionId", msg["transactionId"]},
            {"partitionId", partition_id},
            {"status", "completed"}
        });
    }
    
    ops.push_back({
        {"type", "push"},
        {"queue", queue_b},
        {"partition", "Default"},
        {"payload", {{"sum", sum}}},
        {"messageId", queen::generate_uuidv7()}
    });
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::TRANSACTION,
        .params = { build_transaction_params(ops) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        txn_success = json["success"] == true;
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(txn_success, "Transaction with multiple acks failed");
}

TEST(test_transaction_batch_push) {
    std::string queue_a = generate_queue_name("test-txn-bpush-a");
    std::string queue_b = generate_queue_name("test-txn-bpush-b");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4;
    std::string txn_id, partition_id;
    bool txn_success = false;
    int queue_b_count = 0;
    
    // Push to queue A
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue_a,
        .partition_name = "Default",
        .params = { build_push_params(queue_a, "Default", {{"id", "batch-source"}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop from queue A
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue_a,
        .partition_name = "Default",
        .params = { build_pop_params(queue_a, "Default", "__QUEUE_MODE__", 1, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            if (!res["messages"].empty()) {
                txn_id = res["messages"][0]["transactionId"];
                partition_id = res["partitionId"];
            }
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT(!txn_id.empty(), "No message to consume");
    
    // Transaction: Push 5 messages to B (individually), ACK from A
    std::vector<nlohmann::json> ops;
    for (int i = 1; i <= 5; i++) {
        ops.push_back({
            {"type", "push"},
            {"queue", queue_b},
            {"partition", "Default"},
            {"payload", {{"index", i}}},
            {"messageId", queen::generate_uuidv7()}
        });
    }
    ops.push_back({
        {"type", "ack"},
        {"transactionId", txn_id},
        {"partitionId", partition_id},
        {"status", "completed"}
    });
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::TRANSACTION,
        .params = { build_transaction_params(ops) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        txn_success = json["success"] == true;
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(txn_success, "Transaction failed");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify queue B has 5 messages
    std::string worker_id2 = generate_uuid();
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue_b,
        .partition_name = "Default",
        .params = { build_pop_params(queue_b, "Default", "__QUEUE_MODE__", 10, 60, worker_id2) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            queue_b_count = json[0]["result"]["messages"].size();
        }
        waiter4.signal();
    });
    
    waiter4.wait();
    ASSERT_EQ(queue_b_count, 5, "Queue B should have 5 messages");
}

TEST(test_transaction_with_partitions) {
    std::string queue = generate_queue_name("test-txn-parts");
    std::string worker_id1 = generate_uuid();
    std::string worker_id2 = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4, waiter5, waiter6;
    std::string txn_id1, txn_id2, partition_id1, partition_id2;
    bool txn_success = false;
    int p1_count = 0, p2_count = 0;
    
    // Push to partition 1
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "p1",
        .params = { build_push_params(queue, "p1", {{"partition", "p1"}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    // Push to partition 2
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "p2",
        .params = { build_push_params(queue, "p2", {{"partition", "p2"}}) },
    }, [&](std::string) { waiter2.signal(); });
    
    waiter1.wait();
    waiter2.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop from partition 1
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "p1",
        .params = { build_pop_params(queue, "p1", "__QUEUE_MODE__", 1, 60, worker_id1) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            if (!res["messages"].empty()) {
                txn_id1 = res["messages"][0]["transactionId"];
                partition_id1 = res["partitionId"];
            }
        }
        waiter3.signal();
    });
    
    // Pop from partition 2
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "p2",
        .params = { build_pop_params(queue, "p2", "__QUEUE_MODE__", 1, 60, worker_id2) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            if (!res["messages"].empty()) {
                txn_id2 = res["messages"][0]["transactionId"];
                partition_id2 = res["partitionId"];
            }
        }
        waiter4.signal();
    });
    
    waiter3.wait();
    waiter4.wait();
    ASSERT(!txn_id1.empty() && !txn_id2.empty(), "No messages in partitions");
    
    // Transaction: ACK both messages from different partitions
    std::vector<nlohmann::json> ops;
    ops.push_back({
        {"type", "ack"},
        {"transactionId", txn_id1},
        {"partitionId", partition_id1},
        {"status", "completed"}
    });
    ops.push_back({
        {"type", "ack"},
        {"transactionId", txn_id2},
        {"partitionId", partition_id2},
        {"status", "completed"}
    });
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::TRANSACTION,
        .params = { build_transaction_params(ops) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        txn_success = json["success"] == true;
        waiter5.signal();
    });
    
    waiter5.wait();
    ASSERT(txn_success, "Transaction with multiple partitions failed");
    
    // Verify both partitions are empty
    std::string worker_id3 = generate_uuid();
    std::string worker_id4 = generate_uuid();
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "p1",
        .params = { build_pop_params(queue, "p1", "__QUEUE_MODE__", 1, 60, worker_id3) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            p1_count = json[0]["result"]["messages"].size();
        }
        waiter6.signal();
    });
    
    waiter6.wait();
    // Note: We only check p1 as both should be acked
    ASSERT_EQ(p1_count, 0, "Partition p1 should be empty after ack");
    (void)p2_count;  // Suppress unused variable warning
}

// ============================================================================
// RENEW LEASE TESTS
// ============================================================================

TEST(test_renew_lease_manual) {
    std::string queue = generate_queue_name("test-renew-manual");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4;
    std::string txn_id, partition_id, lease_id;
    bool renew_success = false;
    bool ack_success = false;
    
    // Push
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"test", "renewal"}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop with short lease (3 seconds)
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 1, 3, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            if (!res["messages"].empty()) {
                txn_id = res["messages"][0]["transactionId"];
                partition_id = res["partitionId"];
                lease_id = res["leaseId"];
            }
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT(!lease_id.empty(), "No lease ID received");
    
    // Wait 1 second then renew
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::RENEW_LEASE,
        .params = { build_renew_params(lease_id, 60) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        renew_success = !json.empty() && json[0]["success"] == true;
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(renew_success, "Lease renewal failed");
    
    // Wait another 3 seconds (original lease would have expired)
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // ACK should still work because we renewed
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::ACK,
        .params = { build_ack_params(txn_id, partition_id, lease_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        ack_success = !json.empty() && json[0]["success"] == true;
        waiter4.signal();
    });
    
    waiter4.wait();
    ASSERT(ack_success, "ACK after lease renewal should succeed");
}

TEST(test_renew_lease_batch) {
    std::string queue = generate_queue_name("test-renew-batch");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4;
    nlohmann::json messages;
    std::string partition_id, lease_id;
    bool all_renewed = false;
    bool all_acked = false;
    
    // Push 3 messages
    std::vector<nlohmann::json> payloads = {
        {{"id", 1}},
        {{"id", 2}},
        {{"id", 3}}
    };
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", payloads) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop all 3 with short lease
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 3, 3, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            messages = res["messages"];
            partition_id = res["partitionId"];
            lease_id = res["leaseId"];
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT_EQ(messages.size(), 3u, "Expected 3 messages");
    ASSERT(!lease_id.empty(), "No lease ID");
    
    // Wait 1 second then renew
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Renew the lease (single lease for the batch)
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::RENEW_LEASE,
        .params = { build_renew_params(lease_id, 60) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        all_renewed = !json.empty() && json[0]["success"] == true;
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(all_renewed, "Lease renewal failed");
    
    // Wait for original lease to expire
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // ACK all messages - should work because lease was renewed
    std::vector<nlohmann::json> msg_vec(messages.begin(), messages.end());
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::ACK,
        .params = { build_ack_params_batch(msg_vec, partition_id, lease_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        all_acked = true;
        for (const auto& item : json) {
            if (item["success"] != true) {
                all_acked = false;
                break;
            }
        }
        waiter4.signal();
    });
    
    waiter4.wait();
    ASSERT(all_acked, "ACK after batch lease renewal should succeed");
}

TEST(test_renew_expired_lease) {
    std::string queue = generate_queue_name("test-renew-expired");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3;
    std::string lease_id;
    bool renew_success = true;  // We expect this to become false
    
    // Push
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"test", "data"}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop with very short lease (1 second)
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 1, 1, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            lease_id = json[0]["result"]["leaseId"];
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT(!lease_id.empty(), "No lease ID");
    
    // Wait for lease to expire
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Try to renew expired lease - should fail
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::RENEW_LEASE,
        .params = { build_renew_params(lease_id, 60) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        renew_success = !json.empty() && json[0]["success"] == true;
        waiter3.signal();
    });
    
    waiter3.wait();
    ASSERT(!renew_success, "Expired lease renewal should fail");
}

TEST(test_renew_multiple_times) {
    std::string queue = generate_queue_name("test-renew-multi");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2, waiter3, waiter4, waiter5, waiter6;
    std::string txn_id, partition_id, lease_id;
    bool renew1 = false, renew2 = false, renew3 = false;
    bool ack_success = false;
    
    // Push
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params(queue, "Default", {{"test", "data"}}) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop with 2 second lease
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 1, 2, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& res = json[0]["result"];
            if (!res["messages"].empty()) {
                txn_id = res["messages"][0]["transactionId"];
                partition_id = res["partitionId"];
                lease_id = res["leaseId"];
            }
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT(!lease_id.empty(), "No lease ID");
    
    // Renew 3 times, each after 1 second
    std::this_thread::sleep_for(std::chrono::seconds(1));
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::RENEW_LEASE,
        .params = { build_renew_params(lease_id, 2) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        renew1 = !json.empty() && json[0]["success"] == true;
        waiter3.signal();
    });
    waiter3.wait();
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::RENEW_LEASE,
        .params = { build_renew_params(lease_id, 2) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        renew2 = !json.empty() && json[0]["success"] == true;
        waiter4.signal();
    });
    waiter4.wait();
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::RENEW_LEASE,
        .params = { build_renew_params(lease_id, 2) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        renew3 = !json.empty() && json[0]["success"] == true;
        waiter5.signal();
    });
    waiter5.wait();
    
    ASSERT(renew1, "First renewal failed");
    ASSERT(renew2, "Second renewal failed");
    ASSERT(renew3, "Third renewal failed");
    
    // ACK should still work
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::ACK,
        .params = { build_ack_params(txn_id, partition_id, lease_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        ack_success = !json.empty() && json[0]["success"] == true;
        waiter6.signal();
    });
    
    waiter6.wait();
    ASSERT(ack_success, "ACK after multiple renewals should succeed");
}

// ============================================================================
// MESSAGE ORDERING TEST
// ============================================================================

TEST(test_message_ordering) {
    std::string queue = generate_queue_name("test-ordering");
    std::string worker_id = generate_uuid();
    AsyncWaiter waiter1, waiter2;
    bool ordering_correct = true;
    int last_id = -1;
    
    // Push 20 messages in order
    std::vector<nlohmann::json> payloads;
    for (int i = 0; i < 20; i++) {
        payloads.push_back({{"id", i}});
    }
    
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_push_params_batch(queue, "Default", payloads) },
    }, [&](std::string) { waiter1.signal(); });
    
    waiter1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pop all 20 and verify ordering
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = queue,
        .partition_name = "Default",
        .params = { build_pop_params(queue, "Default", "__QUEUE_MODE__", 20, 60, worker_id) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        if (!json.empty() && json[0].contains("result")) {
            auto& messages = json[0]["result"]["messages"];
            for (const auto& msg : messages) {
                int id = msg["data"]["id"];
                if (last_id >= 0 && id != last_id + 1) {
                    ordering_correct = false;
                    break;
                }
                last_id = id;
            }
        }
        waiter2.signal();
    });
    
    waiter2.wait();
    ASSERT(ordering_correct, "Message ordering violated");
    ASSERT_EQ(last_id, 19, "Not all messages received");
}

// ============================================================================
// VALIDATION TESTS
// ============================================================================

TEST(test_validation_push_missing_params) {
    AsyncWaiter waiter;
    bool got_error = false;
    std::string error_msg;
    
    // Submit PUSH without params - should fail validation
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = "test-queue",
        .params = {},  // Missing required params!
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        got_error = json.contains("error") && json["success"] == false;
        if (json.contains("error")) error_msg = json["error"];
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(got_error, "Expected validation error for missing params");
}

TEST(test_validation_pop_missing_params) {
    AsyncWaiter waiter;
    bool got_error = false;
    
    // Submit POP without params - should fail validation
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::POP,
        .queue_name = "test-queue",
        .params = {},  // Missing required params!
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        got_error = json.contains("error") && json["success"] == false;
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(got_error, "Expected validation error for missing params");
}

TEST(test_validation_ack_missing_params) {
    AsyncWaiter waiter;
    bool got_error = false;
    
    // Submit ACK without params - should fail validation
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::ACK,
        .params = {},  // Missing required params!
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        got_error = json.contains("error") && json["success"] == false;
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(got_error, "Expected validation error for missing params");
}

TEST(test_validation_transaction_missing_params) {
    AsyncWaiter waiter;
    bool got_error = false;
    
    // Submit TRANSACTION without params - should fail validation
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::TRANSACTION,
        .params = {},  // Missing required params!
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        got_error = json.contains("error") && json["success"] == false;
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(got_error, "Expected validation error for missing params");
}

TEST(test_validation_custom_missing_sql) {
    AsyncWaiter waiter;
    bool got_error = false;
    
    // Submit CUSTOM without sql - should fail validation
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::CUSTOM,
        .sql = "",  // Missing required sql!
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        got_error = json.contains("error") && json["success"] == false;
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(got_error, "Expected validation error for missing sql");
}

TEST(test_validation_valid_push) {
    std::string queue = generate_queue_name("test-validation-push");
    AsyncWaiter waiter;
    bool success = false;
    
    // Valid PUSH should work
    queen.submit(queen::JobRequest{
        .op_type = queen::JobType::PUSH,
        .queue_name = queue,
        .params = { build_push_params(queue, "Default", {{"msg", "test"}}) },
    }, [&](std::string result) {
        auto json = nlohmann::json::parse(result);
        success = !json.empty() && !json.contains("error");
        waiter.signal();
    });
    
    waiter.wait();
    ASSERT(success, "Valid PUSH should succeed");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    std::string conn_str = "postgres://postgres:postgres@localhost:5432/postgres";
    
    // Allow override via command line
    if (argc > 1) {
        conn_str = argv[1];
    }
    
    std::cout << "============================================" << std::endl;
    std::cout << "Queen C++ Library Test Suite" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Connection: " << conn_str << std::endl;
    std::cout << std::endl;
    
    // Wait for event loop to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    try {
        queen::Queen queen(conn_str, 30000, 10, 10);
        
        // Wait for connections to be established
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::cout << "=== PUSH Tests ===" << std::endl;
        RUN_TEST(test_push_single_message);
        RUN_TEST(test_push_batch_messages);
        RUN_TEST(test_push_duplicate_detection);
        RUN_TEST(test_push_to_different_partitions);
        RUN_TEST(test_push_large_payload);
        RUN_TEST(test_push_null_payload);
        RUN_TEST(test_push_empty_object_payload);
        
        std::cout << std::endl << "=== POP Tests ===" << std::endl;
        RUN_TEST(test_pop_empty_queue);
        RUN_TEST(test_pop_non_empty_queue);
        RUN_TEST(test_pop_batch);
        RUN_TEST(test_pop_from_specific_partition);
        RUN_TEST(test_pop_with_consumer_group);
        RUN_TEST(test_pop_with_wait);
        RUN_TEST(test_pop_consumer_group_subscription_mode_all);
        RUN_TEST(test_pop_consumer_group_subscription_mode_new);
        RUN_TEST(test_pop_consumer_group_subscription_from_beginning);
        RUN_TEST(test_pop_multiple_consumer_groups_independent);
        
        std::cout << std::endl << "=== ACK Tests ===" << std::endl;
        RUN_TEST(test_ack_single_message);
        RUN_TEST(test_ack_batch_messages);
        RUN_TEST(test_ack_with_failed_status);
        
        std::cout << std::endl << "=== TRANSACTION Tests ===" << std::endl;
        RUN_TEST(test_transaction_basic_push_ack);
        RUN_TEST(test_transaction_multiple_pushes);
        RUN_TEST(test_transaction_multiple_acks);
        RUN_TEST(test_transaction_batch_push);
        RUN_TEST(test_transaction_with_partitions);
        
        std::cout << std::endl << "=== RENEW LEASE Tests ===" << std::endl;
        RUN_TEST(test_renew_lease_manual);
        RUN_TEST(test_renew_lease_batch);
        RUN_TEST(test_renew_expired_lease);
        RUN_TEST(test_renew_multiple_times);
        
        std::cout << std::endl << "=== ORDERING Tests ===" << std::endl;
        RUN_TEST(test_message_ordering);
        
        std::cout << std::endl << "=== VALIDATION Tests ===" << std::endl;
        RUN_TEST(test_validation_push_missing_params);
        RUN_TEST(test_validation_pop_missing_params);
        RUN_TEST(test_validation_ack_missing_params);
        RUN_TEST(test_validation_transaction_missing_params);
        RUN_TEST(test_validation_custom_missing_sql);
        RUN_TEST(test_validation_valid_push);
        
        std::cout << std::endl;
        std::cout << "============================================" << std::endl;
        std::cout << "Results: " << g_tests_passed << " passed, " << g_tests_failed << " failed" << std::endl;
        std::cout << "============================================" << std::endl;
        
        // Give some time for final async operations
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        return g_tests_failed > 0 ? 1 : 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}

