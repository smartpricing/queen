/**
 * test_contention.cpp - Benchmark to measure PUSH/POP contention
 * 
 * This test demonstrates the performance difference between:
 *   1. PUSH-only operations
 *   2. POP-only operations  
 *   3. Mixed PUSH+POP operations (concurrent)
 * 
 * Expected behavior: Mixed operations are slower due to:
 *   - partition_lookup table row-level locking (PUSH trigger vs POP SKIP LOCKED)
 *   - Single slot per job type in Queen's event loop
 * 
 * Usage:
 *   make test-contention
 *   # or manually:
 *   ./build/test_contention
 * 
 * Prerequisites:
 *   - PostgreSQL running with queen schema initialized
 *   - Connection string: postgres://postgres:postgres@localhost:5432/postgres
 */

#include "queen.hpp"
#include <chrono>
#include <atomic>
#include <thread>
#include <iomanip>
#include <mutex>
#include <condition_variable>

using namespace std::chrono;

// Configuration
const char* CONN_STRING = "postgres://postgres:postgres@localhost:5432/postgres";
const int DB_CONNECTIONS = 10;
const int TIMER_INTERVAL_MS = 10;

// Test parameters
const int OPERATIONS_PER_TEST = 1000;
const int WARMUP_OPS = 100;
const int SUSTAINED_DURATION_SEC = 5;  // For sustained load tests

// High-load test parameters (matching real scenario)
const int HIGH_LOAD_PUSH_STREAMS = 200;   // Simulates 1000 push connections
const int HIGH_LOAD_POP_STREAMS = 80;     // Simulates 400 pop connections
const int HIGH_LOAD_DURATION_SEC = 10;

// Unique queue names to avoid interference between tests
std::string get_unique_queue(const std::string& prefix) {
    static std::atomic<int> counter{0};
    return prefix + "_" + std::to_string(time(nullptr)) + "_" + std::to_string(counter++);
}

// Statistics
struct BenchmarkStats {
    std::atomic<int> completed{0};
    std::atomic<int> errors{0};
    steady_clock::time_point start_time;
    steady_clock::time_point end_time;
    
    void reset() {
        completed = 0;
        errors = 0;
    }
    
    double ops_per_sec() const {
        auto duration = duration_cast<milliseconds>(end_time - start_time).count();
        if (duration == 0) return 0;
        return (completed.load() * 1000.0) / duration;
    }
    
    double avg_latency_ms() const {
        if (completed == 0) return 0;
        auto duration = duration_cast<milliseconds>(end_time - start_time).count();
        return static_cast<double>(duration) / completed.load();
    }
};

// Wait for N completions with timeout
bool wait_for_completions(BenchmarkStats& stats, int expected, int timeout_sec = 30) {
    auto deadline = steady_clock::now() + seconds(timeout_sec);
    while (stats.completed.load() < expected && steady_clock::now() < deadline) {
        std::this_thread::sleep_for(milliseconds(10));
    }
    return stats.completed.load() >= expected;
}

// ============================================================================
// Test 1: PUSH-only benchmark
// ============================================================================
void benchmark_push_only(queen::Queen& q, int num_ops) {
    std::string queue = get_unique_queue("push_bench");
    BenchmarkStats stats;
    
    spdlog::info("=== PUSH-ONLY Benchmark ({} ops) ===", num_ops);
    spdlog::info("Queue: {}", queue);
    
    stats.start_time = steady_clock::now();
    
    for (int i = 0; i < num_ops; i++) {
        std::string partition = "p" + std::to_string(i % 10);  // 10 partitions
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"i", i}, {"data", "test payload"}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&stats, i](std::string result) {
            try {
                auto json = nlohmann::json::parse(result);
                if (json.is_array() && !json.empty()) {
                    stats.completed++;
                } else {
                    stats.errors++;
                    spdlog::warn("PUSH {} unexpected result: {}", i, result);
                }
            } catch (...) {
                stats.errors++;
            }
        });
    }
    
    if (!wait_for_completions(stats, num_ops)) {
        spdlog::error("PUSH-only timed out! Completed: {}/{}", stats.completed.load(), num_ops);
    }
    
    stats.end_time = steady_clock::now();
    
    spdlog::info("  Completed: {}, Errors: {}", stats.completed.load(), stats.errors.load());
    spdlog::info("  Throughput: {:.1f} ops/sec", stats.ops_per_sec());
    spdlog::info("  Avg latency: {:.2f} ms", stats.avg_latency_ms());
    spdlog::info("");
}

