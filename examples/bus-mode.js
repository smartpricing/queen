#!/usr/bin/env node

/**
 * Bus Mode Example for Queen V3
 * 
 * This example demonstrates the new bus/pub-sub functionality where
 * multiple consumer groups can receive ALL messages independently,
 * similar to Kafka's consumer groups.
 * 
 * Each consumer group maintains its own progress through the message stream.
 */

import { createQueenClient } from '../src/client/index.js';

async function main() {
  const client = createQueenClient({
    baseUrl: process.env.QUEEN_URL || 'http://localhost:6632',
    timeout: 35000
  });

  const queue = 'events';
  
  console.log('🚀 Starting Bus Mode Example');
  console.log('📦 Queue:', queue);
  console.log('');
  console.log('This example will:');
  console.log('1. Push some test messages to the queue');
  console.log('2. Start 3 different consumer groups');
  console.log('3. Show how each group receives ALL messages independently');
  console.log('');
  
  // First, push some test messages
  console.log('📤 Pushing test messages...');
  const testMessages = [];
  for (let i = 1; i <= 5; i++) {
    testMessages.push({
      queue,
      payload: {
        id: i,
        type: 'test-event',
        timestamp: new Date().toISOString(),
        data: `Message ${i}`
      }
    });
  }
  
  await client.push({ items: testMessages });
  console.log(`✅ Pushed ${testMessages.length} messages\n`);
  
  // Track messages received by each consumer group
  const received = {
    analytics: [],
    notifications: [],
    audit: []
  };
  
  // Consumer Group 1: Analytics Service
  console.log('🔵 Starting Analytics Consumer Group...');
  const analyticsConsumer = client.consume({
    queue,
    consumerGroup: 'analytics-service',
    handler: async (message) => {
      console.log(`  [Analytics] Received message ${message.data.id}: ${message.data.data}`);
      received.analytics.push(message.data.id);
      
      // Simulate processing
      await new Promise(resolve => setTimeout(resolve, 100));
    },
    options: {
      batch: 2,
      wait: false  // Don't wait for new messages in this example
    }
  });
  
  // Consumer Group 2: Notification Service
  console.log('🟢 Starting Notifications Consumer Group...');
  const notificationConsumer = client.consume({
    queue,
    consumerGroup: 'notification-service',
    handler: async (message) => {
      console.log(`  [Notifications] Received message ${message.data.id}: ${message.data.data}`);
      received.notifications.push(message.data.id);
      
      // Simulate processing
      await new Promise(resolve => setTimeout(resolve, 150));
    },
    options: {
      batch: 1,
      wait: false
    }
  });
  
  // Consumer Group 3: Audit Log Service
  console.log('🟡 Starting Audit Log Consumer Group...');
  const auditConsumer = client.consume({
    queue,
    consumerGroup: 'audit-log',
    handler: async (message) => {
      console.log(`  [Audit] Received message ${message.data.id}: ${message.data.data}`);
      received.audit.push(message.data.id);
      
      // Simulate processing
      await new Promise(resolve => setTimeout(resolve, 50));
    },
    options: {
      batch: 3,
      wait: false
    }
  });
  
  // Wait for all messages to be processed
  console.log('\n⏳ Processing messages...\n');
  await new Promise(resolve => setTimeout(resolve, 3000));
  
  // Stop all consumers
  analyticsConsumer();
  notificationConsumer();
  auditConsumer();
  
  // Display results
  console.log('\n📊 Results:');
  console.log('═══════════════════════════════════════════');
  console.log(`Analytics Service received: [${received.analytics.sort().join(', ')}]`);
  console.log(`Notification Service received: [${received.notifications.sort().join(', ')}]`);
  console.log(`Audit Log Service received: [${received.audit.sort().join(', ')}]`);
  console.log('');
  
  // Verify all groups received all messages
  const allReceived = 
    received.analytics.length === testMessages.length &&
    received.notifications.length === testMessages.length &&
    received.audit.length === testMessages.length;
  
  if (allReceived) {
    console.log('✅ SUCCESS: All consumer groups received ALL messages!');
    console.log('   This demonstrates bus/pub-sub mode where each consumer group');
    console.log('   gets its own copy of every message, independently.');
  } else {
    console.log('⚠️  Some messages were not received by all groups.');
    console.log('   This might be due to timing. Try running again.');
  }
  
  console.log('\n🎯 Key Differences from Queue Mode:');
  console.log('  • In Queue Mode: Messages are consumed by ONE worker');
  console.log('  • In Bus Mode: Messages are consumed by ALL consumer groups');
  console.log('  • Each group maintains its own progress/offset');
  console.log('  • Perfect for: analytics, notifications, audit logs, etc.');
}

// Demonstrate subscription modes
async function demonstrateSubscriptionModes() {
  const client = createQueenClient({
    baseUrl: process.env.QUEEN_URL || 'http://localhost:6632'
  });
  
  const queue = 'subscription-test';
  
  console.log('\n\n📅 Demonstrating Subscription Modes');
  console.log('════════════════════════════════════════════');
  
  // Push some historical messages
  console.log('📤 Pushing historical messages...');
  for (let i = 1; i <= 3; i++) {
    await client.push({
      items: [{
        queue,
        payload: { id: i, type: 'historical', data: `Historical message ${i}` }
      }]
    });
  }
  
  console.log('✅ Historical messages pushed\n');
  
  // Consumer 1: Consume all messages (default)
  console.log('🔵 Consumer Group "all-messages" (default mode):');
  const allMessages = [];
  const consumer1 = client.consume({
    queue,
    consumerGroup: 'all-messages',
    handler: async (msg) => {
      console.log(`  Received: ${msg.data.type} - ${msg.data.data}`);
      allMessages.push(msg.data.id);
    },
    options: { wait: false }
  });
  
  await new Promise(resolve => setTimeout(resolve, 1000));
  consumer1();
  
  // Consumer 2: Only new messages from subscription time
  console.log('\n🟢 Consumer Group "new-only" (subscriptionMode: "new"):');
  const newOnly = [];
  const consumer2 = client.consume({
    queue,
    consumerGroup: 'new-only',
    handler: async (msg) => {
      console.log(`  Received: ${msg.data.type} - ${msg.data.data}`);
      newOnly.push(msg.data.id);
    },
    options: { 
      wait: false,
      subscriptionMode: 'new'
    }
  });
  
  // Push new messages after subscription
  console.log('\n📤 Pushing new messages...');
  for (let i = 4; i <= 6; i++) {
    await client.push({
      items: [{
        queue,
        payload: { id: i, type: 'new', data: `New message ${i}` }
      }]
    });
  }
  
  await new Promise(resolve => setTimeout(resolve, 1000));
  consumer2();
  
  // Display results
  console.log('\n📊 Subscription Mode Results:');
  console.log('════════════════════════════════════');
  console.log(`"all-messages" group received: [${allMessages.sort().join(', ')}] (all 6 messages)`);
  console.log(`"new-only" group received: [${newOnly.sort().join(', ')}] (only new messages)`);
  console.log('');
  console.log('✅ This demonstrates how consumer groups can choose when to start consuming!');
}

// Run the examples
main()
  .then(() => demonstrateSubscriptionModes())
  .then(() => {
    console.log('\n✨ Bus mode examples completed!');
    process.exit(0);
  })
  .catch(error => {
    console.error('❌ Error:', error);
    process.exit(1);
  });
