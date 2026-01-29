/**
 * Test: Window Buffer with Staggered Push (Debounce Behavior)
 * 
 * This test verifies window buffer DEBOUNCE behavior:
 * - Push message 1 at T=0
 * - Wait 1 second
 * - Push message 2 at T=1 (this RESETS the window!)
 * - Window buffer is 3 seconds
 * 
 * Expected behavior (DEBOUNCE):
 * - Window buffer waits until 3 seconds after the LAST message
 * - Message 2 pushed at T=1, so window expires at T=1+3=T=4
 * - BOTH messages are released together at T=4
 * 
 * This is different from delayedProcessing which delays each message individually!
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'test-window-buffer-staggered'

// Delete to start fresh
console.log('Deleting queue to start fresh...')
await queen.queue(queueName).delete()
await new Promise(r => setTimeout(r, 500))

// Create queue with 3 second window buffer
console.log('Creating queue with 3 second window buffer...')
const createResult = await queen
  .queue(queueName)
  .config({
    windowBuffer: 3  // Hold messages for 3 seconds before they become available
  })
  .create()

console.log('Queue created with windowBuffer:', createResult.options?.windowBuffer)

const startTime = Date.now()
const elapsed = () => ((Date.now() - startTime) / 1000).toFixed(1)

// Push message 1
console.log(`\n[${elapsed()}s] Pushing message 1...`)
await queen.queue(queueName).push([
  { data: { id: 1, message: 'First message' } }
])
console.log(`[${elapsed()}s] Message 1 pushed (will be available at ~3s)`)

// Wait 1 second
console.log(`[${elapsed()}s] Waiting 1 second...`)
await new Promise(r => setTimeout(r, 1000))

// Push message 2
console.log(`[${elapsed()}s] Pushing message 2...`)
await queen.queue(queueName).push([
  { data: { id: 2, message: 'Second message' } }
])
console.log(`[${elapsed()}s] Message 2 pushed (will be available at ~4s)`)

// Start consuming
console.log(`\n[${elapsed()}s] Starting consume() with batch(100)...`)
console.log('Timeline (DEBOUNCE behavior):')
console.log('  - Message 2 pushed at T=1s resets the window')
console.log('  - Window expires at T=1+3=T=4s')
console.log('  - BOTH messages should arrive together at ~T=4s\n')

let batchCount = 0
let totalMessages = 0

await queen
  .queue(queueName)
  .batch(100)  // Request up to 100 messages per batch
  .limit(2)    // Stop after receiving 2 messages total
  .autoAck(false)
  .consume(async (messages) => {
    batchCount++
    console.log(`[${elapsed()}s] >>> Batch #${batchCount}: Received ${messages.length} message(s)`)
    
    for (const msg of messages) {
      totalMessages++
      console.log(`         Message: ${JSON.stringify(msg.data)}`)
    }
    
    return messages
  })
  .onSuccess(async (messages) => {
    await queen.ack(messages, true)
    console.log(`         Acknowledged ${messages.length} message(s)`)
  })
  .onError(async (messages, error) => {
    console.error('Error:', error.message)
    await queen.ack(messages, false)
  })

// Summary
console.log('\n=== SUMMARY ===')
console.log(`Total batches: ${batchCount}`)
console.log(`Total messages: ${totalMessages}`)

if (batchCount === 1 && totalMessages === 2) {
  console.log('SUCCESS: Both messages arrived in 1 batch (DEBOUNCE working!)')
  console.log('')
  console.log('Window buffer works like debounce:')
  console.log('- Waits until N seconds after the LAST message')
  console.log('- Then releases ALL accumulated messages together')
} else if (batchCount === 2 && totalMessages === 2) {
  console.log('ISSUE: Messages arrived in SEPARATE batches')
  console.log('This suggests window buffer is not using debounce semantics.')
} else {
  console.log('RESULT: Unexpected behavior')
}

// Cleanup
await queen.close()
console.log('\nDone!')
