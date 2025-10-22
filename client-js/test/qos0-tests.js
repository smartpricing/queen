import { startTest, passTest, failTest, assert } from './utils.js';

/**
 * Test QoS 0 server-side buffering
 */
export async function testQoS0Buffering(client) {
  startTest('QoS 0 server-side buffering', 'qos0');
  
  const queue = 'test-qos0-buffer-' + Date.now();
  await client.queue(queue);
  
  // Push 10 events with buffering
  const start = Date.now();
  for (let i = 0; i < 10; i++) {
    await client.push(queue, { index: i }, {
      bufferMs: 100,
      bufferMax: 100
    });
  }
  const pushTime = Date.now() - start;
  
  // Should return quickly (buffered, not written to DB yet)
  assert(pushTime < 200, `Push should be fast (buffered), took ${pushTime}ms`);
  
  // Wait for server to flush buffer
  await new Promise(resolve => setTimeout(resolve, 200));
  
  // Consume all events
  const messages = [];
  for await (const msg of client.take(queue, { batch: 20 })) {
    messages.push(msg);
    await client.ack(msg);
    if (messages.length >= 10) break;
  }
  
  assert(messages.length === 10, `Should receive all 10 events, got ${messages.length}`);
  
  // Verify all events received
  const indices = messages.map(m => m.data.index).sort((a, b) => a - b);
  for (let i = 0; i < 10; i++) {
    assert(indices.includes(i), `Should have event with index ${i}`);
  }
  
  passTest('QoS 0 buffering works correctly');
}

/**
 * Test auto-acknowledgment
 */
export async function testAutoAck(client) {
  startTest('Auto-acknowledgment', 'qos0');
  
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
  
  assert(msg !== undefined, 'Should receive message');
  assert(msg.data.test === 'autoack-data', 'Should have correct data');
  
  // Message should NOT be available again (already auto-acked)
  await new Promise(resolve => setTimeout(resolve, 100));
  
  let count = 0;
  for await (const m of client.take(queue, { wait: false, batch: 10 })) {
    count++;
    await client.ack(m);
    break;
  }
  
  assert(count === 0, 'Message should not be available again (auto-acked)');
  
  passTest('Auto-ack works correctly');
}

/**
 * Test consumer group with auto-ack
 */
export async function testConsumerGroupAutoAck(client) {
  startTest('Consumer group with auto-ack', 'qos0');
  
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
  
  assert(msgA !== undefined, 'Group A should receive message');
  assert(msgB !== undefined, 'Group B should receive message');
  assert(msgA.data.value === 42, 'Group A should have correct data');
  assert(msgB.data.value === 42, 'Group B should have correct data');
  
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
  
  assert(countA === 0, 'Group A should have auto-acked');
  assert(countB === 0, 'Group B should have auto-acked');
  
  passTest('Consumer group auto-ack works correctly');
}

/**
 * Test normal push preserves FIFO ordering
 */
export async function testFIFOOrdering(client) {
  startTest('Normal push preserves FIFO', 'qos0');
  
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
  
  assert(messages.length === 20, `Should have 20 messages, got ${messages.length}`);
  
  // Verify FIFO ordering
  for (let i = 0; i < 20; i++) {
    assert(messages[i].data.order === i, `Message ${i} out of order: got ${messages[i].data.order}`);
  }
  
  passTest('FIFO ordering preserved for normal push');
}

/**
 * Test mixed buffered and non-buffered operations
 */
export async function testMixedOperations(client) {
  startTest('Mixed buffered/non-buffered operations', 'qos0');
  
  const queue = 'test-mixed-' + Date.now();
  await client.queue(queue);
  
  // Push with buffering
  await client.push(queue, { type: 'buffered', id: 1 }, {
    bufferMs: 100,
    bufferMax: 10
  });
  
  // Push without buffering
  await client.push(queue, { type: 'immediate', id: 2 });
  
  // Wait for buffer flush
  await new Promise(resolve => setTimeout(resolve, 150));
  
  // Should have both messages
  const messages = [];
  for await (const msg of client.take(queue, { batch: 10 })) {
    messages.push(msg);
    await client.ack(msg);
    if (messages.length >= 2) break;
  }
  
  assert(messages.length === 2, `Should have 2 messages, got ${messages.length}`);
  
  const types = messages.map(m => m.data.type);
  assert(types.includes('buffered'), 'Should have buffered message');
  assert(types.includes('immediate'), 'Should have immediate message');
  
  passTest('Mixed operations work correctly');
}

/**
 * Test batched payload format
 */
export async function testBatchedPayload(client) {
  startTest('Batched event payload format', 'qos0');
  
  const queue = 'test-batch-payload-' + Date.now();
  await client.queue(queue);
  
  // Push multiple events with buffering
  for (let i = 0; i < 5; i++) {
    await client.push(queue, { index: i, timestamp: Date.now() }, {
      bufferMs: 50,
      bufferMax: 10
    });
  }
  
  // Wait for flush
  await new Promise(resolve => setTimeout(resolve, 100));
  
  // Take message (should be batched)
  let msg;
  for await (const m of client.take(queue, { batch: 10 })) {
    msg = m;
    break;
  }
  
  assert(msg !== undefined, 'Should receive message');
  
  // Payload should be an array (batched events)
  assert(Array.isArray(msg.data), `Payload should be array, got ${typeof msg.data}`);
  assert(msg.data.length === 5, `Should have 5 batched events, got ${msg.data.length}`);
  
  // Verify all events are there
  for (let i = 0; i < 5; i++) {
    const hasIndex = msg.data.some(event => event.index === i);
    assert(hasIndex, `Should have event with index ${i}`);
  }
  
  await client.ack(msg);
  
  passTest('Batched payload format correct');
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

