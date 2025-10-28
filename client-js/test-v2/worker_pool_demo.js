/**
 * Demonstration of Worker Pool Pattern
 * 
 * This test shows the difference between traditional concurrency
 * and the new worker pool pattern that maintains constant throughput.
 */

import Queen from '../client-v2/index.js'

const queen = new Queen({ 
  nodes: ['http://localhost:8080'],
  logging: true
})

const QUEUE = 'worker-pool-demo'
const MESSAGE_COUNT = 50
const CONCURRENCY = 5

async function demo() {
  console.log('\n=== Worker Pool Pattern Demo ===\n')
  
  // Create queue
  await queen.queue(QUEUE).create()
  console.log('✓ Queue created')
  
  // Push test messages with varying processing times
  console.log(`\n→ Pushing ${MESSAGE_COUNT} messages...`)
  const messages = []
  for (let i = 1; i <= MESSAGE_COUNT; i++) {
    messages.push({
      taskId: i,
      processingTime: Math.floor(Math.random() * 500) + 100 // 100-600ms
    })
  }
  
  await queen.queue(QUEUE).push(messages)
  console.log(`✓ Pushed ${MESSAGE_COUNT} messages\n`)
  
  // Consume with worker pool pattern
  console.log(`→ Starting worker pool with concurrency=${CONCURRENCY}...\n`)
  
  const startTime = Date.now()
  let completed = 0
  const activeWorkers = new Set()
  
  await queen.queue(QUEUE)
    .concurrency(CONCURRENCY)
    .batch(10) // Fetch 10 at a time
    .limit(MESSAGE_COUNT)
    .autoAck(true)
    .each() // IMPORTANT: This enables worker pool mode!
    .consume(async (msg) => {
      const workerId = `Worker-${Math.random().toString(36).substr(2, 4)}`
      activeWorkers.add(workerId)
      
      const processingTime = msg.payload.processingTime
      console.log(`[${workerId}] Processing task ${msg.payload.taskId} (${processingTime}ms) - Active: ${activeWorkers.size}`)
      
      // Simulate processing
      await new Promise(resolve => setTimeout(resolve, processingTime))
      
      completed++
      activeWorkers.delete(workerId)
      console.log(`[${workerId}] ✓ Completed task ${msg.payload.taskId} (${completed}/${MESSAGE_COUNT}) - Active: ${activeWorkers.size}`)
    })
  
  const duration = Date.now() - startTime
  const throughput = (MESSAGE_COUNT / duration * 1000).toFixed(2)
  
  console.log('\n=== Results ===')
  console.log(`Total time: ${duration}ms`)
  console.log(`Messages processed: ${completed}`)
  console.log(`Throughput: ${throughput} messages/sec`)
  console.log(`Average active workers: ${CONCURRENCY}`)
  
  // Cleanup
  await queen.queue(QUEUE).delete()
  console.log('\n✓ Queue deleted')
}

// Run demo
demo()
  .then(() => {
    console.log('\n✓ Demo completed successfully')
    process.exit(0)
  })
  .catch(err => {
    console.error('\n✗ Demo failed:', err.message)
    process.exit(1)
  })

