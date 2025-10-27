import { Queen } from 'queen-mq'

/**
 * ============================================================================
 * QUEEN CLIENT - NEW DESIGN
 * ============================================================================
 * 
 * Quick Reference:
 * 
 * INITIALIZATION:  new Queen('url') or new Queen(['url1', 'url2']) or new Queen({ urls, timeoutMillis, ... })
 * QUEUE CONFIG:    await queen.queue('name').create() or .config({ leaseTime, ... }).create()
 * PUSH:            await queen.queue('name').push([{ payload: data }])
 * CONSUME:         await queen.queue('name').consume(async msg => { ... })
 * POP:             const msgs = await queen.queue('name').pop()
 * ACK:             await queen.ack(msg) or await queen.ack(msg, true/false/'retry')
 * TRANSACTIONS:    const tx = queen.transaction(); await tx.ack(msg); await tx.commit()
 * SHUTDOWN:        await queen.close()
 * 
 * TIME UNIT CONVENTIONS:
 *   - Properties with "Millis" suffix → milliseconds (timeoutMillis, idleMillis, timeMillis)
 *   - Properties with "Seconds" suffix → seconds (retentionSeconds, completedRetentionSeconds)
 *   - Properties without suffix (time-related) → seconds (leaseTime, delayedProcessing, windowBuffer)
 * 
 * See "DEFAULT VALUES" section below for all defaults
 * 
 * ============================================================================
 */

// # Create Client

const queen = new Queen('http://localhost:6632')

// OR
const queen2 = new Queen(['http://localhost:6632'])

// OR with full configuration
const queen3 = new Queen({
  urls: ['http://localhost:6632', 'http://server2:6632'],
  timeoutMillis: 120000,              // Request timeout (milliseconds)
  retryAttempts: 5,                   // Number of retry attempts on failure
  retryDelayMillis: 2000,             // Initial delay between retries (milliseconds, exponential backoff)
  loadBalancingStrategy: 'round-robin', // 'round-robin' or 'session'
  enableFailover: true                // Try other servers if one fails
})

// ============================================================================
// DEFAULT VALUES - Complete Reference
// ============================================================================

/**
 * CLIENT INITIALIZATION DEFAULTS
 */
const defaultClientConfig = {
  timeoutMillis: 30000,                // 30 seconds (milliseconds)
  retryAttempts: 3,                    // 3 retry attempts
  retryDelayMillis: 1000,              // 1 second initial delay (milliseconds, exponential backoff)
  loadBalancingStrategy: 'round-robin', // 'round-robin' or 'session'
  enableFailover: true                 // Auto-failover to other servers
}

/**
 * QUEUE CONFIGURATION DEFAULTS
 */
const defaultQueueConfig = {
  leaseTime: 300,                      // 5 minutes (in seconds)
  retryLimit: 3,                       // Max 3 retries before DLQ
  priority: 0,                         // Default priority (higher = processed first)
  delayedProcessing: 0,                // No delay (in seconds)
  windowBuffer: 0,                     // No window buffering (in seconds)
  maxSize: 10000,                      // Max 10,000 messages per queue
  retentionSeconds: 0,                 // No retention (keep forever)
  completedRetentionSeconds: 0,        // No retention for completed messages
  encryptionEnabled: false             // No encryption by default
}

/**
 * CONSUME DEFAULTS
 */
const defaultConsumeOptions = {
  concurrency: 1,                      // Single worker
  batch: 1,                            // One message at a time
  autoAck: true,                       // Auto-acknowledge by default
  wait: true,                          // Long polling enabled (for consume)
  timeoutMillis: 30000,                // 30 seconds long poll timeout (milliseconds)
  limit: null,                         // No limit (run forever)
  idleMillis: null,                    // No idle timeout (milliseconds)
  renewLease: false,                   // No auto-renewal
  renewLeaseIntervalMillis: null,      // Auto-renewal interval (milliseconds) when enabled
  subscriptionMode: null,              // No subscription mode (standard queue mode)
  subscriptionFrom: null               // No subscription start point
}

/**
 * POP DEFAULTS
 */
const defaultPopOptions = {
  batch: 1,                            // One message
  wait: false,                         // No long polling (immediate return)
  timeoutMillis: 30000,                // 30 seconds if wait=true (milliseconds)
  autoAck: false                       // Manual ack required
}

/**
 * PUSH DEFAULTS
 */
