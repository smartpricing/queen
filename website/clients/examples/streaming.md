# Stream Processing

Queen provides powerful stream processing capabilities for real-time windowed aggregations across multiple queues. Streams collect messages from one or more source queues into time-based windows, allowing you to process related events together.

## Core Concepts

### What is a Stream?

A **stream** is a continuous flow of messages from multiple queues that are grouped into time-based **windows**. Instead of consuming individual messages, you process entire windows of messages that arrived within a specific time period.

**Key Benefits:**
- **Temporal correlation**: Process all messages that arrived within the same time window
- **Multi-queue aggregation**: Combine messages from multiple related queues
- **Partitioned processing**: Group messages by key for isolated processing
- **Automatic windowing**: Server handles window management and delivery

### Window Types

#### Tumbling Windows

Fixed-size, non-overlapping windows. When a window ends, a new one begins immediately.

```javascript
await queen.stream('my-stream', 'my-namespace')
  .sources(['queue1', 'queue2'])
  .tumblingTime(5)  // 5-second windows
  .define();
```

**Example timeline:**
```
Window 1: [0s - 5s)   -> Process at 5s
Window 2: [5s - 10s)  -> Process at 10s
Window 3: [10s - 15s) -> Process at 15s
```

#### Grace Period

Allows late-arriving messages to be included in closed windows:

```javascript
await queen.stream('my-stream', 'my-namespace')
  .sources(['queue1'])
  .tumblingTime(5)
  .gracePeriod(1)  // Wait 1 extra second for late messages
  .define();
```

**Timeline with grace period:**
```
Window: [0s - 5s)
Closes at: 5s
Grace period: 5s - 6s (accepts late messages)
Delivered at: 6s
```

## Basic Stream Example

Simple stream combining messages from multiple queues:

```javascript
import { Queen } from 'queen-mq';
const queen = new Queen('http://localhost:6632');

// Create source queues
await queen.queue('events').namespace('analytics').task('stream').create();
await queen.queue('clicks').namespace('analytics').task('stream').create();

// Define stream with 5-second tumbling windows
await queen.stream('event-stream', 'analytics')
  .sources(['events', 'clicks'])
  .tumblingTime(5)
  .gracePeriod(1)
  .define();

// Consume the stream
const consumer = queen.consumer('event-stream', 'analytics-consumer');

await consumer.process(async (window) => {
  console.log(`Window: ${window.start} - ${window.end}`);
  console.log(`Total messages: ${window.allMessages.length}`);
  
  // Process all messages in the window
  for (const msg of window.allMessages) {
    console.log(msg.data);
  }
});
```

## Partitioned Streaming

**Partitioned streams** ensure that messages with the same partition key are processed together in the same window. This is crucial for maintaining consistency when processing related events.

### Use Cases for Partitioned Streams

- **Per-user analytics**: Group all events for a specific user
- **Session tracking**: Process all events in a user session together
- **Chat/conversation processing**: Handle all messages in a conversation
- **Transaction correlation**: Group related transaction events

### Complete Example: Chat Message Processing

This example demonstrates a pipeline where translation requests are processed and forwarded to an agent queue, with both being aggregated by chat ID:

```javascript
import { Queen } from 'queen-mq';
const queen = new Queen({ url: 'http://localhost:6632' });

// Setup queues
await queen.queue('chat-translations').delete();
await queen.queue('chat-agent').delete();
await queen.queue('chat-translations').namespace('prod').task('stream').create();
await queen.queue('chat-agent').namespace('prod').task('stream').create();

// Define partitioned stream
await queen.stream('chat-stream', 'prod')
  .sources(['chat-translations', 'chat-agent'])
  .partitioned()            // Enable partition-aware processing
  .tumblingTime(5)          // 5-second windows
  .gracePeriod(1)           // 1-second grace period
  .define();

// Producer: Generate translation requests
let chatId = 0;
const producer = async () => {
  while (true) {
    chatId++;
    await queen
      .queue('chat-translations')
      .partition(chatId.toString())  // Set partition key
      .push({
        data: {
          kind: 'translation',
          text: 'Hello world',
          timestamp: new Date().toISOString(),
          chatId: chatId
        }
      });
    await new Promise(resolve => setTimeout(resolve, 100));
  }
};
producer();

// Pipeline: Transform translations to agent messages
const transformer = async () => {
  await queen
    .queue('chat-translations')
    .autoAck(false)
    .batch(1)
    .each()
    .consume(async (message) => {
      const agentMessage = {
        ...message.data,
        kind: 'agent',
        processedAt: new Date().toISOString()
      };
      
      // Transactionally ack and push to next queue
      await queen
        .transaction()
        .ack(message)
        .queue('chat-agent')
        .partition(message.data.chatId.toString())  // Preserve partition key!
        .push([{ data: agentMessage }])
        .commit();
    });
};
transformer();

// Stream consumer: Process windowed messages
const consumer = queen.consumer('chat-stream', 'chat-analytics');

await consumer.process(async (window) => {
  console.log(`\n=== Window ${window.id} ===`);
  console.log(`Time: ${window.start} - ${window.end}`);
  console.log(`Total messages: ${window.allMessages.length}`);
  
  // Group messages by chat ID
  const byChatId = window.groupBy('data.chatId');
  
  // Process each chat's messages together
  for (const [chatId, messages] of Object.entries(byChatId)) {
    console.log(`\nChat ${chatId}:`);
    
    const translations = messages.filter(m => m.data.kind === 'translation');
    const agentReplies = messages.filter(m => m.data.kind === 'agent');
    
    console.log(`  - ${translations.length} translations`);
    console.log(`  - ${agentReplies.length} agent replies`);
    
    // Example: Calculate processing latency
    if (translations.length > 0 && agentReplies.length > 0) {
      const firstTranslation = new Date(translations[0].data.timestamp);
      const lastAgent = new Date(agentReplies[agentReplies.length - 1].data.processedAt);
      const latency = lastAgent - firstTranslation;
      console.log(`  - Processing latency: ${latency}ms`);
    }
  }
});
```

