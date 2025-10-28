/**
 * Example 13: Client Logging
 * 
 * This example demonstrates:
 * - Enabling detailed client logging
 * - Understanding log output
 * - Debugging with logs
 * - Log levels and verbosity
 */

import { Queen } from '../client-js/client-v2/index.js'

console.log('='.repeat(70))
console.log('Example 13: Client Logging')
console.log('='.repeat(70))

console.log('\n📝 By default, Queen client logging is DISABLED')
console.log('To enable logging, set the QUEEN_CLIENT_LOG environment variable:')
console.log('   export QUEEN_CLIENT_LOG=true')
console.log('   node examples/13-logging.js')

console.log('\n--- Running with current logging settings ---\n')

// Check if logging is enabled
const loggingEnabled = process.env.QUEEN_CLIENT_LOG === 'true'
console.log(`Logging enabled: ${loggingEnabled ? '✅ YES' : '❌ NO'}`)

if (!loggingEnabled) {
  console.log('\n💡 TIP: Run this example with logging enabled to see detailed output:')
  console.log('   QUEEN_CLIENT_LOG=true node examples/13-logging.js\n')
}

const queen = new Queen('http://localhost:6632')
console.log('Created Queen client')

const queueName = 'logging-example'

// Operation 1: Queue creation
console.log('\n--- Creating Queue ---')
await queen.queue(queueName).create()
console.log('Queue created')

// Operation 2: Push with buffering
console.log('\n--- Pushing with Buffering ---')
for (let i = 1; i <= 5; i++) {
  await queen
    .queue(queueName)
    .buffer({ messageCount: 10, timeMillis: 1000 })
    .push([{ data: { id: i, timestamp: Date.now() } }])
}
console.log('Pushed 5 messages (buffered)')

// Operation 3: Flush buffers
console.log('\n--- Flushing Buffers ---')
await queen.flushAllBuffers()
console.log('Buffers flushed')

// Operation 4: Pop messages
console.log('\n--- Popping Messages ---')
const messages = await queen
  .queue(queueName)
  .batch(5)
  .wait(true)
  .pop()
console.log(`Popped ${messages.length} messages`)

// Operation 5: Acknowledge
console.log('\n--- Acknowledging Messages ---')
await queen.ack(messages, true)
console.log('Messages acknowledged')

// Operation 6: Transaction
console.log('\n--- Transaction Example ---')
await queen.queue(queueName).push([
  { data: { id: 100, value: 'test' } }
])

const [msg] = await queen.queue(queueName).pop()
await queen
  .transaction()
  .ack(msg)
  .queue('output-queue')
  .push([{ data: { processed: true } }])
  .commit()
console.log('Transaction committed')

// Operation 7: Consumer with error
console.log('\n--- Consumer with Error Handling ---')
await queen.queue(queueName).push([
  { data: { id: 200, shouldFail: false } },
  { data: { id: 201, shouldFail: true } }
])

let processedCount = 0
await queen
  .queue(queueName)
  .limit(2)
  .consume(async (message) => {
    processedCount++
    console.log(`Processing message ${message.data.id}`)
    
    if (message.data.shouldFail) {
      throw new Error('Simulated failure')
    }
  })
  .onError(async (message, error) => {
    console.log(`Error: ${error.message}`)
  })

console.log(`Processed ${processedCount} messages`)

// Demonstrate what logs show
console.log('\n='.repeat(70))
console.log('What You\'ll See in Logs (when enabled):')
console.log('='.repeat(70))

console.log(`
When QUEEN_CLIENT_LOG=true, you'll see detailed logs like:

[2025-10-28T10:30:45.123Z] [INFO] [Queen.constructor] {"status":"initialized","urls":1}
  ↳ Client initialized with 1 URL

[2025-10-28T10:30:45.234Z] [INFO] [QueueBuilder.create] {"queue":"logging-example"}
  ↳ Queue creation requested

[2025-10-28T10:30:45.345Z] [INFO] [QueueBuilder.push] {"queue":"logging-example","partition":"Default","count":1,"buffered":true}
  ↳ Message added to buffer

[2025-10-28T10:30:45.456Z] [INFO] [BufferManager.addMessage] {"queueAddress":"logging-example/Default","messageCount":1}
  ↳ Buffer now contains 1 message

[2025-10-28T10:30:46.567Z] [INFO] [BufferManager.flushBuffer] {"queueAddress":"logging-example/Default","messageCount":5}
  ↳ Flushing 5 buffered messages

[2025-10-28T10:30:46.678Z] [INFO] [HttpClient.request] {"method":"POST","url":"http://localhost:6632/api/v1/push","hasBody":true}
  ↳ HTTP request sent

[2025-10-28T10:30:46.789Z] [INFO] [HttpClient.response] {"method":"POST","url":"http://localhost:6632/api/v1/push","status":200}
  ↳ HTTP response received

[2025-10-28T10:30:46.890Z] [INFO] [QueueBuilder.pop] {"queue":"logging-example","batch":5,"wait":true}
  ↳ Pop operation started

[2025-10-28T10:30:47.123Z] [INFO] [Queen.ack] {"type":"batch","count":5}
  ↳ Batch acknowledgment

[2025-10-28T10:30:47.234Z] [INFO] [TransactionBuilder.commit] {"operationCount":2}
  ↳ Transaction with 2 operations

[2025-10-28T10:30:47.345Z] [ERROR] [ConsumerManager.processMessage] {"transactionId":"...","error":"Simulated failure"}
  ↳ Error during message processing

[2025-10-28T10:30:47.456Z] [INFO] [Queen.close] {"status":"completed"}
  ↳ Client shutdown complete
`)

console.log('='.repeat(70))
console.log('Log Components:')
console.log('='.repeat(70))
console.log(`
1. Timestamp: ISO 8601 format with milliseconds
2. Level: INFO, WARN, or ERROR
3. Component: Where the log came from (e.g., Queen, HttpClient, BufferManager)
4. Context: JSON object with relevant details

This helps you:
- Debug connection issues (HttpClient logs)
- Understand message flow (QueueBuilder logs)
- Monitor performance (timing between operations)
- Troubleshoot errors (ERROR level logs)
- Verify buffer behavior (BufferManager logs)
`)

// Cleanup
console.log('--- Cleanup ---')
await queen.close()
console.log('✅ Done!')

console.log('\n💡 Remember: Disable logging in production or use it selectively')
console.log('   Logs can impact performance and generate large log files')
