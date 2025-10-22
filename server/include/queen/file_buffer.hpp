#pragma once

#include <string>
#include <atomic>
#include <memory>
#include <vector>
#include <mutex>
#include <map>
#include <filesystem>
#include <json.hpp>

namespace queen {

class QueueManager;

/**
 * FileBufferManager - Dual purpose file-based buffer:
 * 1. QoS 0 batching - buffer events for performance
 * 2. PostgreSQL failover - buffer when DB is down
 * 
 * Uses append-only files with O_APPEND for thread-safe writes.
 * Background thread processes files and flushes to database.
 */
class FileBufferManager {
public:
    FileBufferManager(
        std::shared_ptr<QueueManager> queue_manager,
        const std::string& buffer_dir = "/var/lib/queen/buffers",
        int flush_interval_ms = 100,
        size_t max_batch_size = 100,
        bool do_startup_recovery = true  // Only worker 0 should do recovery
    );
    
    ~FileBufferManager();
    
    // Check if startup recovery is complete
    bool is_ready() const { return ready_.load(); }
    
    /**
     * Write event to buffer (fast, durable)
     * Returns false only if disk is full or I/O error
     * 
     * Thread-safe: Multiple threads can call simultaneously
     */
    bool write_event(const nlohmann::json& event);
    
    /**
     * Mark database as unhealthy (called when push fails)
     * This allows fast failover - subsequent pushes skip DB attempt
     */
    void mark_db_unhealthy() { db_healthy_.store(false); }
    
    // Stats
    size_t get_pending_count() const { return pending_count_.load(); }
    size_t get_failed_count() const { return failed_count_.load(); }
    bool is_db_healthy() const { return db_healthy_.load(); }

private:
    // Startup recovery (blocking)
    void startup_recovery();
    size_t recover_failover_files();
    size_t recover_qos0_files();
    
    // Background processing (continuous)
    void background_processor();
    void process_qos0_events();
    void process_failover_events();
    
    // File operations
    void rotate_file(const std::string& active_file, int& fd, const std::string& processing_file);
    bool flush_batched_to_db(const std::vector<nlohmann::json>& events);
    bool flush_single_to_db(const nlohmann::json& event);
    void move_to_failed(const std::string& file, const std::string& type);
    void retry_failed_files();
    
    // Helper to read events from file
    std::vector<nlohmann::json> read_events_from_file(const std::string& file_path, size_t max_count = 0);
    
    std::shared_ptr<QueueManager> queue_manager_;
    std::string buffer_dir_;
    
    // Separate files for QoS 0 vs failover
    std::string qos0_file_;
    std::string failover_file_;
    int qos0_fd_;
    int failover_fd_;
    
    // Thread control
    std::atomic<bool> running_{true};
    std::atomic<bool> ready_{false};
    std::atomic<bool> processor_running_{false};
    
    // Configuration
    int flush_interval_ms_;
    size_t max_batch_size_;
    
    // Stats
    std::atomic<size_t> pending_count_{0};
    std::atomic<size_t> failed_count_{0};
    std::atomic<bool> db_healthy_{true};
    
    // Mutex for file rotation (rare operation)
    std::mutex rotation_mutex_;
};

} // namespace queen

