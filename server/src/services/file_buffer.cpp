#include "queen/file_buffer.hpp"
#include "queen/async_queue_manager.hpp"
#include <spdlog/spdlog.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <thread>

namespace queen {

FileBufferManager::FileBufferManager(
    std::shared_ptr<AsyncQueueManager> qm,
    const std::string& buffer_dir,
    int flush_interval_ms,
    size_t max_batch_size,
    size_t max_events_per_file,
    bool do_startup_recovery
)  : queue_manager_(qm),
    buffer_dir_(buffer_dir),
    current_qos0_fd_(-1),
    current_failover_fd_(-1),
    flush_interval_ms_(flush_interval_ms),
    max_batch_size_(max_batch_size),
    max_events_per_file_(max_events_per_file) {
    
    spdlog::info("FileBufferManager initializing: dir={}, recovery={}", buffer_dir_, do_startup_recovery);
    
    // Create directories with proper error handling
    try {
        if (!std::filesystem::exists(buffer_dir_)) {
            spdlog::info("Creating buffer directory: {}", buffer_dir_);
            std::filesystem::create_directories(buffer_dir_);
            
            // Set permissions (755) - owner: rwx, group: rx, others: rx
            std::filesystem::permissions(
                buffer_dir_,
                std::filesystem::perms::owner_all | 
                std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::replace
            );
        }
        
        std::string failed_dir = buffer_dir_ + "/failed";
        if (!std::filesystem::exists(failed_dir)) {
            spdlog::info("Creating failed directory: {}", failed_dir);
            std::filesystem::create_directories(failed_dir);
            
            std::filesystem::permissions(
                failed_dir,
                std::filesystem::perms::owner_all | 
                std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::replace
            );
        }
        
        spdlog::info("Buffer directories ready: {}", buffer_dir_);
        
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("Failed to create buffer directories: {} (error code: {})", 
                     e.what(), e.code().value());
        spdlog::error("Make sure the parent directory is writable or run with appropriate permissions");
        throw;
    } catch (const std::exception& e) {
        spdlog::error("Failed to create buffer directories: {}", e.what());
        throw;
    }
    
    // BLOCKING: Startup recovery before accepting requests (only for worker 0)
    if (do_startup_recovery) {
        spdlog::info("Starting recovery of buffered events...");
        cleanup_incomplete_tmp_files();  // Clean up any .tmp files from crash
        startup_recovery();
        spdlog::info("Recovery complete");
    } else {
        spdlog::info("Skipping startup recovery (will be done by Worker 0)");
    }
    
    // Create initial buffer files
    try {
        create_new_buffer_file("qos0");
        create_new_buffer_file("failover");
        spdlog::info("Buffer files initialized successfully");
    } catch (const std::exception& e) {
        spdlog::error("Failed to create initial buffer files: {}", e.what());
        throw;
    }
    
    // Start background processor thread
    processor_running_ = true;
    std::thread processor([this]() { background_processor(); });
    processor.detach();
    
    ready_ = true;
    spdlog::info("FileBufferManager ready");
}

FileBufferManager::~FileBufferManager() {
    spdlog::info("FileBufferManager shutting down...");
    running_ = false;
    
    // Wait for processor to stop
    while (processor_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Close file descriptors and finalize current files
    if (current_qos0_fd_ >= 0) {
        close(current_qos0_fd_);
        if (!current_qos0_file_.empty()) {
            finalize_buffer_file(current_qos0_file_);
        }
    }
    if (current_failover_fd_ >= 0) {
        close(current_failover_fd_);
        if (!current_failover_file_.empty()) {
            finalize_buffer_file(current_failover_file_);
        }
    }
    
    spdlog::info("FileBufferManager stopped");
}

bool FileBufferManager::write_event(const nlohmann::json& event) {
    // Determine which file type to write to
    bool is_failover = event.value("failover", false);
    
    // Select appropriate mutex and file tracking variables
    std::mutex& file_mutex = is_failover ? failover_file_mutex_ : qos0_file_mutex_;
    std::lock_guard<std::mutex> lock(file_mutex);
    
    int& current_fd = is_failover ? current_failover_fd_ : current_qos0_fd_;
    std::string& current_file = is_failover ? current_failover_file_ : current_qos0_file_;
    std::atomic<size_t>& current_count = is_failover ? current_failover_count_ : current_qos0_count_;
    
    // Check if we need to rotate to a new file
    if (current_count >= max_events_per_file_) {
        spdlog::debug("Rotating {} buffer file (reached {} events)", 
                     is_failover ? "failover" : "qos0", current_count.load());
        
        // Close and finalize current file
        if (current_fd >= 0) {
            close(current_fd);
            finalize_buffer_file(current_file);
        }
        
        // Create new buffer file
        create_new_buffer_file(is_failover ? "failover" : "qos0");
        current_count = 0;
    }
    
    // Ensure we have an open file
    if (current_fd < 0) {
        spdlog::error("No valid file descriptor for {} buffer", is_failover ? "failover" : "qos0");
        return false;
    }
    
    // Serialize event
    std::string event_str = event.dump();
    uint32_t len = static_cast<uint32_t>(event_str.size());
    
    // Write length + data atomically using writev (O_APPEND ensures atomicity)
    struct iovec iov[2];
    iov[0].iov_base = &len;
    iov[0].iov_len = sizeof(len);
    iov[1].iov_base = (void*)event_str.c_str();
    iov[1].iov_len = len;
    
    ssize_t written = writev(current_fd, iov, 2);
    
    if (written < 0) {
        spdlog::error("Failed to write to buffer file: {} (fd={})", strerror(errno), current_fd);
        return false;
    }
    
    if (written != static_cast<ssize_t>(sizeof(len) + len)) {
        spdlog::error("Partial write to buffer file: wrote {} of {} bytes", written, sizeof(len) + len);
        return false;
    }
    
    current_count++;
    pending_count_++;
    
    // Record last write time (milliseconds since epoch)
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    if (is_failover) {
        last_failover_write_time_ = now_ms;
    } else {
        last_qos0_write_time_ = now_ms;
    }
    
    return true;
}

void FileBufferManager::startup_recovery() {
    auto start_time = std::chrono::steady_clock::now();
    size_t failover_recovered = 0;
    size_t qos0_recovered = 0;
    
    // Limit startup recovery to prevent blocking forever
    const int MAX_STARTUP_RECOVERY_SECONDS = 3600;
    
    spdlog::info("Starting recovery (max {}s to avoid blocking server startup)...", 
                 MAX_STARTUP_RECOVERY_SECONDS);
    
    // Phase 1: Recover failover files FIRST (preserve FIFO order)
    spdlog::info("Phase 1: Recovering failover events (FIFO order)...");
    
    auto phase1_start = std::chrono::steady_clock::now();
    failover_recovered = recover_failover_files();
    auto phase1_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - phase1_start
    ).count();
    
    if (phase1_elapsed >= MAX_STARTUP_RECOVERY_SECONDS) {
        spdlog::warn("Phase 1 took {}s (limit: {}s), skipping Phase 2. Background processor will continue recovery.", 
                    phase1_elapsed, MAX_STARTUP_RECOVERY_SECONDS);
    } else {
        // Phase 2: Recover QoS 0 files (batched)
        spdlog::info("Phase 2: Recovering QoS 0 events (batched)...");
        qos0_recovered = recover_qos0_files();
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time
    ).count();
    
    spdlog::info("Startup recovery completed in {}ms: failover={}, qos0={}", 
                 elapsed, failover_recovered, qos0_recovered);
    
    if (failover_recovered > 0 || qos0_recovered > 0) {
        spdlog::info("Background processor will continue processing any remaining buffered events");
    }
}

size_t FileBufferManager::recover_failover_files() {
    size_t total_recovered = 0;
    
    // Collect files to process in order
    std::vector<std::string> files_to_process;
    
    // 1. Failed files (oldest first)
    std::string failed_dir = buffer_dir_ + "/failed";
    if (std::filesystem::exists(failed_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(failed_dir)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("failover_") == 0 && filename.find(".buf") != std::string::npos) {
                files_to_process.push_back(entry.path().string());
            }
        }
    }
    
