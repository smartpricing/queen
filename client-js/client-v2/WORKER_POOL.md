# Worker Pool Pattern

## Overview

The Worker Pool pattern maintains **constant concurrency** during message processing, ensuring optimal throughput by immediately spawning new workers when others complete.

## Problem with Traditional Concurrency

The traditional approach using `Promise.all()` creates N workers that each poll and process sequentially:

```
Worker 1: [Poll] → [Process] → [Poll] → [Process] → ...
Worker 2: [Poll] → [Process] → [Poll] → [Process] → ...
Worker 3: [Poll] → [Process] → [Poll] → [Process] → ...
```

**Issues:**
- Workers sit idle while polling
- If one worker finishes faster, it waits for its own next poll
- Throughput drops when workers are between poll cycles
- Uneven message processing times cause inefficiency

## Worker Pool Solution

The new pattern separates polling from processing:

```
Poller (continuous) → [Message Queue] → Worker Pool
                                            ↓
                                    N concurrent workers
                                            ↓
                                   When worker completes
                                            ↓
                                   Spawn replacement immediately
```

**Benefits:**
- ✅ Maintains constant N workers processing at all times
- ✅ No idle time between message completions
- ✅ Better load distribution
- ✅ Higher sustained throughput

## When is it Activated?

Worker Pool mode is automatically enabled when **both** conditions are met:

1. `concurrency > 1` (multiple workers)
2. `.each()` is called (process messages individually)

## Usage

```javascript
await queen.queue('tasks')
  .concurrency(10)        // 10 concurrent workers
  .batch(50)              // Fetch 50 messages at a time
  .each()                 // ← Enables worker pool mode!
  .autoAck(true)
  .consume(async (msg) => {
    // Process message
    // When this completes, a new worker spawns immediately
    await processTask(msg)
  })
```

## Traditional Mode (Batch Processing)

Without `.each()`, the traditional `Promise.all()` pattern is used:

```javascript
await queen.queue('tasks')
  .concurrency(5)
  .batch(10)
  // No .each() call
  .consume(async (messages) => {
    // Process entire batch
    // Traditional parallel workers
    await processBatch(messages)
  })
```

This is better for batch operations where you want to process groups of messages together.

## Performance Comparison

### Traditional Pattern
- Concurrency: 5
- Message processing time: varies 100-500ms
- Effective throughput: ~50-60 msgs/sec (workers often idle)

### Worker Pool Pattern
- Concurrency: 5  
- Message processing time: varies 100-500ms
- Effective throughput: ~80-100 msgs/sec (always N workers active)

## Implementation Details

The worker pool maintains:
- **Message queue**: Buffered messages waiting for processing
- **Active worker count**: Tracks current concurrent workers
- **Continuous poller**: Keeps message queue filled
- **Dynamic spawning**: Creates new worker immediately when one completes

### Key Code

```javascript
// Worker completion triggers immediate replacement
finally {
  activeWorkers--
  
  // Spawn replacement if work available
  if (messageQueue.length > 0 && polling) {
    const nextMessage = messageQueue.shift()
    processOneMessage(nextMessage, workerId) // Don't await!
  }
}
```

## Monitoring

With logging enabled, you'll see:

```
[ConsumerManager.workerPool] messages-received count=50 activeWorkers=5 queueSize=0
[ConsumerManager.workerPool] workerId=123 processing activeWorkers=5 queueSize=45
[ConsumerManager.workerPool] workerId=123 completed processedCount=1 activeWorkers=4
[ConsumerManager.workerPool] workerId=124 processing activeWorkers=5 queueSize=44
```

Notice:
- `activeWorkers` quickly ramps to concurrency limit (5)
- Stays at that level consistently
- `queueSize` shows buffer of pending work

## Best Practices

1. **Use `.each()` for individual message processing**
   - Best for tasks with varying processing times
   - Optimal for high-throughput scenarios
   
2. **Use batch mode (no `.each()`) for batch operations**
   - When you need to process messages together
   - When you want transaction-like behavior across a batch

3. **Set appropriate `batch` size**
   - Higher batch = fewer HTTP requests
   - Lower batch = more responsive to changes
   - Recommended: 2-5x your concurrency level

4. **Monitor with logging**
   ```javascript
   const queen = new Queen({ 
     nodes: ['http://localhost:8080'],
     logging: true  // See worker pool activity
   })
   ```

## Example: High-Throughput Processing

```javascript
// Process 10,000 images with 20 concurrent workers
await queen.queue('image-processing')
  .concurrency(20)           // 20 workers in pool
  .batch(100)                // Fetch 100 at a time
  .each()                    // Worker pool mode
  .autoAck(true)
  .renewLease(true, 30000)   // Renew every 30s
  .consume(async (msg) => {
    await processImage(msg.payload.imageUrl)
  })
```

This maintains constant 20 workers processing, achieving maximum throughput.

