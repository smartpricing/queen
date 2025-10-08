#!/usr/bin/env node

/**
 * Continuous Consumer Example for Queen V2
 * 
 * This example demonstrates how the Queen client performs continuous
 * long polling without delays when timeouts occur. The consumer will
 * immediately start a new request when the previous one times out,
 * ensuring no messages are missed and minimizing latency.
 */

import { createQueenClient } from '../src/client/index.js';

async function main() {
  // Create client with configuration
  const client = createQueenClient({
    baseUrl: process.env.QUEEN_URL || 'http://localhost:6632',
    timeout: 35000  // Client timeout for non-polling requests
  });

  // V2: Using queue and optional partition
  const queue = process.env.QUEUE || 'email-translations';
  const partition = process.env.PARTITION;  // Optional: if not set, consumes from any partition
  const namespace = process.env.NAMESPACE;  // Optional: for filtering
  const task = process.env.TASK;           // Optional: for filtering

  console.log('🚀 Starting continuous consumer (V2)');
  if (queue) {
    console.log(`📦 Queue: ${queue}${partition ? '/' + partition : ' (any partition)'}`);
  } else if (namespace || task) {
    console.log(`🔍 Filtering by: ${namespace ? 'namespace=' + namespace : ''} ${task ? 'task=' + task : ''}`);
  }
  console.log('⏱️  Long polling timeout: 30 seconds');
  console.log('🔄 Will immediately retry on timeout (no delays)\n');

  let stats = {
    messages: 0,
    errors: 0,
    startTime: Date.now(),
    lastMessageTime: null,
    partitionCounts: {}
  };

  // Start the consumer
  const stop = client.consume({
    queue: queue,
    partition: partition,
    namespace: namespace,
    task: task,
    handler: async (message) => {
      stats.messages++;
      stats.lastMessageTime = Date.now();
      
      // Track messages per partition
      const partitionKey = `${message.queue}/${message.partition}`;
      stats.partitionCounts[partitionKey] = (stats.partitionCounts[partitionKey] || 0) + 1;
      
      console.log(`✉️  [${new Date().toISOString()}] Message received:`, {
        queue: message.queue,
        partition: message.partition,
        transactionId: message.transactionId,
        retryCount: message.retryCount,
        data: message.data
      });

      // Simulate processing
      await processMessage(message);
    },
    options: {
      wait: true,         // Enable long polling
      timeout: 30000,     // 30 second server timeout
      batch: 10,          // Process up to 10 messages at once
      stopOnError: false  // Continue processing on errors
    }
  });

  // Print statistics every minute
  const statsInterval = setInterval(() => {
    const uptime = Math.floor((Date.now() - stats.startTime) / 1000);
    const rate = stats.messages / (uptime / 60);
    
    console.log('\n📊 Statistics:');
    console.log(`  Uptime: ${formatUptime(uptime)}`);
    console.log(`  Messages processed: ${stats.messages}`);
    console.log(`  Processing rate: ${rate.toFixed(2)} msg/min`);
    console.log(`  Errors: ${stats.errors}`);
    
    // Show partition distribution
    if (Object.keys(stats.partitionCounts).length > 0) {
      console.log('  Distribution by partition:');
      for (const [partition, count] of Object.entries(stats.partitionCounts)) {
        const percentage = ((count / stats.messages) * 100).toFixed(1);
        console.log(`    ${partition}: ${count} messages (${percentage}%)`);
      }
    }
    
    if (stats.lastMessageTime) {
      const idle = Math.floor((Date.now() - stats.lastMessageTime) / 1000);
      console.log(`  Idle time: ${idle}s`);
    }
    console.log('');
  }, 60000);

  // Handle shutdown
  process.on('SIGINT', () => {
    console.log('\n\n🛑 Shutting down consumer...');
    
    clearInterval(statsInterval);
    stop();
    
    const uptime = Math.floor((Date.now() - stats.startTime) / 1000);
    console.log('\n📈 Final Statistics:');
    console.log(`  Total runtime: ${formatUptime(uptime)}`);
    console.log(`  Messages processed: ${stats.messages}`);
    console.log(`  Errors encountered: ${stats.errors}`);
    
    if (Object.keys(stats.partitionCounts).length > 0) {
      console.log('  Final partition distribution:');
      for (const [partition, count] of Object.entries(stats.partitionCounts)) {
        const percentage = ((count / stats.messages) * 100).toFixed(1);
        console.log(`    ${partition}: ${count} messages (${percentage}%)`);
      }
    }
    
    process.exit(0);
  });

  console.log('✅ Consumer started. Press Ctrl+C to stop.\n');
}

async function processMessage(message) {
  // Simulate some processing work
  const processingTime = Math.random() * 1000 + 500; // 0.5-1.5 seconds
  await new Promise(resolve => setTimeout(resolve, processingTime));
  
  // Randomly simulate occasional processing errors (5% chance)
  if (Math.random() < 0.05) {
    throw new Error('Simulated processing error');
  }
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

// Run the consumer
main().catch(error => {
  console.error('❌ Fatal error:', error);
  process.exit(1);
});