// ============================================================================
// Test 2: POP-only benchmark (requires pre-populated queue)
// ============================================================================
void benchmark_pop_only(queen::Queen& q, int num_ops) {
    std::string queue = get_unique_queue("pop_bench");
    BenchmarkStats push_stats, pop_stats;
    
    spdlog::info("=== POP-ONLY Benchmark ({} ops) ===", num_ops);
    spdlog::info("Queue: {}", queue);
    
    // First, populate the queue with messages
    spdlog::info("  Pre-populating queue with {} messages...", num_ops);
    for (int i = 0; i < num_ops; i++) {
        std::string partition = "p" + std::to_string(i % 10);
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"i", i}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&push_stats](std::string) {
            push_stats.completed++;
        });
    }
    
    wait_for_completions(push_stats, num_ops);
    spdlog::info("  Queue populated with {} messages", push_stats.completed.load());
    
    // Small delay to let things settle
    std::this_thread::sleep_for(milliseconds(100));
    
    // Now benchmark POP operations
    pop_stats.start_time = steady_clock::now();
    
    for (int i = 0; i < num_ops; i++) {
        std::string partition = "p" + std::to_string(i % 10);
        std::string worker_id = queen::generate_uuidv7();
        
        nlohmann::json pop_params = nlohmann::json::array();
        pop_params.push_back({
            {"idx", 0},
            {"queue_name", queue},
            {"partition_name", partition},
            {"consumer_group", "__QUEUE_MODE__"},
            {"batch_size", 1},
            {"lease_seconds", 60},
            {"worker_id", worker_id},
            {"sub_mode", "all"},
            {"auto_ack", true}  // Auto-ack to avoid ACK overhead
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::POP,
            .request_id = worker_id,
            .queue_name = queue,
            .partition_name = partition,
            .params = {pop_params.dump()},
        }, [&pop_stats](std::string /*result*/) {
            pop_stats.completed++;
        });
    }
    
    if (!wait_for_completions(pop_stats, num_ops)) {
        spdlog::error("POP-only timed out! Completed: {}/{}", pop_stats.completed.load(), num_ops);
    }
    
    pop_stats.end_time = steady_clock::now();
    
    spdlog::info("  Completed: {}, Errors: {}", pop_stats.completed.load(), pop_stats.errors.load());
    spdlog::info("  Throughput: {:.1f} ops/sec", pop_stats.ops_per_sec());
    spdlog::info("  Avg latency: {:.2f} ms", pop_stats.avg_latency_ms());
    spdlog::info("");
}

// ============================================================================
// Test 3: Mixed PUSH+POP benchmark (concurrent operations)
// ============================================================================
void benchmark_mixed(queen::Queen& q, int num_ops) {
    std::string queue = get_unique_queue("mixed_bench");
    BenchmarkStats push_stats, pop_stats;
    
    int push_ops = num_ops / 2;
    int pop_ops = num_ops / 2;
    
    spdlog::info("=== MIXED PUSH+POP Benchmark ({} push + {} pop) ===", push_ops, pop_ops);
    spdlog::info("Queue: {}", queue);
    
    // First seed the queue with some messages so POP has something to find
    spdlog::info("  Seeding queue with {} messages...", push_ops);
    BenchmarkStats seed_stats;
    for (int i = 0; i < push_ops; i++) {
        std::string partition = "p" + std::to_string(i % 10);
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"seed", i}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&seed_stats](std::string) {
            seed_stats.completed++;
        });
    }
    wait_for_completions(seed_stats, push_ops);
    spdlog::info("  Seeded {} messages", seed_stats.completed.load());
    
    std::this_thread::sleep_for(milliseconds(100));
    
    // Now run PUSH and POP concurrently
    auto start_time = steady_clock::now();
    push_stats.start_time = start_time;
    pop_stats.start_time = start_time;
    
    // Interleave PUSH and POP submissions to maximize contention
    for (int i = 0; i < std::max(push_ops, pop_ops); i++) {
        // Submit a PUSH
        if (i < push_ops) {
            std::string partition = "p" + std::to_string(i % 10);
            nlohmann::json items = nlohmann::json::array();
            items.push_back({
                {"queue", queue},
                {"partition", partition},
                {"payload", {{"mixed_push", i}}}
            });
            
            q.submit(queen::JobRequest{
                .op_type = queen::JobType::PUSH,
                .queue_name = queue,
                .partition_name = partition,
                .params = {items.dump()},
            }, [&push_stats](std::string result) {
                try {
                    auto json = nlohmann::json::parse(result);
                    if (json.is_array()) {
                        push_stats.completed++;
                    } else {
                        push_stats.errors++;
                    }
                } catch (...) {
                    push_stats.errors++;
                }
            });
        }
        
        // Submit a POP (to the same partition - maximizes contention!)
        if (i < pop_ops) {
            std::string partition = "p" + std::to_string(i % 10);
            std::string worker_id = queen::generate_uuidv7();
            
            nlohmann::json pop_params = nlohmann::json::array();
            pop_params.push_back({
                {"idx", 0},
                {"queue_name", queue},
                {"partition_name", partition},
                {"consumer_group", "__QUEUE_MODE__"},
                {"batch_size", 1},
                {"lease_seconds", 60},
                {"worker_id", worker_id},
                {"sub_mode", "all"},
                {"auto_ack", true}
            });
            
            q.submit(queen::JobRequest{
                .op_type = queen::JobType::POP,
                .request_id = worker_id,
                .queue_name = queue,
                .partition_name = partition,
                .params = {pop_params.dump()},
            }, [&pop_stats](std::string) {
                pop_stats.completed++;
            });
        }
    }
    
    // Wait for both to complete
    int total_expected = push_ops + pop_ops;
    auto deadline = steady_clock::now() + seconds(60);
    while ((push_stats.completed.load() + pop_stats.completed.load()) < total_expected 
           && steady_clock::now() < deadline) {
        std::this_thread::sleep_for(milliseconds(10));
    }
    
    auto end_time = steady_clock::now();
    push_stats.end_time = end_time;
    pop_stats.end_time = end_time;
    
    auto total_duration = duration_cast<milliseconds>(end_time - start_time).count();
    int total_completed = push_stats.completed.load() + pop_stats.completed.load();
    double total_ops_per_sec = (total_completed * 1000.0) / total_duration;
    
    spdlog::info("  PUSH: {} completed, {} errors", push_stats.completed.load(), push_stats.errors.load());
    spdlog::info("  POP:  {} completed, {} errors", pop_stats.completed.load(), pop_stats.errors.load());
    spdlog::info("  Combined throughput: {:.1f} ops/sec", total_ops_per_sec);
    spdlog::info("  Duration: {} ms", total_duration);
    spdlog::info("");
}

