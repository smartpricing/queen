#!/usr/bin/env node

/**
 * Multi-Server Load Balancing Test
 * 
 * This script tests the Queen client with multiple servers using different
 * load balancing strategies (ROUND_ROBIN and SESSION).
 * 
 * Usage:
 * 1. Start two Queen servers on different ports:
 *    PORT=6632 node src/server.js
 *    PORT=6633 node src/server.js
 * 
 * 2. Run this test:
 *    node examples/multi-server-test.js
 */

import { createQueenClient, LoadBalancingStrategy } from '../src/client/index.js';

const servers = [
  'http://localhost:6632',
  'http://localhost:6633'
];

// Test configuration
const TEST_QUEUE = 'multi-server-test';
const TEST_NAMESPACE = 'load-balancing';

// Helper to log with timestamp
const log = (message, type = 'info') => {
  const timestamp = new Date().toISOString();
  const prefix = type === 'error' ? '❌' : type === 'success' ? '✅' : 'ℹ️';
  console.log(`[${timestamp}] ${prefix} ${message}`);
};

// Test Round Robin strategy
async function testRoundRobinStrategy() {
  log('Testing ROUND_ROBIN load balancing strategy', 'info');
  
  const client = createQueenClient({
    baseUrls: servers,
    loadBalancingStrategy: LoadBalancingStrategy.ROUND_ROBIN,
    enableFailover: true
  });
  
  // Configure the queue
  await client.configure({
    queue: TEST_QUEUE,
    namespace: TEST_NAMESPACE,
    task: 'round-robin-test',
    options: {
      maxRetries: 3,
      retryDelay: 1000
    }
  });
  
  // Push multiple messages to see round-robin distribution
  const messages = [];
  for (let i = 1; i <= 10; i++) {
    messages.push({
      queue: TEST_QUEUE,
      partition: 'Default',
      payload: { 
        id: i, 
        strategy: 'round-robin',
        timestamp: Date.now() 
      }
    });
  }
  
  log(`Pushing ${messages.length} messages with ROUND_ROBIN strategy`);
  const pushResult = await client.push({ items: messages });
  log(`Pushed messages: ${JSON.stringify(pushResult)}`, 'success');
  
  // Get load balancer stats
  const stats = client.getLoadBalancerStats();
  log(`Load balancer stats: ${JSON.stringify(stats, null, 2)}`);
  
  // Pop messages to verify they were stored
  log('Popping messages to verify...');
  const popResult = await client.pop({
    queue: TEST_QUEUE,
    batch: 5,
    wait: false
  });
  
  if (popResult.messages && popResult.messages.length > 0) {
    log(`Retrieved ${popResult.messages.length} messages`, 'success');
    
    // Acknowledge messages
    for (const msg of popResult.messages) {
      await client.ack(msg.transactionId, 'completed');
    }
    log('Messages acknowledged', 'success');
  } else {
    log('No messages retrieved', 'warning');
  }
  
  return client;
}

// Test Session affinity strategy
async function testSessionStrategy() {
  log('\nTesting SESSION load balancing strategy', 'info');
  
  const client = createQueenClient({
    baseUrls: servers,
    loadBalancingStrategy: LoadBalancingStrategy.SESSION,
    enableFailover: true
  });
  
  // Configure the queue
  await client.configure({
    queue: TEST_QUEUE,
    namespace: TEST_NAMESPACE,
    task: 'session-test',
    options: {
      maxRetries: 3,
      retryDelay: 1000
    }
  });
  
  // Push multiple messages - should all go to the same server
  const messages = [];
  for (let i = 1; i <= 10; i++) {
    messages.push({
      queue: TEST_QUEUE,
      partition: 'Session',
      payload: { 
        id: i, 
        strategy: 'session',
        timestamp: Date.now() 
      }
    });
  }
  
  log(`Pushing ${messages.length} messages with SESSION strategy`);
  const pushResult = await client.push({ items: messages });
  log(`Pushed messages: ${JSON.stringify(pushResult)}`, 'success');
  
  // Get load balancer stats
  const stats = client.getLoadBalancerStats();
  log(`Load balancer stats: ${JSON.stringify(stats, null, 2)}`);
  
  // Pop messages to verify they were stored
  log('Popping messages to verify...');
  const popResult = await client.pop({
    queue: TEST_QUEUE,
    partition: 'Session',
    batch: 5,
    wait: false
  });
  
  if (popResult.messages && popResult.messages.length > 0) {
    log(`Retrieved ${popResult.messages.length} messages from session-affinity server`, 'success');
    
    // Acknowledge messages
    for (const msg of popResult.messages) {
      await client.ack(msg.transactionId, 'completed');
    }
    log('Messages acknowledged', 'success');
  } else {
    log('No messages retrieved', 'warning');
  }
  
  return client;
}

