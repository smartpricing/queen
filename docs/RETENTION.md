# Message Retention & Automatic Cleanup

## Overview

Queen provides **automatic background cleanup services** to prevent database bloat and maintain optimal performance. These services run periodically to delete old messages, inactive partitions, and stale metrics.

**Two background services handle cleanup:**

1. **RetentionService** - Cleans up old messages, partitions, and metrics
2. **EvictionService** - Evicts messages exceeding max wait time

---

## Table of Contents

- [What Gets Cleaned Up Automatically](#what-gets-cleaned-up-automatically)
- [Message Retention](#message-retention)
- [Message Eviction](#message-eviction)
- [Queue Configuration](#queue-configuration)
- [Background Services](#background-services)
- [Configuration Options](#configuration-options)
- [Monitoring](#monitoring)
- [Best Practices](#best-practices)
- [Troubleshooting](#troubleshooting)

---

## What Gets Cleaned Up Automatically

### ✅ Automatic (No Configuration Required)

| Data Type | Cleanup After | Configurable Via |
|-----------|---------------|------------------|
| **Inactive Partitions** | 7 days | `PARTITION_CLEANUP_DAYS` |
| **Old Metrics** | 90 days | `METRICS_RETENTION_DAYS` |
| **Retention History** | 90 days | `METRICS_RETENTION_DAYS` |

### ❌ Manual Configuration Required

| Data Type | Default Behavior | Requires |
|-----------|------------------|----------|
| **Messages** | Kept forever | `retentionEnabled: true` + `retentionSeconds` |
| **Completed Messages** | Kept forever | `retentionEnabled: true` + `completedRetentionSeconds` |
| **Expired Waiting Messages** | Kept forever | `maxWaitTimeSeconds > 0` |

⚠️ **Important:** Without retention configuration, your messages table will grow indefinitely!

---

## Message Retention

### How It Works

The RetentionService runs periodically (default: every 5 minutes) and deletes messages based on queue configuration:

1. **Unconsumed Messages** - Deleted after `retentionSeconds`
2. **Completed Messages** - Deleted after `completedRetentionSeconds` (only if already consumed)

### Enable Retention

```javascript
// JavaScript Client
await queen.createQueue('my-queue', {
  retentionEnabled: true,              // Must be true to enable cleanup
  retentionSeconds: 86400,             // Delete unconsumed messages after 1 day
  completedRetentionSeconds: 3600,     // Delete completed messages after 1 hour
});
```

```bash
# HTTP API
curl -X POST http://localhost:6632/api/v1/resources/queues \
  -H "Content-Type: application/json" \
  -d '{
    "queueName": "my-queue",
    "options": {
      "retentionEnabled": true,
      "retentionSeconds": 86400,
      "completedRetentionSeconds": 3600
    }
  }'
```

### Retention Types

#### 1. Unconsumed Message Retention

Deletes messages that have never been consumed and are older than `retentionSeconds`:

```sql
DELETE FROM queen.messages
WHERE id IN (
    SELECT m.id 
    FROM queen.messages m
    JOIN queen.partitions p ON m.partition_id = p.id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.retention_enabled = true
      AND q.retention_seconds > 0
      AND m.created_at < NOW() - (q.retention_seconds || ' seconds')::INTERVAL
    LIMIT 1000  -- Batch size
)
```

**Use cases:**
- Prevent unbounded queue growth
- Clean up messages that were never processed
- Time-sensitive data that becomes irrelevant

#### 2. Completed Message Retention

Deletes messages that have been consumed and are older than `completedRetentionSeconds`:

```sql
DELETE FROM queen.messages
WHERE id IN (
    SELECT DISTINCT m.id
    FROM queen.messages m
    JOIN queen.partition_consumers pc ON m.partition_id = pc.partition_id
    JOIN queen.queues q ON p.queue_id = q.id
    WHERE q.retention_enabled = true
      AND q.completed_retention_seconds > 0
      AND m.id <= pc.last_consumed_id  -- Only consumed messages
      AND m.created_at < NOW() - (q.completed_retention_seconds || ' seconds')::INTERVAL
    LIMIT 1000
)
```

**Use cases:**
- Keep audit trail for recent messages
- Allow reprocessing for a limited time
- Reduce storage costs while maintaining short-term history

### Example Configurations

#### Short-Term Processing Queue
```javascript
await queen.createQueue('realtime-events', {
  retentionEnabled: true,
  retentionSeconds: 3600,           // 1 hour
  completedRetentionSeconds: 600,   // 10 minutes
});
```

#### Standard Queue
```javascript
await queen.createQueue('tasks', {
  retentionEnabled: true,
  retentionSeconds: 86400,          // 1 day
  completedRetentionSeconds: 3600,  // 1 hour
});
```

#### Long-Term Archive
```javascript
await queen.createQueue('audit-log', {
  retentionEnabled: true,
  retentionSeconds: 2592000,        // 30 days
  completedRetentionSeconds: 604800, // 7 days
});
```

#### No Automatic Deletion (Manual Cleanup)
```javascript
await queen.createQueue('permanent-queue', {
  retentionEnabled: false,  // Messages kept forever
});
```

---

## Message Eviction

### How It Works

The EvictionService runs periodically (default: every 1 minute) and deletes messages that have been waiting too long to be consumed.

**This is useful for:**
- Time-sensitive messages that lose value over time
- Preventing stale message processing
- Queue overflow protection

### Enable Eviction

```javascript
await queen.createQueue('time-sensitive', {
  maxWaitTimeSeconds: 300,  // Evict messages waiting > 5 minutes
});
```

### SQL Query

```sql
DELETE FROM queen.messages
WHERE id IN (
    SELECT DISTINCT m.id
    FROM queen.messages m
    JOIN queen.queues q ON p.queue_id = q.id
    LEFT JOIN queen.partition_consumers pc ON m.partition_id = pc.partition_id
    WHERE q.max_wait_time_seconds > 0
      AND m.created_at < NOW() - (q.max_wait_time_seconds || ' seconds')::INTERVAL
      AND (pc.last_consumed_id IS NULL OR m.id > pc.last_consumed_id)  -- Only pending messages
    LIMIT 1000
)
```

### Example: Real-Time Notifications

```javascript
await queen.createQueue('notifications', {
  maxWaitTimeSeconds: 60,              // Evict after 1 minute
  retentionEnabled: true,
  completedRetentionSeconds: 300,      // Keep completed for 5 minutes
});
```

**Behavior:**
- Message pushed at 10:00:00
- Not consumed by 10:01:00
- Evicted by EvictionService
- Notification becomes irrelevant if not delivered quickly

---

## Queue Configuration

### Complete Configuration Example

```javascript
await queen.createQueue('production-queue', {
  // Retention settings
  retentionEnabled: true,
  retentionSeconds: 86400,             // 1 day for unconsumed
  completedRetentionSeconds: 3600,     // 1 hour for completed
  
  // Eviction settings
  maxWaitTimeSeconds: 600,             // Evict if waiting > 10 minutes
  
  // Other queue settings
  leaseTime: 300,                      // 5 minute lease
  retryLimit: 3,
  dlqAfterMaxRetries: true,
});
```

### Configuration Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `retentionEnabled` | boolean | `false` | Enable automatic message cleanup |
| `retentionSeconds` | integer | `0` | Delete unconsumed messages after N seconds |
| `completedRetentionSeconds` | integer | `0` | Delete completed messages after N seconds |
| `maxWaitTimeSeconds` | integer | `0` | Evict messages waiting longer than N seconds |

**Important:** `retentionEnabled` must be `true` for retention settings to work!

---

## Background Services

### RetentionService

**Runs every 5 minutes by default**

**Operations:**
1. Cleanup expired messages (unconsumed)
2. Cleanup completed messages
3. Cleanup inactive partitions
4. Cleanup old metrics

**Batch Processing:**
- Deletes up to 1000 messages per cycle (configurable)
- Multiple cycles run if more messages need deletion
- Short transactions prevent long table locks

**Startup Log:**
```
[info] [Worker 0] Starting background retention service...
[info] RetentionService started: interval=300000ms, batch_size=1000, 
       partition_cleanup_days=7, metrics_retention_days=90
```

**Activity Log:**
```
[info] RetentionService: Cleaned up expired_messages=523, completed_messages=1247, 
       inactive_partitions=2, old_metrics=45
```

### EvictionService

**Runs every 1 minute by default**

**Operations:**
1. Evict messages exceeding max_wait_time

**Batch Processing:**
- Evicts up to 1000 messages per cycle (configurable)

**Startup Log:**
```
[info] [Worker 0] Starting background eviction service...
[info] EvictionService started: interval=60000ms, batch_size=1000
```

**Activity Log:**
```
[info] EvictionService: Evicted 127 messages exceeding max_wait_time
```

### Thread Pool Usage

Both services use the **global system thread pool** for efficient resource management:

```
┌─────────────────────────────────────┐
│  Global System Thread Pool          │
│  (Shared by all system operations)  │
└────────────┬────────────────────────┘
             │
    ┌────────┴────────┐
    ▼                 ▼
┌──────────────┐  ┌──────────────┐
│ Retention    │  │ Eviction     │
│ Service      │  │ Service      │
└──────────────┘  └──────────────┘
```

---

## Configuration Options

### Environment Variables

All cleanup behavior can be configured via environment variables:

#### Retention Service

```bash
# How often to run cleanup (milliseconds)
export RETENTION_INTERVAL=300000        # Default: 5 minutes

# Max messages to delete per cycle
export RETENTION_BATCH_SIZE=1000        # Default: 1000

# Delete inactive partitions after N days
export PARTITION_CLEANUP_DAYS=7         # Default: 7 days

# Keep metrics for N days
export METRICS_RETENTION_DAYS=90        # Default: 90 days
```

#### Eviction Service

```bash
# How often to run eviction (milliseconds)
export EVICTION_INTERVAL=60000          # Default: 1 minute

# Max messages to evict per cycle
export EVICTION_BATCH_SIZE=1000         # Default: 1000
```

### Tuning for Different Workloads

#### High-Throughput (Many Messages)

```bash
# More aggressive cleanup
export RETENTION_INTERVAL=60000         # Every 1 minute
export RETENTION_BATCH_SIZE=5000        # Larger batches
export EVICTION_INTERVAL=30000          # Every 30 seconds
export EVICTION_BATCH_SIZE=2000
```

#### Low-Load (Fewer Messages)

```bash
# Gentler cleanup
export RETENTION_INTERVAL=600000        # Every 10 minutes
export RETENTION_BATCH_SIZE=500         # Smaller batches
export EVICTION_INTERVAL=120000         # Every 2 minutes
export EVICTION_BATCH_SIZE=500
```

#### Testing/Development

```bash
# Fast cleanup for testing
export RETENTION_INTERVAL=5000          # Every 5 seconds
export RETENTION_BATCH_SIZE=100
export EVICTION_INTERVAL=3000           # Every 3 seconds
export EVICTION_BATCH_SIZE=100
```

---

## Monitoring

### Check Retention History

```sql
-- View cleanup activity
SELECT 
    p.name as partition_name,
    q.name as queue_name,
    rh.messages_deleted,
    rh.retention_type,
    rh.executed_at
FROM queen.retention_history rh
JOIN queen.partitions p ON rh.partition_id = p.id
JOIN queen.queues q ON p.queue_id = q.id
ORDER BY rh.executed_at DESC
LIMIT 100;
```

**Example output:**
```
 partition_name | queue_name    | messages_deleted | retention_type              | executed_at
----------------|---------------|------------------|-----------------------------|--------------------------
 Default        | my-queue      | 1523             | completed_retention         | 2025-10-30 10:15:23.456
 Default        | my-queue      | 234              | max_wait_time_eviction      | 2025-10-30 10:14:12.123
 partition-1    | other-queue   | 45               | completed_retention         | 2025-10-30 10:10:45.789
```

### Cleanup Statistics

```sql
-- Total messages cleaned up by type
SELECT 
    retention_type,
    SUM(messages_deleted) as total_deleted,
    COUNT(*) as cleanup_runs,
    AVG(messages_deleted) as avg_per_run
FROM queen.retention_history
WHERE executed_at > NOW() - INTERVAL '7 days'
GROUP BY retention_type
ORDER BY total_deleted DESC;
```

### Queue Status

```sql
-- Check which queues have retention enabled
SELECT 
    name,
    retention_enabled,
    retention_seconds,
    completed_retention_seconds,
    max_wait_time_seconds,
    (SELECT COUNT(*) FROM queen.messages m 
     JOIN queen.partitions p ON m.partition_id = p.id 
     WHERE p.queue_id = q.id) as current_message_count
FROM queen.queues q
ORDER BY current_message_count DESC;
```

### Server Logs

```bash
# Watch cleanup activity
tail -f /var/log/queen/queen.log | grep -E "RetentionService|EvictionService"

# Check startup
grep "Starting background" /var/log/queen/queen.log

# View cleanup summary
grep "Cleaned up" /var/log/queen/queen.log
```

---

## Best Practices

### 1. Always Configure Retention for Production

❌ **Don't:**
```javascript
// Messages will accumulate forever!
await queen.createQueue('my-queue', {});
```

✅ **Do:**
```javascript
await queen.createQueue('my-queue', {
  retentionEnabled: true,
  retentionSeconds: 86400,
  completedRetentionSeconds: 3600,
});
```

### 2. Match Retention to Business Requirements

**Consider:**
- How long do messages remain relevant?
- Do you need an audit trail?
- What's the reprocessing window?
- Storage cost vs. retention duration

**Examples:**

```javascript
// Real-time events - short retention
await queen.createQueue('live-updates', {
  retentionEnabled: true,
  retentionSeconds: 3600,           // 1 hour
  completedRetentionSeconds: 300,   // 5 minutes
  maxWaitTimeSeconds: 60,           // Evict after 1 minute
});

// Batch processing - moderate retention
await queen.createQueue('nightly-jobs', {
  retentionEnabled: true,
  retentionSeconds: 86400,          // 1 day
  completedRetentionSeconds: 7200,  // 2 hours
});

// Compliance/audit - long retention
await queen.createQueue('audit-trail', {
  retentionEnabled: true,
  retentionSeconds: 2592000,        // 30 days
  completedRetentionSeconds: 2592000, // 30 days (keep completed too)
});
```

### 3. Monitor Database Growth

```sql
-- Check database size
SELECT pg_size_pretty(pg_database_size('your_database'));

-- Check table sizes
SELECT 
    schemaname,
    tablename,
    pg_size_pretty(pg_total_relation_size(schemaname||'.'||tablename)) AS size
FROM pg_tables
WHERE schemaname = 'queen'
ORDER BY pg_total_relation_size(schemaname||'.'||tablename) DESC;
```

### 4. Test Retention Settings

```javascript
// Use short retention for testing
const TEST_CONFIG = {
  retentionEnabled: true,
  retentionSeconds: 60,              // 1 minute
  completedRetentionSeconds: 30,     // 30 seconds
};

// Set fast cleanup interval
// RETENTION_INTERVAL=5000 (5 seconds)
```

### 5. Index for Performance

If cleanup becomes slow, add indexes:

```sql
-- Index for time-based cleanup (if not already present)
CREATE INDEX CONCURRENTLY idx_messages_created_at 
ON queen.messages(created_at);

-- Index for partition activity
CREATE INDEX CONCURRENTLY idx_partitions_last_activity 
ON queen.partitions(last_activity);
```

---

## Troubleshooting

### Messages Not Being Deleted

**Check queue configuration:**
```sql
SELECT name, retention_enabled, retention_seconds, completed_retention_seconds
FROM queen.queues
WHERE name = 'your-queue';
```

**Common issues:**
- ❌ `retention_enabled = false` - Must be `true`
- ❌ `retention_seconds = 0` - Must be greater than 0
- ❌ Messages not old enough yet
- ❌ Service not running (check logs)

### Cleanup Running Too Slowly

**Symptoms:**
- Messages accumulating faster than cleanup
- Large backlog of old messages

**Solutions:**

1. **Increase batch size:**
```bash
export RETENTION_BATCH_SIZE=5000
```

2. **Run more frequently:**
```bash
export RETENTION_INTERVAL=60000  # Every 1 minute
```

3. **Add database indexes** (see Best Practices)

4. **One-time manual cleanup:**
```sql
-- Delete old messages manually (be careful!)
DELETE FROM queen.messages
WHERE created_at < NOW() - INTERVAL '30 days'
  AND id IN (
    SELECT id FROM queen.messages
    ORDER BY created_at
    LIMIT 100000
  );
```

### Service Not Starting

**Check logs:**
```bash
grep "Worker 0" /var/log/queen/queen.log | grep "background"
```

**Possible causes:**
- Database not available at startup
- Not running on Worker 0
- Configuration loading failed

### High Database Load

**Symptoms:**
- Slow queries during cleanup
- Lock contention
- High CPU usage

**Solutions:**

1. **Reduce batch size:**
```bash
export RETENTION_BATCH_SIZE=100
```

2. **Increase interval:**
```bash
export RETENTION_INTERVAL=600000  # Every 10 minutes
```

3. **Run during off-peak hours** (requires custom scheduling)

---

## Performance Impact

### Database Load

**Per cleanup cycle (default settings):**
- 1-4 DELETE queries (depending on what needs cleanup)
- Each query limited to 1000 rows (batch size)
- Queries use existing indexes
- Row-level locks only (no table locks)
- Typical duration: < 100ms per cycle

### Resource Usage

**CPU:** Minimal (< 1% during cleanup)
**Memory:** Minimal (small batch processing)
**I/O:** Moderate during active cleanup, minimal otherwise

### Lock Contention

Batch deletions minimize lock time:
- Each cycle completes quickly (< 1 second)
- Row-level locks only
- No impact on reads
- Minimal impact on concurrent writes

---

## Migration Guide

### Enabling Retention on Existing Queues

If you have existing queues without retention, you can update them:

```javascript
// Update existing queue configuration
await queen.createQueue('existing-queue', {
  retentionEnabled: true,
  retentionSeconds: 86400,
  completedRetentionSeconds: 3600,
  // ... other existing settings ...
});
```

**Note:** Existing messages will be subject to the new retention rules on the next cleanup cycle.

### Testing Before Rollout

1. **Test on staging first:**
```bash
# Use aggressive settings to verify behavior
export RETENTION_INTERVAL=10000  # 10 seconds
export RETENTION_BATCH_SIZE=10
```

2. **Monitor retention history:**
```sql
SELECT * FROM queen.retention_history 
ORDER BY executed_at DESC LIMIT 10;
```

3. **Verify expected behavior:**
- Push test messages
- Wait for retention period + cleanup cycle
- Confirm messages are deleted

4. **Roll out to production gradually:**
```javascript
// Start with long retention
retentionSeconds: 604800,  // 7 days

// Then reduce as needed
retentionSeconds: 86400,   // 1 day
```

---

## Summary

✅ **Automatic cleanup prevents database bloat**  
✅ **Configurable per-queue retention policies**  
✅ **Efficient batch processing**  
✅ **Multiple cleanup strategies (retention, eviction, partitions, metrics)**  
✅ **Observable via logs and retention_history**  
✅ **Production-ready with sensible defaults**  

**Remember:** Always configure retention for production queues to prevent unbounded database growth!

For more details on background job implementation, see [BACKGROUND_JOBS.md](../server/BACKGROUND_JOBS.md).