// ============================================================================
// Test 4: Wildcard POP contention (worst case for partition_lookup locking)
// ============================================================================
void benchmark_wildcard_contention(queen::Queen& q, int num_ops) {
    std::string queue = get_unique_queue("wildcard_bench");
    BenchmarkStats push_stats, pop_stats;
    
    int push_ops = num_ops / 2;
    int pop_ops = num_ops / 2;
    
    spdlog::info("=== WILDCARD POP Contention Benchmark ({} push + {} wildcard pop) ===", push_ops, pop_ops);
    spdlog::info("Queue: {}", queue);
    spdlog::info("  This test uses wildcard POP (no partition specified)");
    spdlog::info("  which triggers FOR UPDATE SKIP LOCKED on partition_lookup");
    
    // Seed the queue
    BenchmarkStats seed_stats;
    for (int i = 0; i < push_ops; i++) {
        std::string partition = "p" + std::to_string(i % 10);
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"seed", i}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&seed_stats](std::string) {
            seed_stats.completed++;
        });
    }
    wait_for_completions(seed_stats, push_ops);
    std::this_thread::sleep_for(milliseconds(100));
    
    auto start_time = steady_clock::now();
    push_stats.start_time = start_time;
    pop_stats.start_time = start_time;
    
    // Interleave PUSH and WILDCARD POP
    for (int i = 0; i < std::max(push_ops, pop_ops); i++) {
        // PUSH to specific partition
        if (i < push_ops) {
            std::string partition = "p" + std::to_string(i % 10);
            nlohmann::json items = nlohmann::json::array();
            items.push_back({
                {"queue", queue},
                {"partition", partition},
                {"payload", {{"wildcard_push", i}}}
            });
            
            q.submit(queen::JobRequest{
                .op_type = queen::JobType::PUSH,
                .queue_name = queue,
                .partition_name = partition,
                .params = {items.dump()},
            }, [&push_stats](std::string) {
                push_stats.completed++;
            });
        }
        
        // WILDCARD POP (partition_name is empty - triggers discovery logic)
        if (i < pop_ops) {
            std::string worker_id = queen::generate_uuidv7();
            
            nlohmann::json pop_params = nlohmann::json::array();
            pop_params.push_back({
                {"idx", 0},
                {"queue_name", queue},
                // NOTE: partition_name omitted = wildcard discovery
                {"consumer_group", "__QUEUE_MODE__"},
                {"batch_size", 1},
                {"lease_seconds", 60},
                {"worker_id", worker_id},
                {"sub_mode", "all"},
                {"auto_ack", true}
            });
            
            q.submit(queen::JobRequest{
                .op_type = queen::JobType::POP,
                .request_id = worker_id,
                .queue_name = queue,
                .partition_name = "",  // Wildcard!
                .params = {pop_params.dump()},
            }, [&pop_stats](std::string) {
                pop_stats.completed++;
            });
        }
    }
    
    // Wait for completion
    int total_expected = push_ops + pop_ops;
    auto deadline = steady_clock::now() + seconds(60);
    while ((push_stats.completed.load() + pop_stats.completed.load()) < total_expected 
           && steady_clock::now() < deadline) {
        std::this_thread::sleep_for(milliseconds(10));
    }
    
    auto end_time = steady_clock::now();
    auto total_duration = duration_cast<milliseconds>(end_time - start_time).count();
    int total_completed = push_stats.completed.load() + pop_stats.completed.load();
    double total_ops_per_sec = (total_completed * 1000.0) / total_duration;
    
    spdlog::info("  PUSH: {} completed", push_stats.completed.load());
    spdlog::info("  Wildcard POP: {} completed", pop_stats.completed.load());
    spdlog::info("  Combined throughput: {:.1f} ops/sec", total_ops_per_sec);
    spdlog::info("  Duration: {} ms", total_duration);
    spdlog::info("");
}

