/**
 * Bus Mode Tests for Queen Message Queue
 * Tests: Consumer groups, mixed mode, subscription modes, consumer group isolation
 */

import { startTest, passTest, failTest, sleep, dbPool, log } from './utils.js';

/**
 * Test: Bus Mode - Consumer Groups
 */
export async function testBusConsumerGroups(client) {
  startTest('Bus Mode - Consumer Groups', 'enterprise');
  
  try {
    const queue = 'test-bus-mode';
    
    // Configure queue
    await client.queue(queue, {});
    
    // Push test messages
    const messages = Array.from({ length: 5 }, (_, i) => ({
      id: i + 1,
      data: `Message ${i + 1}`
    }));
    
    await client.push(queue, messages);
    await sleep(100);
    
    // Consumer Group 1: Take messages
    const group1Messages = [];
    for await (const msg of client.take(`${queue}@group1`, { limit: 3, batch: 3 })) {
      group1Messages.push(msg);
    }
    
    if (group1Messages.length !== 3) {
      throw new Error(`Group1 expected 3 messages, got ${group1Messages.length}`);
    }
    
    // Consumer Group 2: Should get the same messages
    const group2Messages = [];
    for await (const msg of client.take(`${queue}@group2`, { limit: 3, batch: 3 })) {
      group2Messages.push(msg);
    }
    
    if (group2Messages.length !== 3) {
      throw new Error(`Group2 expected 3 messages, got ${group2Messages.length}`);
    }
    
    // Verify both groups got the same message IDs
    const group1Ids = group1Messages.map(m => m.payload.id).sort();
    const group2Ids = group2Messages.map(m => m.payload.id).sort();
    
    if (JSON.stringify(group1Ids) !== JSON.stringify(group2Ids)) {
      throw new Error('Consumer groups did not receive the same messages');
    }
    
    // Acknowledge messages for both groups
    for (const msg of group1Messages) {
      await client.ack(msg, true, { group: 'group1' });
    }
    
    for (const msg of group2Messages) {
      await client.ack(msg, true, { group: 'group2' });
    }
    
    // Take remaining messages
    const group1Remaining = [];
    for await (const msg of client.take(`${queue}@group1`, { limit: 5 })) {
      group1Remaining.push(msg);
      await client.ack(msg, true, { group: 'group1' });
    }
    
    if (group1Remaining.length !== 2) {
      throw new Error(`Expected 2 remaining messages for group1, got ${group1Remaining.length}`);
    }
    
    passTest('Consumer groups receive all messages independently');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Mixed Mode - Queue and Bus Together
 */
export async function testMixedMode(client) {
  startTest('Mixed Mode - Queue and Bus Together', 'enterprise');
  
  try {
    const queue = 'test-mixed-mode';
    
    // Configure queue
    await client.queue(queue, {});
    
    // Push test messages
    const messages = Array.from({ length: 6 }, (_, i) => ({
      id: i + 1,
      data: `Mixed message ${i + 1}`
    }));
    
    await client.push(queue, messages);
    await sleep(100);
    
    // Queue Mode: Workers compete for messages
    const worker1Messages = [];
    for await (const msg of client.take(queue, { limit: 2, batch: 2 })) {
      worker1Messages.push(msg);
      await client.ack(msg);
    }
    
    const worker2Messages = [];
    for await (const msg of client.take(queue, { limit: 2, batch: 2 })) {
      worker2Messages.push(msg);
      await client.ack(msg);
    }
    
    if (!worker1Messages.length || !worker2Messages.length) {
      throw new Error('Workers did not receive messages');
    }
    
    // Workers should get different messages (competing)
    const worker1Ids = worker1Messages.map(m => m.payload.id);
    const worker2Ids = worker2Messages.map(m => m.payload.id);
    const intersection = worker1Ids.filter(id => worker2Ids.includes(id));
    
    if (intersection.length > 0) {
      throw new Error('Workers received the same messages - should be competing');
    }
    
    // Bus Mode: Consumer groups get all messages
    const analyticsMessages = [];
    for await (const msg of client.take(`${queue}@analytics`, { limit: 10 })) {
      analyticsMessages.push(msg);
      await client.ack(msg, true, { group: 'analytics' });
    }
    
    const auditMessages = [];
    for await (const msg of client.take(`${queue}@audit`, { limit: 10 })) {
      auditMessages.push(msg);
      await client.ack(msg, true, { group: 'audit' });
    }
    
    // Both groups should get all 6 messages
    if (analyticsMessages.length !== 6) {
      throw new Error(`Analytics group expected 6 messages, got ${analyticsMessages.length}`);
    }
    
    if (auditMessages.length !== 6) {
      throw new Error(`Audit group expected 6 messages, got ${auditMessages.length}`);
    }
    
    // Verify groups got all messages
    const analyticsIds = analyticsMessages.map(m => m.payload.id).sort();
    const auditIds = auditMessages.map(m => m.payload.id).sort();
    
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
 * Test: Consumer Group Subscription Modes
 */
export async function testConsumerGroupSubscriptionModes(client) {
  startTest('Consumer Group Subscription Modes', 'enterprise');
  
  try {
    const queue = 'test-subscription-modes';
    
    // Configure queue
    await client.queue(queue, {});
    
    // Push historical messages
    const historicalMessages = Array.from({ length: 3 }, (_, i) => ({
      id: i + 1,
      type: 'historical',
      data: `Historical ${i + 1}`
    }));
    
    await client.push(queue, historicalMessages);
    await sleep(100);
    
    // Group 1: Default mode (consume all)
    const allMessages = [];
    for await (const msg of client.take(`${queue}@all-messages`, { limit: 10 })) {
      allMessages.push(msg);
      // ACK immediately to release partition lock
      await client.ack(msg, true, { group: 'all-messages' });
    }
    
    if (allMessages.length !== 3) {
      throw new Error(`All-messages group expected 3 messages, got ${allMessages.length}`);
    }
    
    // Group 2: New messages only (subscription mode = 'new')
    const newOnlyMessages = [];
    for await (const msg of client.take(`${queue}@new-only`, { 
      limit: 10,
      subscriptionMode: 'new'
    })) {
      newOnlyMessages.push(msg);
    }
    
    if (newOnlyMessages.length !== 0) {
      throw new Error(`New-only group should get 0 historical messages, got ${newOnlyMessages.length}`);
    }
    
    // Wait to ensure subscription timestamp is in the past
    await sleep(1000);
    
    // Push new messages
    const newMessages = Array.from({ length: 3 }, (_, i) => ({
      id: i + 4,
      type: 'new',
      data: `New ${i + 4}`
    }));
    
    await client.push(queue, newMessages);
    await sleep(100);
    
    // All-messages group should get the new messages
    const allMessagesNew = [];
    for await (const msg of client.take(`${queue}@all-messages`, { limit: 10 })) {
      allMessagesNew.push(msg);
      // ACK immediately
      await client.ack(msg, true, { group: 'all-messages' });
    }
    
    if (allMessagesNew.length !== 3) {
      throw new Error(`All-messages group expected 3 new messages, got ${allMessagesNew.length}`);
    }
    
    // New-only group should also get the new messages
    const newOnlyNew = [];
    for await (const msg of client.take(`${queue}@new-only`, { limit: 10 })) {
      newOnlyNew.push(msg);
      await client.ack(msg, true, { group: 'new-only' });
    }
    
    if (newOnlyNew.length !== 3) {
      throw new Error(`New-only group expected 3 new messages, got ${newOnlyNew.length}`);
    }
    
    passTest('Consumer group subscription modes work correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Consumer Group Isolation
 */
export async function testConsumerGroupIsolation(client) {
  startTest('Consumer Group Isolation', 'enterprise');
  
  try {
    const queue = 'test-group-isolation';
    
    // Configure queue
    await client.queue(queue, {});
    
    // Push messages
    const messages = Array.from({ length: 4 }, (_, i) => ({
      id: i + 1,
      data: `Isolation test ${i + 1}`
    }));
    
    await client.push(queue, messages);
    await sleep(100);
    
    // Group 1: Take some messages first
    const group1Messages = [];
    for await (const msg of client.take(`${queue}@group1`, { limit: 2, batch: 2 })) {
      group1Messages.push(msg);
      await client.ack(msg, true, { group: 'group1' });
    }
    
    if (group1Messages.length !== 2) {
      throw new Error(`Group1 expected 2 messages, got ${group1Messages.length}`);
    }
    
    // Group 2: Should get ALL 4 messages (different consumer group, independent)
    // Consumer groups don't race - they each get all messages
    const group2Messages = [];
    for await (const msg of client.take(`${queue}@group2`, { limit: 10, batch: 10 })) {
      group2Messages.push(msg);
    }
    
    if (group2Messages.length !== 4) {
      log(`Group2 got ${group2Messages.length} messages, expected 4 - consumer group isolation may not be working`, 'warning');
      throw new Error(`Group2 expected all 4 messages, got ${group2Messages.length}`);
    }
    
    // Group 1: Take remaining messages
    const group1Remaining = [];
    for await (const msg of client.take(`${queue}@group1`, { limit: 10, batch: 10 })) {
      group1Remaining.push(msg);
      await client.ack(msg, true, { group: 'group1' });
    }
    
    if (group1Remaining.length !== 2) {
      throw new Error(`Group1 expected 2 remaining messages, got ${group1Remaining.length}`);
    }
    
    // Verify isolation: Fail first 2 messages for group2, complete the rest
    for (let i = 0; i < group2Messages.length; i++) {
      if (i < 2) {
        await client.ack(group2Messages[i], false, { error: 'Test failure', group: 'group2' });
      } else {
        await client.ack(group2Messages[i], true, { group: 'group2' });
      }
    }
    
    // Verify isolation: Check that group1's first message is completed for group1 but failed for group2
    const statusCheck = await dbPool.query(`
      SELECT ms.status, ms.consumer_group
      FROM queen.messages_status ms
      JOIN queen.messages m ON ms.message_id = m.id
      WHERE m.transaction_id = $1
      ORDER BY ms.consumer_group
    `, [group1Messages[0].transactionId]);
    
    const group1Status = statusCheck.rows.find(r => r.consumer_group === 'group1');
    const group2Status = statusCheck.rows.find(r => r.consumer_group === 'group2');
    
    if (group1Status?.status !== 'completed') {
      throw new Error(`Group1 status should be completed, got ${group1Status?.status}`);
    }
    
    if (group2Status?.status !== 'failed') {
      throw new Error(`Group2 status should be failed, got ${group2Status?.status}`);
    }
    
    passTest('Consumer groups are properly isolated');
  } catch (error) {
    failTest(error);
  }
}

