#!/usr/bin/env node

/**
 * Complete test of the minimalist Queen client
 * Shows all features including namespace/task and message properties
 */

import { Queen } from '../src/client/client.js';

async function main() {
  console.log('🧪 Testing Complete Minimalist Queen Client Features\n');
  
  const client = new Queen({
    baseUrls: ['http://localhost:6632'],
    timeout: 30000
  });
  
  try {
    // ============================================
    // 1. Queue Configuration with Namespace/Task
    // ============================================
    console.log('1️⃣ Configuring queues with namespace and task...');
    
    // Configure queue with namespace and task
    await client.queue('orders-processing', {
      leaseTime: 300,
      retryLimit: 3,
      priority: 10
    }, {
      namespace: 'ecommerce',
      task: 'process'
    });
    
    await client.queue('inventory-updates', {
      leaseTime: 300,
      priority: 5
    }, {
      namespace: 'ecommerce',
      task: 'update'
    });
    
    await client.queue('notifications', {
      priority: 8
    }, {
      namespace: 'ecommerce',
      task: 'notify'
    });
    
    console.log('✅ Queues configured with namespace/task metadata\n');
    
    // ============================================
    // 2. Push with Message Properties
    // ============================================
    console.log('2️⃣ Pushing messages with properties...');
    
    // Method 1: Properties in options
    await client.push('orders-processing', {
      orderId: 123,
      amount: 99.99
    }, {
      transactionId: 'txn-123'
    });
    
    // Method 2: Properties in the message object
    await client.push('orders-processing', {
      orderId: 124,
      amount: 199.99,
      transactionId: 'txn-124'  // Will be extracted
    });
    
    // Method 3: Batch with mixed properties
    await client.push('inventory-updates', [
      {
        sku: 'PROD-001',
        quantity: 5,
        transactionId: 'inv-001'
      },
      {
        sku: 'PROD-002',
        quantity: 10,
        transactionId: 'inv-002'
      }
    ]);
    
    // Method 4: Using special properties format
    await client.push('notifications', {
      _payload: { userId: 789, message: 'Order shipped' },
      _transactionId: 'notif-001'
    });
    
    console.log('✅ Messages pushed with various property patterns\n');
    
    // ============================================
    // 3. Take by Queue/Partition
    // ============================================
    console.log('3️⃣ Taking messages by queue/partition...');
    
    // Take from specific queue
    let count = 0;
    for await (const msg of client.take('orders-processing', { limit: 2 })) {
      count++;
      console.log(`  📩 Order ${count}:`, msg.data);
      console.log(`     Transaction ID: ${msg.transactionId}`);
      await client.ack(msg);
    }
    console.log('✅ Processed orders\n');
    
    // ============================================
    // 4. Take by Namespace/Task
    // ============================================
    console.log('4️⃣ Taking messages by namespace/task...');
    
    // Take all messages from ecommerce namespace
    count = 0;
    for await (const msg of client.take('namespace:ecommerce', { limit: 2 })) {
      count++;
      console.log(`  📦 Namespace message ${count} from ${msg.queue}:`, msg.data);
      await client.ack(msg);
    }
    
    // Take only 'update' tasks
    for await (const msg of client.take('task:update', { limit: 2 })) {
      console.log(`  🔄 Update task from ${msg.queue}:`, msg.data);
      await client.ack(msg);
    }
    
    // Combine namespace and task
    for await (const msg of client.take('namespace:ecommerce/task:notify', { limit: 1 })) {
      console.log(`  🔔 Notification:`, msg.data);
      await client.ack(msg);
    }
    
    console.log('✅ Processed messages by namespace/task\n');
    
    // ============================================
    // 5. Consumer Groups with Subscription Modes
    // ============================================
    console.log('5️⃣ Testing consumer groups with subscription modes...');
    
    // Configure events queue first
    await client.queue('events', {
      leaseTime: 300,
      retryLimit: 3
    });
    
    // Push test events
    await client.push('events', [
      { event: 'user.signup', userId: 1 },
      { event: 'user.login', userId: 2 },
      { event: 'user.purchase', userId: 3 }
    ]);
    
    // Consumer group starting from all messages
    for await (const event of client.take('events@analytics', { 
      limit: 1,
      subscriptionMode: 'all'
    })) {
      console.log('  📊 Analytics (all):', event.data);
      await client.ack(event, true, { group: 'analytics' });
    }
    
    // Consumer group starting from new messages only
    for await (const event of client.take('events@realtime', { 
      limit: 1,
      subscriptionMode: 'new'
    })) {
      console.log('  ⚡ Realtime (new only):', event.data);
      await client.ack(event, true, { group: 'realtime' });
    }
    
    console.log('✅ Consumer groups tested\n');
    
    // ============================================
    // 6. Advanced Patterns
    // ============================================
    console.log('6️⃣ Testing advanced patterns...');
    
    // Configure tasks queue
    await client.queue('tasks', {
      leaseTime: 300,
      priority: 8
    });
    
    // Pattern 1: Partitioned messages with properties
    await client.push('tasks/urgent', {
      taskId: 'TASK-001',
      priority: 'critical'
    }, {
      transactionId: 'task-txn-001'
    });
    
    // Pattern 2: Consumer group on specific partition
    for await (const task of client.take('tasks/urgent@workers', { limit: 1 })) {
      console.log('  🚨 Urgent task for worker group:', task.data);
      await client.ack(task, true, { group: 'workers' });
    }
    
    // Configure logs queue
    await client.queue('logs', {
      leaseTime: 300,
      retentionSeconds: 86400  // Keep for 1 day
    });
    
    // Pattern 3: Batch push with partition
    await client.push('logs/audit', [
      { action: 'login', user: 'alice', timestamp: Date.now() },
      { action: 'update', user: 'bob', timestamp: Date.now() },
      { action: 'delete', user: 'charlie', timestamp: Date.now() }
    ]);
    
    console.log('✅ Advanced patterns tested\n');
    
    // ============================================
    // 7. Error Handling
    // ============================================
    console.log('7️⃣ Testing error handling...');
    
    // Configure error-test queue
    await client.queue('error-test', {
      leaseTime: 60,
      retryLimit: 2
    });
    
    // Push a message that will fail processing
    await client.push('error-test', { 
      willFail: true,
      reason: 'Testing failure path'
    });
    
    for await (const msg of client.take('error-test', { limit: 1 })) {
      try {
        // Simulate processing error
        throw new Error('Simulated processing error');
      } catch (error) {
        console.log('  ❌ Failed to process:', msg.data);
        // Acknowledge with failure and error context
        await client.ack(msg, false, { 
          error: error.message,
          retryable: true 
        });
      }
    }
    
    console.log('✅ Error handling tested\n');
    
    console.log('🎉 All features tested successfully!');
    
  } catch (error) {
    console.error('❌ Test failed:', error.message);
    process.exit(1);
  } finally {
    await client.close();
  }
}

// Run tests
main().catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});