    // 2. All complete .buf files in main directory (not .tmp)
    for (const auto& entry : std::filesystem::directory_iterator(buffer_dir_)) {
        std::string filename = entry.path().filename().string();
        if (filename.find("failover_") == 0 && 
            filename.find(".buf") != std::string::npos &&
            filename.find(".buf.tmp") == std::string::npos) {
            files_to_process.push_back(entry.path().string());
        }
    }
    
    // Sort by filename (UUIDv7 is time-sortable)
    std::sort(files_to_process.begin(), files_to_process.end());
    
    // Process each file ONE BY ONE (preserve FIFO)
    for (const auto& file_path : files_to_process) {
        size_t file_size = std::filesystem::file_size(file_path);
        spdlog::info("Recovering failover file: {} ({} bytes)", file_path, file_size);
        
        auto events = read_events_from_file(file_path);
        
        if (events.empty()) {
            spdlog::warn("No events read from file (possibly corrupted), removing: {}", file_path);
            std::filesystem::remove(file_path);
            continue;
        }
        
        spdlog::info("Processing {} events from failover file (will preserve FIFO order)...", events.size());
        
        // Process in batches of 100 for efficiency (still preserves order within batches)
        const size_t RECOVERY_BATCH_SIZE = 100;
        size_t file_recovered = 0;
        
        for (size_t i = 0; i < events.size(); i += RECOVERY_BATCH_SIZE) {
            if (!db_healthy_) {
                spdlog::warn("DB became unhealthy during recovery at event {}/{}, stopping", 
                           file_recovered, events.size());
                break;
            }
            
            size_t batch_size = std::min(RECOVERY_BATCH_SIZE, events.size() - i);
            std::vector<nlohmann::json> batch(events.begin() + i, events.begin() + i + batch_size);
            
            if (flush_batched_to_db(batch)) {
                file_recovered += batch_size;
                total_recovered += batch_size;
                // Don't decrement pending_count during recovery - these events predate this server instance
                
                // Log progress every 10000 events
                if (file_recovered % 10000 == 0 || file_recovered == events.size()) {
                    spdlog::info("Recovery progress: {}/{} events processed ({:.1f}%)", 
                               file_recovered, events.size(), 
                               (file_recovered * 100.0 / events.size()));
                }
            } else {
                db_healthy_ = false;
                spdlog::warn("Failed to flush batch during recovery at event {}/{}", file_recovered + 1, events.size());
                break;
            }
        }
        
        if (db_healthy_ && file_recovered == events.size()) {
            std::filesystem::remove(file_path);
            spdlog::info("Successfully recovered all {} events from {}", file_recovered, file_path);
        } else {
            spdlog::error("DB unavailable during recovery, keeping file: {} ({}/{} events recovered)", 
                        file_path, file_recovered, events.size());
            break;
        }
    }
    
