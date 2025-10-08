#!/usr/bin/env node

/**
 * Simple test for partition priority within a queue
 */

import { createQueenClient } from './src/client/queenClient.js';

const client = createQueenClient({
  baseUrl: 'http://localhost:6632'
});

async function testPartitionPriority() {
  console.log('🧪 Testing Partition Priority Implementation...\n');

  try {
    // Create messages in different partitions with different priorities
    console.log('📋 Creating messages in different priority partitions...');
    
    // Create messages with timestamps to ensure order
    const baseTime = Date.now();
    
    // Low priority partition (created first, should be processed last)
    await client.push({
      items: [{
        queue: 'priority-test-queue',
        partition: 'low-priority',
        payload: { 
          message: 'Low priority message', 
          priority: 1,
          timestamp: baseTime 
        }
      }]
    });

    // High priority partition (created second, should be processed first)
    await client.push({
      items: [{
        queue: 'priority-test-queue',
        partition: 'high-priority',
        payload: { 
          message: 'High priority message', 
          priority: 10,
          timestamp: baseTime + 1000 
        }
      }]
    });

    // Medium priority partition (created third, should be processed second)
    await client.push({
      items: [{
        queue: 'priority-test-queue',
        partition: 'medium-priority',
        payload: { 
          message: 'Medium priority message', 
          priority: 5,
          timestamp: baseTime + 2000 
        }
      }]
    });

    console.log('✅ Test messages pushed to partitions\n');

    // Wait a moment for messages to be stored
    await new Promise(resolve => setTimeout(resolve, 200));

    // Pop all messages from the queue (should get them in priority order)
    console.log('🔍 Popping messages from queue (testing partition priority)...');
    
    const result = await client.pop({ 
      queue: 'priority-test-queue',
      batch: 3
    });
    
    if (result.messages && result.messages.length > 0) {
      console.log('📊 Partition Priority Results:');
      console.log('Expected order: High (10) → Medium (5) → Low (1)');
      console.log('Actual order:');
      
      result.messages.forEach((message, index) => {
        console.log(`  ${index + 1}. Partition: ${message.partition} (Priority: ${message.priority}) - ${message.data.message}`);
      });

      // Check priority ordering
      let priorityCorrect = true;
      for (let i = 1; i < result.messages.length; i++) {
        const prevPriority = result.messages[i-1].priority || 0;
        const currPriority = result.messages[i].priority || 0;
        
        if (currPriority > prevPriority) {
          priorityCorrect = false;
          console.log(`❌ Priority violation: message ${i+1} has higher priority (${currPriority}) than message ${i} (${prevPriority})`);
        }
      }
      
      if (priorityCorrect) {
        console.log('\n✅ Partition priority test PASSED! Messages processed in correct priority order.');
      } else {
        console.log('\n❌ Partition priority test FAILED! Messages not processed in priority order.');
      }

      // Acknowledge messages
      for (const message of result.messages) {
        await client.ack(message.transactionId, 'completed');
      }
    } else {
      console.log('⚠️  No messages retrieved for partition priority test.');
    }

  } catch (error) {
    console.error('❌ Test failed with error:', error.message);
    console.error('Stack:', error.stack);
  }
}

// Run test
async function runTest() {
  console.log('🚀 Starting Partition Priority Test\n');
  console.log('Make sure the Queen server is running on localhost:6632\n');
  
  await testPartitionPriority();
  
  console.log('\n🏁 Test completed!');
}

runTest().catch(console.error);
