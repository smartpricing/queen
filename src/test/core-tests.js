/**
 * Core Feature Tests for Queen Message Queue
 * Tests: Queue configuration, push, take, acknowledgment, delayed processing, FIFO ordering
 */

import { startTest, passTest, failTest, sleep, dbPool, getMessageCount, log } from './utils.js';

/**
 * Test 1: Queue Creation Policy
 * Verifies that queues must be created via queue() method
 */
export async function testQueueCreationPolicy(client) {
  startTest('Queue Creation Policy');
  
  try {
    // Test 1: Push to non-existent queue should fail
    const nonExistentQueue = 'queue-that-does-not-exist-' + Date.now();
    try {
      await client.push(nonExistentQueue, { test: 'should fail' });
      throw new Error('Push to non-existent queue should have failed');
    } catch (error) {
      if (!error.message.includes('does not exist')) {
        throw error;
      }
      log('Push to non-existent queue correctly failed', 'success');
    }
    
    // Test 2: Create queue via queue() method
    const testQueue = 'test-creation-policy-' + Date.now();
    await client.queue(testQueue, {
      retryLimit: 3,
      priority: 5
    }, {
      namespace: 'test',
      task: 'creation-policy'
    });
    log('Queue created via queue() method', 'success');
    
    // Test 3: Push to configured queue should succeed
    await client.push(testQueue, { test: 'message 1' });
    log('Push to configured queue succeeded', 'success');
    
    // Test 4: Push to new partition (should create on-demand)
    await client.push(`${testQueue}/NewPartition`, { test: 'message 2' });
    log('Partition created on-demand', 'success');
    
    passTest('Queue creation policy working correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 2: Single Message Push
 */
export async function testSingleMessagePush(client) {
  startTest('Single Message Push');
  
  try {
    const queue = 'test-single-push';
    
    // Configure queue first
    await client.queue(queue, {});
    
    // Push single message
    await client.push(queue, { 
      message: 'Single test message', 
      timestamp: Date.now() 
    });
    
    // Verify message was stored
    const count = await getMessageCount(queue);
    if (count !== 1) {
      throw new Error(`Expected 1 message in database, got ${count}`);
    }
    
    // Take and verify the message
    let messageReceived = false;
    for await (const msg of client.take(queue, { limit: 1 })) {
      if (msg.data && msg.data.message === 'Single test message') {
        messageReceived = true;
        await client.ack(msg);
      }
    }
    
    if (!messageReceived) {
      throw new Error('Failed to receive pushed message');
    }
    
    passTest('Single message pushed successfully');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 3: Batch Message Push
 */
export async function testBatchMessagePush(client) {
  startTest('Batch Message Push');
  
  try {
    const queue = 'test-batch-push';
    
    // Configure queue first
    await client.queue(queue, {});
    
    const batchSize = 5;
    const messages = Array.from({ length: batchSize }, (_, i) => ({
      message: `Batch message ${i + 1}`,
      index: i + 1
    }));
    
    // Push batch
    await client.push(`${queue}/batch-partition`, messages);
    
    // Verify messages were stored
    const count = await getMessageCount(queue, 'batch-partition');
    if (count !== batchSize) {
      throw new Error(`Expected ${batchSize} messages in database, got ${count}`);
    }
    
    // Take all messages
    const received = [];
    for await (const msg of client.take(`${queue}/batch-partition`, { limit: batchSize })) {
      received.push(msg);
      await client.ack(msg);
    }
    
    if (received.length !== batchSize) {
      throw new Error(`Expected ${batchSize} messages, got ${received.length}`);
    }
    
    passTest(`Batch of ${batchSize} messages pushed successfully`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 4: Queue Configuration
 */
export async function testQueueConfiguration(client) {
  startTest('Queue Configuration');
  
  try {
    const queue = 'test-config-queue';
    
    await client.queue(queue, {
      leaseTime: 600,
      retryLimit: 5,
      priority: 8,
      delayedProcessing: 2,
      windowBuffer: 3
    });
    
    // Verify configuration was applied by checking database
    const result = await dbPool.query(`
      SELECT lease_time, retry_limit, priority, delayed_processing, window_buffer
      FROM queen.queues
      WHERE name = $1
    `, [queue]);
    
    if (result.rows.length === 0) {
      throw new Error('Configured queue not found in database');
    }
    
    const row = result.rows[0];
    if (row.lease_time !== 600 || row.retry_limit !== 5) {
      throw new Error('Configuration options not saved correctly');
    }
    
    passTest('Queue configuration applied successfully');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 5: Take and Acknowledgment
 */
export async function testTakeAndAcknowledgment(client) {
  startTest('Take and Acknowledgment');
  
  try {
    const queue = 'test-take-ack';
    
    // Configure queue first
    await client.queue(queue, {});
    
    // Push test messages
    await client.push(`${queue}/ack-partition`, [
      { message: 'Message to complete' },
      { message: 'Message to fail' }
    ]);
    
    await sleep(100);
    
    // Take messages
    const messages = [];
    for await (const msg of client.take(`${queue}/ack-partition`, { limit: 2, batch: 2 })) {
      messages.push(msg);
    }
    
    if (messages.length !== 2) {
      throw new Error('Expected 2 messages from take');
    }
    
    // Acknowledge first as completed
    await client.ack(messages[0], true);
    
    // Acknowledge second as failed
    await client.ack(messages[1], false, { 
      error: 'Test failure' 
    });
    
    passTest('Take and acknowledgment work correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 6: Delayed Processing
 */
export async function testDelayedProcessing(client) {
  startTest('Delayed Processing');
  
  try {
    const queue = 'test-delayed-queue';
    
    // Configure queue with delayed processing
    await client.queue(queue, {
      delayedProcessing: 2 // 2 seconds delay
    });
    
    const startTime = Date.now();
    
    // Push message
    await client.push(queue, { 
      message: 'Delayed message', 
      sentAt: startTime 
    });
    
    // Try to take immediately (should get nothing)
    let gotImmediate = false;
    for await (const msg of client.take(queue, { limit: 1 })) {
      gotImmediate = true;
    }
    
    if (gotImmediate) {
      throw new Error('Got message immediately when it should be delayed');
    }
    
    // Wait for delay period plus buffer
    await sleep(2500);
    
    // Try to take again (should get the message now)
    let gotDelayed = false;
    let processingDelay = 0;
    
    for await (const msg of client.take(queue, { limit: 1 })) {
      gotDelayed = true;
      processingDelay = Date.now() - startTime;
      await client.ack(msg);
    }
    
    if (!gotDelayed) {
      throw new Error('Did not get delayed message after delay period');
    }
    
    if (processingDelay < 2000) {
      throw new Error(`Message processed too early: ${processingDelay}ms`);
    }
    
    passTest(`Delayed processing works correctly (${processingDelay}ms delay)`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 7: FIFO Ordering Within Partitions
 */
export async function testPartitionFIFOOrdering(client) {
  startTest('FIFO Ordering Within Partitions');
  
  try {
    const queue = 'test-partition-fifo';
    
    // Configure queue
    await client.queue(queue, {
      priority: 0
    });
    
    const partitions = [
      { name: 'ultra-high', priority: 100 },
      { name: 'high', priority: 10 },
      { name: 'medium', priority: 5 },
      { name: 'low', priority: 1 }
    ];
    
    // Create partitions manually in DB
    for (const partition of partitions) {
      await dbPool.query(`
        INSERT INTO queen.partitions (queue_id, name)
        SELECT q.id, $1
        FROM queen.queues q
        WHERE q.name = $2
        ON CONFLICT (queue_id, name) DO NOTHING
      `, [partition.name, queue]);
    }
    
    // Push messages to partitions
    for (const p of partitions) {
      await client.push(`${queue}/${p.name}`, {
        message: `${p.name} priority message`,
        priority: p.priority
      });
    }
    
    await sleep(100);
    
    // Take messages from different partitions
    const allMessages = [];
    for (let i = 0; i < partitions.length; i++) {
      for await (const msg of client.take(queue, { limit: 1, batch: 1 })) {
        allMessages.push(msg);
        await client.ack(msg);
      }
    }
    
    if (allMessages.length !== partitions.length) {
      throw new Error(`Expected ${partitions.length} messages, got ${allMessages.length}`);
    }
    
    passTest('FIFO ordering within partitions works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 8: Window Buffer
 * Verifies that windowBuffer delays message consumption across all access modes
 */
export async function testWindowBuffer(client) {
  startTest('Window Buffer');
  
  try {
    const timestamp = Date.now();
    const queueName = 'test-window-buffer-' + timestamp;
    const partition = 'test-partition';
    
    // Test CASE 1: Direct partition access
    await client.queue(queueName, { windowBuffer: 5 });
    
    // Push a message
    await client.push(`${queueName}/${partition}`, { test: 'case1' });
    
    // Try to take immediately - should get nothing
    let gotImmediate = false;
    for await (const msg of client.take(`${queueName}/${partition}`, { limit: 1 })) {
      gotImmediate = true;
      await client.ack(msg);
    }
    
    if (gotImmediate) {
      throw new Error('Got message immediately in direct access - windowBuffer not working');
    }
    log('Direct partition access: windowBuffer correctly delayed message');
    
    // Wait for window to pass
    await sleep(6000);
    
    // Now should get the message
    let gotDelayed = false;
    for await (const msg of client.take(`${queueName}/${partition}`, { limit: 1 })) {
      gotDelayed = true;
      await client.ack(msg);
    }
    
    if (!gotDelayed) {
      throw new Error('Did not get message after window expired');
    }
    log('Message available after window buffer expired');
    
    // Test CASE 2: Queue-level access
    const queue2 = 'test-window-buffer-q2-' + timestamp;
    await client.queue(queue2, { windowBuffer: 5 });
    await client.push(`${queue2}/p1`, { test: 'case2' });
    
    let gotCase2Immediate = false;
    for await (const msg of client.take(queue2, { limit: 1 })) {
      gotCase2Immediate = true;
      await client.ack(msg);
    }
    
    if (gotCase2Immediate) {
      throw new Error('Got message immediately in queue access - windowBuffer not working');
    }
    log('Queue-level access: windowBuffer correctly delayed message');
    
    await sleep(6000);
    
    let gotCase2Delayed = false;
    for await (const msg of client.take(queue2, { limit: 1 })) {
      gotCase2Delayed = true;
      await client.ack(msg);
    }
    
    if (!gotCase2Delayed) {
      throw new Error('Did not get message after window expired (queue access)');
    }
    
    // Test CASE 3: Namespace/task filtered access
    const ns = `test-ns-${timestamp}`;
    const task = `test-task-${timestamp}`;
    const queue3 = 'test-window-buffer-filtered-' + timestamp;
    
    await client.queue(queue3, { windowBuffer: 5 }, ns, task);
    await client.push(`${queue3}/p1`, { test: 'case3' });
    
    let gotCase3Immediate = false;
    for await (const msg of client.take(`namespace:${ns}/task:${task}`, { limit: 1 })) {
      gotCase3Immediate = true;
      await client.ack(msg);
    }
    
    if (gotCase3Immediate) {
      throw new Error('Got message immediately in filtered access - windowBuffer not working');
    }
    log('Namespace/task filtered access: windowBuffer correctly delayed message');
    
    await sleep(6000);
    
    let gotCase3Delayed = false;
    for await (const msg of client.take(`namespace:${ns}/task:${task}`, { limit: 1 })) {
      gotCase3Delayed = true;
      await client.ack(msg);
    }
    
    if (!gotCase3Delayed) {
      throw new Error('Did not get message after window expired (filtered access)');
    }
    
    passTest('Window buffer works correctly across all access modes');
  } catch (error) {
    failTest(error);
  }
}

