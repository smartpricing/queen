#ifndef _QUEEN_CONCURRENCY_CONTROLLER_HPP_
#define _QUEEN_CONCURRENCY_CONTROLLER_HPP_

#include <cstdint>

#include "../metrics.hpp"

namespace queen {

// Layer 3 — per-type concurrency controller (abstract base).
//
// Gates how many in-flight batches of a given type may be dispatched to
// PostgreSQL at once. Knows NOTHING about queue contents or batch sizes.
// Answers exactly one question per type: "can we start another batch?"
//
// Invariants:
//   - Every successful `try_acquire()` must be paired with exactly one
//     `release()` and one `on_completion()`. The libqueen design calls both
//     exactly once via the single `_on_slot_freed` path.
//   - All methods must be safe to call from the libuv event-loop thread.
//     (Concrete implementations in this tree are event-loop-only; no
//     cross-thread use is required by the design.)
class ConcurrencyController {
  public:
    virtual ~ConcurrencyController() = default;

    // Try to acquire one in-flight slot. Returns true if granted.
    virtual bool     try_acquire() noexcept = 0;

    // Release an in-flight slot previously acquired via try_acquire().
    virtual void     release() noexcept = 0;

    // Feed a completion record for adaptive controllers (Vegas, etc.).
    // Static controllers may ignore this.
    virtual void     on_completion(const CompletionRecord& r) noexcept = 0;

    // Current effective limit on in-flight batches.
    virtual uint16_t current_limit() const noexcept = 0;

    // Current in-flight count.
    virtual uint16_t in_flight() const noexcept = 0;
};

} // namespace queen

#endif // _QUEEN_CONCURRENCY_CONTROLLER_HPP_
