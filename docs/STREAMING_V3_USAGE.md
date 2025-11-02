# Queen Streaming V3 - Usage Guide

## Quick Start

### 1. Define a Stream

```javascript
import { Queen } from './client-js/client-v2/Queen.js';

const queen = new Queen({ url: 'http://localhost:6632' });

await queen.stream('my-stream', 'production')
  .sources(['queue-1', 'queue-2'])  // Source queues
  .tumblingTime(60)                  // 60-second windows
  .gracePeriod(10)                   // 10-second grace for late messages
  .define();
```

### 2. Consume Windows

```javascript
const consumer = queen.consumer('my-stream', 'consumer-group-1');

consumer.process(async (window) => {
  console.log(`Window: ${window.start} to ${window.end}`);
  console.log(`Messages: ${window.allMessages.length}`);
  
  // Client-side aggregation
  const stats = window.aggregate({
    count: true,
    sum: ['payload.amount'],
    avg: ['payload.amount']
  });
  
  console.log(`Total: $${stats.sum['payload.amount']}`);
  console.log(`Average: $${stats.avg['payload.amount']}`);
  
  // Your processing logic here
  await processWindow(window);
});
```

## Understanding Watermarks

**Critical Concept:** Windows become ready when the **watermark** advances past `window_end + grace_period`.

The watermark tracks the **maximum message timestamp** seen across all source queues. It only advances when **new messages arrive**.

### Example Timeline

```
Time: 10:00:00 - Push 100 messages
Time: 10:00:05 - Watermark = 10:00:00 (last message time)
Time: 10:00:20 - Watermark still 10:00:00 (no new messages!)
Time: 10:00:30 - Push 1 message → Watermark = 10:00:30

Window [10:00:00 - 10:01:00] with 10s grace:
- Ready when: watermark > 10:01:10
- At 10:00:20: watermark = 10:00:00 → NOT READY ❌
- At 10:00:35: watermark = 10:00:30 → NOT READY ❌
- At 10:01:15: watermark = 10:01:11 → READY ✅
```

**For Testing:** Push messages **over time** to advance the watermark, or keep a background producer running.

## Configuration Options

### Stream Configuration

```javascript
await queen.stream('stream-name', 'namespace')
  .sources(['queue1', 'queue2'])     // Required: source queues
  .partitioned()                     // Optional: group by partition
  .tumblingTime(seconds)             // Required: window duration
  .gracePeriod(seconds)              // Optional: default 30s
  .leaseTimeout(seconds)             // Optional: default 60s
  .define();
```

**Partitioned vs Global:**
- **Global** (default): All partitions combined into one stream
- **Partitioned**: Each partition processed separately (parallel processing)

### Consumer Options

```javascript
const consumer = queen.consumer('stream-name', 'consumer-group');

// Configuration
consumer.pollTimeout = 30000;        // 30s long-poll timeout
consumer.leaseRenewInterval = 20000; // Renew every 20s
```

## Window API

### Filtering

```javascript
consumer.process(async (window) => {
  // Filter messages
  const clicks = window
    .filter(msg => msg.payload.type === 'click')
    .aggregate({ count: true });
  
  console.log(`Clicks: ${clicks.count}`);
});
```

### Grouping

```javascript
consumer.process(async (window) => {
  // Group by user
  const byUser = window.groupBy('payload.userId');
  
  for (const [userId, messages] of Object.entries(byUser)) {
    console.log(`User ${userId}: ${messages.length} events`);
  }
});
```

### Aggregations

```javascript
consumer.process(async (window) => {
  const stats = window.aggregate({
    count: true,                    // Count messages
    sum: ['payload.amount'],        // Sum field
    avg: ['payload.amount'],        // Average field
    min: ['payload.amount'],        // Minimum value
    max: ['payload.amount']         // Maximum value
  });
  
  console.log(stats);
  // {
  //   count: 100,
  //   sum: { 'payload.amount': 5000 },
  //   avg: { 'payload.amount': 50 },
  //   min: { 'payload.amount': 10 },
  //   max: { 'payload.amount': 200 }
  // }
});
```

### Chain Operations

```javascript
consumer.process(async (window) => {
  // Chain filter → aggregate
  const purchaseStats = window
    .filter(msg => msg.payload.type === 'purchase')
    .aggregate({ count: true, sum: ['payload.amount'] });
  
  // Reset to original messages
  const allStats = window
    .reset()  // Back to allMessages
    .aggregate({ count: true });
});
```

