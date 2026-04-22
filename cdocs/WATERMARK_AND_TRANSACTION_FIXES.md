# Watermark and Transaction Correctness Fixes

## 0. Context

This document collects three localized SQL-only correctness fixes identified during the deep review of the POP path:

- **Fix A**: Bootstrap `EXISTS` spam for new consumer groups when all partitions are temporarily leased.
- **Fix B**: `update_consumer_group_subscription_v1` does not invalidate `consumer_watermarks`.
- **Fix C**: The transactional push path redundantly writes to `partition_lookup` with wrong semantics (no monotonicity guard, no `updated_at` refresh).

All three are small, independent patches. They can be deployed together or separately. They have zero performance impact on the hot path and are safe to ship ahead of the larger push/ack/libqueen plans.

## 1. Fix A — Bootstrap `EXISTS` spam for new consumer groups under contention

### 1.1 Location

`lib/schema/procedures/002_pop_unified.sql`, inside the wildcard discovery branch of `pop_unified_batch`, specifically the "LOCKED DATA" path (lines 848–854 in the current source).

### 1.2 Observed pathology

The wildcard path runs an initial narrowed scan filtered by `pl.updated_at >= (v_watermark - interval '2 minutes')`. If that returns no partition:

1. `v_watermark_verified_at` is consulted — if it's within 30 s, trust the watermark and report `no_available_partition` (cheap).
2. Otherwise, run a ~13 ms `EXISTS` check to decide whether any pending data exists at all (ignoring leases).
3. If `EXISTS` returns **true** (data exists but all partitions are temporarily leased), we want to *throttle* future `EXISTS` checks for 30 s to avoid spam.
4. If `EXISTS` returns **false** (queue is truly empty for this consumer group), advance the watermark and the cache timestamp.

The "throttle" step updates `consumer_watermarks.updated_at = NOW()`:

```sql
UPDATE queen.consumer_watermarks
SET updated_at = v_now
WHERE queue_name = v_req.queue_name AND consumer_group = v_req.consumer_group;
```

**Bug**: if no row exists yet in `consumer_watermarks` for `(queue_name, consumer_group)` — which is the case for a **brand-new consumer group** — this `UPDATE` matches zero rows. `v_watermark_verified_at` stays NULL. On the next pop, the `EXISTS` check runs again. And again. The 30 s throttle fails open during the bootstrap window.

Impact under pathological load: a new consumer group polling a hot queue where all partitions are leased by other workers triggers the `EXISTS` query on every pop request instead of once every 30 s. This is ~13 ms of DB work per pop and can multiply when many workers bootstrap simultaneously.

### 1.3 Fix

Replace the `UPDATE` with an `INSERT ... ON CONFLICT DO UPDATE` that seeds a row on first encounter:

```sql
ELSE
    -- LOCKED DATA: pending data exists but is currently held by other workers.
    -- Do NOT advance the scan watermark (last_empty_scan_at), because we have
    -- NOT proven the queue is empty. But DO record updated_at = now so the
    -- 30 s EXISTS throttle engages for this (queue, consumer_group). Seeding
    -- the row is essential on first encounter by a brand-new consumer group —
    -- a plain UPDATE would match zero rows and fail to engage the throttle.
    INSERT INTO queen.consumer_watermarks
        (queue_name, consumer_group, last_empty_scan_at, updated_at)
    VALUES
        (v_req.queue_name, v_req.consumer_group,
         '1970-01-01 00:00:00+00'::timestamptz,   -- keep scan filter maximally permissive
         v_now)
    ON CONFLICT (queue_name, consumer_group)
    DO UPDATE SET updated_at = v_now;
END IF;
```

### 1.4 Why seeding `last_empty_scan_at = epoch` is correct

The wildcard scan filter is:

```sql
WHERE pl.updated_at >= (v_watermark - interval '2 minutes')
```

With `v_watermark = epoch`, this becomes `pl.updated_at >= (epoch - 2 min)`, which matches everything. Seeding to epoch on first insert means the scan filter stays maximally permissive — identical to the "no row exists" behavior. This matches the existing default at the top of the procedure:

```sql
IF v_watermark IS NULL THEN
    v_watermark := '1970-01-01 00:00:00+00'::TIMESTAMPTZ;
    v_watermark_verified_at := NULL;
END IF;
```

So the scan behavior is unchanged; only the throttle engages.

### 1.5 Correctness invariants preserved

