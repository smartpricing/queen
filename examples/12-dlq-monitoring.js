/**
 * Example: Dead Letter Queue (DLQ) Monitoring
 * 
 * This example demonstrates how to:
 * 1. Configure a queue with DLQ enabled
 * 2. Monitor failed messages in the DLQ
 * 3. Query DLQ with various filters
 */

import { Queen } from '../client-js/client-v2/index.js'

const client = new Queen({ baseUrl: 'http://localhost:6632' })

async function main() {
  // 1. Create a queue with DLQ enabled
  console.log('Creating queue with DLQ enabled...')
  await client
    .queue('payment-processing')
    .config({
      retryLimit: 3,
      dlqAfterMaxRetries: true,  // Auto-move to DLQ after max retries
      maxWaitTimeSeconds: 3600   // Move to DLQ if not processed within 1 hour
    })
    .create()

  // 2. Simulate some failed messages
  console.log('\nPushing test messages...')
  await client
    .queue('payment-processing')
    .push([
      { data: { orderId: '12345', amount: 100 } },
      { data: { orderId: '12346', amount: 200 } }
    ])

  // 3. Consume with failures
  console.log('\nProcessing messages (some will fail)...')
  await client
    .queue('payment-processing')
    .batch(10)
    .limit(10)
    .each()
    .consume(async (msg) => {
      // Simulate random failures
      if (msg.payload.orderId === '12346') {
        throw new Error('Payment gateway timeout')
      }
      console.log(`✓ Processed order ${msg.payload.orderId}`)
    })
    .onError(async (msg, error) => {
      console.log(`✗ Failed order ${msg.payload.orderId}: ${error.message}`)
      await client.ack(msg, false, { error: error.message })
    })

  // Wait for DLQ processing
  await new Promise(resolve => setTimeout(resolve, 1000))

  // 4. Query DLQ messages
  console.log('\n=== Checking Dead Letter Queue ===')
  
  const dlqResult = await client
    .queue('payment-processing')
    .dlq()
    .limit(10)
    .get()

  console.log(`\nFound ${dlqResult.total} message(s) in DLQ`)
  
  if (dlqResult.messages.length > 0) {
    console.log('\nDLQ Messages:')
    dlqResult.messages.forEach((msg, i) => {
      console.log(`\n${i + 1}. Order ${msg.data.orderId}`)
      console.log(`   Error: ${msg.errorMessage}`)
      console.log(`   Retry Count: ${msg.retryCount}`)
      console.log(`   Failed At: ${msg.movedToDlqAt}`)
      console.log(`   Transaction ID: ${msg.transactionId}`)
    })
  }

  // 5. Query with consumer group filter
  console.log('\n=== Querying DLQ for specific consumer group ===')
  
  const groupDlqResult = await client
    .queue('payment-processing')
    .dlq('my-worker-group')  // Specific consumer group
    .limit(10)
    .get()

  console.log(`Found ${groupDlqResult.total} message(s) for consumer group 'my-worker-group'`)

  // 6. Query with date range
  console.log('\n=== Querying DLQ with date range ===')
  
  const today = new Date()
  const yesterday = new Date(today.getTime() - 24 * 60 * 60 * 1000)
  
  const dateDlqResult = await client
    .queue('payment-processing')
    .dlq()
    .from(yesterday.toISOString())
    .to(today.toISOString())
    .limit(10)
    .get()

  console.log(`Found ${dateDlqResult.total} message(s) in the last 24 hours`)

  // 7. Pagination example
  console.log('\n=== Pagination Example ===')
  
  const page1 = await client
    .queue('payment-processing')
    .dlq()
    .limit(5)
    .offset(0)
    .get()

  console.log(`Page 1: ${page1.messages.length} messages (Total: ${page1.total})`)

  const page2 = await client
    .queue('payment-processing')
    .dlq()
    .limit(5)
    .offset(5)
    .get()

  console.log(`Page 2: ${page2.messages.length} messages`)

  console.log('\n✅ DLQ monitoring example completed!')
}

main().catch(console.error)