const defaultPushOptions = {
  // No defaults - all optional
  // transactionId: auto-generated UUID if not provided
  // traceId: optional (must be valid UUID if provided)
  // buffer: null (no client-side buffering)
}

/**
 * CLIENT-SIDE BUFFER DEFAULTS
 */
const defaultBufferOptions = {
  messageCount: 100,                   // Flush after 100 messages
  timeMillis: 1000                     // Or flush after 1 second (milliseconds)
  // Both conditions - whichever comes first triggers flush
}

/**
 * TRANSACTION DEFAULTS
 */
// No defaults - transactions are explicit:
// - Create: queen.transaction()
// - Build: tx.ack(msg), tx.queue('name').push([...])
// - Commit: await tx.commit()

// ============================================================================
// DEFAULTS IN PRACTICE - Examples
// ============================================================================

// Example 1: Minimal queue creation (all defaults applied)
await queen.queue('simple-queue').create()
// → leaseTime: 300s, retryLimit: 3, priority: 0, maxSize: 10000

// Example 2: Override specific defaults
await queen.queue('critical-queue').config({
  leaseTime: 600,    // Override: 10 minutes instead of 5
  retryLimit: 5,     // Override: 5 retries instead of 3
  priority: 10       // Override: High priority instead of 0
  // Other values use defaults
}).create()

// Example 3: Minimal consume (all defaults applied)
await queen.queue('tasks').consume(async msg => {
  return processMessage(msg.payload)
})
// → concurrency: 1, batch: 1, autoAck: true, wait: true (long polling)

// Example 4: High-throughput consume (override defaults)
await queen.queue('events')
  .concurrency(20)    // Override: 20 workers instead of 1
  .batch(50)          // Override: 50 messages instead of 1
  .consume(async msgs => {
    return msgs.map(m => processMessage(m.payload))
  })
// → autoAck: true (default), wait: true (default)

// Example 5: Pop with defaults
const messages = await queen.queue('tasks').pop()
// → batch: 1, wait: false, autoAck: false (must ack manually)

// ============================================================================

// # Configure things

await queen.queue('translations-queue').create()

// Fluent interface
await queen.queue('transaltions-queue')
.namespace('chat')
.task('core')
.create()

// or direct config method
await queen
.queue('transaltions-queue')
.config({
  leaseTime: 600,           // Time in seconds before message lease expires
  retryLimit: 5,            // Maximum retry attempts before message goes to failed
  priority: 8,              // Queue priority (higher = processed first)
  delayedProcessing: 2,     // Delay in seconds before message is available
  windowBuffer: 3,          // Time in seconds messages wait before being available
  maxSize: 10000            // Maximum number of messages in queue  
})
.create()

await queen.queue('transaltions-queue').delete()

// # Push things

await queen
.queue('translations-queue')
.partition('x')
.push([
  { transactionId: 'xx', payload: {}}
])

await queen
.queue('translations-queue')
.partition('x')
.push([
  { transactionId: 'xx', traceId: '', payload: {}}
])
.onDuplicate( async (msg, error) => {
  // triggered if message is duplicated
})
.onSuccess( async (msg) => {

})
.onError( async (msgs, error) => {
  console.log(error)
})

// Client-side buffering (per-queue)
await queen
.queue('translations-queue')
.partition('x')
.buffer({ messageCount: 100, timeMillis: 100})  // Buffer for this queue
.push([
  { transactionId: 'xx', payload: {}}
])

// Manual buffer flush
await queen.queue('translations-queue').flushBuffer()  // Flush this queue's buffer
await queen.flushAllBuffers()                          // Flush all buffers across all queues

// Buffer statistics
const stats = queen.getBufferStats()
console.log(stats) // { activeBuffers: 2, totalBufferedMessages: 150, oldestBufferAge: 2500, flushesPerformed: 10 }

/**
 * Message structure
 * @typedef {Object} Message
 * @property {string} id - Message ID
 * @property {string} transactionId - Transaction ID (for idempotency)
 * @property {string} [traceId] - Optional trace ID (UUID)
 * @property {string} leaseId - Lease ID (for renewal)
 * @property {string} partitionId - Partition ID
 * @property {Object} payload - User data (YOUR PAYLOAD HERE)
 * @property {Date} createdAt - Message creation timestamp
 * @property {Date} leaseExpiresAt - Lease expiration timestamp
 */

// # Consume things

