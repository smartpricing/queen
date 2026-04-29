// ============================================================================
// /metrics/prometheus  —  Prometheus text exposition for queen
// ============================================================================
//
// Each replica exposes its OWN process state (db pool, threadpools, response
// registries, file buffer, latest MetricsCollector sample, shared_state
// snapshot) plus a single read of queen.worker_metrics_summary for the
// cluster-wide lifetime totals.
//
// Cluster-wide series are emitted with the synthetic label  scope="cluster"
// so PromQL queries that span replicas can use  max(queen_cluster_*)  rather
// than  sum(...)  (every replica returns the same singleton totals).
//
// Per-replica series carry no extra label — Prometheus already labels them
// with  instance  /  job  via its scrape config.
//
// Exposition format reference:
//   https://prometheus.io/docs/instrumenting/exposition_formats/

#include "queen/routes/route_registry.hpp"
#include "queen/routes/route_context.hpp"
#include "queen/routes/route_helpers.hpp"
#include "queen/async_queue_manager.hpp"
#include "queen/file_buffer.hpp"
#include "queen/response_queue.hpp"
#include "queen/metrics_collector.hpp"
#include "queen/shared_state_manager.hpp"
#include "queen.hpp"
#include "threadpool.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include <json.hpp>
#include <spdlog/spdlog.h>

// External globals defined in acceptor_server.cpp / status.cpp.
namespace queen {
extern std::vector<std::shared_ptr<ResponseRegistry>> worker_response_registries;
}  // namespace queen

