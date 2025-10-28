/**
 * Example 01: Basic Usage
 * 
 * This example demonstrates the fundamental operations:
 * - Creating a queue
 * - Pushing messages
 * - Consuming messages with auto-ack
 * - Manual pop and ack
 */

import { Queen } from '../client-js/client-v2/index.js'

const queen = new Queen('http://localhost:6632')

const queueName = 'basic-example'

console.log('Creating queue...')
await queen.queue(queueName).create()

// Push a single message
console.log('Pushing message...')
await queen.queue(queueName).push([
  { data: { id: 1, message: 'Hello from Queen!' } }
])

// Method 1: Consume with auto-ack
console.log('\nMethod 1: Consuming with auto-ack...')
await queen
  .queue(queueName)
  .limit(1)  // Process just 1 message then stop
  .consume(async (message) => {
    console.log('Received:', message.data)
    // Automatically acked on success!
  })

// Push another message for Method 2
await queen.queue(queueName).push([
  { data: { id: 2, message: 'Another message' } }
])

// Method 2: Manual pop and ack
console.log('\nMethod 2: Manual pop and ack...')
const messages = await queen.queue(queueName).pop()

if (messages.length > 0) {
  const message = messages[0]
  console.log('Popped:', message.data)
  
  // Process the message
  console.log('Processing...')
  
  // Manually acknowledge
  await queen.ack(message, true)
  console.log('Acknowledged!')
}

// Cleanup
console.log('\nCleaning up...')
await queen.close()
console.log('Done!')
