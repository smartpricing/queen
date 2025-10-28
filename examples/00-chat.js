/**
 * Example 00: Chat Processing Pipeline
 * 
 * This example demonstrates a real-world chat processing system:
 * - Messages flow through translation -> agent queues
 * - Partitions ensure per-chat ordering
 * - Continuous processing with error handling
 * - Transaction-based pipeline for reliability
 */

import { Queen } from '../client-js/client-v2/index.js'
import { v4 as uuid } from 'uuid'

const queen = new Queen('http://localhost:6632')

const queueTranslations = 'chat-translations'
const queueAgent = 'chat-agent'

console.log('Creating queues...')
await queen
  .queue(queueTranslations)
  .config({ leaseTime: 300 })
  .create()

await queen
  .queue(queueAgent)
  .config({ leaseTime: 300 })
  .create()

console.log('Queues created!')
console.log('Starting chat processing pipeline...\n')

// Producer: Generate chat messages
let messageCount = 0
const produce = async () => {
  while (true) {
    const chatId = uuid()
    
    await queen
      .queue(queueTranslations)
      .partition(chatId)  // Partition by chat ID for ordering
      .push([{
        transactionId: uuid(),
        data: {
          chatId: chatId,
          event: 'translation-processing',
          message: `Chat message ${messageCount}`,
          timestamp: Date.now()
        }
      }])
    
    console.log(`📤 Produced message ${messageCount} for chat ${chatId.slice(0, 8)}...`)
    messageCount++
    
    await new Promise(resolve => setTimeout(resolve, 5000))
  }
}

// Stage 1: Translation processing
const translationProcessor = async () => {
  console.log('🌍 Translation processor started\n')
  
  await queen
    .queue(queueTranslations)
    .autoAck(false)  // Manual ack for transaction
    .consume(async (message) => {
      console.log(`🌍 Translating chat ${message.data.chatId.slice(0, 8)}...`)
      
      // Simulate translation
      const translated = {
        ...message.data,
        event: 'agent-processing',
        translated: true,
        language: 'en'
      }
      
      // Transaction: ack translation, push to agent queue
      await queen
        .transaction()
        .ack(message)
        .queue(queueAgent)
        .push([{
          data: translated
        }])
        .commit()
      
      console.log(`  ✅ Translated and forwarded to agent queue`)
    })
    .onError(async (message, error) => {
      console.error(`  ❌ Translation error: ${error.message}`)
      await queen.ack(message, false)  // Retry
    })
}

// Stage 2: Agent processing
const agentProcessor = async () => {
  console.log('🤖 Agent processor started\n')
  
  await queen
    .queue(queueAgent)
    .consume(async (message) => {
      console.log(`🤖 Agent processing chat ${message.data.chatId.slice(0, 8)}...`)
      
      // Simulate agent response
      console.log(`  💬 Message: "${message.data.message}"`)
      console.log(`  🌍 Translated: ${message.data.translated}`)
      console.log(`  ✅ Agent response sent`)
    })
    .onError(async (message, error) => {
      console.error(`  ❌ Agent error: ${error.message}`)
      // Auto-nack on error
    })
}

// Start all processes
console.log('Starting all processors...\n')
produce().catch(console.error)
translationProcessor().catch(console.error)
agentProcessor().catch(console.error)

// Graceful shutdown
process.on('SIGINT', async () => {
  console.log('\n\nShutting down gracefully...')
  await queen.close()
  process.exit(0)
})
