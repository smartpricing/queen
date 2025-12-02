/**
 * Queen Streaming V3 - Complete Example
 * 
 * This example demonstrates the full streaming functionality:
 * 1. Defining a stream
 * 2. Producing messages to source queues
 * 3. Consuming windows with aggregation
 * 4. Partitioned processing
 * 5. Client-side filtering and transformations
 */

import { Queen } from '../client-js/client-v2/Queen.js';

const queen = new Queen({
  url: 'http://localhost:6632'
});

// ============================================================================
// STEP 1: Define Source Queues
// ============================================================================

async function defineSourceQueues() {
  console.log('\n📋 Step 1: Defining source queues...');
  
  // Queue for user clicks
  await queen.queue('user-clicks')
    .namespace('analytics')
    .task('tracking')
    .create();
  
  // Queue for user purchases
  await queen.queue('user-purchases')
    .namespace('analytics')
    .task('tracking')
    .create();
  
  console.log('✅ Source queues created: user-clicks, user-purchases');
}

// ============================================================================
// STEP 2: Define Streams
// ============================================================================

async function defineStreams() {
  console.log('\n📊 Step 2: Defining streams...');
  
  // Global stream (all partitions processed together)
  await queen.stream('user-activity-global', 'analytics')
    .sources(['user-clicks', 'user-purchases'])
    .tumblingTime(10)   // 10-second windows (for testing)
    .gracePeriod(5)     // 5-second grace period
    .define();
  
  console.log('✅ Global stream created: user-activity-global (10s windows)');
  
  // Partitioned stream (each partition processed separately)
  await queen.stream('user-activity-partitioned', 'analytics')
    .sources(['user-clicks', 'user-purchases'])
    .partitioned()      // Process by partition
    .tumblingTime(10)   // 10-second windows (for testing)
    .gracePeriod(5)     // 5-second grace period
    .define();
  
  console.log('✅ Partitioned stream created: user-activity-partitioned (10s windows)');
}

// ============================================================================
// STEP 3: Produce Test Data
// ============================================================================

async function produceTestData() {
  console.log('\n📤 Step 3: Producing test data...');
  
  const users = ['user-A', 'user-B', 'user-C'];
  const partitions = ['partition-A', 'partition-B', 'partition-C'];
  const eventCount = 50;
  
  const messages = [];
  
  for (let i = 0; i < eventCount; i++) {
    const userId = users[Math.floor(Math.random() * users.length)];
    const partition = partitions[i % partitions.length];  // Round-robin partitions
    
    // Alternating clicks and purchases
    if (i % 3 === 0) {
      // Purchase event
      messages.push({
        queue: 'user-purchases',
        partition,
        data: {
          type: 'purchase',
          userId,
          productId: `product-${Math.floor(Math.random() * 10)}`,
          amount: (Math.random() * 100).toFixed(2),
          timestamp: new Date().toISOString()
        }
      });
    } else {
      // Click event
      messages.push({
        queue: 'user-clicks',
        partition,
        data: {
          type: 'click',
          userId,
          page: `/page-${Math.floor(Math.random() * 5)}`,
          timestamp: new Date().toISOString()
        }
      });
    }
  }
  
  // Push messages - need to group by queue AND partition
  // The client API requires setting partition before push
  const messagesByQueueAndPartition = {};
  
  for (const msg of messages) {
    const key = `${msg.queue}:${msg.partition}`;
    if (!messagesByQueueAndPartition[key]) {
      messagesByQueueAndPartition[key] = [];
    }
    messagesByQueueAndPartition[key].push({ data: msg.data });
  }
  
  // Push to each queue/partition combination
  for (const [key, msgs] of Object.entries(messagesByQueueAndPartition)) {
    const [queueName, partitionName] = key.split(':');
    await queen.queue(queueName).partition(partitionName).push(msgs);
  }
  
  console.log(`✅ Produced ${eventCount} events across ${partitions.length} partitions`);
  
  // Push one more batch after a delay to advance the watermark
  console.log('⏳ Waiting 20 seconds, then pushing final batch to advance watermark...');
  await new Promise(resolve => setTimeout(resolve, 20000));
  
  // Push 10 more messages to advance watermark past the grace boundary
  await queen.queue('user-clicks').partition('partition-A').push(
    Array.from({ length: 10 }, (_, i) => ({
      data: {
        type: 'click',
        userId: 'watermark-advancer',
        page: '/final',
        timestamp: new Date().toISOString()
      }
    }))
  );
  
  console.log('✅ Pushed final batch to advance watermark');
  console.log('⏳ Waiting 5 more seconds for windows to become ready...');
  await new Promise(resolve => setTimeout(resolve, 5000));
}

