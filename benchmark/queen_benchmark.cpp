/**
 * ┌───────────────────────────────────────────────────────────────┐
 * │                                                               │
 * │   ███████╗ ██╗   ██╗ ███████╗ ███████╗ ███╗   ██╗           │
 * │   ██╔═══██╗██║   ██║ ██╔════╝ ██╔════╝ ████╗  ██║           │
 * │   ██║   ██║██║   ██║ █████╗   █████╗   ██╔██╗ ██║           │
 * │   ██║▄▄ ██║██║   ██║ ██╔══╝   ██╔══╝   ██║╚██╗██║           │
 * │   ╚██████╔╝╚██████╔╝ ███████╗ ███████╗ ██║ ╚████║           │
 * │    ╚══▀▀═╝  ╚═════╝  ╚══════╝ ╚══════╝ ╚═╝  ╚═══╝           │
 * │                                                               │
 * │                  Performance Benchmark Tool                   │
 * │                                                               │
 * └───────────────────────────────────────────────────────────────┘
 * 
 * Benchmark tool for Queen Message Queue C++ Client
 * 
 * Modes:
 *   producer  - Push messages to queues
 *   consumer  - Consume messages from queues
 * 
 * Queue Modes:
 *   single-queue  - One queue with N partitions (each thread → partition)
 *   multi-queue   - N queues with one partition each (each thread → queue)
 * 
 * Usage:
 *   ./bin/benchmark producer --threads 4 --count 10000 --batch 100 --partitions 4 --mode single-queue
 *   ./bin/benchmark consumer --threads 4 --batch 100 --partitions 4 --mode single-queue
 */

#include "../client-cpp/queen_client.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>

using namespace queen;
using json = nlohmann::json;

// ============================================================================
// Configuration
// ============================================================================

struct BenchmarkConfig {
    std::string mode;              // "producer" or "consumer"
    int threads = 1;
    std::string server_url = "http://localhost:6632";
    int batch_size = 100;
    int message_count = 10000;     // For producer
    int partitions = 4;
    std::string queue_mode = "single-queue";  // "single-queue" or "multi-queue"
    
    void print() const {
        std::cout << "========================================" << std::endl;
        std::cout << "Queen Benchmark Configuration" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Mode:           " << mode << std::endl;
        std::cout << "Threads:        " << threads << std::endl;
        std::cout << "Server URL:     " << server_url << std::endl;
        std::cout << "Batch Size:     " << batch_size << std::endl;
        if (mode == "producer") {
            std::cout << "Message Count:  " << message_count << std::endl;
        }
        std::cout << "Partitions:     " << partitions << std::endl;
        std::cout << "Queue Mode:     " << queue_mode << std::endl;
        std::cout << "========================================\n" << std::endl;
    }
};

// ============================================================================
// Statistics Tracking
// ============================================================================

struct BenchmarkStats {
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> bytes_processed{0};
    std::atomic<uint64_t> errors{0};
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    
    void start() {
        start_time = std::chrono::steady_clock::now();
    }
    
    void stop() {
        end_time = std::chrono::steady_clock::now();
    }
    
    double elapsed_seconds() const {
        return std::chrono::duration<double>(end_time - start_time).count();
    }
    
    void print_summary() const {
        double elapsed = elapsed_seconds();
        uint64_t total_messages = messages_processed.load();
        uint64_t total_bytes = bytes_processed.load();
        uint64_t total_errors = errors.load();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "Benchmark Results" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Total Messages:     " << total_messages << std::endl;
        std::cout << "Total Bytes:        " << total_bytes << std::endl;
        std::cout << "Errors:             " << total_errors << std::endl;
        std::cout << "Elapsed Time:       " << std::fixed << std::setprecision(2) 
                  << elapsed << " seconds" << std::endl;
        
        if (elapsed > 0) {
            double msg_per_sec = total_messages / elapsed;
            double mb_per_sec = (total_bytes / elapsed) / (1024.0 * 1024.0);
            
            std::cout << "Throughput:         " << std::fixed << std::setprecision(0)
                      << msg_per_sec << " msg/sec" << std::endl;
            std::cout << "Bandwidth:          " << std::fixed << std::setprecision(2)
                      << mb_per_sec << " MB/sec" << std::endl;
        }
        
        std::cout << "========================================" << std::endl;
    }
};

// ============================================================================
// Queue Setup
// ============================================================================

void setup_queues(QueenClient& client, const BenchmarkConfig& config) {
    std::cout << "Setting up queues..." << std::endl;
    
    if (config.queue_mode == "single-queue") {
        // Delete and recreate single queue
        try {
            client.queue("benchmark-queue").del();
            std::cout << "✓ Deleted existing queue: benchmark-queue" << std::endl;
        } catch (...) {
            // Queue might not exist, ignore
        }
        
        client.queue("benchmark-queue").create();
        std::cout << "✓ Created single queue: benchmark-queue" << std::endl;
    } else {
        // Delete and recreate multiple queues
        int deleted = 0;
        for (int i = 0; i < config.partitions; i++) {
            std::string queue_name = "benchmark-queue-" + std::to_string(i);
            try {
                client.queue(queue_name).del();
                deleted++;
            } catch (...) {
                // Queue might not exist, ignore
            }
        }
        if (deleted > 0) {
            std::cout << "✓ Deleted " << deleted << " existing queues" << std::endl;
        }
        
        for (int i = 0; i < config.partitions; i++) {
            std::string queue_name = "benchmark-queue-" + std::to_string(i);
            client.queue(queue_name).create();
        }
        std::cout << "✓ Created " << config.partitions << " queues" << std::endl;
    }
    
    std::cout << std::endl;
}

