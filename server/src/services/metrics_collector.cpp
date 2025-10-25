#include "queen/metrics_collector.hpp"

namespace queen {

MetricsCollector::MetricsCollector(
    std::shared_ptr<DatabasePool> db_pool,
    std::shared_ptr<astp::ThreadPool> db_thread_pool,
    std::shared_ptr<astp::ThreadPool> system_thread_pool,
    const std::string& hostname,
    int port,
    const std::string& worker_id,
    int sample_interval_ms,
    int aggregation_window_s
) : db_pool_(db_pool), 
    db_thread_pool_(db_thread_pool),
    system_thread_pool_(system_thread_pool),
    start_time_(std::chrono::steady_clock::now()),
    hostname_(hostname),
    port_(port),
    worker_id_(worker_id),
    sample_interval_ms_(sample_interval_ms),
    aggregation_window_s_(aggregation_window_s) {
    
    samples_.reserve(aggregation_window_s * 2);
}

MetricsCollector::~MetricsCollector() {
    stop();
}

void MetricsCollector::start() {
    if (running_) {
        spdlog::warn("MetricsCollector already running");
        return;
    }
    
    running_ = true;
    
    // Start the collection loop as a recursive task in system threadpool
    schedule_next_collection();
    
    spdlog::info("MetricsCollector started: hostname={}, port={}, worker={}, "
                 "sampling every {}ms, aggregating every {}s",
                 hostname_, port_, worker_id_,
                 sample_interval_ms_, aggregation_window_s_);
}

void MetricsCollector::stop() {
    if (!running_) return;
    
    running_ = false;
    
    spdlog::info("MetricsCollector stopped");
}

void MetricsCollector::schedule_next_collection() {
    if (!running_) return;
    
    // Schedule the collection task in system threadpool
    system_thread_pool_->push([this]() {
        this->collection_cycle();
    });
}

void MetricsCollector::collection_cycle() {
    auto cycle_start = std::chrono::steady_clock::now();
    
    try {
        // Collect single sample
        MetricsSample sample = collect_sample();
        
        // Store in buffer
        {
            std::lock_guard<std::mutex> lock(samples_mutex_);
            samples_.push_back(sample);
        }
        
        // Check if it's time to aggregate and save
        if (should_aggregate()) {
            aggregate_and_save();
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Metrics collection error: {}", e.what());
    }
    
    // Calculate sleep time
    auto cycle_end = std::chrono::steady_clock::now();
    auto cycle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        cycle_end - cycle_start
    );
    
    auto sleep_time = sample_interval_ms_ - cycle_duration.count();
    if (sleep_time < 0) sleep_time = 0;
    
    // Sleep, then recursively schedule next collection
    if (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        schedule_next_collection();  // Recursive scheduling
    }
}

bool MetricsCollector::should_aggregate() {
    std::lock_guard<std::mutex> lock(samples_mutex_);
    if (samples_.empty()) return false;
    
    auto now = std::chrono::system_clock::now();
    auto first_sample_time = samples_.front().timestamp;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - first_sample_time
    );
    
    return elapsed.count() >= aggregation_window_s_;
}

MetricsSample MetricsCollector::collect_sample() {
    MetricsSample sample{};
    sample.timestamp = std::chrono::system_clock::now();
    
    // Platform-specific CPU and memory
    #ifdef __linux__
    collect_linux_metrics(sample);
    #elif defined(__APPLE__)
    collect_macos_metrics(sample);
    #endif
    
    // Database pool metrics
    sample.db_pool_size = db_pool_->size();
    sample.db_pool_idle = db_pool_->available();
    sample.db_pool_active = sample.db_pool_size - sample.db_pool_idle;
    
    // ThreadPool metrics
    sample.db_threadpool_size = db_thread_pool_->pool_size();
    sample.db_threadpool_queue_size = db_thread_pool_->queue_size();
    sample.system_threadpool_size = system_thread_pool_->pool_size();
    sample.system_threadpool_queue_size = system_thread_pool_->queue_size();
    
    // Uptime
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - start_time_
    );
    sample.uptime_seconds = uptime.count();
    
    return sample;
}