// ============================================================================
// Test 5: Sustained Load - measures real contention over time
// ============================================================================
// This test better captures real-world contention by:
// 1. Running continuous PUSH operations
// 2. Running POP with wait=true (long-polling) that goes into backoff
// 3. Measuring how backoff/wakeup cycles affect throughput
void benchmark_sustained_load(queen::Queen& q, int duration_sec) {
    std::string queue = get_unique_queue("sustained_bench");
    
    std::atomic<int> push_completed{0};
    std::atomic<int> pop_completed{0};
    std::atomic<int> pop_empty{0};  // POPs that returned no messages
    std::atomic<bool> running{true};
    
    spdlog::info("=== SUSTAINED LOAD Benchmark ({} seconds) ===", duration_sec);
    spdlog::info("Queue: {}", queue);
    spdlog::info("  Continuous PUSH + long-polling POP with backoff");
    
    auto start_time = steady_clock::now();
    auto end_time = start_time + seconds(duration_sec);
    
    // Function to submit a PUSH
    std::function<void()> submit_push = [&]() {
        if (!running.load()) return;
        
        std::string partition = "p" + std::to_string(push_completed.load() % 10);
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"ts", duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&](std::string /*result*/) {
            push_completed++;
            // Submit another PUSH to keep sustained load
            if (running.load()) {
                submit_push();
            }
        });
    };
    
    // Function to submit a long-polling POP
    std::function<void()> submit_pop = [&]() {
        if (!running.load()) return;
        
        std::string worker_id = queen::generate_uuidv7();
        std::string partition = "p" + std::to_string(pop_completed.load() % 10);
        
        nlohmann::json pop_params = nlohmann::json::array();
        pop_params.push_back({
            {"idx", 0},
            {"queue_name", queue},
            {"partition_name", partition},
            {"consumer_group", "__QUEUE_MODE__"},
            {"batch_size", 1},
            {"lease_seconds", 60},
            {"worker_id", worker_id},
            {"sub_mode", "all"},
            {"auto_ack", true}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::POP,
            .request_id = worker_id,
            .queue_name = queue,
            .partition_name = partition,
            .params = {pop_params.dump()},
            // Enable long-polling with 500ms timeout
            .wait_deadline = steady_clock::now() + milliseconds(500),
            .next_check = steady_clock::now(),
        }, [&](std::string result) {
            try {
                auto json = nlohmann::json::parse(result);
                if (json.is_array() && !json.empty() && json[0].contains("result")) {
                    auto& messages = json[0]["result"]["messages"];
                    if (messages.empty()) {
                        pop_empty++;
                    } else {
                        pop_completed++;
                    }
                }
            } catch (...) {
                pop_empty++;
            }
            // Submit another POP
            if (running.load()) {
                submit_pop();
            }
        });
    };
    
    // Start multiple PUSH and POP streams (simulates concurrent clients)
    const int PUSH_STREAMS = 5;
    const int POP_STREAMS = 5;
    
    spdlog::info("  Starting {} PUSH streams + {} POP streams...", PUSH_STREAMS, POP_STREAMS);
    
    for (int i = 0; i < PUSH_STREAMS; i++) {
        submit_push();
    }
    for (int i = 0; i < POP_STREAMS; i++) {
        submit_pop();
    }
    
    // Wait for duration
    while (steady_clock::now() < end_time) {
        std::this_thread::sleep_for(milliseconds(100));
    }
    
    running.store(false);
    
    // Let pending operations complete
    std::this_thread::sleep_for(milliseconds(500));
    
    auto actual_duration = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
    double push_rate = (push_completed.load() * 1000.0) / actual_duration;
    double pop_rate = (pop_completed.load() * 1000.0) / actual_duration;
    double empty_rate = (pop_empty.load() * 1000.0) / actual_duration;
    
    spdlog::info("  Duration: {} ms", actual_duration);
    spdlog::info("  PUSH: {} completed ({:.1f}/sec)", push_completed.load(), push_rate);
    spdlog::info("  POP:  {} with messages ({:.1f}/sec)", pop_completed.load(), pop_rate);
    spdlog::info("  POP:  {} empty (backoff hits) ({:.1f}/sec)", pop_empty.load(), empty_rate);
    spdlog::info("  Empty ratio: {:.1f}%", (pop_empty.load() * 100.0) / std::max(1, pop_completed.load() + pop_empty.load()));
    spdlog::info("");
}

