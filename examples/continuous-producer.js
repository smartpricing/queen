#!/usr/bin/env node

/**
 * Continuous Producer Example
 * 
 * This example demonstrates a continuous message producer that pushes
 * messages to a Queen queue at regular intervals. It's designed to work
 * with the continuous-consumer.js example to demonstrate the full
 * message flow with long polling.
 */

import { createQueenClient } from '../src/client/index.js';

async function main() {
  // Create client with configuration
  const client = createQueenClient({
    baseUrl: process.env.QUEEN_URL || 'http://localhost:6632',
    timeout: 10000  // 10 second timeout for push operations
  });

  const namespace = process.env.NS || 'smartchat';
  const task = process.env.TASK || 'translations';
  const queue = process.env.QUEUE || 'email';
  
  // Production rate configuration
  const messagesPerBatch = parseInt(process.env.BATCH_SIZE) || 100;
  const intervalMilliseconds = parseInt(process.env.INTERVAL) || 10000;
  const burstMode = process.env.BURST === 'true';
  const highPerformanceMode = process.env.HIGH_PERF === 'true';

  console.log('🚀 Starting continuous producer');
  console.log(`📦 Target Queue: ${namespace}/${task}/${queue}`);
  console.log(`📊 Configuration:`);
  console.log(`   - Messages per batch: ${messagesPerBatch}`);
  console.log(`   - Interval: ${intervalMilliseconds} milliseconds`);
  console.log(`   - Burst mode: ${burstMode ? 'enabled' : 'disabled'}`);
  console.log(`   - High performance: ${highPerformanceMode ? 'enabled' : 'disabled'}`);
  
  if (highPerformanceMode) {
    console.log('\n⚡ HIGH PERFORMANCE MODE ENABLED');
    console.log('   Target: 10,000+ messages/second');
    console.log('   Make sure server is running with: DB_POOL_SIZE=100 node start-optimized.js');
  }
  console.log('');

  // Configure the queue
  try {
    await client.configure({
      ns: namespace,
      task: task,
      queue: queue,
      options: {
        maxRetries: 3,
        retryDelay: 1000,
        dlqAfterMaxRetries: true
      }
    });
    console.log('✅ Queue configured successfully\n');
  } catch (error) {
    console.log('⚠️  Queue configuration failed (may already exist):', error.message, '\n');
  }

  let stats = {
    batches: 0,
    messages: 0,
    errors: 0,
    startTime: Date.now(),
    lastBatchTime: null
  };

  // Message generator function
  function generateMessages(count) {
    const messages = [];
    const templates = [
      { type: 'welcome', subject: 'Welcome to our service!' },
      { type: 'notification', subject: 'New feature available' },
      { type: 'reminder', subject: 'Don\'t forget to check out...' },
      { type: 'newsletter', subject: 'Weekly updates' },
      { type: 'alert', subject: 'Important information' }
    ];

    for (let i = 0; i < count; i++) {
      const template = templates[Math.floor(Math.random() * templates.length)];
      const messageId = `msg-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
      
      messages.push({
        ns: namespace,
        task: task,
        queue: queue,
        payload: {
          id: messageId,
          type: template.type,
          email: {
            to: `user${Math.floor(Math.random() * 1000)}@example.com`,
            subject: template.subject,
            template: template.type,
            variables: {
              userName: `User${Math.floor(Math.random() * 1000)}`,
              timestamp: new Date().toISOString(),
              locale: ['en', 'es', 'fr', 'de'][Math.floor(Math.random() * 4)]
            }
          },
          priority: Math.random() > 0.8 ? 'high' : 'normal',
          createdAt: new Date().toISOString()
        }
      });
    }
    
    return messages;
  }

  // Producer function
  async function produceMessages() {
    const batchSize = burstMode 
      ? messagesPerBatch * (1 + Math.floor(Math.random() * 3)) // 1x to 3x in burst mode
      : messagesPerBatch;
    
    const messages = generateMessages(batchSize);
    
    try {
      console.log(`📤 [${new Date().toISOString()}] Pushing ${messages.length} messages...`);
      
      const result = await client.push({
        items: messages
      });
      
      stats.batches++;
      stats.messages += result.messages.length;
      stats.lastBatchTime = Date.now();
      
      console.log(`✅ Successfully pushed ${result.messages.length} messages`);
      console.log(`   Sample IDs: ${result.messages.slice(0, 3).map(m => m.id).join(', ')}${result.messages.length > 3 ? '...' : ''}`);
      
      // Show sample message details occasionally
      if (stats.batches % 5 === 0) {
        console.log(`   Sample payload:`, JSON.stringify(messages[0].payload, null, 2));
      }
      
    } catch (error) {
      stats.errors++;
      console.error(`❌ Failed to push messages:`, error.message);
    }
  }

  // Start producing messages
  console.log('🏁 Starting production cycle...\n');
  
  if (highPerformanceMode) {
    // High performance mode: Multiple concurrent producers
    const concurrentProducers = 10;
    const highPerfBatchSize = 1000;
    const highPerfInterval = 100; // 10 producers * 1000 msgs * 10/sec = 100,000 msgs/sec theoretical max
    
    console.log(`⚡ Running ${concurrentProducers} concurrent producers`);
    console.log(`⚡ Each producing ${highPerfBatchSize} messages every ${highPerfInterval}ms`);
    console.log(`⚡ Theoretical max: ${(concurrentProducers * highPerfBatchSize * (1000/highPerfInterval)).toFixed(0)} messages/second\n`);
    
    // Override batch size for high performance
    const originalBatchSize = messagesPerBatch;
    
    // Start multiple concurrent producers
    const producers = [];
    for (let i = 0; i < concurrentProducers; i++) {
      const producerId = i;
      
      // Stagger start times to avoid thundering herd
      setTimeout(() => {
        const interval = setInterval(async () => {
          const messages = generateMessages(highPerfBatchSize);
          
          try {
            const start = Date.now();
            const result = await client.push({ items: messages });
            const duration = Date.now() - start;
            
            stats.batches++;
            stats.messages += result.messages.length;
            stats.lastBatchTime = Date.now();
            
            if (stats.batches % 10 === 0) {
              console.log(`⚡ [Producer ${producerId}] Pushed ${result.messages.length} messages in ${duration}ms`);
            }
          } catch (error) {
            stats.errors++;
            console.error(`❌ [Producer ${producerId}] Failed:`, error.message);
          }
        }, highPerfInterval);
        
        producers.push(interval);
      }, i * 10); // Stagger by 10ms
    }
    
    // Store intervals for cleanup
    globalThis.highPerfProducers = producers;
  } else {
    // Normal mode: Single producer
    // Initial batch
    await produceMessages();
    
    // Set up interval for continuous production
    const productionInterval = setInterval(async () => {
      await produceMessages();
    }, intervalMilliseconds);
    
    globalThis.productionInterval = productionInterval;
  }

  // Statistics reporting
  const statsInterval = setInterval(() => {
    const uptime = Math.floor((Date.now() - stats.startTime) / 1000);
    const rate = stats.messages / (uptime / 60);
    
    console.log('\n📊 Production Statistics:');
    console.log(`  Uptime: ${formatUptime(uptime)}`);
    console.log(`  Batches sent: ${stats.batches}`);
    console.log(`  Messages sent: ${stats.messages}`);
    console.log(`  Production rate: ${rate.toFixed(2)} msg/min`);
    console.log(`  Errors: ${stats.errors}`);
    
    if (stats.lastBatchTime) {
      const timeSinceLastBatch = Math.floor((Date.now() - stats.lastBatchTime) / 1000);
      console.log(`  Time since last batch: ${timeSinceLastBatch}s`);
    }
    console.log('');
  }, 60000);

  // Handle shutdown
  process.on('SIGINT', () => {
    console.log('\n\n🛑 Shutting down producer...');
    
    // Clear intervals based on mode
    if (globalThis.highPerfProducers) {
      globalThis.highPerfProducers.forEach(interval => clearInterval(interval));
    }
    if (globalThis.productionInterval) {
      clearInterval(globalThis.productionInterval);
    }
    clearInterval(statsInterval);
    
    const uptime = Math.floor((Date.now() - stats.startTime) / 1000);
    console.log('\n📈 Final Production Statistics:');
    console.log(`  Total runtime: ${formatUptime(uptime)}`);
    console.log(`  Total batches: ${stats.batches}`);
    console.log(`  Total messages: ${stats.messages}`);
    console.log(`  Average batch size: ${(stats.messages / stats.batches).toFixed(1)}`);
    console.log(`  Overall rate: ${(stats.messages / (uptime / 60)).toFixed(2)} msg/min`);
    console.log(`  Errors encountered: ${stats.errors}`);
    console.log(`  Success rate: ${((1 - stats.errors / stats.batches) * 100).toFixed(1)}%`);
    
    process.exit(0);
  });

  // Simulate varying load (optional - only in normal mode)
  if (process.env.VARY_LOAD === 'true' && !highPerformanceMode) {
    console.log('📈 Variable load mode enabled - production rate will vary\n');
    
    setInterval(() => {
      // Randomly adjust the production rate
      const factor = 0.5 + Math.random() * 2; // 0.5x to 2.5x
      const newInterval = Math.floor(intervalMilliseconds * factor);
      
      if (globalThis.productionInterval) {
        clearInterval(globalThis.productionInterval);
        globalThis.productionInterval = setInterval(async () => {
          await produceMessages();
        }, newInterval);
        
        console.log(`🔄 Production rate adjusted: new interval ${newInterval}ms`);
      }
    }, 120000); // Adjust every 2 minutes
  }

  console.log('✅ Producer started. Press Ctrl+C to stop.\n');
}

function formatUptime(seconds) {
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const secs = seconds % 60;
  
  const parts = [];
  if (hours > 0) parts.push(`${hours}h`);
  if (minutes > 0) parts.push(`${minutes}m`);
  parts.push(`${secs}s`);
  
  return parts.join(' ');
}

// Run the producer
main().catch(error => {
  console.error('❌ Fatal error:', error);
  process.exit(1);
});
