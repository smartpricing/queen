/**
 * Test script for the new Dashboard Status API endpoints
 * 
 * This script demonstrates all 5 new endpoints:
 * 1. GET /api/v1/status - Dashboard overview
 * 2. GET /api/v1/status/queues - List queues with stats
 * 3. GET /api/v1/status/queues/:queue - Queue detail
 * 4. GET /api/v1/status/queues/:queue/messages - Queue messages
 * 5. GET /api/v1/status/analytics - Advanced analytics
 */

import { createQueenClient } from '../src/client/queenClient.js';

const BASE_URL = process.env.QUEEN_URL || 'http://localhost:3000';

// Create client
const client = createQueenClient({ baseUrl: BASE_URL });

// Helper to make GET requests
async function get(path) {
  const response = await fetch(`${BASE_URL}${path}`, {
    method: 'GET',
    headers: {
      'Content-Type': 'application/json'
    }
  });
  
  if (!response.ok) {
    const error = await response.text();
    throw new Error(`HTTP ${response.status}: ${error}`);
  }
  
  return await response.json();
}

// Helper to format JSON output
function printJson(label, data) {
  console.log(`\n${'='.repeat(80)}`);
  console.log(`📊 ${label}`);
  console.log('='.repeat(80));
  console.log(JSON.stringify(data, null, 2));
}

async function runTests() {
  console.log('🚀 Testing Queen Dashboard Status API\n');
  
  try {
    // First, let's create some test data
    console.log('📝 Setting up test data...');
    
    // Configure a test queue
    await client.configure({
      queue: 'dashboard-test-queue',
      namespace: 'test-namespace',
      task: 'test-task',
      partitions: 3
    });
    
    // Push some test messages
    const messages = [];
    for (let i = 0; i < 20; i++) {
      messages.push({
        queue: 'dashboard-test-queue',
        partition: `partition-${i % 3}`,
        payload: {
          messageNumber: i,
          timestamp: new Date().toISOString(),
          data: `Test message ${i}`
        }
      });
    }
    
    await client.pushBatch(messages);
    console.log('✅ Created test queue with 20 messages\n');
    
    // Wait a bit for messages to be processed
    await new Promise(resolve => setTimeout(resolve, 1000));
    
    // Test 1: Dashboard Status
    console.log('🧪 Test 1: Dashboard Status (GET /api/v1/status)');
    const status = await get('/api/v1/status');
    printJson('Dashboard Status', {
      timeRange: status.timeRange,
      queuesCount: status.queues.length,
      messages: status.messages,
      leases: status.leases,
      throughputSamples: status.throughput.length,
      dlqTotal: status.deadLetterQueue.totalMessages
    });
    
    // Test 2: Queues List
    console.log('\n🧪 Test 2: List Queues (GET /api/v1/status/queues)');
    const queues = await get('/api/v1/status/queues?limit=10');
    printJson('Queues List', {
      total: queues.pagination.total,
      queues: queues.queues.map(q => ({
        name: q.name,
        namespace: q.namespace,
        partitions: q.partitions,
        pending: q.messages.pending,
        completed: q.messages.completed
      }))
    });
    
    // Test 3: Queue Detail
    console.log('\n🧪 Test 3: Queue Detail (GET /api/v1/status/queues/:queue)');
    const queueDetail = await get('/api/v1/status/queues/dashboard-test-queue');
    printJson('Queue Detail', {
      queue: queueDetail.queue,
      totals: queueDetail.totals,
      partitionCount: queueDetail.partitions.length,
      partitions: queueDetail.partitions.map(p => ({
        name: p.name,
        messages: p.messages,
        cursor: p.cursor
      }))
    });
    
    // Test 4: Queue Messages
    console.log('\n🧪 Test 4: Queue Messages (GET /api/v1/status/queues/:queue/messages)');
    const messages_result = await get('/api/v1/status/queues/dashboard-test-queue/messages?limit=5');
    printJson('Queue Messages', {
      total: messages_result.pagination.total,
      showing: messages_result.messages.length,
      messages: messages_result.messages.map(m => ({
        transactionId: m.transactionId,
        partition: m.partition,
        status: m.status,
        createdAt: m.createdAt
      }))
    });
    
    // Test 5: Filter by status
    console.log('\n🧪 Test 5: Filter Messages by Status (GET /api/v1/status/queues/:queue/messages?status=pending)');
    const pendingMessages = await get('/api/v1/status/queues/dashboard-test-queue/messages?status=pending&limit=5');
    printJson('Pending Messages', {
      total: pendingMessages.pagination.total,
      messages: pendingMessages.messages.map(m => ({
        transactionId: m.transactionId,
        status: m.status,
        age: m.age
      }))
    });
    
    // Test 6: Analytics (last hour with minute interval)
    console.log('\n🧪 Test 6: Analytics (GET /api/v1/status/analytics?interval=minute)');
    const analytics = await get('/api/v1/status/analytics?interval=minute');
    printJson('Analytics Overview', {
      timeRange: analytics.timeRange,
      throughput: {
        totals: analytics.throughput.totals,
        recentSamples: analytics.throughput.timeSeries.slice(0, 3)
      },
      latency: analytics.latency.overall,
      errorRates: analytics.errorRates.overall,
      currentDepth: analytics.queueDepths.current,
      topQueues: analytics.topQueues.slice(0, 3),
      dlqTotal: analytics.deadLetterQueue.total
    });
    
    // Test 7: Filter by namespace
    console.log('\n🧪 Test 7: Dashboard Status filtered by namespace (GET /api/v1/status?namespace=test-namespace)');
    const statusFiltered = await get('/api/v1/status?namespace=test-namespace');
    printJson('Filtered Status', {
      queuesCount: statusFiltered.queues.length,
      queues: statusFiltered.queues.map(q => q.name),
      messages: statusFiltered.messages
    });
    
    // Test 8: Analytics with time range
    console.log('\n🧪 Test 8: Analytics with custom time range (last 30 minutes)');
    const thirtyMinutesAgo = new Date(Date.now() - 30 * 60 * 1000).toISOString();
    const now = new Date().toISOString();
    const analyticsTimeRange = await get(`/api/v1/status/analytics?from=${thirtyMinutesAgo}&to=${now}&interval=minute`);
    printJson('Analytics (Last 30 min)', {
      timeRange: analyticsTimeRange.timeRange,
      throughputSamples: analyticsTimeRange.throughput.timeSeries.length,
      totals: analyticsTimeRange.throughput.totals
    });
    
    console.log('\n' + '='.repeat(80));
    console.log('✅ All tests completed successfully!');
    console.log('='.repeat(80));
    
  } catch (error) {
    console.error('\n❌ Test failed:', error.message);
    console.error(error.stack);
    process.exit(1);
  }
}

// Run tests
runTests().then(() => {
  console.log('\n✨ Dashboard API test complete!');
  process.exit(0);
}).catch(error => {
  console.error('Fatal error:', error);
  process.exit(1);
});

