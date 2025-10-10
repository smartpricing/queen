#!/usr/bin/env node

/**
 * Test the minimalist Queen client interface
 */

import { Queen } from '../src/client/client.js';

async function main() {
  console.log('🧪 Testing Minimalist Queen Client\n');
  
  // Create client instance
  const client = new Queen({
    baseUrls: ['http://localhost:6632'],
    timeout: 30000,
    retryAttempts: 3,
    retryDelay: 1000
  });
  
  const testQueue = 'test-minimal-' + Date.now();
  
  try {
    // 1. Configure queue
    console.log('1️⃣ Configuring queue...');
    await client.queue(testQueue, {
      leaseTime: 300,
      retryLimit: 3,
      retryDelay: 1000
    });
    console.log('✅ Queue configured:', testQueue);
    
    // 2. Push single message
    console.log('\n2️⃣ Pushing single message...');
    await client.push(testQueue, {
      id: 1,
      message: 'Hello, Queen!',
      timestamp: new Date().toISOString()
    });
    console.log('✅ Single message pushed');
    
    // 3. Push batch of messages
    console.log('\n3️⃣ Pushing batch of messages...');
    const batch = [
      { id: 2, message: 'Message 2' },
      { id: 3, message: 'Message 3' },
      { id: 4, message: 'Message 4' }
    ];
    await client.push(testQueue, batch);
    console.log('✅ Batch of 3 messages pushed');
    
    // 4. Push to specific partition
    console.log('\n4️⃣ Pushing to urgent partition...');
    await client.push(`${testQueue}/urgent`, {
      id: 5,
      message: 'Urgent message!',
      priority: 'high'
    });
    console.log('✅ Message pushed to urgent partition');
    
    // 5. Take messages with limit
    console.log('\n5️⃣ Taking 3 messages...');
    let count = 0;
    for await (const message of client.take(testQueue, { limit: 3 })) {
      count++;
      console.log(`  📩 Message ${count}:`, message.data);
      await client.ack(message);
    }
    console.log('✅ Took and acknowledged 3 messages');
    
    // 6. Take from specific partition
    console.log('\n6️⃣ Taking from urgent partition...');
    for await (const message of client.take(`${testQueue}/urgent`, { limit: 1 })) {
      console.log('  🚨 Urgent message:', message.data);
      await client.ack(message, true);
    }
    console.log('✅ Processed urgent message');
    
    // 7. Test failure acknowledgment
    console.log('\n7️⃣ Testing failure acknowledgment...');
    await client.push(testQueue, { id: 6, message: 'Will fail' });
    
    for await (const message of client.take(testQueue, { limit: 1 })) {
      console.log('  ❌ Simulating failure for:', message.data);
      await client.ack(message, false, { error: 'Simulated error' });
    }
    console.log('✅ Failed acknowledgment sent');
    
    // 8. Test consumer groups (if needed)
    console.log('\n8️⃣ Testing consumer groups...');
    await client.push(testQueue, { id: 7, message: 'For consumer group' });
    
    for await (const message of client.take(`${testQueue}@test-group`, { limit: 1 })) {
      console.log('  👥 Message via consumer group:', message.data);
      await client.ack(message, true, { group: 'test-group' });
    }
    console.log('✅ Consumer group test complete');
    
    console.log('\n🎉 All tests passed!');
    
  } catch (error) {
    console.error('\n❌ Test failed:', error.message);
    process.exit(1);
  } finally {
    await client.close();
  }
}

// Run tests
main().catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});
