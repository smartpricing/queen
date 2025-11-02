/**
 * Simple Streaming Test - Wait for actual window processing
 */

import { Queen } from '../client-js/client-v2/Queen.js';

const queen = new Queen({ url: 'http://localhost:6632' });

async function main() {
  console.log('\n========== Simple Streaming Test ==========\n');
  
  // 1. Create queue
  console.log('1. Creating queue...');
  await queen.queue('test-stream-queue').namespace('test').task('stream').create();
  
  // 2. Define stream with very short window (10 seconds)
  console.log('2. Defining stream with 10s windows...');
  await queen.stream('test-stream', 'test')
    .sources(['test-stream-queue'])
    .tumblingTime(10)   // 10 second windows
    .gracePeriod(5)     // 5 second grace
    .define();
  
  // 3. Push test messages over time to advance watermark
  console.log('3. Pushing messages in batches to advance watermark...');
  console.log('   (Need watermark to advance past window + grace for windows to become ready)');
  
  // Push 4 batches, 5 seconds apart (20 seconds total)
  // This ensures watermark advances past first window + grace period
  for (let batch = 0; batch < 4; batch++) {
    const batchSize = 10;
    await queen.queue('test-stream-queue').partition('test-partition').push(
      Array.from({ length: batchSize }, (_, i) => ({
        data: {
          id: batch * batchSize + i,
          batch: batch,
          value: Math.random() * 100,
          timestamp: new Date().toISOString()
        }
      }))
    );
    
    console.log(`   Batch ${batch + 1}: Pushed ${batchSize} messages`);
    
    if (batch < 3) {
      await new Promise(resolve => setTimeout(resolve, 5000));  // Wait 5s between batches
    }
  }
  
  console.log('✅ Pushed 40 messages in 4 batches over 15 seconds');
  console.log('   (Watermark should now be past first window + grace period)');
  
  // 4. Wait a bit more to be safe
  console.log('\n4. Waiting 5 more seconds...');
  await new Promise(resolve => setTimeout(resolve, 5000));
  
  // 5. Consume ONE window
  console.log('\n5. Starting consumer (will process 1 window and stop)...\n');
  
  const consumer = queen.consumer('test-stream', 'test-consumer');
  let windowsProcessed = 0;
  
  await new Promise((resolve) => {
    consumer.process(async (window) => {
      windowsProcessed++;
      
      console.log(`\n📦 Window ${windowsProcessed}:`);
      console.log(`   ID: ${window.id}`);
      console.log(`   Lease: ${window.leaseId}`);
      console.log(`   Key: ${window.key}`);
      console.log(`   Start: ${window.start}`);
      console.log(`   End: ${window.end}`);
      console.log(`   Messages: ${window.allMessages.length}`);
      
      // Aggregate
      const stats = window.aggregate({
        count: true,
        sum: ['payload.value'],
        avg: ['payload.value'],
        min: ['payload.value'],
        max: ['payload.value']
      });
      
      console.log(`\n   Aggregations:`);
      console.log(`   - Count: ${stats.count}`);
      console.log(`   - Sum: ${stats.sum['payload.value'].toFixed(2)}`);
      console.log(`   - Avg: ${stats.avg['payload.value'].toFixed(2)}`);
      console.log(`   - Min: ${stats.min['payload.value'].toFixed(2)}`);
      console.log(`   - Max: ${stats.max['payload.value'].toFixed(2)}`);
      
      // Print first 3 messages
      console.log(`\n   First 3 messages:`);
      window.allMessages.slice(0, 3).forEach((msg, i) => {
        console.log(`   ${i + 1}. ID=${msg.id.substr(0, 8)}..., value=${msg.payload.value.toFixed(2)}`);
      });
      
      console.log(`\n✅ Window processed successfully!\n`);
      
      // Stop after first window
      consumer.stop();
      resolve();
      
    }).catch(err => {
      console.error('Consumer error:', err);
      resolve();
    });
    
    // Safety timeout
    setTimeout(() => {
      consumer.stop();
      resolve();
    }, 30000);
  });
  
  console.log(`\n========================================`);
  console.log(`✅ Test complete! Processed ${windowsProcessed} window(s)`);
  console.log(`========================================\n`);
  
  await queen.close();
  process.exit(0);
}

main().catch(console.error);

