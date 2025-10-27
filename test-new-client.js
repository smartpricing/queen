/**
 * Simple test for new Queen client
 */

import { Queen } from './client-js/client-v2/index.js'

async function main() {
  console.log('🚀 Testing new Queen client...\n')

  // Create client
  const queen = new Queen('http://localhost:6632')
  console.log('✅ Client created')

  // Create queue
  console.log('\n📝 Creating queue...')
  await queen.queue('test-queue-v2').create()
  console.log('✅ Queue created')

  // Push a message
  console.log('\n📤 Pushing message...')
  await queen.queue('test-queue-v2').push([
    { payload: { message: 'Hello from new client!', timestamp: Date.now() } }
  ])
  console.log('✅ Message pushed')

  // Pop a message
  console.log('\n📥 Popping message...')
  const messages = await queen.queue('test-queue-v2').batch(1).pop()
  console.log(`✅ Received ${messages.length} message(s)`)
  if (messages.length > 0) {
    console.log('   Message:', messages[0].payload)
  }

  // Ack the message
  if (messages.length > 0) {
    console.log('\n✔️  Acknowledging message...')
    await queen.ack(messages[0], true)
    console.log('✅ Message acknowledged')
  }

  // Test buffer stats
  console.log('\n📊 Buffer stats:')
  console.log(queen.getBufferStats())

  // Test transaction
  console.log('\n⚡ Testing transaction...')
  await queen.queue('test-queue-v2').push([
    { payload: { message: 'Transaction test 1' } },
    { payload: { message: 'Transaction test 2' } }
  ])

  const txMessages = await queen.queue('test-queue-v2').batch(2).pop()
  if (txMessages.length === 2) {
    const tx = queen.transaction()
    tx.ack(txMessages)
    tx.queue('test-queue-v2').push([{ payload: { message: 'Processed!' } }])
    await tx.commit()
    console.log('✅ Transaction committed')
  }

  // Cleanup
  console.log('\n🧹 Closing client...')
  await queen.close()

  console.log('\n🎉 All tests passed!\n')
  process.exit(0)
}

main().catch(error => {
  console.error('\n❌ Test failed:', error)
  process.exit(1)
})