// Auto-ack (no callbacks) - simplest pattern
await queen
.queue('translations-queue')
.consume( async (msg) => {
  // If this completes successfully → auto ack(msg, true)
  // If this throws → auto ack(msg, false)
  const result = await processMessage(msg.payload)
  return result
}) // runs forever with auto-ack

// Manual ack (with error callback) - full control
await queen
.queue('translations-queue')
.consume( async (msg) => {
  // Process message
  return msg
})
.onError (async (msg, error) => {
  // Custom error handling - YOU must ack manually
  if (error.retryable) {
    await queen.ack(msg, 'retry')  // Retry later
  } else {
    await queen.ack(msg, false)  // Failed permanently
  }
}) // runs forever

// Manual ack with BOTH success and error callbacks
await queen
.queue('translations-queue')
.consume( async (msg) => {
  return processMessage(msg.payload)
})
.onSuccess(async (msg, result) => {
  // YOU must ack manually when onSuccess is defined
  await queen.ack(msg, true)
  // Log or do something with result
})
.onError(async (msg, error) => {
  // YOU must ack manually
  await queen.ack(msg, false)
})

// Explicit autoAck control
await queen
.queue('translations-queue')
.autoAck(false)  // Must ack manually in all cases
.consume( async (msg) => {
  const result = await processMessage(msg.payload)
  await queen.ack(msg, true)  // Manual ack required
  return result
})

// Batch with prefetch optimization
await queen
.queue('translations-queue')
.concurrency(10) // 10 workers
.batch(5)        // Fetch 5 messages per request (efficient network usage)
.each()          // But process them one at a time (simpler callback)
.consume( async (msg) => {
  // Called once per message, but fetched in batches of 5
  return msg
})

// Batch processing (process all 5 together)
await queen
.queue('translations-queue')
.concurrency(10) // 10 workers
.batch(5)        // Fetch AND process 5 messages together
.consume( async (msgs) => {
  // msgs is an array of 5 messages
  return msgs.map(m => processMessage(m.payload))
})

// Consumer group
await queen
.queue('translations-queue')
.group('analysys')
.concurrency(10)
.batch(5)
.consume( async (msg) => {
  return msg
})

// Consumer group with namespace/task filtering
// Note: Consumes from ALL queues matching the namespace/task filter
await queen
.queue()  // Empty queue() means "any queue matching filters"
.namespace('chat')
.task('agent')
.group('analysys')
.concurrency(10)
.batch(5)
.consume( async (msg) => {
  // Receives messages from any queue with namespace='chat' and task='agent'
  return msg
})

// Flow control with limit and idle timeout
await queen
.queue()
.namespace('chat')
.task('agent')
.group('analysys')
.renewLease(true, 1000)  // Auto-renew lease every 1000ms (milliseconds)
.concurrency(10)
.idleMillis(100000)  // Stop after 100 seconds of no messages (milliseconds)
.limit(500)          // Stop after processing 500 messages
.batch(5)
.consume( async (msg) => {
  return msg
})

// Transaction in consume context
await queen
.queue('translations-queue')
.partition('x')
.concurrency(10)
.batch(5)
.consume( async (msg) => {
  return msg
})
.onSuccess(async (msg) => {
  // Create transaction for atomic operations
  const tx = queen.transaction()
  await tx.ack(msg)
  await tx.queue('next').push([{ payload: msg.payload }])
  await tx.commit()  // Explicit commit required
})
.onError (async (msg, error) => {
  await queen.ack(msg, false)
})


// Manual stop with AbortController (Node.js standard pattern)
const controller = new AbortController()
await queen
  .queue('translations-queue')
  .concurrency(10)
  .batch(5)
  .consume(async (msg) => {
    // Process message
    return msg
  }, { signal: controller.signal })

// Later... stop the consumer gracefully
controller.abort()

// Alternative: Use limit/idleMillis for automatic stopping
await queen
  .queue('translations-queue')
  .limit(1000)         // Stop after 1000 messages
  .idleMillis(60000)   // Or stop after 60 seconds idle (milliseconds)
  .consume(async (msg) => {
    return msg
  })

// Subscription mode - replay from specific point (bus mode)
await queen
.queue('events')
.group('analytics')
.subscriptionMode('earliest')  // 'earliest', 'latest', or 'timestamp'
.consume(async (msg) => {
  // Process all events from the beginning
})

