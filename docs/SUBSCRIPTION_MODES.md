# Subscription Modes

Complete guide to controlling where consumer groups start reading messages.

## Overview

Subscription modes allow you to control where a new consumer group starts reading messages from a queue. This is crucial for:
- Preventing accidental processing of large historical backlogs
- Real-time systems that only care about new events
- Replay and debugging scenarios
- Data migration and backfilling

**Key Point:** Subscription modes only apply when a consumer group is **first created**. Existing consumer groups continue from their saved position.

## Server Default Configuration

### Default Behavior (Backward Compatible)

By default, new consumer groups process **all messages** from the beginning:

```javascript
await queen.queue('events')
  .group('new-analytics')
  .consume(async (msg) => {
    // Processes ALL messages, including historical
  })
```

### Changing the Server Default

You can configure the server to make **all new consumer groups** skip historical messages by default:

```bash
export DEFAULT_SUBSCRIPTION_MODE="new"
./bin/queen-server
```

**When to use:**
- ✅ Real-time notification systems
- ✅ Monitoring and alerting platforms
- ✅ Event-driven microservices
- ✅ Development/staging environments
- ✅ Systems where only new events matter

**When NOT to use:**
- ❌ Analytics systems that need historical data
- ❌ Data processing pipelines that replay events
- ❌ Audit systems that track all messages

## Client Subscription Modes

Clients can always override the server default by explicitly specifying a subscription mode.

### Mode 1: All Messages (Default)

Process all messages from the beginning:

```javascript
await queen.queue('events')
  .group('analytics')
  // No subscriptionMode = all messages (or whatever server default is)
  .consume(async (msg) => {
    // Processes ALL messages
  })
```

**Explicit override when server has DEFAULT_SUBSCRIPTION_MODE="new":**
```javascript
await queen.queue('events')
  .group('analytics')
  .subscriptionMode('all')  // Force process all messages
  .consume(async (msg) => {
    // Processes ALL messages even if server default is "new"
  })
```

### Mode 2: New Messages Only

Skip all historical messages:

```javascript
await queen.queue('events')
  .group('realtime-alerts')
  .subscriptionMode('new')  // Skip history
  .consume(async (msg) => {
    // Only new messages after subscription
  })
```

**Aliases:**
- `.subscriptionMode('new')`
- `.subscriptionMode('new-only')`
- `.subscriptionFrom('now')`

### Mode 3: From Specific Timestamp

Start from a specific point in time:

```javascript
await queen.queue('events')
  .group('recovery-processor')
  .subscriptionFrom('2025-01-01T00:00:00.000Z')  // ISO 8601
  .consume(async (msg) => {
    // Messages from Jan 1, 2025 onwards
  })
```

**Dynamic timestamp:**
```javascript
// Start from 1 hour ago
const oneHourAgo = new Date(Date.now() - 3600000).toISOString()

await queen.queue('events')
  .group('recent-events')
  .subscriptionFrom(oneHourAgo)
  .consume(async (msg) => {
    // Messages from last hour
  })
```

## Real-World Examples

### Example 1: Multi-Purpose Processing

```javascript
// Analytics: Need all historical data
await queen.queue('user-events')
  .group('analytics')
  // No subscriptionMode = process all
  .consume(async (event) => {
    await updateAnalytics(event.data)
  })

// Notifications: Only new events
await queen.queue('user-events')
  .group('notifications')
  .subscriptionMode('new')
  .consume(async (event) => {
    await sendNotification(event.data)
  })
```

### Example 2: Debugging Incident

```javascript
// Incident happened at 2:30 PM
const incidentTime = '2025-11-08T14:30:00.000Z'

// Replay from that point
await queen.queue('transactions')
  .group('debug-' + Date.now())  // Unique group name
  .subscriptionFrom(incidentTime)
  .limit(100)  // Process first 100 messages
  .consume(async (msg) => {
    console.log('Replaying transaction:', msg.data)
    // Add debugging logic here
  })
```

### Example 3: Server Default in Action

**Server configuration:**
```bash
export DEFAULT_SUBSCRIPTION_MODE="new"
./bin/queen-server
```

