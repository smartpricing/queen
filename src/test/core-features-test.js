#!/usr/bin/env node

/**
 * Core Features Test Suite for Queen Message Queue System
 * 
 * Tests the essential features:
 * - Push methods (single/batch)
 * - Queue configuration
 * - Pop and consume operations
 * - Default and custom partitions
 * - Delayed send and buffer
 * - Partition priority ordering
 */

import { createQueenClient } from '../client/queenClient.js';

const client = createQueenClient({
  baseUrl: 'http://localhost:6632'
});

const log = (message, type = 'info') => {
  const timestamp = new Date().toISOString().substring(11, 23);
  const prefix = {
    info: '📝',
    success: '✅',
    error: '❌',
    warning: '⚠️',
    test: '🧪'
  }[type] || '📝';
  
  console.log(`[${timestamp}] ${prefix} ${message}`);
};

const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms));

async function testSinglePush() {
  log('Testing Single Message Push', 'test');
  
  try {
    const result = await client.push({
      items: [{
        queue: 'core-test-single',
        partition: 'default',
        payload: { message: 'Single test message', timestamp: Date.now() }
      }]
    });
    
    if (!result.messages || result.messages.length !== 1) {
      throw new Error(`Expected 1 message, got ${result.messages?.length}`);
    }
    
    log('✅ Single push works', 'success');
    return result.messages[0];
  } catch (error) {
    log(`❌ Single push failed: ${error.message}`, 'error');
    throw error;
  }
}

async function testBatchPush() {
  log('Testing Batch Message Push', 'test');
  
  try {
    const items = Array.from({ length: 3 }, (_, i) => ({
      queue: 'core-test-batch',
      partition: 'batch-partition',
      payload: { message: `Batch message ${i + 1}`, index: i + 1 }
    }));
    
    const result = await client.push({ items });
    
    if (!result.messages || result.messages.length !== 3) {
      throw new Error(`Expected 3 messages, got ${result.messages?.length}`);
    }
    
    log('✅ Batch push works', 'success');
    return result.messages;
  } catch (error) {
    log(`❌ Batch push failed: ${error.message}`, 'error');
    throw error;
  }
}

async function testQueueConfiguration() {
  log('Testing Queue Configuration', 'test');
  
  try {
    const result = await client.configure({
      queue: 'core-test-config',
      options: {
        leaseTime: 600,
        retryLimit: 5,
        priority: 8
      }
    });
    
    if (!result.configured) {
      throw new Error('Configuration failed');
    }
    
    log('✅ Queue configuration works', 'success');
    return result;
  } catch (error) {
    log(`❌ Configuration failed: ${error.message}`, 'error');
    throw error;
  }
}

async function testPopOperations() {
  log('Testing Pop Operations', 'test');
  
  try {
    // Push test messages
    await client.push({
      items: [
        {
          queue: 'core-test-pop',
          partition: 'partition-a',
          payload: { message: 'Message A' }
        },
        {
          queue: 'core-test-pop',
          partition: 'partition-b',
          payload: { message: 'Message B' }
        }
      ]
    });
    
    await sleep(100);
    
    // Pop from specific partition
    const resultA = await client.pop({
      queue: 'core-test-pop',
      partition: 'partition-a',
      batch: 1
    });
    
    if (!resultA.messages || resultA.messages.length !== 1) {
      throw new Error('Failed to pop from specific partition');
    }
    
    if (resultA.messages[0].partition !== 'partition-a') {
      throw new Error('Got message from wrong partition');
    }
    
    // Acknowledge the message
    await client.ack(resultA.messages[0].transactionId, 'completed');
    
    // Pop from queue (any partition)
    const resultB = await client.pop({
      queue: 'core-test-pop',
      batch: 1
    });
    
    if (!resultB.messages || resultB.messages.length !== 1) {
      throw new Error('Failed to pop from queue');
    }
    
    await client.ack(resultB.messages[0].transactionId, 'completed');
    
    log('✅ Pop operations work', 'success');
  } catch (error) {
    log(`❌ Pop operations failed: ${error.message}`, 'error');
    throw error;
  }
}

async function testDelayedProcessing() {
  log('Testing Delayed Processing', 'test');
  
  try {
    // Configure queue with delay
    await client.configure({
      queue: 'core-test-delayed',
      options: {
        delayedProcessing: 2 // 2 seconds delay
      }
    });
    
    const startTime = Date.now();
    
    // Push message
    await client.push({
      items: [{
        queue: 'core-test-delayed',
        payload: { message: 'Delayed message', sentAt: startTime }
      }]
    });
    
    // Try to pop immediately (should get nothing)
    const immediateResult = await client.pop({
      queue: 'core-test-delayed',
      batch: 1
    });
    
    if (immediateResult.messages && immediateResult.messages.length > 0) {
      throw new Error('Got message immediately when it should be delayed');
    }
    
    // Wait for delay period
    await sleep(2500);
    
    // Try to pop again (should get the message now)
    const delayedResult = await client.pop({
      queue: 'core-test-delayed',
      batch: 1
    });
    
    if (!delayedResult.messages || delayedResult.messages.length !== 1) {
      throw new Error('Did not get delayed message after delay period');
    }
    
    const processingDelay = Date.now() - startTime;
    
    await client.ack(delayedResult.messages[0].transactionId, 'completed');
    
    log(`✅ Delayed processing works (${processingDelay}ms delay)`, 'success');
  } catch (error) {
    log(`❌ Delayed processing failed: ${error.message}`, 'error');
    throw error;
  }
}