namespace queen {
namespace routes {

namespace {

// ---------------------------------------------------------------------------
// Exposition-format helpers.
// ---------------------------------------------------------------------------

// Escape label values per the Prometheus text format: backslash, double-quote
// and newline must be escaped. Everything else is passed through.
std::string escape_label(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            default:   out += c;
        }
    }
    return out;
}

// Tiny accumulator that builds the response body incrementally. Each metric
// family is emitted as: HELP line, TYPE line, then one or more sample lines.
class Body {
public:
    void help(const std::string& name, const std::string& text) {
        buf_ << "# HELP " << name << ' ' << text << '\n';
    }
    void type(const std::string& name, const std::string& kind) {
        buf_ << "# TYPE " << name << ' ' << kind << '\n';
    }
    // Emit  name value  with no labels.
    template <typename T>
    void sample(const std::string& name, T value) {
        buf_ << name << ' ' << value << '\n';
    }
    // Emit  name{labels} value  — labels is an already-formatted
    // {k="v",k2="v2"} string (or empty).
    template <typename T>
    void sample(const std::string& name,
                const std::string& labels,
                T value) {
        buf_ << name << labels << ' ' << value << '\n';
    }
    std::string str() const { return buf_.str(); }

private:
    std::ostringstream buf_;
};

// Build a  {k="v",k2="v2"}  label block. Pairs with empty values are skipped.
std::string labels(std::initializer_list<std::pair<const char*, std::string>> pairs) {
    std::string out;
    bool first = true;
    for (const auto& [k, v] : pairs) {
        if (v.empty()) continue;
        if (first) { out += '{'; first = false; }
        else       { out += ','; }
        out += k;
        out += "=\"";
        out += escape_label(v);
        out += '"';
    }
    if (!first) out += '}';
    return out;
}

// ---------------------------------------------------------------------------
// Metric emitters — each one writes a complete metric family.
// ---------------------------------------------------------------------------

// Process / runtime gauges sourced from the latest in-memory MetricsSample.
void write_process_metrics(Body& b, const RouteContext& ctx) {
    MetricsSample s{};
    bool have_sample = false;
    if (ctx.metrics_collector) {
        s = ctx.metrics_collector->get_latest_sample();
        have_sample = s.timestamp.time_since_epoch().count() != 0;
    }

    b.help("queen_uptime_seconds", "Seconds since the queen process started.");
    b.type("queen_uptime_seconds", "gauge");
    b.sample("queen_uptime_seconds", have_sample ? s.uptime_seconds : 0);

    if (!have_sample) return;

    b.help("queen_cpu_user_microseconds_total",
           "Cumulative user CPU time consumed by the process, in microseconds.");
    b.type("queen_cpu_user_microseconds_total", "counter");
    b.sample("queen_cpu_user_microseconds_total", s.cpu_user_us);

    b.help("queen_cpu_system_microseconds_total",
           "Cumulative system CPU time consumed by the process, in microseconds.");
    b.type("queen_cpu_system_microseconds_total", "counter");
    b.sample("queen_cpu_system_microseconds_total", s.cpu_system_us);

    b.help("queen_process_resident_memory_bytes",
           "Resident set size of the queen process, in bytes.");
    b.type("queen_process_resident_memory_bytes", "gauge");
    b.sample("queen_process_resident_memory_bytes", s.memory_rss_bytes);

    b.help("queen_process_virtual_memory_bytes",
           "Virtual memory size of the queen process, in bytes.");
    b.type("queen_process_virtual_memory_bytes", "gauge");
    b.sample("queen_process_virtual_memory_bytes", s.memory_virtual_bytes);
}

// Database connection-pool gauges (read live from AsyncQueueManager).
void write_db_pool_metrics(Body& b, const RouteContext& ctx) {
    auto pool = ctx.async_queue_manager->get_pool_stats();

    b.help("queen_db_pool_size",
           "Total connections in the asynchronous Postgres pool.");
    b.type("queen_db_pool_size", "gauge");
    b.sample("queen_db_pool_size", pool.total);

    b.help("queen_db_pool_idle",
           "Idle connections currently available in the asynchronous Postgres pool.");
    b.type("queen_db_pool_idle", "gauge");
    b.sample("queen_db_pool_idle", pool.available);

    b.help("queen_db_pool_active",
           "Connections currently checked out from the asynchronous Postgres pool.");
    b.type("queen_db_pool_active", "gauge");
    b.sample("queen_db_pool_active", pool.total - pool.available);
}

// Threadpool gauges. Both pools are emitted with a  pool="db|system"  label.
void write_threadpool_metrics(Body& b, const RouteContext& ctx) {
    b.help("queen_threadpool_size",
           "Worker count in each background threadpool.");
    b.type("queen_threadpool_size", "gauge");
    if (ctx.db_thread_pool) {
        b.sample("queen_threadpool_size",
                 labels({{"pool", "db"}}),
                 ctx.db_thread_pool->pool_size());
    }
    if (ctx.system_thread_pool) {
        b.sample("queen_threadpool_size",
                 labels({{"pool", "system"}}),
                 ctx.system_thread_pool->pool_size());
    }

    b.help("queen_threadpool_queue_size",
           "Tasks pending in each background threadpool.");
    b.type("queen_threadpool_queue_size", "gauge");
    if (ctx.db_thread_pool) {
        b.sample("queen_threadpool_queue_size",
                 labels({{"pool", "db"}}),
                 ctx.db_thread_pool->queue_size());
    }
    if (ctx.system_thread_pool) {
        b.sample("queen_threadpool_queue_size",
                 labels({{"pool", "system"}}),
                 ctx.system_thread_pool->queue_size());
    }
}

// Per-worker response-registry gauges.
void write_registry_metrics(Body& b) {
    b.help("queen_response_registry_size",
           "In-flight HTTP responses awaiting completion in each worker.");
    b.type("queen_response_registry_size", "gauge");
    for (size_t i = 0; i < worker_response_registries.size(); ++i) {
        const auto& reg = worker_response_registries[i];
        if (!reg) continue;
        b.sample("queen_response_registry_size",
                 labels({{"worker_id", std::to_string(i)}}),
                 reg->size());
    }
}

// File buffer (Worker 0 only — defensive: ctx.file_buffer may be null on
// other workers, in which case we publish zero so the series is always
// present and Grafana can scrape any replica).
void write_file_buffer_metrics(Body& b, const RouteContext& ctx) {
    uint64_t pending = 0;
    uint64_t failed = 0;
    int      healthy = 1;
    if (ctx.file_buffer) {
        pending = ctx.file_buffer->get_pending_count();
        failed  = ctx.file_buffer->get_failed_count();
        healthy = ctx.file_buffer->is_db_healthy() ? 1 : 0;
    }

    b.help("queen_file_buffer_pending",
           "Push events currently buffered to disk awaiting database flush.");
    b.type("queen_file_buffer_pending", "gauge");
    b.sample("queen_file_buffer_pending", pending);

    b.help("queen_file_buffer_failed",
           "Push events that failed to flush from the file buffer.");
    b.type("queen_file_buffer_failed", "gauge");
    b.sample("queen_file_buffer_failed", failed);

    b.help("queen_file_buffer_db_healthy",
           "1 if the file buffer believes the database is healthy, 0 otherwise.");
    b.type("queen_file_buffer_db_healthy", "gauge");
    b.sample("queen_file_buffer_db_healthy", healthy);
}

// Sidecar / cluster-sync metrics from the latest MetricsSample. These are
// running counters reported as gauges because MetricsCollector resets them
// each aggregation window — Prometheus will compute rates via PromQL on the
// raw "count over the last sample window" value.
void write_sidecar_metrics(Body& b, const RouteContext& ctx) {
    if (!ctx.metrics_collector) return;
    MetricsSample s = ctx.metrics_collector->get_latest_sample();
    if (s.timestamp.time_since_epoch().count() == 0) return;

    b.help("queen_sidecar_op_count",
           "Sidecar operations observed in the latest sample window, by op.");
    b.type("queen_sidecar_op_count", "gauge");
    b.sample("queen_sidecar_op_count", labels({{"op", "push"}}), s.sidecar_push_count);
    b.sample("queen_sidecar_op_count", labels({{"op", "pop"}}),  s.sidecar_pop_count);
    b.sample("queen_sidecar_op_count", labels({{"op", "ack"}}),  s.sidecar_ack_count);

    b.help("queen_sidecar_op_latency_microseconds",
           "Average sidecar operation latency in the latest sample window, by op.");
    b.type("queen_sidecar_op_latency_microseconds", "gauge");
    b.sample("queen_sidecar_op_latency_microseconds", labels({{"op", "push"}}), s.sidecar_push_latency_us);
    b.sample("queen_sidecar_op_latency_microseconds", labels({{"op", "pop"}}),  s.sidecar_pop_latency_us);
    b.sample("queen_sidecar_op_latency_microseconds", labels({{"op", "ack"}}),  s.sidecar_ack_latency_us);

    b.help("queen_sidecar_op_items",
           "Items processed by sidecar operations in the latest sample window, by op.");
    b.type("queen_sidecar_op_items", "gauge");
    b.sample("queen_sidecar_op_items", labels({{"op", "push"}}), s.sidecar_push_items);
    b.sample("queen_sidecar_op_items", labels({{"op", "pop"}}),  s.sidecar_pop_items);
    b.sample("queen_sidecar_op_items", labels({{"op", "ack"}}),  s.sidecar_ack_items);

    b.help("queen_queue_backoff_groups",
           "Total consumer groups currently in pop-backoff across all queues.");
    b.type("queen_queue_backoff_groups", "gauge");
    b.sample("queen_queue_backoff_groups", s.total_backed_off_groups);

    b.help("queen_queue_backoff_queues",
           "Number of queues that have at least one consumer group in backoff.");
    b.type("queen_queue_backoff_queues", "gauge");
    b.sample("queen_queue_backoff_queues", s.queues_with_backoff);

    b.help("queen_queue_backoff_avg_interval_milliseconds",
           "Average backoff interval across all backed-off groups, in milliseconds.");
    b.type("queen_queue_backoff_avg_interval_milliseconds", "gauge");
    b.sample("queen_queue_backoff_avg_interval_milliseconds", s.avg_backoff_interval_ms);

    // Cluster-sync block — only emitted when SharedState is enabled.
    b.help("queen_shared_state_enabled",
           "1 if cross-replica UDP shared-state sync is enabled, 0 otherwise.");
    b.type("queen_shared_state_enabled", "gauge");
    b.sample("queen_shared_state_enabled", s.shared_state_enabled ? 1 : 0);

    if (!s.shared_state_enabled) return;

    b.help("queen_qc_cache_size", "Entries in the queue-config cache.");
    b.type("queen_qc_cache_size", "gauge");
    b.sample("queen_qc_cache_size", s.qc_cache_size);

    b.help("queen_qc_cache_hits_total", "Cumulative queue-config cache hits.");
    b.type("queen_qc_cache_hits_total", "counter");
    b.sample("queen_qc_cache_hits_total", s.qc_cache_hits);

    b.help("queen_qc_cache_misses_total", "Cumulative queue-config cache misses.");
    b.type("queen_qc_cache_misses_total", "counter");
    b.sample("queen_qc_cache_misses_total", s.qc_cache_misses);

    b.help("queen_servers", "Replicas tracked by shared-state, by liveness state.");
    b.type("queen_servers", "gauge");
    b.sample("queen_servers", labels({{"state", "alive"}}), s.servers_alive);
    b.sample("queen_servers", labels({{"state", "dead"}}),  s.servers_dead);

    b.help("queen_transport_messages_total",
           "Cumulative UDP-sync messages by direction.");
    b.type("queen_transport_messages_total", "counter");
    b.sample("queen_transport_messages_total", labels({{"dir", "sent"}}),     s.transport_sent);
    b.sample("queen_transport_messages_total", labels({{"dir", "received"}}), s.transport_received);
    b.sample("queen_transport_messages_total", labels({{"dir", "dropped"}}),  s.transport_dropped);
}

// Cluster-wide lifetime totals from queen.worker_metrics_summary. Returns
// the formatted Prometheus block to append to the body. Returns the empty
// string when the SP result is absent or malformed.
std::string format_cluster_totals(const std::string& sp_result) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(sp_result);
    } catch (const std::exception& e) {
        spdlog::debug("[/metrics/prometheus] cluster totals parse failed: {}", e.what());
        return {};
    }

    // libqueen's CUSTOM op wraps the SP result. The exact wrapping varies
    // across queens: it may be the raw object, or an array of single-row
    // objects whose first key is the SP function name. We probe both.
    auto unwrap = [&]() -> nlohmann::json {
        if (j.is_object() && j.contains("get_system_totals_v1")) {
            return j["get_system_totals_v1"];
        }
        if (j.is_array() && !j.empty()) {
            const auto& first = j[0];
            if (first.is_object() && first.contains("get_system_totals_v1")) {
                return first["get_system_totals_v1"];
            }
            return first;
        }
        return j;
    };

    nlohmann::json totals = unwrap();
    if (!totals.is_object()) return {};

    auto u64 = [&](const char* key) -> uint64_t {
        if (!totals.contains(key) || totals[key].is_null()) return 0;
        if (totals[key].is_number_unsigned()) return totals[key].get<uint64_t>();
        if (totals[key].is_number_integer())  return static_cast<uint64_t>(totals[key].get<int64_t>());
        if (totals[key].is_number_float())    return static_cast<uint64_t>(totals[key].get<double>());
        if (totals[key].is_string()) {
            try { return std::stoull(totals[key].get<std::string>()); } catch (...) { return 0; }
        }
        return 0;
    };

    Body b;
    const std::string scope = labels({{"scope", "cluster"}});

    auto counter = [&](const char* name, const char* help, uint64_t v) {
        b.help(name, help);
        b.type(name, "counter");
        b.sample(name, scope, v);
    };
    auto gauge = [&](const char* name, const char* help, uint64_t v) {
        b.help(name, help);
        b.type(name, "gauge");
        b.sample(name, scope, v);
    };

    counter("queen_cluster_push_requests_total",
            "Cumulative push HTTP requests handled across the cluster.",
            u64("pushRequests"));
    counter("queen_cluster_pop_requests_total",
            "Cumulative pop HTTP requests handled across the cluster.",
            u64("popRequests"));
    counter("queen_cluster_ack_requests_total",
            "Cumulative ack HTTP requests handled across the cluster.",
            u64("ackRequests"));
    counter("queen_cluster_transactions_total",
            "Cumulative transaction calls handled across the cluster.",
            u64("transactions"));

    counter("queen_cluster_push_messages_total",
            "Cumulative messages pushed across the cluster.",
            u64("pushMessages"));
    counter("queen_cluster_pop_messages_total",
            "Cumulative messages popped across the cluster.",
            u64("popMessages"));
    counter("queen_cluster_ack_messages_total",
            "Cumulative ack attempts across the cluster.",
            u64("ackMessages"));

    b.help("queen_cluster_ack_total",
           "Cumulative acks across the cluster, by result.");
    b.type("queen_cluster_ack_total", "counter");
    b.sample("queen_cluster_ack_total",
             labels({{"scope", "cluster"}, {"result", "success"}}),
             u64("ackSuccess"));
    b.sample("queen_cluster_ack_total",
             labels({{"scope", "cluster"}, {"result", "failed"}}),
             u64("ackFailed"));

    counter("queen_cluster_db_errors_total",
            "Cumulative database errors observed across the cluster.",
            u64("dbErrors"));
    counter("queen_cluster_dlq_total",
            "Cumulative messages routed to the dead-letter queue across the cluster.",
            u64("dlqCount"));

    gauge("queen_cluster_pending_messages",
          "Pending = push_messages_total - pop_messages_total (cluster wide).",
          u64("pendingMessages"));

    return b.str();
}