## Advanced Features

### Seeking

Rewind a consumer to a specific timestamp:

```javascript
const consumer = queen.consumer('my-stream', 'my-group');

// Seek to 1 hour ago
const oneHourAgo = new Date(Date.now() - 60 * 60 * 1000).toISOString();
await consumer.seek(oneHourAgo);

// Next poll will start from that timestamp
consumer.process(async (window) => {
  // Process historical windows
});
```

### Lease Renewal

Leases are **automatically renewed** by the consumer every 20 seconds (default). For long-running processing:

```javascript
consumer.leaseRenewInterval = 10000;  // Renew every 10s for shorter timeout

consumer.process(async (window) => {
  // Lease is automatically renewed in the background
  await longRunningProcess(window);  // Can take > 60s
});
```

### ACK/NACK

```javascript
consumer.process(async (window) => {
  try {
    await processWindow(window);
    // Auto-ACKed on success ✅
  } catch (err) {
    // Auto-NACKed on error ❌
    // Window will be re-delivered to another consumer
    throw err;
  }
});
```

## Production Patterns

### High Throughput

Use partitioned streams for parallel processing:

```javascript
// Define partitioned stream
await queen.stream('events', 'prod')
  .sources(['user-events'])
  .partitioned()              // Each partition processed separately
  .tumblingTime(60)
  .define();

// Run multiple consumers (they'll get different partitions)
// Consumer 1
consumer.process(async (window) => {
  console.log(`Processing partition: ${window.key}`);
  // ... handle window
});
```

### Exactly-Once Semantics

Use transaction IDs from window messages:

```javascript
consumer.process(async (window) => {
  const processedIds = await getProcessedIds();
  
  const newMessages = window.allMessages.filter(
    msg => !processedIds.has(msg.id)
  );
  
  for (const msg of newMessages) {
    await processMessage(msg);
    await markProcessed(msg.id);
  }
});
```

### Error Handling

```javascript
consumer.process(async (window) => {
  const results = {
    success: 0,
    failed: 0
  };
  
  for (const msg of window.allMessages) {
    try {
      await processMessage(msg);
      results.success++;
    } catch (err) {
      console.error(`Failed to process ${msg.id}:`, err);
      results.failed++;
    }
  }
  
  if (results.failed > 0) {
    // NACK the window - will be retried
    throw new Error(`${results.failed} messages failed`);
  }
  
  // ACK the window
  console.log(`Processed ${results.success} messages successfully`);
});
```

## Examples

- **`examples/17-streaming-simple.js`** - Simple working example (VERIFIED ✅)
- **`examples/16-streaming.js`** - Comprehensive example with multiple patterns

## Common Issues

### Windows Never Become Ready

**Symptom:** Consumers always get `204 No Content`

**Cause:** Watermark hasn't advanced past `window_end + grace_period`

**Solution:** Ensure messages continue to arrive to advance the watermark. For testing, push messages over time (see example 17).

### Duplicate Key Errors (Fixed ✅)

Old leases are now automatically cleaned up before creating new ones.

### Segmentation Faults (Fixed ✅)

All response handling now uses the ResponseQueue pattern - never touch `res` from thread pool.

## Performance Tips

1. **Batch size:** Larger windows = fewer polls but higher latency
2. **Grace period:** Longer grace = more complete windows but higher latency
3. **Partitioned streams:** Enable parallel processing across partitions
4. **Client-side filtering:** Reduce data transfer by filtering before aggregation

## Monitoring

Check stream status via database:

```sql
-- Active leases
SELECT s.name, l.consumer_group, l.window_start, l.window_end, l.lease_expires_at
FROM queen.stream_leases l
JOIN queen.streams s ON l.stream_id = s.id
WHERE l.lease_expires_at > NOW();

-- Consumer progress
SELECT s.name, o.consumer_group, o.stream_key, 
       o.last_acked_window_end, o.total_windows_consumed
FROM queen.stream_consumer_offsets o
JOIN queen.streams s ON o.stream_id = s.id
ORDER BY s.name, o.consumer_group;

-- Watermarks
SELECT queue_name, max_created_at, NOW() - max_created_at as age
FROM queen.queue_watermarks
ORDER BY queue_name;
```

---

**Version:** Queen Streaming V3  
**Status:** Production Ready ✅  
**Last Updated:** November 2, 2025