// Subscription from timestamp
await queen
.queue('events')
.group('reprocessing')
.subscriptionMode('timestamp')
.subscriptionFrom('2024-01-01T00:00:00Z')
.limit(10000)  // Process 10k messages then stop
.consume(async (msg) => {
  // Reprocess events from Jan 1st
})

// Manual transactions (outside consume context)
const tx = queen.transaction()
await tx.ack(msg1)
await tx.queue('next-queue').push([{ payload: processedData }])
await tx.queue('another-queue').push([{ payload: otherData }])
await tx.commit()

// Or fluent style:
await queen.transaction()
  .ack(msg1)
  .ack(msg2)
  .queue('next').push([data1])
  .queue('logs').push([data2])
  .commit()

// # Pop (just if people want)
const msgss = await queen
.queue('translations-queue')
.batch(10)
.pop()

await queen.ack(msgss)  // Manual ack

// Pop with wait (long polling)
const msgs = await queen
.queue('translations-queue')
.batch(10)
.wait(true)  // Wait for messages if queue is empty
.pop()

await queen.ack(msgs)

// Pop with auto-ack (acked at server on pop)
const autoAckedMsgs = await queen
.queue('translations-queue')
.batch(10)
.autoAck()  // Server acks immediately on pop
.pop()
// No need to ack - already done

// Manual lease renewal (for long-running processing)
const message = await queen.queue('tasks').pop()
// ... do some processing ...
await queen.renew(message)  // Extend the lease
// ... more processing ...
await queen.ack(message)

// Graceful shutdown
await queen.close()  // Flush buffers, stop workers, cleanup
// Also auto-triggered on SIGTERM/SIGKILL

// ============================================================================
// PREVENTING DOUBLE-ACK BUGS
// ============================================================================

// ❌ WRONG: Double ack in auto-ack mode
await queen.queue('tasks').consume(async msg => {
  await queen.ack(msg, true)  // Manual ack
  return msg  // Also triggers auto-ack → DOUBLE ACK!
})
// Solution: Client should detect and throw error:
// Error: Cannot manually ack in auto-ack mode. Set .autoAck(false) first.

// ✅ CORRECT: Explicit autoAck(false)
await queen.queue('tasks')
  .autoAck(false)  // Disable auto-ack
  .consume(async msg => {
    await queen.ack(msg, true)  // Now this is fine
    return msg
  })

// ============================================================================
// QUEUE CREATION - Must be explicit
// ============================================================================

// ❌ WRONG: Push to non-existent queue
await queen.queue('new-queue').push([{ payload: 'data' }])
// Error: Queue 'new-queue' does not exist. 
//        Create it first: await queen.queue('new-queue').create()

// ✅ CORRECT: Create queue first (idempotent)
await queen.queue('new-queue').create()  // Call multiple times - no problem
await queen.queue('new-queue').create()  // Still fine
await queen.queue('new-queue').push([{ payload: 'data' }])  // Now works

// ============================================================================
// MULTIPLE CONSUMERS - Creates multiple workers
// ============================================================================

// This creates 2 independent workers (not an error):
await queen.queue('tasks').consume(async msg => { /* worker 1 */ })
await queen.queue('tasks').consume(async msg => { /* worker 2 */ })
// Both will process messages from the same queue independently