void MetricsCollector::collect_linux_metrics(MetricsSample& sample) {
    #ifdef __linux__
    // CPU from /proc/self/stat
    std::ifstream stat_file("/proc/self/stat");
    if (stat_file.is_open()) {
        std::string line;
        std::getline(stat_file, line);
        
        std::istringstream iss(line);
        std::string field;
        
        // Skip to field 14 (utime) and 15 (stime)
        for (int i = 0; i < 13; i++) iss >> field;
        
        unsigned long utime, stime;
        iss >> utime >> stime;
        
        // Convert clock ticks to microseconds
        long ticks_per_sec = sysconf(_SC_CLK_TCK);
        sample.cpu_user_us = (utime * 1000000ULL) / ticks_per_sec;
        sample.cpu_system_us = (stime * 1000000ULL) / ticks_per_sec;
    }
    
    // Memory from /proc/self/status
    std::ifstream status_file("/proc/self/status");
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.find("VmRSS:") == 0) {
            std::istringstream iss(line);
            std::string label;
            unsigned long value;
            iss >> label >> value;
            sample.memory_rss_bytes = value * 1024ULL; // kB to bytes
        } else if (line.find("VmSize:") == 0) {
            std::istringstream iss(line);
            std::string label;
            unsigned long value;
            iss >> label >> value;
            sample.memory_virtual_bytes = value * 1024ULL;
        }
    }
    #endif
}

void MetricsCollector::collect_macos_metrics(MetricsSample& sample) {
    #ifdef __APPLE__
    // CPU via getrusage
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        sample.cpu_user_us = usage.ru_utime.tv_sec * 1000000ULL + usage.ru_utime.tv_usec;
        sample.cpu_system_us = usage.ru_stime.tv_sec * 1000000ULL + usage.ru_stime.tv_usec;
    }
    
    // Memory via task_info
    struct task_basic_info info;
    mach_msg_type_number_t size = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, 
                 (task_info_t)&info, &size) == KERN_SUCCESS) {
        sample.memory_rss_bytes = info.resident_size;
        sample.memory_virtual_bytes = info.virtual_size;
    }
    #endif
}

void MetricsCollector::aggregate_and_save() {
    std::vector<MetricsSample> samples_to_aggregate;
    
    {
        std::lock_guard<std::mutex> lock(samples_mutex_);
        samples_to_aggregate = std::move(samples_);
        samples_.clear();
        samples_.reserve(aggregation_window_s_ * 2);
    }
    
    if (samples_to_aggregate.empty()) return;
    
    AggregatedMetrics agg = aggregate(samples_to_aggregate);
    
    // Save to database via DB threadpool (not system threadpool)
    db_thread_pool_->push([this, agg]() {
        store_aggregated_metrics(agg);
    });
    
    spdlog::debug("Aggregated {} samples for storage", samples_to_aggregate.size());
}

