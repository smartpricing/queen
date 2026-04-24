#ifndef _QUEEN_PENDING_JOB_HPP_
#define _QUEEN_PENDING_JOB_HPP_

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace queen {

// Job type enumeration.
//
// Values PUSH..CUSTOM are the actual operation types. `_COUNT` is the size
// sentinel used for per-type array indexing (e.g. `std::array<T, _COUNT>`).
// `_SENTINEL` is used as the "no type" marker for an idle DB slot.
//
// NOTE: the numeric order of PUSH..CUSTOM matters for the drain orchestrator's
// round-robin fairness; additions must be appended before `_COUNT`.
enum class JobType : uint8_t {
    PUSH = 0,        // Push messages (batchable)
    POP,             // Pop messages for a specific partition (batchable in groups)
    ACK,             // Acknowledge (batchable)
    TRANSACTION,     // Atomic transaction (NOT batchable - serial)
    RENEW_LEASE,     // Renew message lease (batchable)
    CUSTOM,          // Custom SQL query (NOT batchable - per-job SQL)
    _COUNT,          // sentinel - array size, not a real type
    _SENTINEL = 0xFF // "no type" sentinel (idle slot)
};

// Compile-time number of real JobType values (excludes sentinels).
constexpr size_t JobTypeCount = static_cast<size_t>(JobType::_COUNT);

// Cast helper: JobType → array index.
constexpr size_t
job_type_index(JobType t) noexcept {
    return static_cast<size_t>(t);
}

// Cast helper: array index → JobType.
constexpr JobType
job_type_from_index(size_t i) noexcept {
    return static_cast<JobType>(i);
}

inline const char*
job_type_name(JobType t) noexcept {
    switch (t) {
        case JobType::PUSH:        return "push";
        case JobType::POP:         return "pop";
        case JobType::ACK:         return "ack";
        case JobType::TRANSACTION: return "transaction";
        case JobType::RENEW_LEASE: return "renew_lease";
        case JobType::CUSTOM:      return "custom";
        default:                   return "?";
    }
}

// SQL dispatch table for batchable types. CUSTOM is dispatched per-job
// (each job carries its own `sql`), so its value is a sentinel.
inline const std::map<JobType, std::string>&
JobTypeToSqlTable() {
    static const std::map<JobType, std::string> table = {
        {JobType::PUSH,        "SELECT queen.push_messages_v3($1::jsonb)"},
        {JobType::POP,         "SELECT queen.pop_unified_batch_v3($1::jsonb)"},
        {JobType::ACK,         "SELECT queen.ack_messages_v2($1::jsonb)"},
        {JobType::TRANSACTION, "SELECT queen.execute_transaction_v2($1::jsonb)"},
        {JobType::RENEW_LEASE, "SELECT queen.renew_lease_v2($1::jsonb)"},
        {JobType::CUSTOM,      "CUSTOM"},
    };
    return table;
}

// Kept for source-compatibility with pre-refactor code that referenced
// the global `JobTypeToSql` map.
// (Defined in the namespace so `queen::JobTypeToSql` still resolves.)
inline const auto& JobTypeToSql = JobTypeToSqlTable();

struct JobRequest {
    JobType     op_type;
    std::string request_id;
    std::string sql;                        // For CUSTOM queries only.
    std::vector<std::string> params;

    std::string queue_name;                 // For notification matching.
    std::string partition_name;             // For grouping.
    std::string consumer_group;             // For grouping.

    size_t item_count = 0;                  // Items in this request (metrics).
    std::chrono::steady_clock::time_point queued_at;

    std::chrono::steady_clock::time_point wait_deadline; // POP long-poll deadline.
    std::chrono::steady_clock::time_point next_check;    // POP earliest-retry time.
    uint16_t backoff_count = 0;

    int  batch_size = 1;
    bool auto_ack   = false;
};

struct PendingJob {
    JobRequest job;
    std::function<void(std::string result)> callback;
};

} // namespace queen

#endif // _QUEEN_PENDING_JOB_HPP_
