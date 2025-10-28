# 👑 Queen Client

Welcome to Queen client! This is your friendly guide to mastering message queues without losing your sanity. We'll start simple and gradually unlock the superpowers. 🚀

## Table of Contents

- [Getting Started](#getting-started)
- [Part 1: Hello Queue!](#part-1-hello-queue)
- [Part 2: Push & Consume Basics](#part-2-push--consume-basics)
- [Part 3: Pop vs Consume (Choose Your Adventure)](#part-3-pop-vs-consume-choose-your-adventure)
- [Part 4: Partitions - Organize Your World](#part-4-partitions---organize-your-world)
- [Part 5: Consumer Groups - Share the Load](#part-5-consumer-groups---share-the-load)
- [Part 6: Namespaces & Tasks - The Wildcard Way](#part-6-namespaces--tasks---the-wildcard-way)
- [Part 7: Transactions - All or Nothing](#part-7-transactions---all-or-nothing)
- [Part 8: Client-Side Buffering - Speed Demon Mode](#part-8-client-side-buffering---speed-demon-mode)
- [Part 9: Dead Letter Queue - When Things Go Wrong](#part-9-dead-letter-queue---when-things-go-wrong)
- [Part 10: Lease Renewal - Keep It Locked](#part-10-lease-renewal---keep-it-locked)
- [Part 11: Queue Configuration - Fine Tuning](#part-11-queue-configuration---fine-tuning)
- [Part 12: Callbacks & Error Handling](#part-12-callbacks--error-handling)
- [Part 13: Graceful Shutdown](#part-13-graceful-shutdown)
- [Cheat Sheet](#cheat-sheet)

---

## Getting Started

First, install and import:

```javascript
import { Queen } from './client-js/client-v2/index.js'

// Connect to your Queen server
const queen = new Queen('http://localhost:6632')

// Or with multiple servers for high availability
const queen = new Queen(['http://server1:6632', 'http://server2:6632'])

// Or with full configuration
const queen = new Queen({
  urls: ['http://server1:6632', 'http://server2:6632'],
  timeoutMillis: 30000,
  retryAttempts: 3,
  loadBalancingStrategy: 'round-robin', // or 'session'
  enableFailover: true
})
```

That's it! You're connected. Now let's do something fun. 🎉

---

## Part 1: Hello Queue!

Every journey starts with a queue. Let's create one:

```javascript
// Create a simple queue
await queen.queue('my-tasks').create()

// That's it! The queue exists now with sensible defaults
```

Need to customize it? We'll get to that later. For now, let's keep it simple.

Want to delete a queue? (Be careful! ⚠️)

```javascript
await queen.queue('my-tasks').delete()
```

---

## Part 2: Push & Consume Basics

### Pushing Messages (aka "Adding Work to Do")

```javascript
// Push a single message
await queen.queue('my-tasks').push([
  { data: { job: 'send-email', to: 'alice@example.com' } }
])

// Push multiple messages at once
await queen.queue('my-tasks').push([
  { data: { job: 'send-email', to: 'alice@example.com' } },
  { data: { job: 'send-email', to: 'bob@example.com' } },
  { data: { job: 'resize-image', id: 123 } }
])
```

**Pro tip:** Notice the array? Always wrap your messages in an array, even for a single message.

### Consuming Messages (aka "Getting Work Done")

The easiest way to process messages:

```javascript
await queen.queue('my-tasks').consume(async (message) => {
  console.log('Processing:', message.data)
  
  // Do your work here
  await sendEmail(message.data.to)
  
  // That's it! If your function succeeds, the message is automatically acknowledged
  // If it throws an error, the message is automatically rejected and will retry
})
```

**What just happened?**
1. The consumer pulls messages from the queue
2. Your function processes each message
3. If successful → message is marked as complete ✅
4. If error → message goes back to the queue for retry 🔄

This runs **forever** by default. Perfect for background workers!

Want to process just a few messages and stop?

```javascript
// Process exactly 10 messages then stop
await queen
  .queue('my-tasks')
  .limit(10)
  .consume(async (message) => {
    console.log('Processing:', message.data)
  })
```

---

## Part 3: Pop vs Consume (Choose Your Adventure)

### The Consume Way (Recommended for Workers)

**Use when:** You want a long-running worker that continuously processes messages.

```javascript
// Runs forever, processing messages as they arrive
await queen.queue('my-tasks').consume(async (message) => {
  // Your processing logic
})
```

### The Pop Way (Good for On-Demand Processing)

**Use when:** You want to grab messages manually and control everything yourself.

```javascript
// Grab one message right now
const messages = await queen.queue('my-tasks').pop()

if (messages.length > 0) {
  const message = messages[0]
  
  try {
    // Do your work
    await processMessage(message.data)
    
    // Tell Queen it succeeded
    await queen.ack(message, true)
  } catch (error) {
    // Tell Queen it failed
    await queen.ack(message, false, { error: error.message })
  }
}
```

**Key differences:**
- `consume()` = Long-running, auto-ack, loops automatically
- `pop()` = One-shot, manual-ack, you control the loop

Want to pop multiple messages?

```javascript
// Grab up to 10 messages at once
const messages = await queen
  .queue('my-tasks')
  .batch(10)
  .pop()

console.log(`Got ${messages.length} messages`)
```

Want to wait if no messages are available?

```javascript
// Wait up to 30 seconds for messages to arrive
const messages = await queen
  .queue('my-tasks')
  .batch(10)
  .wait(true)  // Enable long polling
  .pop()
```

---

## Part 4: Partitions - Organize Your World

Think of partitions like lanes on a highway. Each lane processes independently.

**Why use partitions?**
- Process different types of work in parallel
- Ensure order within a partition
- Isolate failures

### Creating Partitioned Messages

```javascript
// Send messages to specific partitions
await queen
  .queue('user-events')
  .partition('user-123')
  .push([
    { data: { event: 'login', timestamp: Date.now() } }
  ])

await queen
  .queue('user-events')
  .partition('user-456')
  .push([
    { data: { event: 'logout', timestamp: Date.now() } }
  ])
```

**Important:** Messages in the same partition are **ordered**. Messages in different partitions are **independent**.

### Consuming from a Specific Partition

```javascript
// Process only messages from user-123's partition
await queen
  .queue('user-events')
  .partition('user-123')
  .consume(async (message) => {
    console.log('User 123 did:', message.data.event)
  })
```

### Real-World Example: Per-User Processing

```javascript
// Each user gets their own partition for ordered processing
const userId = 'alice-007'

// Push user-specific events
await queen
  .queue('user-commands')
  .partition(userId)
  .push([
    { data: { action: 'create-post', title: 'Hello World' } },
    { data: { action: 'like-post', postId: 123 } },
    { data: { action: 'comment', postId: 123, text: 'Nice!' } }
  ])

// Process user's commands in order
await queen
  .queue('user-commands')
  .partition(userId)
  .consume(async (message) => {
    // These will be processed in exact order
    console.log(`${userId} doing:`, message.data.action)
  })
```

---

## Part 5: Consumer Groups - Share the Load

Consumer groups let multiple workers share the same queue while ensuring each message is processed exactly once.

**Use cases:**
- Scale horizontally (run multiple workers)
- A/B testing (send copies to different systems)
- Fan-out patterns (process each message multiple ways)

### Basic Consumer Groups

```javascript
// Worker 1 in group "processors"
await queen
  .queue('emails')
  .group('processors')
  .consume(async (message) => {
    console.log('Worker 1 processing:', message.data)
  })

// Worker 2 in the SAME group (shares the load)
await queen
  .queue('emails')
  .group('processors')
  .consume(async (message) => {
    console.log('Worker 2 processing:', message.data)
  })
```

**Result:** Messages are distributed between Worker 1 and Worker 2. Each message goes to only ONE worker.

### Multiple Consumer Groups (Fan-Out)

```javascript
// Group 1: Send emails
await queen
  .queue('notifications')
  .group('email-sender')
  .consume(async (message) => {
    await sendEmail(message.data)
  })

// Group 2: Log to analytics (processes THE SAME messages)
await queen
  .queue('notifications')
  .group('analytics')
  .consume(async (message) => {
    await trackEvent(message.data)
  })
```

**Result:** Every message is processed by BOTH groups independently! 🎉

### Real-World Example: Order Processing

```javascript
// Main order processor (high priority)
await queen
  .queue('orders')
  .group('order-fulfillment')
  .concurrency(5)  // Run 5 workers in parallel
  .consume(async (message) => {
    await processOrder(message.data)
  })

// Analytics processor (separate group, same messages)
await queen
  .queue('orders')
  .group('analytics')
  .consume(async (message) => {
    await logOrderMetrics(message.data)
  })
```

---

## Part 6: Namespaces & Tasks - The Wildcard Way

Sometimes you don't care about specific queues. You want to process messages based on **what they do** or **where they belong**.

### Namespaces (Logical Grouping)

Think of namespaces as folders for your queues.

```javascript
// Create queues with namespaces
await queen.queue('billing-invoices').namespace('accounting').create()
await queen.queue('billing-receipts').namespace('accounting').create()
await queen.queue('user-emails').namespace('notifications').create()

// Push to specific queues
await queen.queue('billing-invoices').push([
  { data: { invoice: 'INV-001' } }
])

// Consume from ALL queues in the 'accounting' namespace
await queen
  .queue()
  .namespace('accounting')
  .consume(async (message) => {
    // This will receive messages from BOTH billing-invoices AND billing-receipts
    console.log('Accounting message:', message.data)
  })
```

**Why this is cool:** Add new queues to the namespace later, and existing consumers automatically process them! 🎯

### Tasks (Processing Types)

Tasks are like tags that describe what needs to be done.

```javascript
// Create queues with tasks
await queen.queue('video-uploads').task('video-processing').create()
await queen.queue('image-uploads').task('image-processing').create()

// Consume by task type
await queen
  .queue()
  .task('video-processing')
  .consume(async (message) => {
    // Only video processing messages
    await processVideo(message.data)
  })
```

### Combining Namespace + Task

```javascript
// Super specific filtering!
await queen
  .queue()
  .namespace('media')
  .task('urgent-processing')
  .consume(async (message) => {
    // Only urgent media processing from the media namespace
  })
```

---

## Part 7: Transactions - All or Nothing

Transactions are atomic operations. Either **everything** succeeds or **nothing** does.

**Use cases:**
- Ack one message and push to another queue (pipeline pattern)
- Process multiple messages atomically
- Ensure consistency across operations

### Basic Transaction: Ack + Push

```javascript
// Pop a message
const messages = await queen.queue('raw-data').batch(1).pop()

if (messages.length > 0) {
  const message = messages[0]
  
  // Process it
  const processed = await transformData(message.data)
  
  // Atomically: ack the input AND push the output
  await queen
    .transaction()
    .ack(message)  // Complete the input message
    .queue('processed-data')
    .push([{ data: processed }])  // Add to next queue
    .commit()
}

// If commit fails, NOTHING happens. Message stays in raw-data queue!
```

### Multi-Queue Pipeline

```javascript
// Pop from queue A
const messages = await queen.queue('queue-a').batch(1).pop()

// Transaction: ack from A, push to B and C
await queen
  .transaction()
  .ack(messages[0])
  .queue('queue-b')
  .push([{ data: { step: 2, value: messages[0].data.value * 2 } }])
  .queue('queue-c')
  .push([{ data: { step: 2, value: messages[0].data.value * 2 } }])
  .commit()

// Atomic! Either all three operations succeed, or none do
```

### Batch Processing Transaction

```javascript
// Pop multiple messages
const messages = await queen.queue('inputs').batch(10).pop()

// Process them
const results = messages.map(m => process(m.data))

// Atomically ack all inputs and push all outputs
const txn = queen.transaction()

// Ack all inputs
for (const message of messages) {
  txn.ack(message)
}

// Push all outputs
txn.queue('outputs').push(results.map(r => ({ data: r })))

await txn.commit()
```

### Transaction with Consumer

Want to consume with transactions? Easy:

```javascript
await queen
  .queue('source')
  .autoAck(false)  // Must disable auto-ack for manual transaction
  .consume(async (message) => {
    // Do work
    const result = await processMessage(message.data)
    
    // Transactionally ack and push result
    await queen
      .transaction()
      .ack(message)
      .queue('destination')
      .push([{ data: result }])
      .commit()
  })
```

---

## Part 8: Client-Side Buffering - Speed Demon Mode

Pushing messages one-at-a-time is slow. Buffering batches them up for massive speed boosts! 🚄

### How Buffering Works

Instead of sending messages immediately:
1. Messages collect in a local buffer
2. Buffer flushes when it reaches a **count** or **time** threshold
3. All buffered messages are sent in one HTTP request

**Result:** 10x-100x faster throughput!

### Basic Buffering

```javascript
// Buffer up to 100 messages OR 1 second (whichever comes first)
await queen
  .queue('logs')
  .buffer({ messageCount: 100, timeMillis: 1000 })
  .push([
    { data: { level: 'info', message: 'User logged in' } }
  ])

// Message is now buffered, not sent yet
// Will send when 100 messages accumulate OR 1 second passes
```

### High-Throughput Example

```javascript
// Send 10,000 messages super fast
for (let i = 0; i < 10000; i++) {
  await queen
    .queue('events')
    .buffer({ messageCount: 500, timeMillis: 100 })
    .push([
      { data: { id: i, timestamp: Date.now() } }
    ])
}

// Flush any remaining buffered messages
await queen.flushAllBuffers()
```

**Performance:** This might take seconds instead of minutes! ⚡

### Manual Flush

```javascript
// Flush all buffers for all queues
await queen.flushAllBuffers()

// Flush a specific queue's buffer
await queen.queue('my-queue').flushBuffer()

// Get buffer statistics
const stats = queen.getBufferStats()
console.log('Buffers:', stats)
// Example output: { 'my-queue/Default': { count: 45, size: 1234 } }
```

### Real-World Example: Log Aggregation

```javascript
// High-frequency logging with buffering
class Logger {
  constructor(queen) {
    this.queen = queen
  }
  
  async log(level, message) {
    await this.queen
      .queue('application-logs')
      .buffer({ messageCount: 1000, timeMillis: 5000 })
      .push([
        { data: { level, message, timestamp: Date.now() } }
      ])
  }
  
  async flush() {
    await this.queen.flushAllBuffers()
  }
}

const logger = new Logger(queen)
await logger.log('info', 'Server started')
await logger.log('debug', 'Processing request...')
// Logs are buffered and sent in batches!
```

---

## Part 9: Dead Letter Queue - When Things Go Wrong

Not all messages can be processed. Some are just... problematic. The DLQ is where failed messages go to be examined.

### How DLQ Works

1. Message fails (your handler throws an error)
2. Message retries (up to `retryLimit`)
3. After max retries → moves to Dead Letter Queue
4. You can query DLQ to see what went wrong

### Enable DLQ

```javascript
// Create queue with DLQ enabled
await queen
  .queue('risky-business')
  .config({
    retryLimit: 3,              // Try 3 times
    dlqAfterMaxRetries: true    // Send to DLQ after 3 failures
  })
  .create()
```

### Process Messages (Some Will Fail)

```javascript
await queen
  .queue('risky-business')
  .consume(async (message) => {
    if (message.data.value < 0) {
      throw new Error('Negative values not allowed!')
    }
    // Process normally
  })
```

### Query the DLQ

```javascript
// Get failed messages
const dlq = await queen
  .queue('risky-business')
  .dlq()
  .limit(10)
  .get()

console.log(`Found ${dlq.total} failed messages`)

for (const message of dlq.messages) {
  console.log('Failed message:', message.data)
  console.log('Error was:', message.errorMessage)
  console.log('Failed at:', message.dlqTimestamp)
}
```

### DLQ with Consumer Groups

```javascript
// Check DLQ for a specific consumer group
const dlq = await queen
  .queue('risky-business')
  .dlq('my-consumer-group')
  .limit(100)
  .get()
```

### Advanced DLQ Queries

```javascript
// Query with time range
const dlq = await queen
  .queue('risky-business')
  .dlq()
  .from('2025-01-01')
  .to('2025-01-31')
  .limit(100)
  .offset(0)  // Pagination
  .get()
```

---

## Part 10: Lease Renewal - Keep It Locked

When you pop a message, you get a "lease" (a lock). The lease expires after `leaseTime` seconds. If your processing takes longer, you need to **renew** the lease.

### Why Lease Renewal?

Imagine processing a video that takes 10 minutes, but your lease is 5 minutes. After 5 minutes, Queen thinks you died and gives the message to someone else. Oops! 😱

### Automatic Lease Renewal (Easy Mode)

```javascript
await queen
  .queue('long-tasks')
  .renewLease(true, 60000)  // Renew every 60 seconds
  .consume(async (message) => {
    // Even if this takes 30 minutes, the lease keeps renewing automatically!
    await processVeryLongTask(message.data)
  })
```

**What happens:** Every 60 seconds, Queen automatically extends your lease. Your function can take as long as needed!

### Manual Lease Renewal

```javascript
// Pop a message
const messages = await queen.queue('long-tasks').pop()
const message = messages[0]

// Start long processing
const timer = setInterval(async () => {
  await queen.renew(message)  // Extend lease
  console.log('Lease renewed!')
}, 30000)  // Every 30 seconds

try {
  await processVeryLongTask(message.data)
  await queen.ack(message, true)
} finally {
  clearInterval(timer)
}
```

### Batch Lease Renewal

```javascript
// Renew multiple messages at once
const messages = await queen.queue('tasks').batch(10).pop()

// Renew all of them
await queen.renew(messages)
```

### Using Just the Lease ID

```javascript
const message = messages[0]

// Renew by lease ID
await queen.renew(message.leaseId)
```

---

## Part 11: Queue Configuration - Fine Tuning

Queues have lots of knobs to turn. Let's explore them all!

### Complete Configuration Example

```javascript
await queen
  .queue('super-queue')
  .config({
    // Lease & Retry
    leaseTime: 300,                    // 5 minutes to process (seconds)
    retryLimit: 3,                     // Retry 3 times before giving up
    retryDelay: 5000,                  // Wait 5 seconds between retries (milliseconds)
    
    // Dead Letter Queue
    dlqAfterMaxRetries: true,          // Move to DLQ after max retries
    
    // Priority
    priority: 5,                       // Higher number = higher priority (0-10)
    
    // Delays & Buffers
    delayedProcessing: 60,             // Messages become available after 60 seconds
    windowBuffer: 30,                  // Hold messages for 30 seconds to batch them
    
    // Capacity
    maxSize: 10000,                    // Max 10,000 messages in queue
    
    // Retention
    retentionSeconds: 86400,           // Keep pending messages for 24 hours
    completedRetentionSeconds: 3600,   // Keep completed messages for 1 hour
    ttl: 86400,                        // Message expires after 24 hours (seconds)
    
    // Security
    encryptionEnabled: true            // Encrypt message payloads at rest
  })
  .create()
```

### Priority Queues

Higher priority queues are processed first!

```javascript
// High priority queue
await queen
  .queue('urgent-alerts')
  .config({ priority: 10 })
  .create()

// Normal priority
await queen
  .queue('regular-tasks')
  .config({ priority: 5 })
  .create()

// Low priority
await queen
  .queue('background-jobs')
  .config({ priority: 1 })
  .create()

// Consumer processes urgent-alerts first, then regular-tasks, then background-jobs
await queen.queue().namespace('all').consume(async (message) => {
  console.log('Processing:', message)
})
```

### Delayed Processing

Messages don't become available until the delay passes.

```javascript
// Messages are invisible for 60 seconds
await queen
  .queue('scheduled-tasks')
  .config({ delayedProcessing: 60 })
  .create()

// Push a message
await queen.queue('scheduled-tasks').push([
  { data: { task: 'send-reminder' } }
])

// Pop immediately: gets nothing!
const now = await queen.queue('scheduled-tasks').pop()
console.log(now)  // []

// Wait 60 seconds...
await new Promise(r => setTimeout(r, 61000))

// Pop again: now we get the message!
const later = await queen.queue('scheduled-tasks').pop()
console.log(later)  // [{ data: { task: 'send-reminder' } }]
```

### Window Buffering (Server-Side Batching)

Holds messages server-side to create natural batches.

```javascript
// Hold messages for 5 seconds to batch them
await queen
  .queue('events')
  .config({ windowBuffer: 5 })
  .create()

// Push 10 messages quickly
for (let i = 0; i < 10; i++) {
  await queen.queue('events').push([{ data: { id: i } }])
}

// Consumer gets them all at once!
await queen
  .queue('events')
  .batch(100)
  .consume(async (messages) => {
    console.log(`Got ${messages.length} messages in one batch!`)
    // Likely: "Got 10 messages in one batch!"
  })
```

### Message TTL (Time To Live)

Messages expire and are deleted automatically.

```javascript
// Messages live for 1 hour max
await queen
  .queue('temporary-data')
  .config({ ttl: 3600 })
  .create()

// Messages older than 1 hour are automatically deleted
```

### Encryption

Sensitive data? Enable encryption!

```javascript
await queen
  .queue('customer-pii')
  .config({ encryptionEnabled: true })
  .create()

// Messages are encrypted at rest
// Decrypted automatically when consumed
```

---

## Part 12: Callbacks & Error Handling

Sometimes you need more control over what happens when messages succeed or fail.

### Success Callback

```javascript
await queen
  .queue('tasks')
  .consume(async (message) => {
    return await processMessage(message.data)
  })
  .onSuccess(async (message, result) => {
    console.log('Success! Result:', result)
    // Custom ack logic could go here
  })
```

### Error Callback

```javascript
await queen
  .queue('tasks')
  .consume(async (message) => {
    throw new Error('Something went wrong!')
  })
  .onError(async (message, error) => {
    console.error('Failed:', error.message)
    // Log to external service, send alert, etc.
  })
```

### Both Callbacks (Full Control)

```javascript
await queen
  .queue('tasks')
  .autoAck(false)  // Disable auto-ack to manually control it
  .consume(async (message) => {
    return await riskyOperation(message.data)
  })
  .onSuccess(async (message, result) => {
    console.log('Success!')
    await queen.ack(message, true)
  })
  .onError(async (message, error) => {
    console.error('Error:', error.message)
    
    // Custom logic: retry or DLQ?
    if (error.message.includes('temporary')) {
      // Retry
      await queen.ack(message, false)
    } else {
      // Send to DLQ immediately
      await queen.ack(message, 'failed', { error: error.message })
    }
  })
```

### Push Callbacks

```javascript
await queen
  .queue('tasks')
  .push([
    { data: { id: 1 } },
    { data: { id: 2 } }
  ])
  .onSuccess(async (messages) => {
    console.log('Pushed successfully!')
  })
  .onError(async (messages, error) => {
    console.error('Push failed:', error)
  })
  .onDuplicate(async (messages, error) => {
    console.warn('Duplicate transaction IDs detected')
  })
```

### Batch Ack with Mixed Results

```javascript
const messages = await queen.queue('tasks').batch(10).pop()

// Process and mark each message individually
for (const message of messages) {
  try {
    await processMessage(message.data)
    message._status = true  // Mark as success
  } catch (error) {
    message._status = false  // Mark as failure
    message._error = error.message
  }
}

// Batch ack with individual statuses
await queen.ack(messages)
// Queen will ack some and nack others based on _status
```

---

## Part 13: Graceful Shutdown

Always clean up properly when shutting down!

### Why Graceful Shutdown?

When you kill a process:
1. Buffered messages need to be flushed
2. In-progress messages need to finish
3. Connections need to close properly

### Automatic Shutdown (Built-In)

Queen automatically handles `SIGINT` and `SIGTERM`:

```javascript
const queen = new Queen('http://localhost:6632')

// Your app runs...

// User presses Ctrl+C or Docker sends SIGTERM:
// Queen automatically flushes buffers and closes cleanly!
```

### Manual Shutdown

```javascript
const queen = new Queen('http://localhost:6632')

// Do work...

// Shutdown manually
await queen.close()
console.log('Queen shut down cleanly')
```

### Shutdown with AbortController

For consumers, use signals to stop them gracefully:

```javascript
const controller = new AbortController()

// Start consumer with abort signal
const consumerPromise = queen
  .queue('tasks')
  .consume(async (message) => {
    await processMessage(message.data)
  }, { signal: controller.signal })

// Later... stop the consumer
controller.abort()

// Wait for consumer to finish current message and stop
await consumerPromise

// Close Queen
await queen.close()
```

---

## Cheat Sheet

### Connection

```javascript
const queen = new Queen('http://localhost:6632')
const queen = new Queen(['http://server1:6632', 'http://server2:6632'])
```

### Queue Operations

```javascript
// Create
await queen.queue('my-queue').create()
await queen.queue('my-queue').config({ priority: 5 }).create()

// Delete
await queen.queue('my-queue').delete()
```

### Push

```javascript
// Simple
await queen.queue('q').push([{ data: { value: 1 } }])

// With partition
await queen.queue('q').partition('p1').push([{ data: { value: 1 } }])

// With buffering
await queen.queue('q').buffer({ messageCount: 100, timeMillis: 1000 }).push([{ data: { value: 1 } }])

// With custom transaction ID
await queen.queue('q').push([{ transactionId: 'my-id', data: { value: 1 } }])
```

### Pop

```javascript
// Simple pop
const msgs = await queen.queue('q').pop()

// Pop multiple
const msgs = await queen.queue('q').batch(10).pop()

// Pop with long polling
const msgs = await queen.queue('q').batch(10).wait(true).pop()

// Pop from partition
const msgs = await queen.queue('q').partition('p1').pop()
```

### Consume

```javascript
// Simple consume (runs forever)
await queen.queue('q').consume(async (msg) => { /* process */ })

// Consume with limit
await queen.queue('q').limit(10).consume(async (msg) => { /* process */ })

// Consume batches
await queen.queue('q').batch(10).consume(async (msgs) => { /* process array */ })

// Consume with concurrency
await queen.queue('q').concurrency(5).consume(async (msg) => { /* 5 parallel workers */ })

// Consume from partition
await queen.queue('q').partition('p1').consume(async (msg) => { /* process */ })

// Consume with consumer group
await queen.queue('q').group('my-group').consume(async (msg) => { /* process */ })

// Consume by namespace
await queen.queue().namespace('my-ns').consume(async (msg) => { /* process */ })

// Consume by task
await queen.queue().task('my-task').consume(async (msg) => { /* process */ })
```

### Acknowledgment

```javascript
// Ack success
await queen.ack(message, true)

// Ack failure (will retry)
await queen.ack(message, false)

// Ack with error details
await queen.ack(message, false, { error: 'Something went wrong' })

// Batch ack
await queen.ack([msg1, msg2, msg3], true)
```

### Transactions

```javascript
await queen
  .transaction()
  .ack(message)
  .queue('output-queue')
  .push([{ data: { result: 'processed' } }])
  .commit()
```

### Lease Renewal

```javascript
// Manual renewal
await queen.renew(message)
await queen.renew([msg1, msg2, msg3])
await queen.renew(message.leaseId)

// Auto renewal
await queen.queue('q').renewLease(true, 60000).consume(async (msg) => { /* process */ })
```

### Buffering

```javascript
// Flush all buffers
await queen.flushAllBuffers()

// Flush specific queue
await queen.queue('q').flushBuffer()

// Get buffer stats
const stats = queen.getBufferStats()
```

### DLQ

```javascript
// Query DLQ
const dlq = await queen.queue('q').dlq().limit(10).get()
const dlq = await queen.queue('q').dlq('consumer-group').limit(10).get()
const dlq = await queen.queue('q').dlq().from('2025-01-01').to('2025-01-31').get()
```

### Shutdown

```javascript
await queen.close()
```

---

## Configuration Defaults

### Client Defaults
```javascript
{
  timeoutMillis: 30000,               // 30 seconds
  retryAttempts: 3,
  retryDelayMillis: 1000,
  loadBalancingStrategy: 'round-robin',
  enableFailover: true
}
```

### Queue Defaults
```javascript
{
  leaseTime: 300,                     // 5 minutes
  retryLimit: 3,
  priority: 0,
  delayedProcessing: 0,
  windowBuffer: 0,
  maxSize: 0,                         // Unlimited
  retentionSeconds: 0,                // Keep forever
  completedRetentionSeconds: 0,
  encryptionEnabled: false
}
```

### Consume Defaults
```javascript
{
  concurrency: 1,
  batch: 1,
  autoAck: true,
  wait: true,                         // Long polling
  timeoutMillis: 30000,
  limit: null,                        // Run forever
  idleMillis: null,                   // No idle timeout
  renewLease: false
}
```

### Pop Defaults
```javascript
{
  batch: 1,
  wait: false,                        // No long polling
  autoAck: false                      // Manual ack required
}
```

---

## Logging

Enable detailed logging for debugging:

```bash
export QUEEN_CLIENT_LOG=true
node your-app.js
```

Example log output:
```
[2025-10-28T10:30:45.123Z] [INFO] [Queen.constructor] {"status":"initialized","urls":1}
[2025-10-28T10:30:45.234Z] [INFO] [QueueBuilder.push] {"queue":"tasks","partition":"Default","count":5}
[2025-10-28T10:30:46.789Z] [INFO] [HttpClient.request] {"method":"POST","url":"http://localhost:6632/api/v1/push"}
```

---

## Tips & Best Practices

1. **Use `consume()` for workers** - It's simpler and handles retries automatically
2. **Use `pop()` for control** - When you need precise control over acking and timing
3. **Buffer for speed** - Always use buffering when pushing many messages
4. **Partitions for order** - Use partitions when message order matters
5. **Consumer groups for scale** - Run multiple workers in the same group
6. **Transactions for consistency** - Use transactions when operations must be atomic
7. **Enable DLQ** - Always enable DLQ in production to catch failures
8. **Renew long leases** - Use auto-renewal for long-running tasks
9. **Graceful shutdown** - Always call `queen.close()` before exiting
10. **Monitor DLQ** - Regularly check your DLQ for failed messages

---

## Real-World Example: Complete Pipeline

Here's a complete example showing many features together:

```javascript
import { Queen } from './client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

// Setup queues
await queen.queue('raw-events').config({ priority: 5 }).create()
await queen.queue('processed-events').config({ priority: 10 }).create()
await queen.queue('notifications').config({ delayedProcessing: 60 }).create()

// Stage 1: Ingest raw events with buffering
async function ingestEvents() {
  for (let i = 0; i < 10000; i++) {
    await queen
      .queue('raw-events')
      .partition(`user-${i % 100}`)  // Partition by user
      .buffer({ messageCount: 500, timeMillis: 1000 })
      .push([{
        data: {
          userId: i % 100,
          event: 'page_view',
          timestamp: Date.now()
        }
      }])
  }
  
  await queen.flushAllBuffers()
  console.log('Ingestion complete!')
}

// Stage 2: Process events with transaction
async function processEvents() {
  await queen
    .queue('raw-events')
    .group('processors')
    .concurrency(5)
    .batch(10)
    .autoAck(false)  // Manual ack for transactions
    .consume(async (messages) => {
      // Process batch
      const processed = messages.map(m => ({
        userId: m.data.userId,
        processed: true,
        timestamp: Date.now()
      }))
      
      // Atomic: ack inputs and push outputs
      const txn = queen.transaction()
      
      for (const msg of messages) {
        txn.ack(msg)
      }
      
      txn.queue('processed-events').push(
        processed.map(p => ({ data: p }))
      )
      
      await txn.commit()
    })
    .onError(async (messages, error) => {
      console.error('Processing failed:', error)
      await queen.ack(messages, false)
    })
}

// Stage 3: Send notifications (delayed)
async function sendNotifications() {
  await queen
    .queue('processed-events')
    .group('notifiers')
    .renewLease(true, 30000)  // Auto-renew every 30s
    .consume(async (message) => {
      // Queue delayed notification
      await queen
        .queue('notifications')
        .push([{
          data: {
            userId: message.data.userId,
            message: 'Your data has been processed!'
          }
        }])
      
      console.log(`Notification queued for user ${message.data.userId}`)
    })
}

// Stage 4: Check DLQ periodically
setInterval(async () => {
  const dlq = await queen
    .queue('raw-events')
    .dlq()
    .limit(10)
    .get()
  
  if (dlq.total > 0) {
    console.warn(`⚠️  ${dlq.total} failed messages in DLQ!`)
  }
}, 60000)  // Check every minute

// Run the pipeline
await ingestEvents()
await Promise.all([
  processEvents(),
  sendNotifications()
])

// Graceful shutdown
process.on('SIGINT', async () => {
  await queen.close()
  process.exit(0)
})
```

---

## What's Next?

You now know everything about Queen v2! 🎉

**Additional resources:**
- [API Documentation](../../API.md) - Complete API reference
- [Test Examples](../test-v2/) - 94 working test cases
- [Architecture Guide](../../docs/) - Deep dive into Queen's internals

**Need help?** Check out the test files in `test-v2/` - they're full of working examples!

Happy queuing! 👑✨
