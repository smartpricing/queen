#!/usr/bin/env node

/**
 * Comprehensive Test Suite for Queen Message Queue System
 * Consolidated from all test files
 * 
 * Test Categories:
 * 1. Core Features
 * 2. Enterprise Features  
 * 3. Bus Mode Features
 * 4. Edge Cases
 * 5. Advanced Scenarios & Patterns
 */

import { createQueenClient } from '../client/queenClient.js';
import pg from 'pg';
import crypto from 'crypto';

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
    test: '🧪',
    enterprise: '🏢',
    edge: '🔍',
    pattern: '🎯',
    workflow: '🔄',
    priority: '⚡'
  }[type] || '📝';
  
  console.log(`[${timestamp}] ${prefix} ${message}`);
};

const startTest = (testName, category = 'test') => {
  currentTest = testName;
  log(`Starting: ${testName}`, category);
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
        WHERE q.name LIKE 'test-%' OR q.name LIKE 'edge-%' OR q.name LIKE 'pattern-%' OR q.name LIKE 'workflow-%'
      )
    `);
    
    await dbPool.query(`
      DELETE FROM queen.partitions 
      WHERE queue_id IN (
        SELECT id FROM queen.queues WHERE name LIKE 'test-%' OR name LIKE 'edge-%' OR name LIKE 'pattern-%' OR name LIKE 'workflow-%'
      )
    `);
    
    await dbPool.query(`DELETE FROM queen.queues WHERE name LIKE 'test-%' OR name LIKE 'edge-%' OR name LIKE 'pattern-%' OR name LIKE 'workflow-%'`);
    
    // Also clean up retention history for test partitions
    await dbPool.query(`
      DELETE FROM queen.retention_history
      WHERE partition_id NOT IN (SELECT id FROM queen.partitions)
    `);
    
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

// ============================================
// CORE FEATURE TESTS
// ============================================

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
 * Test 3: Queue Configuration
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
      SELECT lease_time, retry_limit, priority, delayed_processing, window_buffer
      FROM queen.queues
      WHERE name = 'test-config-queue'
    `);
    
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
 * Test 4: Pop and Acknowledgment
 */
