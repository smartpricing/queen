#!/usr/bin/env node

/**
 * Cache Testing with Multiple Servers
 * 
 * This script helps test if cache is causing issues when using multiple servers.
 * It sends requests alternately to different servers to check cache consistency.
 * 
 * Usage:
 * 1. Start two Queen servers on different ports:
 *    PORT=6632 node src/server.js
 *    PORT=6633 node src/server.js
 * 
 * 2. Run this test:
 *    node examples/test-cache-multi-server.js
 */

import { createQueenClient, LoadBalancingStrategy } from '../src/client/index.js';

const servers = [
  'http://localhost:6632',
  'http://localhost:6633'
];

const TEST_QUEUE = 'cache-test';
const TEST_NAMESPACE = 'cache-verification';

// Helper to log with server info
const log = (message, server = null) => {
  const timestamp = new Date().toISOString().split('T')[1].slice(0, -1);
  const serverInfo = server ? ` [${server}]` : '';
  console.log(`${timestamp}${serverInfo} | ${message}`);
};

async function testCacheConsistency() {
  console.log('=' .repeat(80));
  console.log('Testing Cache Consistency Across Multiple Servers');
  console.log('=' .repeat(80));
  
  // Create client with ROUND_ROBIN to alternate between servers
  const roundRobinClient = createQueenClient({
    baseUrls: servers,
    loadBalancingStrategy: LoadBalancingStrategy.ROUND_ROBIN,
    enableFailover: false // Disable failover to see actual server responses
  });
  
  // Create client with SESSION to stick to one server
  const sessionClient = createQueenClient({
    baseUrls: servers,
    loadBalancingStrategy: LoadBalancingStrategy.SESSION,
    enableFailover: false
  });
  
  try {
    // Step 1: Configure queue on both servers via round-robin
    log('Step 1: Configuring queue on both servers...');
    
    for (let i = 0; i < servers.length; i++) {
      await roundRobinClient.configure({
        queue: TEST_QUEUE,
        namespace: TEST_NAMESPACE,
        task: 'cache-test',
        options: {
          maxRetries: 3,
          retryDelay: 1000,
          priority: 5
        }
      });
      const stats = roundRobinClient.getLoadBalancerStats();
      log(`Configuration sent to server ${i + 1}`, servers[i]);
    }
    
    // Step 2: Push messages alternating between servers
    log('\nStep 2: Pushing messages to alternate servers...');
    
    for (let i = 1; i <= 6; i++) {
      const result = await roundRobinClient.push({
        items: [{
          queue: TEST_QUEUE,
          partition: 'Default',
          payload: { 
            messageId: i,
            server: `push-${i % 2 === 0 ? 2 : 1}`,
            timestamp: Date.now()
          }
        }]
      });
      
      const stats = roundRobinClient.getLoadBalancerStats();
      log(`Message ${i} pushed`, servers[(i - 1) % servers.length]);
    }
    
    // Step 3: Check queue stats from both servers
    log('\nStep 3: Checking queue stats from each server...');
    
    for (let i = 0; i < servers.length; i++) {
      try {
        const analytics = await roundRobinClient.analytics.queue(TEST_QUEUE);
        log(`Queue stats from server ${i + 1}: ${JSON.stringify(analytics)}`, servers[i]);
      } catch (error) {
        log(`Failed to get stats from server ${i + 1}: ${error.message}`, servers[i]);
      }
    }
    
    // Step 4: Pop messages from different servers
    log('\nStep 4: Popping messages from different servers...');
    
    // Pop from server 1 (using session client for consistency)
    const sessionClient1 = createQueenClient({
      baseUrl: servers[0],
      enableFailover: false
    });
    
    const pop1 = await sessionClient1.pop({
      queue: TEST_QUEUE,
      batch: 3,
      wait: false
    });
    
    log(`Server 1 returned ${pop1.messages?.length || 0} messages`, servers[0]);
    if (pop1.messages && pop1.messages.length > 0) {
      for (const msg of pop1.messages) {
        log(`  - Message: ${JSON.stringify(msg.payload)}`, servers[0]);
        await sessionClient1.ack(msg.transactionId, 'completed');
      }
    }
    
    // Pop from server 2
    const sessionClient2 = createQueenClient({
      baseUrl: servers[1],
      enableFailover: false
    });
    
    const pop2 = await sessionClient2.pop({
      queue: TEST_QUEUE,
      batch: 3,
      wait: false
    });
    
    log(`Server 2 returned ${pop2.messages?.length || 0} messages`, servers[1]);
    if (pop2.messages && pop2.messages.length > 0) {
      for (const msg of pop2.messages) {
        log(`  - Message: ${JSON.stringify(msg.payload)}`, servers[1]);
        await sessionClient2.ack(msg.transactionId, 'completed');
      }
    }
    
    // Step 5: Test resource cache consistency
    log('\nStep 5: Testing resource cache consistency...');
    
    // Update configuration on server 1
    const updateClient1 = createQueenClient({
      baseUrl: servers[0],
      enableFailover: false
    });
    
    await updateClient1.configure({
      queue: TEST_QUEUE,
      namespace: TEST_NAMESPACE,
      task: 'cache-test-updated',
      options: {
        maxRetries: 5, // Changed from 3
        retryDelay: 2000, // Changed from 1000
        priority: 10 // Changed from 5
      }
    });
    
    log('Configuration updated on server 1', servers[0]);
    
    // Wait a moment for potential cache propagation
    await new Promise(resolve => setTimeout(resolve, 1000));
    
    // Check if server 2 sees the update
    log('Checking if configuration changes are visible on both servers...');
    
    // Push a message to server 2 and see if it uses new or old config
    const testClient2 = createQueenClient({
      baseUrl: servers[1],
      enableFailover: false
    });
    
    await testClient2.push({
      items: [{
        queue: TEST_QUEUE,
        partition: 'CacheTest',
        payload: { 
          test: 'cache-verification',
          expectedConfig: 'Should use database config, not cached'
        }
      }]
    });
    
    log('Test message pushed to server 2', servers[1]);
    
    // Pop and check the message properties
    const verifyPop = await testClient2.pop({
      queue: TEST_QUEUE,
      partition: 'CacheTest',
      wait: false
    });
    
    if (verifyPop.messages && verifyPop.messages.length > 0) {
      const msg = verifyPop.messages[0];
      log(`Message from server 2: priority=${msg.priority}, retryCount=${msg.retryCount}`, servers[1]);
      
      // Check queue configuration directly to verify cache consistency
      const queueStats = await testClient2.analytics.queue(TEST_QUEUE);
      log(`Queue config from server 2: ${JSON.stringify(queueStats)}`, servers[1]);
      
      // Priority is included in the message and should reflect the updated value
      if (msg.priority === 10) {
        log('✅ Cache consistency verified! Server 2 sees updated priority configuration', servers[1]);
      } else {
        log(`⚠️  Potential cache issue: Server 2 may be using cached configuration (priority=${msg.priority} instead of 10)`, servers[1]);
      }
      
      await testClient2.ack(msg.transactionId, 'completed');
    }
    
    // Step 6: Clean up
    log('\nStep 6: Cleaning up test data...');
    
    // Clear the queue on both servers
    await roundRobinClient.queues.clear(TEST_QUEUE);
    log('Test queue cleared');
    
    console.log('\n' + '=' .repeat(80));
    log('✅ Cache consistency test completed!');
    console.log('=' .repeat(80));
    
    // Print summary
    console.log('\nSummary:');
    console.log('- If both servers returned the same messages, they share the same database');
    console.log('- If configuration changes were visible on both servers, cache is properly invalidated');
    console.log('- If you see cache issues, check the resourceCache implementation in queueManagerOptimized.js');
    
  } catch (error) {
    log(`❌ Test failed: ${error.message}`);
    console.error(error);
    process.exit(1);
  }
}

// Run the test
testCacheConsistency().catch(console.error);