// ============================================================================
// Test 6: Single Partition Contention (maximum row lock contention)
// ============================================================================
void benchmark_single_partition_contention(queen::Queen& q, int duration_sec) {
    std::string queue = get_unique_queue("single_part_bench");
    const std::string SINGLE_PARTITION = "contention_target";
    
    std::atomic<int> push_completed{0};
    std::atomic<int> pop_completed{0};
    std::atomic<int> pop_empty{0};
    std::atomic<bool> running{true};
    
    spdlog::info("=== SINGLE PARTITION Contention ({} seconds) ===", duration_sec);
    spdlog::info("Queue: {}, Partition: {}", queue, SINGLE_PARTITION);
    spdlog::info("  All ops target ONE partition = maximum partition_lookup row contention");
    
    auto start_time = steady_clock::now();
    auto end_time = start_time + seconds(duration_sec);
    
    std::function<void()> submit_push = [&]() {
        if (!running.load()) return;
        
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", SINGLE_PARTITION},
            {"payload", {{"ts", duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count()}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = SINGLE_PARTITION,
            .params = {items.dump()},
        }, [&](std::string /*result*/) {
            push_completed++;
            if (running.load()) submit_push();
        });
    };
    
    std::function<void()> submit_pop = [&]() {
        if (!running.load()) return;
        
        std::string worker_id = queen::generate_uuidv7();
        nlohmann::json pop_params = nlohmann::json::array();
        pop_params.push_back({
            {"idx", 0},
            {"queue_name", queue},
            {"partition_name", SINGLE_PARTITION},
            {"consumer_group", "__QUEUE_MODE__"},
            {"batch_size", 1},
            {"lease_seconds", 60},
            {"worker_id", worker_id},
            {"sub_mode", "all"},
            {"auto_ack", true}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::POP,
            .request_id = worker_id,
            .queue_name = queue,
            .partition_name = SINGLE_PARTITION,
            .params = {pop_params.dump()},
            .wait_deadline = steady_clock::now() + milliseconds(500),
            .next_check = steady_clock::now(),
        }, [&](std::string result) {
            try {
                auto json = nlohmann::json::parse(result);
                if (json.is_array() && !json.empty() && json[0].contains("result")) {
                    auto& messages = json[0]["result"]["messages"];
                    if (messages.empty()) pop_empty++;
                    else pop_completed++;
                }
            } catch (...) {
                pop_empty++;
            }
            if (running.load()) submit_pop();
        });
    };
    
    // Start streams
    for (int i = 0; i < 5; i++) submit_push();
    for (int i = 0; i < 5; i++) submit_pop();
    
    while (steady_clock::now() < end_time) {
        std::this_thread::sleep_for(milliseconds(100));
    }
    
    running.store(false);
    std::this_thread::sleep_for(milliseconds(500));
    
    auto actual_duration = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
    double push_rate = (push_completed.load() * 1000.0) / actual_duration;
    double pop_rate = (pop_completed.load() * 1000.0) / actual_duration;
    
    spdlog::info("  Duration: {} ms", actual_duration);
    spdlog::info("  PUSH: {} completed ({:.1f}/sec)", push_completed.load(), push_rate);
    spdlog::info("  POP:  {} with messages ({:.1f}/sec)", pop_completed.load(), pop_rate);
    spdlog::info("  POP:  {} empty (contention/backoff)", pop_empty.load());
    spdlog::info("  Empty ratio: {:.1f}%", (pop_empty.load() * 100.0) / std::max(1, pop_completed.load() + pop_empty.load()));
    spdlog::info("");
}

