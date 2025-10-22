import { Queen } from '../client-js/client/index.js';

const client = new Queen({ baseUrls: ['http://localhost:6632'] });

console.log('🎉 Queen QoS 0 Event Streaming Demo\n');

// ============================================
// Example 1: QoS 0 Event Streaming
// ============================================

async function demoQoS0Buffering() {
  console.log('📊 Example 1: QoS 0 Buffering (High Performance)');
  console.log('================================================\n');
  
  const queue = 'demo-metrics-' + Date.now();
  
  // Create queue (no special config needed!)
  await client.queue(queue);
  
  console.log('Publishing 100 high-frequency events with buffering...');
  const start = Date.now();
  
  for (let i = 0; i < 100; i++) {
    // Push with buffering options (QoS 0)
    await client.push(queue, {
      cpu: Math.random() * 100,
      memory: Math.random() * 100,
      timestamp: Date.now()
    }, {
      bufferMs: 100,      // Server batches for 100ms
      bufferMax: 100      // Or until 100 events
    });
  }
  
  const pushTime = Date.now() - start;
  console.log(`✅ Published 100 events in ${pushTime}ms`);
  console.log(`   Server will batch these into ~1-10 DB writes (100x improvement)`);
  console.log(`   File: /var/lib/queen/buffers/qos0.buf\n`);
  
  // Wait for buffered events to flush
  console.log('⏳ Waiting 200ms for server to flush buffer...');
  await new Promise(resolve => setTimeout(resolve, 200));
  
  // Consume with auto-ack
  console.log('📥 Consuming with auto-ack (no manual ack needed)...\n');
  
  let count = 0;
  for await (const msg of client.take(queue, { 
    batch: 10,
    autoAck: true  // Auto-acknowledge on delivery
  })) {
    console.log(`   Event ${++count}: CPU=${msg.data.cpu.toFixed(1)}%, Memory=${msg.data.memory.toFixed(1)}%`);
    // No ack() call needed - automatically acknowledged!
    
    if (count >= 100) break;
  }
  
  console.log(`\n✅ Consumed ${count} events (all auto-acked)\n`);
}

// ============================================
// Example 2: Multiple Consumer Groups
// ============================================

async function demoConsumerGroups() {
  console.log('👥 Example 2: Consumer Groups (Fan-Out Pattern)');
  console.log('================================================\n');
  
  const queue = 'demo-user-events-' + Date.now();
  
  await client.queue(queue);
  
  // Publisher
  const publishEvents = async () => {
    console.log('📤 Publishing user events...');
    
    const events = [
      { userId: 123, action: 'login', timestamp: new Date() },
      { userId: 123, action: 'view_product', productId: 456 },
      { userId: 123, action: 'add_to_cart', productId: 456 },
      { userId: 123, action: 'checkout', total: 99.99 },
      { userId: 123, action: 'logout', timestamp: new Date() }
    ];
    
    for (const event of events) {
      await client.push(queue, event, {
        bufferMs: 50,
        bufferMax: 10
      });
    }
    
    console.log(`   Published ${events.length} events (buffered)\n`);
    await new Promise(resolve => setTimeout(resolve, 100));
  };
  
  // Consumer Group 1: Dashboard (auto-ack)
  const dashboardConsumer = async () => {
    console.log('📺 [Dashboard] Consuming with auto-ack...');
    let count = 0;
    
    for await (const event of client.take(`${queue}@dashboard`, { 
      batch: 10,
      autoAck: true 
    })) {
      console.log(`   [Dashboard] ${event.data.action} (userId: ${event.data.userId})`);
      if (++count >= 5) break;
    }
    
    console.log(`   [Dashboard] Done (${count} events, auto-acked)\n`);
  };
  
  // Consumer Group 2: Analytics (auto-ack)
  const analyticsConsumer = async () => {
    console.log('📈 [Analytics] Consuming with auto-ack...');
    let count = 0;
    
    for await (const event of client.take(`${queue}@analytics`, { 
      batch: 10,
      autoAck: true 
    })) {
      console.log(`   [Analytics] Tracking: ${event.data.action}`);
      if (++count >= 5) break;
    }
    
    console.log(`   [Analytics] Done (${count} events, auto-acked)\n`);
  };
  
  // Consumer Group 3: Billing (manual ack for reliability)
  const billingConsumer = async () => {
    console.log('💰 [Billing] Consuming with manual ack (critical)...');
    let count = 0;
    
    for await (const event of client.take(`${queue}@billing`, { batch: 10 })) {
      console.log(`   [Billing] Processing: ${event.data.action}`);
      
      // Critical processing - manual ack required
      if (event.data.action === 'checkout') {
        console.log(`   [Billing] Generating invoice for $${event.data.total}`);
      }
      
      await client.ack(event, true, { group: 'billing' });
      
      if (++count >= 5) break;
    }
    
    console.log(`   [Billing] Done (${count} events, manually acked)\n`);
  };
  
  // Run all in parallel
  await publishEvents();
  await Promise.all([
    dashboardConsumer(),
    analyticsConsumer(),
    billingConsumer()
  ]);
  
  console.log('✅ All consumer groups processed same events independently\n');
}