    return total_recovered;
}

size_t FileBufferManager::recover_qos0_files() {
    size_t total_recovered = 0;
    
    std::vector<std::string> files_to_process;
    
    // 1. Failed files
    std::string failed_dir = buffer_dir_ + "/failed";
    if (std::filesystem::exists(failed_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(failed_dir)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("qos0_") == 0 && filename.find(".buf") != std::string::npos) {
                files_to_process.push_back(entry.path().string());
            }
        }
    }
    
    // 2. All complete .buf files in main directory (not .tmp)
    for (const auto& entry : std::filesystem::directory_iterator(buffer_dir_)) {
        std::string filename = entry.path().filename().string();
        if (filename.find("qos0_") == 0 && 
            filename.find(".buf") != std::string::npos &&
            filename.find(".buf.tmp") == std::string::npos) {
            files_to_process.push_back(entry.path().string());
        }
    }
    
    // Sort by filename (UUIDv7 is time-sortable)
    std::sort(files_to_process.begin(), files_to_process.end());
    
    // Process each file (batched)
    for (const auto& file_path : files_to_process) {
        spdlog::info("Recovering QoS 0 file: {}", file_path);
        
        auto events = read_events_from_file(file_path);
        
        // Process in batches
        for (size_t i = 0; i < events.size(); i += max_batch_size_) {
            if (!db_healthy_) break;
            
            size_t batch_size = std::min(max_batch_size_, events.size() - i);
            std::vector<nlohmann::json> batch(
                events.begin() + i,
                events.begin() + i + batch_size
            );
            
            if (flush_batched_to_db(batch)) {
                total_recovered += batch.size();
                // Don't decrement pending_count during recovery
            } else {
                db_healthy_ = false;
                break;
            }
        }
        
        if (db_healthy_) {
            std::filesystem::remove(file_path);
            spdlog::info("Recovered {} events from {}", events.size(), file_path);
        } else {
            spdlog::error("DB unavailable, keeping file: {}", file_path);
            break;
        }
    }
    
    return total_recovered;
}