// ============================================================================
// STEP 4: Consume Global Stream
// ============================================================================

async function consumeGlobalStream() {
  console.log('\n🔄 Step 4: Consuming global stream...');
  
  const consumer = queen.consumer('user-activity-global', 'analytics-global');
  
  let windowCount = 0;
  const maxWindows = 3;
  
  consumer.process(async (window) => {
    windowCount++;
    console.log(`\n📦 Window ${windowCount} [Global]:`);
    console.log(`   ID: ${window.id}`);
    console.log(`   Key: ${window.key}`);
    console.log(`   Start: ${window.start}`);
    console.log(`   End: ${window.end}`);
    console.log(`   Messages: ${window.allMessages.length}`);
    
    // Client-side aggregation
    const stats = window.aggregate({
      count: true,
      sum: ['data.amount']
    });
    console.log(window.messages);
    const purchases = window.messages.filter(m => m.data.type === 'purchase').length;
    const clicks = window.messages.filter(m => m.data.type === 'click').length;
    
    console.log(`   Clicks: ${clicks}`);
    console.log(`   Purchases: ${purchases}`);
    console.log(`   Total Revenue: $${stats.sum['data.amount'] || 0}`);
    
    // Group by user
    const byUser = window.groupBy('data.userId');
    console.log(`   Active Users: ${Object.keys(byUser).length}`);
    
    // Stop after processing some windows
    if (windowCount >= maxWindows) {
      console.log(`\n✅ Processed ${windowCount} windows from global stream`);
      consumer.stop();
    }
  }).catch(err => {
    console.error('Error consuming global stream:', err);
  });
}

// ============================================================================
// STEP 5: Consume Partitioned Stream
// ============================================================================

async function consumePartitionedStream() {
  console.log('\n🔄 Step 5: Consuming partitioned stream...');
  
  const consumer = queen.consumer('user-activity-partitioned', 'analytics-partitioned');
  
  let windowCount = 0;
  const maxWindows = 5;
  
  consumer.process(async (window) => {
    windowCount++;
    console.log(`\n📦 Window ${windowCount} [Partitioned]:`);
    console.log(`   ID: ${window.id}`);
    console.log(`   Key: ${window.key} (partition)`);
    console.log(`   Start: ${window.start}`);
    console.log(`   End: ${window.end}`);
    console.log(`   Messages: ${window.allMessages.length}`);
    
    // Filter and aggregate
    const clickCount = window
      .filter(m => m.data.type === 'click')
      .aggregate({ count: true });
    
    const purchaseStats = window
      .reset()  // Reset to original messages
      .filter(m => m.data.type === 'purchase')
      .aggregate({ 
        count: true,
        sum: ['data.amount']
      });
    
    console.log(`   Clicks in partition: ${clickCount.count}`);
    console.log(`   Purchases in partition: ${purchaseStats.count}`);
    console.log(`   Revenue in partition: $${purchaseStats.sum?.['data.amount'] || 0}`);
    
    // Stop after processing some windows
    if (windowCount >= maxWindows) {
      console.log(`\n✅ Processed ${windowCount} windows from partitioned stream`);
      consumer.stop();
    }
  }).catch(err => {
    console.error('Error consuming partitioned stream:', err);
  });
}