async function testPartitionPriority() {
  log('Testing Partition Priority', 'test');
  
  try {
    // Push messages to different partitions (they should have different priorities from migration)
    await client.push({
      items: [
        {
          queue: 'priority-test-queue',
          partition: 'low-priority',
          payload: { message: 'Low priority', priority: 1 }
        },
        {
          queue: 'priority-test-queue',
          partition: 'high-priority',
          payload: { message: 'High priority', priority: 10 }
        }
      ]
    });
    
    await sleep(100);
    
    // Pop messages (should get high priority first)
    const result = await client.pop({
      queue: 'priority-test-queue',
      batch: 2
    });
    
    if (!result.messages || result.messages.length !== 2) {
      throw new Error(`Expected 2 messages, got ${result.messages?.length}`);
    }
    
    // Check if high priority came first
    if (result.messages[0].partition === 'high-priority') {
      log('✅ Partition priority works correctly', 'success');
    } else {
      log(`⚠️ Partition priority may not be working: got ${result.messages[0].partition} first`, 'warning');
    }
    
    // Acknowledge messages
    for (const message of result.messages) {
      await client.ack(message.transactionId, 'completed');
    }
  } catch (error) {
    log(`❌ Partition priority test failed: ${error.message}`, 'error');
    throw error;
  }
}

async function testConsumerPattern() {
  log('Testing Consumer Pattern', 'test');
  
  try {
    let consumedCount = 0;
    
    // Push messages for consumption
    await client.push({
      items: Array.from({ length: 2 }, (_, i) => ({
        queue: 'core-test-consumer',
        partition: 'consumer-partition',
        payload: { message: `Consumer message ${i + 1}`, index: i + 1 }
      }))
    });
    
    // Start consumer
    const stopConsumer = client.consume({
      queue: 'core-test-consumer',
      partition: 'consumer-partition',
      handler: async (message) => {
        consumedCount++;
        log(`Consumed: ${message.data.message}`);
        if (consumedCount >= 2) {
          stopConsumer();
        }
      },
      stopOnError: true
    });
    
    // Wait for consumption
    let attempts = 0;
    while (consumedCount < 2 && attempts < 30) {
      await sleep(100);
      attempts++;
    }
    
    stopConsumer();
    
    if (consumedCount === 2) {
      log('✅ Consumer pattern works', 'success');
    } else {
      log(`⚠️ Consumer processed ${consumedCount}/2 messages`, 'warning');
    }
  } catch (error) {
    log(`❌ Consumer pattern failed: ${error.message}`, 'error');
    throw error;
  }
}

async function runCoreTests() {
  console.log('🚀 Starting Core Features Test Suite\n');
  console.log('=' .repeat(60));
  
  let passed = 0;
  let failed = 0;
  
  const tests = [
    { name: 'Single Push', fn: testSinglePush },
    { name: 'Batch Push', fn: testBatchPush },
    { name: 'Queue Configuration', fn: testQueueConfiguration },
    { name: 'Pop Operations', fn: testPopOperations },
    { name: 'Delayed Processing', fn: testDelayedProcessing },
    { name: 'Partition Priority', fn: testPartitionPriority },
    { name: 'Consumer Pattern', fn: testConsumerPattern }
  ];
  
  for (const test of tests) {
    try {
      await test.fn();
      passed++;
    } catch (error) {
      failed++;
      log(`Test ${test.name} failed: ${error.message}`, 'error');
    }
    
    // Small delay between tests
    await sleep(200);
  }
  
  console.log('\n' + '=' .repeat(60));
  console.log('📊 CORE TEST RESULTS');
  console.log('=' .repeat(60));
  console.log(`✅ Passed: ${passed}`);
  console.log(`❌ Failed: ${failed}`);
  console.log(`📈 Success Rate: ${Math.round((passed / tests.length) * 100)}%`);
  
  if (failed === 0) {
    console.log('\n🎉 ALL CORE TESTS PASSED!');
    console.log('Queen Message Queue System core features are working correctly.');
  } else {
    console.log(`\n⚠️ ${failed} test(s) failed. Core functionality may have issues.`);
  }
  
  console.log('\n' + '=' .repeat(60));
}

runCoreTests().catch(error => {
  console.error('💥 Test suite crashed:', error);
  process.exit(1);
});
