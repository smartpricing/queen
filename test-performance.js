#!/usr/bin/env node

/**
 * Performance Test for Queen
 * Tests the server's ability to handle high message throughput
 */

import { createQueenClient } from './src/client/index.js';

async function testPerformance() {
  console.log('🚀 Queen Performance Test');
  console.log('==========================\n');
  
  // Check server health
  try {
    const healthResponse = await fetch('http://localhost:6632/health');
    const health = await healthResponse.json();
    console.log('✅ Server is running:', health.status);
    if (health.stats) {
      console.log('   Pool connections:', health.stats.pool);
    }
  } catch (error) {
    console.error('❌ Server is not running. Start it with:');
    console.error('   DB_POOL_SIZE=100 npm start\n');
    process.exit(1);
  }
  
  const client = createQueenClient({
    baseUrl: 'http://localhost:6632',
    timeout: 30000
  });
  
  // Configure test queue
  await client.configure({
    ns: 'perftest',
    task: 'benchmark',
    queue: 'messages',
    options: {
      maxRetries: 1,
      retryDelay: 1000
    }
  });
  
  console.log('\n📊 Starting performance test...\n');
  
  // Test parameters
  const tests = [
    { name: 'Small batches', batchSize: 10, count: 10, delay: 100 },
    { name: 'Medium batches', batchSize: 100, count: 10, delay: 100 },
    { name: 'Large batches', batchSize: 1000, count: 5, delay: 200 },
    { name: 'Burst mode', batchSize: 500, count: 20, delay: 50 }
  ];
  
  for (const test of tests) {
    console.log(`\n🔧 Test: ${test.name}`);
    console.log(`   Batch size: ${test.batchSize} messages`);
    console.log(`   Iterations: ${test.count}`);
    console.log(`   Delay: ${test.delay}ms\n`);
    
    const startTime = Date.now();
    let totalMessages = 0;
    let errors = 0;
    
    for (let i = 0; i < test.count; i++) {
      // Generate batch of messages
      const messages = [];
      for (let j = 0; j < test.batchSize; j++) {
        messages.push({
          ns: 'perftest',
          task: 'benchmark',
          queue: 'messages',
          payload: {
            testId: `${test.name}-${i}-${j}`,
            timestamp: Date.now(),
            data: {
              batch: i,
              index: j,
              test: test.name,
              randomData: Math.random().toString(36).substring(7)
            }
          }
        });
      }
      
      try {
        const batchStart = Date.now();
        const result = await client.push({ items: messages });
        const batchTime = Date.now() - batchStart;
        
        totalMessages += result.messages.length;
        
        if (i % Math.max(1, Math.floor(test.count / 5)) === 0) {
          console.log(`   ✓ Batch ${i + 1}/${test.count}: ${result.messages.length} messages in ${batchTime}ms`);
        }
      } catch (error) {
        errors++;
        console.error(`   ✗ Batch ${i + 1} failed:`, error.message);
      }
      
      // Small delay between batches
      if (i < test.count - 1) {
        await new Promise(resolve => setTimeout(resolve, test.delay));
      }
    }
    
    const duration = (Date.now() - startTime) / 1000;
    const rate = totalMessages / duration;
    
    console.log(`\n   📈 Results:`);
    console.log(`      Total messages: ${totalMessages}`);
    console.log(`      Duration: ${duration.toFixed(2)}s`);
    console.log(`      Rate: ${rate.toFixed(0)} msg/s`);
    console.log(`      Errors: ${errors}`);
    
    // Check server metrics
    try {
      const metricsResponse = await fetch('http://localhost:6632/metrics');
      const metrics = await metricsResponse.json();
      console.log(`      Server msg/s: ${metrics.messages.rate.toFixed(0)}`);
      console.log(`      Pool status: ${metrics.database.idleConnections}/${metrics.database.poolSize} idle`);
    } catch (error) {
      // Metrics endpoint might not be available
    }
  }
  
  console.log('\n✅ Performance test complete!\n');
  
  // Final server stats
  try {
    const healthResponse = await fetch('http://localhost:6632/health');
    const health = await healthResponse.json();
    if (health.stats) {
      console.log('📊 Final server statistics:');
      console.log(`   Total messages processed: ${health.stats.messages}`);
      console.log(`   Average rate: ${health.stats.messagesPerSecond} msg/s`);
      console.log(`   Uptime: ${health.uptime}`);
    }
  } catch (error) {
    // Health endpoint might not have all stats
  }
}

// Run the test
testPerformance().catch(error => {
  console.error('❌ Test failed:', error);
  process.exit(1);
});
