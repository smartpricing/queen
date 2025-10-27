#!/usr/bin/env node

/**
 * Complete Test Suite for Queen Message Queue System
 * Using the new minimalist Queen client interface
 * 
 * Test Categories:
 * 1. Core Features
 * 2. Partition Locking
 * 3. Enterprise Features (coming soon)
 * 4. Bus Mode Features (coming soon)
 * 5. Edge Cases (coming soon)
 * 6. Advanced Scenarios & Patterns (coming soon)
 */

import { Queen } from '../client/client.js';
import { 
  TEST_CONFIG, 
  initDb, 
  closeDb, 
  cleanupTestData, 
  printResults, 
  log,
  resetResults
} from './utils.js';

// Import human tests
import {
  testCreateQueue,
  workerQueueTest
} from './human.js';

// Import test modules
import {
  testQueueCreationPolicy,
  testSingleMessagePush,
  testBatchMessagePush,
  testQueueConfiguration,
  testTakeAndAcknowledgment,
  testDelayedProcessing,
  testPartitionFIFOOrdering,
  testWindowBuffer
} from './core-tests.js';

import {
  testPartitionLocking,
  testBusPartitionLocking,
  testSpecificPartitionLocking,
  testNamespaceTaskFiltering,
  testNamespaceTaskBusMode
} from './partition-locking-tests.js';

import {
  testMessageEncryption,
  testRetentionPendingMessages,
  testRetentionCompletedMessages,
  testPartitionRetention,
  testConsumerEncryption,
  testMessageEviction,
  testCombinedEnterpriseFeatures,
  testEnterpriseErrorHandling
} from './enterprise-tests.js';

import {
  testBusConsumerGroups,
  testMixedMode,
  testConsumerGroupSubscriptionModes,
  testConsumerGroupIsolation
} from './bus-mode-tests.js';

import {
  testEmptyAndNullPayloads,
  testVeryLargePayloads,
  testConcurrentPushOperations,
  testConcurrentTakeOperations,
  testManyConsumerGroups,
  testRetryLimitExhaustion,
  testLeaseExpiration,
  testSQLInjectionPrevention,
  testXSSPrevention
} from './edge-case-tests.js';

import {
  testDuplicateTransactionIdsAcrossPartitions,
  testBatchAckWithDuplicateTransactionIds,
  testDLQWithDuplicateTransactionIds,
  testAnalyticsAPIWithDuplicateTransactionIds
} from './partition-transaction-tests.js';

import {
  testMultiStagePipeline,
  testFanOutFanIn,
  testComplexPriorityScenarios,
  testDynamicPriorityAdjustment,
  testSagaPattern,
  testRateLimiting,
  testTimeBatchProcessing,
  testEventSourcing,
  testQueueMetrics,
  testMultipleConsumersSinglePartitionOrdering,
  testDeadLetterQueuePattern,
  testCircuitBreaker,
  testMessageDeduplication
} from './advanced-pattern-tests.js';

import {
  testPipelineBasic,
  testPipelineErrorHandling,
  testPipelineAutoRenewal,
  testPipelineParallel,
  testPipelineConcurrencyRaceCondition,
  testPipelineContinuous,
  testTransactionAPI,
  testPipelineAtomic,
  testManualLeaseRenewal,
  testLeaseValidation,
  testComplexPipelineWorkflow,
  runAdvancedClientTests
} from './advanced-client-tests.js';

import {
  testQoS0Buffering,
  testAutoAck,
  testConsumerGroupAutoAck,
  testFIFOOrdering,
  testMixedOperations,
  testBatchedPayload
} from './qos0-tests.js';

// Helper to run a test with error handling and delay
async function runTest(testFn) {
  try {
    await testFn();
  } catch (error) {
    log(`Unexpected error in test: ${error.message}`, 'error');
  }
  await new Promise(resolve => setTimeout(resolve, 100)); // Small delay between tests
}

// Parse command-line arguments
const args = process.argv.slice(2);
const testFilter = args[0]; // e.g., 'core', 'bus', 'edge', 'enterprise', 'partition', 'advanced'

