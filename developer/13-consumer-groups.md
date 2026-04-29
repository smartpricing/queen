# 13 — Consumer groups

Consumer groups let you fan out the same message stream to multiple independent consumers. Each group has its own independent progress (`last_consumed_id`) per partition, so adding a new group doesn't disturb existing ones, and one group falling behind doesn't stall the others.

This page is the developer-side: the data model, subscription modes, and the procedures that implement them.

User-facing docs: `[docs/concepts.html](../docs/concepts.html)`.

---

## Data model

Two tables drive consumer groups:

### `queen.partition_consumers`

Per-(partition, consumer_group) state. **One row per partition per group**, created lazily on first pop.

```sql
partition_id UUID,
consumer_group VARCHAR(255) DEFAULT '__QUEUE_MODE__',
last_consumed_id UUID DEFAULT '00000000-0000-0000-0000-000000000000',
last_consumed_created_at TIMESTAMPTZ,
total_messages_consumed BIGINT,
last_consumed_at TIMESTAMPTZ,
lease_expires_at TIMESTAMPTZ,
lease_acquired_at TIMESTAMPTZ,
worker_id VARCHAR(255),
batch_size INTEGER,
acked_count INTEGER,
message_batch JSONB,
batch_retry_count INTEGER,
created_at TIMESTAMPTZ,
UNIQUE(partition_id, consumer_group)
```

Two roles for this table:

1. **Offset tracking** (`last_consumed_id`, `last_consumed_created_at`) — Kafka-equivalent of consumer offsets.
2. **Lease state** (`worker_id`, `lease_expires_at`, `message_batch`, `batch_size`, `acked_count`) — which consumer currently has this partition leased, and how much of the batch is still in flight.

The sentinel `'00000000-0000-0000-0000-000000000000'` means "this group has not consumed anything yet from this partition." Several procedures key off this — see [05 — Database schema](05-database-schema.md) and [09 — Retention](09-retention.md).

The default group `'__QUEUE_MODE__'` is what the broker uses when no `consumer_group` is specified — it gives you classic "queue mode" (one logical consumer for the queue).

### `queen.consumer_groups_metadata`

Per-(group, queue, partition, namespace, task) **subscription configuration**: when this group started subscribing and how it should be initialized.

```sql
consumer_group TEXT NOT NULL,
queue_name TEXT NOT NULL DEFAULT '',
partition_name TEXT NOT NULL DEFAULT '',
namespace TEXT NOT NULL DEFAULT '',
task TEXT NOT NULL DEFAULT '',
subscription_mode TEXT NOT NULL,
subscription_timestamp TIMESTAMPTZ NOT NULL,
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
UNIQUE (consumer_group, queue_name, partition_name, namespace, task)
```

This table answers "when this group joins for the first time, which messages should it see?" The answer depends on `subscription_mode`.

---

## Subscription modes

Three options. Set by the **client** on the request as `subscriptionMode`; the broker normalizes and stores into `consumer_groups_metadata.subscription_mode` on first join. **The wire values are different from the stored values** — the client passes one of `'all'`, `'new'`, or an ISO timestamp string; the procedure stores `'all'`, `'new'`, or `'timestamp'` (with the parsed timestamp in `subscription_timestamp`).

### `'all'` (default)

The group sees **every** message in the queue, including history.

**Implementation:** `last_consumed_id` is left at the sentinel UUID `'00000000-0000-0000-0000-000000000000'`. The consumer reads from the very first message in each partition.

Use this for: a new processor that needs to backfill, or for replay/reprocessing.

### `'new'`

The group sees only messages **created at or after the moment of first join**.

**Implementation:** the procedure records `subscription_timestamp = NOW()` and stores `subscription_mode = 'new'`. Pop reads use `COALESCE(last_consumed_created_at, subscription_timestamp)` as the lower bound, so messages older than the join time are filtered out even though `last_consumed_id` is still the sentinel.

Use this for: a new analytics consumer that doesn't need history. The wire value is `subscriptionMode: 'new'`. (`'now'` works as a synonym in the pop procedure path.)

### `'timestamp'` — replay from a specific point

The client passes an ISO 8601 timestamp string as `subscriptionMode`. The procedure parses it, stores `subscription_mode = 'timestamp'`, sets `subscription_timestamp` to the parsed value, and pop reads use it as the lower bound (same code path as `'new'`).

Use this for: replay from a known point in time (e.g. "reprocess everything since the bug was fixed at 14:00 yesterday").

> If the timestamp string fails to parse, the procedure silently falls through to default behavior (`'all'`). Worth being aware of when debugging "why is my replay reading from the beginning?"

The procedures handling this are in `lib/schema/procedures/002_pop_unified.sql` (the subscription-mode initialization branch, around line 190) and `lib/schema/procedures/008_consumer_groups.sql` (listing, replay, deletion).

---

## How a pop interacts with consumer groups

When `pop` is called with `consumer_group=alpha`:

1. **Resolve subscription** — Check `consumer_groups_metadata`. If no row, this is the first join: insert one using the request's mode + timestamp. If a row exists, use it.
2. **Resolve partition state** — Check `partition_consumers` for `(partition, alpha)`. If no row, create it with `last_consumed_id` initialized per the subscription mode.
3. **Claim** — Try `pg_try_advisory_xact_lock(hashtextextended(partition_id::text, …))`. The lock key is **per-partition only**, not per-(partition, group). Held just for the duration of this procedure call.
4. **Lease** — Write `worker_id` + `lease_expires_at` into `partition_consumers` (the per-group row).
5. **Read** — Return messages with `(created_at, id) > (last_consumed_created_at, last_consumed_id)` for this group, capped at `batch_size`. The lease covers all of them.
6. **Ack** — `ack_messages_v2` advances `last_consumed_id` to the highest acked ID. When `acked_count >= batch_size`, the lease is auto-released (consumer can immediately re-pop).