// Send the assembled body as Prometheus text. Must run on the worker loop
// thread (uWS responses are not thread-safe).
void send_prometheus(uWS::HttpResponse<false>* res, const std::string& body) {
    res->writeStatus("200 OK");
    res->writeHeader("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
    res->writeHeader("Cache-Control", "no-cache");
    res->end(body);
}

}  // namespace

// ---------------------------------------------------------------------------
// Route registration.
// ---------------------------------------------------------------------------
void setup_prometheus_routes(uWS::App* app, const RouteContext& ctx) {
    app->get("/metrics/prometheus", [ctx](auto* res, auto* req) {
        (void)req;

        // Build the in-process portion synchronously — none of these reads
        // touch the database.
        Body body;
        try {
            write_process_metrics(body, ctx);
            write_db_pool_metrics(body, ctx);
            write_threadpool_metrics(body, ctx);
            write_registry_metrics(body);
            write_file_buffer_metrics(body, ctx);
            write_sidecar_metrics(body, ctx);
        } catch (const std::exception& e) {
            spdlog::warn("[/metrics/prometheus] live block failed: {}", e.what());
        }

        const std::string live = body.str();

        // If the per-worker libqueen instance is not ready (early startup
        // window, or DB unavailable in failover mode) just return live
        // metrics — Prometheus will still scrape successfully.
        if (!ctx.queen) {
            send_prometheus(res, live);
            return;
        }

        // Track abort so the deferred callback knows whether it can write.
        // Both onAborted and worker_loop->defer run on the same uWS loop
        // thread, so a plain bool would suffice; using atomic for clarity.
        auto aborted = std::make_shared<std::atomic<bool>>(false);
        res->onAborted([aborted]() { aborted->store(true, std::memory_order_relaxed); });

        auto* worker_loop = ctx.worker_loop;

        queen::JobRequest job_req;
        job_req.op_type    = queen::JobType::CUSTOM;
        job_req.request_id = "prom_" + queen::generate_uuidv7();
        job_req.sql        = "SELECT queen.get_system_totals_v1()";

        ctx.queen->submit(std::move(job_req),
            [res, worker_loop, aborted, live = std::move(live)](std::string result) {
                worker_loop->defer(
                    [res, aborted, live = std::move(live), result = std::move(result)]() mutable {
                        if (aborted->load(std::memory_order_relaxed)) return;

                        std::string cluster = format_cluster_totals(result);
                        if (cluster.empty()) {
                            send_prometheus(res, live);
                        } else {
                            live.append(cluster);
                            send_prometheus(res, live);
                        }
                    });
            });
    });
}

}  // namespace routes
}  // namespace queen