// ============================================
// Example 3: PostgreSQL Failover Demo
// ============================================

async function demoPostgreSQLFailover() {
  console.log('🔄 Example 3: PostgreSQL Failover');
  console.log('==================================\n');
  
  const queue = 'demo-failover-' + Date.now();
  
  await client.queue(queue);
  
  console.log('📤 Pushing critical tasks (normal push)...');
  console.log('   If PostgreSQL is down, these go to file buffer automatically\n');
  
  for (let i = 0; i < 10; i++) {
    const result = await client.push(queue, {
      taskId: i,
      type: 'process-payment',
      amount: (Math.random() * 1000).toFixed(2)
    });
    
    // Check if DB is healthy from response
    if (result.dbHealthy === false) {
      console.log(`   ⚠️  Event ${i}: Buffered to file (PostgreSQL down)`);
    } else {
      console.log(`   ✅ Event ${i}: Written to PostgreSQL directly`);
    }
  }
  
  console.log('\n💡 Benefits:');
  console.log('   - Zero message loss even if PostgreSQL is down');
  console.log('   - Events buffered to /var/lib/queen/buffers/failover.buf');
  console.log('   - Automatic replay when PostgreSQL recovers');
  console.log('   - FIFO ordering preserved\n');
}

// ============================================
// Example 4: Performance Comparison
// ============================================

async function demoPerformance() {
  console.log('⚡ Example 4: Performance Comparison');
  console.log('====================================\n');
  
  const iterations = 100;
  
  // Test 1: Normal push (no buffering)
  const normalQueue = 'perf-normal-' + Date.now();
  await client.queue(normalQueue);
  
  console.log(`🐢 Normal push (${iterations} events):`);
  const normalStart = Date.now();
  
  for (let i = 0; i < iterations; i++) {
    await client.push(normalQueue, { index: i });
  }
  
  const normalTime = Date.now() - normalStart;
  const normalThroughput = (iterations / normalTime * 1000).toFixed(0);
  console.log(`   Time: ${normalTime}ms`);
  console.log(`   Throughput: ${normalThroughput} events/sec`);
  console.log(`   DB writes: ~${iterations}\n`);
  
  // Test 2: QoS 0 push (with buffering)
  const qos0Queue = 'perf-qos0-' + Date.now();
  await client.queue(qos0Queue);
  
  console.log(`🚀 QoS 0 push (${iterations} events with buffering):`);
  const qos0Start = Date.now();
  
  for (let i = 0; i < iterations; i++) {
    await client.push(qos0Queue, { index: i }, {
      bufferMs: 100,
      bufferMax: 100
    });
  }
  
  const qos0Time = Date.now() - qos0Start;
  const qos0Throughput = (iterations / qos0Time * 1000).toFixed(0);
  const improvement = (normalTime / qos0Time).toFixed(1);
  
  console.log(`   Time: ${qos0Time}ms`);
  console.log(`   Throughput: ${qos0Throughput} events/sec`);
  console.log(`   DB writes: ~${Math.ceil(iterations / 100)}`);
  console.log(`   ⚡ ${improvement}x faster!\n`);
  
  // Wait for flush
  await new Promise(resolve => setTimeout(resolve, 200));
}

// ============================================
// Run All Examples
// ============================================

async function main() {
  try {
    await demoQoS0Buffering();
    await demoConsumerGroups();
    await demoPostgreSQLFailover();
    await demoPerformance();
    
    console.log('🎉 All examples completed successfully!');
    console.log('\n💡 Key Takeaways:');
    console.log('   • Use { bufferMs, bufferMax } for high-frequency events');
    console.log('   • Use { autoAck: true } for fire-and-forget consumption');
    console.log('   • PostgreSQL failover works automatically (zero message loss)');
    console.log('   • Same API for everything - just add options!');
    
  } catch (error) {
    console.error('❌ Error:', error.message);
  }
  
  process.exit(0);
}

main();

