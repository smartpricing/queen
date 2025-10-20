/**
 * Test to validate windowBuffer functionality
 * Ensures windowBuffer works correctly for all access modes:
 * - Direct partition access
 * - Queue-level access  
 * - Namespace/task filtered access
 */

import { Queen } from '../client/index.js';
import { sleep } from './utils.js';

const client = new Queen({
  baseUrl: 'http://localhost:6632'
});

async function testWindowBuffer() {
  console.log('\n🧪 Testing windowBuffer functionality...\n');
  
  const timestamp = Date.now();
  const queueName = 'test-window-buffer-' + timestamp;
  const partition = 'test-partition';
  
  // Delete queue if it exists (cleanup from previous runs)
  try {
    await client.queueDelete(queueName);
    console.log(`0. Deleted existing queue "${queueName}" (cleanup)`);
  } catch (err) {
    // Queue doesn't exist, that's fine
  }
  
  // Configure queue with 10 second window buffer
  console.log(`1. Creating queue "${queueName}" with windowBuffer: 10 seconds`);
  await client.queue(queueName, {
    windowBuffer: 10
  });
  
  // Push first message
  console.log('2. Pushing first message at', new Date().toISOString());
  await client.push(`${queueName}/${partition}`, { message: 'Message 1' });
  
  // Wait 2 seconds and push second message
  await sleep(2000);
  console.log('3. Pushing second message at', new Date().toISOString());
  await client.push(`${queueName}/${partition}`, { message: 'Message 2' });
  
  // Try to pop immediately (should get NOTHING due to windowBuffer)
  console.log('\n4. Trying to pop immediately (should get NOTHING due to 10s windowBuffer)...');
  
  // Test CASE 1: Direct partition access
  console.log('\n   TEST CASE 1: Direct partition access');
  let case1Result = 0;
  for await (const msg of client.take(`${queueName}/${partition}`, { limit: 10, batch: 10 })) {
    case1Result++;
    await client.ack(msg, true);
  }
  const case1Status = case1Result === 0 ? '✅' : '❌';
  console.log(`   ${case1Status} DIRECT ACCESS: Got ${case1Result} messages (expected 0)`);
  
  // Test CASE 2: Queue-level access
  console.log('\n   TEST CASE 2: Queue-level access');
  let case2Result = 0;
  for await (const msg of client.take(queueName, { limit: 10, batch: 10 })) {
    case2Result++;
    await client.ack(msg, true);
  }
  const case2Status = case2Result === 0 ? '✅' : '❌';
  console.log(`   ${case2Status} QUEUE ACCESS: Got ${case2Result} messages (expected 0)`);
  
  // Test CASE 3: Namespace/task filtered access (should work correctly)
  console.log('\n   TEST CASE 3: Namespace/task filtered access');
  
  // Use unique namespace/task per test run to avoid collisions
  const testNamespace = `test-ns-${timestamp}`;
  const testTask = `test-task-${timestamp}`;
  const filteredQueue = `${queueName}-filtered`;
  
  // Delete queue if it exists
  try {
    await client.queueDelete(filteredQueue);
  } catch (err) {
    // Queue doesn't exist, that's fine
  }
  
  await client.queue(filteredQueue, {
    windowBuffer: 10
  }, testNamespace, testTask);
  
  await client.push(`${filteredQueue}/${partition}`, { message: 'Filtered message' });
  
  let case3Result = 0;
  for await (const msg of client.take(`namespace:${testNamespace}/task:${testTask}`, { limit: 10, batch: 10 })) {
    case3Result++;
    await client.ack(msg, true);
  }
  const case3Status = case3Result === 0 ? '✅' : '❌';
  console.log(`   ${case3Status} FILTERED ACCESS: Got ${case3Result} messages (expected 0)`);
  
  console.log('\n📊 SUMMARY:');
  const allPassed = case1Result === 0 && case2Result === 0 && case3Result === 0;
  
  if (allPassed) {
    console.log('   ✅ All tests PASSED! windowBuffer is working correctly in all access modes.');
  } else {
    console.log('   Issues found:');
    if (case1Result !== 0) console.log('   ❌ Direct partition access: windowBuffer NOT enforced');
    if (case2Result !== 0) console.log('   ❌ Queue-level access: windowBuffer NOT enforced');
    if (case3Result !== 0) console.log('   ❌ Namespace/task filtered access: windowBuffer NOT enforced');
  }
  
  console.log('\n' + (allPassed ? '✅ Test complete. All fixed!' : '❌ Test complete. Issues remain.') + '\n');
}

testWindowBuffer().catch(console.error);