### Why Preserve Partition Keys?

When using partitioned streams, **always preserve the partition key** through your pipeline:

```javascript
// ✅ CORRECT: Same partition key throughout
await queen.queue('source').partition('user-123').push(msg);
await queen.queue('destination').partition('user-123').push(transformed);

// ❌ WRONG: Different partition keys break correlation
await queen.queue('source').partition('user-123').push(msg);
await queen.queue('destination').partition('other-key').push(transformed);
```

## Window Operations

The `window` object provided to your consumer includes powerful methods for processing messages:

```javascript
await consumer.process(async (window) => {
  // Window metadata
  console.log(window.id);         // Unique window identifier
  console.log(window.start);      // Window start time (ISO 8601)
  console.log(window.end);        // Window end time (ISO 8601)
  
  // All messages in the window
  console.log(window.allMessages.length);
  
  // Group by any field path
  const byUserId = window.groupBy('data.userId');
  const byEventType = window.groupBy('data.event.type');
  const byQueue = window.groupBy('queue_name');
  
  // Process grouped messages
  for (const [key, messages] of Object.entries(byUserId)) {
    console.log(`User ${key}: ${messages.length} events`);
  }
});
```

### groupBy Examples

```javascript
// Group by nested field
const grouped = window.groupBy('data.user.country');
// Result: { 'US': [...], 'UK': [...], 'DE': [...] }

// Group by source queue
const byQueue = window.groupBy('queue_name');
// Result: { 'events': [...], 'clicks': [...] }

// Group by message priority
const byPriority = window.groupBy('priority');
// Result: { '0': [...], '5': [...], '10': [...] }
```

## Stream Configuration

### Define a Stream

```javascript
await queen.stream('stream-name', 'namespace')
  .sources(['queue1', 'queue2', 'queue3'])  // 1 or more source queues
  .partitioned()                             // Optional: enable partitioning
  .tumblingTime(5)                           // Window size in seconds
  .gracePeriod(2)                            // Optional: grace period in seconds
  .define();
```

### Stream Lifecycle

```javascript
// Define a stream
await queen.stream('my-stream', 'analytics').sources(['events']).tumblingTime(5).define();

// Get stream info
const info = await queen.stream('my-stream', 'analytics').info();
console.log(info);

// Delete a stream (also deletes the underlying queue)
await queen.stream('my-stream', 'analytics').delete();
```

## Consumer Groups for Streams

Streams support consumer groups for horizontal scaling:

```javascript
// Multiple consumers in the same group
const consumer1 = queen.consumer('my-stream', 'analytics-group');
const consumer2 = queen.consumer('my-stream', 'analytics-group');
const consumer3 = queen.consumer('my-stream', 'analytics-group');

// Each window is delivered to only ONE consumer in the group
await Promise.all([
  consumer1.process(processWindow),
  consumer2.process(processWindow),
  consumer3.process(processWindow)
]);
```

**Benefits:**
- **Horizontal scaling**: Add more consumers to process windows faster
- **Load distribution**: Windows are automatically distributed across consumers
- **Fault tolerance**: If one consumer fails, others continue processing

## Performance Tuning

### Server-Side Configuration

Stream processing performance can be tuned via environment variables on the Queen server.

