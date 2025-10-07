#!/usr/bin/env node

/**
 * Continuous Consumer Example
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

  const namespace = process.env.NS || 'smartchat';
  const task = process.env.TASK || 'translations';
  const queue = process.env.QUEUE || 'email';

  console.log('🚀 Starting continuous consumer');
  console.log(`📦 Queue: ${namespace}/${task}/${queue}`);
  console.log('⏱️  Long polling timeout: 30 seconds');
  console.log('🔄 Will immediately retry on timeout (no delays)\n');

  let stats = {
    messages: 0,
    errors: 0,
    startTime: Date.now(),
    lastMessageTime: null
  };

  // Start the consumer
  const stop = client.consume({
    ns: namespace,
    task: task,
    queue: queue,
    handler: async (message) => {
      stats.messages++;
      stats.lastMessageTime = Date.now();
      
      console.log(`✉️  [${new Date().toISOString()}] Message received:`, {
        id: message.id,
        transactionId: message.transactionId,
        attempt: message.attempt,
        data: message.data
      });

      // Simulate processing
      await processMessage(message);
    },
    options: {
      wait: true,         // Enable long polling
      timeout: 30000,     // 30 second server timeout
      batch: 1000,          // Process up to 10 messages at once
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
    
    process.exit(0);
  });

  console.log('✅ Consumer started. Press Ctrl+C to stop.\n');
}

async function processMessage(message) {
  // Simulate some processing work
  //const processingTime = Math.random() * 1000 + 500; // 0.5-1.5 seconds
  //await new Promise(resolve => setTimeout(resolve, processingTime));
  
  // Randomly simulate occasional processing errors (5% chance)
  //if (Math.random() < 0.05) {
  //  throw new Error('Simulated processing error');
 // }
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