1. `last_empty_scan_at` is never advanced when pending data exists (still true: we only write epoch on INSERT, no change on CONFLICT).
2. `last_empty_scan_at` is still advanced only in the "truly empty" branch (unchanged).
3. The 30 s `EXISTS` throttle now engages for all consumer groups, including brand-new ones.

### 1.6 Test plan

Add to `client-js/test-v2/watermark.js`:

```
test bootstrapExistsThrottleForNewConsumerGroup:
  # Setup a queue with multiple partitions, messages pending, all leased by other consumers.
  1. Create queue Q with 10 partitions.
  2. Push 1 message to each partition.
  3. Simulate all partitions leased:
       INSERT INTO queen.partition_consumers(partition_id, consumer_group, lease_expires_at, worker_id)
       SELECT partition_id, 'blocker-cg', NOW() + interval '1 hour', 'blocker'
       FROM queen.partition_lookup WHERE queue_name = Q;
  4. Establish a baseline of pg_stat_statements calls for the EXISTS query inside pop_unified_batch.
     (Either parse the statements table or tag via pg_stat_statements.query_id.)
  5. BRAND NEW consumer group C executes wildcard pop on Q.
  6. Assert: exactly 1 EXISTS-query execution observed for C so far.
  7. Execute 20 more wildcard pops for C over the next 10 seconds.
  8. Assert: still exactly 1 EXISTS execution (throttle engaged).
  9. Wait 31 seconds.
 10. Execute wildcard pop for C.
 11. Assert: exactly 2 EXISTS executions now (second verification ran).
```

Without the fix, step 8 fails: every one of the 20 pops re-runs EXISTS.

### 1.7 Risk

None. The new INSERT path only runs in a case where the UPDATE would do nothing; behavior in all other cases is identical.

### 1.8 Patch size

~7 lines changed in `lib/schema/procedures/002_pop_unified.sql`. Single function `pop_unified_batch`.

---

## 2. Fix B — `update_consumer_group_subscription_v1` does not invalidate watermark

### 2.1 Location

`lib/schema/procedures/008_consumer_groups.sql`, function `update_consumer_group_subscription_v1`.

### 2.2 Observed pathology

The function updates `consumer_groups_metadata.subscription_timestamp` but does not touch `consumer_watermarks`. If an operator uses this function to move the subscription timestamp **backwards** (to replay historical data), a stale watermark from prior "caught-up" runs can block the replay:

- The wildcard scan filter `pl.updated_at >= (v_watermark - interval '2 minutes')` excludes partitions whose `updated_at` is older than the watermark minus 2 minutes.
- Replayed partitions were updated long ago (old data) but the consumer group's watermark is recent (previously caught up), so those partitions fail the filter.
- The `EXISTS` re-verification has the same filter, so it also returns false.
- The consumer group effectively cannot see historical data until the watermark is evicted.

The same bug affected `seek_consumer_group_v1` and `delete_consumer_group*` historically and was fixed there (the procedures already `DELETE FROM consumer_watermarks`). `update_consumer_group_subscription_v1` missed the same treatment.

### 2.3 Fix

Mirror the pattern used by `seek_consumer_group_v1` and `delete_consumer_group`:

```sql
CREATE OR REPLACE FUNCTION queen.update_consumer_group_subscription_v1(
    p_consumer_group TEXT,
    p_new_timestamp  TEXT
)
RETURNS JSONB
LANGUAGE plpgsql
AS $$
DECLARE
    v_updated_count INTEGER;
BEGIN
    UPDATE queen.consumer_groups_metadata
    SET subscription_timestamp = p_new_timestamp::timestamptz
    WHERE consumer_group = p_consumer_group;

    GET DIAGNOSTICS v_updated_count = ROW_COUNT;

    -- Invalidate watermarks for this consumer group so a backward move of the
    -- subscription timestamp re-exposes historical partitions to the wildcard
    -- scan filter. Symmetric with delete_consumer_group / seek-backwards.
    -- We delete unconditionally (even for forward moves) because:
    --   - Forward moves: watermark will be re-established on the next empty
    --     scan at most 30 s later; transient extra work is negligible.
    --   - Backward moves: deletion is REQUIRED for correctness.
    -- Deleting always simplifies the contract and removes a foot-gun.
    DELETE FROM queen.consumer_watermarks
    WHERE consumer_group = p_consumer_group;

    RETURN jsonb_build_object(
        'success',        true,
        'consumerGroup',  p_consumer_group,
        'newTimestamp',   p_new_timestamp,
        'rowsUpdated',    v_updated_count
    );
END;
$$;
```

