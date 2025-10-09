#!/usr/bin/env node

/**
 * Comprehensive Test Suite for Queen Message Queue System
 * 
 * Tests all features of the system:
 * - Push methods (single/batch)
 * - Queue configuration
 * - Pop and consume operations
 * - Default and custom partitions
 * - Delayed send and buffer
 * - Partition priority ordering
 * - Queue priority ordering
 * - Error handling and edge cases
 */

import { createQueenClient } from '../client/queenClient.js';
import pg from 'pg';

// Test configuration
const TEST_CONFIG = {
  baseUrl: 'http://localhost:6632',
  dbConfig: {
    host: process.env.PG_HOST || 'localhost',
    port: process.env.PG_PORT || 5432,
    database: process.env.PG_DB || 'postgres',
    user: process.env.PG_USER || 'postgres',
    password: process.env.PG_PASSWORD || 'postgres'
  }
};

// Global test state
let client;
let dbPool;
let testResults = [];
let currentTest = '';

// Test utilities
const log = (message, type = 'info') => {
  const timestamp = new Date().toISOString().substring(11, 23);
  const prefix = {
    info: '📝',
    success: '✅',
    error: '❌',
    warning: '⚠️',
    test: '🧪'
  }[type] || '📝';
  
  console.log(`[${timestamp}] ${prefix} ${message}`);
};

const startTest = (testName) => {
  currentTest = testName;
  log(`Starting: ${testName}`, 'test');
};

const passTest = (message = '') => {
  const result = { test: currentTest, status: 'PASS', message };
  testResults.push(result);
  log(`PASS: ${currentTest} ${message}`, 'success');
};

const failTest = (error) => {
  const result = { test: currentTest, status: 'FAIL', error: error.message };
  testResults.push(result);
  log(`FAIL: ${currentTest} - ${error.message}`, 'error');
};

const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms));

// Database utilities
const cleanupTestData = async () => {
  try {
    await dbPool.query(`
      DELETE FROM queen.messages 
      WHERE partition_id IN (
        SELECT p.id FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name LIKE 'test-%'
      )
    `);
    
    await dbPool.query(`
      DELETE FROM queen.partitions 
      WHERE queue_id IN (
        SELECT id FROM queen.queues WHERE name LIKE 'test-%'
      )
    `);
    
    await dbPool.query(`DELETE FROM queen.queues WHERE name LIKE 'test-%'`);
    
    log('Test data cleaned up');
  } catch (error) {
    log(`Cleanup error: ${error.message}`, 'warning');
  }
};

const getMessageCount = async (queueName, partitionName = null) => {
  const query = partitionName 
    ? `SELECT COUNT(*) as count FROM queen.messages m
       JOIN queen.partitions p ON m.partition_id = p.id
       JOIN queen.queues q ON p.queue_id = q.id
       WHERE q.name = $1 AND p.name = $2`
    : `SELECT COUNT(*) as count FROM queen.messages m
       JOIN queen.partitions p ON m.partition_id = p.id
       JOIN queen.queues q ON p.queue_id = q.id
       WHERE q.name = $1`;
       
  const params = partitionName ? [queueName, partitionName] : [queueName];
  const result = await dbPool.query(query, params);
  return parseInt(result.rows[0].count);
};

// Test Suite Implementation

/**
 * Test 1: Single Message Push
 */