Two consumer groups *do* serialize on the same partition's advisory lock, but only for the few milliseconds the procedure call takes. The lock is **not** held for the duration of the consumer's lease — once the procedure returns, the lock is released. The lease itself is enforced by `partition_consumers.lease_expires_at` per group, so within a group only one consumer can be in flight at a time. Across groups, contention is on the lock for ~ms, then both proceed independently (each has its own row in `partition_consumers`).

This is the behavior that gives Queen "fan-out with fairness" without per-group locking.

---

## Routes that work with consumer groups


| HTTP                                      | Route                        | Procedure               |
| ----------------------------------------- | ---------------------------- | ----------------------- |
| `POST /api/v1/pop` (with `consumerGroup`) | `routes/pop.cpp`             | `pop_unified_batch_v4`  |
| `POST /api/v1/ack` (with `consumerGroup`) | `routes/ack.cpp`             | `ack_messages_v2`       |
| `POST /api/v1/consumer-groups/replay`     | `routes/consumer_groups.cpp` | `replay_to_timestamp`   |
| `GET /api/v1/consumer-groups`             | `routes/consumer_groups.cpp` | `list_consumer_groups`  |
| `DELETE /api/v1/consumer-groups/:name`    | `routes/consumer_groups.cpp` | `delete_consumer_group` |


---

## Replay

A group can be reset back to a timestamp:

```bash
curl -X POST http://localhost:6632/api/v1/consumer-groups/replay \
  -H 'Content-Type: application/json' \
  -d '{
    "consumerGroup": "alpha",
    "queue": "orders",
    "fromTimestamp": "2025-04-01T00:00:00Z"
  }'
```

What happens:

1. The procedure walks every `partition_consumers` row for `(group, queue)`.
2. For each, computes the largest `messages.id` whose `created_at < fromTimestamp` and overwrites `last_consumed_id` with it.
3. The next `pop` for that group will see all messages from `fromTimestamp` forward.

Replay is **destructive to progress** — once you replay, the group's previous progress is lost. There's no undo.

---

## Wildcard pop with multiple groups

A wildcard pop (`pop?queue=orders&consumerGroup=alpha`) walks `partition_lookup` for the queue and tries to claim partitions. The advisory lock key is **per-partition only** — `hashtextextended(partition_id::text, …)`. So:

- `alpha` and `beta` momentarily contend on the same partition's lock when they pop concurrently, but each call is short (single-digit ms) and they're naturally interleaved.
- Each group reads from its own row in `partition_consumers`, so they have independent offsets and the data they see is independent.
- Within a group, `partition_consumers.lease_expires_at` enforces single-consumer-at-a-time per partition (different from the advisory lock, which is just the procedure-level critical section).

This is what gives Queen its "fan-out with fairness" property: each group gets a full copy of every message because each has its own offset, and within a group the lease prevents two consumers from racing on the same offset.

---

## Things to be careful about when changing consumer-group code

1. **Don't break the sentinel UUID handling.** Several CHECKs in `schema.sql` and HAVING clauses in retention rely on `'00000000-0000-0000-0000-000000000000'` meaning "not consumed yet." If you change the sentinel value, retention will silently delete unconsumed messages.
2. **Subscription mode is set on first join, never changed.** A second pop with a different `subscription_mode` is silently ignored. If you want to "convert" a group's mode, you must `DELETE FROM consumer_groups_metadata` for that group and recreate.
3. **The advisory lock is per-partition, not per-(partition, group).** Two consumer groups serialize through the procedure-level lock for a few ms each. Don't widen the lock scope (e.g. by changing the namespace constant) — you'll cross-couple unrelated partitions. Don't try to "split" it by adding the group to the hash either; it would invalidate the assumption that one consumer at a time mutates a given partition's `partition_lookup`-related state.
4. **Lease state is shared across pops.** If a consumer pops, doesn't ack, the lease expires, and another consumer of the same group pops — the second consumer takes over the same `last_consumed_id`. **Already-popped-but-not-acked messages are re-delivered.** Ack is the *only* thing that moves the offset forward. Document this behavior in any client SDK you change.
5. `**message_batch` is a JSONB column.** When a consumer pops, the messages are stamped into `message_batch` so a re-pop after a crash can resume from the same batch. Don't shrink this column or change its schema without careful ack handling.
6. **Renew_lease is per-(partition, worker_id), not per-group.** A single `renew` call extends every partition leased by that `worker_id`. Useful when a wildcard pop grabbed N partitions: one `renew` keeps them all alive.

---

## Operational note: "stuck" consumer groups

If a group's `last_consumed_id` never advances (consumer stopped acking, but the row still exists), it pins:

- **Completed-retention deletion** — the `MIN(last_consumed_id)` across groups stays at the stuck point, so consumed messages aren't cleaned up.
- **Inactive-partition cleanup** — the partition has a consumer registered, so it isn't deleted as inactive.

Detect with the dashboard's "consumer lag" view, or with this query:

```sql
SELECT q.name, p.name AS partition, pc.consumer_group,
       pc.last_consumed_id, pc.last_consumed_at,
       NOW() - pc.last_consumed_at AS staleness
FROM queen.partition_consumers pc
JOIN queen.partitions p ON p.id = pc.partition_id
JOIN queen.queues q ON q.id = p.queue_id
WHERE pc.last_consumed_at < NOW() - INTERVAL '7 days'
ORDER BY staleness DESC;
```

To unblock: either resume the consumer (no code change), or `DELETE FROM partition_consumers WHERE consumer_group = '...'` to drop the group entirely. The next pop from that group will recreate the row per its subscription mode.