// ============================================================================
// Test 7: PUSH-only sustained (baseline for comparison)
// ============================================================================
void benchmark_push_only_sustained(queen::Queen& q, int duration_sec) {
    std::string queue = get_unique_queue("push_sustained");
    
    std::atomic<int> completed{0};
    std::atomic<bool> running{true};
    
    spdlog::info("=== PUSH-ONLY Sustained ({} seconds) ===", duration_sec);
    spdlog::info("Queue: {}", queue);
    
    auto start_time = steady_clock::now();
    auto end_time = start_time + seconds(duration_sec);
    
    std::function<void()> submit_push = [&]() {
        if (!running.load()) return;
        
        std::string partition = "p" + std::to_string(completed.load() % 10);
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"i", completed.load()}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&](std::string /*result*/) {
            completed++;
            if (running.load()) submit_push();
        });
    };
    
    // Start 10 concurrent streams
    for (int i = 0; i < 10; i++) submit_push();
    
    while (steady_clock::now() < end_time) {
        std::this_thread::sleep_for(milliseconds(100));
    }
    
    running.store(false);
    std::this_thread::sleep_for(milliseconds(300));
    
    auto actual_duration = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
    double rate = (completed.load() * 1000.0) / actual_duration;
    
    spdlog::info("  Completed: {} ({:.1f}/sec)", completed.load(), rate);
    spdlog::info("");
}

// ============================================================================
// Test 8: HIGH LOAD - Simulates 1000 PUSH + 400 POP connections
// ============================================================================
void benchmark_high_load_push_only(queen::Queen& q, int num_streams, int duration_sec) {
    std::string queue = get_unique_queue("highload_push");
    
    std::atomic<int> completed{0};
    std::atomic<bool> running{true};
    
    spdlog::info("=== HIGH LOAD PUSH-ONLY ({} streams, {} seconds) ===", num_streams, duration_sec);
    spdlog::info("Queue: {}", queue);
    
    auto start_time = steady_clock::now();
    auto end_time = start_time + seconds(duration_sec);
    
    std::function<void()> submit_push = [&]() {
        if (!running.load()) return;
        
        std::string partition = "p" + std::to_string(completed.load() % 100);  // 100 partitions
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"i", completed.load()}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&](std::string /*result*/) {
            completed++;
            if (running.load()) submit_push();
        });
    };
    
    for (int i = 0; i < num_streams; i++) submit_push();
    
    while (steady_clock::now() < end_time) {
        std::this_thread::sleep_for(milliseconds(100));
    }
    
    running.store(false);
    std::this_thread::sleep_for(milliseconds(500));
    
    auto actual_duration = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
    double rate = (completed.load() * 1000.0) / actual_duration;
    
    spdlog::info("  Completed: {} ({:.0f}/sec)", completed.load(), rate);
    spdlog::info("");
}

void benchmark_high_load_pop_only(queen::Queen& q, int num_streams, int duration_sec) {
    std::string queue = get_unique_queue("highload_pop");
    
    std::atomic<int> push_done{0};
    std::atomic<int> pop_done{0};
    std::atomic<bool> running{true};
    
    spdlog::info("=== HIGH LOAD POP-ONLY ({} streams, {} seconds) ===", num_streams, duration_sec);
    spdlog::info("Queue: {}", queue);
    
    // Pre-populate with lots of messages
    int seed_count = num_streams * 100;
    spdlog::info("  Seeding {} messages...", seed_count);
    
    for (int i = 0; i < seed_count; i++) {
        std::string partition = "p" + std::to_string(i % 100);
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"seed", i}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&](std::string /*result*/) {
            push_done++;
        });
    }
    
    // Wait for seeding
    while (push_done.load() < seed_count) {
        std::this_thread::sleep_for(milliseconds(50));
    }
    spdlog::info("  Seeded {} messages", push_done.load());
    std::this_thread::sleep_for(milliseconds(200));
    
    auto start_time = steady_clock::now();
    auto end_time = start_time + seconds(duration_sec);
    
    std::function<void()> submit_pop = [&]() {
        if (!running.load()) return;
        
        std::string worker_id = queen::generate_uuidv7();
        std::string partition = "p" + std::to_string(pop_done.load() % 100);
        
        nlohmann::json pop_params = nlohmann::json::array();
        pop_params.push_back({
            {"idx", 0},
            {"queue_name", queue},
            {"partition_name", partition},
            {"consumer_group", "__QUEUE_MODE__"},
            {"batch_size", 1},
            {"lease_seconds", 60},
            {"worker_id", worker_id},
            {"sub_mode", "all"},
            {"auto_ack", true}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::POP,
            .request_id = worker_id,
            .queue_name = queue,
            .partition_name = partition,
            .params = {pop_params.dump()},
        }, [&](std::string /*result*/) {
            pop_done++;
            if (running.load()) submit_pop();
        });
    };
    
    pop_done.store(0);  // Reset for POP counting
    for (int i = 0; i < num_streams; i++) submit_pop();
    
    while (steady_clock::now() < end_time) {
        std::this_thread::sleep_for(milliseconds(100));
    }
    
    running.store(false);
    std::this_thread::sleep_for(milliseconds(500));
    
    auto actual_duration = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
    double rate = (pop_done.load() * 1000.0) / actual_duration;
    
    spdlog::info("  POP Completed: {} ({:.0f}/sec)", pop_done.load(), rate);
    spdlog::info("");
}