void FileBufferManager::background_processor() {
    spdlog::info("Background processor thread started");
    
    int retry_counter = 0;
    const int RETRY_INTERVAL_CYCLES = 1000 / flush_interval_ms_;  // Try every 1 second (not 5)
    
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(flush_interval_ms_));
        retry_counter++;
        
        try {
            // Check if current .tmp files should be finalized
            // Finalize if: (1) reached max events, OR (2) has events and is older than 2x flush interval
            
            // Get current time
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            
            // QoS 0 file - check for finalization
            if (current_qos0_fd_ >= 0 && !current_qos0_file_.empty()) {
                bool should_finalize = false;
                size_t count = current_qos0_count_.load();
                
                // Reason 1: Reached max events
                if (count >= max_events_per_file_) {
                    should_finalize = true;
                }
                // Reason 2: Has events and no writes for 2x flush interval (200ms default)
                else if (count > 0) {
                    uint64_t last_write = last_qos0_write_time_.load();
                    uint64_t time_since_write = now_ms - last_write;
                    
                    if (time_since_write > static_cast<uint64_t>(flush_interval_ms_ * 2)) {
                        should_finalize = true;
                        spdlog::debug("QoS 0: Time-based finalization ({} events, {}ms since last write)", 
                                     count, time_since_write);
                    }
                }
                
                if (should_finalize) {
                    std::lock_guard<std::mutex> lock(qos0_file_mutex_);
                    if (current_qos0_fd_ >= 0) {  // Double check
                        spdlog::debug("QoS 0: Finalizing buffer file ({} events)", current_qos0_count_.load());
                        close(current_qos0_fd_);
                        finalize_buffer_file(current_qos0_file_);
                        create_new_buffer_file("qos0");
                        current_qos0_count_ = 0;
                    }
                }
            }
            
            // Failover file - check for finalization
            if (current_failover_fd_ >= 0 && !current_failover_file_.empty()) {
                bool should_finalize = false;
                size_t count = current_failover_count_.load();
                
                if (count >= max_events_per_file_) {
                    should_finalize = true;
                }
                else if (count > 0) {
                    uint64_t last_write = last_failover_write_time_.load();
                    uint64_t time_since_write = now_ms - last_write;
                    
                    if (time_since_write > static_cast<uint64_t>(flush_interval_ms_ * 2)) {
                        should_finalize = true;
                        spdlog::debug("Failover: Time-based finalization ({} events, {}ms since last write)", 
                                     count, time_since_write);
                    }
                }
                
                if (should_finalize) {
                    std::lock_guard<std::mutex> lock(failover_file_mutex_);
                    if (current_failover_fd_ >= 0) {  // Double check
                        spdlog::debug("Failover: Finalizing buffer file ({} events)", current_failover_count_.load());
                        close(current_failover_fd_);
                        finalize_buffer_file(current_failover_file_);
                        create_new_buffer_file("failover");
                        current_failover_count_ = 0;
                    }
                }
            }
            
            // Process failover events - if DB is healthy, process aggressively
            // Process multiple files per cycle to drain faster (10 files = ~100 events in 100ms)
            int max_files_per_cycle = db_healthy_ ? 10 : 1;
            for (int i = 0; i < max_files_per_cycle; i++) {
                process_failover_events();
                // Stop if no more files to process
                if (!has_failover_files()) break;
            }
            
            // Process QoS 0 events
            process_qos0_events();
            
            // Retry failed files periodically (every ~1 second)
            if (retry_counter >= RETRY_INTERVAL_CYCLES) {
                retry_counter = 0;
                
                if (!db_healthy_) {
                    spdlog::info("DB marked unhealthy, attempting retry to check for recovery...");
                }
                
                retry_failed_files();
            }
            
        } catch (const std::exception& e) {
            spdlog::error("Background processor error: {}", e.what());
        }
    }
    
    processor_running_ = false;
    spdlog::info("Background processor thread stopped");
}

void FileBufferManager::process_failover_events() {
    // Find all complete .buf files (not .tmp)
    std::vector<std::string> files_to_process;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(buffer_dir_)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("failover_") == 0 && 
                filename.find(".buf") != std::string::npos &&
                filename.find(".buf.tmp") == std::string::npos) {
                files_to_process.push_back(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Error scanning for failover files: {}", e.what());
        return;
    }
    
    if (files_to_process.empty()) {
        return;  // No files to process
    }
    
    // Sort by filename (UUIDv7 ensures time ordering)
    std::sort(files_to_process.begin(), files_to_process.end());
    
    // If there are many files, log it
    if (files_to_process.size() > 10) {
        spdlog::info("Failover: Found {} buffer files to process, processing oldest first", 
                    files_to_process.size());
    }
    
    // Process oldest file first (FIFO order)
    std::string file_path = files_to_process[0];
    
    auto events = read_events_from_file(file_path);
    if (events.empty()) {
        spdlog::warn("Empty failover file, removing: {}", file_path);
        std::filesystem::remove(file_path);
        return;
    }
    
    size_t num_batches = (events.size() + max_batch_size_ - 1) / max_batch_size_;
    spdlog::info("Failover: Processing {} events from {} in {} batches (db_healthy={}, duplicate key errors are normal during recovery)", 
                 events.size(), std::filesystem::path(file_path).filename().string(), 
                 num_batches, db_healthy_.load());
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Group by queue+partition for batching
    // FIFO is preserved WITHIN each partition (which is what matters)
    std::map<std::pair<std::string, std::string>, std::vector<nlohmann::json>> grouped;
    
    for (const auto& event : events) {
        std::string queue = event["queue"];
        std::string partition = event.value("partition", "Default");
        grouped[{queue, partition}].push_back(event);
    }
    
    size_t total_processed = 0;
    
    // Process each partition's events in batches
    for (const auto& [key, partition_events] : grouped) {
        // Process in batches of max_batch_size_
        for (size_t i = 0; i < partition_events.size(); i += max_batch_size_) {
            size_t batch_size = std::min(max_batch_size_, partition_events.size() - i);
            std::vector<nlohmann::json> batch(
                partition_events.begin() + i,
                partition_events.begin() + i + batch_size
            );
            
            if (flush_batched_to_db(batch)) {
                total_processed += batch_size;
                pending_count_ -= batch_size;  // Decrement pending counter
                
                // If this is the first success after being down, log recovery
                if (!db_healthy_) {
                    spdlog::info("PostgreSQL recovery detected! Batch flush succeeded");
                }
                db_healthy_ = true;
                
                // THROTTLE: Small delay every 1000 events to avoid monopolizing pool
                // This ensures live push requests can get connections
                if (total_processed % 1000 == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                
                // Log progress every 1000 events
                if (total_processed % 1000 == 0 || total_processed == events.size()) {
                    spdlog::info("Failover: Progress {}/{} events ({:.1f}%)", 
                               total_processed, events.size(), 
                               (total_processed * 100.0) / events.size());
                }
            } else {
                db_healthy_ = false;
                spdlog::warn("Failed to flush batch at event {}/{}, DB appears down", total_processed, events.size());
                
                // Move remaining events to failed
                move_to_failed(file_path, "failover");
                failed_count_ += (events.size() - total_processed);
                return;
            }
        }
    }
    
    if (db_healthy_ && total_processed == events.size()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time
        ).count();
        
        std::filesystem::remove(file_path);
        spdlog::info("Failover: Completed {} events in {}ms ({:.0f} events/sec) - file removed", 
                     total_processed, duration,
                     duration > 0 ? (total_processed * 1000.0 / duration) : 0.0);
    } else {
        // Move to failed directory
        move_to_failed(file_path, "failover");
        failed_count_ += (events.size() - total_processed);
    }
}

