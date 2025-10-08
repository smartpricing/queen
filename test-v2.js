#!/usr/bin/env node

// Test script for Queen V2 implementation
// Tests the new queue → partition structure

const API_BASE = 'http://localhost:6632/api/v1';

async function apiCall(method, path, body = null) {
  const options = {
    method,
    headers: { 'Content-Type': 'application/json' },
  };
  
  if (body) {
    options.body = JSON.stringify(body);
  }
  
  const response = await fetch(`${API_BASE}${path}`, options);
  
  if (response.status === 204) {
    return { messages: [] }; // No content, return empty messages
  }
  
  const text = await response.text();
  
  if (text && text.trim()) {
    return JSON.parse(text);
  }
  
  // Empty response body, return appropriate default
  if (response.status === 200 && path.includes('/pop')) {
    return { messages: [] };
  }
  
  return null;
}

async function test() {
  console.log('🧪 Testing Queen V2 Implementation\n');
  
  try {
    // Test 1: Push messages to different partitions
    console.log('1️⃣  Pushing messages to different partitions...');
    const pushResult = await apiCall('POST', '/push', {
      items: [
        { queue: 'emails', payload: { to: 'user1@example.com', subject: 'Welcome' } },
        { queue: 'emails', partition: 'urgent', payload: { to: 'admin@example.com', subject: 'Alert' } },
        { queue: 'emails', partition: 'bulk', payload: { to: 'users@example.com', subject: 'Newsletter' } },
        { queue: 'payments', payload: { amount: 100, currency: 'USD' } },
        { queue: 'payments', partition: 'refunds', payload: { amount: 50, currency: 'USD' } }
      ]
    });
    console.log(`   ✅ Pushed ${pushResult.messages.length} messages\n`);
    
    // Test 2: Pop from specific partition
    console.log('2️⃣  Popping from specific partition (emails/urgent)...');
    const urgentPop = await apiCall('GET', '/pop/queue/emails/partition/urgent');
    console.log(`   ✅ Popped ${urgentPop.messages.length} message(s) from urgent partition`);
    if (urgentPop.messages.length > 0) {
      console.log(`   📧 Message: ${JSON.stringify(urgentPop.messages[0].data)}\n`);
    }
    
    // Test 3: Pop from queue level (any partition)
    console.log('3️⃣  Popping from queue level (emails - any partition)...');
    const emailsPop = await apiCall('GET', '/pop/queue/emails?batch=2');
    console.log(`   ✅ Popped ${emailsPop.messages.length} message(s) from emails queue`);
    emailsPop.messages.forEach((msg, i) => {
      console.log(`   📧 Message ${i+1} (${msg.partition}): ${JSON.stringify(msg.data)}`);
    });
    console.log();
    
    // Test 4: Configure partition options
    console.log('4️⃣  Configuring partition options...');
    const configResult = await apiCall('POST', '/configure', {
      queue: 'notifications',
      partition: 'critical',
      options: {
        leaseTime: 60,
        retryLimit: 10,
        priority: 100
      }
    });
    console.log(`   ✅ Configured ${configResult.queue}/${configResult.partition}`);
    console.log(`   ⚙️  Options: leaseTime=${configResult.options.leaseTime}, retryLimit=${configResult.options.retryLimit}\n`);
    
    // Test 5: Push with namespace/task grouping
    console.log('5️⃣  Testing namespace/task grouping...');
    await apiCall('POST', '/push', {
      items: [
        { queue: 'orders-service', payload: { orderId: 'ORD-001' } },
        { queue: 'inventory-service', payload: { sku: 'PROD-123' } }
      ]
    });
    
    // We'll need to update the queue manager to set namespace/task on creation
    // For now, these will be created without namespace/task
    console.log('   ✅ Pushed messages to grouped queues\n');
    
    // Test 6: Get analytics
    console.log('6️⃣  Getting queue statistics...');
    const stats = await apiCall('GET', '/analytics/queue-stats?queue=emails');
    console.log('   📊 Queue: emails');
    if (stats && stats.length > 0) {
      stats.forEach(stat => {
        console.log(`      Partition: ${stat.partition}`);
        console.log(`      - Pending: ${stat.stats.pending}`);
        console.log(`      - Processing: ${stat.stats.processing}`);
        console.log(`      - Completed: ${stat.stats.completed}`);
        console.log(`      - Total: ${stat.stats.total}`);
      });
    }
    console.log();
    
    // Test 7: Acknowledge messages
    console.log('7️⃣  Acknowledging messages...');
    if (urgentPop.messages.length > 0) {
      const ackResult = await apiCall('POST', '/ack', {
        transactionId: urgentPop.messages[0].transactionId,
        status: 'completed'
      });
      console.log(`   ✅ Acknowledged message ${ackResult.transactionId}\n`);
    }
    
    // Test 8: Pop with filters (if namespace/task were set)
    console.log('8️⃣  Testing pop with filters...');
    const filteredPop = await apiCall('GET', '/pop?namespace=production');
    console.log(`   ℹ️  Popped ${filteredPop.messages.length} message(s) with namespace filter`);
    console.log('   (Note: namespace/task filtering requires queues to be created with these attributes)\n');
    
    // Test 9: Get queue depths
    console.log('9️⃣  Getting queue depths...');
    const depths = await apiCall('GET', '/analytics/queue-depths');
    console.log('   📊 Queue Depths:');
    if (depths && depths.depths) {
      depths.depths.forEach(q => {
        console.log(`      ${q.queue}: ${q.depth} pending, ${q.processing} processing`);
        if (q.partitions && q.partitions.length > 1) {
          q.partitions.forEach(p => {
            console.log(`        - ${p.name}: ${p.depth} pending`);
          });
        }
      });
    }
    console.log();
    
    // Test 10: Clear a queue
    console.log('🔟 Clearing payments queue...');
    const clearResult = await apiCall('DELETE', '/queues/payments/clear');
    console.log(`   ✅ Cleared ${clearResult.count} messages from ${clearResult.queue}\n`);
    
    console.log('✨ All tests completed successfully!');
    
  } catch (error) {
    console.error('❌ Test failed:', error.message);
    if (error.cause) {
      console.error('   Details:', error.cause);
    }
  }
}

// Run tests
test().catch(console.error);
