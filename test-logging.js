#!/usr/bin/env node

/**
 * Test script to verify logging functionality
 * This will test push, pop, ack, retry, and other state-changing operations
 */

import { QueenClient } from './src/client/queenClient.js';

const client = new QueenClient({
  host: 'localhost',
  port: 6632
});

async function testLogging() {
  console.log('🧪 Testing Queen logging functionality...\n');
  
  try {
    // Test 1: Push messages
    console.log('📤 Test 1: Pushing messages...');
    const pushResult = await client.push([
      { queue: 'test-queue', partition: 'test-partition', payload: { test: 'message1' } },
      { queue: 'test-queue', partition: 'test-partition', payload: { test: 'message2' } },
      { queue: 'test-queue', partition: 'test-partition', payload: { test: 'message3' } }
    ]);
    console.log('Push result:', pushResult);
    console.log('✅ Check server logs for PUSH operation\n');
    
    // Wait a bit
    await new Promise(resolve => setTimeout(resolve, 1000));
    
    // Test 2: Pop messages
    console.log('📥 Test 2: Popping messages...');
    const popResult = await client.pop({ 
      queue: 'test-queue', 
      partition: 'test-partition' 
    }, { batch: 2 });
    console.log('Pop result:', popResult);
    console.log('✅ Check server logs for POP operation\n');
    
    // Wait a bit
    await new Promise(resolve => setTimeout(resolve, 1000));
    
    // Test 3: Acknowledge message (completed)
    if (popResult.messages.length > 0) {
      console.log('✔️ Test 3: Acknowledging message as completed...');
      const ackResult = await client.ack(
        popResult.messages[0].transactionId,
        'completed'
      );
      console.log('Ack result:', ackResult);
      console.log('✅ Check server logs for ACK operation\n');
    }
    
    // Wait a bit
    await new Promise(resolve => setTimeout(resolve, 1000));
    
    // Test 4: Acknowledge message (failed)
    if (popResult.messages.length > 1) {
      console.log('❌ Test 4: Acknowledging message as failed...');
      const ackResult = await client.ack(
        popResult.messages[1].transactionId,
        'failed',
        'Test error message'
      );
      console.log('Ack result:', ackResult);
      console.log('✅ Check server logs for ACK and RETRY_SCHEDULED operations\n');
    }
    
    // Wait a bit
    await new Promise(resolve => setTimeout(resolve, 1000));
    
    // Test 5: Clear queue
    console.log('🗑️ Test 5: Clearing queue...');
    const clearResult = await client.request('DELETE', '/messages/clear', {
      queue: 'test-queue',
      partition: 'test-partition'
    });
    console.log('Clear result:', clearResult);
    console.log('✅ Check server logs for CLEAR_QUEUE operation\n');
    
    // Test 6: Push with namespace/task and pop with filters
    console.log('🏷️ Test 6: Testing filtered operations...');
    await client.request('POST', '/configure', {
      queue: 'filtered-queue',
      namespace: 'test-namespace',
      task: 'test-task'
    });
    
    await client.push([
      { queue: 'filtered-queue', payload: { test: 'filtered-message' } }
    ]);
    
    const filteredPop = await client.request('POST', '/pop', {
      namespace: 'test-namespace'
    });
    console.log('Filtered pop result:', filteredPop);
    console.log('✅ Check server logs for POP_FILTERED operation\n');
    
    console.log('✨ All tests completed! Check server logs for detailed operation logging.');
    
  } catch (error) {
    console.error('Test error:', error);
  }
}

// Run the tests
testLogging().catch(console.error);
