/**
 * Partition-Scoped Transaction ID Tests
 * 
 * Tests that transaction_id uniqueness is properly scoped to partitions.
 * With the constraint UNIQUE(partition_id, transaction_id), the same transaction_id
 * can exist in multiple partitions. This tests that ACK operations correctly
 * identify and operate on the right message by using BOTH partition_id AND transaction_id.
 */

import { startTest, passTest, failTest, sleep, dbPool, getMessageCount, log, TEST_CONFIG } from './utils.js';

/**
 * Test: Duplicate transaction IDs across partitions with correct ACK targeting
 * 
 * This is the critical test that verifies the fix for partition-scoped transaction_ids.
 * Without the fix, ACKing a message in one partition could accidentally ACK
 * a message with the same transaction_id in a different partition.
 */
export async function testDuplicateTransactionIdsAcrossPartitions(client) {
  startTest('Duplicate Transaction IDs Across Partitions - ACK Targeting', 'edge');
  
  try {
    const queue = 'test-duplicate-txn-ids';
    const partition1 = 'partition-A';
    const partition2 = 'partition-B';
    const sharedTxnId = 'shared-txn-12345';
    
    // Configure queue
    await client.queue(queue, { leaseTime: 30 });
    
    // CRITICAL: Push messages with the SAME transaction_id to DIFFERENT partitions
    log(`Pushing message with txn=${sharedTxnId} to ${partition1}`, 'test');
    await client.push(`${queue}/${partition1}`, 
      { data: 'Message in Partition A', partition: partition1 },
      { transactionId: sharedTxnId }
    );
    
    log(`Pushing message with txn=${sharedTxnId} to ${partition2}`, 'test');
    await client.push(`${queue}/${partition2}`, 
      { data: 'Message in Partition B', partition: partition2 },
      { transactionId: sharedTxnId }
    );
    
    await sleep(100);
    
    // Verify both messages exist in database
    const msgCountA = await getMessageCount(queue, partition1);
    const msgCountB = await getMessageCount(queue, partition2);
    
    if (msgCountA !== 1) {
      throw new Error(`Expected 1 message in ${partition1}, found ${msgCountA}`);
    }
    if (msgCountB !== 1) {
      throw new Error(`Expected 1 message in ${partition2}, found ${msgCountB}`);
    }
    
    log('✓ Both messages with same transaction_id exist in different partitions', 'success');
    
    // Take message from partition A
    log(`Taking message from ${partition1}`, 'test');
    let messageA = null;
    for await (const msg of client.take(`${queue}/${partition1}`, { limit: 1 })) {
      messageA = msg;
      break;
    }
    
    if (!messageA) {
      throw new Error('Failed to take message from partition A');
    }
    
    if (messageA.transactionId !== sharedTxnId) {
      throw new Error(`Expected transaction_id ${sharedTxnId}, got ${messageA.transactionId}`);
    }
    
    if (messageA.data.partition !== partition1) {
      throw new Error(`Expected partition ${partition1}, got ${messageA.data.partition}`);
    }
    
    log(`✓ Received correct message from ${partition1}: ${JSON.stringify(messageA.data)}`, 'success');
    
    // ACK the message from partition A
    log(`ACKing message in ${partition1} with txn=${sharedTxnId}`, 'test');
    await client.ack(messageA, true);
    
    await sleep(200);
    
    // CRITICAL CHECK: Verify message in partition A is acknowledged
    const result = await dbPool.query(`
      SELECT 
        m.id,
        m.transaction_id,
        p.name as partition_name,
        pc.last_consumed_id,
        CASE
          WHEN m.id <= pc.last_consumed_id AND DATE_TRUNC('milliseconds', m.created_at) <= DATE_TRUNC('milliseconds', pc.last_consumed_created_at)
          THEN 'completed'
          ELSE 'pending'
        END as status
      FROM queen.messages m
      JOIN queen.partitions p ON p.id = m.partition_id
      JOIN queen.queues q ON q.id = p.queue_id
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
      WHERE q.name = $1 AND m.transaction_id = $2
      ORDER BY p.name
    `, [queue, sharedTxnId]);
    
    if (result.rows.length !== 2) {
      throw new Error(`Expected 2 messages in database, found ${result.rows.length}`);
    }
    
    const msgA = result.rows.find(r => r.partition_name === partition1);
    const msgB = result.rows.find(r => r.partition_name === partition2);
    
    if (!msgA || !msgB) {
      throw new Error('Could not find both messages in query result');
    }
    
    log(`Message A (${partition1}): status=${msgA.status}`, 'test');
    log(`Message B (${partition2}): status=${msgB.status}`, 'test');
    
    // CRITICAL ASSERTION: Message in partition A should be completed
    if (msgA.status !== 'completed') {
      throw new Error(`Message in ${partition1} should be completed, but status is: ${msgA.status}`);
    }
    
    // CRITICAL ASSERTION: Message in partition B should still be pending
    if (msgB.status !== 'pending') {
      throw new Error(`Message in ${partition2} should still be pending, but status is: ${msgB.status}`);
    }
    
    log(`✓ ACK correctly targeted only ${partition1}, ${partition2} remains pending`, 'success');
    
    // Now take and ACK message from partition B
    log(`Taking message from ${partition2}`, 'test');
    let messageB = null;
    for await (const msg of client.take(`${queue}/${partition2}`, { limit: 1 })) {
      messageB = msg;
      break;
    }
    
    if (!messageB) {
      throw new Error('Failed to take message from partition B');
    }
    
    if (messageB.transactionId !== sharedTxnId) {
      throw new Error(`Expected transaction_id ${sharedTxnId}, got ${messageB.transactionId}`);
    }
    
    if (messageB.data.partition !== partition2) {
      throw new Error(`Expected partition ${partition2}, got ${messageB.data.partition}`);
    }
    
    log(`✓ Received correct message from ${partition2}: ${JSON.stringify(messageB.data)}`, 'success');
    
    // ACK the message from partition B
    log(`ACKing message in ${partition2} with txn=${sharedTxnId}`, 'test');
    await client.ack(messageB, true);
    
    await sleep(200);
    
    // Verify both messages are now completed
    const finalResult = await dbPool.query(`
      SELECT 
        p.name as partition_name,
        CASE
          WHEN m.id <= pc.last_consumed_id AND DATE_TRUNC('milliseconds', m.created_at) <= DATE_TRUNC('milliseconds', pc.last_consumed_created_at)
          THEN 'completed'
          ELSE 'pending'
        END as status
      FROM queen.messages m
      JOIN queen.partitions p ON p.id = m.partition_id
      JOIN queen.queues q ON q.id = p.queue_id
      LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
      WHERE q.name = $1 AND m.transaction_id = $2
      ORDER BY p.name
    `, [queue, sharedTxnId]);
    
    const finalMsgA = finalResult.rows.find(r => r.partition_name === partition1);
    const finalMsgB = finalResult.rows.find(r => r.partition_name === partition2);
    
    if (finalMsgA.status !== 'completed' || finalMsgB.status !== 'completed') {
      throw new Error('Both messages should be completed after individual ACKs');
    }
    
    log('✓ Both messages correctly ACKed independently', 'success');
    
    passTest('Partition-scoped transaction_id ACK targeting works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Batch ACK with duplicate transaction IDs across partitions
 * 
 * Tests that batch acknowledgment correctly scopes to partition_id
 */
export async function testBatchAckWithDuplicateTransactionIds(client) {
  startTest('Batch ACK with Duplicate Transaction IDs', 'edge');
  
  try {
    const queue = 'test-batch-ack-duplicate';
    const partition1 = 'partition-X';
    const partition2 = 'partition-Y';
    
    await client.queue(queue, { leaseTime: 30 });
    
    // Push 3 messages to partition1 with specific transaction IDs
    const txnIds = ['batch-1', 'batch-2', 'batch-3'];
    
    for (const txnId of txnIds) {
      await client.push(`${queue}/${partition1}`, 
        { data: `P1-${txnId}` },
        { transactionId: txnId }
      );
      
      // Push same transaction_id to partition2
      await client.push(`${queue}/${partition2}`, 
        { data: `P2-${txnId}` },
        { transactionId: txnId }
      );
    }
    
    await sleep(500); // Give more time for messages to be ready
    
    // Take all messages from partition1 (batch size 10 to get all 3)
    const messagesP1 = [];
    for await (const msg of client.take(`${queue}/${partition1}`, { batch: 10, limit: 10 })) {
      messagesP1.push(msg);
      if (messagesP1.length >= 3) break;
    }
    
    if (messagesP1.length !== 3) {
      log(`DEBUG: Only got ${messagesP1.length} messages. Transaction IDs: ${messagesP1.map(m => m.transactionId).join(', ')}`, 'warning');
      
      // Check database to see what's there
      const dbCheck = await dbPool.query(`
        SELECT m.transaction_id, p.name as partition_name, m.created_at
        FROM queen.messages m
        JOIN queen.partitions p ON p.id = m.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        WHERE q.name = $1
        ORDER BY p.name, m.created_at
      `, [queue]);
      
      log(`DEBUG: Database has ${dbCheck.rows.length} messages: ${JSON.stringify(dbCheck.rows)}`, 'warning');
      throw new Error(`Expected 3 messages from partition1, got ${messagesP1.length}`);
    }
    
    log(`Received ${messagesP1.length} messages from ${partition1}`, 'test');
    
    // Batch ACK all messages from partition1
    for (const msg of messagesP1) {
      await client.ack(msg, true);
    }
    
    await sleep(200);
    
    // Verify partition1 messages are completed but partition2 messages are still pending
    for (const txnId of txnIds) {
      const result = await dbPool.query(`
        SELECT 
          p.name as partition_name,
          CASE
            WHEN m.id <= pc.last_consumed_id AND DATE_TRUNC('milliseconds', m.created_at) <= DATE_TRUNC('milliseconds', pc.last_consumed_created_at)
            THEN 'completed'
            ELSE 'pending'
          END as status
        FROM queen.messages m
        JOIN queen.partitions p ON p.id = m.partition_id
        JOIN queen.queues q ON q.id = p.queue_id
        LEFT JOIN queen.partition_consumers pc ON pc.partition_id = p.id AND pc.consumer_group = '__QUEUE_MODE__'
        WHERE q.name = $1 AND m.transaction_id = $2
        ORDER BY p.name
      `, [queue, txnId]);
      
      const p1Status = result.rows.find(r => r.partition_name === partition1)?.status;
      const p2Status = result.rows.find(r => r.partition_name === partition2)?.status;
      
      if (p1Status !== 'completed') {
        throw new Error(`${partition1} message with txn=${txnId} should be completed, got ${p1Status}`);
      }
      
      if (p2Status !== 'pending') {
        throw new Error(`${partition2} message with txn=${txnId} should be pending, got ${p2Status}`);
      }
    }
    
    log(`✓ Batch ACK in ${partition1} did not affect ${partition2}`, 'success');
    
    passTest('Batch ACK correctly scopes by partition_id');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: DLQ with duplicate transaction IDs
 * 
 * Tests that moving messages to DLQ correctly scopes by partition_id
 */
export async function testDLQWithDuplicateTransactionIds(client) {
  startTest('DLQ with Duplicate Transaction IDs', 'edge');
  
  try {
    const queue = 'test-dlq-duplicate';
    const partition1 = 'partition-DLQ-A';
    const partition2 = 'partition-DLQ-B';
    const txnId = 'dlq-shared-txn';
    
    await client.queue(queue, { retryLimit: 0 }); // Immediate DLQ
    
    // Push same transaction_id to both partitions
    await client.push(`${queue}/${partition1}`, { data: 'P1-data' }, { transactionId: txnId });
    await client.push(`${queue}/${partition2}`, { data: 'P2-data' }, { transactionId: txnId });
    
    await sleep(100);
    
    // Take from partition1 and fail it
    let msgP1 = null;
    for await (const msg of client.take(`${queue}/${partition1}`, { limit: 1 })) {
      msgP1 = msg;
      break;
    }
    
    if (!msgP1) {
      throw new Error('Failed to take message from partition1');
    }
    
    // ACK as failed - should go to DLQ
    await client.ack(msgP1, false, { error: 'Test error for P1' });
    
    await sleep(200);
    
    // Check DLQ - only partition1 message should be there
    const dlqResult = await dbPool.query(`
      SELECT 
        p.name as partition_name,
        dlq.error_message
      FROM queen.dead_letter_queue dlq
      JOIN queen.messages m ON dlq.message_id = m.id
      JOIN queen.partitions p ON p.id = m.partition_id
      JOIN queen.queues q ON q.id = p.queue_id
      WHERE q.name = $1 AND m.transaction_id = $2
    `, [queue, txnId]);
    
    if (dlqResult.rows.length !== 1) {
      throw new Error(`Expected 1 message in DLQ, found ${dlqResult.rows.length}`);
    }
    
    if (dlqResult.rows[0].partition_name !== partition1) {
      throw new Error(`Expected DLQ message from ${partition1}, got ${dlqResult.rows[0].partition_name}`);
    }
    
    if (!dlqResult.rows[0].error_message.includes('Test error for P1')) {
      throw new Error(`Unexpected error message: ${dlqResult.rows[0].error_message}`);
    }
    
    log(`✓ DLQ correctly scoped to ${partition1}`, 'success');
    
    // Verify partition2 message is still pending
    let msgP2 = null;
    for await (const msg of client.take(`${queue}/${partition2}`, { limit: 1 })) {
      msgP2 = msg;
      break;
    }
    
    if (!msgP2) {
      throw new Error('Message in partition2 should still be available');
    }
    
    if (msgP2.transactionId !== txnId) {
      throw new Error(`Expected transaction_id ${txnId} in P2, got ${msgP2.transactionId}`);
    }
    
    log(`✓ ${partition2} message with same txn_id is still available`, 'success');
    
    passTest('DLQ correctly scopes by partition_id');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Analytics API with duplicate transaction IDs
 * 
 * Tests that the GET /api/v1/messages/:partitionId/:transactionId endpoint
 * returns the correct message when the same transaction_id exists in multiple partitions
 */
export async function testAnalyticsAPIWithDuplicateTransactionIds(client) {
  startTest('Analytics API with Duplicate Transaction IDs', 'edge');
  
  try {
    const queue = 'test-analytics-duplicate';
    const partition1 = 'analytics-P1';
    const partition2 = 'analytics-P2';
    const txnId = 'analytics-shared-txn';
    
    await client.queue(queue, {});
    
    // Push messages with same transaction_id to both partitions
    await client.push(`${queue}/${partition1}`, 
      { unique: 'data-from-P1', timestamp: Date.now() },
      { transactionId: txnId }
    );
    
    await client.push(`${queue}/${partition2}`, 
      { unique: 'data-from-P2', timestamp: Date.now() + 1000 },
      { transactionId: txnId }
    );
    
    await sleep(100);
    
    // Get partition IDs from database
    const partitionResult = await dbPool.query(`
      SELECT p.id, p.name
      FROM queen.partitions p
      JOIN queen.queues q ON q.id = p.queue_id
      WHERE q.name = $1
      ORDER BY p.name
    `, [queue]);
    
    const p1Id = partitionResult.rows.find(r => r.name === partition1)?.id;
    const p2Id = partitionResult.rows.find(r => r.name === partition2)?.id;
    
    if (!p1Id || !p2Id) {
      throw new Error('Failed to get partition IDs');
    }
    
    log(`Partition IDs: ${partition1}=${p1Id}, ${partition2}=${p2Id}`, 'test');
    
    // Fetch message using Analytics API (via HTTP)
    const baseUrl = TEST_CONFIG.baseUrls[0];
    
    // Fetch from partition1
    const response1 = await fetch(`${baseUrl}/api/v1/messages/${p1Id}/${txnId}`);
    if (!response1.ok) {
      throw new Error(`Failed to fetch message from partition1: ${response1.statusText}`);
    }
    const data1 = await response1.json();
    
    if (data1.transactionId !== txnId) {
      throw new Error(`Expected transactionId ${txnId}, got ${data1.transactionId}`);
    }
    
    if (data1.partition !== partition1) {
      throw new Error(`Expected partition ${partition1}, got ${data1.partition}`);
    }
    
    if (data1.payload.unique !== 'data-from-P1') {
      throw new Error(`Expected data from P1, got: ${JSON.stringify(data1.payload)}`);
    }
    
    log(`✓ Analytics API correctly returned message from ${partition1}`, 'success');
    
    // Fetch from partition2
    const response2 = await fetch(`${baseUrl}/api/v1/messages/${p2Id}/${txnId}`);
    if (!response2.ok) {
      throw new Error(`Failed to fetch message from partition2: ${response2.statusText}`);
    }
    const data2 = await response2.json();
    
    if (data2.transactionId !== txnId) {
      throw new Error(`Expected transactionId ${txnId}, got ${data2.transactionId}`);
    }
    
    if (data2.partition !== partition2) {
      throw new Error(`Expected partition ${partition2}, got ${data2.partition}`);
    }
    
    if (data2.payload.unique !== 'data-from-P2') {
      throw new Error(`Expected data from P2, got: ${JSON.stringify(data2.payload)}`);
    }
    
    log(`✓ Analytics API correctly returned message from ${partition2}`, 'success');
    
    passTest('Analytics API correctly distinguishes messages by partition_id');
  } catch (error) {
    failTest(error);
  }
}

