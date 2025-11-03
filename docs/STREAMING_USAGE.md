# Queen Streaming - Usage Guide

## Overview

Queen Streaming provides **time-based windowing** over your message queues, enabling powerful stream processing patterns like aggregations, joins, and event-time processing with exactly-once semantics.

---

## Table of Contents

1. [Core Concepts](#core-concepts)
2. [Quick Start](#quick-start)
3. [Stream Types](#stream-types)
4. [Window Configuration](#window-configuration)
5. [Consumer Groups](#consumer-groups)
6. [Common Patterns](#common-patterns)
7. [API Reference](#api-reference)
8. [Best Practices](#best-practices)

---

## Core Concepts

### What is a Stream?

A **stream** is a time-ordered view over one or more message queues, divided into **windows** for processing.

```
Queue: user-events
├─ Message 1 (10:00:05)
├─ Message 2 (10:00:12)
├─ Message 3 (10:00:58)
└─ Message 4 (10:01:02)

Stream: user-activity (1-minute windows)
├─ Window 1 [10:00:00 - 10:01:00] → Messages 1, 2, 3
└─ Window 2 [10:01:00 - 10:02:00] → Message 4
```

### Key Components

**Stream Definition**
- **Name**: Unique identifier for the stream
- **Namespace**: Logical grouping
- **Source Queues**: One or more queues to consume from
- **Window Type**: How to divide time (tumbling, sliding, session)
- **Window Duration**: Size of each window
- **Grace Period**: How long to wait for late-arriving data
- **Partitioned**: Whether to process partitions separately

**Consumer Group**
- Multiple consumers can process the same stream
- Each group maintains independent progress
- Load balancing within a group
- At-least-once to exactly-once semantics

**Window**
- A batch of messages within a time range
- Includes start time, end time, and messages
- Has a lease for processing
- Must be acknowledged when done

---

## Quick Start

### 1. Create a Stream

```javascript
import { Queen } from 'queen-client';

const queen = new Queen({ url: 'http://localhost:6632' });

// Define a 1-minute tumbling window stream
await queen.stream('user-activity-stream', 'analytics')
  .sources(['user-events', 'page-views'])
  .tumblingTime(60)  // 60 seconds
  .gracePeriod(10)   // 10 seconds for late data
  .define();
```

### 2. Consume Windows

```javascript
const consumer = queen.consumer('user-activity-stream', 'analytics-processor');

await consumer.process(async (window) => {
  console.log(`Processing window: ${window.start} - ${window.end}`);
  console.log(`Messages in window: ${window.allMessages.length}`);
  
  // Process messages
  for (const msg of window.allMessages) {
    console.log(msg.data);
  }
  
  // Window is automatically acknowledged on success
});
```

---

## Stream Types

### Global Streams

Process all messages from all source queues together, regardless of partition.

**Use Case**: Aggregate metrics across all users, system-wide monitoring

```javascript
// Example: System-wide analytics
await queen.stream('system-metrics', 'monitoring')
  .sources(['api-logs', 'app-logs', 'db-logs'])
  .tumblingTime(300)  // 5-minute windows
  .gracePeriod(30)
  .define();

// Consumer sees all messages together
await consumer.process(async (window) => {
  const totalRequests = window.allMessages.length;
  const errorCount = window.allMessages.filter(m => m.data.error).length;
  
  console.log(`Total: ${totalRequests}, Errors: ${errorCount}`);
});
```

### Partitioned Streams

Process each partition independently. **Partitions with the same NAME across different source queues are grouped together.**

**Use Case**: Per-user analytics, per-session processing, isolated workflows

```javascript
// Example: Per-chat conversation processing
await queen.stream('chat-stream', 'chat')
  .sources(['chat-messages', 'chat-reactions', 'chat-edits'])
  .partitioned()       // Process each chatId separately
  .tumblingTime(60)    // 1-minute windows per chat
  .gracePeriod(10)
  .define();

// Producer: Push to partition by chatId
await queen.queue('chat-messages')
  .partition('chat-123')
  .push({ user: 'alice', text: 'Hello!' });

await queen.queue('chat-reactions')
  .partition('chat-123')
  .push({ user: 'bob', reaction: '👍' });

// Consumer: Gets ONE window with BOTH messages for chat-123
await consumer.process(async (window) => {
  console.log(`Chat: ${window.key}`);  // "chat-123"
  console.log(`Messages: ${window.allMessages.length}`);  // 2
  
  // Group by message type if needed
  const messages = window.allMessages.filter(m => m.data.text);
  const reactions = window.allMessages.filter(m => m.data.reaction);
});
```

**Key Insight**: Partitioned streams group by **partition name**, so:
- `test-chat-messages` partition "chat-123" (UUID: xxx-111)
- `test-chat-reactions` partition "chat-123" (UUID: yyy-222)

→ Both go into the **same window** because they have the same partition **name**: "chat-123"

---

## Window Configuration

### Tumbling Windows

Non-overlapping, fixed-size windows.

```javascript
await queen.stream('events-stream', 'analytics')
  .sources(['events'])
  .tumblingTime(60)  // 60-second windows
  .gracePeriod(10)   // Wait 10s for late data
  .define();
```

**Timeline:**
```
10:00:00 - 10:01:00  Window 1
10:01:00 - 10:02:00  Window 2
10:02:00 - 10:03:00  Window 3
```

### Window Parameters

**Duration** (`tumblingTime`)
- Time in seconds for each window
- Messages are grouped by their `created_at` timestamp

**Grace Period** (`gracePeriod`)
- How long to wait after window end for late-arriving messages
- Window becomes "ready" when: `current_time >= window_end + grace_period`

**Lease Timeout** (`leaseTimeout`)
- How long a consumer has to process a window
- Automatically renews if processing takes longer

```javascript
await queen.stream('my-stream', 'ns')
  .sources(['queue1'])
  .tumblingTime(300)      // 5-minute windows
  .gracePeriod(60)        // Wait 1 minute for late data
  .leaseTimeout(120)      // 2-minute processing timeout
  .define();
```

---

## Consumer Groups

Multiple consumer groups can independently process the same stream.

### Single Consumer Group

```javascript
// Consumer group "processor-1"
const consumer1 = queen.consumer('events-stream', 'processor-1');

await consumer1.process(async (window) => {
  // Save to database
  await db.saveAnalytics(window.allMessages);
});
```

### Multiple Consumer Groups

```javascript
// Group 1: Real-time analytics
const analytics = queen.consumer('events-stream', 'analytics');
await analytics.process(async (window) => {
  await updateDashboard(window.allMessages);
});

// Group 2: Archive to S3
const archiver = queen.consumer('events-stream', 'archiver');
await archiver.process(async (window) => {
  await s3.upload(window.allMessages);
});

// Both groups process the SAME windows independently
```

### Load Balancing

Multiple consumers in the same group share the workload:

```javascript
// Worker 1
const consumer1 = queen.consumer('events-stream', 'processors');
await consumer1.process(processWindow);

// Worker 2
const consumer2 = queen.consumer('events-stream', 'processors');
await consumer2.process(processWindow);

// Workers share windows - each window goes to ONE worker
```

---

## Common Patterns

### 1. Event-Time Aggregation

Aggregate metrics over time windows:

```javascript
await queen.stream('metrics-stream', 'monitoring')
  .sources(['api-metrics'])
  .tumblingTime(60)  // 1-minute aggregations
  .define();

const consumer = queen.consumer('metrics-stream', 'aggregator');

await consumer.process(async (window) => {
  const metrics = {
    timestamp: window.start,
    totalRequests: window.allMessages.length,
    avgResponseTime: calculateAvg(window.allMessages, 'data.responseTime'),
    errorRate: window.allMessages.filter(m => m.data.status >= 500).length / window.allMessages.length,
    p95ResponseTime: calculateP95(window.allMessages, 'data.responseTime')
  };
  
  await saveMetrics(metrics);
});
```

### 2. Stream Join (Multiple Queues)

Join related events from different queues:

```javascript
// Setup: Two queues with partitions by userId
await queen.queue('user-clicks').namespace('analytics').create();
await queen.queue('user-purchases').namespace('analytics').create();

// Stream joining both queues
await queen.stream('user-behavior', 'analytics')
  .sources(['user-clicks', 'user-purchases'])
  .partitioned()         // Process each user separately
  .tumblingTime(3600)    // 1-hour windows per user
  .gracePeriod(300)      // 5-minute grace period
  .define();

// Producer: Push events by userId
await queen.queue('user-clicks')
  .partition('user-123')
  .push({ page: '/product', timestamp: Date.now() });

await queen.queue('user-purchases')
  .partition('user-123')
  .push({ product: 'ABC', amount: 99.99 });

// Consumer: Process combined events per user
const consumer = queen.consumer('user-behavior', 'behavior-analyzer');

await consumer.process(async (window) => {
  // Window contains events from BOTH queues for this user
  const clicks = window.allMessages.filter(m => m.data.page);
  const purchases = window.allMessages.filter(m => m.data.product);
  
  const analysis = {
    userId: window.key,
    period: { start: window.start, end: window.end },
    clicks: clicks.length,
    purchases: purchases.length,
    conversionRate: purchases.length / clicks.length,
    revenue: purchases.reduce((sum, p) => sum + p.data.amount, 0)
  };
  
  await saveAnalysis(analysis);
});
```

### 3. Sessionization

Group events into sessions with gaps:

```javascript
// For now, use tumbling windows with appropriate duration
await queen.stream('user-sessions', 'analytics')
  .sources(['user-events'])
  .partitioned()         // Per user
  .tumblingTime(1800)    // 30-minute windows
  .gracePeriod(300)
  .define();

const consumer = queen.consumer('user-sessions', 'session-processor');

await consumer.process(async (window) => {
  // Detect session boundaries within window
  const sessions = detectSessions(window.allMessages, 300); // 5-min inactivity gap
  
  for (const session of sessions) {
    await saveSession({
      userId: window.key,
      sessionId: session.id,
      events: session.events,
      duration: session.duration
    });
  }
});
```

### 4. Real-Time Pipeline with Transactional Guarantees

Process a stream and push results to another queue atomically:

```javascript
// Stream 1: Aggregate raw events
await queen.stream('raw-events', 'pipeline')
  .sources(['sensor-data'])
  .tumblingTime(10)  // 10-second windows
  .define();

const consumer = queen.consumer('raw-events', 'processor');

await consumer.process(async (window) => {
  // Calculate aggregates
  const avg = calculateAvg(window.allMessages, 'data.value');
  const max = Math.max(...window.allMessages.map(m => m.data.value));
  
  // Push to next queue
  await queen.queue('processed-metrics').push({
    timestamp: window.start,
    avg,
    max,
    count: window.allMessages.length
  });
  
  // Window auto-acknowledged on success
});

// Stream 2: Further processing
await queen.stream('processed-metrics-stream', 'pipeline')
  .sources(['processed-metrics'])
  .tumblingTime(60)
  .define();
```

### 5. Late Data Handling

Handle late-arriving data with grace periods:

```javascript
await queen.stream('iot-data', 'sensors')
  .sources(['sensor-readings'])
  .tumblingTime(60)     // 1-minute windows
  .gracePeriod(30)      // Wait 30s for late data
  .define();

// Timeline example:
// 10:00:00 - 10:01:00  Window period
// 10:01:00 - 10:01:30  Grace period (accept late data)
// 10:01:30+            Window becomes available for processing

const consumer = queen.consumer('iot-data', 'processor');

await consumer.process(async (window) => {
  // Window includes all messages that arrived during grace period
  console.log(`Window ${window.start} - ${window.end}`);
  console.log(`Messages: ${window.allMessages.length} (includes late arrivals)`);
});
```

### 6. Multi-Stage Processing Pipeline

Chain multiple streams for complex processing:

```javascript
// Stage 1: Raw event ingestion
await queen.stream('raw-clicks', 'analytics')
  .sources(['click-events'])
  .tumblingTime(10)
  .define();

const stage1 = queen.consumer('raw-clicks', 'enricher');
await stage1.process(async (window) => {
  for (const click of window.allMessages) {
    // Enrich with user data
    const enriched = await enrichClickEvent(click.data);
    await queen.queue('enriched-clicks').push(enriched);
  }
});

// Stage 2: Enriched data aggregation
await queen.stream('enriched-clicks-stream', 'analytics')
  .sources(['enriched-clicks'])
  .tumblingTime(60)
  .define();

const stage2 = queen.consumer('enriched-clicks-stream', 'aggregator');
await stage2.process(async (window) => {
  const metrics = aggregateMetrics(window.allMessages);
  await queen.queue('final-metrics').push(metrics);
});
```

---

## API Reference

### Defining a Stream

```javascript
// Global stream (all partitions together)
await queen.stream(streamName, namespace)
  .sources([queue1, queue2, ...])
  .tumblingTime(seconds)
  .gracePeriod(seconds)     // Optional, default: 30s
  .leaseTimeout(seconds)    // Optional, default: 60s
  .define();

// Partitioned stream (each partition separately)
await queen.stream(streamName, namespace)
  .sources([queue1, queue2, ...])
  .partitioned()
  .tumblingTime(seconds)
  .gracePeriod(seconds)
  .leaseTimeout(seconds)
  .define();
```

### Creating a Consumer

```javascript
const consumer = queen.consumer(streamName, consumerGroup);

await consumer.process(async (window) => {
  // Process window
  // Automatically acknowledged on success
  // Automatically retried on failure (lease expires)
});
```

### Window Object

```javascript
window = {
  id: 'stream-id:partition:start:end',  // Unique window identifier
  leaseId: 'uuid',                       // Lease for this processing attempt
  key: 'partition-name' or '__GLOBAL__', // Partition name or global
  start: '2025-11-03T10:00:00+00',       // Window start time
  end: '2025-11-03T10:01:00+00',         // Window end time
  allMessages: [...],                    // Array of messages in window
  
  // Helper methods
  groupBy(field),                        // Group messages by field
  ack(success),                          // Manual acknowledgment
  renew(extendMs)                        // Renew lease if processing is slow
}
```

### Window Helper Methods

```javascript
await consumer.process(async (window) => {
  // Group messages by a field
  const byUser = window.groupBy('data.userId');
  // Returns: { 'user-1': [msg1, msg2], 'user-2': [msg3] }
  
  // Manual acknowledgment
  try {
    await processWindow(window);
    await window.ack(true);  // Success
  } catch (error) {
    await window.ack(false); // Failure - window will be retried
  }
  
  // Renew lease for long processing
  await window.renew(60000); // Extend by 60 seconds
});
```

### Seeking to Timestamp

Reset consumer group progress to a specific time:

```javascript
// Seek to specific timestamp
await queen.stream('my-stream', 'namespace')
  .seek('consumer-group', '2025-11-03T10:00:00+00');

// Consumer will start processing from that timestamp
```

---

## Common Patterns

### Pattern 1: Real-Time Analytics Dashboard

```javascript
// Stream: 1-minute tumbling windows
await queen.stream('page-views-stream', 'analytics')
  .sources(['page-views'])
  .tumblingTime(60)
  .gracePeriod(10)
  .define();

const consumer = queen.consumer('page-views-stream', 'dashboard-updater');

await consumer.process(async (window) => {
  const metrics = {
    timestamp: window.start,
    totalViews: window.allMessages.length,
    uniqueUsers: new Set(window.allMessages.map(m => m.data.userId)).size,
    topPages: getTopN(window.allMessages, 'data.page', 10),
    avgDuration: calculateAvg(window.allMessages, 'data.duration')
  };
  
  await updateDashboard(metrics);
  await redis.set('latest-metrics', JSON.stringify(metrics));
});
```

### Pattern 2: Multi-Step Workflow with Stream Join

```javascript
// Queues: order-created, payment-processed, shipment-created
// Goal: Join events by orderId within 1-hour window

await queen.stream('order-lifecycle', 'orders')
  .sources(['order-created', 'payment-processed', 'shipment-created'])
  .partitioned()        // Group by orderId
  .tumblingTime(3600)   // 1-hour windows
  .gracePeriod(300)     // 5-minute grace
  .define();

const consumer = queen.consumer('order-lifecycle', 'order-processor');

await consumer.process(async (window) => {
  const orderId = window.key;
  
  // Find events
  const created = window.allMessages.find(m => m.data.event === 'order-created');
  const paid = window.allMessages.find(m => m.data.event === 'payment-processed');
  const shipped = window.allMessages.find(m => m.data.event === 'shipment-created');
  
  // Process based on completion
  if (created && paid && shipped) {
    console.log(`✅ Order ${orderId} complete!`);
    await notifyCustomer(orderId, 'shipped');
  } else if (created && paid) {
    console.log(`⏳ Order ${orderId} awaiting shipment`);
  } else if (created && !paid) {
    console.log(`⚠️  Order ${orderId} payment pending`);
    await sendPaymentReminder(orderId);
  }
});
```

### Pattern 3: Fraud Detection with Time Windows

```javascript
await queen.stream('transaction-stream', 'fraud-detection')
  .sources(['transactions'])
  .partitioned()        // Per user
  .tumblingTime(300)    // 5-minute windows
  .gracePeriod(30)
  .define();

const consumer = queen.consumer('transaction-stream', 'fraud-detector');

await consumer.process(async (window) => {
  const userId = window.key;
  const transactions = window.allMessages;
  
  // Fraud rules
  const totalAmount = transactions.reduce((sum, t) => sum + t.data.amount, 0);
  const uniqueLocations = new Set(transactions.map(t => t.data.location)).size;
  const transactionCount = transactions.length;
  
  if (totalAmount > 10000 || uniqueLocations > 5 || transactionCount > 20) {
    await queen.queue('fraud-alerts').push({
      userId,
      period: { start: window.start, end: window.end },
      totalAmount,
      transactionCount,
      uniqueLocations,
      severity: 'high'
    });
  }
});
```

### Pattern 4: ETL Pipeline with Exactly-Once Semantics

```javascript
// Extract: Stream from source
await queen.stream('raw-logs', 'etl')
  .sources(['application-logs'])
  .tumblingTime(600)  // 10-minute batches
  .define();

const etl = queen.consumer('raw-logs', 'etl-processor');

await etl.process(async (window) => {
  // Transform
  const transformed = window.allMessages.map(msg => ({
    timestamp: msg.created_at,
    level: msg.data.level,
    message: msg.data.message,
    metadata: extractMetadata(msg.data)
  }));
  
  // Load to data warehouse (exactly-once)
  await dataWarehouse.bulkInsert('logs', transformed);
  
  // Window auto-acked = exactly-once delivery
});
```

### Pattern 5: Chat Room Aggregation

```javascript
// Queues partitioned by chatId
await queen.queue('chat-messages').namespace('chat').create();
await queen.queue('chat-reactions').namespace('chat').create();
await queen.queue('chat-edits').namespace('chat').create();

// Stream: 5-second windows per chat room
await queen.stream('chat-activity', 'chat')
  .sources(['chat-messages', 'chat-reactions', 'chat-edits'])
  .partitioned()      // Each chatId separately
  .tumblingTime(5)
  .gracePeriod(1)
  .define();

// Producer
const chatId = 'room-42';

await queen.queue('chat-messages')
  .partition(chatId)
  .push({ user: 'alice', text: 'Hello!' });

await queen.queue('chat-reactions')
  .partition(chatId)
  .push({ user: 'bob', emoji: '👍', messageId: 'msg-1' });

// Consumer: Get all chat activity in 5-second windows
const consumer = queen.consumer('chat-activity', 'chat-processor');

await consumer.process(async (window) => {
  console.log(`Chat room: ${window.key}`);
  
  // Group by activity type
  const grouped = window.groupBy('data.user');
  
  // All messages from all 3 queues for this chatId in this window
  const messages = window.allMessages.filter(m => m.data.text);
  const reactions = window.allMessages.filter(m => m.data.emoji);
  const edits = window.allMessages.filter(m => m.data.editedText);
  
  console.log(`Messages: ${messages.length}, Reactions: ${reactions.length}, Edits: ${edits.length}`);
  
  // Update chat summary
  await updateChatSummary(window.key, {
    period: { start: window.start, end: window.end },
    messages,
    reactions,
    edits,
    activeUsers: new Set(window.allMessages.map(m => m.data.user)).size
  });
});
```

### Pattern 6: Sensor Data with Late Arrivals

```javascript
await queen.stream('sensor-readings', 'iot')
  .sources(['temperature-sensors', 'humidity-sensors'])
  .tumblingTime(60)     // 1-minute windows
  .gracePeriod(30)      // 30s grace for network delays
  .define();

const consumer = queen.consumer('sensor-readings', 'iot-processor');

await consumer.process(async (window) => {
  // Calculate environmental metrics
  const temps = window.allMessages
    .filter(m => m.data.type === 'temperature')
    .map(m => m.data.value);
  
  const humidity = window.allMessages
    .filter(m => m.data.type === 'humidity')
    .map(m => m.data.value);
  
  const metrics = {
    timestamp: window.start,
    avgTemp: average(temps),
    minTemp: Math.min(...temps),
    maxTemp: Math.max(...temps),
    avgHumidity: average(humidity),
    alertLevel: determineAlertLevel(temps, humidity)
  };
  
  await saveMetrics(metrics);
  
  if (metrics.alertLevel === 'critical') {
    await sendAlert(metrics);
  }
});
```

---

## Best Practices

### 1. Choose the Right Window Size

**Small windows (5-60 seconds)**
- Real-time dashboards
- Fraud detection
- Immediate alerts
- Higher processing frequency

**Medium windows (1-15 minutes)**
- Analytics aggregation
- Batch processing
- Resource efficiency balance

**Large windows (1+ hours)**
- ETL jobs
- Daily reports
- Historical analysis
- Lower processing overhead

### 2. Set Appropriate Grace Periods

```javascript
// Network-based sources (APIs, sensors)
.gracePeriod(30)  // 30s to account for network delays

// Database CDC
.gracePeriod(10)  // 10s, usually reliable

// Internal queues
.gracePeriod(5)   // 5s, minimal delay
```

### 3. Handle Long Processing

For windows that take longer than the lease timeout:

```javascript
await consumer.process(async (window) => {
  const startTime = Date.now();
  
  for (const message of window.allMessages) {
    await processMessage(message);
    
    // Renew lease if getting close to timeout
    if (Date.now() - startTime > 50000) {  // 50 seconds
      await window.renew(60000);  // Extend by 60s
      startTime = Date.now();
    }
  }
});
```

### 4. Error Handling

```javascript
await consumer.process(async (window) => {
  try {
    await processWindow(window);
    // Auto-acked on success
  } catch (error) {
    console.error(`Failed to process window ${window.id}:`, error);
    
    // Option 1: Manual NACK (window will be retried)
    await window.ack(false);
    
    // Option 2: Send to DLQ and ACK
    await queen.queue('failed-windows').push({
      windowId: window.id,
      error: error.message,
      messages: window.allMessages
    });
    await window.ack(true);
    
    // Option 3: Let it throw (lease expires, auto-retry)
    throw error;
  }
});
```

### 5. Idempotent Processing

Ensure your processing is idempotent for exactly-once semantics:

```javascript
await consumer.process(async (window) => {
  const windowId = window.id;
  
  // Check if already processed
  if (await db.isWindowProcessed(windowId)) {
    console.log(`Window ${windowId} already processed, skipping`);
    return;  // Auto-ack
  }
  
  // Process
  const result = await processWindow(window);
  
  // Save with window ID as deduplication key
  await db.saveResults(windowId, result);
});
```

### 6. Monitoring and Metrics

```javascript
const consumer = queen.consumer('my-stream', 'my-group');

let windowsProcessed = 0;
let messagesProcessed = 0;
let errors = 0;

await consumer.process(async (window) => {
  const startTime = Date.now();
  
  try {
    await processWindow(window);
    
    windowsProcessed++;
    messagesProcessed += window.allMessages.length;
    
    const duration = Date.now() - startTime;
    console.log(`✅ Window processed in ${duration}ms (${window.allMessages.length} messages)`);
    
  } catch (error) {
    errors++;
    console.error(`❌ Window failed:`, error);
    throw error;
  }
});

// Periodic metrics logging
setInterval(() => {
  console.log(`Stats: ${windowsProcessed} windows, ${messagesProcessed} messages, ${errors} errors`);
}, 60000);
```

---

## Advanced Examples

### Example: Real-Time Chat Translation Pipeline

Complete end-to-end example with your use case:

```javascript
import { Queen } from 'queen-client';
const queen = new Queen({ url: 'http://localhost:6632' });

// 1. Setup queues
await queen.queue('chat-messages').namespace('chat').create();
await queen.queue('chat-translations').namespace('chat').create();

// 2. Define stream (partitioned by chatId)
await queen.stream('chat-complete', 'chat')
  .sources(['chat-messages', 'chat-translations'])
  .partitioned()      // Each chat separately
  .tumblingTime(5)    // 5-second windows
  .gracePeriod(1)     // 1-second grace
  .define();

// 3. Producer: Original messages
setInterval(async () => {
  const chatId = selectRandomChat(['chat-1', 'chat-2', 'chat-3']);
  
  await queen.queue('chat-messages')
    .partition(chatId)
    .push({
      user: 'alice',
      text: 'Hello, world!',
      timestamp: Date.now()
    });
}, 1000);

// 4. Translator service: Consumes messages and produces translations
const translator = queen.queue('chat-messages')
  .autoAck(false)
  .each()
  .consume(async (message) => {
    // Translate
    const translated = await translate(message.data.text, 'es');
    
    // Push translation + ACK original (atomic)
    await queen.transaction()
      .ack(message)
      .queue('chat-translations')
      .partition(message.partition)  // Same partition name!
      .push([{
        originalText: message.data.text,
        translatedText: translated,
        language: 'es'
      }])
      .commit();
  });

// 5. Stream consumer: Gets BOTH original and translation
const consumer = queen.consumer('chat-complete', 'chat-aggregator');

await consumer.process(async (window) => {
  const chatId = window.key;
  
  // Messages from BOTH queues in same window
  const originals = window.allMessages.filter(m => m.data.text && !m.data.originalText);
  const translations = window.allMessages.filter(m => m.data.translatedText);
  
  console.log(`Chat ${chatId}:`);
  console.log(`  Original messages: ${originals.length}`);
  console.log(`  Translations: ${translations.length}`);
  
  // Save complete conversation snapshot
  await saveConversationWindow({
    chatId,
    period: { start: window.start, end: window.end },
    originals,
    translations
  });
});
```

### Example: IoT Data Aggregation by Device

```javascript
// Queues: device-temperature, device-pressure, device-status
await queen.stream('device-telemetry', 'iot')
  .sources(['device-temperature', 'device-pressure', 'device-status'])
  .partitioned()         // Each deviceId separately
  .tumblingTime(60)      // 1-minute aggregations per device
  .gracePeriod(15)
  .define();

// Producer: Multiple sensors per device
await queen.queue('device-temperature')
  .partition('device-001')
  .push({ value: 72.5, unit: 'F' });

await queen.queue('device-pressure')
  .partition('device-001')
  .push({ value: 14.7, unit: 'psi' });

// Consumer: Aggregate all sensor data per device
const consumer = queen.consumer('device-telemetry', 'aggregator');

await consumer.process(async (window) => {
  const deviceId = window.key;
  
  // Extract different sensor types
  const temps = window.allMessages
    .filter(m => m.data.unit === 'F')
    .map(m => m.data.value);
  
  const pressures = window.allMessages
    .filter(m => m.data.unit === 'psi')
    .map(m => m.data.value);
  
  // Create comprehensive device snapshot
  const snapshot = {
    deviceId,
    timestamp: window.start,
    temperature: {
      avg: average(temps),
      min: Math.min(...temps),
      max: Math.max(...temps),
      readings: temps.length
    },
    pressure: {
      avg: average(pressures),
      min: Math.min(...pressures),
      max: Math.max(...pressures),
      readings: pressures.length
    },
    health: determineDeviceHealth(temps, pressures)
  };
  
  await saveDeviceSnapshot(snapshot);
  
  if (snapshot.health === 'critical') {
    await alertOps(deviceId, snapshot);
  }
});
```

---

## Troubleshooting

### Windows Not Appearing

**Problem**: Consumer polls but doesn't receive windows

**Solutions:**
1. Check grace period - window won't appear until `current_time >= window_end + grace_period`
2. Verify messages exist in source queues
3. Check watermarks are updating (triggered by message inserts)
4. Ensure source queues are correctly linked to stream

### Duplicate Processing

**Problem**: Same window processed multiple times

**Solutions:**
1. Implement idempotent processing with window ID tracking
2. Check lease timeout is appropriate for processing time
3. Use `window.renew()` for long-running operations
4. Verify ACK is being called (auto or manual)

### Messages Missing from Windows

**Problem**: Messages exist but don't appear in windows

**Solutions:**
1. Check message timestamps fall within window boundaries
2. Verify grace period hasn't expired before messages arrived
3. For partitioned streams, ensure partition **names** match across queues
4. Check stream source configuration

### Consumer Group Not Progressing

**Problem**: Consumer keeps getting same windows

**Solutions:**
1. Ensure windows are being acknowledged (auto or manual)
2. Check for errors in processing that prevent ACK
3. Verify lease isn't expiring due to slow processing
4. Use `seek()` to reset progress if needed

---

## Performance Tips

### 1. Batch Size Optimization

```javascript
// Larger windows = fewer processing operations
.tumblingTime(300)  // 5 minutes instead of 10 seconds

// But balance with latency requirements
```

### 2. Parallel Processing

```javascript
// Scale horizontally with multiple consumers
for (let i = 0; i < 4; i++) {
  const consumer = queen.consumer('my-stream', 'processors');
  await consumer.process(processWindow);
}
```

### 3. Efficient Message Processing

```javascript
await consumer.process(async (window) => {
  // Batch database operations
  const batch = window.allMessages.map(transformMessage);
  await db.bulkInsert(batch);  // Single DB call
  
  // vs. individual inserts (slow)
  // for (const msg of window.allMessages) {
  //   await db.insert(msg);  // Many DB calls
  // }
});
```

---

## Migration from Regular Queues

### Before: Traditional Consumer

```javascript
await queen.queue('events')
  .autoAck(false)
  .batch(100)
  .consume(async (messages) => {
    for (const msg of messages) {
      await process(msg);
    }
    await messages.ack();
  });
```

### After: Stream Consumer

```javascript
await queen.stream('events-stream', 'ns')
  .sources(['events'])
  .tumblingTime(10)  // 10-second windows
  .define();

const consumer = queen.consumer('events-stream', 'processor');

await consumer.process(async (window) => {
  for (const msg of window.allMessages) {
    await process(msg);
  }
  // Auto-acked
});
```

**Benefits:**
- Time-based grouping (not just count-based)
- Exactly-once processing per window
- Automatic lease management
- Support for late-arriving data
- Built-in watermark tracking

---

## Summary

Queen Streaming provides powerful capabilities for:

✅ **Time-based windowing** - Process messages in temporal batches  
✅ **Multi-queue joins** - Combine related events from different sources  
✅ **Partitioned processing** - Isolated processing per entity (user, device, etc.)  
✅ **Exactly-once semantics** - Guaranteed processing with window-level ACKs  
✅ **Late data handling** - Grace periods for network delays  
✅ **Scalable consumers** - Load balancing across multiple workers  
✅ **Progress tracking** - Resume from last processed window  

Perfect for analytics pipelines, real-time dashboards, ETL workflows, fraud detection, IoT data processing, and multi-stage event processing!

---

## Next Steps

1. Try the examples in `examples/16-streaming.js`, `17-streaming-simple.js`, `18-streaming.js`, `19-streaming-partitioned.js`
2. Check the web UI at `/streams` to monitor your streams
3. View consumer progress and active windows in the dashboard
4. Read `STREAMING_V3_IMPLEMENTATION.md` for technical details

Happy Streaming! 🚀

