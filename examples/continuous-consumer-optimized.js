#!/usr/bin/env node

/**
 * Optimized Continuous Consumer Example
 * 
 * This optimized version processes messages in parallel batches
 * with batch acknowledgments for maximum throughput.
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
  
  // Optimized settings for high throughput
  const batchSize = parseInt(process.env.BATCH_SIZE) || 50;  // Reasonable batch size
  const parallelWorkers = parseInt(process.env.WORKERS) || 10;  // Process in parallel
  const pollTimeout = parseInt(process.env.POLL_TIMEOUT) || 5000;  // Shorter timeout for faster response

  console.log('🚀 Starting OPTIMIZED continuous consumer');
  console.log(`📦 Queue: ${namespace}/${task}/${queue}`);
  console.log('⚡ Performance settings:');
  console.log(`   - Batch size: ${batchSize}`);
  console.log(`   - Parallel workers: ${parallelWorkers}`);
  console.log(`   - Poll timeout: ${pollTimeout}ms`);
  console.log('');

  let stats = {
    messages: 0,
    errors: 0,
    startTime: Date.now(),
    lastMessageTime: null,
    processingTimes: []
  };

  // Process message function
  async function processMessage(message) {
    // Your actual processing logic here
    // For demo, just simulate minimal work
    return new Promise(resolve => setImmediate(resolve));
  }

  // Optimized consume function with parallel processing
  async function consumeOptimized() {
    let running = true;
    const activeWorkers = new Set();
    
    // Handle shutdown
    process.on('SIGINT', () => {
      console.log('\n\n🛑 Shutting down consumer...');
      running = false;
    });

    while (running) {
      try {
        // Wait if we have too many active workers
        while (activeWorkers.size >= parallelWorkers && running) {
          await new Promise(resolve => setTimeout(resolve, 10));
        }

        if (!running) break;

        // Start a new worker
        const worker = (async () => {
          try {
            // Pop messages with optimized settings
            const result = await client.pop({ 
              ns: namespace,
              task: task,
              queue: queue,
              wait: true,
              timeout: pollTimeout,
              batch: batchSize
            });
            
            if (result.messages && result.messages.length > 0) {
              const startTime = Date.now();
              
              // Process messages in parallel within the batch
              const processPromises = result.messages.map(async (message) => {
                try {
                  await processMessage(message);
                  stats.messages++;
                  stats.lastMessageTime = Date.now();
                  return { transactionId: message.transactionId, status: 'completed' };
                } catch (error) {
                  stats.errors++;
                  console.error(`Error processing message ${message.id}:`, error.message);
                  return { transactionId: message.transactionId, status: 'failed', error: error.message };
                }
              });

              // Wait for all messages in batch to process
              const acknowledgments = await Promise.all(processPromises);
              
              // Batch acknowledge all messages at once
              try {
                await client.ackBatch(acknowledgments);
              } catch (error) {
                console.error('Batch ACK failed:', error);
                // Fall back to individual ACKs if batch fails
                for (const ack of acknowledgments) {
                  try {
                    await client.ack(ack.transactionId, ack.status, ack.error);
                  } catch (e) {
                    console.error(`Individual ACK failed for ${ack.transactionId}:`, e.message);
                  }
                }
              }
              
              const processingTime = Date.now() - startTime;
              stats.processingTimes.push(processingTime);
              
              // Keep only last 100 processing times for average calculation
              if (stats.processingTimes.length > 100) {
                stats.processingTimes.shift();
              }
              
              console.log(`✅ Processed batch of ${result.messages.length} messages in ${processingTime}ms`);
            }
          } catch (error) {
            if (error.name !== 'AbortError') {
              console.error('Worker error:', error);
              await new Promise(resolve => setTimeout(resolve, 1000));
            }
          } finally {
            activeWorkers.delete(worker);
          }
        })();

        activeWorkers.add(worker);
      } catch (error) {
        console.error('Main loop error:', error);
        await new Promise(resolve => setTimeout(resolve, 1000));
      }
    }

    // Wait for all workers to complete
    await Promise.all(Array.from(activeWorkers));
    
    // Final stats
    const uptime = Math.floor((Date.now() - stats.startTime) / 1000);
    const avgProcessingTime = stats.processingTimes.length > 0 
      ? stats.processingTimes.reduce((a, b) => a + b, 0) / stats.processingTimes.length
      : 0;
    
    console.log('\n📈 Final Statistics:');
    console.log(`  Total runtime: ${formatUptime(uptime)}`);
    console.log(`  Messages processed: ${stats.messages}`);
    console.log(`  Throughput: ${(stats.messages / uptime).toFixed(2)} msg/s`);
    console.log(`  Average batch processing time: ${avgProcessingTime.toFixed(2)}ms`);
    console.log(`  Errors encountered: ${stats.errors}`);
    
    process.exit(0);
  }

  // Print statistics every 10 seconds
  const statsInterval = setInterval(() => {
    if (stats.messages === 0) return;
    
    const uptime = Math.floor((Date.now() - stats.startTime) / 1000);
    const throughput = stats.messages / uptime;
    const avgProcessingTime = stats.processingTimes.length > 0 
      ? stats.processingTimes.reduce((a, b) => a + b, 0) / stats.processingTimes.length
      : 0;
    
    console.log('\n📊 Statistics:');
    console.log(`  Uptime: ${formatUptime(uptime)}`);
    console.log(`  Messages processed: ${stats.messages}`);
    console.log(`  Throughput: ${throughput.toFixed(2)} msg/s`);
    console.log(`  Average batch processing: ${avgProcessingTime.toFixed(2)}ms`);
    console.log(`  Errors: ${stats.errors}`);
    
    if (stats.lastMessageTime) {
      const idle = Math.floor((Date.now() - stats.lastMessageTime) / 1000);
      if (idle > 2) {
        console.log(`  ⚠️ Idle time: ${idle}s`);
      }
    }
    console.log('');
  }, 10000);

  console.log('✅ Consumer started. Press Ctrl+C to stop.\n');
  
  // Start consuming
  await consumeOptimized();
  clearInterval(statsInterval);
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