### 2.4 Why delete unconditionally

We could try to compare the old and new timestamps and only delete on backward moves, but:

- Determining the "old" timestamp requires a pre-UPDATE read; more complex code.
- The cost of a stale-watermark-driven extra scan on the first post-update pop is tiny (one EXISTS, ~13 ms) and amortizes over 30 s.
- Unconditional deletion matches the idempotent, always-safe pattern established in sibling procedures.
- Operators rarely change subscription timestamps; rate of calls is negligible.

### 2.5 Scope of the delete

`update_consumer_group_subscription_v1` currently updates `consumer_groups_metadata` WHERE `consumer_group = p_consumer_group` (no queue filter). The DELETE mirrors this scope: all `(queue_name, consumer_group)` entries for the CG. This is consistent with the UPDATE's scope.

If a per-queue variant of this function is ever added, its deletion should be correspondingly filtered by queue_name. No such variant exists today.

### 2.6 Test plan

Add to `client-js/test-v2/watermark.js`:

```
test updateSubscriptionBackwardAllowsReconsume:
  1. Create queue Q and push messages at time T0.
  2. Consumer group C subscribes with subscription_mode='new' at time T1 > T0.
     -> No messages visible to C (all predate subscription).
  3. Trigger a pop (empty) so the watermark advances to near-now.
  4. Advance the watermark artificially (using the helper from the existing watermark tests) to simulate 10 minutes of time passing.
  5. Assert: consumer_watermarks row exists for (Q, C).
  6. Call admin.updateConsumerGroupSubscription(C, T0 - 1 hour).
  7. Assert: watermark row is deleted.
  8. Pop: all historical messages are returned.

test updateSubscriptionForwardStillWorks:
  1. Same setup, but move the subscription timestamp FORWARD (to NOW + 1 minute).
  2. Assert: watermark row is deleted.
  3. Pop: no messages (as expected; nothing past the new timestamp).
  4. Push a message. Pop: message returned.
```

### 2.7 Risk

None. Deleting watermark rows is strictly safe — the worst case is a single extra scan on the next pop, automatically rebuilt after ~30 s of normal operation.

### 2.8 Patch size

4 lines added inside one function.

---

## 3. Fix C — Transactional push redundant/unguarded `partition_lookup` write

### 3.1 Location

`lib/schema/procedures/004_transaction.sql`, inside the `'push'` branch of `execute_transaction_v2` (around lines 76–80 in current source).

### 3.2 Observed pathology

The `'push'` branch of the transactional procedure inserts the message **and** directly inserts/updates `partition_lookup`:

```sql
INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id, is_encrypted)
VALUES (v_message_id, v_txn_id, v_partition_id, v_payload, v_trace_id,
        COALESCE((v_op->>'is_encrypted')::boolean, false));

INSERT INTO queen.partition_lookup (partition_id, queue_name, last_message_id, last_message_created_at)
VALUES (v_partition_id, v_queue_name, v_message_id, NOW())
ON CONFLICT (queue_name, partition_id) DO UPDATE SET
    last_message_id = EXCLUDED.last_message_id,
    last_message_created_at = EXCLUDED.last_message_created_at;
```

Problems with the explicit `partition_lookup` write:

1. **Missing `updated_at` refresh in `DO UPDATE`**. The `DEFAULT NOW()` on the column only fires on INSERT, not on UPDATE. The watermark optimization in `pop_unified_batch` depends on `partition_lookup.updated_at` being a fresh timestamp.

2. **Missing monotonicity guard**. The normal INSERT path uses the statement-level trigger `trg_update_partition_lookup`, which includes:

   ```sql
   WHERE EXCLUDED.last_message_created_at > queen.partition_lookup.last_message_created_at
      OR (EXCLUDED.last_message_created_at = queen.partition_lookup.last_message_created_at
          AND EXCLUDED.last_message_id > queen.partition_lookup.last_message_id);
   ```

   The direct INSERT has no such guard. Two concurrent transactions can race: the one whose `NOW()` is older could clobber the one with the newer `NOW()`, regressing `last_message_id` and `last_message_created_at`. With UUIDv7 and monotonic `NOW()` in production this is unlikely, but the semantics disagree with the trigger.