void FileBufferManager::process_qos0_events() {
    // Find all complete .buf files (not .tmp)
    std::vector<std::string> files_to_process;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(buffer_dir_)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("qos0_") == 0 && 
                filename.find(".buf") != std::string::npos &&
                filename.find(".buf.tmp") == std::string::npos) {
                files_to_process.push_back(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Error scanning for qos0 files: {}", e.what());
        return;
    }
    
    if (files_to_process.empty()) {
        return;  // No files to process
    }
    
    // Sort by filename (UUIDv7 ensures time ordering)
    std::sort(files_to_process.begin(), files_to_process.end());
    
    // Process oldest file first
    std::string file_path = files_to_process[0];
    
    auto events = read_events_from_file(file_path);
    if (events.empty()) {
        spdlog::warn("Empty QoS 0 file, removing: {}", file_path);
        std::filesystem::remove(file_path);
        return;
    }
    
    size_t num_batches = (events.size() + max_batch_size_ - 1) / max_batch_size_;
    spdlog::info("QoS 0: Processing {} events from {} in {} batches", 
                 events.size(), std::filesystem::path(file_path).filename().string(), num_batches);
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Process in batches
    size_t total_processed = 0;
    for (size_t i = 0; i < events.size(); i += max_batch_size_) {
        // Always try first batch (to detect DB recovery), skip others if DB is known to be down
        if (!db_healthy_ && i > 0) {
            spdlog::warn("QoS 0: DB marked unhealthy, stopping batch processing at {}/{}", 
                        total_processed, events.size());
            break;
        }
        
        size_t batch_size = std::min(max_batch_size_, events.size() - i);
        std::vector<nlohmann::json> batch(
            events.begin() + i,
            events.begin() + i + batch_size
        );
        
        size_t batch_num = (i / max_batch_size_) + 1;
        auto batch_start = std::chrono::steady_clock::now();
        
        if (!db_healthy_ && i == 0) {
            spdlog::info("QoS 0: DB marked unhealthy, attempting first batch to check for recovery...");
        }
        
        spdlog::debug("QoS 0: Attempting to flush batch {}/{} ({} events)...", 
                     batch_num, num_batches, batch_size);
        
        if (flush_batched_to_db(batch)) {
            total_processed += batch.size();
            pending_count_ -= batch.size();
            
            auto batch_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - batch_start
            ).count();
            
            spdlog::debug("QoS 0: Flushed batch {}/{} ({} events, {}ms)", 
                         batch_num, num_batches, batch_size, batch_duration);
        } else {
            spdlog::warn("QoS 0: Batch {}/{} flush failed, DB still unhealthy", batch_num, num_batches);
            db_healthy_ = false;
            break;
        }
    }
    
    if (db_healthy_ && total_processed == events.size()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time
        ).count();
        
        std::filesystem::remove(file_path);
        spdlog::info("QoS 0: Completed {} events in {}ms ({} batches, {:.0f} events/sec) - file removed", 
                     total_processed, duration, num_batches,
                     duration > 0 ? (total_processed * 1000.0 / duration) : 0.0);
    } else {
        // Move to failed directory
        move_to_failed(file_path, "qos0");
        failed_count_ += (events.size() - total_processed);
    }
}


