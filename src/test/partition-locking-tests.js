/**
 * Partition Locking Tests for Queen Message Queue
 * Tests: Partition locking in queue mode, bus mode, specific partition requests, namespace/task filtering
 */

import { startTest, passTest, failTest, sleep, dbPool, log } from './utils.js';

/**
 * Test: Partition Locking in Queue Mode
 * Tests that partitions are properly locked when consumed
 */
export async function testPartitionLocking(client) {
  startTest('Partition Locking in Queue Mode');
  
  const QUEUE_NAME = 'test-partition-lock';
  const PARTITION1 = 'partition-A';
  const PARTITION2 = 'partition-B';
  
  try {
    // Configure queue
    await client.queue(QUEUE_NAME, {});
    
    // Push 3 messages to each partition
    await client.push(`${QUEUE_NAME}/${PARTITION1}`, [
      { data: `Message 1 for ${PARTITION1}` },
      { data: `Message 2 for ${PARTITION1}` },
      { data: `Message 3 for ${PARTITION1}` }
    ]);
    
    await client.push(`${QUEUE_NAME}/${PARTITION2}`, [
      { data: `Message 1 for ${PARTITION2}` },
      { data: `Message 2 for ${PARTITION2}` },
      { data: `Message 3 for ${PARTITION2}` }
    ]);
    
    log(`Pushed 6 messages (3 to each partition)`);
    
    // Consumer 1: Take messages (will lock a partition)
    log('Consumer 1: Taking messages...');
    const consumer1Messages = [];
    for await (const msg of client.take(QUEUE_NAME, { limit: 2, batch: 2 })) {
      consumer1Messages.push(msg);
    }
    
    if (consumer1Messages.length === 0) {
      throw new Error('Consumer 1 got no messages');
    }
    
    const partition1 = consumer1Messages[0].partition;
    log(`Consumer 1: Got ${consumer1Messages.length} messages from partition: ${partition1}`);
    
    // Consumer 2: Take messages (should get different partition)
    log('Consumer 2: Taking messages...');
    const consumer2Messages = [];
    for await (const msg of client.take(QUEUE_NAME, { limit: 2, batch: 2 })) {
      consumer2Messages.push(msg);
    }
    
    if (consumer2Messages.length === 0) {
      throw new Error('Consumer 2 got no messages');
    }
    
    const partition2 = consumer2Messages[0].partition;
    log(`Consumer 2: Got ${consumer2Messages.length} messages from partition: ${partition2}`);
    
    // Verify they got different partitions
    if (partition1 === partition2) {
      throw new Error('Both consumers got the same partition - partition locking not working!');
    }
    
    log(`SUCCESS: Consumers got different partitions (${partition1} vs ${partition2})`);
    
    // Consumer 3: Try to take again (should get no messages as both partitions are locked)
    log('Consumer 3: Trying to take (both partitions should be locked)...');
    let consumer3GotMessages = false;
    for await (const msg of client.take(QUEUE_NAME, { limit: 2, batch: 2 })) {
      consumer3GotMessages = true;
    }
    
    if (consumer3GotMessages) {
      throw new Error('Consumer 3 got messages (should have been 0 - both partitions locked)');
    }
    
    log('Consumer 3: Got no messages (correct - both partitions are locked)');
    
    // ACK messages from Consumer 1 to release its partition
    log('Consumer 1: ACKing messages to release partition...');
    for (const msg of consumer1Messages) {
      await client.ack(msg);
    }
    
    // Consumer 4: Should now be able to get remaining message from Consumer 1's partition
    log('Consumer 4: Taking after Consumer 1 released...');
    const consumer4Messages = [];
    for await (const msg of client.take(QUEUE_NAME, { limit: 2, batch: 2 })) {
      consumer4Messages.push(msg);
      await client.ack(msg);
    }
    
    if (consumer4Messages.length > 0) {
      const partition4 = consumer4Messages[0].partition;
      log(`Consumer 4: Got ${consumer4Messages.length} messages from partition: ${partition4}`);
      
      // Should be from the same partition as Consumer 1 had
      if (partition4 !== partition1) {
        throw new Error('Consumer 4 did not get messages from released partition');
      }
    }
    
    // Clean up: ACK all remaining messages
    for (const msg of consumer2Messages) {
      await client.ack(msg);
    }
    
    passTest('Partition locking works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Partition Locking in Bus Mode
 * Tests that partitions are properly locked per consumer group
 */
export async function testBusPartitionLocking(client) {
  startTest('Partition Locking in Bus Mode');
  
  const QUEUE_NAME = 'test-bus-partition-lock';
  const PARTITION1 = 'partition-X';
  const PARTITION2 = 'partition-Y';
  const CONSUMER_GROUP1 = 'group-alpha';
  const CONSUMER_GROUP2 = 'group-beta';
  
  try {
    // Configure queue
    await client.queue(QUEUE_NAME, {});
    
    // Push 3 messages to each partition
    await client.push(`${QUEUE_NAME}/${PARTITION1}`, [
      { data: `Message 1 for ${PARTITION1}` },
      { data: `Message 2 for ${PARTITION1}` },
      { data: `Message 3 for ${PARTITION1}` }
    ]);
    
    await client.push(`${QUEUE_NAME}/${PARTITION2}`, [
      { data: `Message 1 for ${PARTITION2}` },
      { data: `Message 2 for ${PARTITION2}` },
      { data: `Message 3 for ${PARTITION2}` }
    ]);
    
    log(`Pushed 6 messages (3 to each partition)`);
    
    // Group Alpha, Consumer 1: Take messages
    log('Group Alpha, Consumer 1: Taking messages...');
    const group1Consumer1Messages = [];
    for await (const msg of client.take(`${QUEUE_NAME}@${CONSUMER_GROUP1}`, { limit: 2, batch: 2 })) {
      group1Consumer1Messages.push(msg);
    }
    
    if (group1Consumer1Messages.length === 0) {
      throw new Error('Group Alpha Consumer 1 got no messages');
    }
    
    const partition1 = group1Consumer1Messages[0].partition;
    log(`Group Alpha Consumer 1: Got ${group1Consumer1Messages.length} messages from partition: ${partition1}`);
    
    // Group Alpha, Consumer 2: Take messages (should get different partition if locking works)
    log('Group Alpha, Consumer 2: Taking messages...');
    const group1Consumer2Messages = [];
    for await (const msg of client.take(`${QUEUE_NAME}@${CONSUMER_GROUP1}`, { limit: 2, batch: 2 })) {
      group1Consumer2Messages.push(msg);
    }
    
    if (group1Consumer2Messages.length === 0) {
      log('Group Alpha Consumer 2 got no messages (partition locking might be working)');
    } else {
      const partition2 = group1Consumer2Messages[0].partition;
      log(`Group Alpha Consumer 2: Got ${group1Consumer2Messages.length} messages from partition: ${partition2}`);
      
      if (partition1 === partition2) {
        log('WARNING: Both consumers in same group got same partition!');
      } else {
        log('Consumers in same group got different partitions');
      }
    }
    
    // Group Beta, Consumer 1: Should be able to get messages from any partition (different consumer group)
    log('Group Beta, Consumer 1: Taking messages (different consumer group)...');
    const group2Consumer1Messages = [];
    for await (const msg of client.take(`${QUEUE_NAME}@${CONSUMER_GROUP2}`, { limit: 2, batch: 2 })) {
      group2Consumer1Messages.push(msg);
    }
    
    if (group2Consumer1Messages.length > 0) {
      const partition3 = group2Consumer1Messages[0].partition;
      log(`Group Beta Consumer 1: Got ${group2Consumer1Messages.length} messages from partition: ${partition3}`);
      log('Different consumer groups can access same partitions independently (correct)');
    }
    
    // Clean up: ACK all messages
    for (const msg of group1Consumer1Messages) {
      await client.ack(msg, true, { group: CONSUMER_GROUP1 });
    }
    for (const msg of group1Consumer2Messages) {
      await client.ack(msg, true, { group: CONSUMER_GROUP1 });
    }
    for (const msg of group2Consumer1Messages) {
      await client.ack(msg, true, { group: CONSUMER_GROUP2 });
    }
    
    passTest('Bus mode partition locking test completed');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Specific Partition Request with Locking
 * Tests that requesting a specific partition respects locking
 */
export async function testSpecificPartitionLocking(client) {
  startTest('Specific Partition Request with Locking');
  
  const QUEUE_NAME = 'test-specific-partition';
  const PARTITION_NAME = 'specific-partition';
  const CONSUMER_GROUP = 'test-group';
  
  try {
    // Configure queue
    await client.queue(QUEUE_NAME, {});
    
    // Part 1: Test Queue Mode
    log('=== Testing Queue Mode with Specific Partition ===');
    
    // Push test messages
    await client.push(`${QUEUE_NAME}/${PARTITION_NAME}`, [
      { data: 'Message 1' },
      { data: 'Message 2' },
      { data: 'Message 3' },
      { data: 'Message 4' },
      { data: 'Message 5' }
    ]);
    
    log(`Pushed 5 messages to ${PARTITION_NAME}`);
    
    // Consumer 1: Request specific partition
    log('Consumer 1: Requesting specific partition...');
    const consumer1Messages = [];
    for await (const msg of client.take(`${QUEUE_NAME}/${PARTITION_NAME}`, { limit: 2, batch: 2 })) {
      consumer1Messages.push(msg);
    }
    
    if (consumer1Messages.length === 0) {
      throw new Error('Consumer 1 got no messages');
    }
    
    log(`Consumer 1: Got ${consumer1Messages.length} messages from partition: ${consumer1Messages[0].partition}`);
    
    // Consumer 2: Try to request same specific partition (should get nothing due to lock)
    log('Consumer 2: Requesting same specific partition...');
    let consumer2GotMessages = false;
    for await (const msg of client.take(`${QUEUE_NAME}/${PARTITION_NAME}`, { limit: 2, batch: 2 })) {
      consumer2GotMessages = true;
    }
    
    if (consumer2GotMessages) {
      throw new Error('Consumer 2 got messages (should have been 0 due to lock)');
    }
    
    log('Consumer 2: Got no messages (correct - partition is locked by Consumer 1)');
    
    // ACK messages from Consumer 1
    for (const msg of consumer1Messages) {
      await client.ack(msg);
    }
    
    // Consumer 3: Should now get messages from the specific partition
    log('Consumer 3: Requesting specific partition after release...');
    const consumer3Messages = [];
    for await (const msg of client.take(`${QUEUE_NAME}/${PARTITION_NAME}`, { limit: 2, batch: 2 })) {
      consumer3Messages.push(msg);
      await client.ack(msg);
    }
    
    if (consumer3Messages.length === 0) {
      throw new Error('Consumer 3 got no messages (should have gotten remaining messages)');
    }
    
    log(`Consumer 3: Got ${consumer3Messages.length} messages after lease release`);
    
    // Part 2: Test Bus Mode
    log('=== Testing Bus Mode with Specific Partition ===');
    
    // Push fresh messages
    await client.push(`${QUEUE_NAME}/${PARTITION_NAME}`, [
      { data: 'Bus Message 1' },
      { data: 'Bus Message 2' },
      { data: 'Bus Message 3' },
      { data: 'Bus Message 4' },
      { data: 'Bus Message 5' }
    ]);
    
    // Group Alpha, Consumer 1: Request specific partition
    log('Group Alpha, Consumer 1: Requesting specific partition...');
    const busGroup1Messages = [];
    for await (const msg of client.take(`${QUEUE_NAME}/${PARTITION_NAME}@${CONSUMER_GROUP}`, { limit: 2, batch: 2 })) {
      busGroup1Messages.push(msg);
    }
    
    if (busGroup1Messages.length === 0) {
      throw new Error('Group Alpha Consumer 1 got no messages');
    }
    
    log(`Group Alpha Consumer 1: Got ${busGroup1Messages.length} messages`);
    
    // Group Alpha, Consumer 2: Try same specific partition (should be locked)
    log('Group Alpha, Consumer 2: Requesting same specific partition...');
    let busGroup1Consumer2GotMessages = false;
    for await (const msg of client.take(`${QUEUE_NAME}/${PARTITION_NAME}@${CONSUMER_GROUP}`, { limit: 2, batch: 2 })) {
      busGroup1Consumer2GotMessages = true;
    }
    
    if (busGroup1Consumer2GotMessages) {
      throw new Error('Group Alpha Consumer 2: Got messages (should be 0)');
    }
    
    log('Group Alpha Consumer 2: Got no messages (correct - partition locked by Consumer 1)');
    
    // Different Group, Consumer 1: Should get messages (different consumer group)
    log('Group Beta, Consumer 1: Requesting same partition (different group)...');
    const busGroup2Messages = [];
    for await (const msg of client.take(`${QUEUE_NAME}/${PARTITION_NAME}@group-beta`, { limit: 2, batch: 2 })) {
      busGroup2Messages.push(msg);
    }
    
    if (busGroup2Messages.length === 0) {
      throw new Error('Group Beta should be able to access partition (different group)');
    }
    
    log(`Group Beta Consumer 1: Got ${busGroup2Messages.length} messages (different group works)`);
    
    // Clean up
    for (const msg of busGroup1Messages) {
      await client.ack(msg, true, { group: CONSUMER_GROUP });
    }
    for (const msg of busGroup2Messages) {
      await client.ack(msg, true, { group: 'group-beta' });
    }
    
    passTest('Specific partition locking works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Namespace/Task Filtering with Partition Locking
 * Tests that namespace/task filtering properly locks partitions
 */
export async function testNamespaceTaskFiltering(client) {
  startTest('Namespace/Task Filtering with Partition Locking');
  
  try {
    const namespace = 'test-namespace';
    const queues = [
      { name: 'test-queue-task-1', task: 'process', partition: 'p1' },
      { name: 'test-queue-task-2', task: 'process', partition: 'p2' },
      { name: 'test-queue-task-3', task: 'analyze', partition: 'p3' }
    ];
    
    // Configure queues with namespace and task
    for (const q of queues) {
      await client.queue(q.name, { leaseTime: 30 }, {
        namespace: namespace,
        task: q.task
      });
      
      // Push messages to each queue
      await client.push(`${q.name}/${q.partition}`, [
        { data: `Message for ${q.name}` },
        { data: `Message 2 for ${q.name}` }
      ]);
    }
    
    log('Pushed messages to 3 queues with namespace/task metadata');
    
    // Test 1: Take by namespace only (should get messages from all queues)
    log('Consumer 1: Taking by namespace only...');
    const consumer1Messages = [];
    for await (const msg of client.take('namespace:test-namespace', { limit: 3, batch: 3 })) {
      consumer1Messages.push(msg);
    }
    
    if (consumer1Messages.length === 0) {
      throw new Error('Consumer 1 got no messages');
    }
    
    log(`Consumer 1: Got ${consumer1Messages.length} messages from namespace ${namespace}`);
    const partitions1 = [...new Set(consumer1Messages.map(m => m.partition))];
    log(`Consumer 1: Locked partitions: ${partitions1.join(', ')}`);
    
    // Test 2: Another consumer tries to take from same namespace (should get from unlocked partitions only)
    log('Consumer 2: Trying to take from same namespace...');
    const consumer2Messages = [];
    for await (const msg of client.take('namespace:test-namespace', { limit: 3, batch: 3 })) {
      consumer2Messages.push(msg);
    }
    
    if (consumer2Messages.length > 0) {
      const partitions2 = [...new Set(consumer2Messages.map(m => m.partition))];
      log(`Consumer 2: Got ${consumer2Messages.length} messages from partitions: ${partitions2.join(', ')}`);
      
      // Verify no overlap in partitions
      const overlap = partitions1.filter(p => partitions2.includes(p));
      if (overlap.length > 0) {
        throw new Error(`Partition locking failed! Both consumers got partition(s): ${overlap.join(', ')}`);
      }
      log('SUCCESS: No partition overlap between consumers');
    } else {
      log('Consumer 2: Got no messages (all partitions locked)');
    }
    
    // Test 3: Take by specific task (should only get from matching queues)
    log('Consumer 3: Taking by specific task "analyze"...');
    const consumer3Messages = [];
    for await (const msg of client.take('namespace:test-namespace/task:analyze', { limit: 2 })) {
      consumer3Messages.push(msg);
    }
    
    if (consumer3Messages.length > 0) {
      log(`Consumer 3: Got ${consumer3Messages.length} messages for task "analyze"`);
      // Verify all messages are from the analyze task
      const allAnalyze = consumer3Messages.every(m => m.queue === 'test-queue-task-3');
      if (!allAnalyze) {
        throw new Error('Got messages from wrong task!');
      }
    }
    
    // Clean up: ACK all messages
    for (const msg of [...consumer1Messages, ...consumer2Messages, ...consumer3Messages]) {
      await client.ack(msg);
    }
    
    passTest('Namespace/Task filtering with partition locking works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Namespace/Task Filtering in Bus Mode
 * Tests that namespace/task filtering works with consumer groups
 */
export async function testNamespaceTaskBusMode(client) {
  startTest('Namespace/Task Filtering in Bus Mode');
  
  try {
    const namespace = 'bus-namespace';
    const task = 'bus-task';
    const queues = [
      { name: 'test-bus-queue-1', partition: 'bus-p1' },
      { name: 'test-bus-queue-2', partition: 'bus-p2' }
    ];
    
    // Setup queues and push messages
    for (const q of queues) {
      await client.queue(q.name, { leaseTime: 30 }, {
        namespace: namespace,
        task: task
      });
      
      await client.push(`${q.name}/${q.partition}`, [
        { data: `Bus message for ${q.name}` },
        { data: `Bus message 2 for ${q.name}` }
      ]);
    }
    
    log('Pushed messages to 2 queues for bus mode testing');
    
    // Test 1: Consumer Group A takes messages
    log('Group A, Consumer 1: Taking with namespace/task filter...');
    const groupAMessages = [];
    for await (const msg of client.take('namespace:bus-namespace/task:bus-task@group-a', { limit: 2, batch: 2 })) {
      groupAMessages.push(msg);
    }
    
    if (groupAMessages.length === 0) {
      throw new Error('Group A Consumer 1 got no messages');
    }
    
    const partitionsA1 = [...new Set(groupAMessages.map(m => m.partition))];
    log(`Group A Consumer 1: Got ${groupAMessages.length} messages from partitions: ${partitionsA1.join(', ')}`);
    
    // Test 2: Another consumer in Group A (should get from different partitions due to locking)
    log('Group A, Consumer 2: Trying same namespace/task filter...');
    const groupA2Messages = [];
    for await (const msg of client.take('namespace:bus-namespace/task:bus-task@group-a', { limit: 2, batch: 2 })) {
      groupA2Messages.push(msg);
    }
    
    if (groupA2Messages.length > 0) {
      const partitionsA2 = [...new Set(groupA2Messages.map(m => m.partition))];
      log(`Group A Consumer 2: Got ${groupA2Messages.length} messages from partitions: ${partitionsA2.join(', ')}`);
      
      // Check for partition overlap
      const overlapA = partitionsA1.filter(p => partitionsA2.includes(p));
      if (overlapA.length > 0) {
        log(`WARNING: Partition overlap in same group: ${overlapA.join(', ')}`);
      }
    } else {
      log('Group A Consumer 2: No messages (partitions locked by Consumer 1)');
    }
    
    // Test 3: Consumer Group B (should be independent)
    log('Group B, Consumer 1: Taking with same namespace/task filter...');
    const groupBMessages = [];
    for await (const msg of client.take('namespace:bus-namespace/task:bus-task@group-b', { limit: 2, batch: 2 })) {
      groupBMessages.push(msg);
    }
    
    if (groupBMessages.length > 0) {
      const partitionsB1 = [...new Set(groupBMessages.map(m => m.partition))];
      log(`Group B Consumer 1: Got ${groupBMessages.length} messages from partitions: ${partitionsB1.join(', ')}`);
      log('SUCCESS: Different consumer groups can access same partitions independently');
    } else {
      throw new Error('Group B should be able to get messages (different consumer group)');
    }
    
    // Clean up: ACK all messages
    for (const msg of groupAMessages) {
      await client.ack(msg, true, { group: 'group-a' });
    }
    for (const msg of groupA2Messages) {
      await client.ack(msg, true, { group: 'group-a' });
    }
    for (const msg of groupBMessages) {
      await client.ack(msg, true, { group: 'group-b' });
    }
    
    passTest('Namespace/Task filtering in bus mode works correctly');
  } catch (error) {
    failTest(error);
  }
}