// Test failover behavior
async function testFailover() {
  log('\nTesting failover behavior (requires one server to be down)', 'info');
  
  const client = createQueenClient({
    baseUrls: [
      'http://localhost:9999', // Non-existent server
      'http://localhost:6632'  // Working server
    ],
    loadBalancingStrategy: LoadBalancingStrategy.ROUND_ROBIN,
    enableFailover: true,
    timeout: 5000 // Shorter timeout for testing
  });
  
  try {
    // Try to push a message - should failover to working server
    log('Attempting to push message with one server down...');
    const result = await client.push({
      items: [{
        queue: TEST_QUEUE,
        partition: 'Failover',
        payload: { test: 'failover', timestamp: Date.now() }
      }]
    });
    
    log('Message pushed successfully via failover!', 'success');
    log(`Result: ${JSON.stringify(result)}`);
    
    // Get stats to see which server was used
    const stats = client.getLoadBalancerStats();
    log(`Load balancer stats after failover: ${JSON.stringify(stats, null, 2)}`);
    
  } catch (error) {
    log(`Failover test failed: ${error.message}`, 'error');
  }
  
  return client;
}

// Test WebSocket connections with multiple servers
async function testWebSocketConnections() {
  log('\nTesting WebSocket connections with multiple servers', 'info');
  
  const client = createQueenClient({
    baseUrls: servers,
    loadBalancingStrategy: LoadBalancingStrategy.ROUND_ROBIN
  });
  
  // Create WebSocket connections to different servers
  log('Creating WebSocket connections...');
  
  const ws1 = client.createWebSocketConnection('/ws/dashboard');
  log(`WebSocket 1 connected to: ${ws1.serverUrl}`);
  
  const ws2 = client.createWebSocketConnection('/ws/dashboard');
  log(`WebSocket 2 connected to: ${ws2.serverUrl}`);
  
  // Set up message handlers
  ws1.onMessage((data) => {
    log(`WS1 received: ${JSON.stringify(data)}`);
  });
  
  ws2.onMessage((data) => {
    log(`WS2 received: ${JSON.stringify(data)}`);
  });
  
  // Keep connections open for a bit
  await new Promise(resolve => setTimeout(resolve, 2000));
  
  // Close connections
  ws1.close();
  ws2.close();
  log('WebSocket connections closed', 'success');
  
  return client;
}

// Test single server backward compatibility
async function testBackwardCompatibility() {
  log('\nTesting backward compatibility with single baseUrl', 'info');
  
  // Old style - single baseUrl
  const client = createQueenClient({
    baseUrl: 'http://localhost:6632',
    timeout: 30000
  });
  
  try {
    const result = await client.push({
      items: [{
        queue: TEST_QUEUE,
        partition: 'Compatibility',
        payload: { test: 'backward-compatibility', timestamp: Date.now() }
      }]
    });
    
    log('Backward compatibility test passed!', 'success');
    log(`Result: ${JSON.stringify(result)}`);
    
    // This should return null for single-server mode
    const stats = client.getLoadBalancerStats();
    log(`Load balancer stats (should be null): ${stats}`);
    
  } catch (error) {
    log(`Backward compatibility test failed: ${error.message}`, 'error');
  }
  
  return client;
}

// Main test runner
async function runTests() {
  console.log('=' .repeat(80));
  console.log('Queen Multi-Server Load Balancing Test Suite');
  console.log('=' .repeat(80));
  
  try {
    // Test different strategies
    await testRoundRobinStrategy();
    await testSessionStrategy();
    await testFailover();
    await testWebSocketConnections();
    await testBackwardCompatibility();
    
    console.log('\n' + '=' .repeat(80));
    log('All tests completed!', 'success');
    console.log('=' .repeat(80));
    
  } catch (error) {
    log(`Test suite failed: ${error.message}`, 'error');
    console.error(error);
    process.exit(1);
  }
}

// Run the tests
runTests().catch(console.error);