bool FileBufferManager::flush_batched_to_db(const std::vector<nlohmann::json>& events) {
    try {
        // Convert to PushItem format for proper batch insert
        std::vector<PushItem> items;
        items.reserve(events.size());
        
        for (const auto& event : events) {
            // CRITICAL: Transaction ID must be preserved from buffer file
            // Never generate new IDs - that causes duplicates on retry!
            if (!event.contains("transactionId") || event["transactionId"].is_null() || 
                !event["transactionId"].is_string() || event["transactionId"].get<std::string>().empty()) {
                spdlog::error("Buffered event missing transactionId, skipping (corrupted buffer file?)");
                continue;  // Skip this event
            }
            
            PushItem item;
            item.queue = event["queue"];
            item.partition = event.value("partition", "Default");
            item.payload = event["payload"];
            item.transaction_id = event["transactionId"].get<std::string>();
            
            if (event.contains("traceId") && !event["traceId"].is_null() && 
                event["traceId"].is_string() && !event["traceId"].get<std::string>().empty()) {
                item.trace_id = event["traceId"].get<std::string>();
            }
            
            items.push_back(std::move(item));
        }
        
        // Use queue_manager's push_messages for proper batch insert
        auto results = queue_manager_->push_messages(items);
        
        // Check if all succeeded (or are duplicates, which is OK)
        for (size_t i = 0; i < results.size(); i++) {
            const auto& result = results[i];
            if (result.status != "queued" && result.status != "duplicate") {
                spdlog::error("FileBuffer drain failed: item[{}] status='{}', error='{}', tx_id='{}'", 
                             i, result.status, result.error.value_or("none"), result.transaction_id);
                throw std::runtime_error("Push failed: " + result.error.value_or("unknown error"));
            }
            if (result.status == "duplicate") {
                spdlog::debug("FileBuffer drain: item[{}] already exists in DB (duplicate), skipping", i);
            }
        }
        
        // DB is healthy again!
        if (!db_healthy_.load()) {
            spdlog::info("PostgreSQL recovered! Database is healthy again");
            db_healthy_ = true;
        }
        return true;
        
    } catch (const std::exception& e) {
        std::string error_msg = e.what();
        
        // Check if queue doesn't exist - but is DB down or was queue deleted?
        if (error_msg.find("does not exist") != std::string::npos) {
            // Do a health check to determine if DB is down or queue was genuinely deleted
            bool db_is_healthy = queue_manager_->health_check();
            
            if (!db_is_healthy) {
                // DB is still down - treat as DB failure, don't skip messages
                spdlog::error("Recovery: Got 'does not exist' but DB health check failed - DB still down");
                if (db_healthy_.load()) {
                    spdlog::warn("PostgreSQL appears to be down");
                    db_healthy_ = false;
                }
                return false;
            } else {
                // DB is healthy - queue was genuinely deleted
                spdlog::warn("Recovery: Queue no longer exists (deleted), skipping {} stale messages", events.size());
                
                if (!db_healthy_.load()) {
                    spdlog::info("PostgreSQL recovered! Database is healthy again");
                    db_healthy_ = true;
                }
                
                // Return true to indicate file can be deleted (stale data)
                return true;
            }
        }
        
        // Check if this is a duplicate key error (messages already in DB)
        if (error_msg.find("duplicate key value violates unique constraint") != std::string::npos ||
            error_msg.find("messages_transaction_id_key") != std::string::npos) {
            
            spdlog::info("Recovery: Duplicate keys detected (messages already in DB), retrying individually to skip duplicates...");
            
            // Retry each message individually to skip duplicates and non-existent queues
            size_t succeeded = 0;
            size_t duplicates = 0;
            size_t deleted_queues = 0;
            
            for (const auto& event : events) {
                // CRITICAL: Must preserve transaction ID from buffer file
                if (!event.contains("transactionId") || event["transactionId"].is_null() || 
                    !event["transactionId"].is_string() || event["transactionId"].get<std::string>().empty()) {
                    spdlog::error("Buffered event missing transactionId in individual retry, skipping");
                    continue;
                }
                
                try {
                    queue_manager_->push_single_message(
                        event["queue"].get<std::string>(),
                        event.value("partition", "Default"),
                        event["payload"],
                        event.value("namespace", ""),
                        event.value("task", ""),
                        event["transactionId"].get<std::string>(),  // Use actual transaction ID!
                        event.contains("traceId") && event["traceId"].is_string() ? event["traceId"].get<std::string>() : ""
                    );
                    succeeded++;
                } catch (const std::exception& single_error) {
                    std::string single_msg = single_error.what();
                    if (single_msg.find("duplicate key") != std::string::npos) {
                        // This message was already inserted - skip it
                        duplicates++;
                    } else if (single_msg.find("does not exist") != std::string::npos) {
                        // Check if DB is down or queue was genuinely deleted
                        bool db_is_healthy = queue_manager_->health_check();
                        if (!db_is_healthy) {
                            // DB is down - rethrow to fail the batch
                            throw;
                        } else {
                            // Queue was deleted - skip this message
                            deleted_queues++;
                        }
                    } else {
                        // Real error - rethrow
                        throw;
                    }
                }
            }
            
            spdlog::info("Recovery complete: {} new, {} duplicates, {} deleted queues", 
                        succeeded, duplicates, deleted_queues);
            
            // If we successfully processed or skipped all messages, consider it success
            // DB is actually healthy if we got duplicate key errors
            if (!db_healthy_.load()) {
                spdlog::info("PostgreSQL recovered! Database is healthy again");
                db_healthy_ = true;
            }
            return true;
        }
        
        // Not a duplicate key or deleted queue error - treat as DB failure
        spdlog::error("Failed to flush batch to DB: {}", error_msg);
        if (db_healthy_.load()) {
            spdlog::warn("PostgreSQL appears to be down");
            db_healthy_ = false;
        }
        return false;
    }
}