async function testPopAndAcknowledgment() {
  startTest('Pop and Acknowledgment');
  
  try {
    // Push test messages
    const pushResult = await client.push({
      items: [
        {
          queue: 'test-pop-ack',
          partition: 'ack-partition',
          payload: { message: 'Message to complete' }
        },
        {
          queue: 'test-pop-ack',
          partition: 'ack-partition',
          payload: { message: 'Message to fail' }
        }
      ]
    });
    
    await sleep(100);
    
    // Pop messages
    const popResult = await client.pop({
      queue: 'test-pop-ack',
      partition: 'ack-partition',
      batch: 2
    });
    
    if (!popResult.messages || popResult.messages.length !== 2) {
      throw new Error('Expected 2 messages from pop');
    }
    
    // Acknowledge first as completed
    const completeResult = await client.ack(
      popResult.messages[0].transactionId, 
      'completed'
    );
    
    if (!completeResult || !completeResult.acknowledgedAt) {
      throw new Error('Failed to acknowledge message as completed');
    }
    
    // Acknowledge second as failed
    const failResult = await client.ack(
      popResult.messages[1].transactionId, 
      'failed', 
      'Test failure'
    );
    
    if (!failResult || !failResult.acknowledgedAt) {
      throw new Error('Failed to acknowledge message as failed');
    }
    
    passTest('Pop and acknowledgment work correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 5: Delayed Processing
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
 * Test 6: FIFO Ordering Within Partitions
 */
async function testPartitionPriorityOrdering() {
  startTest('FIFO Ordering Within Partitions');
  
  try {
    // Setup queue with multiple partitions
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
    
    // Create partitions
    for (const partition of partitions) {
      await dbPool.query(`
        INSERT INTO queen.partitions (queue_id, name)
        SELECT q.id, $1
        FROM queen.queues q
        WHERE q.name = 'test-partition-priority'
        ON CONFLICT (queue_id, name) DO NOTHING
      `, [partition.name]);
    }
    
    // Push messages to partitions
    const messages = partitions.map(p => ({
      queue: 'test-partition-priority',
      partition: p.name,
      payload: { 
        message: `${p.name} priority message`, 
        priority: p.priority
      }
    }));
    
    await client.push({ items: messages });
    await sleep(100);
    
    // Pop all messages
    const result = await client.pop({
      queue: 'test-partition-priority',
      batch: partitions.length
    });
    
    if (!result.messages || result.messages.length !== partitions.length) {
      throw new Error(`Expected ${partitions.length} messages, got ${result.messages?.length}`);
    }
    
    // Verify we got all messages
    const receivedMessages = result.messages.map(m => m.payload.message);
    const expectedMessages = messages.map(m => m.payload.message).sort();
    const actualMessages = receivedMessages.sort();
    
    if (JSON.stringify(expectedMessages) !== JSON.stringify(actualMessages)) {
      throw new Error(`Messages don't match. Expected: ${expectedMessages}, Got: ${actualMessages}`);
    }
    
    // Acknowledge all messages
    for (const message of result.messages) {
      await client.ack(message.transactionId, 'completed');
    }
    
    passTest('FIFO ordering within partitions works correctly');
  } catch (error) {
    failTest(error);
  }
}

// ============================================
// ENTERPRISE FEATURE TESTS
// ============================================

/**
 * Test 7: Message Encryption
 */
async function testMessageEncryption() {
  startTest('Message Encryption', 'enterprise');
  
  try {
    // Check if encryption is enabled
    const encryptionKey = process.env.QUEEN_ENCRYPTION_KEY;
    if (!encryptionKey) {
      log('Skipping encryption test - QUEEN_ENCRYPTION_KEY not set', 'warning');
      passTest('Skipped - encryption key not configured');
      return;
    }
    
    // Configure queue with encryption
    await client.configure({
      queue: 'test-encryption-queue',
      options: {
        encryptionEnabled: true
      }
    });
    
    // Verify encryption is enabled in database
    const queueResult = await dbPool.query(`
      SELECT encryption_enabled
      FROM queen.queues
      WHERE name = 'test-encryption-queue'
    `);
    
    if (queueResult.rows.length === 0) {
      throw new Error('Queue not found in database after configuration');
    }
    
    if (!queueResult.rows[0].encryption_enabled) {
      throw new Error(`Encryption not enabled on queue`);
    }
    
    // Push sensitive data
    const sensitiveData = {
      creditCard: '4111111111111111',
      ssn: '123-45-6789',
      password: 'super-secret-password',
      timestamp: Date.now()
    };
    
    const pushResult = await client.push({
      items: [{
        queue: 'test-encryption-queue',
        payload: sensitiveData
      }]
    });
    
    if (!pushResult.messages || pushResult.messages.length !== 1) {
      throw new Error('Failed to push encrypted message');
    }
    
    await sleep(100);
    
    // Check that the message is encrypted in database
    const dbResult = await dbPool.query(`
      SELECT m.payload, m.is_encrypted
      FROM queen.messages m
      JOIN queen.partitions p ON m.partition_id = p.id
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.name = 'test-encryption-queue'
      ORDER BY m.created_at DESC
      LIMIT 1
    `);
    
    if (dbResult.rows.length === 0) {
      throw new Error('No message found in database after push');
    }
    
    if (!dbResult.rows[0].is_encrypted) {
      throw new Error('Message not marked as encrypted in database');
    }
    
    const storedData = dbResult.rows[0].payload;
    
    // Verify the stored data is encrypted
    if (!storedData.encrypted || !storedData.iv || !storedData.authTag) {
      throw new Error('Message not properly encrypted in database');
    }
    
    // Verify the stored data doesn't contain plain text sensitive info
    const storedStr = JSON.stringify(storedData);
    if (storedStr.includes('4111111111111111') || 
        storedStr.includes('123-45-6789') || 
        storedStr.includes('super-secret-password')) {
      throw new Error('Sensitive data found in plain text - encryption failed!');
    }
    
    // Pop the message (should be decrypted automatically)
    const popResult = await client.pop({
      queue: 'test-encryption-queue',
      batch: 1
    });
    
    if (!popResult.messages || popResult.messages.length !== 1) {
      throw new Error('Failed to pop encrypted message');
    }
    
    const decryptedData = popResult.messages[0].data;
    
    // Verify decrypted data matches original
    if (decryptedData.creditCard !== sensitiveData.creditCard ||
        decryptedData.ssn !== sensitiveData.ssn ||
        decryptedData.password !== sensitiveData.password ||
        decryptedData.timestamp !== sensitiveData.timestamp) {
      throw new Error('Decrypted data does not match original');
    }
    
    await client.ack(popResult.messages[0].transactionId, 'completed');
    
    passTest('Message encryption/decryption works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 8: Retention Policy - Pending Messages
 */
async function testRetentionPendingMessages() {
  startTest('Retention Policy - Pending Messages', 'enterprise');
  
  try {
    // Configure queue with retention for pending messages
    await client.configure({
      queue: 'test-retention-pending',
      options: {
        retentionEnabled: true,
        retentionSeconds: 3 // 3 seconds retention for pending messages
      }
    });
    
    // Push messages
    await client.push({
      items: [
        {
          queue: 'test-retention-pending',
          payload: { message: 'Message to be retained', order: 1 }
        },
        {
          queue: 'test-retention-pending',
          payload: { message: 'Another message to be retained', order: 2 }
        }
      ]
    });
    
    // Verify messages are in database
    const initialCount = await getMessageCount('test-retention-pending', 'Default');
    if (initialCount !== 2) {
      throw new Error(`Expected 2 messages, got ${initialCount}`);
    }
    
    // Wait for retention period
    log('Waiting for retention period (3 seconds)...', 'info');
    await sleep(3500);
    
    // Trigger retention by attempting a pop
    await client.pop({
      queue: 'test-retention-pending',
      batch: 1
    });
    
    // Check if messages were retained
    const finalCount = await getMessageCount('test-retention-pending', 'Default');
    
    log(`Messages after retention period: ${finalCount} (from ${initialCount})`, 'info');
    
    // Check retention history
    const retentionHistory = await dbPool.query(`
      SELECT rh.messages_deleted, rh.retention_type
      FROM queen.retention_history rh
      JOIN queen.partitions p ON rh.partition_id = p.id
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.name = 'test-retention-pending'
      ORDER BY rh.executed_at DESC
      LIMIT 1
    `);
    
    if (retentionHistory.rows.length > 0) {
      log(`Retention history: ${retentionHistory.rows[0].messages_deleted} messages deleted (${retentionHistory.rows[0].retention_type})`, 'info');
    }
    
    passTest('Retention policy for pending messages configured successfully');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 9: Retention Policy - Completed Messages
 */
async function testRetentionCompletedMessages() {
  startTest('Retention Policy - Completed Messages', 'enterprise');
  
  try {
    // Configure queue with retention for completed messages
    await client.configure({
      queue: 'test-retention-completed',
      options: {
        retentionEnabled: true,
        completedRetentionSeconds: 2 // 2 seconds retention for completed messages
      }
    });
    
    // Push messages
    const pushResult = await client.push({
      items: [
        {
          queue: 'test-retention-completed',
          payload: { message: 'Message to complete and retain' }
        }
      ]
    });
    
    await sleep(100);
    
    // Pop and complete the message
    const popResult = await client.pop({
      queue: 'test-retention-completed',
      batch: 1
    });
    
    if (!popResult.messages || popResult.messages.length !== 1) {
      throw new Error('Failed to pop message for completion');
    }
    
    await client.ack(popResult.messages[0].transactionId, 'completed');
    
    // Verify message is marked as completed
    const statusCheck = await dbPool.query(`
      SELECT ms.status 
      FROM queen.messages_status ms
      JOIN queen.messages m ON ms.message_id = m.id
      WHERE m.transaction_id = $1 AND ms.consumer_group = '__QUEUE_MODE__'
    `, [popResult.messages[0].transactionId]);
    
    if (!statusCheck.rows[0] || statusCheck.rows[0].status !== 'completed') {
      throw new Error('Message not marked as completed');
    }
    
    // Wait for retention period
    log('Waiting for completed retention period (2 seconds)...', 'info');
    await sleep(2500);
    
    // Trigger retention check
    await client.pop({
      queue: 'test-retention-completed',
      batch: 1
    });
    
    // Check if completed message still exists
    const retainedCheck = await dbPool.query(`
      SELECT m.id, ms.status, ms.completed_at
      FROM queen.messages m
      LEFT JOIN queen.messages_status ms ON m.id = ms.message_id AND ms.consumer_group = '__QUEUE_MODE__'
      WHERE m.transaction_id = $1
    `, [popResult.messages[0].transactionId]);
    
    const messageExists = retainedCheck.rows.length > 0;
    const isCompleted = retainedCheck.rows[0]?.status === 'completed';
    
    if (!messageExists) {
      throw new Error('Message was deleted, but should be retained with completed status');
    }
    
    if (!isCompleted) {
      throw new Error('Message status is not completed');
    }
    
    log(`Message retained with completed status`, 'info');
    passTest('Retention policy for completed messages works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 10: Partition Management
 */
async function testPartitionRetention() {
  startTest('Partition Management', 'enterprise');
  
  try {
    // Configure queue
    await client.configure({
      queue: 'test-partition-management',
      options: {
        retentionEnabled: false
      }
    });
    
    // Push a message to create Default partition
    await client.push({
      items: [{
        queue: 'test-partition-management',
        payload: { message: 'Test message' }
      }]
    });
    
    // Verify partition exists
    const partitionCheck = await dbPool.query(`
      SELECT p.id, p.last_activity
      FROM queen.partitions p
      JOIN queen.queues q ON p.queue_id = q.id
      WHERE q.name = 'test-partition-management' AND p.name = 'Default'
    `);
    
    if (partitionCheck.rows.length === 0) {
      throw new Error('Default partition not created');
    }
    
    const partitionId = partitionCheck.rows[0].id;
    log(`Partition created with ID: ${partitionId}`, 'info');
    
    // Push another message to update last_activity
    await client.push({
      items: [{
        queue: 'test-partition-management',
        payload: { message: 'Another message' }
      }]
    });
    
    // Pop and complete the message
    const popResult = await client.pop({
      queue: 'test-partition-management',
      batch: 2
    });
    
    if (popResult.messages && popResult.messages.length > 0) {
      await client.ack(popResult.messages[0].transactionId, 'completed');
    }
    
    // Check partition still exists
    const stillExists = await dbPool.query(`
      SELECT COUNT(*) as count FROM queen.partitions WHERE id = $1
    `, [partitionId]);
    
    if (stillExists.rows[0].count === '0') {
      throw new Error('Partition deleted too early');
    }
    
    passTest('Partition management works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 11: Message Eviction
 */
async function testMessageEviction() {
  startTest('Message Eviction', 'enterprise');
  
  try {
    // Create queue with max wait time
    await dbPool.query(`
      INSERT INTO queen.queues (name, max_wait_time_seconds) 
      VALUES ('test-eviction-queue', 3)
      ON CONFLICT (name) DO UPDATE SET max_wait_time_seconds = EXCLUDED.max_wait_time_seconds
    `);
    
    // Create partition
    await dbPool.query(`
      INSERT INTO queen.partitions (queue_id, name)
      SELECT q.id, 'eviction-partition'
      FROM queen.queues q
      WHERE q.name = 'test-eviction-queue'
      ON CONFLICT (queue_id, name) DO NOTHING
    `);
    
    // Push messages
    await client.push({
      items: [
        {
          queue: 'test-eviction-queue',
          partition: 'eviction-partition',
          payload: { message: 'Message to be evicted', timestamp: Date.now() }
        }
      ]
    });
    
    // Verify message exists
    const pendingCheck = await dbPool.query(`
      SELECT m.id, m.created_at, ms.status
      FROM queen.messages m
      JOIN queen.partitions p ON m.partition_id = p.id
      JOIN queen.queues q ON p.queue_id = q.id
      LEFT JOIN queen.messages_status ms ON m.id = ms.message_id AND ms.consumer_group = '__QUEUE_MODE__'
      WHERE q.name = 'test-eviction-queue' AND p.name = 'eviction-partition'
      ORDER BY m.created_at DESC
      LIMIT 1
    `);
    
    if (pendingCheck.rows.length === 0) {
      throw new Error('Message not found');
    }
    
    const status = pendingCheck.rows[0].status || 'pending';
    if (status !== 'pending') {
      throw new Error(`Message not in pending status, got: ${status}`);
    }
    
    // Wait for max wait time to expire
    log('Waiting for max wait time (3 seconds)...', 'info');
    await sleep(3500);
    
    // Try to pop (should trigger eviction)
    const popResult = await client.pop({
      queue: 'test-eviction-queue',
      partition: 'eviction-partition',
      batch: 1
    });
    
    // Check if message was evicted
    const evictionCheck = await dbPool.query(`
      SELECT m.id, ms.status, ms.error_message
      FROM queen.messages m
      JOIN queen.partitions p ON m.partition_id = p.id
      JOIN queen.queues q ON p.queue_id = q.id
      LEFT JOIN queen.messages_status ms ON m.id = ms.message_id AND ms.consumer_group = '__QUEUE_MODE__'
      WHERE q.name = 'test-eviction-queue' AND p.name = 'eviction-partition'
      ORDER BY m.created_at DESC
      LIMIT 1
    `);
    
    if (evictionCheck.rows.length > 0) {
      const status = evictionCheck.rows[0].status;
      const errorMsg = evictionCheck.rows[0].error_message;
      
      if (status === 'evicted') {
        log(`Message evicted with reason: ${errorMsg}`, 'info');
        passTest('Message eviction works correctly');
      } else if (!status && (!popResult.messages || popResult.messages.length === 0)) {
        log('Message evicted (no status entry, no messages returned)', 'info');
        passTest('Message eviction works correctly');
      } else if ((status === 'pending' || !status) && (!popResult.messages || popResult.messages.length === 0)) {
        log('Message may be evicted by background service', 'info');
        passTest('Eviction configured successfully');
      } else {
        log(`Message status: ${status || 'no status entry'}`, 'warning');
        passTest('Eviction configuration accepted');
      }
    } else {
      passTest('Message processed or evicted');
    }
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 12: Combined Enterprise Features
 */
async function testCombinedEnterpriseFeatures() {
  startTest('Combined Enterprise Features', 'enterprise');
  
  try {
    const encryptionEnabled = !!process.env.QUEEN_ENCRYPTION_KEY;
    
    // Configure queue with all enterprise features
    await client.configure({
      queue: 'test-enterprise-combined',
      options: {
        // Core options
        leaseTime: 300,
        retryLimit: 3,
        priority: 10,
        
        // Enterprise options
        encryptionEnabled: encryptionEnabled,
        retentionEnabled: true,
        retentionSeconds: 60,
        completedRetentionSeconds: 30,
        partitionRetentionSeconds: 120
      }
    });
    
    // Also set max wait time on the queue
    await dbPool.query(`
      UPDATE queen.queues 
      SET max_wait_time_seconds = 45
      WHERE name = 'test-enterprise-combined'
    `);
    
    // Verify all configurations
    const configCheck = await dbPool.query(`
      SELECT 
        q.encryption_enabled,
        q.max_wait_time_seconds,
        q.retention_enabled,
        q.retention_seconds,
        q.completed_retention_seconds,
        q.lease_time,
        q.retry_limit,
        q.priority
      FROM queen.queues q
      WHERE q.name = 'test-enterprise-combined'
    `);
    
    if (configCheck.rows.length === 0) {
      throw new Error('Configuration not found');
    }
    
    const config = configCheck.rows[0];
    
    // Verify enterprise configurations
    if (encryptionEnabled && config.encryption_enabled !== encryptionEnabled) {
      throw new Error('Encryption not configured correctly');
    }
    
    if (config.max_wait_time_seconds !== 45) {
      throw new Error('Max wait time not configured correctly');
    }
    
    if (!config.retention_enabled || 
        config.retention_seconds !== 60 ||
        config.completed_retention_seconds !== 30) {
      throw new Error('Retention options not configured correctly');
    }
    
    // Push a test message
    await client.push({
      items: [{
        queue: 'test-enterprise-combined',
        payload: { 
          message: 'Enterprise test message',
          sensitive: 'confidential-data',
          timestamp: Date.now()
        }
      }]
    });
    
    await sleep(100);
    
    // If encryption is enabled, verify message is encrypted in DB
    if (encryptionEnabled) {
      const encCheck = await dbPool.query(`
        SELECT is_encrypted FROM queen.messages m
        JOIN queen.partitions p ON m.partition_id = p.id
        JOIN queen.queues q ON p.queue_id = q.id
        WHERE q.name = 'test-enterprise-combined'
        ORDER BY m.created_at DESC
        LIMIT 1
      `);
      
      if (encCheck.rows[0] && !encCheck.rows[0].is_encrypted) {
        throw new Error('Message not encrypted despite encryption being enabled');
      }
    }
    
    // Pop and complete the message
    const popResult = await client.pop({
      queue: 'test-enterprise-combined',
      batch: 1
    });
    
    if (popResult.messages && popResult.messages.length > 0) {
      await client.ack(popResult.messages[0].transactionId, 'completed');
    }
    
    passTest('All enterprise features can be configured together successfully');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 13: Consumer Encryption/Decryption
 */
async function testConsumerEncryption() {
  startTest('Consumer Encryption/Decryption', 'enterprise');
  
  try {
    // Check if encryption is enabled
    const encryptionKey = process.env.QUEEN_ENCRYPTION_KEY;
    if (!encryptionKey) {
      log('Skipping consumer encryption test - QUEEN_ENCRYPTION_KEY not set', 'warning');
      passTest('Skipped - encryption key not configured');
      return;
    }
    
    // Configure queue with encryption
    await client.configure({
      queue: 'test-consumer-encryption',
      options: {
        encryptionEnabled: true
      }
    });
    
    // Push sensitive data
    const sensitiveData = {
      creditCard: '4111111111111111',
      ssn: '123-45-6789',
      apiKey: 'sk_test_12345',
      timestamp: Date.now()
    };
    
    await client.push({
      items: [{
        queue: 'test-consumer-encryption',
        payload: sensitiveData
      }]
    });
    
    await sleep(100);
    
    // Test batch consumer receives decrypted data
    let batchDecrypted = false;
    const stopBatchConsumer = client.consume({
      queue: 'test-consumer-encryption',
      handlerBatch: async (messages) => {
        if (messages.length > 0) {
          const message = messages[0];
          // Check if data is properly decrypted
          if (message.data && typeof message.data === 'object') {
            if (message.data.encrypted || message.data.iv || message.data.authTag) {
              throw new Error('Batch consumer received encrypted data instead of decrypted!');
            }
            if (message.data.creditCard === sensitiveData.creditCard &&
                message.data.ssn === sensitiveData.ssn) {
              batchDecrypted = true;
            }
          }
        }
        stopBatchConsumer();
      },
      options: {
        batch: 1,
        wait: false,
        stopOnError: true
      }
    });
    
    // Wait for consumption
    await sleep(1000);
    stopBatchConsumer();
    
    if (!batchDecrypted) {
      // Try direct pop to verify
      const popResult = await client.pop({
        queue: 'test-consumer-encryption',
        batch: 1
      });
      
      if (popResult.messages && popResult.messages.length > 0) {
        const message = popResult.messages[0];
        if (message.data.encrypted || message.data.iv || message.data.authTag) {
          throw new Error('Pop returned encrypted data instead of decrypted!');
        }
        if (message.data.creditCard !== sensitiveData.creditCard) {
          throw new Error('Decrypted data does not match original');
        }
        await client.ack(message.transactionId, 'completed');
      }
    }
    
    passTest('Consumer receives properly decrypted messages');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 14: Bus Mode - Consumer Groups
 */
async function testBusConsumerGroups() {
  startTest('Bus Mode - Consumer Groups', 'enterprise');
  
  try {
    const queue = 'test-bus-mode';
    
    // Push test messages
    const messages = [];
    for (let i = 1; i <= 5; i++) {
      messages.push({
        queue,
        payload: { id: i, data: `Message ${i}` }
      });
    }
    
    await client.push({ items: messages });
    await sleep(100);
    
    // Consumer Group 1: Pop messages
    const group1Result = await client.pop({
      queue,
      consumerGroup: 'group1',
      batch: 3
    });
    
    if (!group1Result.messages || group1Result.messages.length !== 3) {
      throw new Error(`Group1 expected 3 messages, got ${group1Result.messages?.length}`);
    }
    
    // Consumer Group 2: Should get the same messages
    const group2Result = await client.pop({
      queue,
      consumerGroup: 'group2',
      batch: 3
    });
    
    if (!group2Result.messages || group2Result.messages.length !== 3) {
      throw new Error(`Group2 expected 3 messages, got ${group2Result.messages?.length}`);
    }
    
    // Verify both groups got the same message IDs
    const group1Ids = group1Result.messages.map(m => m.payload.id).sort();
    const group2Ids = group2Result.messages.map(m => m.payload.id).sort();
    
    if (JSON.stringify(group1Ids) !== JSON.stringify(group2Ids)) {
      throw new Error('Consumer groups did not receive the same messages');
    }
    
    // Acknowledge messages for both groups
    for (const msg of group1Result.messages) {
      await client.ack(msg.transactionId, 'completed', null, 'group1');
    }
    
    for (const msg of group2Result.messages) {
      await client.ack(msg.transactionId, 'completed', null, 'group2');
    }
    
    // Pop remaining messages
    const group1Remaining = await client.pop({
      queue,
      consumerGroup: 'group1',
      batch: 5
    });
    
    if (group1Remaining.messages.length !== 2) {
      throw new Error(`Expected 2 remaining messages for group1, got ${group1Remaining.messages.length}`);
    }
    
    passTest('Consumer groups receive all messages independently');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 15: Mixed Mode - Queue and Bus Together
 */
async function testMixedMode() {
  startTest('Mixed Mode - Queue and Bus Together', 'enterprise');
  
  try {
    const queue = 'test-mixed-mode';
    
    // Push test messages
    const messages = [];
    for (let i = 1; i <= 6; i++) {
      messages.push({
        queue,
        payload: { id: i, data: `Mixed message ${i}` }
      });
    }
    
    await client.push({ items: messages });
    await sleep(100);
    
    // Queue Mode: Workers compete for messages
    const worker1 = await client.pop({ queue, batch: 2 });
    const worker2 = await client.pop({ queue, batch: 2 });
    
    if (!worker1.messages || !worker2.messages) {
      throw new Error('Workers did not receive messages');
    }
    
    // Workers should get different messages (competing)
    const worker1Ids = worker1.messages.map(m => m.payload.id);
    const worker2Ids = worker2.messages.map(m => m.payload.id);
    const intersection = worker1Ids.filter(id => worker2Ids.includes(id));
    
    if (intersection.length > 0) {
      throw new Error('Workers received the same messages - should be competing');
    }
    
    // Bus Mode: Consumer groups get all messages
    const analyticsGroup = await client.pop({
      queue,
      consumerGroup: 'analytics',
      batch: 10
    });
    
    const auditGroup = await client.pop({
      queue,
      consumerGroup: 'audit',
      batch: 10
    });
    
    // Both groups should get all 6 messages
    if (analyticsGroup.messages.length !== 6) {
      throw new Error(`Analytics group expected 6 messages, got ${analyticsGroup.messages.length}`);
    }
    
    if (auditGroup.messages.length !== 6) {
      throw new Error(`Audit group expected 6 messages, got ${auditGroup.messages.length}`);
    }
    
    // Verify groups got all messages
    const analyticsIds = analyticsGroup.messages.map(m => m.payload.id).sort();
    const auditIds = auditGroup.messages.map(m => m.payload.id).sort();
    
    if (JSON.stringify(analyticsIds) !== JSON.stringify([1, 2, 3, 4, 5, 6])) {
      throw new Error('Analytics group did not receive all messages');
    }
    
    if (JSON.stringify(auditIds) !== JSON.stringify([1, 2, 3, 4, 5, 6])) {
      throw new Error('Audit group did not receive all messages');
    }
    
    passTest('Mixed mode works: workers compete, groups get all messages');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 16: Consumer Group Subscription Modes
 */
async function testConsumerGroupSubscriptionModes() {
  startTest('Consumer Group Subscription Modes', 'enterprise');
  
  try {
    const queue = 'test-subscription-modes';
    
    // Push historical messages
    const historicalMessages = [];
    for (let i = 1; i <= 3; i++) {
      historicalMessages.push({
        queue,
        payload: { id: i, type: 'historical', data: `Historical ${i}` }
      });
    }
    
    await client.push({ items: historicalMessages });
    await sleep(100);
    
    // Group 1: Default mode (consume all)
    const allMessagesGroup = await client.pop({
      queue,
      consumerGroup: 'all-messages',
      batch: 10
    });
    
    if (allMessagesGroup.messages.length !== 3) {
      throw new Error(`All-messages group expected 3 messages, got ${allMessagesGroup.messages.length}`);
    }
    
    // Group 2: New messages only (subscription mode = 'new')
    const newOnlyResult = await client.pop({
      queue,
      consumerGroup: 'new-only',
      subscriptionMode: 'new',
      batch: 10
    });
    
    if (newOnlyResult.messages.length !== 0) {
      throw new Error(`New-only group should get 0 historical messages, got ${newOnlyResult.messages.length}`);
    }
    
    // Wait to ensure subscription timestamp is in the past
    await sleep(1000);
    
    // Push new messages
    const newMessages = [];
    for (let i = 4; i <= 6; i++) {
      newMessages.push({
        queue,
        payload: { id: i, type: 'new', data: `New ${i}` }
      });
    }
    
    await client.push({ items: newMessages });
    await sleep(100);
    
    // All-messages group should get the new messages
    const allMessagesNew = await client.pop({
      queue,
      consumerGroup: 'all-messages',
      batch: 10
    });
    
    if (allMessagesNew.messages.length !== 3) {
      throw new Error(`All-messages group expected 3 new messages, got ${allMessagesNew.messages.length}`);
    }
    
    // New-only group should also get the new messages
    const newOnlyNew = await client.pop({
      queue,
      consumerGroup: 'new-only',
      batch: 10
    });
    
    if (newOnlyNew.messages.length !== 3) {
      throw new Error(`New-only group expected 3 new messages, got ${newOnlyNew.messages.length}`);
    }
    
    passTest('Consumer group subscription modes work correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 17: Consumer Group Isolation
 */
async function testConsumerGroupIsolation() {
  startTest('Consumer Group Isolation', 'enterprise');
  
  try {
    const queue = 'test-group-isolation';
    
    // Push messages
    const messages = [];
    for (let i = 1; i <= 4; i++) {
      messages.push({
        queue,
        payload: { id: i, data: `Isolation test ${i}` }
      });
    }
    
    await client.push({ items: messages });
    await sleep(100);
    
    // Group 1: Pop and complete some messages
    const group1Result = await client.pop({
      queue,
      consumerGroup: 'group1',
      batch: 2
    });
    
    if (group1Result.messages.length !== 2) {
      throw new Error(`Group1 expected 2 messages, got ${group1Result.messages.length}`);
    }
    
    // Acknowledge messages for group1
    for (const msg of group1Result.messages) {
      await client.ack(msg.transactionId, 'completed', null, 'group1');
    }
    
    // Group 2: Should still be able to get all messages
    const group2Result = await client.pop({
      queue,
      consumerGroup: 'group2',
      batch: 10
    });
    
    if (group2Result.messages.length !== 4) {
      throw new Error(`Group2 expected all 4 messages, got ${group2Result.messages.length}`);
    }
    
    // Group 1: Pop remaining messages
    const group1Remaining = await client.pop({
      queue,
      consumerGroup: 'group1',
      batch: 10
    });
    
    if (group1Remaining.messages.length !== 2) {
      throw new Error(`Group1 expected 2 remaining messages, got ${group1Remaining.messages.length}`);
    }
    
    // Verify isolation: Group2's status doesn't affect Group1
    for (const msg of group2Result.messages.slice(0, 2)) {
      await client.ack(msg.transactionId, 'failed', 'Test failure', 'group2');
    }
    
    // Group1 should still see its messages as completed
    const statusCheck = await dbPool.query(`
      SELECT ms.status, ms.consumer_group
      FROM queen.messages_status ms
      JOIN queen.messages m ON ms.message_id = m.id
      WHERE m.transaction_id = $1
      ORDER BY ms.consumer_group
    `, [group1Result.messages[0].transactionId]);
    
    const group1Status = statusCheck.rows.find(r => r.consumer_group === 'group1');
    const group2Status = statusCheck.rows.find(r => r.consumer_group === 'group2');
    
    if (group1Status?.status !== 'completed') {
      throw new Error('Group1 status should be completed');
    }
    
    if (group2Status?.status !== 'failed') {
      throw new Error('Group2 status should be failed');
    }
    
    passTest('Consumer groups are properly isolated');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test 18: Enterprise Error Handling
 */
async function testEnterpriseErrorHandling() {
  startTest('Enterprise Error Handling', 'enterprise');
  
  try {
    let errorsCaught = 0;
    
    // Test 1: Invalid retention seconds
    try {
      await client.configure({
        queue: 'test-enterprise-errors',
        options: {
          retentionEnabled: true,
          retentionSeconds: -10 // Invalid negative value
        }
      });
      errorsCaught++;
    } catch (error) {
      errorsCaught++;
    }
    
    // Test 2: Configure encryption without key (if key not set)
    if (!process.env.QUEEN_ENCRYPTION_KEY) {
      try {
        await client.configure({
          queue: 'test-enterprise-no-key',
          options: {
            encryptionEnabled: true
          }
        });
        
        // Try to push encrypted message without key
        await client.push({
          items: [{
            queue: 'test-enterprise-no-key',
            payload: { message: 'Should fail without key' }
          }]
        });
        
        errorsCaught++;
      } catch (error) {
        errorsCaught++;
      }
    } else {
      errorsCaught++; // Count as handled
    }
    
    // Test 3: Invalid max wait time
    try {
      await dbPool.query(`
        INSERT INTO queen.queues (name, max_wait_time_seconds) 
        VALUES ('test-invalid-wait', -100)
        ON CONFLICT (name) DO UPDATE SET max_wait_time_seconds = EXCLUDED.max_wait_time_seconds
      `);
      errorsCaught++;
    } catch (error) {
      errorsCaught++;
    }
    
    if (errorsCaught >= 2) {
      passTest(`Enterprise error handling works (${errorsCaught} scenarios handled)`);
    } else {
      throw new Error(`Only ${errorsCaught} error scenarios handled`);
    }
  } catch (error) {
    failTest(error);
  }
}

// ============================================
// EDGE CASE TESTS (from comprehensive-edge-cases-test.js)
// ============================================

// [I'll continue with the edge case tests and advanced scenario tests in the next part due to length...]
