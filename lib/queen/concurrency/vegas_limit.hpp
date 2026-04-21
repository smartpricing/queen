#ifndef _QUEEN_VEGAS_LIMIT_HPP_
#define _QUEEN_VEGAS_LIMIT_HPP_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>

#include "concurrency_controller.hpp"

namespace queen {

// Adaptive concurrency controller based on TCP Vegas.
//
// Maintains `limit ∈ [min_limit, max_limit]` driven by the "queueing" load
// estimate `queue_load = in_flight * (1 − rtt_min / rtt_recent)`:
//   - queue_load <  alpha → bandwidth under-utilized → grow
//   - queue_load >  beta  → bandwidth over-committed → shrink
//   - otherwise           → hold.
//
// `rtt_min` is a 30 s sliding minimum (so a single fast outlier doesn't pin
// it). `rtt_recent` is an EMA over the last N completions. The integer
// `limit` is adjusted at most once per `update_interval_ms` to avoid
// thrashing. When `limit` decreases below `in_flight`, we do NOT cancel
// existing batches — future `try_acquire` calls simply fail until naturally
// draining.
//
// Event-loop-only: all state is manipulated from the libqueen drain path.
class VegasLimit final : public ConcurrencyController {
  public:
    struct Config {
        uint16_t min_limit              = 1;
        uint16_t max_limit              = 16;
        uint16_t alpha                  = 3;
        uint16_t beta                   = 6;
        uint16_t rtt_window_samples     = 50;   // EMA window (# of completions).
        std::chrono::seconds rtt_min_window{30};// sliding-min window.
        std::chrono::milliseconds update_interval{1000};
    };

    explicit VegasLimit(Config cfg) noexcept
        : _cfg(sanitize(cfg)),
          _limit(_cfg.min_limit),
          _last_update(std::chrono::steady_clock::now()) {}

    bool
    try_acquire() noexcept override {
        uint16_t cur = _in_flight.load(std::memory_order_relaxed);
        while (true) {
            uint16_t lim = _limit.load(std::memory_order_relaxed);
            if (cur >= lim) return false;
            if (_in_flight.compare_exchange_weak(
                    cur, cur + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                return true;
            }
        }
    }

    void
    release() noexcept override {
        _in_flight.fetch_sub(1, std::memory_order_acq_rel);
    }

    void
    on_completion(const CompletionRecord& r) noexcept override {
        if (!r.ok) return;  // PG errors give bogus RTT; ignore.

        auto rtt = r.rtt();
        if (rtt.count() < 0) return;

        auto now = r.complete_time;

        // Maintain a sliding-minimum monotonic deque: the front is always the
        // current windowed minimum, sorted by time ascending.
        uint64_t rtt_ms = static_cast<uint64_t>(rtt.count());
        if (rtt_ms == 0) rtt_ms = 1; // avoid division-by-zero in ratio.

        // Keep the deque monotonic non-decreasing in rtt: drop back entries
        // shadowed by the new (smaller-or-equal) sample.
        while (!_rtt_min_window.empty() && _rtt_min_window.back().rtt >= rtt_ms) {
            _rtt_min_window.pop_back();
        }
        _rtt_min_window.push_back({now, rtt_ms});

        // Evict stale entries from the front.
        while (!_rtt_min_window.empty()
               && (now - _rtt_min_window.front().time) > _cfg.rtt_min_window) {
            _rtt_min_window.pop_front();
        }

        // EMA of recent RTT, equivalent to a window-N exponential average.
        // alpha = 2 / (N + 1), clamped for N==0.
        double alpha_ema =
            2.0 / (static_cast<double>(_cfg.rtt_window_samples) + 1.0);
        if (_rtt_recent_ms == 0.0) _rtt_recent_ms = static_cast<double>(rtt_ms);
        else _rtt_recent_ms = alpha_ema * static_cast<double>(rtt_ms)
                            + (1.0 - alpha_ema) * _rtt_recent_ms;

        // Throttle limit adjustments.
        if ((now - _last_update) < _cfg.update_interval) return;

        double rtt_min = static_cast<double>(_rtt_min_window.front().rtt);
        uint16_t in_flight_snap = _in_flight.load(std::memory_order_relaxed);

        // queue_load = in_flight × (1 − rtt_min / rtt_recent)
        double queue_load = 0.0;
        if (_rtt_recent_ms > 0.0 && rtt_min > 0.0) {
            double ratio = rtt_min / _rtt_recent_ms;
            if (ratio > 1.0) ratio = 1.0;
            queue_load = static_cast<double>(in_flight_snap) * (1.0 - ratio);
        }

        uint16_t lim = _limit.load(std::memory_order_relaxed);
        if (queue_load < static_cast<double>(_cfg.alpha) && lim < _cfg.max_limit) {
            _limit.store(static_cast<uint16_t>(lim + 1), std::memory_order_relaxed);
        } else if (queue_load > static_cast<double>(_cfg.beta) && lim > _cfg.min_limit) {
            _limit.store(static_cast<uint16_t>(lim - 1), std::memory_order_relaxed);
        }
        _last_update = now;
    }

    uint16_t current_limit() const noexcept override {
        return _limit.load(std::memory_order_relaxed);
    }

    uint16_t in_flight() const noexcept override {
        return _in_flight.load(std::memory_order_relaxed);
    }

    // Test-only accessor.
    double   recent_rtt_ms() const noexcept { return _rtt_recent_ms; }
    uint64_t min_rtt_ms() const noexcept {
        return _rtt_min_window.empty() ? 0 : _rtt_min_window.front().rtt;
    }

  private:
    struct Sample {
        std::chrono::steady_clock::time_point time;
        uint64_t                              rtt;
    };

    static Config sanitize(Config c) noexcept {
        if (c.min_limit == 0) c.min_limit = 1;
        if (c.max_limit < c.min_limit) c.max_limit = c.min_limit;
        if (c.alpha == 0) c.alpha = 1;
        if (c.beta  < c.alpha + 1) c.beta = static_cast<uint16_t>(c.alpha + 1);
        if (c.rtt_window_samples == 0) c.rtt_window_samples = 1;
        if (c.rtt_min_window.count() <= 0) c.rtt_min_window = std::chrono::seconds(1);
        // update_interval == 0 means "no throttle"; negative clamped to 0.
        if (c.update_interval.count() < 0) c.update_interval = std::chrono::milliseconds(0);
        return c;
    }

    Config                                   _cfg;
    std::atomic<uint16_t>                    _in_flight{0};
    std::atomic<uint16_t>                    _limit;
    std::deque<Sample>                       _rtt_min_window;
    double                                   _rtt_recent_ms = 0.0;
    std::chrono::steady_clock::time_point    _last_update;
};

} // namespace queen

#endif // _QUEEN_VEGAS_LIMIT_HPP_