bool FileBufferManager::flush_single_to_db(const nlohmann::json& event) {
    // CRITICAL: Must preserve transaction ID from buffer file
    if (!event.contains("transactionId") || event["transactionId"].is_null() || 
        !event["transactionId"].is_string() || event["transactionId"].get<std::string>().empty()) {
        spdlog::error("Buffered event missing transactionId in flush_single_to_db, skipping");
        return false;
    }
    
    try {
        queue_manager_->push_single_message(
            event["queue"].get<std::string>(),
            event.value("partition", "Default"),
            event["payload"],
            event.value("namespace", ""),
            event.value("task", ""),
            event["transactionId"].get<std::string>(),  // Use actual transaction ID!
            event.contains("traceId") && event["traceId"].is_string() ? event["traceId"].get<std::string>() : ""
        );
        
        // DB is healthy again!
        if (!db_healthy_.load()) {
            spdlog::info("PostgreSQL recovered! Database is healthy again");
            db_healthy_ = true;
        }
        return true;
        
    } catch (const std::exception& e) {
        std::string error_msg = e.what();
        
        // Check if this is a duplicate key error (message already in DB)
        if (error_msg.find("duplicate key value violates unique constraint") != std::string::npos ||
            error_msg.find("messages_transaction_id_key") != std::string::npos) {
            
            // Message already exists - this is actually success!
            spdlog::debug("Message already exists in DB (duplicate), skipping: {}", 
                         event.value("transactionId", "unknown"));
            
            // DB is healthy if we got a duplicate key error
            if (!db_healthy_.load()) {
                spdlog::info("PostgreSQL recovered! Database is healthy again");
                db_healthy_ = true;
            }
            return true;
        }
        
        // Not a duplicate key error - treat as DB failure
        spdlog::error("Failed to flush single event to DB: {}", error_msg);
        if (db_healthy_.load()) {
            spdlog::warn("PostgreSQL appears to be down");
            db_healthy_ = false;
        }
        return false;
    }
}

void FileBufferManager::move_to_failed(const std::string& file, const std::string& type) {
    // Use UUIDv7 for guaranteed unique filenames (time-based + monotonic)
    std::string unique_id = queue_manager_->generate_uuid();
    std::string failed_file = buffer_dir_ + "/failed/" + type + "_" + unique_id + ".buf";
    
    try {
        std::filesystem::rename(file, failed_file);
        spdlog::debug("Moved buffer to failed directory: {}", failed_file);
    } catch (const std::exception& e) {
        spdlog::error("Failed to move file to failed directory: {}", e.what());
    }
}

