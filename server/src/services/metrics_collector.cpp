#include "queen/metrics_collector.hpp"
#include "queen/stream_poll_intention_registry.hpp"
#include "queen/response_queue.hpp"
#include "queen/shared_state_manager.hpp"

namespace queen {

MetricsCollector::MetricsCollector(
    std::shared_ptr<AsyncDbPool> db_pool,
    std::shared_ptr<astp::ThreadPool> db_thread_pool,
    std::shared_ptr<astp::ThreadPool> system_thread_pool,
    std::shared_ptr<StreamPollIntentionRegistry> stream_poll_intention_registry,
    std::shared_ptr<ResponseRegistry> response_registry,
    std::shared_ptr<SharedStateManager> shared_state_manager,
    const std::string& hostname,
    int port,
    const std::string& worker_id,
    int sample_interval_ms,
    int aggregation_window_s
) : db_pool_(db_pool), 
    db_thread_pool_(db_thread_pool),
    system_thread_pool_(system_thread_pool),
    stream_poll_intention_registry_(stream_poll_intention_registry),
    response_registry_(response_registry),
    shared_state_manager_(shared_state_manager),
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
    
    // Registry metrics
    sample.stream_poll_intention_registry_size = stream_poll_intention_registry_->size();
    sample.response_registry_size = response_registry_->size();
    
    // Uptime
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - start_time_
    );
    sample.uptime_seconds = uptime.count();
    
    // SharedState metrics - always collect sidecar ops and backoff (local metrics)
    // Cluster-specific metrics only when enabled
    if (shared_state_manager_) {
        try {
            auto stats = shared_state_manager_->get_stats();
            
            // Sidecar aggregated operations (always collected - local ops)
            if (stats.contains("sidecar_ops")) {
                auto& ops = stats["sidecar_ops"];
                if (ops.contains("push")) {
                    sample.sidecar_push_count = ops["push"].value("count", 0ULL);
                    sample.sidecar_push_latency_us = ops["push"].value("avg_latency_us", 0ULL);
                    sample.sidecar_push_items = ops["push"].value("items", 0ULL);
                }
                if (ops.contains("pop")) {
                    sample.sidecar_pop_count = ops["pop"].value("count", 0ULL);
                    sample.sidecar_pop_latency_us = ops["pop"].value("avg_latency_us", 0ULL);
                    sample.sidecar_pop_items = ops["pop"].value("items", 0ULL);
                }
                if (ops.contains("ack")) {
                    sample.sidecar_ack_count = ops["ack"].value("count", 0ULL);
                    sample.sidecar_ack_latency_us = ops["ack"].value("avg_latency_us", 0ULL);
                    sample.sidecar_ack_items = ops["ack"].value("items", 0ULL);
                }
            }
            
            // Queue backoff summary (always collected - local ops)
            if (stats.contains("queue_backoff_summary") && stats["queue_backoff_summary"].is_array()) {
                auto& summary = stats["queue_backoff_summary"];
                int64_t total_interval = 0;
                int total_groups = 0;
                
                // Store the raw per-queue summary for later
                sample.queue_backoff_summary = summary;
                
                for (const auto& queue_stats : summary) {
                    int backed_off = queue_stats.value("groups_backed_off", 0);
                    if (backed_off > 0) {
                        sample.queues_with_backoff++;
                    }
                    sample.total_backed_off_groups += backed_off;
                    total_interval += queue_stats.value("avg_interval_ms", 0LL) * 
                                     queue_stats.value("groups_tracked", 0);
                    total_groups += queue_stats.value("groups_tracked", 0);
                }
                
                sample.avg_backoff_interval_ms = total_groups > 0 
                    ? total_interval / total_groups : 0;
            }
            
            // Cluster-specific metrics (only when cluster sync is enabled)
            if (shared_state_manager_->is_enabled()) {
                sample.shared_state_enabled = true;
                
                // Queue config cache
                if (stats.contains("queue_config_cache")) {
                    auto& qc = stats["queue_config_cache"];
                    sample.qc_cache_size = qc.value("size", 0);
                    sample.qc_cache_hits = qc.value("hits", 0ULL);
                    sample.qc_cache_misses = qc.value("misses", 0ULL);
                }
                
                // Consumer presence
                if (stats.contains("consumer_presence")) {
                    auto& cp = stats["consumer_presence"];
                    sample.consumer_queues_tracked = cp.value("queues_tracked", 0);
                    sample.consumer_servers_tracked = cp.value("servers_tracked", 0);
                    sample.consumer_total_registrations = cp.value("total_registrations", 0);
                }
                
                // Server health
                if (stats.contains("server_health")) {
                    auto& sh = stats["server_health"];
                    sample.servers_alive = sh.value("servers_alive", 0);
                    sample.servers_dead = sh.value("servers_dead", 0);
                }
                
                // Transport
                if (stats.contains("transport")) {
                    auto& tr = stats["transport"];
                    sample.transport_sent = tr.value("messages_sent", 0ULL);
                    sample.transport_received = tr.value("messages_received", 0ULL);
                    sample.transport_dropped = tr.value("messages_dropped", 0ULL);
                }
            }
            
        } catch (const std::exception& e) {
            spdlog::warn("Failed to collect SharedState metrics: {}", e.what());
        }
    }
    
    return sample;
}