AggregatedMetrics MetricsCollector::aggregate(const std::vector<MetricsSample>& samples) {
    AggregatedMetrics agg{};
    if (samples.empty()) return agg;
    
    agg.window_start = samples.front().timestamp;
    agg.sample_count = samples.size();
    agg.uptime_seconds = samples.back().uptime_seconds;
    
    // CPU requires special handling - convert cumulative time to percentage
    // CPU% = (delta_cpu_time / delta_wall_time) * 100
    auto aggregate_cpu_metric = [&samples](auto extractor) -> AggregatedMetric {
        AggregatedMetric result{};
        std::vector<double> percentages;
        percentages.reserve(samples.size() - 1);
        
        // Calculate percentage for each interval between consecutive samples
        for (size_t i = 1; i < samples.size(); i++) {
            uint64_t prev_cpu = extractor(samples[i-1]);
            uint64_t curr_cpu = extractor(samples[i]);
            
            // Delta CPU time in seconds
            double delta_cpu_seconds = (curr_cpu - prev_cpu) / 1000000.0;
            
            // Assume 1 second between samples (sample_interval_ms_ / 1000)
            double delta_wall_seconds = 1.0;
            
            // CPU percentage
            double cpu_percent = (delta_cpu_seconds / delta_wall_seconds) * 100.0;
            
            // Clamp to reasonable range (0-100%)
            if (cpu_percent < 0) cpu_percent = 0;
            if (cpu_percent > 100) cpu_percent = 100;
            
            percentages.push_back(cpu_percent);
        }
        
        if (percentages.empty()) {
            result.avg = 0;
            result.min = 0;
            result.max = 0;
            result.last = 0;
            return result;
        }
        
        // Aggregate the percentages
        double sum = 0;
        result.min = UINT64_MAX;
        result.max = 0;
        
        for (double pct : percentages) {
            sum += pct;
            result.min = std::min(result.min, static_cast<uint64_t>(pct * 100)); // Store as 0-10000 (percentage * 100)
            result.max = std::max(result.max, static_cast<uint64_t>(pct * 100));
        }
        
        result.avg = (sum / percentages.size()) * 100; // Store as 0-10000 (percentage * 100)
        result.last = static_cast<uint64_t>(percentages.back() * 100);
        
        return result;
    };
    
    // Regular aggregation for non-CPU metrics
    auto aggregate_metric = [&samples](auto extractor) -> AggregatedMetric {
        AggregatedMetric result{};
        uint64_t sum = 0;
        result.min = UINT64_MAX;
        result.max = 0;
        
        for (const auto& s : samples) {
            uint64_t val = extractor(s);
            sum += val;
            result.min = std::min(result.min, val);
            result.max = std::max(result.max, val);
        }
        
        result.avg = static_cast<double>(sum) / samples.size();
        result.last = extractor(samples.back());
        return result;
    };
    
    // CPU metrics - calculate percentage from deltas
    agg.cpu_user_us = aggregate_cpu_metric([](const auto& s) { return s.cpu_user_us; });
    agg.cpu_system_us = aggregate_cpu_metric([](const auto& s) { return s.cpu_system_us; });
    
    // Other metrics - regular aggregation
    agg.memory_rss_bytes = aggregate_metric([](const auto& s) { return s.memory_rss_bytes; });
    agg.memory_virtual_bytes = aggregate_metric([](const auto& s) { return s.memory_virtual_bytes; });
    agg.db_pool_size = aggregate_metric([](const auto& s) { return s.db_pool_size; });
    agg.db_pool_idle = aggregate_metric([](const auto& s) { return s.db_pool_idle; });
    agg.db_pool_active = aggregate_metric([](const auto& s) { return s.db_pool_active; });
    agg.db_threadpool_size = aggregate_metric([](const auto& s) { return s.db_threadpool_size; });
    agg.db_threadpool_queue_size = aggregate_metric([](const auto& s) { return s.db_threadpool_queue_size; });
    agg.system_threadpool_size = aggregate_metric([](const auto& s) { return s.system_threadpool_size; });
    agg.system_threadpool_queue_size = aggregate_metric([](const auto& s) { return s.system_threadpool_queue_size; });
    
    return agg;
}

void MetricsCollector::store_aggregated_metrics(const AggregatedMetrics& agg) {
    try {
        ScopedConnection conn(db_pool_.get());
        
        // Truncate timestamp to minute
        auto time_t_ts = std::chrono::system_clock::to_time_t(agg.window_start);
        std::tm tm = *std::gmtime(&time_t_ts);
        tm.tm_sec = 0;
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:00+00");
        std::string timestamp_str = ss.str();
        
        nlohmann::json metrics_json = agg.to_json();
        std::string metrics_str = metrics_json.dump();
        
        std::string query = R"(
            INSERT INTO queen.system_metrics (
                timestamp, hostname, port, worker_id,
                sample_count, metrics
            ) VALUES ($1, $2, $3, $4, $5, $6)
            ON CONFLICT (timestamp, hostname, port, worker_id)
            DO UPDATE SET 
                sample_count = EXCLUDED.sample_count,
                metrics = EXCLUDED.metrics
        )";
        
        std::vector<std::string> params = {
            timestamp_str,
            hostname_,
            std::to_string(port_),
            worker_id_,
            std::to_string(agg.sample_count),
            metrics_str
        };
        
        auto result = QueryResult(conn->exec_params(query, params));
        
        if (!result.is_success()) {
            spdlog::error("Failed to store metrics: {}", result.error_message());
        } else {
            spdlog::trace("Stored metrics: {}:{}:{} @ {}", 
                         hostname_, port_, worker_id_, timestamp_str);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Exception storing metrics: {}", e.what());
    }
}

} // namespace queen