void FileBufferManager::retry_failed_files() {
    std::string failed_dir = buffer_dir_ + "/failed";
    
    if (!std::filesystem::exists(failed_dir)) {
        return;
    }
    
    // If DB is marked unhealthy, we'll just try to process a failed file
    // If it succeeds, it will mark db_healthy_ = true
    // No separate health check needed - the actual retry IS the health check
    
    // Collect all failed files
    std::vector<std::string> failed_files;
    for (const auto& entry : std::filesystem::directory_iterator(failed_dir)) {
        if (entry.is_regular_file()) {
            failed_files.push_back(entry.path().string());
        }
    }
    
    if (failed_files.empty()) {
        return;
    }
    
    // Sort by filename (oldest first)
    std::sort(failed_files.begin(), failed_files.end());
    
    // If DB is healthy, move ALL failed files back for fast recovery
    // If DB is unhealthy, move just one to test recovery
    int files_to_move = db_healthy_ ? failed_files.size() : 1;
    
    if (files_to_move > 1) {
        spdlog::info("DB healthy - moving ALL {} failed buffer files back for processing", files_to_move);
    }
    
    for (int i = 0; i < files_to_move && i < static_cast<int>(failed_files.size()); i++) {
        std::string failed_file = failed_files[i];
        std::string filename = std::filesystem::path(failed_file).filename().string();
        std::string retry_file = buffer_dir_ + "/" + filename;
        
        try {
            std::filesystem::rename(failed_file, retry_file);
            if (files_to_move == 1) {
                spdlog::info("Retrying failed buffer: {} (moved back to main directory)", filename);
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to move file for retry: {}", e.what());
        }
    }
}

std::vector<nlohmann::json> FileBufferManager::read_events_from_file(const std::string& file_path, size_t max_count) {
    std::vector<nlohmann::json> events;
    std::ifstream file(file_path, std::ios::binary);
    
    if (!file) {
        spdlog::error("Failed to open file for reading: {}", file_path);
        return events;
    }
    
    while (file && (max_count == 0 || events.size() < max_count)) {
        uint32_t len;
        file.read(reinterpret_cast<char*>(&len), sizeof(len));
        
        if (!file || len == 0 || len > 10 * 1024 * 1024) {
            break;  // End of file or invalid length
        }
        
        std::string event_str(len, '\0');
        file.read(&event_str[0], len);
        
        if (!file) {
            break;
        }
        
        try {
            events.push_back(nlohmann::json::parse(event_str));
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse event from file {}: {}", file_path, e.what());
        }
    }
    
    file.close();
    return events;
}

void FileBufferManager::create_new_buffer_file(const std::string& type) {
    // Generate UUID for filename
    std::string uuid = queue_manager_->generate_uuid();
    std::string filename = buffer_dir_ + "/" + type + "_" + uuid + ".buf.tmp";
    
    // Open new file
    int fd = open(filename.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    
    if (fd < 0) {
        spdlog::error("Failed to create new {} buffer file: {} ({})", type, filename, strerror(errno));
        throw std::runtime_error("Failed to create buffer file");
    }
    
    // Update tracking variables based on type
    if (type == "qos0") {
        current_qos0_file_ = filename;
        current_qos0_fd_ = fd;
        current_qos0_count_ = 0;
    } else if (type == "failover") {
        current_failover_file_ = filename;
        current_failover_fd_ = fd;
        current_failover_count_ = 0;
    }
    
    spdlog::debug("Created new {} buffer file: {}", type, filename);
}

void FileBufferManager::finalize_buffer_file(const std::string& tmp_file) {
    if (tmp_file.empty() || tmp_file.find(".tmp") == std::string::npos) {
        return;  // Not a .tmp file
    }
    
    // Remove .tmp extension
    std::string final_file = tmp_file.substr(0, tmp_file.length() - 4);
    
    try {
        // Atomic rename
        std::filesystem::rename(tmp_file, final_file);
        spdlog::debug("Finalized buffer file: {} → {}", tmp_file, final_file);
    } catch (const std::exception& e) {
        spdlog::error("Failed to finalize buffer file {}: {}", tmp_file, e.what());
    }
}

bool FileBufferManager::has_failover_files() const {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(buffer_dir_)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("failover_") == 0 && 
                filename.find(".buf") != std::string::npos &&
                filename.find(".buf.tmp") == std::string::npos) {
                return true;
            }
        }
    } catch (const std::exception& e) {
        return false;
    }
    return false;
}

FileBufferManager::FailedFilesStats FileBufferManager::get_failed_files_stats() const {
    FailedFilesStats stats = {0, 0, 0, 0};
    
    std::string failed_dir = buffer_dir_ + "/failed";
    
    if (!std::filesystem::exists(failed_dir)) {
        return stats;
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(failed_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                size_t file_size = std::filesystem::file_size(entry.path());
                
                stats.file_count++;
                stats.total_bytes += file_size;
                
                if (filename.find("failover_") == 0) {
                    stats.failover_count++;
                } else if (filename.find("qos0_") == 0) {
                    stats.qos0_count++;
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to get failed files stats: {}", e.what());
    }
    
    return stats;
}

void FileBufferManager::cleanup_incomplete_tmp_files() {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(buffer_dir_)) {
            std::string filename = entry.path().filename().string();
            
            // Check if it's a .tmp file
            if (filename.find(".buf.tmp") != std::string::npos) {
                size_t file_size = std::filesystem::file_size(entry.path());
                
                // If file is small (< 1KB), it's incomplete - delete it
                if (file_size < 1024) {
                    spdlog::info("Deleting incomplete buffer file: {} (only {} bytes)", 
                               filename, file_size);
                    std::filesystem::remove(entry.path());
                } else {
                    // File has substantial data - finalize it for recovery
                    spdlog::info("Finalizing incomplete buffer file from crash: {} ({} bytes)", 
                               filename, file_size);
                    finalize_buffer_file(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Error during .tmp file cleanup: {}", e.what());
    }
}

} // namespace queen