**Client code:**
```javascript
// Consumer 1: Uses server default (new messages only)
await queen.queue('events')
  .group('realtime-monitor')
  .consume(async (msg) => {
    // Only new messages (server default applied)
  })

// Consumer 2: Explicit override to process all
await queen.queue('events')
  .group('historical-processor')
  .subscriptionMode('all')  // Override server default
  .consume(async (msg) => {
    // Processes ALL messages
  })
```

## Behavior Details

### First Subscription Matters

The subscription mode is applied **only when the consumer group is first created**:

```javascript
// First run - creates consumer group
await queen.queue('events')
  .group('processor')
  .subscriptionMode('new')  // ✅ Applied! Skips history
  .consume(async (msg) => { /* ... */ })

// Second run - consumer group already exists
await queen.queue('events')
  .group('processor')
  .subscriptionMode('new')  // ❌ Ignored! Uses saved position
  .consume(async (msg) => { /* ... */ })
```

**To reset a consumer group:**
```sql
-- Delete the consumer group to start fresh
DELETE FROM queen.partition_consumers 
WHERE consumer_group = 'processor';
```

### Consumer Group vs Queue Mode

Subscription modes **ONLY** work with consumer groups:

```javascript
// ✅ Works - has consumer group
await queen.queue('events')
  .group('processor')
  .subscriptionMode('new')
  .consume(async (msg) => { /* ... */ })

// ❌ Ignored - no consumer group (queue mode)
await queen.queue('events')
  .subscriptionMode('new')  // This is ignored!
  .consume(async (msg) => { /* ... */ })
```

### Multiple Partitions

Subscription mode applies independently to each partition:

```javascript
await queen.queue('events')
  .group('processor')
  .subscriptionMode('new')
  .consume(async (msg) => { /* ... */ })

// Each partition in 'events' queue tracks its own position
// for the 'processor' consumer group
```

## Configuration Reference

### Environment Variable

| Variable | Default | Valid Values |
|----------|---------|--------------|
| `DEFAULT_SUBSCRIPTION_MODE` | `""` (all messages) | `""`, `"new"`, `"new-only"` |

**Note:** Empty string (`""`) means process all messages (backward compatible default).

### Client API

```javascript
// Method 1: subscriptionMode()
.subscriptionMode('new')      // Skip history
.subscriptionMode('new-only') // Same as 'new'
.subscriptionMode('all')      // Force all messages

// Method 2: subscriptionFrom()
.subscriptionFrom('now')                          // Same as subscriptionMode('new')
.subscriptionFrom('2025-01-01T00:00:00.000Z')    // From timestamp
```

## Database Schema

Consumer groups are tracked in the `partition_consumers` table:

```sql
SELECT 
  consumer_group,
  last_consumed_id,
  last_consumed_created_at,
  total_messages_consumed
FROM queen.partition_consumers
WHERE consumer_group = 'your-group';
```

**Fields:**
- `last_consumed_id`: UUID of last processed message
- `last_consumed_created_at`: Timestamp of last processed message
- When both are NULL → consumer group starts from beginning

### Subscription Metadata Tracking

Since v0.5.5, Queen also tracks consumer group subscriptions in a separate metadata table:

```sql
SELECT 
  consumer_group,
  queue_name,
  partition_name,
  subscription_mode,
  subscription_timestamp,
  created_at
FROM queen.consumer_groups_metadata
WHERE consumer_group = 'your-group';
```

**How it works:**

1. **First pop** - When a consumer group makes its first pop request:
   - Metadata is recorded with `subscription_timestamp = NOW()`
   - This timestamp represents when the consumer group "subscribed"
   
2. **Subsequent pops** - For new partitions or queues:
   - The **original subscription_timestamp** is reused
   - All partitions use the same subscription time
   - Ensures consistent "NEW" mode behavior

3. **NEW mode behavior:**
   - Cursor set to: `(minimal_uuid, subscription_timestamp)`
   - Only messages with `created_at > subscription_timestamp` are consumed
   - True "NEW" semantics - only messages arriving **after** subscription

**Benefits:**
- ✅ Consistent subscription time across all partitions
- ✅ New partitions discovered later don't skip messages
- ✅ True real-time semantics (no arbitrary lookback)
- ✅ Works with namespace/task wildcards