// Show help if requested
if (testFilter === 'help' || testFilter === '--help' || testFilter === '-h') {
  console.log('Usage: node src/test/test-new.js [category]');
  console.log('\nAvailable test categories:');
  console.log('  core       - Core features (queue creation, push, take, ack, etc.)');
  console.log('  partition  - Partition locking tests');
  console.log('  enterprise - Enterprise features (encryption, retention, eviction)');
  console.log('  bus        - Bus mode features (consumer groups, mixed mode)');
  console.log('  edge       - Edge cases (null payloads, large payloads, SQL injection, etc.)');
  console.log('  qos0       - QoS 0 features (buffering, auto-ack, FIFO ordering)');
  console.log('  advanced   - Advanced patterns (pipeline, fan-out, DLQ, circuit breaker)');
  console.log('\nExamples:');
  console.log('  node src/test/test-new.js              # Run all tests');
  console.log('  node src/test/test-new.js core         # Run only core tests');
  console.log('  node src/test/test-new.js bus          # Run only bus mode tests');
  console.log('  node src/test/test-new.js edge         # Run only edge case tests');
  console.log('  node src/test/test-new.js qos0         # Run only QoS 0 tests');
  process.exit(0);
}

async function runTests() {
  console.log('🚀 Starting Queen Message Queue Test Suite');
  console.log('   Using the new minimalist Queen client interface\n');
  if (testFilter) {
    console.log(`   🎯 Running only: ${testFilter} tests\n`);
  }
  console.log('='.repeat(80));
  
  let client;
  
  try {
    // Initialize database connection
    await initDb();
    log('Database connection established');
    
    // Initialize client
    client = new Queen({ 
      baseUrls: TEST_CONFIG.baseUrls,
      timeout: 30000,
      retryAttempts: 3
    });
    log('Queen client initialized');
    
    // Check for encryption key
    if (process.env.QUEEN_ENCRYPTION_KEY) {
      log('Encryption key detected - encryption tests will run', 'info');
    } else {
      log('No encryption key found - encryption tests will be skipped', 'warning');
    }
    
    // Clean up any existing test data
    await cleanupTestData();

    // ============================================
    // HUMAN TESTS
    // ============================================
    if (!testFilter || testFilter === 'human') {
      console.log('\n👤 HUMAN TESTS');
      console.log('-'.repeat(40));
      await runTest(() => testCreateQueue(client));
      await runTest(() => workerQueueTest(client));
    }
    
    // ============================================
    // CORE FEATURE TESTS
    // ============================================
    if (!testFilter || testFilter === 'core') {
      console.log('\n📦 CORE FEATURES');
      console.log('-'.repeat(40));
      
      await runTest(() => testQueueCreationPolicy(client));
      await runTest(() => testSingleMessagePush(client));
      await runTest(() => testBatchMessagePush(client));
      await runTest(() => testQueueConfiguration(client));
      await runTest(() => testTakeAndAcknowledgment(client));
      await runTest(() => testDelayedProcessing(client));
      await runTest(() => testPartitionFIFOOrdering(client));
      await runTest(() => testWindowBuffer(client));
    }
    
    // ============================================
    // PARTITION LOCKING TESTS
    // ============================================
    if (!testFilter || testFilter === 'partition' || testFilter === 'locking') {
      console.log('\n🔒 PARTITION LOCKING');
      console.log('-'.repeat(40));
      
      await runTest(() => testPartitionLocking(client));
      await runTest(() => testBusPartitionLocking(client));
      await runTest(() => testSpecificPartitionLocking(client));
      await runTest(() => testNamespaceTaskFiltering(client));
      await runTest(() => testNamespaceTaskBusMode(client));
    }
    
    // ============================================
    // ENTERPRISE FEATURE TESTS
    // ============================================
    if (!testFilter || testFilter === 'enterprise') {
      console.log('\n🏢 ENTERPRISE FEATURES');
      console.log('-'.repeat(40));
      
      await runTest(() => testMessageEncryption(client));
      await runTest(() => testRetentionPendingMessages(client));
      await runTest(() => testRetentionCompletedMessages(client));
      await runTest(() => testPartitionRetention(client));
      await runTest(() => testConsumerEncryption(client));
      await runTest(() => testMessageEviction(client));
      await runTest(() => testCombinedEnterpriseFeatures(client));
      await runTest(() => testEnterpriseErrorHandling(client));
    }
    
    // ============================================
    // BUS MODE FEATURE TESTS
    // ============================================
    if (!testFilter || testFilter === 'bus') {
      console.log('\n🚌 BUS MODE FEATURES');
      console.log('-'.repeat(40));
      
      await runTest(() => testBusConsumerGroups(client));
      await runTest(() => testMixedMode(client));
      await runTest(() => testConsumerGroupSubscriptionModes(client));
      await runTest(() => testConsumerGroupIsolation(client));
    }
    
    // ============================================
    // EDGE CASE TESTS
    // ============================================
    if (!testFilter || testFilter === 'edge') {
      console.log('\n🔍 EDGE CASES');
      console.log('-'.repeat(40));
      
      await runTest(() => testEmptyAndNullPayloads(client));
      await runTest(() => testVeryLargePayloads(client));
      await runTest(() => testConcurrentPushOperations(client));
      await runTest(() => testConcurrentTakeOperations(client));
      await runTest(() => testManyConsumerGroups(client));
      await runTest(() => testRetryLimitExhaustion(client));
      await runTest(() => testLeaseExpiration(client));
      await runTest(() => testSQLInjectionPrevention(client));
      await runTest(() => testXSSPrevention(client));
      
      // Partition-scoped transaction_id tests
      await runTest(() => testDuplicateTransactionIdsAcrossPartitions(client));
      await runTest(() => testBatchAckWithDuplicateTransactionIds(client));
      await runTest(() => testDLQWithDuplicateTransactionIds(client));
      await runTest(() => testAnalyticsAPIWithDuplicateTransactionIds(client));
    }
    
    // ============================================
    // QOS 0 FEATURE TESTS
    // ============================================
    if (!testFilter || testFilter === 'qos0') {
      console.log('\n⚡ QOS 0 FEATURES');
      console.log('-'.repeat(40));
      
      await runTest(() => testQoS0Buffering(client));
      await runTest(() => testAutoAck(client));
      await runTest(() => testConsumerGroupAutoAck(client));
      await runTest(() => testFIFOOrdering(client));
      await runTest(() => testMixedOperations(client));
      await runTest(() => testBatchedPayload(client));
    }
    
    // ============================================
    // ADVANCED CLIENT FEATURE TESTS
    // ============================================
    if (!testFilter || testFilter === 'advanced' || testFilter === 'client') {
      console.log('\n🚀 ADVANCED CLIENT FEATURES');
      console.log('-'.repeat(40));
      
      await runTest(() => testPipelineBasic(client));
      await runTest(() => testPipelineErrorHandling(client));
      await runTest(() => testPipelineAutoRenewal(client));
      await runTest(() => testPipelineParallel(client));
      await runTest(() => testPipelineConcurrencyRaceCondition(client));
      await runTest(() => testPipelineContinuous(client));
      await runTest(() => testTransactionAPI(client));
      await runTest(() => testPipelineAtomic(client));
      await runTest(() => testManualLeaseRenewal(client));
      await runTest(() => testLeaseValidation(client));
      await runTest(() => testComplexPipelineWorkflow(client));
    }
    
    // ============================================
    // ADVANCED SCENARIO TESTS
    // ============================================
    if (!testFilter || testFilter === 'advanced' || testFilter === 'pattern') {
      console.log('\n🎯 ADVANCED PATTERNS');
      console.log('-'.repeat(40));
      
      await runTest(() => testMultiStagePipeline(client));
      await runTest(() => testFanOutFanIn(client));
      await runTest(() => testComplexPriorityScenarios(client));
      await runTest(() => testDynamicPriorityAdjustment(client));
      await runTest(() => testDeadLetterQueuePattern(client));
      await runTest(() => testSagaPattern(client));
      await runTest(() => testRateLimiting(client));
      await runTest(() => testMessageDeduplication(client));
      await runTest(() => testTimeBatchProcessing(client));
      await runTest(() => testEventSourcing(client));
      await runTest(() => testQueueMetrics(client));
      await runTest(() => testCircuitBreaker(client));
      await runTest(() => testMultipleConsumersSinglePartitionOrdering(client));
    }
    
  } catch (error) {
    log(`Test suite setup failed: ${error.message}`, 'error');
    console.error(error);
  } finally {
    // Cleanup
    if (client) {
      await client.close();
    }
    await cleanupTestData();
    await closeDb();
  }
  
  // Print results
  printResults();
}

// Run the test suite
runTests().catch(error => {
  console.error('💥 Test suite crashed:', error);
  process.exit(1);
});

