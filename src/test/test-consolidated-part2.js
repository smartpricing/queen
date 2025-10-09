// ============================================
// EDGE CASE TESTS
// ============================================

/**
 * Test: Empty and null payloads
 */
async function testEmptyAndNullPayloads() {
  startTest('Empty and Null Payloads', 'edge');
  
  try {
    const queue = 'edge-empty-payloads';
    
    // Test null payload
    await client.push({
      items: [{
        queue,
        payload: null
      }]
    });
    
    // Test undefined payload (should be treated as null)
    await client.push({
      items: [{
        queue
        // payload is undefined
      }]
    });
    
    // Test empty object
    await client.push({
      items: [{
        queue,
        payload: {}
      }]
    });
    
    // Test empty string
    await client.push({
      items: [{
        queue,
        payload: ''
      }]
    });
    
    // Pop and verify all messages
    const result = await client.pop({
      queue,
      batch: 10
    });
    
    if (!result.messages || result.messages.length !== 4) {
      throw new Error(`Expected 4 messages, got ${result.messages?.length}`);
    }
    
    // Verify payloads
    const payloads = result.messages.map(m => m.data);
    
    // Check for null (first two should be null)
    if (payloads[0] !== null) {
      throw new Error(`Expected null for first payload, got ${JSON.stringify(payloads[0])}`);
    }
    
    if (payloads[1] !== null) {
      throw new Error(`Expected null for second payload, got ${JSON.stringify(payloads[1])}`);
    }
    
    // Empty object
    if (typeof payloads[2] !== 'object' || Object.keys(payloads[2]).length !== 0) {
      throw new Error(`Expected empty object, got ${JSON.stringify(payloads[2])}`);
    }
    
    // Empty string
    if (payloads[3] !== '') {
      throw new Error(`Expected empty string, got ${JSON.stringify(payloads[3])}`);
    }
    
    // Acknowledge all
    for (const msg of result.messages) {
      await client.ack(msg.transactionId, 'completed');
    }
    
    passTest('Empty and null payloads handled correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Very large payloads
 */
async function testVeryLargePayloads() {
  startTest('Very Large Payloads', 'edge');
  
  try {
    const queue = 'edge-large-payloads';
    
    // Create a large payload (1MB of data)
    const largeArray = new Array(10000).fill({
      id: crypto.randomBytes(16).toString('hex'),
      data: 'x'.repeat(100),
      nested: {
        field1: 'value1',
        field2: 'value2',
        field3: 'value3'
      }
    });
    
    const largePayload = {
      array: largeArray,
      metadata: {
        size: JSON.stringify(largeArray).length,
        timestamp: Date.now()
      }
    };
    
    // Push large message
    await client.push({
      items: [{
        queue,
        payload: largePayload
      }]
    });
    
    // Pop and verify
    const result = await client.pop({
      queue,
      batch: 1
    });
    
    if (!result.messages || result.messages.length !== 1) {
      throw new Error('Failed to pop large message');
    }
    
    const received = result.messages[0].data;
    if (!received.array || received.array.length !== 10000) {
      throw new Error('Large payload corrupted');
    }
    
    await client.ack(result.messages[0].transactionId, 'completed');
    
    passTest('Very large payloads handled correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Concurrent push operations
 */
async function testConcurrentPushOperations() {
  startTest('Concurrent Push Operations', 'edge');
  
  try {
    const queue = 'edge-concurrent-push';
    const concurrentPushes = 10;
    const messagesPerPush = 5;
    
    // Create multiple push promises
    const pushPromises = [];
    for (let i = 0; i < concurrentPushes; i++) {
      const items = Array.from({ length: messagesPerPush }, (_, j) => ({
        queue,
        payload: {
          pushBatch: i,
          messageIndex: j,
          timestamp: Date.now()
        }
      }));
      
      pushPromises.push(client.push({ items }));
    }
    
    // Execute all pushes concurrently
    const results = await Promise.all(pushPromises);
    
    // Verify all pushes succeeded
    for (let i = 0; i < results.length; i++) {
      if (!results[i].messages || results[i].messages.length !== messagesPerPush) {
        throw new Error(`Push batch ${i} failed`);
      }
    }
    
    // Verify total message count
    const totalExpected = concurrentPushes * messagesPerPush;
    const count = await getMessageCount(queue);
    
    if (count !== totalExpected) {
      throw new Error(`Expected ${totalExpected} messages, got ${count}`);
    }
    
    passTest(`${concurrentPushes} concurrent pushes completed successfully`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Concurrent pop operations
 */
async function testConcurrentPopOperations() {
  startTest('Concurrent Pop Operations', 'edge');
  
  try {
    const queue = 'edge-concurrent-pop';
    const totalMessages = 50;
    const concurrentPops = 10;
    
    // Push messages
    await client.push({
      items: Array.from({ length: totalMessages }, (_, i) => ({
        queue,
        payload: { id: i, data: `Message ${i}` }
      }))
    });
    
    await sleep(100);
    
    // Create multiple pop promises
    const popPromises = [];
    for (let i = 0; i < concurrentPops; i++) {
      popPromises.push(client.pop({
        queue,
        batch: Math.ceil(totalMessages / concurrentPops)
      }));
    }
    
    // Execute all pops concurrently
    const results = await Promise.all(popPromises);
    
    // Collect all messages
    const allMessages = [];
    for (const result of results) {
      if (result.messages) {
        allMessages.push(...result.messages);
      }
    }
    
    // Verify no duplicate messages
    const transactionIds = allMessages.map(m => m.transactionId);
    const uniqueIds = new Set(transactionIds);
    
    if (uniqueIds.size !== allMessages.length) {
      throw new Error('Duplicate messages detected in concurrent pops');
    }
    
    // Verify we got all messages
    if (allMessages.length !== totalMessages) {
      throw new Error(`Expected ${totalMessages} messages, got ${allMessages.length}`);
    }
    
    passTest(`${concurrentPops} concurrent pops handled correctly`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Many consumer groups on same queue
 */
async function testManyConsumerGroups() {
  startTest('Many Consumer Groups on Same Queue', 'edge');
  
  try {
    const queue = 'edge-many-groups';
    const numGroups = 10;
    const numMessages = 5;
    
    // Push messages
    await client.push({
      items: Array.from({ length: numMessages }, (_, i) => ({
        queue,
        payload: { id: i, data: `Broadcast message ${i}` }
      }))
    });
    
    await sleep(100);
    
    // Each consumer group should get all messages
    const groupResults = [];
    for (let g = 0; g < numGroups; g++) {
      const result = await client.pop({
        queue,
        consumerGroup: `group-${g}`,
        batch: numMessages
      });
      
      if (!result.messages || result.messages.length !== numMessages) {
        throw new Error(`Group ${g} didn't receive all messages`);
      }
      
      groupResults.push(result);
    }
    
    // Verify all groups got the same messages
    const firstGroupIds = groupResults[0].messages.map(m => m.payload.id).sort();
    
    for (let g = 1; g < numGroups; g++) {
      const groupIds = groupResults[g].messages.map(m => m.payload.id).sort();
      if (JSON.stringify(groupIds) !== JSON.stringify(firstGroupIds)) {
        throw new Error(`Group ${g} got different messages`);
      }
    }
    
    passTest(`${numGroups} consumer groups all received all messages`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Retry limit exhaustion
 */
async function testRetryLimitExhaustion() {
  startTest('Retry Limit Exhaustion', 'edge');
  
  try {
    const queue = 'edge-retry-exhaustion';
    
    // Configure with low retry limit and DLQ
    await client.configure({
      queue,
      options: {
        retryLimit: 2,
        leaseTime: 1, // Very short lease
        dlqAfterMaxRetries: true
      }
    });
    
    // Push a message
    await client.push({
      items: [{
        queue,
        payload: { test: 'retry exhaustion' }
      }]
    });
    
    await sleep(100);
    
    // Pop and fail multiple times
    for (let i = 0; i < 3; i++) {
      const result = await client.pop({ queue, batch: 1 });
      
      if (result.messages && result.messages.length > 0) {
        await client.ack(result.messages[0].transactionId, 'failed', `Attempt ${i + 1}`);
        await sleep(1500); // Wait for lease to expire
      } else {
        // No message available - it's been moved to DLQ
        break;
      }
    }
    
    // Try to pop again - should get nothing
    const finalPop = await client.pop({ queue, batch: 1 });
    
    if (finalPop.messages && finalPop.messages.length > 0) {
      throw new Error('Message still available after retry exhaustion');
    }
    
    log('Message no longer available after retry exhaustion', 'info');
    passTest('Retry limit exhaustion handled correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Lease expiration and redelivery
 */
async function testLeaseExpiration() {
  startTest('Lease Expiration and Redelivery', 'edge');
  
  try {
    const queue = 'edge-lease-expiration';
    
    // Configure with short lease time
    await client.configure({
      queue,
      options: {
        leaseTime: 2 // 2 seconds
      }
    });
    
    // Push a message
    const pushResult = await client.push({
      items: [{
        queue,
        payload: { test: 'lease expiration' }
      }]
    });
    
    const originalTransactionId = pushResult.messages[0].transactionId;
    
    // Pop the message (acquires lease)
    const firstPop = await client.pop({ queue, batch: 1 });
    
    if (!firstPop.messages || firstPop.messages.length !== 1) {
      throw new Error('Failed to pop message');
    }
    
    const firstTransactionId = firstPop.messages[0].transactionId;
    
    // Don't acknowledge - let lease expire
    // Wait for lease to expire (2 seconds + buffer)
    await sleep(3000);
    
    // Add a small delay to ensure server processes expiration
    await sleep(100);
    
    // Try to pop again - should get the same message
    const secondPop = await client.pop({ queue, batch: 1 });
    
    if (!secondPop.messages || secondPop.messages.length !== 1) {
      throw new Error('Message not redelivered after lease expiration');
    }
    
    const secondTransactionId = secondPop.messages[0].transactionId;
    
    // Transaction ID should be the same - it's the same message being redelivered
    if (firstTransactionId !== secondTransactionId) {
      throw new Error('Transaction ID changed unexpectedly - should be same message');
    }
    
    // Verify it's the same payload
    if (JSON.stringify(secondPop.messages[0].payload) !== JSON.stringify({ test: 'lease expiration' })) {
      throw new Error('Redelivered message has different payload');
    }
    
    // Acknowledge this time
    await client.ack(secondPop.messages[0].transactionId, 'completed');
    
    passTest('Lease expiration and redelivery works correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: SQL injection prevention
 */
async function testSQLInjectionPrevention() {
  startTest('SQL Injection Prevention', 'edge');
  
  try {
    // Try various SQL injection attempts
    const injectionAttempts = [
      "'; DROP TABLE queen.messages; --",
      "1' OR '1'='1",
      "admin'--",
      "' UNION SELECT * FROM queen.queues--",
      "'; UPDATE queen.messages SET payload = 'hacked'--"
    ];
    
    for (const attempt of injectionAttempts) {
      // Try injection in queue name
      try {
        await client.push({
          items: [{
            queue: attempt,
            payload: { test: 'sql injection' }
          }]
        });
      } catch (error) {
        // Expected to fail or be sanitized
      }
      
      // Try injection in partition name
      try {
        await client.push({
          items: [{
            queue: 'edge-sql-test',
            partition: attempt,
            payload: { test: 'sql injection' }
          }]
        });
      } catch (error) {
        // Expected to fail or be sanitized
      }
      
      // Try injection in payload
      await client.push({
        items: [{
          queue: 'edge-sql-test',
          payload: { 
            injection: attempt,
            nested: {
              attempt: attempt
            }
          }
        }]
      });
    }
    
    // Verify database is still intact
    const tableCheck = await dbPool.query(`
      SELECT COUNT(*) as count 
      FROM information_schema.tables 
      WHERE table_schema = 'queen' AND table_name = 'messages'
    `);
    
    if (tableCheck.rows[0].count !== '1') {
      throw new Error('Database structure compromised!');
    }
    
    passTest('SQL injection attempts properly prevented');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: XSS prevention in payloads
 */
async function testXSSPrevention() {
  startTest('XSS Prevention in Payloads', 'edge');
  
  try {
    const queue = 'edge-xss-test';
    
    // Various XSS attempts
    const xssPayloads = [
      '<script>alert("XSS")</script>',
      '<img src=x onerror=alert("XSS")>',
      'javascript:alert("XSS")',
      '<svg onload=alert("XSS")>',
      '"><script>alert("XSS")</script>'
    ];
    
    // Push messages with XSS attempts
    await client.push({
      items: xssPayloads.map(xss => ({
        queue,
        payload: {
          userInput: xss,
          html: xss,
          nested: {
            script: xss
          }
        }
      }))
    });
    
    // Pop and verify payloads are intact (not sanitized at storage level)
    const result = await client.pop({
      queue,
      batch: xssPayloads.length
    });
    
    if (!result.messages || result.messages.length !== xssPayloads.length) {
      throw new Error('Failed to retrieve XSS test messages');
    }
    
    // Verify payloads are stored as-is (sanitization should happen at display)
    for (let i = 0; i < result.messages.length; i++) {
      const msg = result.messages[i];
      if (msg.data.userInput !== xssPayloads[i]) {
        throw new Error('Payload was modified during storage');
      }
    }
    
    passTest('XSS payloads handled safely (stored as-is for application-level handling)');
  } catch (error) {
    failTest(error);
  }
}

// ============================================
// ADVANCED SCENARIO TESTS
// ============================================

/**
 * Test: Multi-stage pipeline workflow
 */
async function testMultiStagePipeline() {
  startTest('Multi-Stage Pipeline Workflow', 'workflow');
  
  try {
    const stages = ['ingestion', 'validation', 'processing', 'enrichment', 'delivery'];
    const itemCount = 10;
    
    // Configure queues for each stage
    for (const stage of stages) {
      await client.configure({
        queue: `workflow-${stage}`,
        options: {
          priority: stages.indexOf(stage) + 1
        }
      });
    }
    
    // Push initial items to ingestion
    const startTime = performance.now();
    await client.push({
      items: Array.from({ length: itemCount }, (_, i) => ({
        queue: 'workflow-ingestion',
        payload: {
          id: i,
          data: `Item ${i}`,
          stage: 'ingestion',
          timestamp: Date.now()
        }
      }))
    });
    
    // Process through each stage
    for (let stageIndex = 0; stageIndex < stages.length - 1; stageIndex++) {
      const currentStage = stages[stageIndex];
      const nextStage = stages[stageIndex + 1];
      
      // Process current stage
      const result = await client.pop({
        queue: `workflow-${currentStage}`,
        batch: itemCount
      });
      
      if (result.messages && result.messages.length > 0) {
        // Process and move to next stage
        const nextItems = result.messages.map(msg => ({
          queue: `workflow-${nextStage}`,
          payload: {
            ...msg.data,
            stage: nextStage,
            previousStage: currentStage,
            processingTime: Date.now() - msg.data.timestamp
          }
        }));
        
        await client.push({ items: nextItems });
        
        // Acknowledge current stage
        for (const msg of result.messages) {
          await client.ack(msg.transactionId, 'completed');
        }
      }
    }
    
    // Verify final delivery
    const finalResult = await client.pop({
      queue: 'workflow-delivery',
      batch: itemCount
    });
    
    if (!finalResult.messages || finalResult.messages.length !== itemCount) {
      throw new Error(`Pipeline incomplete: expected ${itemCount} items in delivery`);
    }
    
    const totalTime = performance.now() - startTime;
    const avgTime = Math.round(totalTime / itemCount);
    
    passTest(`Pipeline processed ${itemCount} items through ${stages.length} stages (avg ${avgTime}ms)`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Fan-out/Fan-in pattern
 */
async function testFanOutFanIn() {
  startTest('Fan-out/Fan-in Pattern', 'pattern');
  
  try {
    const masterQueue = 'pattern-master';
    const workerQueues = ['pattern-worker-1', 'pattern-worker-2', 'pattern-worker-3'];
    const aggregatorQueue = 'pattern-aggregator';
    
    // Configure queues
    await client.configure({ queue: masterQueue });
    for (const worker of workerQueues) {
      await client.configure({ queue: worker });
    }
    await client.configure({ queue: aggregatorQueue });
    
    // Push master task
    const masterTask = {
      id: crypto.randomBytes(16).toString('hex'),
      totalWork: 9,
      data: Array.from({ length: 9 }, (_, i) => `Task ${i}`)
    };
    
    await client.push({
      items: [{
        queue: masterQueue,
        payload: masterTask
      }]
    });
    
    // Pop master task
    const masterResult = await client.pop({ queue: masterQueue, batch: 1 });
    if (!masterResult.messages || masterResult.messages.length !== 1) {
      throw new Error('Failed to get master task');
    }
    
    // Fan-out to workers
    const tasksPerWorker = Math.ceil(masterTask.totalWork / workerQueues.length);
    for (let i = 0; i < workerQueues.length; i++) {
      const startIdx = i * tasksPerWorker;
      const endIdx = Math.min(startIdx + tasksPerWorker, masterTask.totalWork);
      const workerTasks = masterTask.data.slice(startIdx, endIdx);
      
      await client.push({
        items: [{
          queue: workerQueues[i],
          payload: {
            masterId: masterTask.id,
            workerIndex: i,
            tasks: workerTasks
          }
        }]
      });
    }
    
    await client.ack(masterResult.messages[0].transactionId, 'completed');
    
    // Workers process and fan-in to aggregator
    const workerResults = [];
    for (const workerQueue of workerQueues) {
      const result = await client.pop({ queue: workerQueue, batch: 1 });
      if (result.messages && result.messages.length > 0) {
        const msg = result.messages[0];
        
        // Simulate processing
        const processed = {
          masterId: msg.data.masterId,
          workerIndex: msg.data.workerIndex,
          results: msg.data.tasks.map(t => `Processed: ${t}`)
        };
        
        workerResults.push(processed);
        
        // Send to aggregator
        await client.push({
          items: [{
            queue: aggregatorQueue,
            payload: processed
          }]
        });
        
        await client.ack(msg.transactionId, 'completed');
      }
    }
    
    // Aggregate results
    const aggregatedResults = [];
    for (let i = 0; i < workerQueues.length; i++) {
      const result = await client.pop({ queue: aggregatorQueue, batch: 1 });
      if (result.messages && result.messages.length > 0) {
        aggregatedResults.push(result.messages[0].data);
        await client.ack(result.messages[0].transactionId, 'completed');
      }
    }
    
    // Verify all tasks were processed
    const totalProcessed = aggregatedResults.reduce((sum, r) => sum + r.results.length, 0);
    if (totalProcessed !== masterTask.totalWork) {
      throw new Error(`Expected ${masterTask.totalWork} tasks processed, got ${totalProcessed}`);
    }
    
    passTest(`Fan-out/Fan-in: ${masterTask.totalWork} tasks distributed to ${workerQueues.length} workers and aggregated`);
  } catch (error) {
    failTest(error);
  }
}

// ============================================
// MAIN TEST RUNNER
// ============================================

async function runAllTests() {
  console.log('🚀 Starting Complete Queen Message Queue Test Suite');
  console.log('   Including Core, Enterprise, Bus Mode, Edge Cases, and Advanced Scenarios\n');
  console.log('=' .repeat(80));
  
  try {
    // Initialize client and database connection
    client = createQueenClient({ baseUrl: TEST_CONFIG.baseUrl });
    dbPool = new pg.Pool(TEST_CONFIG.dbConfig);
    
    // Test database connection
    await dbPool.query('SELECT 1');
    log('Database connection established');
    
    // Check for encryption key
    if (process.env.QUEEN_ENCRYPTION_KEY) {
      log('Encryption key detected - encryption tests will run', 'info');
    } else {
      log('No encryption key found - encryption tests will be skipped', 'warning');
      log('Set QUEEN_ENCRYPTION_KEY env variable to test encryption', 'info');
    }
    
    // Clean up any existing test data
    await cleanupTestData();
    
    // Core feature tests
    const coreTests = [
      testSingleMessagePush,
      testBatchMessagePush,
      testQueueConfiguration,
      testPopAndAcknowledgment,
      testDelayedProcessing,
      testPartitionPriorityOrdering
    ];
    
    // Enterprise feature tests
    const enterpriseTests = [
      testMessageEncryption,
      testRetentionPendingMessages,
      testRetentionCompletedMessages,
      testPartitionRetention,
      testMessageEviction,
      testCombinedEnterpriseFeatures,
      testConsumerEncryption,
      testEnterpriseErrorHandling
    ];
    
    // Bus mode tests
    const busTests = [
      testBusConsumerGroups,
      testMixedMode,
      testConsumerGroupSubscriptionModes,
      testConsumerGroupIsolation
    ];
    
    // Edge case tests
    const edgeTests = [
      testEmptyAndNullPayloads,
      testVeryLargePayloads,
      testConcurrentPushOperations,
      testConcurrentPopOperations,
      testManyConsumerGroups,
      testRetryLimitExhaustion,
      testLeaseExpiration,
      testSQLInjectionPrevention,
      testXSSPrevention
    ];
    
    // Advanced scenario tests
    const advancedTests = [
      testMultiStagePipeline,
      testFanOutFanIn
    ];
    
    const allTests = [...coreTests, ...enterpriseTests, ...busTests, ...edgeTests, ...advancedTests];
    
    log(`Running ${allTests.length} total tests...`);
    console.log('=' .repeat(80));
    
    // Run core tests
    console.log('\n📦 CORE FEATURES');
    console.log('-' .repeat(40));
    for (const test of coreTests) {
      try {
        await test();
      } catch (error) {
        log(`Unexpected error in test: ${error.message}`, 'error');
      }
      await sleep(100);
    }
    
    // Run enterprise tests
    console.log('\n🏢 ENTERPRISE FEATURES');
    console.log('-' .repeat(40));
    for (const test of enterpriseTests) {
      try {
        await test();
      } catch (error) {
        log(`Unexpected error in test: ${error.message}`, 'error');
      }
      await sleep(100);
    }
    
    // Run bus mode tests
    console.log('\n🚌 BUS MODE FEATURES');
    console.log('-' .repeat(40));
    for (const test of busTests) {
      try {
        await test();
      } catch (error) {
        log(`Unexpected error in test: ${error.message}`, 'error');
      }
      await sleep(100);
    }
    
    // Run edge case tests
    console.log('\n🔍 EDGE CASES');
    console.log('-' .repeat(40));
    for (const test of edgeTests) {
      try {
        await test();
      } catch (error) {
        log(`Unexpected error in test: ${error.message}`, 'error');
      }
      await sleep(100);
    }
    
    // Run advanced scenario tests
    console.log('\n🎯 ADVANCED SCENARIOS');
    console.log('-' .repeat(40));
    for (const test of advancedTests) {
      try {
        await test();
      } catch (error) {
        log(`Unexpected error in test: ${error.message}`, 'error');
      }
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
    console.log('   All features are operational.');
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