#### Worker Configuration

Control how many stream poll workers run and how they check for windows:

```bash
# Number of dedicated stream poll worker threads
# Scale based on number of active stream consumers
STREAM_POLL_WORKER_COUNT=2        # Default: 2
                                  # Low load (<10 consumers): 2 workers
                                  # Medium load (10-50): 4 workers
                                  # High load (50+): 8 workers

# How often workers check for new consumer requests (in-memory check, very cheap)
STREAM_POLL_WORKER_INTERVAL=100   # Default: 100ms

# Maximum concurrent window checks per stream worker
STREAM_CONCURRENT_CHECKS=10       # Default: 10
                                  # Low load: 10 concurrent checks
                                  # Medium load: 15 concurrent checks
                                  # High load: 20 concurrent checks
```

**Thread Pool Sizing:**
The database thread pool needs to accommodate stream workers:
```
DB ThreadPool Size = P + S + (S × C) + T

Where:
  P = POLL_WORKER_COUNT (regular queue poll workers)
  S = STREAM_POLL_WORKER_COUNT
  C = STREAM_CONCURRENT_CHECKS
  T = DB_THREAD_POOL_SERVICE_THREADS (background operations)

Example: 2 + 2 + (2 × 10) + 5 = 29 threads
```

#### Backoff Configuration

Control how aggressively the server checks for completed windows:

```bash
# Minimum time between stream checks per consumer group
STREAM_POLL_INTERVAL=1000         # Default: 1000ms (1 second)
                                  # Lower = more responsive, higher DB load
                                  # Higher = less DB load, higher latency

# Number of consecutive empty checks before backoff starts
STREAM_BACKOFF_THRESHOLD=5        # Default: 5
                                  # Lower = backoff sooner (reduce load)
                                  # Higher = stay aggressive longer

# Exponential backoff multiplier
STREAM_BACKOFF_MULTIPLIER=2.0     # Default: 2.0
                                  # Interval *= multiplier after each empty check

# Maximum poll interval after backoff
STREAM_MAX_POLL_INTERVAL=5000     # Default: 5000ms (5 seconds)
                                  # Caps the exponential backoff growth
```

**Backoff Example:**
With defaults (1000ms initial, threshold 5, multiplier 2.0):
```
Check 1-5: 1000ms (aggressive initial checks)
Check 6:   2000ms (backoff threshold reached)
Check 7:   4000ms
Check 8+:  5000ms (capped at max)
Messages arrive: Reset to 1000ms immediately
```

### Tuning Recommendations

#### Low-Latency Streams (Real-time Analytics)

For sub-second response times:

```bash
STREAM_POLL_WORKER_COUNT=4
STREAM_POLL_WORKER_INTERVAL=50    # Check more frequently
STREAM_POLL_INTERVAL=500          # Aggressive window checks
STREAM_BACKOFF_THRESHOLD=10       # Stay aggressive longer
STREAM_MAX_POLL_INTERVAL=2000     # Lower backoff cap
STREAM_CONCURRENT_CHECKS=15
```

**Best for:** Real-time dashboards, live monitoring, immediate alerts

#### High-Throughput Streams (Heavy Load)

For processing many windows with many consumers:

```bash
STREAM_POLL_WORKER_COUNT=8        # More workers
STREAM_POLL_WORKER_INTERVAL=100
STREAM_POLL_INTERVAL=1000
STREAM_BACKOFF_THRESHOLD=5
STREAM_MAX_POLL_INTERVAL=5000
STREAM_CONCURRENT_CHECKS=20       # More concurrent checks
```

**Best for:** Batch analytics, high-volume event processing, many concurrent streams

#### Low-Load / Development

For minimal resource usage during development or low traffic:

```bash
STREAM_POLL_WORKER_COUNT=1        # Single worker
STREAM_POLL_WORKER_INTERVAL=200   # Less frequent checks
STREAM_POLL_INTERVAL=2000         # Relaxed polling
STREAM_BACKOFF_THRESHOLD=3        # Quick backoff
STREAM_MAX_POLL_INTERVAL=10000    # Higher backoff cap
STREAM_CONCURRENT_CHECKS=5        # Fewer concurrent checks
```

**Best for:** Development, testing, low-traffic production

### Client-Side Best Practices

1. **Use appropriate window sizes**
   - Too small: High overhead, many small windows
   - Too large: Higher latency, larger memory footprint
   - Sweet spot: 5-30 seconds for most use cases

2. **Set reasonable grace periods**
   - Typically 10-20% of window size
   - Balance between completeness and latency
   - Example: 5s window + 1s grace = 6s total latency

