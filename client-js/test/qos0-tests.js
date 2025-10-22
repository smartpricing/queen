import { startTest, passTest, failTest } from './utils.js';

/**
 * Test server-side buffering
 * Uses file buffer for fast writes, server flushes every 100ms (configurable via FILE_BUFFER_FLUSH_MS)
 */
export async function testQoS0Buffering(client) {
  startTest('Server-side buffering', 'qos0');
  
  try {
    const queue = 'test-qos0-buffer-' + Date.now();
    await client.queue(queue);
    
    // Push 10 events with server-side buffering
    const start = Date.now();
    for (let i = 0; i < 10; i++) {
      await client.push(queue, { index: i }, { buffer: true });
    }
    const pushTime = Date.now() - start;
    
    // Should return quickly (buffered to file, server-side batching)
    // With file buffering, push should be fast (< 500ms for 10 messages)
    if (pushTime >= 500) {
      throw new Error(`Push should be fast (file buffered), took ${pushTime}ms`);
    }
    
    // Wait for server to flush buffer (FILE_BUFFER_FLUSH_MS default is 100ms)
    // Add extra margin for processing
    await new Promise(resolve => setTimeout(resolve, 300));
    
    // Consume all events
    const messages = [];
    for await (const msg of client.take(queue, { batch: 20 })) {
      messages.push(msg);
      await client.ack(msg);
      if (messages.length >= 10) break;
    }
    
    if (messages.length !== 10) {
      throw new Error(`Should receive all 10 events, got ${messages.length}`);
    }
    
    // Verify all events received
    const indices = messages.map(m => m.data.index).sort((a, b) => a - b);
    for (let i = 0; i < 10; i++) {
      if (!indices.includes(i)) {
        throw new Error(`Should have event with index ${i}`);
      }
    }
    
    passTest('Server-side buffering works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test auto-acknowledgment
 */
export async function testAutoAck(client) {
  startTest('Auto-acknowledgment', 'qos0');
  
  try {
    const queue = 'test-autoack-' + Date.now();
    await client.queue(queue);
    
    // Push message
    await client.push(queue, { test: 'autoack-data' });
    
    // Take with auto-ack
    let msg;
    for await (const m of client.take(queue, { autoAck: true })) {
      msg = m;
      break;
    }
    
    if (msg === undefined) {
      throw new Error('Should receive message');
    }
    if (msg.data.test !== 'autoack-data') {
      throw new Error('Should have correct data');
    }
    
    // Message should NOT be available again (already auto-acked)
    await new Promise(resolve => setTimeout(resolve, 100));
    
    let count = 0;
    for await (const m of client.take(queue, { wait: false, batch: 10 })) {
      count++;
      await client.ack(m);
      break;
    }
    
    if (count !== 0) {
      throw new Error('Message should not be available again (auto-acked)');
    }
    
    passTest('Auto-ack works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test consumer group with auto-ack
 */
export async function testConsumerGroupAutoAck(client) {
  startTest('Consumer group with auto-ack', 'qos0');
  
  try {
    const queue = 'test-cg-autoack-' + Date.now();
    await client.queue(queue);
    
    // Push message
    await client.push(queue, { value: 42, type: 'test' });
    
    // Wait a bit for message to be available
    await new Promise(resolve => setTimeout(resolve, 100));
    
    // Two consumer groups consume same message
    let msgA, msgB;
    
    for await (const msg of client.take(`${queue}@groupA`, { autoAck: true })) {
      msgA = msg;
      break;
    }
    
    for await (const msg of client.take(`${queue}@groupB`, { autoAck: true })) {
      msgB = msg;
      break;
    }
    
    if (msgA === undefined) {
      throw new Error('Group A should receive message');
    }
    if (msgB === undefined) {
      throw new Error('Group B should receive message');
    }
    if (msgA.data.value !== 42) {
      throw new Error('Group A should have correct data');
    }
    if (msgB.data.value !== 42) {
      throw new Error('Group B should have correct data');
    }
    
    // Both groups should have auto-acked - message not available again
    await new Promise(resolve => setTimeout(resolve, 100));
    
    let countA = 0, countB = 0;
    
    for await (const msg of client.take(`${queue}@groupA`, { wait: false })) {
      countA++;
      await client.ack(msg, true, { group: 'groupA' });
      break;
    }
    
    for await (const msg of client.take(`${queue}@groupB`, { wait: false })) {
      countB++;
      await client.ack(msg, true, { group: 'groupB' });
      break;
    }
    
    if (countA !== 0) {
      throw new Error('Group A should have auto-acked');
    }
    if (countB !== 0) {
      throw new Error('Group B should have auto-acked');
    }
    
    passTest('Consumer group auto-ack works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test normal push preserves FIFO ordering
 */
export async function testFIFOOrdering(client) {
  startTest('Normal push preserves FIFO', 'qos0');
  
  try {
    const queue = 'test-fifo-' + Date.now();
    await client.queue(queue);
    
    // Push without buffering (normal mode)
    for (let i = 0; i < 20; i++) {
      await client.push(queue, { order: i });
    }
    
    // Consume and verify order
    const messages = [];
    for await (const msg of client.take(queue, { batch: 20 })) {
      messages.push(msg);
      await client.ack(msg);
      if (messages.length >= 20) break;
    }
    
    if (messages.length !== 20) {
      throw new Error(`Should have 20 messages, got ${messages.length}`);
    }
    
    // Verify FIFO ordering
    for (let i = 0; i < 20; i++) {
      if (messages[i].data.order !== i) {
        throw new Error(`Message ${i} out of order: got ${messages[i].data.order}`);
      }
    }
    
    passTest('FIFO ordering preserved for normal push');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test mixed buffered and non-buffered operations
 */
export async function testMixedOperations(client) {
  startTest('Mixed buffered and normal operations', 'qos0');
  
  try {
    const queue = 'test-mixed-' + Date.now();
    await client.queue(queue);
    
    // Push with buffering (file buffered)
    await client.push(queue, { type: 'buffered', id: 1 }, { buffer: true });
    
    // Push normal (direct DB)
    await client.push(queue, { type: 'normal', id: 2 });
    
    // Wait for buffer flush (100ms + margin)
    await new Promise(resolve => setTimeout(resolve, 200));
    
    // Should have both messages
    const messages = [];
    for await (const msg of client.take(queue, { batch: 10 })) {
      messages.push(msg);
      await client.ack(msg);
      if (messages.length >= 2) break;
    }
    
    if (messages.length !== 2) {
      throw new Error(`Should have 2 messages, got ${messages.length}`);
    }
    
    const types = messages.map(m => m.data.type);
    if (!types.includes('buffered')) {
      throw new Error('Should have buffered message');
    }
    if (!types.includes('normal')) {
      throw new Error('Should have normal message');
    }
    
    passTest('Mixed buffered and normal operations work correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test buffered events are stored individually
 * Note: Buffering is a server-side optimization for DB writes,
 * not for combining events into a single message
 */
export async function testBatchedPayload(client) {
  startTest('Buffered events stored individually', 'qos0');
  
  try {
    const queue = 'test-batch-payload-' + Date.now();
    await client.queue(queue);
    
    // Push multiple events with buffering
    for (let i = 0; i < 5; i++) {
      await client.push(queue, { index: i, timestamp: Date.now() }, { buffer: true });
    }
    
    // Wait for server flush (100ms + margin)
    await new Promise(resolve => setTimeout(resolve, 200));
    
    // Consume all messages - each event should be a separate message
    const messages = [];
    for await (const msg of client.take(queue, { batch: 10 })) {
      messages.push(msg);
      await client.ack(msg);
      if (messages.length >= 5) break;
    }
    
    if (messages.length !== 5) {
      throw new Error(`Should receive 5 separate messages, got ${messages.length}`);
    }
    
    // Verify each message has individual payload (not batched into array)
    for (const msg of messages) {
      if (typeof msg.data !== 'object' || Array.isArray(msg.data)) {
        throw new Error(`Each message should have object payload, got ${typeof msg.data}`);
      }
      if (typeof msg.data.index !== 'number') {
        throw new Error('Each message should have index property');
      }
    }
    
    // Verify all indices are present
    const indices = messages.map(m => m.data.index).sort((a, b) => a - b);
    for (let i = 0; i < 5; i++) {
      if (!indices.includes(i)) {
        throw new Error(`Should have message with index ${i}`);
      }
    }
    
    passTest('Buffered events stored and retrieved individually');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Run all QoS 0 tests
 */
export async function runQoS0Tests(client) {
  console.log('\n╔════════════════════════════════════════╗');
  console.log('║     Queen QoS 0 Test Suite            ║');
  console.log('╚════════════════════════════════════════╝\n');
  
  await testQoS0Buffering(client);
  await testAutoAck(client);
  await testConsumerGroupAutoAck(client);
  await testFIFOOrdering(client);
  await testMixedOperations(client);
  await testBatchedPayload(client);
  
  console.log('\n✅ All QoS 0 tests passed!\n');
}