3. **Redundancy**. The preceding `INSERT INTO queen.messages` fires `trg_update_partition_lookup` (statement-level, `REFERENCING NEW TABLE AS new_messages`). The trigger already performs the `partition_lookup` write correctly, with both the `updated_at` refresh and the monotonicity guard. The explicit INSERT is therefore pure duplicate work that, worst case, clobbers correct data with incorrect data.

### 3.3 Fix

Delete the explicit `INSERT INTO queen.partition_lookup ...` statement entirely. Let the trigger do the work:

```sql
-- Existing code:
INSERT INTO queen.messages (id, transaction_id, partition_id, payload, trace_id, is_encrypted)
VALUES (v_message_id, v_txn_id, v_partition_id, v_payload, v_trace_id,
        COALESCE((v_op->>'is_encrypted')::boolean, false));

-- Remove: the explicit INSERT INTO queen.partition_lookup ... block.
-- trg_update_partition_lookup fires on the INSERT above and handles partition_lookup
-- correctly, with monotonicity guard and updated_at refresh.

v_op_success := true;

-- ... rest of the branch unchanged ...
```

### 3.4 Pre-deployment verification

Before removing the explicit INSERT, verify:

1. **Trigger exists and fires on plpgsql inserts**. Check `lib/schema/schema.sql:405-409`:
   ```sql
   CREATE OR REPLACE TRIGGER trg_update_partition_lookup
       AFTER INSERT ON queen.messages
       REFERENCING NEW TABLE AS new_messages
       FOR EACH STATEMENT
       EXECUTE FUNCTION queen.update_partition_lookup_trigger();
   ```
   Statement-level triggers on plain INSERTs inside PL/pgSQL functions **do** fire. `REFERENCING NEW TABLE` works because the INSERT is a top-level statement within the function's execution.

2. **Trigger handles single-row inserts**. The trigger uses `DISTINCT ON (partition_id) ORDER BY created_at DESC, id DESC` over `new_messages`, which is correct for any batch size ≥ 1.

