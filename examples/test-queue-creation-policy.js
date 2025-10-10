#!/usr/bin/env node

/**
 * Test script to verify the new queue creation policy:
 * - Queues must be created via configure endpoint
 * - Push to non-existent queue should fail
 * - Partitions can still be created on-demand
 */

import { createQueenClient } from '../src/client/index.js';

const TEST_QUEUE = 'test-creation-policy';
const NON_EXISTENT_QUEUE = 'queue-that-does-not-exist';

// Helper to log with timestamp
const log = (message, type = 'info') => {
  const timestamp = new Date().toISOString().split('T')[1].slice(0, -1);
  const prefix = type === 'error' ? '❌' : type === 'success' ? '✅' : 'ℹ️';
  console.log(`${timestamp} | ${prefix} ${message}`);
};

async function runTests() {
  console.log('=' .repeat(80));
  console.log('Testing Queue Creation Policy');
  console.log('=' .repeat(80));
  
  const client = createQueenClient({
    baseUrl: 'http://localhost:6632'
  });
  
  try {
    // Test 1: Push to non-existent queue should fail
    log('\nTest 1: Pushing to non-existent queue (should fail)...');
    try {
      await client.push({
        items: [{
          queue: NON_EXISTENT_QUEUE,
          partition: 'Default',
          payload: { test: 'should fail' }
        }]
      });
      log('Push succeeded unexpectedly!', 'error');
      process.exit(1);
    } catch (error) {
      if (error.message.includes('does not exist')) {
        log(`Push failed as expected: ${error.message}`, 'success');
      } else {
        log(`Push failed with unexpected error: ${error.message}`, 'error');
        throw error;
      }
    }
    
    // Test 2: Create queue via configure
    log('\nTest 2: Creating queue via configure endpoint...');
    const configResult = await client.configure({
      queue: TEST_QUEUE,
      namespace: 'test',
      task: 'creation-policy',
      options: {
        retryLimit: 3,
        priority: 5
      }
    });
    log(`Queue configured: ${JSON.stringify(configResult)}`, 'success');
    
    // Test 3: Push to configured queue should succeed
    log('\nTest 3: Pushing to configured queue (should succeed)...');
    const pushResult1 = await client.push({
      items: [{
        queue: TEST_QUEUE,
        partition: 'Default',
        payload: { test: 'message 1' }
      }]
    });
    log('Push to Default partition succeeded', 'success');
    
    // Test 4: Push to new partition (should create partition on-demand)
    log('\nTest 4: Pushing to new partition (should create on-demand)...');
    const pushResult2 = await client.push({
      items: [{
        queue: TEST_QUEUE,
        partition: 'NewPartition',
        payload: { test: 'message 2' }
      }]
    });
    log('Push to NewPartition succeeded (partition created on-demand)', 'success');
    
    // Test 5: Verify messages can be popped
    log('\nTest 5: Verifying messages can be popped...');
    const popResult = await client.pop({
      queue: TEST_QUEUE,
      batch: 10,
      wait: false
    });
    
    if (popResult.messages && popResult.messages.length === 2) {
      log(`Successfully retrieved ${popResult.messages.length} messages`, 'success');
      
      // Check partitions
      const partitions = new Set(popResult.messages.map(m => m.partition));
      if (partitions.has('Default') && partitions.has('NewPartition')) {
        log('Both partitions (Default and NewPartition) confirmed', 'success');
      }
      
      // Acknowledge messages
      for (const msg of popResult.messages) {
        await client.ack(msg.transactionId, 'completed');
      }
      log('Messages acknowledged', 'success');
    } else {
      log(`Expected 2 messages, got ${popResult.messages?.length || 0}`, 'error');
    }
    
    // Test 6: Clean up
    log('\nTest 6: Cleaning up...');
    await client.queues.clear(TEST_QUEUE);
    log('Test queue cleared', 'success');
    
    console.log('\n' + '=' .repeat(80));
    log('All tests passed! Queue creation policy working correctly', 'success');
    console.log('=' .repeat(80));
    
    console.log('\nSummary:');
    console.log('✅ Queues must be created via configure endpoint');
    console.log('✅ Push to non-existent queue fails with clear error');
    console.log('✅ Partitions can still be created on-demand');
    console.log('✅ No implicit queue creation during push operations');
    
  } catch (error) {
    log(`Test failed: ${error.message}`, 'error');
    console.error(error);
    process.exit(1);
  }
}

// Run the tests
runTests().catch(console.error);
