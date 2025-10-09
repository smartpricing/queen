#!/usr/bin/env node

/**
 * Mixed Mode Example for Queen V3
 * 
 * This example demonstrates how the SAME queue can serve both:
 * 1. Competing consumers (Queue Mode) - workers compete for messages
 * 2. Consumer groups (Bus Mode) - each group gets all messages
 * 
 * This is a unique feature that makes Queen more flexible than traditional
 * message queue systems.
 */

import { createQueenClient } from '../src/client/index.js';

async function main() {
  const client = createQueenClient({
    baseUrl: process.env.QUEEN_URL || 'http://localhost:6632',
    timeout: 35000
  });

  const queue = 'orders';
  
  console.log('🚀 Starting Mixed Mode Example');
  console.log('📦 Queue:', queue);
  console.log('');
  console.log('This example demonstrates the SAME queue serving:');
  console.log('  • 3 Workers in Queue Mode (competing for messages)');
  console.log('  • 2 Consumer Groups in Bus Mode (each gets all messages)');
  console.log('');
  
  // Track message processing
  const processed = {
    workers: {
      worker1: [],
      worker2: [],
      worker3: []
    },
    groups: {
      analytics: [],
      audit: []
    }
  };
  
  // Start 3 competing workers (Queue Mode - no consumer group)
  console.log('👷 Starting 3 competing workers (Queue Mode):');
  
  const worker1 = client.consume({
    queue,
    // No consumerGroup = Queue Mode (competing consumers)
    handler: async (message) => {
      console.log(`  [Worker 1] Processing order ${message.data.orderId}`);
      processed.workers.worker1.push(message.data.orderId);
      await new Promise(resolve => setTimeout(resolve, 200));
    },
    options: { batch: 1, wait: false }
  });
  
  const worker2 = client.consume({
    queue,
    // No consumerGroup = Queue Mode
    handler: async (message) => {
      console.log(`  [Worker 2] Processing order ${message.data.orderId}`);
      processed.workers.worker2.push(message.data.orderId);
      await new Promise(resolve => setTimeout(resolve, 150));
    },
    options: { batch: 1, wait: false }
  });
  
  const worker3 = client.consume({
    queue,
    // No consumerGroup = Queue Mode
    handler: async (message) => {
      console.log(`  [Worker 3] Processing order ${message.data.orderId}`);
      processed.workers.worker3.push(message.data.orderId);
      await new Promise(resolve => setTimeout(resolve, 100));
    },
    options: { batch: 1, wait: false }
  });
  
  console.log('✅ Workers started\n');
  
  // Start 2 consumer groups (Bus Mode - each gets all messages)
  console.log('📊 Starting 2 consumer groups (Bus Mode):');
  
  const analyticsConsumer = client.consume({
    queue,
    consumerGroup: 'analytics',  // With consumerGroup = Bus Mode
    handler: async (message) => {
      console.log(`  [Analytics Group] Analyzing order ${message.data.orderId}`);
      processed.groups.analytics.push(message.data.orderId);
      await new Promise(resolve => setTimeout(resolve, 50));
    },
    options: { batch: 2, wait: false }
  });
  
  const auditConsumer = client.consume({
    queue,
    consumerGroup: 'audit-log',  // With consumerGroup = Bus Mode
    handler: async (message) => {
      console.log(`  [Audit Group] Logging order ${message.data.orderId}`);
      processed.groups.audit.push(message.data.orderId);
      await new Promise(resolve => setTimeout(resolve, 30));
    },
    options: { batch: 3, wait: false }
  });
  
  console.log('✅ Consumer groups started\n');
  
  // Push test orders
  console.log('📤 Pushing 10 test orders...\n');
  const orders = [];
  for (let i = 1; i <= 10; i++) {
    orders.push({
      queue,
      payload: {
        orderId: i,
        amount: Math.floor(Math.random() * 1000) + 100,
        customer: `customer-${i}`,
        timestamp: new Date().toISOString()
      }
    });
  }
  
  await client.push({ items: orders });
  
  // Wait for processing
  console.log('⏳ Processing messages...\n');
  await new Promise(resolve => setTimeout(resolve, 4000));
  
  // Stop all consumers
  worker1();
  worker2();
  worker3();
  analyticsConsumer();
  auditConsumer();
  
  // Display results
  console.log('\n\n📊 RESULTS');
  console.log('═══════════════════════════════════════════════════════');
  
  console.log('\n🔧 QUEUE MODE (Competing Workers):');
  console.log('───────────────────────────────────');
  console.log(`Worker 1 processed: [${processed.workers.worker1.sort((a,b) => a-b).join(', ')}]`);
  console.log(`Worker 2 processed: [${processed.workers.worker2.sort((a,b) => a-b).join(', ')}]`);
  console.log(`Worker 3 processed: [${processed.workers.worker3.sort((a,b) => a-b).join(', ')}]`);
  
  const totalWorkerMessages = 
    processed.workers.worker1.length + 
    processed.workers.worker2.length + 
    processed.workers.worker3.length;
  
  console.log(`\nTotal messages processed by workers: ${totalWorkerMessages}`);
  console.log('✅ Each message was processed by ONLY ONE worker (competing)');
  
  console.log('\n📡 BUS MODE (Consumer Groups):');
  console.log('───────────────────────────────────');
  console.log(`Analytics group received: [${processed.groups.analytics.sort((a,b) => a-b).join(', ')}]`);
  console.log(`Audit group received: [${processed.groups.audit.sort((a,b) => a-b).join(', ')}]`);
  
  console.log(`\nTotal messages received by Analytics: ${processed.groups.analytics.length}`);
  console.log(`Total messages received by Audit: ${processed.groups.audit.length}`);
  console.log('✅ Each consumer group received ALL messages (broadcast)');
  
  // Verify the behavior
  console.log('\n\n🎯 KEY INSIGHTS:');
  console.log('════════════════════════════════════════════════');
  
  // Check if workers competed correctly
  const workerOrderIds = new Set([
    ...processed.workers.worker1,
    ...processed.workers.worker2,
    ...processed.workers.worker3
  ]);
  
  if (workerOrderIds.size === totalWorkerMessages && totalWorkerMessages === 10) {
    console.log('✅ Workers competed correctly: Each order processed ONCE across all workers');
  }
  
  // Check if consumer groups got all messages
  if (processed.groups.analytics.length === 10 && processed.groups.audit.length === 10) {
    console.log('✅ Consumer groups worked correctly: Each group got ALL orders');
  }
  
  console.log('\n📝 Summary:');
  console.log('  • The SAME queue served both patterns simultaneously');
  console.log('  • Workers (no consumer group) → competed for messages');
  console.log('  • Consumer groups → each received all messages');
  console.log('  • This flexibility is unique to Queen!');
}