async function testSingleMessagePush() {
  startTest('Single Message Push');
  
  try {
    const result = await client.push({
      items: [{
        queue: 'test-single-push',
        partition: 'default',
        payload: { message: 'Single test message', timestamp: Date.now() }
      }]
    });
    
    if (!result.messages || result.messages.length !== 1) {
      throw new Error('Expected 1 message in response');
    }
    
    const message = result.messages[0];
    if (!message.id || !message.transactionId || message.status !== 'queued') {
      throw new Error('Invalid message response format');
    }
    
    // Verify message was stored
    const count = await getMessageCount('test-single-push');
    if (count !== 1) {
      throw new Error(`Expected 1 message in database, got ${count}`);
    }
    
    passTest('Single message pushed successfully');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 2: Batch Message Push
 */
async function testBatchMessagePush() {
  startTest('Batch Message Push');
  
  try {
    const batchSize = 5;
    const items = Array.from({ length: batchSize }, (_, i) => ({
      queue: 'test-batch-push',
      partition: 'batch-partition',
      payload: { message: `Batch message ${i + 1}`, index: i + 1 }
    }));
    
    const result = await client.push({ items });
    
    if (!result.messages || result.messages.length !== batchSize) {
      throw new Error(`Expected ${batchSize} messages in response, got ${result.messages?.length}`);
    }
    
    // Verify all messages have unique transaction IDs
    const transactionIds = result.messages.map(m => m.transactionId);
    const uniqueIds = new Set(transactionIds);
    if (uniqueIds.size !== batchSize) {
      throw new Error('Transaction IDs are not unique');
    }
    
    // Verify messages were stored
    const count = await getMessageCount('test-batch-push', 'batch-partition');
    if (count !== batchSize) {
      throw new Error(`Expected ${batchSize} messages in database, got ${count}`);
    }
    
    passTest(`Batch of ${batchSize} messages pushed successfully`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 3: Default Partition Handling
 */
async function testDefaultPartition() {
  startTest('Default Partition Handling');
  
  try {
    // Push without specifying partition (should use Default)
    await client.push({
      items: [{
        queue: 'test-default-partition',
        payload: { message: 'Default partition message' }
      }]
    });
    
    // Push explicitly to Default partition
    await client.push({
      items: [{
        queue: 'test-default-partition',
        partition: 'Default',
        payload: { message: 'Explicit default partition message' }
      }]
    });
    
    // Verify both messages are in Default partition
    const count = await getMessageCount('test-default-partition', 'Default');
    if (count !== 2) {
      throw new Error(`Expected 2 messages in Default partition, got ${count}`);
    }
    
    passTest('Default partition handling works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 4: Queue Configuration
 */
async function testQueueConfiguration() {
  startTest('Queue Configuration');
  
  try {
    const configResult = await client.configure({
      queue: 'test-config-queue',
      options: {
        leaseTime: 600,
        retryLimit: 5,
        priority: 8,
        delayedProcessing: 2,
        windowBuffer: 3
      }
    });
    
    if (!configResult.configured) {
      throw new Error('Configuration failed');
    }
    
    if (configResult.queue !== 'test-config-queue') {
      throw new Error('Configuration response has wrong queue name');
    }
    
    // Verify configuration was applied by checking database
    const result = await dbPool.query(`
      SELECT p.options, p.priority
      FROM queen.partitions p
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.name = 'test-config-queue' AND p.name = 'configured-partition'
    `);
    
    if (result.rows.length === 0) {
      throw new Error('Configured partition not found in database');
    }
    
    const options = result.rows[0].options;
    if (options.leaseTime !== 600 || options.retryLimit !== 5) {
      throw new Error('Configuration options not saved correctly');
    }
    
    passTest('Queue configuration applied successfully');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 5: Pop from Specific Partition
 */
async function testPopSpecificPartition() {
  startTest('Pop from Specific Partition');
  
  try {
    // Push messages to different partitions
    await client.push({
      items: [
        {
          queue: 'test-pop-specific',
          partition: 'partition-a',
          payload: { message: 'Message in partition A' }
        },
        {
          queue: 'test-pop-specific',
          partition: 'partition-b',
          payload: { message: 'Message in partition B' }
        }
      ]
    });
    
    await sleep(100); // Ensure messages are stored
    
    // Pop from specific partition A
    const resultA = await client.pop({
      queue: 'test-pop-specific',
      partition: 'partition-a',
      batch: 1
    });
    
    if (!resultA.messages || resultA.messages.length !== 1) {
      throw new Error('Expected 1 message from partition A');
    }
    
    if (resultA.messages[0].partition !== 'partition-a') {
      throw new Error('Got message from wrong partition');
    }
    
    if (resultA.messages[0].data.message !== 'Message in partition A') {
      throw new Error('Got wrong message content');
    }
    
    // Acknowledge the message
    await client.ack(resultA.messages[0].transactionId, 'completed');
    
    passTest('Pop from specific partition works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 6: Pop from Queue (Any Partition)
 */
async function testPopFromQueue() {
  startTest('Pop from Queue (Any Partition)');
  
  try {
    // Push messages to different partitions with different priorities
    await dbPool.query(`
      INSERT INTO queen.queues (name, priority) 
      VALUES ('test-pop-queue', 0)
      ON CONFLICT (name) DO UPDATE SET priority = EXCLUDED.priority
    `);
    
    await dbPool.query(`
      INSERT INTO queen.partitions (queue_id, name, priority)
      SELECT q.id, 'high-priority', 10
      FROM queen.queues q
      WHERE q.name = 'test-pop-queue'
      ON CONFLICT (queue_id, name) DO UPDATE SET priority = EXCLUDED.priority
    `);
    
    await dbPool.query(`
      INSERT INTO queen.partitions (queue_id, name, priority)
      SELECT q.id, 'low-priority', 1
      FROM queen.queues q
      WHERE q.name = 'test-pop-queue'
      ON CONFLICT (queue_id, name) DO UPDATE SET priority = EXCLUDED.priority
    `);
    
    await client.push({
      items: [
        {
          queue: 'test-pop-queue',
          partition: 'low-priority',
          payload: { message: 'Low priority message', priority: 1 }
        },
        {
          queue: 'test-pop-queue',
          partition: 'high-priority',
          payload: { message: 'High priority message', priority: 10 }
        }
      ]
    });
    
    await sleep(100);
    
    // Pop from queue (should get high priority partition first)
    const result = await client.pop({
      queue: 'test-pop-queue',
      batch: 2
    });
    
    if (!result.messages || result.messages.length !== 2) {
      throw new Error('Expected 2 messages from queue');
    }
    
    // Verify priority ordering
    if (result.messages[0].partition !== 'high-priority' || 
        result.messages[1].partition !== 'low-priority') {
      throw new Error(`Wrong priority order: got ${result.messages[0].partition} then ${result.messages[1].partition}`);
    }
    
    // Acknowledge messages
    for (const message of result.messages) {
      await client.ack(message.transactionId, 'completed');
    }
    
    passTest('Pop from queue respects partition priority');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 7: Namespace Filtering
 */
async function testNamespaceFiltering() {
  startTest('Namespace Filtering');
  
  try {
    // Setup queues with namespace
    await dbPool.query(`
      INSERT INTO queen.queues (name, namespace, priority) 
      VALUES 
        ('test-ns-queue-1', 'test-namespace', 10),
        ('test-ns-queue-2', 'test-namespace', 5),
        ('test-other-queue', 'other-namespace', 8)
      ON CONFLICT (name) DO UPDATE SET 
        namespace = EXCLUDED.namespace,
        priority = EXCLUDED.priority
    `);
    
    // Create default partitions
    await dbPool.query(`
      INSERT INTO queen.partitions (queue_id, name, priority)
      SELECT q.id, 'Default', 0
      FROM queen.queues q
      WHERE q.name IN ('test-ns-queue-1', 'test-ns-queue-2', 'test-other-queue')
      ON CONFLICT (queue_id, name) DO NOTHING
    `);
    
    // Push messages to different namespaces
    await client.push({
      items: [
        {
          queue: 'test-ns-queue-1',
          partition: 'Default',
          payload: { message: 'Message in test-namespace queue 1' }
        },
        {
          queue: 'test-ns-queue-2',
          partition: 'Default',
          payload: { message: 'Message in test-namespace queue 2' }
        },
        {
          queue: 'test-other-queue',
          partition: 'Default',
          payload: { message: 'Message in other-namespace' }
        }
      ]
    });
    
    await sleep(100);
    
    // Pop with namespace filter
    const result = await client.pop({
      namespace: 'test-namespace',
      batch: 2
    });
    
    if (!result.messages || result.messages.length !== 2) {
      throw new Error('Expected 2 messages from test-namespace');
    }
    
    // Verify messages are from correct namespace and in priority order
    if (result.messages[0].queue !== 'test-ns-queue-1' ||
        result.messages[1].queue !== 'test-ns-queue-2') {
      throw new Error('Wrong queue priority order or namespace filtering failed');
    }
    
    // Verify no message from other namespace
    for (const message of result.messages) {
      if (message.queue === 'test-other-queue') {
        throw new Error('Got message from wrong namespace');
      }
    }
    
    // Acknowledge messages
    for (const message of result.messages) {
      await client.ack(message.transactionId, 'completed');
    }
    
    passTest('Namespace filtering and queue priority work correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 8: Delayed Processing
 */
async function testDelayedProcessing() {
  startTest('Delayed Processing');
  
  try {
    // Configure queue with delayed processing
    await client.configure({
      queue: 'test-delayed-queue',
      options: {
        delayedProcessing: 2 // 2 seconds delay
      }
    });
    
    const startTime = Date.now();
    
    // Push message
    await client.push({
      items: [{
        queue: 'test-delayed-queue',
        payload: { message: 'Delayed message', sentAt: startTime }
      }]
    });
    
    // Try to pop immediately (should get nothing)
    const immediateResult = await client.pop({
      queue: 'test-delayed-queue',
      batch: 1
    });
    
    if (immediateResult.messages && immediateResult.messages.length > 0) {
      throw new Error('Got message immediately when it should be delayed');
    }
    
    // Wait for delay period plus buffer
    await sleep(2500);
    
    // Try to pop again (should get the message now)
    const delayedResult = await client.pop({
      queue: 'test-delayed-queue',
      batch: 1
    });
    
    if (!delayedResult.messages || delayedResult.messages.length !== 1) {
      throw new Error('Did not get delayed message after delay period');
    }
    
    const processingDelay = Date.now() - startTime;
    if (processingDelay < 2000) {
      throw new Error(`Message processed too early: ${processingDelay}ms`);
    }
    
    await client.ack(delayedResult.messages[0].transactionId, 'completed');
    
    passTest(`Delayed processing works correctly (${processingDelay}ms delay)`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 9: Window Buffer
 */
async function testWindowBuffer() {
  startTest('Window Buffer');
  
  try {
    // Configure queue with window buffer
    await client.configure({
      queue: 'test-buffer-queue',
      options: {
        windowBuffer: 2 // 2 seconds buffer
      }
    });
    
    // Push first message
    await client.push({
      items: [{
        queue: 'test-buffer-queue',
        payload: { message: 'First message', order: 1 }
      }]
    });
    
    await sleep(500); // Small delay
    
    // Push second message
    await client.push({
      items: [{
        queue: 'test-buffer-queue',
        payload: { message: 'Second message', order: 2 }
      }]
    });
    
    // Try to pop immediately (should get nothing due to buffer)
    const immediateResult = await client.pop({
      queue: 'test-buffer-queue',
      batch: 2
    });
    
    if (immediateResult.messages && immediateResult.messages.length > 0) {
      throw new Error('Got messages immediately when they should be buffered');
    }
    
    // Wait for buffer period
    await sleep(2500);
    
    // Try to pop again (should get messages now)
    const bufferedResult = await client.pop({
      queue: 'test-buffer-queue',
      batch: 2
    });
    
    if (!bufferedResult.messages || bufferedResult.messages.length !== 2) {
      throw new Error('Did not get buffered messages after buffer period');
    }
    
    // Verify FIFO order
    if (bufferedResult.messages[0].data.order !== 1 ||
        bufferedResult.messages[1].data.order !== 2) {
      throw new Error('Messages not in FIFO order after buffering');
    }
    
    // Acknowledge messages
    for (const message of bufferedResult.messages) {
      await client.ack(message.transactionId, 'completed');
    }
    
    passTest('Window buffer works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 10: Message Acknowledgment
 */
async function testMessageAcknowledgment() {
  startTest('Message Acknowledgment');
  
  try {
    // Push test messages
    const pushResult = await client.push({
      items: [
        {
          queue: 'test-ack-queue',
          partition: 'ack-partition',
          payload: { message: 'Message to complete' }
        },
        {
          queue: 'test-ack-queue',
          partition: 'ack-partition',
          payload: { message: 'Message to fail' }
        }
      ]
    });
    
    await sleep(100);
    
    // Pop messages
    const popResult = await client.pop({
      queue: 'test-ack-queue',
      partition: 'ack-partition',
      batch: 2
    });
    
    if (!popResult.messages || popResult.messages.length !== 2) {
      throw new Error('Expected 2 messages for acknowledgment test');
    }
    
    // Acknowledge first as completed
    const completeResult = await client.ack(
      popResult.messages[0].transactionId, 
      'completed'
    );
    
    if (!completeResult || !completeResult.acknowledgedAt) {
      throw new Error('Failed to acknowledge message as completed');
    }
    
    // Acknowledge second as failed (will be retry_scheduled due to retry logic)
    const failResult = await client.ack(
      popResult.messages[1].transactionId, 
      'failed', 
      'Test failure'
    );
    
    if (!failResult || !failResult.acknowledgedAt) {
      throw new Error('Failed to acknowledge message as failed');
    }
    
    // The failed message should be scheduled for retry, not marked as failed immediately
    const expectedFailStatus = failResult.status === 'retry_scheduled' ? 'pending' : 'failed';
    
    // Verify message statuses in database
    const statusCheck = await dbPool.query(`
      SELECT m.status, m.error_message, m.transaction_id
      FROM queen.messages m
      WHERE m.transaction_id IN ($1, $2)
      ORDER BY m.created_at
    `, [popResult.messages[0].transactionId, popResult.messages[1].transactionId]);
    
    if (statusCheck.rows.length !== 2) {
      throw new Error('Could not find acknowledged messages in database');
    }
    
    // Messages might be returned in different order, so check by transaction ID
    const firstMessage = statusCheck.rows.find(row => row.transaction_id === popResult.messages[0].transactionId);
    const secondMessage = statusCheck.rows.find(row => row.transaction_id === popResult.messages[1].transactionId);
    
    if (!firstMessage || !secondMessage) {
      throw new Error('Could not find messages by transaction ID');
    }
    
    if (firstMessage.status !== 'completed' || secondMessage.status !== expectedFailStatus) {
      throw new Error(`Message statuses not updated correctly: first=${firstMessage.status}, second=${secondMessage.status}, expected completed, ${expectedFailStatus}`);
    }
    
    // Error message might be cleared for retry_scheduled messages
    if (expectedFailStatus === 'failed' && statusCheck.rows[1].error_message !== 'Test failure') {
      throw new Error('Error message not saved correctly');
    }
    
    passTest('Message acknowledgment works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 11: Batch Acknowledgment
 */
async function testBatchAcknowledgment() {
  startTest('Batch Acknowledgment');
  
  try {
    // Push multiple messages
    await client.push({
      items: Array.from({ length: 3 }, (_, i) => ({
        queue: 'test-batch-ack-queue',
        partition: 'batch-ack-partition',
        payload: { message: `Batch ack message ${i + 1}`, index: i + 1 }
      }))
    });
    
    await sleep(100);
    
    // Pop messages
    const popResult = await client.pop({
      queue: 'test-batch-ack-queue',
      partition: 'batch-ack-partition',
      batch: 3
    });
    
    if (!popResult.messages || popResult.messages.length !== 3) {
      throw new Error('Expected 3 messages for batch acknowledgment test');
    }
    
    // Batch acknowledge
    const batchAckResult = await client.ackBatch([
      { transactionId: popResult.messages[0].transactionId, status: 'completed' },
      { transactionId: popResult.messages[1].transactionId, status: 'completed' },
      { transactionId: popResult.messages[2].transactionId, status: 'failed', error: 'Batch test failure' }
    ]);
    
    // Batch ack returns an object with results array
    if (!batchAckResult || !batchAckResult.results || !Array.isArray(batchAckResult.results) || batchAckResult.results.length !== 3) {
      throw new Error(`Batch acknowledgment did not return correct format: got ${JSON.stringify(batchAckResult)}`);
    }
    
    const results = batchAckResult.results;
    
    // Verify all acknowledgments succeeded
    for (let i = 0; i < 3; i++) {
      const expectedStatus = i < 2 ? 'completed' : 'failed';
      const actualResult = results[i];
      
      if (!actualResult || !actualResult.transactionId) {
        throw new Error(`Batch ack ${i + 1} missing transaction ID: ${JSON.stringify(actualResult)}`);
      }
      
      // For failed messages, the status might be retry_scheduled
      const acceptableStatuses = expectedStatus === 'failed' ? ['failed', 'retry_scheduled'] : [expectedStatus];
      
      if (!acceptableStatuses.includes(actualResult.status)) {
        throw new Error(`Batch ack ${i + 1} has wrong status: expected ${expectedStatus} (or retry_scheduled), got ${actualResult.status}`);
      }
    }
    
    passTest('Batch acknowledgment works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 12: Consumer Pattern
 */
async function testConsumerPattern() {
  startTest('Consumer Pattern');
  
  try {
    let consumedMessages = [];
    let consumerStopped = false;
    
    // Push messages for consumption
    await client.push({
      items: Array.from({ length: 3 }, (_, i) => ({
        queue: 'test-consumer-queue',
        partition: 'consumer-partition',
        payload: { message: `Consumer message ${i + 1}`, index: i + 1 }
      }))
    });
    
    // Start consumer
    const stopConsumer = client.consume({
      queue: 'test-consumer-queue',
      partition: 'consumer-partition',
      handler: async (message) => {
        consumedMessages.push(message);
        // Stop after consuming 3 messages
        if (consumedMessages.length >= 3) {
          stopConsumer();
          consumerStopped = true;
        }
      },
      stopOnError: true
    });
    
    // Wait for consumption
    let attempts = 0;
    while (!consumerStopped && attempts < 50) {
      await sleep(100);
      attempts++;
    }
    
    if (!consumerStopped) {
      stopConsumer();
      throw new Error('Consumer did not stop after processing messages');
    }
    
    if (consumedMessages.length !== 3) {
      throw new Error(`Expected 3 consumed messages, got ${consumedMessages.length}`);
    }
    
    // Verify messages were consumed in order
    for (let i = 0; i < 3; i++) {
      if (consumedMessages[i].data.index !== i + 1) {
        throw new Error(`Messages not consumed in FIFO order: expected ${i + 1}, got ${consumedMessages[i].data.index}`);
      }
    }
    
    passTest('Consumer pattern works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 13: Partition Priority Ordering
 */
async function testPartitionPriorityOrdering() {
  startTest('Partition Priority Ordering');
  
  try {
    // Setup queue with multiple partitions of different priorities
    await dbPool.query(`
      INSERT INTO queen.queues (name, priority) 
      VALUES ('test-partition-priority', 0)
      ON CONFLICT (name) DO UPDATE SET priority = EXCLUDED.priority
    `);
    
    const partitions = [
      { name: 'ultra-high', priority: 100 },
      { name: 'high', priority: 10 },
      { name: 'medium', priority: 5 },
      { name: 'low', priority: 1 }
    ];
    
    // Create partitions with priorities
    for (const partition of partitions) {
      await dbPool.query(`
        INSERT INTO queen.partitions (queue_id, name, priority)
        SELECT q.id, $1, $2
        FROM queen.queues q
        WHERE q.name = 'test-partition-priority'
        ON CONFLICT (queue_id, name) DO UPDATE SET priority = EXCLUDED.priority
      `, [partition.name, partition.priority]);
    }
    
    // Push messages to partitions in reverse priority order
    const messages = [];
    for (let i = partitions.length - 1; i >= 0; i--) {
      const partition = partitions[i];
      messages.push({
        queue: 'test-partition-priority',
        partition: partition.name,
        payload: { 
          message: `${partition.name} priority message`, 
          priority: partition.priority,
          order: partitions.length - i
        }
      });
    }
    
    await client.push({ items: messages });
    await sleep(100);
    
    // Pop all messages (should come in priority order)
    const result = await client.pop({
      queue: 'test-partition-priority',
      batch: partitions.length
    });
    
    if (!result.messages || result.messages.length !== partitions.length) {
      throw new Error(`Expected ${partitions.length} messages, got ${result.messages?.length}`);
    }
    
    // Verify priority ordering
    const expectedOrder = ['ultra-high', 'high', 'medium', 'low'];
    for (let i = 0; i < expectedOrder.length; i++) {
      if (result.messages[i].partition !== expectedOrder[i]) {
        throw new Error(`Wrong partition order at position ${i}: expected ${expectedOrder[i]}, got ${result.messages[i].partition}`);
      }
    }
    
    // Acknowledge all messages
    for (const message of result.messages) {
      await client.ack(message.transactionId, 'completed');
    }
    
    passTest('Partition priority ordering works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 14: Queue Priority Ordering
 */
async function testQueuePriorityOrdering() {
  startTest('Queue Priority Ordering');
  
  try {
    // Setup multiple queues with different priorities in same namespace
    const queues = [
      { name: 'test-queue-ultra-high', priority: 100 },
      { name: 'test-queue-high', priority: 10 },
      { name: 'test-queue-medium', priority: 5 },
      { name: 'test-queue-low', priority: 1 }
    ];
    
    // Create queues with priorities and namespace
    for (const queue of queues) {
      await dbPool.query(`
        INSERT INTO queen.queues (name, namespace, priority) 
        VALUES ($1, 'priority-test-namespace', $2)
        ON CONFLICT (name) DO UPDATE SET 
          namespace = EXCLUDED.namespace,
          priority = EXCLUDED.priority
      `, [queue.name, queue.priority]);
      
      // Create default partition
      await dbPool.query(`
        INSERT INTO queen.partitions (queue_id, name, priority)
        SELECT q.id, 'Default', 0
        FROM queen.queues q
        WHERE q.name = $1
        ON CONFLICT (queue_id, name) DO NOTHING
      `, [queue.name]);
    }
    
    // Push messages to queues in reverse priority order
    const messages = [];
    for (let i = queues.length - 1; i >= 0; i--) {
      const queue = queues[i];
      messages.push({
        queue: queue.name,
        partition: 'Default',
        payload: { 
          message: `${queue.name} message`, 
          queuePriority: queue.priority,
          order: queues.length - i
        }
      });
    }
    
    await client.push({ items: messages });
    await sleep(100);
    
    // Pop messages using namespace filter (should come in queue priority order)
    const results = [];
    for (let i = 0; i < queues.length; i++) {
      const result = await client.pop({
        namespace: 'priority-test-namespace',
        batch: 1
      });
      
      if (result.messages && result.messages.length > 0) {
        results.push(result.messages[0]);
        await client.ack(result.messages[0].transactionId, 'completed');
      }
    }
    
    if (results.length !== queues.length) {
      throw new Error(`Expected ${queues.length} messages, got ${results.length}`);
    }
    
    // Verify queue priority ordering
    const expectedOrder = ['test-queue-ultra-high', 'test-queue-high', 'test-queue-medium', 'test-queue-low'];
    for (let i = 0; i < expectedOrder.length; i++) {
      if (results[i].queue !== expectedOrder[i]) {
        throw new Error(`Wrong queue order at position ${i}: expected ${expectedOrder[i]}, got ${results[i].queue}`);
      }
    }
    
    passTest('Queue priority ordering works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 15: FIFO Within Partition
 */
async function testFIFOWithinPartition() {
  startTest('FIFO Within Partition');
  
  try {
    const messageCount = 5;
    const queueName = `test-fifo-queue-${Date.now()}`;
    const partitionName = `fifo-partition-${Date.now()}`;
    
    // Clean up any existing messages in this queue
    await dbPool.query(`
      DELETE FROM queen.messages 
      WHERE partition_id IN (
        SELECT p.id FROM queen.partitions p
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name = $1
      )
    `, [queueName]);
    
    const messages = [];
    
    // Create messages with timestamps to ensure order
    for (let i = 1; i <= messageCount; i++) {
      messages.push({
        queue: queueName,
        partition: partitionName,
        payload: { 
          message: `FIFO message ${i}`, 
          order: i,
          timestamp: Date.now() + i // Ensure increasing timestamps
        }
      });
      
      // Small delay to ensure different timestamps
      await sleep(10);
    }
    
    // Push all messages
    await client.push({ items: messages });
    await sleep(100);
    
    // Pop all messages
    const result = await client.pop({
      queue: queueName,
      partition: partitionName,
      batch: messageCount
    });
    
    if (!result.messages || result.messages.length !== messageCount) {
      throw new Error(`Expected ${messageCount} messages, got ${result.messages?.length}`);
    }
    
    // Verify FIFO order
    for (let i = 0; i < messageCount; i++) {
      const expectedOrder = i + 1;
      const actualOrder = result.messages[i].data.order;
      
      if (actualOrder !== expectedOrder) {
        throw new Error(`FIFO violation at position ${i}: expected order ${expectedOrder}, got ${actualOrder}`);
      }
    }
    
    // Acknowledge all messages
    for (const message of result.messages) {
      await client.ack(message.transactionId, 'completed');
    }
    
    passTest('FIFO ordering within partition works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 16: Error Handling
 */
async function testErrorHandling() {
  startTest('Error Handling');
  
  try {
    let errorsCaught = 0;
    
    // Test 1: Push without queue name
    try {
      await client.push({
        items: [{
          payload: { message: 'No queue name' }
        }]
      });
    } catch (error) {
      if (error.message.includes('queue')) {
        errorsCaught++;
      }
    }
    
    // Test 2: Push without payload
    try {
      await client.push({
        items: [{
          queue: 'test-error-queue'
        }]
      });
    } catch (error) {
      if (error.message.includes('payload')) {
        errorsCaught++;
      }
    }
    
    // Test 3: Pop without specifying queue, namespace, or task
    try {
      await client.pop({});
    } catch (error) {
      if (error.message.includes('Must specify')) {
        errorsCaught++;
      }
    }
    
    // Test 4: Acknowledge non-existent message
    try {
      await client.ack('00000000-0000-0000-0000-000000000000', 'completed');
    } catch (error) {
      if (error.message.includes('not found') || error.message.includes('Message not found')) {
        errorsCaught++;
      }
    }
    
    if (errorsCaught !== 4) {
      throw new Error(`Expected 4 errors to be caught, got ${errorsCaught}. Check individual error handling above.`);
    }
    
    passTest('Error handling works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Main Test Runner
 */
async function runAllTests() {
  console.log('🚀 Starting Comprehensive Queen Message Queue Test Suite\n');
  console.log('=' .repeat(80));
  
  try {
    // Initialize client and database connection
    client = createQueenClient({ baseUrl: TEST_CONFIG.baseUrl });
    dbPool = new pg.Pool(TEST_CONFIG.dbConfig);
    
    // Test database connection
    await dbPool.query('SELECT 1');
    log('Database connection established');
    
    // Clean up any existing test data
    await cleanupTestData();
    
    // Run all tests
    const tests = [
      testSingleMessagePush,
      testBatchMessagePush,
      testDefaultPartition,
      testQueueConfiguration,
      testPopSpecificPartition,
      testPopFromQueue,
      testNamespaceFiltering,
      testDelayedProcessing,
      testWindowBuffer,
      testMessageAcknowledgment,
      testBatchAcknowledgment,
      testConsumerPattern,
      testPartitionPriorityOrdering,
      testQueuePriorityOrdering,
      testFIFOWithinPartition,
      testErrorHandling
    ];
    
    log(`Running ${tests.length} tests...`);
    console.log('=' .repeat(80));
    
    for (const test of tests) {
      try {
        await test();
      } catch (error) {
        log(`Unexpected error in test: ${error.message}`, 'error');
      }
      
      // Small delay between tests
      await sleep(100);
    }
    
  } catch (error) {
    log(`Test suite setup failed: ${error.message}`, 'error');
  } finally {
    // Cleanup
    if (dbPool) {
      await cleanupTestData();
      await dbPool.end();
    }
  }
  
  // Print results
  console.log('\n' + '=' .repeat(80));
  console.log('📊 TEST RESULTS SUMMARY');
  console.log('=' .repeat(80));
  
  const passed = testResults.filter(r => r.status === 'PASS').length;
  const failed = testResults.filter(r => r.status === 'FAIL').length;
  const total = testResults.length;
  
  console.log(`\n📈 Overall Results: ${passed}/${total} tests passed`);
  
  if (failed > 0) {
    console.log('\n❌ Failed Tests:');
    testResults
      .filter(r => r.status === 'FAIL')
      .forEach(r => {
        console.log(`   • ${r.test}: ${r.error}`);
      });
  }
  
  if (passed > 0) {
    console.log('\n✅ Passed Tests:');
    testResults
      .filter(r => r.status === 'PASS')
      .forEach(r => {
        console.log(`   • ${r.test}${r.message ? ': ' + r.message : ''}`);
      });
  }
  
  console.log('\n' + '=' .repeat(80));
  
  if (failed === 0) {
    console.log('🎉 ALL TESTS PASSED! Queen Message Queue System is working correctly.');
  } else {
    console.log(`⚠️  ${failed} test(s) failed. Please review the failures above.`);
    process.exit(1);
  }
}

// Run the test suite
runAllTests().catch(error => {
  console.error('💥 Test suite crashed:', error);
  process.exit(1);
});
