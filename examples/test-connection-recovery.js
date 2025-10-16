/**
 * Test connection recovery during take operations
 * This demonstrates how the client handles server disconnections gracefully
 */

import { Queen } from '../src/client/index.js';

async function testConnectionRecovery() {
  const client = new Queen({
    baseUrl: 'http://localhost:6632',
    retryDelay: 1000,  // 1 second base retry delay
    retryAttempts: 3
  });

  try {
    // Configure a test queue
    await client.queue('test-recovery-queue');
    console.log('Queue configured');

    // Push some test messages
    await client.push('test-recovery-queue', [
      { message: 'Test message 1' },
      { message: 'Test message 2' },
      { message: 'Test message 3' }
    ]);
    console.log('Pushed 3 test messages');

    console.log('\nStarting consumer...');
    console.log('To test: Stop the server after receiving some messages');
    console.log('Expected behavior: Client will retry with exponential backoff indefinitely (max 30s between retries)');
    console.log('                   When server comes back online, it will automatically reconnect\n');

    let messageCount = 0;

    // Consume messages with takeBatch
    for await (const batch of client.takeBatch('test-recovery-queue', { 
      batch: 10, 
      wait: true,
      timeout: 5000 
    })) {
      console.log(`\nReceived batch of ${batch.length} messages:`);
      
      for (const message of batch) {
        messageCount++;
        console.log(`  ${messageCount}. ${JSON.stringify(message.payload)}`);
        
        // Acknowledge the message
        await client.ack(message, true);
      }
    }

    console.log('\nConsumer loop completed');
    console.log(`Total messages processed: ${messageCount}`);

  } catch (error) {
    console.error('\nError in consumer:', error.message);
    console.error('Stack:', error.stack);
  } finally {
    await client.close();
    console.log('\nClient closed');
  }
}

console.log('=== Connection Recovery Test ===\n');
testConnectionRecovery().catch(console.error);