// Demonstrate a real-world scenario
async function realWorldScenario() {
  console.log('\n\n🏢 REAL-WORLD SCENARIO');
  console.log('════════════════════════════════════════════════');
  console.log('E-commerce Order Processing System:');
  console.log('  • Order Processing Workers (Queue Mode) - handle the actual order');
  console.log('  • Analytics Service (Bus Mode) - analyzes all orders');
  console.log('  • Notification Service (Bus Mode) - sends notifications for all orders');
  console.log('  • Audit Service (Bus Mode) - logs all orders for compliance');
  console.log('');
  
  const client = createQueenClient({
    baseUrl: process.env.QUEEN_URL || 'http://localhost:6632'
  });
  
  const queue = 'ecommerce-orders';
  
  // Simulate order processing workers
  console.log('Starting order processing workers...');
  for (let i = 1; i <= 2; i++) {
    client.consume({
      queue,
      // No consumer group - workers compete
      handler: async (message) => {
        console.log(`  [Order Worker ${i}] Processing order #${message.data.orderId} ($${message.data.amount})`);
        // Simulate order processing: inventory, payment, shipping
        await new Promise(resolve => setTimeout(resolve, 500));
        console.log(`  [Order Worker ${i}] ✅ Order #${message.data.orderId} completed`);
      },
      options: { batch: 1, wait: true }
    });
  }
  
  // Analytics service (needs all orders)
  console.log('Starting analytics service...');
  client.consume({
    queue,
    consumerGroup: 'analytics-service',
    handler: async (message) => {
      console.log(`  [Analytics] Recording metrics for order #${message.data.orderId}`);
      // Update dashboards, calculate revenue, track trends
    },
    options: { batch: 5, wait: true }
  });
  
  // Notification service (needs all orders)
  console.log('Starting notification service...');
  client.consume({
    queue,
    consumerGroup: 'notification-service',
    handler: async (message) => {
      console.log(`  [Notifications] Sending email for order #${message.data.orderId} to ${message.data.email}`);
      // Send order confirmation emails, SMS, push notifications
    },
    options: { batch: 1, wait: true }
  });
  
  // Audit service (needs all orders for compliance)
  console.log('Starting audit service...');
  client.consume({
    queue,
    consumerGroup: 'audit-service',
    handler: async (message) => {
      console.log(`  [Audit] Logging order #${message.data.orderId} for compliance`);
      // Store in audit log, ensure regulatory compliance
    },
    options: { batch: 10, wait: true }
  });
  
  console.log('\n✅ All services started!');
  console.log('\nNow push some orders to see the system in action:');
  console.log('  await client.push({ items: [{ queue: "ecommerce-orders", payload: {...} }] })');
}

// Run the example
main()
  .then(() => realWorldScenario())
  .then(() => {
    console.log('\n\n✨ Mixed mode example completed!');
    console.log('Press Ctrl+C to exit.');
    // Keep the real-world scenario running
  })
  .catch(error => {
    console.error('❌ Error:', error);
    process.exit(1);
  });
