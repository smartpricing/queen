#!/usr/bin/env node
import { createQueenClient } from './src/client/index.js';

const client = createQueenClient({
  baseUrl: 'http://localhost:6632',
  timeout: 30000,
  retryAttempts: 3
});

const TEST_NS = 'simple-test';
const TEST_TASK = 'demo';
const TEST_QUEUE = 'messages';

const runSimpleTest = async () => {
  console.log('Simple Queen Client Test\n');
  
  try {
    // 1. Push a message
    console.log('1. Pushing a message...');
    const pushResult = await client.push({
      items: [{
        ns: TEST_NS,
        task: TEST_TASK,
        queue: TEST_QUEUE,
        payload: { message: 'Hello!', timestamp: Date.now() }
      }]
    });
    console.log(`   ✅ Pushed message with transaction ID: ${pushResult.messages[0].transactionId}\n`);
    
    // 2. Pop the message
    console.log('2. Popping the message...');
    const popResult = await client.pop({
      ns: TEST_NS,
      task: TEST_TASK,
      queue: TEST_QUEUE,
      wait: false,
      batch: 1
    });
    
    if (popResult.messages.length > 0) {
      const msg = popResult.messages[0];
      console.log(`   ✅ Got message: ${JSON.stringify(msg.payload)}`);
      console.log(`   Transaction ID: ${msg.transactionId}\n`);
      
      // 3. Acknowledge the message
      console.log('3. Acknowledging the message...');
      await client.ack(msg.transactionId, 'completed');
      console.log('   ✅ Message acknowledged\n');
    } else {
      console.log('   No messages available\n');
    }
    
    console.log('Simple test completed! 🎉');
    
  } catch (error) {
    console.error('Test failed:', error.message);
    if (error.cause) {
      console.error('Cause:', error.cause);
    }
    process.exit(1);
  }
};

runSimpleTest();
