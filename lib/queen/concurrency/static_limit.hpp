#ifndef _QUEEN_STATIC_LIMIT_HPP_
#define _QUEEN_STATIC_LIMIT_HPP_

#include <atomic>
#include <cstdint>

#include "concurrency_controller.hpp"

namespace queen {

// Fixed-limit concurrency controller.
//
// `try_acquire()` CAS-loops against the current `in_flight` counter;
// returns false once the configured `limit` is reached. `on_completion()`
// is a no-op — adaptation is explicitly disabled in this variant.
class StaticLimit final : public ConcurrencyController {
  public:
    explicit StaticLimit(uint16_t limit) noexcept
        : _limit(limit == 0 ? 1 : limit) {}

    bool
    try_acquire() noexcept override {
        uint16_t cur = _in_flight.load(std::memory_order_relaxed);
        while (true) {
            if (cur >= _limit) return false;
            if (_in_flight.compare_exchange_weak(
                    cur, cur + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                return true;
            }
            // cur updated by CAS; retry.
        }
    }

    void
    release() noexcept override {
        _in_flight.fetch_sub(1, std::memory_order_acq_rel);
    }

    void
    on_completion(const CompletionRecord&) noexcept override {
        // no-op: operator requested predictable behavior.
    }

    uint16_t current_limit() const noexcept override { return _limit; }
    uint16_t in_flight()     const noexcept override {
        return _in_flight.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<uint16_t> _in_flight{0};
    uint16_t              _limit;
};

} // namespace queen

#endif // _QUEEN_STATIC_LIMIT_HPP_