void MetricsCollector::collect_linux_metrics([[maybe_unused]] MetricsSample& sample) {
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
    agg.stream_poll_intention_registry_size = aggregate_metric([](const auto& s) { return s.stream_poll_intention_registry_size; });
    agg.response_registry_size = aggregate_metric([](const auto& s) { return s.response_registry_size; });
    
    // Sidecar aggregated operations (always tracked - local ops)
    agg.sidecar_push_count = aggregate_metric([](const auto& s) { return s.sidecar_push_count; });
    agg.sidecar_push_latency_us = aggregate_metric([](const auto& s) { return s.sidecar_push_latency_us; });
    agg.sidecar_push_items = aggregate_metric([](const auto& s) { return s.sidecar_push_items; });
    agg.sidecar_pop_count = aggregate_metric([](const auto& s) { return s.sidecar_pop_count; });
    agg.sidecar_pop_latency_us = aggregate_metric([](const auto& s) { return s.sidecar_pop_latency_us; });
    agg.sidecar_pop_items = aggregate_metric([](const auto& s) { return s.sidecar_pop_items; });
    agg.sidecar_ack_count = aggregate_metric([](const auto& s) { return s.sidecar_ack_count; });
    agg.sidecar_ack_latency_us = aggregate_metric([](const auto& s) { return s.sidecar_ack_latency_us; });
    agg.sidecar_ack_items = aggregate_metric([](const auto& s) { return s.sidecar_ack_items; });
    
    // Queue backoff summary (always tracked - local ops)
    agg.queues_with_backoff = aggregate_metric([](const auto& s) { return s.queues_with_backoff; });
    agg.total_backed_off_groups = aggregate_metric([](const auto& s) { return s.total_backed_off_groups; });
    agg.avg_backoff_interval_ms = aggregate_metric([](const auto& s) { return s.avg_backoff_interval_ms; });
    
    // Per-queue backoff summary - use the last sample's data
    if (!samples.empty()) {
        agg.queue_backoff_summary = samples.back().queue_backoff_summary;
    }
    
    // SharedState / Cluster metrics
    // Check if any sample has shared_state enabled
    agg.shared_state_enabled = std::any_of(samples.begin(), samples.end(), 
        [](const MetricsSample& s) { return s.shared_state_enabled; });
    
    if (agg.shared_state_enabled) {
        agg.qc_cache_size = aggregate_metric([](const auto& s) { return s.qc_cache_size; });
        agg.qc_cache_hits = aggregate_metric([](const auto& s) { return s.qc_cache_hits; });
        agg.qc_cache_misses = aggregate_metric([](const auto& s) { return s.qc_cache_misses; });
        
        agg.consumer_queues_tracked = aggregate_metric([](const auto& s) { return s.consumer_queues_tracked; });
        agg.consumer_servers_tracked = aggregate_metric([](const auto& s) { return s.consumer_servers_tracked; });
        agg.consumer_total_registrations = aggregate_metric([](const auto& s) { return s.consumer_total_registrations; });
        
        agg.servers_alive = aggregate_metric([](const auto& s) { return s.servers_alive; });
        agg.servers_dead = aggregate_metric([](const auto& s) { return s.servers_dead; });
        
        agg.transport_sent = aggregate_metric([](const auto& s) { return s.transport_sent; });
        agg.transport_received = aggregate_metric([](const auto& s) { return s.transport_received; });
        agg.transport_dropped = aggregate_metric([](const auto& s) { return s.transport_dropped; });
    }
    
    return agg;
}

void MetricsCollector::store_aggregated_metrics(const AggregatedMetrics& agg) {
    try {
        auto conn = db_pool_->acquire();
        
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
        
        sendQueryParamsAsync(conn.get(), query, params);
        getCommandResult(conn.get());
        
            spdlog::trace("Stored metrics: {}:{}:{} @ {}", 
                         hostname_, port_, worker_id_, timestamp_str);
        
    } catch (const std::exception& e) {
        spdlog::error("Exception storing metrics: {}", e.what());
    }
}

} // namespace queen