/**
 * ============================================================================
 * COMPLETE FEATURE CHECKLIST - New Queen Client
 * ============================================================================
 * 
 * ✅ CLIENT INITIALIZATION
 * - new Queen(url)                                    // Simple string
 * - new Queen([url1, url2])                          // Array for load balancing
 * - new Queen({ urls, timeoutMillis, retryAttempts, retryDelayMillis, loadBalancingStrategy, enableFailover })
 * 
 * ✅ QUEUE CONFIGURATION
 * - .queue(name).create()                            // Simple create
 * - .queue(name).namespace(ns).task(t).create()      // With metadata
 * - .queue(name).config({ leaseTime, retryLimit, priority, delayedProcessing, windowBuffer, maxSize })
 * - .queue(name).delete()                            // Delete queue
 * 
 * ✅ PUSH OPERATIONS
 * - .queue(name).push([{ transactionId, payload }])
 * - .queue(name).push([{ transactionId, traceId, payload }])
 * - .queue(name).partition(name).push(...)           // To specific partition
 * - .queue(name).push(...).onDuplicate(callback)     // Duplicate handling
 * - .queue(name).push(...).onSuccess(callback)       // Success callback
 * - .queue(name).push(...).onError(callback)         // Error callback
 * 
 * ✅ CLIENT-SIDE BUFFERING (Per-Queue)
 * - .queue(name).buffer({ messageCount, timeMillis }).push(...)
 * - .queue(name).flushBuffer()                       // Manual flush this queue's buffer
 * - .flushAllBuffers()                               // Manual flush all buffers
 * - .getBufferStats()                                // Get buffer statistics
 * 
 * ✅ CONSUME OPERATIONS
 * - .queue(name).consume(async msg => {})            // Simple consume with auto-ack
 * - .queue(name).autoAck(false).consume(...)         // Explicit manual ack mode
 * - .queue(name).consume(...).onError(callback)      // Manual ack on error (auto on success)
 * - .queue(name).consume(...).onSuccess(callback)    // Manual ack on success AND error
 * - .queue(name).concurrency(n).consume(...)         // Parallel workers
 * - .queue(name).batch(n).consume(async msgs => {})  // Batch processing
 * - .queue(name).batch(n).each().consume(async msg => {}) // Prefetch optimization
 * 
 * ✅ CONSUMER GROUPS & FILTERING
 * - .queue(name).group(groupName).consume(...)       // Consumer group
 * - .queue().namespace(ns).task(t).consume(...)      // Consume from ANY queue matching filters
 * - .queue(name).partition(p).consume(...)           // Specific partition
 * 
 * ✅ FLOW CONTROL
 * - .limit(n).consume(...)                           // Consume N messages then stop
 * - .idleMillis(ms).consume(...)                     // Stop after idle timeout (milliseconds)
 * - .renewLease(true, intervalMillis).consume(...)   // Auto lease renewal (milliseconds)
 * - Manual stop: AbortController pattern
 *   const controller = new AbortController()
 *   await queen.queue('tasks').consume(async msg => {}, { signal: controller.signal })
 *   controller.abort()  // Stop gracefully
 * 
 * ✅ SUBSCRIPTION MODE (Bus Mode)
 * - .subscriptionMode('earliest').consume(...)       // Replay from beginning
 * - .subscriptionMode('latest').consume(...)         // Only new messages
 * - .subscriptionMode('timestamp').subscriptionFrom(date).consume(...)  // From timestamp
 * 
 * ✅ TRANSACTIONS
 * - .consume(...).onSuccess(async (msg) => {         // Transaction in consume
 *     const tx = queen.transaction()
 *     await tx.ack(msg)
 *     await tx.queue('next').push([...])
 *     await tx.commit()  // Explicit commit required
 *   })
 * - const tx = queen.transaction()                   // Manual transaction (outside consume)
 * - tx.ack(msg)                                      // Add ack to transaction
 * - tx.queue(name).push([...])                       // Add push to transaction
 * - await tx.commit()                                // Commit atomically
 * 
 * ✅ POP OPERATIONS (Manual fetch)
 * - .queue(name).pop()                               // Pop single message
 * - .queue(name).batch(n).pop()                      // Pop N messages
 * - .queue(name).wait(true).pop()                    // Long polling
 * - .queue(name).autoAck().pop()                     // Auto-ack on pop
 * 
 * ✅ ACKNOWLEDGMENT
 * - await queen.ack(msg)                             // Ack success (default true)
 * - await queen.ack(msg, true)                       // Explicit success
 * - await queen.ack(msg, false)                      // Failed
 * - await queen.ack(msg, 'retry')                    // Custom status
 * - await queen.ack([msg1, msg2])                    // Batch ack
 * 
 * ✅ LEASE RENEWAL
 * - await queen.renew(message)                       // Renew single message lease
 * - await queen.renew([msg1, msg2])                  // Renew multiple leases
 * 
 * ✅ RELIABILITY & CONFIGURATION
 * - timeoutMillis: Request timeout (milliseconds)
 * - retryAttempts: Number of retry attempts
 * - retryDelayMillis: Initial delay with exponential backoff (milliseconds)
 * - loadBalancingStrategy: 'round-robin' or 'session'
 * - enableFailover: Try other servers on failure
 * 
 * ✅ GRACEFUL SHUTDOWN
 * - await queen.close()                              // Manual shutdown
 * - Auto-triggered on SIGTERM/SIGKILL                // Automatic cleanup
 * 
 * ✅ MESSAGE STRUCTURE (TypeScript/JSDoc)
 * @typedef {Object} Message
 * @property {string} id - Message ID
 * @property {string} transactionId - Transaction ID
 * @property {string} [traceId] - Optional trace ID
 * @property {string} leaseId - Lease ID
 * @property {string} partitionId - Partition ID
 * @property {Object} payload - User data
 * @property {Date} createdAt - Creation timestamp
 * @property {Date} leaseExpiresAt - Lease expiration
 * 
 * ============================================================================
 * AUTO-ACK SEMANTICS
 * ============================================================================
 * 
 * Default (autoAck = true, no callbacks):
 *   → Success (no throw) = auto ack(msg, true)
 *   → Throw = auto ack(msg, false)
 * 
 * With .autoAck(false):
 *   → YOU must ack manually in ALL cases
 *   → Client throws error if you try to manually ack in autoAck(true) mode
 * 
 * With onError callback (autoAck still true):
 *   → Success = auto ack(msg, true)
 *   → Error = YOU must ack manually in onError
 * 
 * With onSuccess callback:
 *   → Success = YOU must ack manually in onSuccess
 *   → Error = YOU must ack manually in onError
 * 
 * ============================================================================
 * LONG POLLING
 * ============================================================================
 * 
 * - consume(): Always long polls (default true, configurable to false if needed)
 * - pop(): Default false, configurable with .wait(true)
 * 
 * ============================================================================
 * DESIGN DECISIONS & IMPROVEMENTS
 * ============================================================================
 * 
 * 1. CONSISTENT API: .queue() everywhere (not .queen())
 * 2. PER-QUEUE BUFFERING: .queue(name).buffer() for clarity
 * 3. EXPLICIT TRANSACTIONS: Always create tx in onSuccess, always call commit()
 * 4. EXPLICIT AUTO-ACK: .autoAck(false) to disable, with error on manual ack in auto mode
 * 5. PREFETCH OPTIMIZATION: .batch(5).each() for network efficiency + simple callbacks
 * 6. NAMESPACE CONSUMPTION: .queue().namespace(ns).task(t) consumes from ALL matching queues
 * 7. IDEMPOTENT CREATE: .create() can be called multiple times safely
 * 8. MULTIPLE WORKERS: Multiple .consume() calls create independent workers (not an error)
 * 9. HELPFUL ERRORS: "Queue doesn't exist" → suggest .create()
 * 10. DOUBLE-ACK PREVENTION: Client detects and throws error on manual ack in auto mode
 * 11. ABORT CONTROLLER: Standard Node.js pattern for manual stop
 * 12. SMART DEFAULTS: See "DEFAULT VALUES" section at top of file
 * 
 * ============================================================================
 * DEFAULT VALUES SUMMARY (see top of file for complete reference)
 * ============================================================================
 * 
 * TIME UNIT CONVENTIONS:
 *   - Properties with "Millis" suffix: milliseconds (timeoutMillis, idleMillis, etc.)
 *   - Properties with "Seconds" suffix: seconds (retentionSeconds, etc.)
 *   - Properties without suffix but time-related: seconds (leaseTime, delayedProcessing, etc.)
 * 
 * Client:
 *   - timeoutMillis: 30000 (30s, milliseconds)
 *   - retryAttempts: 3
 *   - retryDelayMillis: 1000 (1s, milliseconds, exponential backoff)
 *   - loadBalancingStrategy: 'round-robin'
 *   - enableFailover: true
 * 
 * Queue (all times in SECONDS):
 *   - leaseTime: 300 (5 min, seconds)
 *   - retryLimit: 3
 *   - priority: 0
 *   - delayedProcessing: 0 (seconds)
 *   - windowBuffer: 0 (seconds)
 *   - maxSize: 10000
 *   - retentionSeconds: 0 (seconds)
 *   - completedRetentionSeconds: 0 (seconds)
 * 
 * Consume:
 *   - concurrency: 1
 *   - batch: 1
 *   - autoAck: true
 *   - wait: true (long polling enabled)
 *   - timeoutMillis: 30000 (30s, milliseconds)
 *   - limit: null (run forever)
 *   - idleMillis: null (no idle timeout, milliseconds)
 *   - renewLeaseIntervalMillis: null (milliseconds)
 * 
 * Pop:
 *   - batch: 1
 *   - wait: false (no long polling)
 *   - timeoutMillis: 30000 (30s, milliseconds)
 *   - autoAck: false (manual ack required)
 * 
 * Buffer:
 *   - messageCount: 100
 *   - timeMillis: 1000 (1s, milliseconds)
 * 
 * ============================================================================
 */
