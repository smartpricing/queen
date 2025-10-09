### Query Analysis and Improvement Plan

This document outlines the planned changes to improve SQL safety, correctness, FIFO guarantees, duplicate-avoidance, and performance.

## Goals
- Enforce per-partition FIFO even when no partition is specified.
- Prevent duplicate message selection across workers.
- Harden transactions against injection and long blocks.
- Reduce planner anti-patterns; improve query efficiency.
- Align retention/eviction with schema v2 semantics.

## Changes and Why

1) Security and transaction hardening
- What: Whitelist transaction isolation levels in `withTransaction` (no dynamic interpolation).
- Why: Avoids SQL injection risk and invalid isolation values.

- What: Set `statement_timeout` and `lock_timeout` via `SET LOCAL` at transaction start.
- Why: Prevents long blocking operations and stuck workers under contention.

- What: Add retry for serialization failures (SQLSTATE 40001) and deadlocks (40P01) in the query wrapper.
- Why: These are expected under high contention; bounded retry improves resiliency.

2) Strict FIFO when partition is unspecified
- What: Partition-first selection in queue-mode and namespace/task pop paths; lock the chosen partition, then fetch only from that partition ordered by `created_at ASC`.
- Why: Guarantees FIFO per partition and eliminates mixed-partition batches.

- What: Add fairness heuristic for choosing the partition (e.g., partition with the oldest pending message or least recent `last_activity`).
- Why: Prevents starvation and balances throughput without breaking FIFO.

3) Availability predicate simplification
- What: Replace `LEFT JOIN messages_status ... WHERE (ms.id IS NULL OR ms.status IN (...))` with `NOT EXISTS`/UNION.
- Why: Removes OR conditions that block index use; produces better plans.

- What: Precompute per-queue time thresholds (delayed_processing, max_wait_time, window_buffer) in a small CTE.
- Why: Avoids non-sargable predicates involving NOW() and enables index range scans on `created_at`.

4) Pop query shaping
- What: Eliminate correlated subselects in `RETURNING` by projecting needed columns in the CTE and returning them directly.
- Why: Avoids per-row subqueries; simpler and faster.

- What: Keep `FOR UPDATE OF m SKIP LOCKED` in queue-mode pops.
- Why: Ensures a message is only claimed by one worker in competing consumer mode.

5) Bus-mode (consumer groups)
- What: Use `ON CONFLICT DO NOTHING` (or update) when inserting into `messages_status`.
- Why: Handles races on `(message_id, consumer_group)` without surfacing errors; maintains single-delivery per group.

6) API pagination
- What: Implement keyset (cursor) pagination for `listMessages` while preserving offset for backward compatibility.
- Why: OFFSET is O(n) at high pages; keyset keeps scans bounded and stable using `(created_at, id)`.

7) Retention and eviction
- What: Align `retentionService` with schema v2: delete only when configured to do so; rely on cascade for statuses.
- Why: Prevents unintended loss of unprocessed messages; keeps behavior explicit.

- What: Keep eviction as status transitions (e.g., `evicted`) and log counts to retention history.
- Why: Avoids destructive deletes while signaling expired messages.

8) Tests
- What: Add/adjust tests for single-partition FIFO (when partition unspecified), no duplicates under concurrency (queue/bus), keyset pagination, and retention/eviction behavior.
- Why: Prevent regressions and validate concurrency semantics.

## Rollout Order
1. Transaction hardening (whitelist isolation, timeouts) and retry policy.
2. Partition-first pop logic (queue-mode, namespace/task) with fairness.
3. Availability predicate rewrite and time-threshold precompute.
4. Pop `RETURNING` cleanup (remove correlated subselects).
5. Bus-mode conflict handling with ON CONFLICT.
6. Keyset pagination for `listMessages`.
7. Retention adjustments.
8. Tests and docs.