3. **Process windows efficiently**
   - Minimize processing time in your consumer
   - Use async operations where possible
   - Consider batching database writes

4. **Monitor window sizes**
   ```javascript
   await consumer.process(async (window) => {
     if (window.allMessages.length > 10000) {
       console.warn('Large window detected, consider smaller window size');
     }
   });
   ```

## Advanced Patterns

### Multi-Stage Streaming Pipeline

Create complex event processing pipelines:

```javascript
// Stage 1: Raw events
await queen.stream('raw-events', 'pipeline')
  .sources(['clicks', 'views', 'conversions'])
  .tumblingTime(10)
  .define();

// Stage 2: Enriched events (consume from Stage 1, push to enriched queue)
const enrichConsumer = queen.consumer('raw-events', 'enricher');
await enrichConsumer.process(async (window) => {
  for (const msg of window.allMessages) {
    const enriched = await enrichEvent(msg.data);
    await queen.queue('enriched-events').push({ data: enriched });
  }
});

// Stage 3: Final analytics
await queen.stream('analytics', 'pipeline')
  .sources(['enriched-events'])
  .tumblingTime(60)  // Longer window for aggregation
  .define();

const analyticsConsumer = queen.consumer('analytics', 'aggregator');
await analyticsConsumer.process(async (window) => {
  const stats = calculateStats(window.allMessages);
  await saveToDatabase(stats);
});
```

### Time-Series Aggregation

Rolling metrics calculation:

```javascript
await queen.stream('metrics-5s', 'monitoring')
  .sources(['app-metrics'])
  .tumblingTime(5)
  .define();

const consumer = queen.consumer('metrics-5s', 'aggregator');

await consumer.process(async (window) => {
  const metrics = window.allMessages.map(m => m.data.value);
  
  const stats = {
    timestamp: window.start,
    count: metrics.length,
    sum: metrics.reduce((a, b) => a + b, 0),
    avg: metrics.reduce((a, b) => a + b, 0) / metrics.length,
    min: Math.min(...metrics),
    max: Math.max(...metrics),
    p50: percentile(metrics, 50),
    p95: percentile(metrics, 95),
    p99: percentile(metrics, 99)
  };
  
  await saveMetrics(stats);
});
```

### Session Windows (Custom Implementation)

While Queen provides tumbling windows, you can implement session-like windows using partitioning:

```javascript
await queen.stream('user-sessions', 'analytics')
  .sources(['user-events'])
  .partitioned()           // Group by user
  .tumblingTime(30)        // 30-second base window
  .gracePeriod(5)
  .define();

const consumer = queen.consumer('user-sessions', 'session-analyzer');

await consumer.process(async (window) => {
  const byUser = window.groupBy('data.userId');
  
  for (const [userId, events] of Object.entries(byUser)) {
    // Analyze user's session within this window
    const session = {
      userId,
      events: events.length,
      duration: calculateDuration(events),
      pages: new Set(events.map(e => e.data.page)).size
    };
    
    await saveSession(session);
  }
});
```

## Troubleshooting

### Windows Not Arriving

**Symptoms:** Consumer never receives windows

**Checks:**
1. Verify source queues exist and have messages
2. Check window size - may need to wait for window to complete
3. Verify consumer group name matches
4. Check server logs for errors
5. Ensure `STREAM_POLL_WORKER_COUNT > 0`

### High Latency

**Symptoms:** Windows arrive much later than expected

**Solutions:**
1. Reduce `STREAM_POLL_INTERVAL` on server
2. Increase `STREAM_POLL_WORKER_COUNT`
3. Reduce `STREAM_CONCURRENT_CHECKS` if DB is overloaded
4. Check if grace period is too long
5. Monitor database performance

### High Database Load

**Symptoms:** Database CPU/IO is very high

**Solutions:**
1. Increase `STREAM_POLL_INTERVAL` (check less frequently)
2. Increase `STREAM_BACKOFF_THRESHOLD` (backoff sooner)
3. Reduce `STREAM_CONCURRENT_CHECKS`
4. Reduce `STREAM_POLL_WORKER_COUNT`
5. Use longer tumbling windows

### Memory Issues

**Symptoms:** Server memory usage grows over time

**Solutions:**
1. Reduce window size (smaller `tumblingTime`)
2. Process windows faster in consumer
3. Reduce number of source queues per stream
4. Check for unprocessed windows piling up

## More Examples

See the [examples directory](https://github.com/smartpricing/queen/tree/master/examples) for complete working examples:
- `18-streaming.js` - Basic non-partitioned streaming
- `19-streaming-partitioned.js` - Partitioned streaming with pipeline
- `16-streaming.js` - Additional streaming patterns
