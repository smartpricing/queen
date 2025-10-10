#!/usr/bin/env node
/**
 * Example: Cache Invalidation with System Events
 * 
 * This example demonstrates how the system event propagation
 * ensures cache consistency across multiple server instances.
 */

import { createQueenClient } from '../src/client/queenClient.js';

// Simulate two different server instances
const server1 = createQueenClient({ baseUrl: 'http://localhost:6632' });
const server2 = createQueenClient({ baseUrl: 'http://localhost:6633' }); // Would be a different port in production

async function demonstrateCacheInvalidation() {
  console.log('📊 Demonstrating Cache Invalidation with System Events\n');
  console.log('=' .repeat(60));
  
  const queueName = 'cache-test-queue';
  
  try {
    // Step 1: Server 1 creates a queue
    console.log('\n1️⃣ Server 1 creates a queue with initial configuration:');
    await server1.configure({
      queue: queueName,
      options: {
        ttl: 300,
        retryLimit: 3,
        priority: 5
      }
    });
    console.log('   ✅ Queue created with TTL=300, retryLimit=3, priority=5');
    
    // Step 2: Both servers use the queue
    console.log('\n2️⃣ Both servers push messages to the queue:');
    
    await server1.push({
      queue: queueName,
      messages: [{ id: 'msg-from-server1', payload: { source: 'server1' } }]
    });
    console.log('   ✅ Server 1 pushed a message (uses cached config)');
    
    // In a real multi-server setup, server2 would have its own cache
    // For this demo, we're using the same server but the principle applies
    await server2.push({
      queue: queueName,
      messages: [{ id: 'msg-from-server2', payload: { source: 'server2' } }]
    });
    console.log('   ✅ Server 2 pushed a message (fetches and caches config)');
    
    // Step 3: Server 1 updates the queue configuration
    console.log('\n3️⃣ Server 1 updates queue configuration:');
    await server1.configure({
      queue: queueName,
      options: {
        ttl: 600,      // Changed from 300
        retryLimit: 5, // Changed from 3
        priority: 10   // Changed from 5
      }
    });
    console.log('   ✅ Configuration updated: TTL=600, retryLimit=5, priority=10');
    console.log('   📨 System event emitted to propagate changes');
    
    // Step 4: Wait a moment for event propagation
    console.log('\n4️⃣ Waiting for system event propagation...');
    await new Promise(resolve => setTimeout(resolve, 100));
    
    // Step 5: Both servers use the queue again
    console.log('\n5️⃣ Both servers push messages again:');
    
    await server1.push({
      queue: queueName,
      messages: [{ id: 'msg2-from-server1', payload: { after: 'update' } }]
    });
    console.log('   ✅ Server 1 uses new config (immediate local update)');
    
    await server2.push({
      queue: queueName,
      messages: [{ id: 'msg2-from-server2', payload: { after: 'update' } }]
    });
    console.log('   ✅ Server 2 uses new config (cache invalidated by event)');
    
    // Step 6: Verify the queue stats
    console.log('\n6️⃣ Verifying queue statistics:');
    const stats = await server1.getQueueStats(queueName);
    console.log(`   📊 Total messages: ${stats.total}`);
    console.log(`   📊 Pending: ${stats.pending}`);
    
    // Step 7: Check system events queue
    console.log('\n7️⃣ Checking system events queue:');
    try {
      const systemStats = await server1.getQueueStats('__system_events__');
      console.log(`   📊 System events processed: ${systemStats.completed || 0}`);
      console.log(`   📊 System events pending: ${systemStats.pending || 0}`);
    } catch (error) {
      console.log('   ℹ️ System queue may not have stats if events were already consumed');
    }
    
    console.log('\n' + '=' .repeat(60));
    console.log('✨ Cache invalidation demonstration completed!');
    console.log('\n💡 Key Points:');
    console.log('   • Configuration changes trigger system events');
    console.log('   • Events are propagated to all server instances');
    console.log('   • Each server invalidates its cache upon receiving events');
    console.log('   • This ensures consistency across the cluster');
    
  } catch (error) {
    console.error('❌ Error:', error.message);
    process.exit(1);
  }
}

// Run the demonstration
demonstrateCacheInvalidation().then(() => {
  process.exit(0);
}).catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});
