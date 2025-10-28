/**
 * Example: Client Logging
 * 
 * This example demonstrates the client-side logging feature.
 * 
 * To enable logging, set the QUEEN_CLIENT_LOG environment variable:
 *   QUEEN_CLIENT_LOG=true node examples/13-logging.js
 * 
 * Without the env var, no logs will be produced (default behavior).
 */

import { Queen } from '../client-js/client-v2/index.js'

async function main() {
  console.log('='.repeat(60))
  console.log('Queen Client V2 - Logging Example')
  console.log('='.repeat(60))
  
  const loggingEnabled = process.env.QUEEN_CLIENT_LOG === 'true'
  console.log(`\nLogging is ${loggingEnabled ? 'ENABLED' : 'DISABLED'}`)
  
  if (!loggingEnabled) {
    console.log('\nTo enable detailed operation logging, run:')
    console.log('  QUEEN_CLIENT_LOG=true node examples/13-logging.js\n')
  } else {
    console.log('\nDetailed operation logs will appear below:\n')
  }

  try {
    // Initialize client (logs initialization)
    const queen = new Queen('http://localhost:6632')
    
    // Create queue (logs queue creation)
    console.log('\n1. Creating queue...')
    await queen.queue('logging-demo').create()
    
    // Push messages (logs push operation)
    console.log('\n2. Pushing messages...')
    await queen.queue('logging-demo').push([
      { payload: { message: 'First message' } },
      { payload: { message: 'Second message' } },
      { payload: { message: 'Third message' } }
    ])
    
    // Pop messages (logs pop operation and HTTP requests)
    console.log('\n3. Popping messages...')
    const messages = await queen.queue('logging-demo').batch(3).pop()
    console.log(`Received ${messages.length} messages`)
    
    // Acknowledge messages (logs ack operations)
    if (messages.length > 0) {
      console.log('\n4. Acknowledging messages...')
      await queen.ack(messages, true)
    }
    
    // Get buffer stats (logs stats retrieval)
    console.log('\n5. Getting buffer stats...')
    const stats = queen.getBufferStats()
    console.log('Stats:', stats)
    
    // Clean up (logs queue deletion and client shutdown)
    console.log('\n6. Cleaning up...')
    await queen.queue('logging-demo').delete()
    await queen.close()
    
    console.log('\n' + '='.repeat(60))
    console.log('Example completed successfully!')
    console.log('='.repeat(60))
    
    if (loggingEnabled) {
      console.log('\nAll operations were logged with:')
      console.log('  - ISO 8601 timestamp')
      console.log('  - Operation name (Component.method)')
      console.log('  - Relevant context (queue name, counts, etc.)')
      console.log('\nUse these logs for:')
      console.log('  - Debugging issues')
      console.log('  - Performance monitoring')
      console.log('  - Audit trails')
      console.log('  - Understanding client behavior')
    }
    
  } catch (error) {
    console.error('\nError:', error.message)
    console.error('Stack:', error.stack)
    process.exit(1)
  }
}

main()