// ============================================================================
// STEP 6: Advanced Processing Example
// ============================================================================

async function advancedProcessing() {
  console.log('\n🚀 Step 6: Advanced processing example...');
  
  const consumer = queen.consumer('user-activity-global', 'analytics-advanced');
  
  let windowCount = 0;
  
  consumer.process(async (window) => {
    windowCount++;
    console.log(`\n🔬 Advanced Window ${windowCount}:`);
    
    // Complex aggregation: revenue per user
    const byUser = window.groupBy('data.userId');
    const userStats = {};
    
    for (const [userId, messages] of Object.entries(byUser)) {
      const userWindow = new (window.constructor)({
        ...window,
        messages: messages
      });
      
      const stats = userWindow.aggregate({
        count: true,
        sum: ['data.amount']
      });
      
      userStats[userId] = {
        events: stats.count,
        revenue: stats.sum?.['data.amount'] || 0,
        purchases: messages.filter(m => m.data.type === 'purchase').length,
        clicks: messages.filter(m => m.data.type === 'click').length
      };
    }
    
    console.log('   User Statistics:');
    for (const [userId, stats] of Object.entries(userStats)) {
      console.log(`     ${userId}:`);
      console.log(`       Events: ${stats.events}`);
      console.log(`       Clicks: ${stats.clicks}`);
      console.log(`       Purchases: ${stats.purchases}`);
      console.log(`       Revenue: $${stats.revenue.toFixed(2)}`);
    }
    
    // Simulate processing work
    await new Promise(resolve => setTimeout(resolve, 100));
    
    if (windowCount >= 2) {
      console.log(`\n✅ Advanced processing complete`);
      consumer.stop();
    }
  }).catch(err => {
    console.error('Error in advanced processing:', err);
  });
}

// ============================================================================
// STEP 7: Seek Example
// ============================================================================

async function seekExample() {
  console.log('\n⏪ Step 7: Seek example...');
  
  const consumer = queen.consumer('user-activity-global', 'analytics-seek-test');
  
  // Seek to a specific time (1 hour ago)
  const oneHourAgo = new Date(Date.now() - 60 * 60 * 1000).toISOString();
  
  console.log(`   Seeking to: ${oneHourAgo}`);
  await consumer.seek(oneHourAgo);
  console.log('✅ Seek completed');
  
  // Now consumer will start from that timestamp
  console.log('   (Next poll will start from the seeked position)');
}

// ============================================================================
// Main Execution
// ============================================================================

async function main() {
  console.log('\n========================================');
  console.log('Queen Streaming V3 - Complete Example');
  console.log('========================================\n');
  
  try {
    // Setup
    await defineSourceQueues();
    await defineStreams();
    
    // Produce data
    await produceTestData();
    
    // Run consumers in parallel (for demo, in practice you'd run these separately)
    console.log('\n⏳ Consumers will poll until they get windows or time out...\n');
    
    await Promise.race([
      consumeGlobalStream(),
      new Promise(resolve => setTimeout(resolve, 40000))  // 40s timeout
    ]);
    
    await Promise.race([
      consumePartitionedStream(),
      new Promise(resolve => setTimeout(resolve, 40000))  // 40s timeout
    ]);
    
    await Promise.race([
      advancedProcessing(),
      new Promise(resolve => setTimeout(resolve, 40000))  // 40s timeout
    ]);
    
    // Seek example
    await seekExample();
    
    console.log('\n========================================');
    console.log('✅ All streaming examples completed!');
    console.log('========================================\n');
    
  } catch (error) {
    console.error('\n❌ Error:', error);
    console.error(error.stack);
  } finally {
    await queen.close();
    process.exit(0);
  }
}

// Run
main().catch(console.error);

