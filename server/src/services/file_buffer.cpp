#include "queen/file_buffer.hpp"
#include "queen/queue_manager.hpp"
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

// Helper to generate UUID (simple version)
static std::string generate_uuid() {
    auto now = std::chrono::system_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    return std::to_string(ns);
}

FileBufferManager::FileBufferManager(
    std::shared_ptr<QueueManager> qm,
    const std::string& buffer_dir,
    int flush_interval_ms,
    size_t max_batch_size,
    bool do_startup_recovery
)  : queue_manager_(qm),
    buffer_dir_(buffer_dir),
    qos0_fd_(-1),
    failover_fd_(-1),
    flush_interval_ms_(flush_interval_ms),
    max_batch_size_(max_batch_size) {
    
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
    
    // Open buffer files
    qos0_file_ = buffer_dir_ + "/qos0.buf";
    failover_file_ = buffer_dir_ + "/failover.buf";
    
    qos0_fd_ = open(qos0_file_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    failover_fd_ = open(failover_file_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    
    if (qos0_fd_ < 0 || failover_fd_ < 0) {
        spdlog::error("Failed to open buffer files: qos0_fd={}, failover_fd={}", qos0_fd_, failover_fd_);
        throw std::runtime_error("Failed to open buffer files");
    }
    
    spdlog::info("Buffer files opened successfully");
    
    // BLOCKING: Startup recovery before accepting requests (only for worker 0)
    if (do_startup_recovery) {
        spdlog::info("Starting recovery of buffered events...");
        startup_recovery();
        spdlog::info("Recovery complete");
    } else {
        spdlog::info("Skipping startup recovery (will be done by Worker 0)");
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
    
    // Close file descriptors
    if (qos0_fd_ >= 0) {
        close(qos0_fd_);
    }
    if (failover_fd_ >= 0) {
        close(failover_fd_);
    }
    
    spdlog::info("FileBufferManager stopped");
}

bool FileBufferManager::write_event(const nlohmann::json& event) {
    std::string event_str = event.dump();
    uint32_t len = static_cast<uint32_t>(event_str.size());
    
    // Determine which file to write to
    bool is_failover = event.value("failover", false);
    
    // CRITICAL: Lock to prevent race with rotation
    // If rotation happens mid-write, fd becomes invalid
    std::lock_guard<std::mutex> lock(rotation_mutex_);
    
    int fd = is_failover ? failover_fd_ : qos0_fd_;
    
    // Write length + data atomically using writev (O_APPEND ensures atomicity)
    struct iovec iov[2];
    iov[0].iov_base = &len;
    iov[0].iov_len = sizeof(len);
    iov[1].iov_base = (void*)event_str.c_str();
    iov[1].iov_len = len;
    
    ssize_t written = writev(fd, iov, 2);
    
    if (written < 0) {
        spdlog::error("Failed to write to buffer file: {} (fd={})", strerror(errno), fd);
        return false;
    }
    
    if (written != static_cast<ssize_t>(sizeof(len) + len)) {
        spdlog::error("Partial write to buffer file: wrote {} of {} bytes", written, sizeof(len) + len);
        return false;
    }
    
    pending_count_++;
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
            if (filename.find("failover_") == 0) {
                files_to_process.push_back(entry.path().string());
            }
        }
        std::sort(files_to_process.begin(), files_to_process.end());
    }
    
    // 2. Processing file
    std::string processing_file = buffer_dir_ + "/failover_processing.buf";
    if (std::filesystem::exists(processing_file)) {
        files_to_process.push_back(processing_file);
    }
    
    // 3. Active file
    if (std::filesystem::exists(failover_file_)) {
        struct stat st;
        if (stat(failover_file_.c_str(), &st) == 0 && st.st_size > 0) {
            files_to_process.push_back(failover_file_);
        }
    }
    
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
    
    // Failed files
    std::string failed_dir = buffer_dir_ + "/failed";
    if (std::filesystem::exists(failed_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(failed_dir)) {
            std::string filename = entry.path().filename().string();
            if (filename.find("qos0_") == 0) {
                files_to_process.push_back(entry.path().string());
            }
        }
    }
    
    // Processing file
    std::string processing_file = buffer_dir_ + "/qos0_processing.buf";
    if (std::filesystem::exists(processing_file)) {
        files_to_process.push_back(processing_file);
    }
    
    // Active file
    if (std::filesystem::exists(qos0_file_)) {
        struct stat st;
        if (stat(qos0_file_.c_str(), &st) == 0 && st.st_size > 0) {
            files_to_process.push_back(qos0_file_);
        }
    }
    
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
    const int RETRY_INTERVAL_CYCLES = 5000 / flush_interval_ms_;  // Try every 5 seconds
    
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(flush_interval_ms_));
        retry_counter++;
        
        try {
            // Always try to process failover events (to detect DB recovery)
            process_failover_events();
            
            // Process QoS 0 events
            process_qos0_events();
            
            // Retry failed files periodically (every ~5 seconds)
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
    // Rotate file
    std::string processing_file = buffer_dir_ + "/failover_processing.buf";
    rotate_file(failover_file_, failover_fd_, processing_file);
    
    if (!std::filesystem::exists(processing_file)) {
        // No processing file - nothing to do
        // (retry_failed_files() is called periodically by background_processor)
        return;
    }
    
    auto events = read_events_from_file(processing_file);
    if (events.empty()) {
        std::filesystem::remove(processing_file);
        return;
    }
    
    spdlog::info("Processing {} failover events from {} (db_healthy={})", 
                 events.size(), processing_file, db_healthy_.load());
    
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
                
                // Log progress every 10k events
                if (total_processed % 10000 < batch_size) {
                    spdlog::info("Progress: {}/{} events ({:.1f}%)", 
                               total_processed, events.size(), 
                               (total_processed * 100.0) / events.size());
                }
            } else {
                db_healthy_ = false;
                spdlog::warn("Failed to flush batch at event {}/{}, DB appears down", total_processed, events.size());
                
                // Move remaining events to failed
                move_to_failed(processing_file, "failover");
                failed_count_ += (events.size() - total_processed);
                return;
            }
        }
    }
    
    if (db_healthy_ && total_processed == events.size()) {
        std::filesystem::remove(processing_file);
        spdlog::info("Successfully processed all {} failover events", total_processed);
    }
}