3. **No out-of-band writers** to `partition_lookup`. Search confirms only these writers:
   - `trg_update_partition_lookup` (this trigger).
   - The explicit INSERT in `execute_transaction_v2` (what we're removing).
   - `seek_consumer_group_v1` reads `partition_lookup` but does not write.
   - Migration code in `server/src/routes/migration.cpp` manipulates for migration only.

### 3.5 Correctness post-fix

The invariants the watermark optimization relies on stay satisfied:

- Every message insertion — whether via `push_messages_v2` or `execute_transaction_v2` — goes through `INSERT INTO queen.messages` and therefore triggers `trg_update_partition_lookup`.
- The trigger writes `last_message_*` and `updated_at` with a proper monotonicity guard.
- No code path bypasses the trigger for writes.

### 3.6 Test plan

Add to `client-js/test-v2/transaction.js`:

```
test transactionalPushUpdatesPartitionLookup:
  1. Create queue Q, partition P.
  2. Record pre-test timestamp T_before.
  3. Execute transaction:
       [{ type: 'push', queue: Q, partition: P, payload: {n:1} }]
  4. Record timestamp T_after.
  5. Query queen.partition_lookup WHERE queue_name = Q AND partition_id = <P.id>.
  6. Assert row exists, last_message_id is the message just inserted,
     last_message_created_at is between T_before and T_after,
     updated_at is between T_before and T_after.

test transactionalPushMonotonicityAcrossTransactions:
  1. Two concurrent transactions each push 1 message to same (Q, P).
     Use artificial control on one to set created_at older than the other (via
     a controlled test-only path, if available; otherwise rely on natural ordering).
  2. Query partition_lookup: last_message_created_at should be the MAX across both,
     not the last-writer-wins.
  3. Assert updated_at is fresh (within the last second) regardless of which won.

test transactionalPushTriggerFiresOnSingleInsert:
  1. Execute a single-message transactional push.
  2. Query partition_lookup: verify trigger populated the row.
  3. Also verify updated_at was set (not left at DEFAULT from a stale previous row).
```

### 3.7 Risk

**Small**. The only failure mode is "trigger doesn't fire for plpgsql INSERTs". This is standard, well-documented PostgreSQL behavior; any hypothetical regression would be caught immediately by the tests above and by any functional push test. Additionally, the trigger has been the primary (and correct) mechanism all along for the non-transactional push path, so we know it works.

Edge case: if an operator manually disables the trigger for any reason (maintenance, debugging), the transactional push path would silently fail to update `partition_lookup`. This already causes breakage under the current code (the direct INSERT misses `updated_at`). Document the trigger's criticality in `lib/schema/schema.sql` with a comment, and add a startup-time assertion that the trigger is enabled.

### 3.8 Patch size

5 lines removed in `lib/schema/procedures/004_transaction.sql`. One function, one branch, no signature change.

---

## 4. Deployment plan for all three fixes

### 4.1 Ordering

- **All three fixes are independent** and can be deployed in any order or combined.
- **Recommended order**: A → B → C. A has the clearest performance payoff; B has the highest correctness payoff; C has the smallest surface area and gives the watermark invariants a cleaner foundation for future optimization.

### 4.2 Combined patch form

Can all be shipped in a single release as a set of `CREATE OR REPLACE FUNCTION` statements:

```sql
-- Fix A
CREATE OR REPLACE FUNCTION queen.pop_unified_batch(...) ...
-- Fix B
CREATE OR REPLACE FUNCTION queen.update_consumer_group_subscription_v1(...) ...
-- Fix C
CREATE OR REPLACE FUNCTION queen.execute_transaction_v2(...) ...
```

Each `CREATE OR REPLACE` preserves the function OID, so the switchover is atomic and zero-downtime.

### 4.3 Schema version bump

Bump the schema version constant (if any) and record a migration note in release notes. No DDL on tables is required.

### 4.4 Rollback

Each fix is a body-only change to a procedure. Rollback is `CREATE OR REPLACE` back to the prior body. No data migration, no downtime.

### 4.5 Observability

Before deploying Fix A, capture a 1-hour baseline of:

- Count of `EXISTS`-containing plan executions in `pg_stat_statements` for the `pop_unified_batch` query.
- Rows inserted/updated into `consumer_watermarks` per minute.

After deploying Fix A, verify:

- `EXISTS` execution rate drops sharply for consumer groups that are in the "all partitions leased" state.
- `consumer_watermarks` row count stabilizes (no runaway growth).

Before deploying Fix C, capture:

- `pg_stat_user_tables` for `queen.partition_lookup`: `n_tup_upd` per minute.

After deploying Fix C, verify:

- `n_tup_upd` on `partition_lookup` drops roughly by the transactional-push rate (we removed one UPDATE per transactional push message). This is the improvement signature.

### 4.6 Tests required before merge

- All three tests in their respective `client-js/test-v2/*.js` files passing.
- Existing test suites passing unchanged.
- `pg_stat_statements` sanity check showing no unexpected new queries.

## 5. Code locations that change

| File | Fix | Change |
|---|---|---|
| `lib/schema/procedures/002_pop_unified.sql` | A | Replace the `UPDATE consumer_watermarks SET updated_at = v_now ...` block with the `INSERT ... ON CONFLICT DO UPDATE` variant. |
| `lib/schema/procedures/008_consumer_groups.sql` | B | Append a `DELETE FROM queen.consumer_watermarks WHERE consumer_group = p_consumer_group;` to `update_consumer_group_subscription_v1`. |
| `lib/schema/procedures/004_transaction.sql` | C | Remove the `INSERT INTO queen.partition_lookup ...` block from the `'push'` branch of `execute_transaction_v2`. |
| `client-js/test-v2/watermark.js` | A, B | Add tests listed in §1.6 and §2.6. |
| `client-js/test-v2/transaction.js` | C | Add tests listed in §3.6. |
| `lib/schema/schema.sql` | C (optional) | Add comment above `trg_update_partition_lookup` noting that the transactional push path depends on this trigger. |

## 6. Out of scope

- **Per-queue `update_consumer_group_subscription_v2`** with queue filter: add when/if there's a user need.
- **Adding a UNIQUE constraint on `(partition_id, transaction_id)` in `queen.messages`**: already present as `messages_partition_transaction_unique`. Mentioned for completeness.
- **Reintroducing an index on `partition_lookup.updated_at`**: explicitly avoided per the documented HOT-preservation strategy.
- **Per-consumer-group watermark TTL / GC**: watermarks persist forever unless a consumer group is deleted or seeked. Acceptable today.
- **Unifying auto-ack cursor-advance code path with the ACK procedure**: tracked in the ACK improvement plan as follow-up.
