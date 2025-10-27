/**
 * Simple test for new Queen client
 */

import { Queen } from './client-js/client-v2/index.js'

const q = new Queen('http://localhost:6632')

// ============================================================================
// WITHOUT CALLBACKS - Throws on error (normal behavior)
// ============================================================================

try {
    await q.queue('test-alice').config({ retryLimit: 5 }).create()
    console.log('✅ Queue created successfully')
} catch (error) {
    console.log('❌ Queue creation failed (caught):', error.message)
    // Server is down, so we'll stop here for this demo
    process.exit(1)
}

// ============================================================================
// WITH CALLBACKS - Does NOT throw, calls callbacks instead
// ============================================================================

// Example 1: Create with callbacks (doesn't throw even if server is down)
await q.queue('test-alice-2')
.config({ retryLimit: 5 })
.create()
.onSuccess(async (result) => {
    console.log('✅ Queue created:', result)
})
.onError(async (error) => {
    console.log('❌ Queue creation failed:', error.message)
})

// Example 2: Push with callbacks
await q.queue('test-alice')
.partition('0')
.push([{
    transactionId: 'alice-02',
    payload: { id: 89 }
}])
.onDuplicate(async (items, error) => {
    console.log('⚠️  Duplicate detected:', items.map(i => i.transactionId))
})
.onSuccess(async (items) => {
    console.log('✅ Successfully pushed:', items.map(i => i.transactionId))
})
.onError(async (items, error) => {
    console.log('❌ Failed to push:', error.message)
})

await q.queue('test-alice')
.partition('0')
.push([{
    payload: { id: 90 }
}])
.onDuplicate(async (items, error) => {
    console.log('⚠️  Duplicate detected:', items.map(i => i.transactionId))
})
.onSuccess(async (items) => {
    console.log('✅ Successfully pushed:', items.map(i => i.transactionId))
})
.onError(async (items, error) => {
    console.log('❌ Failed to push:', error.message)
})

// Example 3: Delete with callbacks
await q.queue('test-alice-3')
.delete()
.onSuccess(async (result) => {
    console.log('✅ Queue deleted')
})
.onError(async (error) => {
    console.log('❌ Delete failed:', error.message)
})

// Example 4: Consume with callbacks (manual ack required)
await q.queue('test-alice')
.limit(1)  // Just consume 1 message for testing
.each()
.consume(async (msg) => {
    console.log('Processing message:', msg)
    return msg  // Your processing logic here
})
.onSuccess(async (msg, result) => {
    console.log('✅ Successfully processed:', msg)
    await q.ack(msg, true)  // Manual ack required when using callbacks
})
.onError(async (msg, error) => {
    console.log('❌ Processing failed:', error.message)
    await q.ack(msg, false)  // Manual ack on error
})

// Example 5: Push a message for the consumer group to receive
await q.queue('test-alice')
.partition('0')
.push([{
    payload: { id: 91, message: 'For consumer group' }
}])

// Example 6: Consume with consumer group (will receive messages from now on)
await q.queue('test-alice')
.group('test-group')
.limit(1)  // Just consume 1 message for testing
.each()
.consume(async (msg) => {
    console.log('Processing group message:', msg)
    return msg  // Your processing logic here
})
.onSuccess(async (msg, result) => {
    console.log('✅ Successfully group processed:', msg.transactionId)
    await q.ack(msg, true, { group: 'test-group' })  // Manual ack with group context
})
.onError(async (msg, error) => {
    console.log('❌ Processing failed:', error.message)
    await q.ack(msg, false, { group: 'test-group' })  // Manual ack on error
})

// OR: Use subscriptionMode to replay from beginning
await q.queue('test-alice')
.group('replay-group')
.subscriptionMode('now')  // Replay ALL messages from the beginning
.limit(10)
.each()
.consume(async (msg) => {
    console.log('Replaying message:', msg.transactionId, msg.data)
})
.onSuccess(async (msg) => {
    console.log('✅ Replayed:', msg.transactionId)
    await q.ack(msg, true, { group: 'replay-group' })
})