// ============================================================================
// Producer
// ============================================================================

void producer_thread(int thread_id, const BenchmarkConfig& config, BenchmarkStats& stats) {
    QueenClient client(config.server_url);
    
    int messages_per_thread = config.message_count / config.threads;
    int partition_id = thread_id % config.partitions;
    
    std::cout << "Producer thread " << thread_id << " starting (partition: " 
              << partition_id << ", messages: " << messages_per_thread << ")" << std::endl;
    
    try {
        // Determine queue and partition
        std::string queue_name;
        std::string partition_name;
        
        if (config.queue_mode == "single-queue") {
            queue_name = "benchmark-queue";
            partition_name = "partition-" + std::to_string(partition_id);
        } else {
            queue_name = "benchmark-queue-" + std::to_string(partition_id);
            partition_name = "Default";
        }
        
        // Create batches and push directly (like Node.js version - no client-side buffering)
        int batches_to_send = (messages_per_thread + config.batch_size - 1) / config.batch_size;
        
        for (int batch_num = 0; batch_num < batches_to_send; batch_num++) {
            std::vector<json> batch_messages;
            
            int batch_start = batch_num * config.batch_size;
            int batch_end = std::min(batch_start + config.batch_size, messages_per_thread);
            
            for (int i = batch_start; i < batch_end; i++) {
                json payload = {
                    {"thread_id", thread_id},
                    {"id", i},  // Use 'id' for ordering validation
                    {"partition", partition_id},
                    {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
                    {"data", std::string(100, 'x')}  // 100 bytes of data
                };
                
                batch_messages.push_back({{"data", payload}});
            }
            
            // Push entire batch at once
            auto queue_builder = client.queue(queue_name);
            if (partition_name != "Default") {
                queue_builder.partition(partition_name);
            }
            
            queue_builder.push(batch_messages);
            
            stats.messages_processed += batch_messages.size();
            stats.bytes_processed += batch_messages.size() * 300;  // Approximate
            
            // Progress indicator
            if ((batch_num + 1) % 10 == 0 || batch_num == batches_to_send - 1) {
                std::cout << "Thread " << thread_id << ": Pushed " 
                         << std::min((batch_num + 1) * config.batch_size, messages_per_thread)
                         << "/" << messages_per_thread << " messages" << std::endl;
            }
        }
        
        std::cout << "Producer thread " << thread_id << " completed (" 
                  << messages_per_thread << " messages)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Producer thread " << thread_id << " error: " << e.what() << std::endl;
        stats.errors++;
    }
}

void run_producer(const BenchmarkConfig& config) {
    std::cout << "Starting Producer Benchmark..." << std::endl;
    std::cout << "Target: " << config.message_count << " messages across " 
              << config.threads << " threads\n" << std::endl;
    
    BenchmarkStats stats;
    std::vector<std::thread> threads;
    
    stats.start();
    
    // Start producer threads
    for (int i = 0; i < config.threads; i++) {
        threads.emplace_back(producer_thread, i, std::ref(config), std::ref(stats));
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    stats.stop();
    stats.print_summary();
}

// ============================================================================
// Consumer
// ============================================================================

void consumer_thread(int thread_id, const BenchmarkConfig& config, BenchmarkStats& stats,
                    std::atomic<bool>& stop_signal) {
    QueenClient client(config.server_url);
    
    int partition_id = thread_id % config.partitions;
    
    std::cout << "Consumer thread " << thread_id << " starting (partition: " 
              << partition_id << ")" << std::endl;
    
    try {
        // Determine queue and partition
        std::string queue_name;
        std::string partition_name;
        
        if (config.queue_mode == "single-queue") {
            queue_name = "benchmark-queue";
            partition_name = "partition-" + std::to_string(partition_id);
        } else {
            queue_name = "benchmark-queue-" + std::to_string(partition_id);
            partition_name = "Default";
        }
        
        auto queue_builder = client.queue(queue_name);
        if (partition_name != "Default") {
            queue_builder.partition(partition_name);
        }
        
        queue_builder
            .batch(config.batch_size)
            .wait(true)  // Long polling
            .idle_millis(5000)  // Stop after 5 seconds of no messages
            .auto_ack(true)
            .consume([&](const json& messages) {
                int msg_count = messages.is_array() ? messages.size() : 1;
                stats.messages_processed += msg_count;
                
                // Estimate bytes
                for (const auto& msg : (messages.is_array() ? messages : json::array({messages}))) {
                    if (msg.contains("data")) {
                        stats.bytes_processed += msg.dump().size();
                    }
                }
            }, &stop_signal);
        
        std::cout << "Consumer thread " << thread_id << " completed" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Consumer thread " << thread_id << " error: " << e.what() << std::endl;
        stats.errors++;
    }
}

void run_consumer(const BenchmarkConfig& config) {
    std::cout << "Starting Consumer Benchmark..." << std::endl;
    std::cout << "Running " << config.threads << " concurrent consumers\n" << std::endl;
    
    BenchmarkStats stats;
    std::vector<std::thread> threads;
    std::atomic<bool> stop_signal{false};
    
    stats.start();
    
    // Start consumer threads
    for (int i = 0; i < config.threads; i++) {
        threads.emplace_back(consumer_thread, i, std::ref(config), std::ref(stats), 
                           std::ref(stop_signal));
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    stats.stop();
    stats.print_summary();
}

// ============================================================================
// Argument Parsing
// ============================================================================

void print_usage(const char* program_name) {
    std::cout << "Queen Message Queue - Benchmark Tool" << std::endl;
    std::cout << "\nUsage:" << std::endl;
    std::cout << "  " << program_name << " <mode> [options]" << std::endl;
    std::cout << "\nModes:" << std::endl;
    std::cout << "  producer      - Push messages to queues" << std::endl;
    std::cout << "  consumer      - Consume messages from queues" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  --threads N           Number of concurrent threads (default: 1)" << std::endl;
    std::cout << "  --server URL          Server URL (default: http://localhost:6632)" << std::endl;
    std::cout << "  --batch N             Batch size (default: 100)" << std::endl;
    std::cout << "  --count N             Total messages to produce (default: 10000)" << std::endl;
    std::cout << "  --partitions N        Number of partitions/queues (default: 4)" << std::endl;
    std::cout << "  --mode MODE           Queue mode: single-queue or multi-queue (default: single-queue)" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  # Producer: 4 threads, 100K messages, single queue with 4 partitions" << std::endl;
    std::cout << "  " << program_name << " producer --threads 4 --count 100000 --batch 500 --partitions 4 --mode single-queue" << std::endl;
    std::cout << "\n  # Consumer: 4 threads, batch 100, consume from 4 partitions" << std::endl;
    std::cout << "  " << program_name << " consumer --threads 4 --batch 100 --partitions 4 --mode single-queue" << std::endl;
    std::cout << "\n  # Multi-queue: 8 queues, 8 threads (one thread per queue)" << std::endl;
    std::cout << "  " << program_name << " producer --threads 8 --count 100000 --partitions 8 --mode multi-queue" << std::endl;
    std::cout << "  " << program_name << " consumer --threads 8 --batch 100 --partitions 8 --mode multi-queue" << std::endl;
}

BenchmarkConfig parse_args(int argc, char** argv) {
    BenchmarkConfig config;
    
    if (argc < 2) {
        print_usage(argv[0]);
        exit(1);
    }
    
    config.mode = argv[1];
    
    if (config.mode != "producer" && config.mode != "consumer") {
        std::cerr << "Error: Invalid mode '" << config.mode << "'" << std::endl;
        std::cerr << "Must be 'producer' or 'consumer'" << std::endl;
        print_usage(argv[0]);
        exit(1);
    }
    
    // Parse options
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--threads" && i + 1 < argc) {
            config.threads = std::stoi(argv[++i]);
        } else if (arg == "--server" && i + 1 < argc) {
            config.server_url = argv[++i];
        } else if (arg == "--batch" && i + 1 < argc) {
            config.batch_size = std::stoi(argv[++i]);
        } else if (arg == "--count" && i + 1 < argc) {
            config.message_count = std::stoi(argv[++i]);
        } else if (arg == "--partitions" && i + 1 < argc) {
            config.partitions = std::stoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            config.queue_mode = argv[++i];
            if (config.queue_mode != "single-queue" && config.queue_mode != "multi-queue") {
                std::cerr << "Error: Invalid queue mode '" << config.queue_mode << "'" << std::endl;
                std::cerr << "Must be 'single-queue' or 'multi-queue'" << std::endl;
                exit(1);
            }
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            exit(0);
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            exit(1);
        }
    }
    
    // Validation
    if (config.threads < 1) {
        std::cerr << "Error: threads must be >= 1" << std::endl;
        exit(1);
    }
    if (config.batch_size < 1) {
        std::cerr << "Error: batch size must be >= 1" << std::endl;
        exit(1);
    }
    if (config.partitions < 1) {
        std::cerr << "Error: partitions must be >= 1" << std::endl;
        exit(1);
    }
    if (config.mode == "producer" && config.message_count < 1) {
        std::cerr << "Error: message count must be >= 1" << std::endl;
        exit(1);
    }
    
    return config;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    BenchmarkConfig config = parse_args(argc, argv);
    config.print();
    
    // Setup queues
    QueenClient setup_client(config.server_url);
    setup_queues(setup_client, config);
    
    // Run benchmark
    if (config.mode == "producer") {
        run_producer(config);
    } else {
        run_consumer(config);
    }
    
    return 0;
}