void benchmark_high_load_combined(queen::Queen& q, int push_streams, int pop_streams, int duration_sec) {
    std::string queue = get_unique_queue("highload_combined");
    
    std::atomic<int> push_done{0};
    std::atomic<int> pop_done{0};
    std::atomic<int> pop_empty{0};
    std::atomic<bool> running{true};
    
    spdlog::info("=== HIGH LOAD COMBINED ({} PUSH + {} POP streams, {} seconds) ===", 
                 push_streams, pop_streams, duration_sec);
    spdlog::info("Queue: {}", queue);
    spdlog::info("  This should show the dramatic slowdown!");
    
    // Seed with some initial messages
    int seed_count = pop_streams * 10;
    spdlog::info("  Seeding {} messages...", seed_count);
    std::atomic<int> seed_done{0};
    
    for (int i = 0; i < seed_count; i++) {
        std::string partition = "p" + std::to_string(i % 100);
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"seed", i}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&](std::string /*result*/) {
            seed_done++;
        });
    }
    
    while (seed_done.load() < seed_count) {
        std::this_thread::sleep_for(milliseconds(50));
    }
    std::this_thread::sleep_for(milliseconds(200));
    
    auto start_time = steady_clock::now();
    auto end_time = start_time + seconds(duration_sec);
    
    // PUSH stream
    std::function<void()> submit_push = [&]() {
        if (!running.load()) return;
        
        std::string partition = "p" + std::to_string(push_done.load() % 100);
        nlohmann::json items = nlohmann::json::array();
        items.push_back({
            {"queue", queue},
            {"partition", partition},
            {"payload", {{"push", push_done.load()}}}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = queue,
            .partition_name = partition,
            .params = {items.dump()},
        }, [&](std::string /*result*/) {
            push_done++;
            if (running.load()) submit_push();
        });
    };
    
    // POP stream
    std::function<void()> submit_pop = [&]() {
        if (!running.load()) return;
        
        std::string worker_id = queen::generate_uuidv7();
        std::string partition = "p" + std::to_string(pop_done.load() % 100);
        
        nlohmann::json pop_params = nlohmann::json::array();
        pop_params.push_back({
            {"idx", 0},
            {"queue_name", queue},
            {"partition_name", partition},
            {"consumer_group", "__QUEUE_MODE__"},
            {"batch_size", 1},
            {"lease_seconds", 60},
            {"worker_id", worker_id},
            {"sub_mode", "all"},
            {"auto_ack", true}
        });
        
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::POP,
            .request_id = worker_id,
            .queue_name = queue,
            .partition_name = partition,
            .params = {pop_params.dump()},
        }, [&](std::string result) {
            try {
                auto json = nlohmann::json::parse(result);
                if (json.is_array() && !json.empty() && json[0].contains("result")) {
                    if (json[0]["result"]["messages"].empty()) {
                        pop_empty++;
                    } else {
                        pop_done++;
                    }
                }
            } catch (...) {}
            if (running.load()) submit_pop();
        });
    };
    
    // Start all streams simultaneously
    spdlog::info("  Starting {} PUSH + {} POP streams...", push_streams, pop_streams);
    for (int i = 0; i < push_streams; i++) submit_push();
    for (int i = 0; i < pop_streams; i++) submit_pop();
    
    while (steady_clock::now() < end_time) {
        std::this_thread::sleep_for(milliseconds(100));
    }
    
    running.store(false);
    std::this_thread::sleep_for(milliseconds(500));
    
    auto actual_duration = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
    double push_rate = (push_done.load() * 1000.0) / actual_duration;
    double pop_rate = (pop_done.load() * 1000.0) / actual_duration;
    double combined_rate = ((push_done.load() + pop_done.load()) * 1000.0) / actual_duration;
    
    spdlog::info("  Duration: {} ms", actual_duration);
    spdlog::info("  PUSH: {} ({:.0f}/sec)", push_done.load(), push_rate);
    spdlog::info("  POP:  {} with messages ({:.0f}/sec)", pop_done.load(), pop_rate);
    spdlog::info("  POP:  {} empty", pop_empty.load());
    spdlog::info("  COMBINED: {:.0f}/sec", combined_rate);
    spdlog::info("");
}