void FileBufferManager::process_qos0_events() {
    // Rotate file
    std::string processing_file = buffer_dir_ + "/qos0_processing.buf";
    rotate_file(qos0_file_, qos0_fd_, processing_file);
    
    if (!std::filesystem::exists(processing_file)) {
        return;
    }
    
    auto events = read_events_from_file(processing_file);
    if (events.empty()) {
        std::filesystem::remove(processing_file);
        return;
    }
    
    // Process in batches
    size_t total_processed = 0;
    for (size_t i = 0; i < events.size(); i += max_batch_size_) {
        if (!db_healthy_) break;
        
        size_t batch_size = std::min(max_batch_size_, events.size() - i);
        std::vector<nlohmann::json> batch(
            events.begin() + i,
            events.begin() + i + batch_size
        );
        
        if (flush_batched_to_db(batch)) {
            total_processed += batch.size();
            pending_count_ -= batch.size();
        } else {
            db_healthy_ = false;
            break;
        }
    }
    
    if (db_healthy_ && total_processed == events.size()) {
        std::filesystem::remove(processing_file);
        spdlog::debug("Processed {} QoS 0 events in {} batches", 
                     total_processed, (total_processed + max_batch_size_ - 1) / max_batch_size_);
    } else {
        // Move to failed directory
        move_to_failed(processing_file, "qos0");
        failed_count_ += (events.size() - total_processed);
    }
}

void FileBufferManager::rotate_file(const std::string& active_file, int& fd, const std::string& processing_file) {
    std::lock_guard<std::mutex> lock(rotation_mutex_);
    
    // Check if active file has data
    struct stat st;
    if (stat(active_file.c_str(), &st) != 0 || st.st_size == 0) {
        return;
    }
    
    // Close current fd
    close(fd);
    
    // Rename active → processing
    std::filesystem::rename(active_file, processing_file);
    
    // Open new active file
    fd = open(active_file.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    
    if (fd < 0) {
        spdlog::error("Failed to reopen buffer file: {}", active_file);
    }
}

bool FileBufferManager::flush_batched_to_db(const std::vector<nlohmann::json>& events) {
    try {
        // Convert to PushItem format for proper batch insert
        std::vector<PushItem> items;
        items.reserve(events.size());
        
        for (const auto& event : events) {
            PushItem item;
            item.queue = event["queue"];
            item.partition = event.value("partition", "Default");
            item.payload = event["payload"];
            
            if (event.contains("transactionId") && !event["transactionId"].empty()) {
                item.transaction_id = event["transactionId"];
            }
            if (event.contains("traceId") && !event["traceId"].empty()) {
                item.trace_id = event["traceId"];
            }
            
            items.push_back(std::move(item));
        }
        
        // Use queue_manager's push_messages for proper batch insert
        auto results = queue_manager_->push_messages(items);
        
        // Check if all succeeded
        for (const auto& result : results) {
            if (result.status != "queued") {
                throw std::runtime_error("Push failed: " + result.error.value_or("unknown error"));
            }
        }
        
        // DB is healthy again!
        if (!db_healthy_.load()) {
            spdlog::info("PostgreSQL recovered! Database is healthy again");
            db_healthy_ = true;
        }
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to flush batch to DB: {}", e.what());
        if (db_healthy_.load()) {
            spdlog::warn("PostgreSQL appears to be down");
            db_healthy_ = false;
        }
        return false;
    }
}

bool FileBufferManager::flush_single_to_db(const nlohmann::json& event) {
    try {
        queue_manager_->push_single_message(
            event["queue"],
            event.value("partition", "Default"),
            event["payload"],
            event.value("namespace", ""),
            event.value("task", ""),
            event.value("transactionId", ""),
            event.value("traceId", "")
        );
        
        // DB is healthy again!
        if (!db_healthy_.load()) {
            spdlog::info("PostgreSQL recovered! Database is healthy again");
            db_healthy_ = true;
        }
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to flush single event to DB: {}", e.what());
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
    
    // Try to retry one failed file
    for (const auto& entry : std::filesystem::directory_iterator(failed_dir)) {
        if (!entry.is_regular_file()) continue;
        
        std::string filename = entry.path().filename().string();
        std::string processing_file;
        
        if (filename.find("failover_") == 0) {
            processing_file = buffer_dir_ + "/failover_processing.buf";
        } else if (filename.find("qos0_") == 0) {
            processing_file = buffer_dir_ + "/qos0_processing.buf";
        } else {
            continue;
        }
        
        // Only retry if processing file doesn't exist
        if (std::filesystem::exists(processing_file)) {
            continue;
        }
        
        std::string failed_file_path = entry.path().string();
        std::filesystem::rename(entry.path(), processing_file);
        spdlog::info("Retrying failed buffer: {} (will process on next cycle)", failed_file_path);
        break;  // Only retry one at a time
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

} // namespace queen

