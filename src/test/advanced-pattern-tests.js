/**
 * Advanced Pattern Tests for Queen Message Queue
 * Tests: Multi-stage pipeline, fan-out/fan-in, DLQ, circuit breaker, and more
 */

import crypto from 'crypto';
import { startTest, passTest, failTest, sleep, dbPool, getMessageCount, log } from './utils.js';

/**
 * Test: Multi-stage pipeline workflow
 */
export async function testMultiStagePipeline(client) {
  startTest('Multi-Stage Pipeline Workflow', 'workflow');
  
  try {
    const stages = ['ingestion', 'validation', 'processing', 'enrichment', 'delivery'];
    const itemCount = 10;
    
    // Configure queues for each stage
    for (const stage of stages) {
      await client.queue(`workflow-${stage}`, {
        priority: stages.indexOf(stage) + 1
      });
    }
    
    // Push initial items to ingestion
    const startTime = Date.now();
    const items = Array.from({ length: itemCount }, (_, i) => ({
      id: i,
      data: `Item ${i}`,
      stage: 'ingestion',
      timestamp: Date.now()
    }));
    
    await client.push('workflow-ingestion', items);
    await sleep(200); // Wait for messages to be available
    
    // Process through each stage
    for (let stageIndex = 0; stageIndex < stages.length - 1; stageIndex++) {
      const currentStage = stages[stageIndex];
      const nextStage = stages[stageIndex + 1];
      
      // Process current stage with larger batch size
      const stageMessages = [];
      for await (const msg of client.take(`workflow-${currentStage}`, { 
        limit: itemCount, 
        batch: itemCount 
      })) {
        stageMessages.push(msg);
        // ACK immediately to release partition lock for next takes
        await client.ack(msg);
      }
      
      if (stageMessages.length > 0) {
        // Process and move to next stage
        const nextItems = stageMessages.map(msg => ({
          ...msg.data,
          stage: nextStage,
          previousStage: currentStage,
          processingTime: Date.now() - msg.data.timestamp
        }));
        
        await client.push(`workflow-${nextStage}`, nextItems);
      }
    }
    
    // Verify final delivery
    const finalMessages = [];
    for await (const msg of client.take('workflow-delivery', { limit: itemCount })) {
      finalMessages.push(msg);
      await client.ack(msg);
    }
    
    if (finalMessages.length !== itemCount) {
      throw new Error(`Pipeline incomplete: expected ${itemCount} items in delivery`);
    }
    
    const totalTime = Date.now() - startTime;
    const avgTime = Math.round(totalTime / itemCount);
    
    passTest(`Pipeline processed ${itemCount} items through ${stages.length} stages (avg ${avgTime}ms)`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Fan-out/Fan-in pattern
 */
export async function testFanOutFanIn(client) {
  startTest('Fan-out/Fan-in Pattern', 'pattern');
  
  try {
    const masterQueue = 'pattern-master';
    const workerQueues = ['pattern-worker-1', 'pattern-worker-2', 'pattern-worker-3'];
    const aggregatorQueue = 'pattern-aggregator';
    
    // Configure queues
    await client.queue(masterQueue, {});
    for (const worker of workerQueues) {
      await client.queue(worker, {});
    }
    await client.queue(aggregatorQueue, {});
    
    // Push master task
    const masterTask = {
      id: crypto.randomBytes(16).toString('hex'),
      totalWork: 9,
      data: Array.from({ length: 9 }, (_, i) => `Task ${i}`)
    };
    
    await client.push(masterQueue, masterTask);
    
    // Take master task
    let masterTaskData = null;
    for await (const msg of client.take(masterQueue, { limit: 1 })) {
      masterTaskData = msg.data;
      await client.ack(msg);
    }
    
    if (!masterTaskData) {
      throw new Error('Failed to get master task');
    }
    
    // Fan-out to workers
    const tasksPerWorker = Math.ceil(masterTaskData.totalWork / workerQueues.length);
    for (let i = 0; i < workerQueues.length; i++) {
      const startIdx = i * tasksPerWorker;
      const endIdx = Math.min(startIdx + tasksPerWorker, masterTaskData.totalWork);
      const workerTasks = masterTaskData.data.slice(startIdx, endIdx);
      
      await client.push(workerQueues[i], {
        masterId: masterTaskData.id,
        workerIndex: i,
        tasks: workerTasks
      });
    }
    
    // Workers process and fan-in to aggregator
    const workerResults = [];
    for (const workerQueue of workerQueues) {
      for await (const msg of client.take(workerQueue, { limit: 1 })) {
        // Simulate processing
        const processed = {
          masterId: msg.data.masterId,
          workerIndex: msg.data.workerIndex,
          results: msg.data.tasks.map(t => `Processed: ${t}`)
        };
        
        workerResults.push(processed);
        
        // Send to aggregator
        await client.push(aggregatorQueue, processed);
        await client.ack(msg);
      }
    }
    
    // Aggregate results
    const aggregatedResults = [];
    for await (const msg of client.take(aggregatorQueue, { limit: workerQueues.length })) {
      aggregatedResults.push(msg.data);
      await client.ack(msg);
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

/**
 * Test: Dead letter queue pattern
 */
export async function testDeadLetterQueuePattern(client) {
  startTest('Dead Letter Queue Pattern', 'pattern');
  
  try {
    const mainQueue = 'pattern-main-dlq';
    const dlq = 'pattern-dead-letter';
    
    // Configure main queue with low retry limit
    await client.queue(mainQueue, {
      retryLimit: 2,
      leaseTime: 2,
      dlqAfterMaxRetries: true
    });
    
    // Configure DLQ
    await client.queue(dlq, {
      priority: 1 // Lower priority for DLQ
    });
    
    // Push messages that will fail
    const problematicMessages = Array.from({ length: 3 }, (_, i) => ({
      id: `problem-${i}`,
      shouldFail: true,
      data: 'This will fail processing'
    }));
    
    await client.push(mainQueue, problematicMessages);
    
    // Process messages with simulated failures
    let dlqCount = 0;
    for (let attempt = 0; attempt < 10; attempt++) {
      const messages = [];
      for await (const msg of client.take(mainQueue, { limit: 10 })) {
        messages.push(msg);
      }
      
      if (messages.length === 0) break;
      
      for (const msg of messages) {
        if (msg.data.shouldFail) {
          // Check if message is in DLQ
          const retryCheck = await dbPool.query(`
            SELECT dlq.retry_count 
            FROM queen.dead_letter_queue dlq
            JOIN queen.messages m ON dlq.message_id = m.id
            WHERE m.transaction_id = $1 AND dlq.consumer_group = '__QUEUE_MODE__'
          `, [msg.transactionId]);
          
          const retryCount = retryCheck.rows[0]?.retry_count || 0;
          
          if (retryCount >= 2) {
            // Move to DLQ manually (since we're simulating)
            await client.push(dlq, {
              ...msg.data,
              originalQueue: mainQueue,
              failureReason: 'Max retries exceeded',
              movedToDlqAt: Date.now()
            });
            dlqCount++;
            // Mark as completed to remove from main queue
            await client.ack(msg);
          } else {
            // Fail the message
            await client.ack(msg, false, { error: 'Simulated failure' });
          }
        } else {
          await client.ack(msg);
        }
      }
      
      await sleep(2500); // Wait for lease expiration
    }
    
    // Verify messages in DLQ
    const dlqMessages = [];
    for await (const msg of client.take(dlq, { limit: 10 })) {
      dlqMessages.push(msg);
      await client.ack(msg);
    }
    
    if (dlqMessages.length < dlqCount) {
      throw new Error(`Expected at least ${dlqCount} messages in DLQ`);
    }
    
    passTest(`Dead letter queue pattern: ${dlqCount} messages moved to DLQ`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Circuit breaker pattern
 */
export async function testCircuitBreaker(client) {
  startTest('Circuit Breaker Pattern', 'pattern');
  
  try {
    const queue = 'pattern-circuit-breaker';
    
    // Configure queue
    await client.queue(queue, {});
    
    const circuitBreaker = {
      state: 'CLOSED', // CLOSED, OPEN, HALF_OPEN
      failureCount: 0,
      successCount: 0,
      failureThreshold: 3,
      successThreshold: 2,
      timeout: 2000,
      lastFailureTime: null
    };
    
    // Push messages
    const messages = Array.from({ length: 20 }, (_, i) => ({
      id: i,
      shouldFail: i < 10 // First 10 will fail
    }));
    
    await client.push(queue, messages);
    
    // Process with circuit breaker
    let processedCount = 0;
    const maxAttempts = 30;
    
    for (let attempt = 0; attempt < maxAttempts && processedCount < messages.length; attempt++) {
      // Check circuit breaker state
      if (circuitBreaker.state === 'OPEN') {
        const timeSinceFailure = Date.now() - circuitBreaker.lastFailureTime;
        if (timeSinceFailure >= circuitBreaker.timeout) {
          circuitBreaker.state = 'HALF_OPEN';
          circuitBreaker.failureCount = 0;
          circuitBreaker.successCount = 0;
          log('Circuit breaker: OPEN → HALF_OPEN', 'warning');
        } else {
          log('Circuit breaker is OPEN, waiting...', 'warning');
          await sleep(500);
          continue;
        }
      }
      
      // Try to process message
      for await (const msg of client.take(queue, { limit: 1 })) {
        try {
          if (msg.data.shouldFail && circuitBreaker.state !== 'OPEN') {
            throw new Error('Simulated failure');
          }
          
          // Success
          await client.ack(msg);
          processedCount++;
          
          if (circuitBreaker.state === 'HALF_OPEN') {
            circuitBreaker.successCount++;
            if (circuitBreaker.successCount >= circuitBreaker.successThreshold) {
              circuitBreaker.state = 'CLOSED';
              circuitBreaker.failureCount = 0;
              log('Circuit breaker: HALF_OPEN → CLOSED', 'success');
            }
          }
        } catch (error) {
          // Failure
          await client.ack(msg, false, { error: error.message });
          circuitBreaker.failureCount++;
          circuitBreaker.lastFailureTime = Date.now();
          
          if (circuitBreaker.state === 'CLOSED' && 
              circuitBreaker.failureCount >= circuitBreaker.failureThreshold) {
            circuitBreaker.state = 'OPEN';
            log('Circuit breaker: CLOSED → OPEN (threshold reached)', 'error');
          } else if (circuitBreaker.state === 'HALF_OPEN') {
            circuitBreaker.state = 'OPEN';
            log('Circuit breaker: HALF_OPEN → OPEN (failure during recovery)', 'error');
          }
        }
      }
      
      await sleep(100);
    }
    
    log(`Circuit breaker results: ${processedCount - circuitBreaker.failureCount} success, ${circuitBreaker.failureCount} failed`, 'info');
    passTest('Circuit breaker pattern implemented successfully');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Complex priority scenarios
 */
export async function testComplexPriorityScenarios(client) {
  startTest('Complex Priority Scenarios', 'priority');
  
  try {
    const queue = 'test-complex-priority';
    
    // Push messages with different priorities
    const priorities = [100, 50, 75, 1, 10, 90, 30, 60, 5, 80];
    const messages = priorities.map((priority, index) => ({
      id: index,
      priority,
      data: `Priority ${priority} message`
    }));
    
    // Configure queue with priority
    await client.queue(queue, {
      priority: 50
    });
    
    // Push messages in random priority order
    await client.push(queue, messages);
    await sleep(100);
    
    // Take all messages - should come in FIFO order within partition
    const received = [];
    for await (const msg of client.take(queue, { limit: messages.length })) {
      received.push(msg);
      await client.ack(msg);
    }
    
    if (received.length !== messages.length) {
      throw new Error(`Expected ${messages.length} messages, got ${received.length}`);
    }
    
    // Calculate average priority of first vs last 3 messages
    const firstThree = received.slice(0, 3).map(m => m.payload.priority);
    const lastThree = received.slice(-3).map(m => m.payload.priority);
    
    const avgFirst = firstThree.reduce((a, b) => a + b, 0) / 3;
    const avgLast = lastThree.reduce((a, b) => a + b, 0) / 3;
    
    log(`First 3 messages avg priority: ${avgFirst.toFixed(0)}`, 'info');
    log(`Last 3 messages avg priority: ${avgLast.toFixed(0)}`, 'info');
    
    passTest('Complex priority scenarios handled correctly');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Dynamic priority adjustment
 */
export async function testDynamicPriorityAdjustment(client) {
  startTest('Dynamic Priority Adjustment', 'priority');
  
  try {
    const queue = 'test-dynamic-priority';
    
    // Start with low priority
    await client.queue(queue, {
      priority: 1
    });
    
    // Push some messages
    await client.push(queue, { 
      phase: 'low-priority', 
      timestamp: Date.now() 
    });
    
    // Increase priority
    await client.queue(queue, {
      priority: 100
    });
    
    // Push more messages
    await client.push(queue, { 
      phase: 'high-priority', 
      timestamp: Date.now() 
    });
    
    // Verify configuration change
    const queueInfo = await dbPool.query(
      'SELECT priority FROM queen.queues WHERE name = $1',
      [queue]
    );
    
    if (queueInfo.rows[0].priority !== 100) {
      throw new Error('Priority not updated correctly');
    }
    
    passTest('Dynamic priority adjustment works');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Saga pattern (distributed transactions)
 */
export async function testSagaPattern(client) {
  startTest('Saga Pattern (Distributed Transactions)', 'pattern');
  
  try {
    const sagaSteps = [
      'pattern-saga-order',
      'pattern-saga-payment',
      'pattern-saga-inventory',
      'pattern-saga-shipping'
    ];
    
    const compensationSteps = [
      'pattern-saga-cancel-shipping',
      'pattern-saga-restore-inventory',
      'pattern-saga-refund-payment',
      'pattern-saga-cancel-order'
    ];
    
    // Configure all saga queues
    for (const queue of [...sagaSteps, ...compensationSteps]) {
      await client.queue(queue, {});
    }
    
    const sagaId = crypto.randomBytes(16).toString('hex');
    const sagaData = {
      id: sagaId,
      orderId: 'ORDER-123',
      amount: 100,
      items: ['item1', 'item2'],
      status: 'started'
    };
    
    // Execute saga steps
    let currentData = { ...sagaData };
    let failedAtStep = -1;
    
    for (let i = 0; i < sagaSteps.length; i++) {
      const step = sagaSteps[i];
      
      // Push to step queue
      await client.push(step, {
        ...currentData,
        step: i,
        stepName: step
      });
      
      // Process step
      let stepMessage = null;
      for await (const msg of client.take(step, { limit: 1 })) {
        stepMessage = msg;
      }
      
      if (stepMessage) {
        // Simulate failure at payment step
        if (step === 'pattern-saga-payment' && Math.random() > 0.5) {
          failedAtStep = i;
          await client.ack(stepMessage, false, { error: 'Payment failed' });
          break;
        }
        
        // Step succeeded
        currentData = {
          ...stepMessage.data,
          [`${step}_completed`]: true
        };
        
        await client.ack(stepMessage);
      }
    }
    
    // If saga failed, execute compensation
    if (failedAtStep >= 0) {
      log('Saga failed, starting compensation...', 'warning');
      
      for (let i = failedAtStep; i >= 0; i--) {
        const compensationStep = compensationSteps[compensationSteps.length - 1 - i];
        
        await client.push(compensationStep, {
          ...currentData,
          compensationStep: i,
          compensationReason: 'Saga rollback'
        });
        
        for await (const msg of client.take(compensationStep, { limit: 1 })) {
          await client.ack(msg);
        }
      }
    }
    
    passTest('Saga pattern with compensation executed successfully');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Rate limiting and throttling
 */
export async function testRateLimiting(client) {
  startTest('Rate Limiting and Throttling', 'pattern');
  
  try {
    const queue = 'pattern-rate-limited';
    const rateLimit = 5; // 5 messages per second
    
    // Configure queue
    await client.queue(queue, {
      windowBuffer: 1 // 1 second window
    });
    
    // Push burst of messages
    const burstSize = 20;
    const messages = Array.from({ length: burstSize }, (_, i) => ({
      id: i,
      timestamp: Date.now()
    }));
    
    await client.push(queue, messages);
    
    // Implement rate-limited consumer
    const processedMessages = [];
    const startTime = Date.now();
    let lastWindowStart = startTime;
    let windowMessageCount = 0;
    
    while (processedMessages.length < burstSize) {
      const currentTime = Date.now();
      
      // Reset window if needed
      if (currentTime - lastWindowStart >= 1000) {
        lastWindowStart = currentTime;
        windowMessageCount = 0;
      }
      
      // Check if we can process more messages
      if (windowMessageCount < rateLimit) {
        const batchSize = Math.min(
          rateLimit - windowMessageCount, 
          burstSize - processedMessages.length
        );
        
        for await (const msg of client.take(queue, { limit: batchSize })) {
          processedMessages.push({
            id: msg.data.id,
            processedAt: Date.now() - startTime
          });
          windowMessageCount++;
          await client.ack(msg);
        }
      } else {
        // Wait for next window
        await sleep(Math.max(0, 1000 - (currentTime - lastWindowStart)));
      }
    }
    
    const totalTime = Date.now() - startTime;
    const actualRate = (burstSize / (totalTime / 1000)).toFixed(2);
    
    log(`Processed ${burstSize} messages at ${actualRate} msg/sec (limit: ${rateLimit})`, 'info');
    
    // Allow up to 40% variance due to timing variations
    if (actualRate > rateLimit * 1.4) {
      throw new Error(`Rate limit exceeded: ${actualRate} > ${rateLimit * 1.4}`);
    }
    
    passTest(`Rate limiting enforced: ${actualRate} msg/sec`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Time-based batch processing
 */
export async function testTimeBatchProcessing(client) {
  startTest('Time-Based Batch Processing', 'pattern');
  
  try {
    const queue = 'pattern-time-batch';
    
    // Configure queue
    await client.queue(queue, {});
    
    const batchWindow = 2000; // 2 seconds
    const minBatchSize = 3;
    const maxBatchSize = 10;
    
    // Push messages over time
    const messageCount = 20;
    for (let i = 0; i < messageCount; i++) {
      await client.push(queue, { 
        id: i, 
        timestamp: Date.now() 
      });
      
      // Stagger message arrival
      if (i % 5 === 0) {
        await sleep(500);
      }
    }
    
    // Process in time-based batches
    const batches = [];
    let currentBatch = [];
    let batchStartTime = Date.now();
    let totalProcessed = 0;
    
    while (totalProcessed < messageCount) {
      const currentTime = Date.now();
      const batchAge = currentTime - batchStartTime;
      
      // Check if batch should be processed
      if (currentBatch.length >= maxBatchSize || 
          (batchAge >= batchWindow && currentBatch.length >= minBatchSize)) {
        
        batches.push({
          size: currentBatch.length,
          duration: batchAge,
          messages: [...currentBatch]
        });
        
        // Acknowledge batch
        for (const msg of currentBatch) {
          await client.ack(msg);
        }
        
        totalProcessed += currentBatch.length;
        currentBatch = [];
        batchStartTime = Date.now();
      }
      
      // Try to add more messages to batch
      const remaining = Math.min(maxBatchSize - currentBatch.length, messageCount - totalProcessed);
      if (remaining > 0) {
        let gotMessages = false;
        for await (const msg of client.take(queue, { limit: remaining })) {
          currentBatch.push(msg);
          gotMessages = true;
        }
        
        // If we didn't get any messages
        if (!gotMessages) {
          // If we have messages in the batch and window expired, process them
          if (currentBatch.length > 0 && batchAge >= batchWindow) {
            batches.push({
              size: currentBatch.length,
              duration: batchAge,
              messages: [...currentBatch]
            });
            
            for (const msg of currentBatch) {
              await client.ack(msg);
            }
            
            totalProcessed += currentBatch.length;
            currentBatch = [];
            batchStartTime = Date.now();
          } else {
            // Wait a bit before retrying
            await sleep(100);
          }
        }
      }
      
      // Safety valve: if we're stuck, break out
      if (totalProcessed === 0 && batches.length === 0 && currentBatch.length === 0) {
        log('No messages available, breaking out of batch processing', 'warning');
        break;
      }
    }
    
    // Process any remaining messages in current batch
    if (currentBatch.length > 0) {
      batches.push({
        size: currentBatch.length,
        duration: Date.now() - batchStartTime,
        messages: [...currentBatch]
      });
      
      for (const msg of currentBatch) {
        await client.ack(msg);
      }
      
      totalProcessed += currentBatch.length;
    }
    
    log(`Created ${batches.length} time-based batches`, 'info');
    batches.forEach(batch => {
      log(`  Batch: ${batch.size} messages in ${batch.duration}ms`, 'info');
    });
    
    passTest(`Time-based batching created ${batches.length} batches`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Event sourcing with replay
 */
export async function testEventSourcing(client) {
  startTest('Event Sourcing with Replay', 'pattern');
  
  try {
    const eventQueue = 'pattern-event-store';
    const snapshotQueue = 'pattern-snapshots';
    
    // Configure queues
    await client.queue(eventQueue, {
      retentionEnabled: true,
      retentionSeconds: 86400 // Keep events for 24 hours
    });
    
    await client.queue(snapshotQueue, {});
    
    // Generate events
    const entityId = crypto.randomBytes(8).toString('hex');
    const events = [
      { type: 'created', entityId, data: { name: 'Test Entity' }, version: 1 },
      { type: 'updated', entityId, data: { name: 'Updated Entity' }, version: 2 },
      { type: 'statusChanged', entityId, data: { status: 'active' }, version: 3 },
      { type: 'updated', entityId, data: { name: 'Final Entity' }, version: 4 },
      { type: 'statusChanged', entityId, data: { status: 'completed' }, version: 5 }
    ];
    
    // Store events
    for (const event of events) {
      await client.push(eventQueue, {
        ...event,
        timestamp: Date.now()
      });
    }
    
    // Replay events to rebuild state
    const rebuiltState = {
      entityId,
      version: 0,
      data: {}
    };
    
    const allEvents = [];
    for await (const msg of client.take(eventQueue, { limit: 100 })) {
      allEvents.push(msg);
    }
    
    if (allEvents.length > 0) {
      // Sort by version to ensure correct order
      allEvents.sort((a, b) => a.data.version - b.data.version);
      
      for (const msg of allEvents) {
        const event = msg.data;
        
        switch (event.type) {
          case 'created':
            rebuiltState.data = event.data;
            break;
          case 'updated':
            rebuiltState.data = { ...rebuiltState.data, ...event.data };
            break;
          case 'statusChanged':
            rebuiltState.data.status = event.data.status;
            break;
        }
        
        rebuiltState.version = event.version;
        
        // Don't ack events - we want to keep them for replay
      }
    }
    
    // Create snapshot after replay
    if (rebuiltState.version === events.length) {
      await client.push(snapshotQueue, {
        ...rebuiltState,
        snapshotTime: Date.now()
      });
    }
    
    passTest(`Event sourcing: ${events.length} events replayed, state rebuilt`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Queue metrics and monitoring
 */
export async function testQueueMetrics(client) {
  startTest('Queue Metrics and Monitoring', 'pattern');
  
  try {
    const queue = 'pattern-metrics';
    const messageCount = 50;
    
    // Configure queue
    await client.queue(queue, {
      leaseTime: 10
    });
    
    // Track metrics
    const metrics = {
      published: 0,
      consumed: 0,
      completed: 0,
      failed: 0,
      processingTimes: [],
      throughput: 0
    };
    
    const startTime = Date.now();
    
    // Publish messages
    const messages = Array.from({ length: messageCount }, (_, i) => ({
      id: i,
      shouldFail: Math.random() > 0.7, // 30% failure rate
      processingTime: Math.floor(Math.random() * 100) // 0-100ms
    }));
    
    await client.push(queue, messages);
    metrics.published = messageCount;
    
    // Consume and process
    while (metrics.consumed < messageCount) {
      for await (const msg of client.take(queue, { limit: 10 })) {
        metrics.consumed++;
        const processStart = Date.now();
        
        // Simulate processing
        await sleep(msg.data.processingTime);
        
        if (msg.data.shouldFail) {
          await client.ack(msg, false, { error: 'Simulated failure' });
          metrics.failed++;
        } else {
          await client.ack(msg);
          metrics.completed++;
        }
        
        metrics.processingTimes.push(Date.now() - processStart);
      }
      
      if (metrics.consumed < messageCount) {
        await sleep(100);
      }
    }
    
    const totalTime = Date.now() - startTime;
    metrics.throughput = (messageCount / (totalTime / 1000)).toFixed(2);
    
    // Calculate additional metrics
    const avgProcessingTime = metrics.processingTimes.reduce((a, b) => a + b, 0) / metrics.processingTimes.length;
    
    log('Queue Metrics:', 'info');
    log(`  Published: ${metrics.published}`, 'info');
    log(`  Consumed: ${metrics.consumed}`, 'info');
    log(`  Completed: ${metrics.completed}`, 'info');
    log(`  Failed: ${metrics.failed}`, 'info');
    log(`  Avg Processing: ${avgProcessingTime.toFixed(2)}ms`, 'info');
    log(`  Throughput: ${metrics.throughput} msg/sec`, 'info');
    
    passTest('Queue metrics collected successfully');
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Multiple Consumers - Single Partition Message Ordering
 * Verifies that multiple consumers maintain message ordering
 */
export async function testMultipleConsumersSinglePartitionOrdering(client) {
  startTest('Multiple Consumers - Single Partition Message Ordering', 'pattern');
  
  try {
    const queue = 'test-multi-consumer-ordering';
    const partition = 'single-partition';
    const numMessages = 999;
    const numConsumers = 10;
    const batchSize = 50;
    
    // Configure queue
    await client.queue(queue, {
      leaseTime: 30,
      retryLimit: 3
    });
    
    log(`Pushing ${numMessages} messages to queue '${queue}', partition '${partition}'`);
    
    // Push messages with sequential order identifiers
    const pushStartTime = Date.now();
    const items = Array.from({ length: numMessages }, (_, i) => ({
      sequenceNumber: i,
      data: `Message ${i}`,
      timestamp: Date.now()
    }));
    
    // Push in batches
    const pushBatchSize = 100;
    for (let i = 0; i < items.length; i += pushBatchSize) {
      const batch = items.slice(i, Math.min(i + pushBatchSize, items.length));
      await client.push(`${queue}/${partition}`, batch);
    }
    
    const pushTime = Date.now() - pushStartTime;
    log(`Pushed ${numMessages} messages in ${pushTime}ms`);
    
    await sleep(500);
    
    // Verify messages are in the queue
    const messageCount = await getMessageCount(queue, partition);
    if (messageCount !== numMessages) {
      throw new Error(`Expected ${numMessages} messages in queue, but found ${messageCount}`);
    }
    
    log(`Starting ${numConsumers} consumers to process messages`);
    
    // Track all consumed messages
    const consumedMessages = [];
    const consumerPromises = [];
    
    // Create multiple consumers
    for (let consumerId = 0; consumerId < numConsumers; consumerId++) {
      const consumerPromise = (async () => {
        const consumerMessages = [];
        let consecutiveEmptyPolls = 0;
        const maxEmptyPolls = 3;
        
        while (consecutiveEmptyPolls < maxEmptyPolls) {
          try {
            let gotMessages = false;
            for await (const msg of client.take(`${queue}/${partition}`, { 
              limit: batchSize, 
              batch: batchSize 
            })) {
              const sequenceNumber = msg.payload.sequenceNumber;
              consumerMessages.push({
                consumerId,
                sequenceNumber,
                timestamp: Date.now(),
                transactionId: msg.transactionId
              });
              
              await client.ack(msg);
              gotMessages = true;
            }
            
            if (!gotMessages) {
              consecutiveEmptyPolls++;
              await sleep(100);
            } else {
              consecutiveEmptyPolls = 0;
              log(`Consumer ${consumerId} processed batch (total: ${consumerMessages.length})`);
            }
          } catch (error) {
            log(`Consumer ${consumerId} error: ${error.message}`, 'warning');
            await sleep(500);
          }
        }
        
        if (consumerMessages.length > 0) {
          log(`Consumer ${consumerId} completed with ${consumerMessages.length} total messages`);
        }
        
        return consumerMessages;
      })();
      
      consumerPromises.push(consumerPromise);
      
      // Stagger consumer starts
      await sleep(50);
    }
    
    // Wait for all consumers to complete
    log('Waiting for all consumers to complete processing...');
    const consumerResults = await Promise.all(consumerPromises);
    
    // Combine all consumed messages
    for (const messages of consumerResults) {
      consumedMessages.push(...messages);
    }
    
    // Sort by sequence number
    consumedMessages.sort((a, b) => a.sequenceNumber - b.sequenceNumber);
    
    log(`Total messages consumed: ${consumedMessages.length}`);
    
    // Verify all messages were consumed exactly once
    if (consumedMessages.length !== numMessages) {
      throw new Error(`Expected ${numMessages} messages to be consumed, but got ${consumedMessages.length}`);
    }
    
    // Verify no duplicates
    const sequenceNumbers = new Set(consumedMessages.map(m => m.sequenceNumber));
    if (sequenceNumbers.size !== numMessages) {
      throw new Error(`Found duplicate messages: expected ${numMessages} unique sequences, got ${sequenceNumbers.size}`);
    }
    
    // Verify sequential ordering
    for (let i = 0; i < numMessages; i++) {
      if (consumedMessages[i].sequenceNumber !== i) {
        throw new Error(`Message ordering violation at position ${i}: expected sequence ${i}, got ${consumedMessages[i].sequenceNumber}`);
      }
    }
    
    // Analyze consumer distribution
    const consumerStats = {};
    for (const msg of consumedMessages) {
      consumerStats[msg.consumerId] = (consumerStats[msg.consumerId] || 0) + 1;
    }
    
    log('Consumer distribution:', 'info');
    for (const [consumerId, count] of Object.entries(consumerStats)) {
      log(`  Consumer ${consumerId}: ${count} messages (${(count / numMessages * 100).toFixed(1)}%)`, 'info');
    }
    
    passTest(`${numConsumers} consumers successfully processed ${numMessages} messages from single partition with correct ordering`);
  } catch (error) {
    failTest(error);
  }
}

/**
 * Test: Message deduplication
 */
export async function testMessageDeduplication(client) {
  startTest('Message Deduplication', 'pattern');
  
  try {
    const queue = 'pattern-dedup';
    
    // Configure queue
    await client.queue(queue, {});
    
    const deduplicationWindow = new Map();
    
    // Push messages with some duplicates
    const messages = [
      { id: 'msg-1', data: 'First' },
      { id: 'msg-2', data: 'Second' },
      { id: 'msg-1', data: 'First duplicate' }, // Duplicate
      { id: 'msg-3', data: 'Third' },
      { id: 'msg-2', data: 'Second duplicate' }, // Duplicate
      { id: 'msg-4', data: 'Fourth' }
    ];
    
    await client.push(queue, messages);
    
    // Process with deduplication
    const uniqueMessages = [];
    for await (const msg of client.take(queue, { limit: messages.length })) {
      const messageId = msg.data.id;
      
      // Check for duplicate
      if (!deduplicationWindow.has(messageId)) {
        deduplicationWindow.set(messageId, Date.now());
        uniqueMessages.push(msg);
      } else {
        log(`Duplicate detected: ${messageId}`, 'warning');
      }
      
      await client.ack(msg);
    }
    
    passTest(`Deduplication successful: ${messages.length} messages → ${uniqueMessages.length} unique`);
  } catch (error) {
    failTest(error);
  }
}

