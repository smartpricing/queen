#ifndef _QUEEN_METRICS_HPP_
#define _QUEEN_METRICS_HPP_

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

#include "pending_job.hpp"

namespace queen {

// Record produced at batch-fire time. Stored on the DBConnection slot and
// stamped back into the CompletionRecord when PG returns.
struct FireRecord {
    std::chrono::steady_clock::time_point fire_time;
    JobType                               type              = JobType::_SENTINEL;
    uint32_t                              batch_size        = 0;
    uint32_t                              queue_size_at_fire = 0;
    std::chrono::milliseconds             oldest_age_at_fire{0};
    uint16_t                              in_flight_at_fire = 0;
    uint16_t                              concurrency_limit_at_fire = 0;
    uint8_t                               slot_idx          = 0;
};

// Record produced on batch completion. Fed into the per-type concurrency
// controller (`on_completion`) and the per-type metrics ring buffer.
struct CompletionRecord {
    FireRecord                            fired;
    std::chrono::steady_clock::time_point complete_time;
    bool                                  ok                = true;
    int                                   pg_error_code     = 0;

    // Convenience: round-trip time between fire and completion.
    std::chrono::milliseconds rtt() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            complete_time - fired.fire_time);
    }
};

// Fixed-capacity ring buffer. Event-loop-only; no allocations on the hot path.
template <typename T, size_t Capacity>
class RingBuffer {
  public:
    static_assert(Capacity > 0, "RingBuffer capacity must be > 0");

    void
    push(const T& v) noexcept {
        _data[_head] = v;
        _head = (_head + 1) % Capacity;
        if (_size < Capacity) ++_size;
    }

    size_t size() const noexcept { return _size; }
    bool   empty() const noexcept { return _size == 0; }

    // Copy-out snapshot for percentile computation.
    void
    snapshot(std::vector<T>& out) const {
        out.clear();
        out.reserve(_size);
        if (_size < Capacity) {
            for (size_t i = 0; i < _size; ++i) out.push_back(_data[i]);
        } else {
            for (size_t i = 0; i < Capacity; ++i) {
                out.push_back(_data[(_head + i) % Capacity]);
            }
        }
    }

  private:
    std::array<T, Capacity> _data{};
    size_t                  _head = 0;
    size_t                  _size = 0;
};

// Per-type observability counters + bounded ring buffers.
// All operations must happen on the libuv event-loop thread.
struct PerTypeMetrics {
    // Counters (monotonic since worker start).
    uint64_t batches_fired_total       = 0;
    uint64_t batch_items_fired_total   = 0;
    uint64_t completions_ok_total      = 0;
    uint64_t completions_err_total     = 0;

    // Ring buffers of recent fires/completions (bounded 1024).
    static constexpr size_t kRingCapacity = 1024;
    RingBuffer<FireRecord,        kRingCapacity> fires;
    RingBuffer<CompletionRecord,  kRingCapacity> completions;

    void
    record_fire(const FireRecord& r) noexcept {
        ++batches_fired_total;
        batch_items_fired_total += r.batch_size;
        fires.push(r);
    }

    void
    record_completion(const CompletionRecord& r) noexcept {
        if (r.ok) ++completions_ok_total;
        else      ++completions_err_total;
        completions.push(r);
    }

    // Compute a percentile (0..100) from a snapshot of recent completion RTTs.
    // Returns 0 if no samples. `pctl` is clamped to [0, 100].
    uint64_t
    rtt_percentile_ms(double pctl) const {
        std::vector<CompletionRecord> snap;
        completions.snapshot(snap);
        if (snap.empty()) return 0;
        std::vector<uint64_t> v;
        v.reserve(snap.size());
        for (const auto& c : snap) v.push_back(static_cast<uint64_t>(c.rtt().count()));
        std::sort(v.begin(), v.end());
        if (pctl < 0.0) pctl = 0.0;
        if (pctl > 100.0) pctl = 100.0;
        size_t idx = static_cast<size_t>((pctl / 100.0) * (v.size() - 1));
        if (idx >= v.size()) idx = v.size() - 1;
        return v[idx];
    }

    uint64_t
    batch_size_percentile(double pctl) const {
        std::vector<FireRecord> snap;
        fires.snapshot(snap);
        if (snap.empty()) return 0;
        std::vector<uint32_t> v;
        v.reserve(snap.size());
        for (const auto& f : snap) v.push_back(f.batch_size);
        std::sort(v.begin(), v.end());
        if (pctl < 0.0) pctl = 0.0;
        if (pctl > 100.0) pctl = 100.0;
        size_t idx = static_cast<size_t>((pctl / 100.0) * (v.size() - 1));
        if (idx >= v.size()) idx = v.size() - 1;
        return v[idx];
    }
};

} // namespace queen

#endif // _QUEEN_METRICS_HPP_
