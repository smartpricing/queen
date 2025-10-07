#!/usr/bin/env node
import { createQueenClient } from './src/client/index.js';

const client = createQueenClient({
  baseUrl: 'http://localhost:6632',
  timeout: 30000,
  retryAttempts: 3
});

const TEST_NS = 'test-client';
const TEST_TASK = 'demo';
const TEST_QUEUE = 'messages';

const runDemo = async () => {
  console.log('Queen Client SDK Demo\n');
  
  // 1. Configure queue with advanced features
  console.log('1. Configuring queue with delayed processing...');
  await client.configure({
    ns: TEST_NS,
    task: TEST_TASK,
    queue: TEST_QUEUE,
    options: {
      leaseTime: 60,
      priority: 10,
      delayedProcessing: 5, // Wait 5 seconds before processing
      retryLimit: 3
    }
  });
  console.log('   ✅ Queue configured\n');
  
  // 2. Push some messages
  console.log('2. Pushing messages...');
  const pushResult = await client.push({
    items: [
      {
        ns: TEST_NS,
        task: TEST_TASK,
        queue: TEST_QUEUE,
        payload: { message: 'Hello from client SDK!', timestamp: Date.now() }
      },
      {
        ns: TEST_NS,
        task: TEST_TASK,
        queue: TEST_QUEUE,
        payload: { message: 'Second message', timestamp: Date.now() }
      }
    ]
  });
  console.log(`   ✅ Pushed ${pushResult.messages.length} messages\n`);
  
  // 3. Check analytics
  console.log('3. Checking queue analytics...');
  const stats = await client.analytics.namespace(TEST_NS);
  console.log(`   Namespace: ${stats.namespace}`);
  console.log(`   Total pending: ${stats.totals.pending}`);
  console.log(`   Total processing: ${stats.totals.processing}`);
  console.log(`   Total completed: ${stats.totals.completed}\n`);
  
  // 4. Pop messages (will wait due to delayedProcessing)
  console.log('4. Popping messages (waiting for delayed processing)...');
  console.log('   Note: Messages have 5-second delay before becoming available');
  
  const startTime = Date.now();
  const popResult = await client.pop({
    ns: TEST_NS,
    task: TEST_TASK,
    queue: TEST_QUEUE,
    wait: true,
    timeout: 10000,
    batch: 2
  });
  
  const waitTime = Date.now() - startTime;
  console.log(`   ✅ Got ${popResult.messages.length} messages after ${Math.round(waitTime/1000)}s`);
  
  for (const msg of popResult.messages) {
    console.log(`   - Message: ${JSON.stringify(msg.payload)}`);
  }
  console.log();
  
  // 5. Acknowledge messages
  console.log('5. Acknowledging messages...');
  for (const msg of popResult.messages) {
    await client.ack(msg.transactionId, 'completed');
    console.log(`   ✅ Acknowledged ${msg.transactionId}`);
  }
  console.log();
  
  // 6. Check throughput
  console.log('6. Checking throughput...');
  const throughput = await client.analytics.throughput();
  if (throughput.throughput.length > 0) {
    const latest = throughput.throughput[0];
    console.log(`   Latest: ${latest.messagesPerMinute} msg/min (${latest.messagesPerSecond} msg/sec)`);
  } else {
    console.log('   No throughput data yet');
  }
  console.log();
  
  // 7. Test batch operations
  console.log('7. Testing batch ACK...');
  
  // Push more messages
  const batchPush = await client.push({
    items: Array.from({ length: 5 }, (_, i) => ({
      ns: TEST_NS,
      task: TEST_TASK,
      queue: 'batch-test',
      payload: { id: i + 1, data: `Batch item ${i + 1}` }
    }))
  });
  
  // Pop them
  const batchPop = await client.pop({
    ns: TEST_NS,
    task: TEST_TASK,
    queue: 'batch-test',
    batch: 5
  });
  
  // Batch acknowledge
  if (batchPop.messages.length > 0) {
    const ackResult = await client.ackBatch(
      batchPop.messages.map((msg, i) => ({
        transactionId: msg.transactionId,
        status: i % 2 === 0 ? 'completed' : 'failed',
        error: i % 2 === 1 ? 'Simulated failure' : null
      }))
    );
    
    console.log(`   ✅ Batch acknowledged ${ackResult.processed} messages`);
    console.log(`   - Completed: ${ackResult.results.filter(r => r.status === 'completed' || r.status === 'pending').length}`);
    console.log(`   - Failed: ${ackResult.results.filter(r => r.status === 'failed' || r.status === 'dead_letter').length}\n`);
  } else {
    console.log('   No messages to acknowledge\n');
  }
  
  // 8. Test consumer helper
  console.log('8. Testing consumer helper (will process 3 messages)...');
  
  // Push messages for consumer
  await client.push({
    items: Array.from({ length: 3 }, (_, i) => ({
      ns: TEST_NS,
      task: TEST_TASK,
      queue: 'consumer-test',
      payload: { id: i + 1, message: `Consumer test ${i + 1}` }
    }))
  });
  
  let processed = 0;
  
  // Create promise that resolves when 3 messages are processed
  await new Promise((resolve) => {
    const stopFn = client.consume({
      ns: TEST_NS,
      task: TEST_TASK,
      queue: 'consumer-test',
      handler: async (message) => {
        console.log(`   Processing: ${JSON.stringify(message.payload)}`);
        processed++;
        if (processed >= 3) {
          stopFn();
          setTimeout(resolve, 100); // Give it time to stop
        }
      },
      options: {
        wait: false,
        batch: 1
      }
    });
  });
  
  console.log(`   ✅ Consumer processed ${processed} messages\n`);
  
  console.log('Demo completed successfully! 🎉');
};

runDemo().catch(error => {
  console.error('Demo failed:', error);
  process.exit(1);
});