// ============================================================================
// Main
// ============================================================================
int main() {
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    
    spdlog::info("╔════════════════════════════════════════════════════════════╗");
    spdlog::info("║        Queen PUSH/POP Contention Benchmark                 ║");
    spdlog::info("╚════════════════════════════════════════════════════════════╝");
    spdlog::info("");
    spdlog::info("Configuration:");
    spdlog::info("  DB connections: {}", DB_CONNECTIONS);
    spdlog::info("  Timer interval: {} ms", TIMER_INTERVAL_MS);
    spdlog::info("  Operations per test: {}", OPERATIONS_PER_TEST);
    spdlog::info("");
    
    // Initialize Queen
    spdlog::info("Initializing Queen...");
    queen::Queen q(CONN_STRING, 30000, DB_CONNECTIONS, TIMER_INTERVAL_MS);
    
    // Wait for initialization
    std::this_thread::sleep_for(milliseconds(500));
    spdlog::info("Queen initialized.\n");
    
    // Warmup
    spdlog::info("Warmup ({} ops)...\n", WARMUP_OPS);
    BenchmarkStats warmup;
    std::string warmup_queue = get_unique_queue("warmup");
    for (int i = 0; i < WARMUP_OPS; i++) {
        nlohmann::json items = nlohmann::json::array();
        items.push_back({{"queue", warmup_queue}, {"partition", "p0"}, {"payload", {{"w", i}}}});
        q.submit(queen::JobRequest{
            .op_type = queen::JobType::PUSH,
            .queue_name = warmup_queue,
            .partition_name = "p0",
            .params = {items.dump()},
        }, [&warmup](std::string) { warmup.completed++; });
    }
    wait_for_completions(warmup, WARMUP_OPS);
    spdlog::info("Warmup complete.\n");
    
    // Run benchmarks
    spdlog::info("════════════════════════════════════════════════════════════");
    benchmark_push_only(q, OPERATIONS_PER_TEST);
    
    spdlog::info("════════════════════════════════════════════════════════════");
    benchmark_pop_only(q, OPERATIONS_PER_TEST);
    
    spdlog::info("════════════════════════════════════════════════════════════");
    benchmark_mixed(q, OPERATIONS_PER_TEST);
    
    spdlog::info("════════════════════════════════════════════════════════════");
    benchmark_wildcard_contention(q, OPERATIONS_PER_TEST);
    
    // Sustained load tests (better measure of real-world contention)
    spdlog::info("════════════════════════════════════════════════════════════");
    spdlog::info("SUSTAINED LOAD TESTS (more realistic contention measurement)");
    spdlog::info("════════════════════════════════════════════════════════════");
    
    benchmark_push_only_sustained(q, SUSTAINED_DURATION_SEC);
    
    spdlog::info("════════════════════════════════════════════════════════════");
    benchmark_sustained_load(q, SUSTAINED_DURATION_SEC);
    
    spdlog::info("════════════════════════════════════════════════════════════");
    benchmark_single_partition_contention(q, SUSTAINED_DURATION_SEC);
    
    // HIGH LOAD TESTS - Simulates real scenario with 1000 PUSH + 400 POP
    spdlog::info("════════════════════════════════════════════════════════════");
    spdlog::info("HIGH LOAD TESTS (simulating 1000 PUSH + 400 POP connections)");
    spdlog::info("════════════════════════════════════════════════════════════");
    
    benchmark_high_load_push_only(q, HIGH_LOAD_PUSH_STREAMS, HIGH_LOAD_DURATION_SEC);
    
    spdlog::info("════════════════════════════════════════════════════════════");
    benchmark_high_load_pop_only(q, HIGH_LOAD_POP_STREAMS, HIGH_LOAD_DURATION_SEC);
    
    spdlog::info("════════════════════════════════════════════════════════════");
    benchmark_high_load_combined(q, HIGH_LOAD_PUSH_STREAMS, HIGH_LOAD_POP_STREAMS, HIGH_LOAD_DURATION_SEC);
    
    // Summary
    spdlog::info("════════════════════════════════════════════════════════════");
    spdlog::info("BENCHMARK COMPLETE");
    spdlog::info("");
    spdlog::info("Key metrics to compare:");
    spdlog::info("  1. HIGH LOAD PUSH-only rate vs COMBINED PUSH rate");
    spdlog::info("  2. HIGH LOAD POP-only rate vs COMBINED POP rate");
    spdlog::info("  3. If COMBINED << PUSH-only + POP-only, contention is confirmed");
    spdlog::info("");
    
    // Keep alive briefly to see final logs
    std::this_thread::sleep_for(milliseconds(500));
    
    return 0;
}