## Troubleshooting

### Problem: Consumer group processes old messages despite subscriptionMode('new')

**Cause:** The consumer group already exists from a previous run.

**Solution:** Delete the consumer group and recreate:
```sql
DELETE FROM queen.partition_consumers 
WHERE consumer_group = 'your-group-name';
```

Or use a unique group name each time for testing:
```javascript
.group('test-' + Date.now())
```

### Problem: Want to reset existing consumer group

**Solution:** Delete both partition consumers AND subscription metadata, then recreate:

```sql
-- Delete partition-level state
DELETE FROM queen.partition_consumers WHERE consumer_group = 'processor';

-- Delete subscription metadata (v0.5.5+)
DELETE FROM queen.consumer_groups_metadata WHERE consumer_group = 'processor';
```

**Alternative:** Manually update the cursor and metadata:
```sql
-- Option 1: Skip to latest message on all partitions
UPDATE queen.partition_consumers
SET 
  last_consumed_created_at = NOW(),
  last_consumed_id = (
    SELECT MAX(id) FROM queen.messages m 
    WHERE m.partition_id = partition_consumers.partition_id
  )
WHERE consumer_group = 'processor';

-- Update subscription metadata to NOW
UPDATE queen.consumer_groups_metadata
SET subscription_timestamp = NOW()
WHERE consumer_group = 'processor';
```

### Problem: Server default not taking effect

**Verify configuration:**
```bash
# Check if environment variable is set
echo $DEFAULT_SUBSCRIPTION_MODE

# Check server logs on startup
LOG_LEVEL=debug ./bin/queen-server
# Look for: "Applying default subscription mode 'new' for consumer group..."
```

## Migration Guide

### From No Default to DEFAULT_SUBSCRIPTION_MODE="new"

**Impact:** Existing consumer groups are **NOT affected**. Only new consumer groups created after the change will skip history.

**Safe Migration:**
1. Set `DEFAULT_SUBSCRIPTION_MODE="new"`
2. Restart server
3. Test with a new consumer group
4. Existing groups continue normally

**Rollback:** Remove the environment variable and restart server.

### From "new" Default Back to "all"

Simply remove the environment variable or set it to empty string:

```bash
export DEFAULT_SUBSCRIPTION_MODE=""  # Or unset DEFAULT_SUBSCRIPTION_MODE
./bin/queen-server
```

## Best Practices

### 1. Name Groups Based on Subscription Mode

```javascript
// ✅ Good: Name indicates behavior
.group('realtime-alerts-new')
.group('analytics-all-history')
.group('recovery-from-incident-20251108')

// ❌ Bad: Name doesn't indicate behavior
.group('processor')
.group('worker-1')
```

### 2. Be Explicit in Production

Even if server has a default, be explicit in production code:

```javascript
// ✅ Good: Clear intent
.group('production-alerts')
.subscriptionMode('new')  // Explicit!
.consume(...)

// ⚠️ Okay but risky: Relies on server default
.group('production-alerts')
.consume(...)  // What happens if server config changes?
```

### 3. Document Your Choice

```javascript
// ✅ Good: Comment explains why
await queen.queue('user-events')
  .group('notifications')
  .subscriptionMode('new')  // Only care about new signups, not historical
  .consume(async (event) => {
    await sendWelcomeEmail(event.data)
  })
```

### 4. Monitor Consumer Group Lag

Check that consumer groups aren't falling behind:

```sql
-- Check consumer group status
SELECT 
  consumer_group,
  last_consumed_at,
  total_messages_consumed,
  (
    SELECT COUNT(*) FROM queen.messages m
    WHERE m.partition_id = pc.partition_id
      AND m.created_at > pc.last_consumed_created_at
  ) as lag
FROM queen.partition_consumers pc
WHERE consumer_group = 'your-group';
```

## See Also

- [Consumer Groups Guide](../website/guide/consumer-groups.md) - Complete consumer groups guide
- [ENV_VARIABLES.md](../server/ENV_VARIABLES.md) - All environment variables
- [Example Code](../examples/22-subscription-modes.js) - Working examples
- [JavaScript Client README](../client-js/client-v2/README.md) - Client API reference

